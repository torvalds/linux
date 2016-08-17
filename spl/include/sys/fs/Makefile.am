COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/sys/fs/swapnode.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/sys/fs
kernel_HEADERS = $(KERNEL_H)
endif
