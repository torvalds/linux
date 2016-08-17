SUBDIRS = linux sys

COMMON_H = \
	$(top_srcdir)/include/zfeature_common.h \
	$(top_srcdir)/include/zfs_comutil.h \
	$(top_srcdir)/include/zfs_deleg.h \
	$(top_srcdir)/include/zfs_fletcher.h \
	$(top_srcdir)/include/zfs_namecheck.h \
	$(top_srcdir)/include/zfs_prop.h \
	$(top_srcdir)/include/zpios-ctl.h

KERNEL_H = \
	$(top_srcdir)/include/zpios-internal.h

USER_H = \
	$(top_srcdir)/include/libnvpair.h \
	$(top_srcdir)/include/libuutil_common.h \
	$(top_srcdir)/include/libuutil.h \
	$(top_srcdir)/include/libuutil_impl.h \
	$(top_srcdir)/include/libzfs.h \
	$(top_srcdir)/include/libzfs_core.h \
	$(top_srcdir)/include/libzfs_impl.h

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
