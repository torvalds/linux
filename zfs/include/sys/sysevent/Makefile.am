COMMON_H = \
	$(top_srcdir)/include/sys/sysevent/eventdefs.h \
	$(top_srcdir)/include/sys/sysevent/dev.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/sysevent
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/sysevent
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
