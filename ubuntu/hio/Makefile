ifneq ($(KERNELRELEASE),)
#	hio-y := hio_main.o
#	obj-$(CONFIG_HIO_DRIVER) += hio.o
	obj-m += hio.o
else

KVER=
ifeq ($(KVER),)
  KVER=$(shell uname -r)
endif

PDIR=
ifeq ($(PDIR),)
  PDIR=extra
endif

PREFIX=

MODDIR=/lib/modules/$(KVER)/$(PDIR)/hio/
MOD=hio.ko

ifeq ($(KVER),2.6.32-300.3.1.el6uek.x86_64)
  FLAGS += -DSSD_QUEUE_PBIO
endif
ifeq ($(KVER),2.6.32-220.el6.x86_64)
  FLAGS += -DSSD_QUEUE_PBIO
endif
ifeq ($(KVER),2.6.32-358.el6.x86_64)
  FLAGS += -DSSD_QUEUE_PBIO
endif
ifeq ($(KVER),2.6.32-358.23.2.el6.x86_64)
  FLAGS += -DSSD_QUEUE_PBIO
endif
ifeq ($(KVER),3.0.58-0.6.6-xen)
  FLAGS += -DSSD_QUEUE_PBIO
endif
ifeq ($(KVER),3.2.0-4-amd64)
  FLAGS += -DSSD_BIOVEC_PHYS_MERGEABLE_FIXED
endif
ifeq ($(KVER),2.6.39-400.209.1.el5uek) #Oracle Linux Server release 5.10
  FLAGS += -DSSD_BIOVEC_PHYS_MERGEABLE_FIXED
endif
ifeq ($(KVER),2.6.39-400.215.10.el5uek) #Oracle Linux Server release 5.11
  FLAGS += -DSSD_BIOVEC_PHYS_MERGEABLE_FIXED
endif
ifeq ($(KVER),2.6.39-200.24.1.el6uek.x86_64) #Oracle Linux Server release 6.3
  FLAGS += -DSSD_BIOVEC_PHYS_MERGEABLE_FIXED
endif
ifeq ($(KVER),2.6.39-400.17.1.el6uek.x86_64) #Oracle Linux Server release 6.4
  FLAGS += -DSSD_BIOVEC_PHYS_MERGEABLE_FIXED
endif


KERNELDIR ?= /lib/modules/$(KVER)/build
PWD       := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) EXTRA_CFLAGS="$(FLAGS)" modules
clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.*

install: default
	mkdir -p $(PREFIX)/$(MODDIR)
	install -m 444 $(MOD) $(PREFIX)/$(MODDIR)
	@if [ "$(PREFIX)" = "" ]; then /sbin/depmod -a ;\
	else echo " *** Run 'depmod -a' to update the module database.";\
	fi
endif

