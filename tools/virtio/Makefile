all: test mod
test: virtio_test
virtio_test: virtio_ring.o virtio_test.o
CFLAGS += -g -O2 -Wall -I. -I ../../usr/include/ -Wno-pointer-sign -fno-strict-overflow  -MMD
vpath %.c ../../drivers/virtio
mod:
	${MAKE} -C `pwd`/../.. M=`pwd`/vhost_test
.PHONY: all test mod clean
clean:
	${RM} *.o vhost_test/*.o vhost_test/.*.cmd \
              vhost_test/Module.symvers vhost_test/modules.order *.d
-include *.d
