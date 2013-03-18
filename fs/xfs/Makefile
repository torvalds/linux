#
# Copyright (c) 2000-2005 Silicon Graphics, Inc.
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write the Free Software Foundation,
# Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#

ccflags-y += -I$(src)			# needed for trace events

ccflags-$(CONFIG_XFS_DEBUG) += -g

obj-$(CONFIG_XFS_FS)		+= xfs.o

# this one should be compiled first, as the tracing macros can easily blow up
xfs-y				+= xfs_trace.o

# highlevel code
xfs-y				+= xfs_aops.o \
				   xfs_bit.o \
				   xfs_buf.o \
				   xfs_dfrag.o \
				   xfs_discard.o \
				   xfs_error.o \
				   xfs_export.o \
				   xfs_extent_busy.o \
				   xfs_file.o \
				   xfs_filestream.o \
				   xfs_fsops.o \
				   xfs_globals.o \
				   xfs_icache.o \
				   xfs_ioctl.o \
				   xfs_iomap.o \
				   xfs_iops.o \
				   xfs_itable.o \
				   xfs_message.o \
				   xfs_mru_cache.o \
				   xfs_super.o \
				   xfs_xattr.o \
				   xfs_rename.o \
				   xfs_utils.o \
				   xfs_vnodeops.o \
				   kmem.o \
				   uuid.o

# code shared with libxfs
xfs-y				+= xfs_alloc.o \
				   xfs_alloc_btree.o \
				   xfs_attr.o \
				   xfs_attr_leaf.o \
				   xfs_bmap.o \
				   xfs_bmap_btree.o \
				   xfs_btree.o \
				   xfs_da_btree.o \
				   xfs_dir2.o \
				   xfs_dir2_block.o \
				   xfs_dir2_data.o \
				   xfs_dir2_leaf.o \
				   xfs_dir2_node.o \
				   xfs_dir2_sf.o \
				   xfs_ialloc.o \
				   xfs_ialloc_btree.o \
				   xfs_inode.o \
				   xfs_log_recover.o \
				   xfs_mount.o \
				   xfs_trans.o

# low-level transaction/log code
xfs-y				+= xfs_log.o \
				   xfs_log_cil.o \
				   xfs_buf_item.o \
				   xfs_extfree_item.o \
				   xfs_inode_item.o \
				   xfs_trans_ail.o \
				   xfs_trans_buf.o \
				   xfs_trans_extfree.o \
				   xfs_trans_inode.o \

# optional features
xfs-$(CONFIG_XFS_QUOTA)		+= xfs_dquot.o \
				   xfs_dquot_item.o \
				   xfs_trans_dquot.o \
				   xfs_qm_syscalls.o \
				   xfs_qm_bhv.o \
				   xfs_qm.o \
				   xfs_quotaops.o
xfs-$(CONFIG_XFS_RT)		+= xfs_rtalloc.o
xfs-$(CONFIG_XFS_POSIX_ACL)	+= xfs_acl.o
xfs-$(CONFIG_PROC_FS)		+= xfs_stats.o
xfs-$(CONFIG_SYSCTL)		+= xfs_sysctl.o
xfs-$(CONFIG_COMPAT)		+= xfs_ioctl32.o
