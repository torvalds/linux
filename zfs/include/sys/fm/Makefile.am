SUBDIRS = fs

COMMON_H = \
	$(top_srcdir)/include/sys/fm/protocol.h \
	$(top_srcdir)/include/sys/fm/util.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/fm
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/fm
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
