SUBDIRS = fm fs

COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/sys/acl.h \
	$(top_srcdir)/include/sys/acl_impl.h \
	$(top_srcdir)/include/sys/atomic.h \
	$(top_srcdir)/include/sys/attr.h \
	$(top_srcdir)/include/sys/bitmap.h \
	$(top_srcdir)/include/sys/bootconf.h \
	$(top_srcdir)/include/sys/bootprops.h \
	$(top_srcdir)/include/sys/buf.h \
	$(top_srcdir)/include/sys/byteorder.h \
	$(top_srcdir)/include/sys/callb.h \
	$(top_srcdir)/include/sys/callo.h \
	$(top_srcdir)/include/sys/cmn_err.h \
	$(top_srcdir)/include/sys/compress.h \
	$(top_srcdir)/include/sys/condvar.h \
	$(top_srcdir)/include/sys/conf.h \
	$(top_srcdir)/include/sys/console.h \
	$(top_srcdir)/include/sys/cpupart.h \
	$(top_srcdir)/include/sys/cpuvar.h \
	$(top_srcdir)/include/sys/crc32.h \
	$(top_srcdir)/include/sys/cred.h \
	$(top_srcdir)/include/sys/ctype.h \
	$(top_srcdir)/include/sys/ddi.h \
	$(top_srcdir)/include/sys/debug.h \
	$(top_srcdir)/include/sys/dirent.h \
	$(top_srcdir)/include/sys/disp.h \
	$(top_srcdir)/include/sys/dkio.h \
	$(top_srcdir)/include/sys/dkioc_free_util.h \
	$(top_srcdir)/include/sys/dklabel.h \
	$(top_srcdir)/include/sys/dnlc.h \
	$(top_srcdir)/include/sys/dumphdr.h \
	$(top_srcdir)/include/sys/efi_partition.h \
	$(top_srcdir)/include/sys/errno.h \
	$(top_srcdir)/include/sys/extdirent.h \
	$(top_srcdir)/include/sys/fcntl.h \
	$(top_srcdir)/include/sys/file.h \
	$(top_srcdir)/include/sys/idmap.h \
	$(top_srcdir)/include/sys/int_limits.h \
	$(top_srcdir)/include/sys/int_types.h \
	$(top_srcdir)/include/sys/inttypes.h \
	$(top_srcdir)/include/sys/isa_defs.h \
	$(top_srcdir)/include/sys/kidmap.h \
	$(top_srcdir)/include/sys/kmem.h \
	$(top_srcdir)/include/sys/kmem_cache.h \
	$(top_srcdir)/include/sys/kobj.h \
	$(top_srcdir)/include/sys/kstat.h \
	$(top_srcdir)/include/sys/list.h \
	$(top_srcdir)/include/sys/mkdev.h \
	$(top_srcdir)/include/sys/mntent.h \
	$(top_srcdir)/include/sys/modctl.h \
	$(top_srcdir)/include/sys/mode.h \
	$(top_srcdir)/include/sys/mount.h \
	$(top_srcdir)/include/sys/mutex.h \
	$(top_srcdir)/include/sys/note.h \
	$(top_srcdir)/include/sys/open.h \
	$(top_srcdir)/include/sys/param.h \
	$(top_srcdir)/include/sys/pathname.h \
	$(top_srcdir)/include/sys/policy.h \
	$(top_srcdir)/include/sys/pool.h \
	$(top_srcdir)/include/sys/priv_impl.h \
	$(top_srcdir)/include/sys/processor.h \
	$(top_srcdir)/include/sys/proc.h \
	$(top_srcdir)/include/sys/pset.h \
	$(top_srcdir)/include/sys/random.h \
	$(top_srcdir)/include/sys/refstr.h \
	$(top_srcdir)/include/sys/resource.h \
	$(top_srcdir)/include/sys/rwlock.h \
	$(top_srcdir)/include/sys/sdt.h \
	$(top_srcdir)/include/sys/sid.h \
	$(top_srcdir)/include/sys/signal.h \
	$(top_srcdir)/include/sys/stat.h \
	$(top_srcdir)/include/sys/stropts.h \
	$(top_srcdir)/include/sys/sunddi.h \
	$(top_srcdir)/include/sys/sunldi.h \
	$(top_srcdir)/include/sys/sysdc.h \
	$(top_srcdir)/include/sys/sysmacros.h \
	$(top_srcdir)/include/sys/systeminfo.h \
	$(top_srcdir)/include/sys/systm.h \
	$(top_srcdir)/include/sys/taskq.h \
	$(top_srcdir)/include/sys/thread.h \
	$(top_srcdir)/include/sys/time.h \
	$(top_srcdir)/include/sys/timer.h \
	$(top_srcdir)/include/sys/t_lock.h \
	$(top_srcdir)/include/sys/tsd.h \
	$(top_srcdir)/include/sys/types32.h \
	$(top_srcdir)/include/sys/types.h \
	$(top_srcdir)/include/sys/u8_textprep.h \
	$(top_srcdir)/include/sys/uio.h \
	$(top_srcdir)/include/sys/unistd.h \
	$(top_srcdir)/include/sys/user.h \
	$(top_srcdir)/include/sys/va_list.h \
	$(top_srcdir)/include/sys/varargs.h \
	$(top_srcdir)/include/sys/vfs.h \
	$(top_srcdir)/include/sys/vfs_opreg.h \
	$(top_srcdir)/include/sys/vmem.h \
	$(top_srcdir)/include/sys/vmsystm.h \
	$(top_srcdir)/include/sys/vnode.h \
	$(top_srcdir)/include/sys/zmod.h \
	$(top_srcdir)/include/sys/zone.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include/sys
kernel_HEADERS = $(KERNEL_H)
endif

