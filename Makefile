CC = gcc
CFLAGS = -g -Wall -Wshadow -Wno-unused-function `pkg-config --cflags gstreamer-1.0`
LDFLAGS = `(pkg-config --libs gstreamer-1.0)`

SRCS = $(wildcard *.c)

all:
	make clean
	make build

build:
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o h264-gdr

clean:
	$(RM) h264-gdr
