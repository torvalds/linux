COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/linux/dcache_compat.h \
	$(top_srcdir)/include/linux/xattr_compat.h \
	$(top_srcdir)/include/linux/vfs_compat.h \
	$(top_srcdir)/include/linux/blkdev_compat.h \
	$(top_srcdir)/include/linux/utsname_compat.h \
	$(top_srcdir)/include/linux/kmap_compat.h \
	$(top_srcdir)/include/linux/simd_x86.h \
	$(top_srcdir)/include/linux/simd_aarch64.h \
	$(top_srcdir)/include/linux/mod_compat.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/linux
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/linux
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
