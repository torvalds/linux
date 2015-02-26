CC = gcc
CFLAGS = -Wall -g

all: iio_event_monitor lsiio generic_buffer

iio_event_monitor: iio_event_monitor.o iio_utils.o

lsiio: lsiio.o iio_utils.o

generic_buffer: generic_buffer.o iio_utils.o

%.o: %.c iio_utils.h

.PHONY: clean
clean:
	rm -f *.o iio_event_monitor lsiio generic_buffer
