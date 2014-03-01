
obj-$(CONFIG_BTRFS_FS) := btrfs.o

btrfs-y += super.o ctree.o extent-tree.o print-tree.o root-tree.o dir-item.o \
	   file-item.o inode-item.o inode-map.o disk-io.o \
	   transaction.o inode.o file.o tree-defrag.o \
	   extent_map.o sysfs.o struct-funcs.o xattr.o ordered-data.o \
	   extent_io.o volumes.o async-thread.o ioctl.o locking.o orphan.o \
	   export.o tree-log.o free-space-cache.o zlib.o lzo.o \
	   compression.o delayed-ref.o relocation.o delayed-inode.o scrub.o \
	   reada.o backref.o ulist.o qgroup.o send.o dev-replace.o raid56.o \
	   uuid-tree.o props.o hash.o

btrfs-$(CONFIG_BTRFS_FS_POSIX_ACL) += acl.o
btrfs-$(CONFIG_BTRFS_FS_CHECK_INTEGRITY) += check-integrity.o

btrfs-$(CONFIG_BTRFS_FS_RUN_SANITY_TESTS) += tests/free-space-tests.o \
	tests/extent-buffer-tests.o tests/btrfs-tests.o \
	tests/extent-io-tests.o tests/inode-tests.o
