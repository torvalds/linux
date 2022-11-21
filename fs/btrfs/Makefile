# SPDX-License-Identifier: GPL-2.0

# Subset of W=1 warnings
subdir-ccflags-y += -Wextra -Wunused -Wno-unused-parameter
subdir-ccflags-y += -Wmissing-declarations
subdir-ccflags-y += -Wmissing-format-attribute
subdir-ccflags-y += -Wmissing-prototypes
subdir-ccflags-y += -Wold-style-definition
subdir-ccflags-y += -Wmissing-include-dirs
condflags := \
	$(call cc-option, -Wunused-but-set-variable)		\
	$(call cc-option, -Wunused-const-variable)		\
	$(call cc-option, -Wpacked-not-aligned)			\
	$(call cc-option, -Wstringop-truncation)
subdir-ccflags-y += $(condflags)
# The following turn off the warnings enabled by -Wextra
subdir-ccflags-y += -Wno-missing-field-initializers
subdir-ccflags-y += -Wno-sign-compare
subdir-ccflags-y += -Wno-type-limits
subdir-ccflags-y += -Wno-shift-negative-value

obj-$(CONFIG_BTRFS_FS) := btrfs.o

btrfs-y += super.o ctree.o extent-tree.o print-tree.o root-tree.o dir-item.o \
	   file-item.o inode-item.o disk-io.o \
	   transaction.o inode.o file.o tree-defrag.o \
	   extent_map.o sysfs.o struct-funcs.o xattr.o ordered-data.o \
	   extent_io.o volumes.o async-thread.o ioctl.o locking.o orphan.o \
	   export.o tree-log.o free-space-cache.o zlib.o lzo.o zstd.o \
	   compression.o delayed-ref.o relocation.o delayed-inode.o scrub.o \
	   backref.o ulist.o qgroup.o send.o dev-replace.o raid56.o \
	   uuid-tree.o props.o free-space-tree.o tree-checker.o space-info.o \
	   block-rsv.o delalloc-space.o block-group.o discard.o reflink.o \
	   subpage.o tree-mod-log.o

btrfs-$(CONFIG_BTRFS_FS_POSIX_ACL) += acl.o
btrfs-$(CONFIG_BTRFS_FS_CHECK_INTEGRITY) += check-integrity.o
btrfs-$(CONFIG_BTRFS_FS_REF_VERIFY) += ref-verify.o
btrfs-$(CONFIG_BLK_DEV_ZONED) += zoned.o
btrfs-$(CONFIG_FS_VERITY) += verity.o

btrfs-$(CONFIG_BTRFS_FS_RUN_SANITY_TESTS) += tests/free-space-tests.o \
	tests/extent-buffer-tests.o tests/btrfs-tests.o \
	tests/extent-io-tests.o tests/inode-tests.o tests/qgroup-tests.o \
	tests/free-space-tree-tests.o tests/extent-map-tests.o
