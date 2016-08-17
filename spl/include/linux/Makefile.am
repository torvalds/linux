COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/linux/bitops_compat.h \
	$(top_srcdir)/include/linux/compiler_compat.h \
	$(top_srcdir)/include/linux/delay_compat.h \
	$(top_srcdir)/include/linux/file_compat.h \
	$(top_srcdir)/include/linux/list_compat.h \
	$(top_srcdir)/include/linux/math64_compat.h \
	$(top_srcdir)/include/linux/mm_compat.h \
	$(top_srcdir)/include/linux/proc_compat.h \
	$(top_srcdir)/include/linux/rwsem_compat.h \
	$(top_srcdir)/include/linux/wait_compat.h \
	$(top_srcdir)/include/linux/zlib_compat.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/linux
kernel_HEADERS = $(KERNEL_H)
endif
