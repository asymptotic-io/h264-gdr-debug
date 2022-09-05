#!/bin/sh
gst-launch-1.0 videotestsrc pattern=black num-buffers=1 ! video/x-raw,format=I420,width=1920,height=1440 ! x264enc ! h264parse ! video/x-h264,stream-format=avc ! filesink location=black.h264 -v
