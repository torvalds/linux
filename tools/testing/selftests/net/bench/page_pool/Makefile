BENCH_PAGE_POOL_SIMPLE_TEST_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KDIR ?= /lib/modules/$(shell uname -r)/build

ifeq ($(V),1)
Q =
else
Q = @
endif

obj-m	+= bench_page_pool.o
bench_page_pool-y += bench_page_pool_simple.o time_bench.o

all:
	+$(Q)make -C $(KDIR) M=$(BENCH_PAGE_POOL_SIMPLE_TEST_DIR) modules

clean:
	+$(Q)make -C $(KDIR) M=$(BENCH_PAGE_POOL_SIMPLE_TEST_DIR) clean
