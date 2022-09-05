#include <assert.h>
#include <gst/gst.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

gchar black_frame[2048] = { 0 };

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }

    default:
      break;
  }

  return TRUE;
}

static void
push_key_frame (GstPad * pad, gint bytes_read)
{
  GstBuffer *black_frame_buffer;
  GstMapInfo map_info;
  gsize size;
  GstFlowReturn ret = GST_FLOW_OK;

  black_frame_buffer = gst_buffer_new_allocate (NULL, bytes_read, NULL);
  if (black_frame_buffer == NULL) {
    g_print ("Failed to allocate buffer for black frame\n");
    return;
  }

  gst_buffer_map (black_frame_buffer, &map_info, GST_MAP_WRITE);
  memcpy (map_info.data, black_frame, bytes_read);
  gst_buffer_unmap (black_frame_buffer, &map_info);

  size = gst_buffer_get_size (black_frame_buffer);

  ret = gst_pad_push (pad, black_frame_buffer);

  g_print ("Pushed buffer of size: %ld ret: %s active: %d\n", size,
      gst_flow_get_name (ret), gst_pad_is_active (pad));
}

static GstPadProbeReturn
pad_probe_event_cb (GstPad * pad, GstPadProbeInfo * info, gpointer userdata)
{
  gint bytes_read = *((gint *) userdata);
  GstEventType event_type;

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) != GST_EVENT_SEGMENT)
    return GST_PAD_PROBE_PASS;

  event_type = GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info));
  g_print ("Event: %s\n", gst_event_type_get_name (event_type));

  push_key_frame (pad, bytes_read);
  push_key_frame (pad, bytes_read);

  return GST_PAD_PROBE_REMOVE;
}

static int
read_black_frame_from_file ()
{
  gint fd = -1;
  gint bytes_read;

  fd = open ("black.h264", O_RDONLY);
  if (fd == -1) {
    g_print ("Failed to open black frame file\n");
    goto error;
  }

  bytes_read = read (fd, black_frame, sizeof (black_frame));
  if (bytes_read == 0 || bytes_read < 0) {
    g_print ("Failed to read black frame\n");
    close (fd);
    goto error;
  }

  close (fd);

  g_print ("Black frame buffer size: %d\n", bytes_read);

  return bytes_read;

error:
  return -1;
}

int
main (int argc, char *argv[])
{
  GstElement *filesrc = NULL;
  GstElement *parse = NULL;
  GstElement *vid_dec = NULL;
  GstElement *sink = NULL;
  GstElement *pipeline = NULL;
  GMainLoop *main_loop = NULL;
  GstBus *bus = NULL;
  GstStateChangeReturn ret;
  guint bus_watch_id;
  GstPad *pad;

  gint bytes_read;

  gst_init (NULL, NULL);

  filesrc = gst_element_factory_make ("filesrc", "video_file_src");
  if (!filesrc) {
    g_print ("Failed to create filesrc\n");
    goto error;
  }
  g_object_set (G_OBJECT (filesrc), "location", "main.h264", NULL);


  parse = gst_element_factory_make ("h264parse", "video_parser");
  if (!parse) {
    g_print ("Failed to create h264parse\n");
    goto error;
  }
  // vid_dec = gst_element_factory_make ("avdec_h264", "video_decoder");
  vid_dec = gst_element_factory_make ("v4l2h264dec", "video_decoder");
  if (!vid_dec) {
    g_print ("Failed to create video decoder\n");
    goto error;
  }

  sink = gst_element_factory_make ("fakesink", "video_sink");
  if (!sink) {
    g_print ("Failed to create video sink");
    goto error;
  }
  // g_object_set (G_OBJECT (sink), "dump", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, NULL);
  g_object_set (G_OBJECT (sink), "num-buffers", 2, NULL);

  pipeline = gst_pipeline_new ("h264-gdr-test");

  gst_bin_add_many (GST_BIN (pipeline), filesrc, parse, vid_dec, sink, NULL);

  if (!gst_element_link (filesrc, parse)) {
    g_print ("Could not link filesrc and video parser\n");
    goto error;
  }

  if (!gst_element_link (parse, vid_dec)) {
    g_print ("Could not link video parser and video decoder\n");
    goto error;
  }

  if (!gst_element_link (vid_dec, sink)) {
    g_print ("Could not link video decoder and video sink\n");
    goto error;
  }

  bytes_read = read_black_frame_from_file ();
  if (bytes_read < 0) {
    g_print ("Failed to read black frame from file\n");
    goto error;
  }

  main_loop = g_main_loop_new (NULL, FALSE);

  bus = gst_element_get_bus (pipeline);
  bus_watch_id = gst_bus_add_watch (bus, bus_call, main_loop);
  gst_object_unref (bus);

  pad = gst_element_get_static_pad (parse, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      pad_probe_event_cb, &bytes_read, NULL);
  gst_object_unref (pad);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to set pipeline to playing\n");
    goto error;
  }

  g_print ("Starting main loop\n");

  g_main_loop_run (main_loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_source_remove (bus_watch_id);
  g_main_loop_unref (main_loop);

  if (pipeline)
    gst_object_unref (pipeline);

  gst_deinit ();

  return 0;

error:
  if (pipeline)
    gst_object_unref (pipeline);
  if (filesrc)
    gst_object_unref (filesrc);
  if (parse)
    gst_object_unref (parse);
  if (vid_dec)
    gst_object_unref (vid_dec);
  if (sink)
    gst_object_unref (sink);

  gst_deinit ();

  return -1;
}
