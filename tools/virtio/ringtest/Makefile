# SPDX-License-Identifier: GPL-2.0
all:

all: ring virtio_ring_0_9 virtio_ring_poll virtio_ring_iyesrder ptr_ring yesring

CFLAGS += -Wall
CFLAGS += -pthread -O2 -ggdb -flto -fwhole-program
LDFLAGS += -pthread -O2 -ggdb -flto -fwhole-program

main.o: main.c main.h
ring.o: ring.c main.h
ptr_ring.o: ptr_ring.c main.h ../../../include/linux/ptr_ring.h
virtio_ring_0_9.o: virtio_ring_0_9.c main.h
virtio_ring_poll.o: virtio_ring_poll.c virtio_ring_0_9.c main.h
virtio_ring_iyesrder.o: virtio_ring_iyesrder.c virtio_ring_0_9.c main.h
ring: ring.o main.o
virtio_ring_0_9: virtio_ring_0_9.o main.o
virtio_ring_poll: virtio_ring_poll.o main.o
virtio_ring_iyesrder: virtio_ring_iyesrder.o main.o
ptr_ring: ptr_ring.o main.o
yesring: yesring.o main.o
clean:
	-rm main.o
	-rm ring.o ring
	-rm virtio_ring_0_9.o virtio_ring_0_9
	-rm virtio_ring_poll.o virtio_ring_poll
	-rm virtio_ring_iyesrder.o virtio_ring_iyesrder
	-rm ptr_ring.o ptr_ring
	-rm yesring.o yesring

.PHONY: all clean
