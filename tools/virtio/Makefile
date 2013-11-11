all: test mod
test: virtio_test vringh_test
virtio_test: virtio_ring.o virtio_test.o
vringh_test: vringh_test.o vringh.o virtio_ring.o

CFLAGS += -g -O2 -Wall -I. -I ../../usr/include/ -Wno-pointer-sign -fno-strict-overflow -fno-strict-aliasing -fno-common -MMD -U_FORTIFY_SOURCE
vpath %.c ../../drivers/virtio ../../drivers/vhost
mod:
	${MAKE} -C `pwd`/../.. M=`pwd`/vhost_test
.PHONY: all test mod clean
clean:
	${RM} *.o vringh_test virtio_test vhost_test/*.o vhost_test/.*.cmd \
              vhost_test/Module.symvers vhost_test/modules.order *.d
-include *.d
