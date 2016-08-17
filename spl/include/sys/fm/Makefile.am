COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/sys/fm/protocol.h \
	$(top_srcdir)/include/sys/fm/util.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/sys/fm
kernel_HEADERS = $(KERNEL_H)
endif
