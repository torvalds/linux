COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/fs/fs_subr.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/fs
kernel_HEADERS = $(KERNEL_H)
endif
