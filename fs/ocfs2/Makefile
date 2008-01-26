EXTRA_CFLAGS += -Ifs/ocfs2

EXTRA_CFLAGS += -DCATCH_BH_JBD_RACES

obj-$(CONFIG_OCFS2_FS) += ocfs2.o

ocfs2-objs := \
	alloc.o 		\
	aops.o 			\
	buffer_head_io.o	\
	dcache.o 		\
	dir.o 			\
	dlmglue.o 		\
	export.o 		\
	extent_map.o 		\
	file.o 			\
	heartbeat.o 		\
	inode.o 		\
	ioctl.o 		\
	journal.o 		\
	localalloc.o 		\
	locks.o			\
	mmap.o 			\
	namei.o 		\
	resize.o		\
	slot_map.o 		\
	suballoc.o 		\
	super.o 		\
	symlink.o 		\
	sysfile.o 		\
	uptodate.o		\
	ver.o

obj-$(CONFIG_OCFS2_FS) += cluster/
obj-$(CONFIG_OCFS2_FS) += dlm/
