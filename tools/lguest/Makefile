# This creates the demonstration utility "lguest" which runs a Linux guest.
CFLAGS:=-m32 -Wall -Wmissing-declarations -Wmissing-prototypes -O3 -U_FORTIFY_SOURCE -Iinclude

all: lguest

include/linux/virtio_types.h: ../../include/uapi/linux/virtio_types.h
	mkdir -p include/linux 2>&1 || true
	ln -sf ../../../../include/uapi/linux/virtio_types.h $@

lguest: include/linux/virtio_types.h

clean:
	rm -f lguest
