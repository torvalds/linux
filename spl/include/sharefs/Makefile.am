COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/sharefs/share.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/sharefs
kernel_HEADERS = $(KERNEL_H)
endif
