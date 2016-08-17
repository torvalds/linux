COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/vm/anon.h \
	$(top_srcdir)/include/vm/pvn.h \
	$(top_srcdir)/include/vm/seg_kmem.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/vm
kernel_HEADERS = $(KERNEL_H)
endif
