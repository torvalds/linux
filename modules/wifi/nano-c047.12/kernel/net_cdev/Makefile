# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSBULL_DEBUG -Wall # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS) -Wall -g

ifneq ($(KERNELRELEASE),)
# call from kernel build system

nano_net_cdev-objs := net.o cdev.o

obj-m   := nano_net_cdev.o


else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

default:
	echo KERNELDIR = $(KERNELDIR) 
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif



clean: 
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

depend .depend dep: 
	$(CC) $(CFLAGS) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
