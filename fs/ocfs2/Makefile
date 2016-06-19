ccflags-y := -Ifs/ocfs2

ccflags-y += -DCATCH_BH_JBD_RACES

obj-$(CONFIG_OCFS2_FS) += 	\
	ocfs2.o			\
	ocfs2_stackglue.o

obj-$(CONFIG_OCFS2_FS_O2CB) += ocfs2_stack_o2cb.o
obj-$(CONFIG_OCFS2_FS_USERSPACE_CLUSTER) += ocfs2_stack_user.o

ocfs2-objs := \
	alloc.o 		\
	aops.o 			\
	blockcheck.o		\
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
	refcounttree.o		\
	reservations.o		\
	move_extents.o		\
	resize.o		\
	slot_map.o 		\
	suballoc.o 		\
	super.o 		\
	symlink.o 		\
	sysfile.o 		\
	uptodate.o		\
	quota_local.o		\
	quota_global.o		\
	xattr.o			\
	acl.o	\
	filecheck.o

ocfs2_stackglue-objs := stackglue.o
ocfs2_stack_o2cb-objs := stack_o2cb.o
ocfs2_stack_user-objs := stack_user.o

obj-$(CONFIG_OCFS2_FS) += dlmfs/
# cluster/ is always needed when OCFS2_FS for masklog support
obj-$(CONFIG_OCFS2_FS) += cluster/
obj-$(CONFIG_OCFS2_FS_O2CB) += dlm/
