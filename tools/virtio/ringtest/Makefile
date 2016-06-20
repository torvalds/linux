all:

all: ring virtio_ring_0_9 virtio_ring_poll virtio_ring_inorder noring

CFLAGS += -Wall
CFLAGS += -pthread -O2 -ggdb
LDFLAGS += -pthread -O2 -ggdb

main.o: main.c main.h
ring.o: ring.c main.h
virtio_ring_0_9.o: virtio_ring_0_9.c main.h
virtio_ring_poll.o: virtio_ring_poll.c virtio_ring_0_9.c main.h
virtio_ring_inorder.o: virtio_ring_inorder.c virtio_ring_0_9.c main.h
ring: ring.o main.o
virtio_ring_0_9: virtio_ring_0_9.o main.o
virtio_ring_poll: virtio_ring_poll.o main.o
virtio_ring_inorder: virtio_ring_inorder.o main.o
noring: noring.o main.o
clean:
	-rm main.o
	-rm ring.o ring
	-rm virtio_ring_0_9.o virtio_ring_0_9
	-rm virtio_ring_poll.o virtio_ring_poll
	-rm virtio_ring_inorder.o virtio_ring_inorder
	-rm noring.o noring

.PHONY: all clean
