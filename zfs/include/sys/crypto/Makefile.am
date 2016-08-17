COMMON_H = \
	$(top_srcdir)/include/sys/crypto/api.h \
	$(top_srcdir)/include/sys/crypto/common.h \
	$(top_srcdir)/include/sys/crypto/icp.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/crypto
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/crypto
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
