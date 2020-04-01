# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux nfs filesystem routines.
#

obj-$(CONFIG_NFS_FS) += nfs.o

CFLAGS_nfstrace.o += -I$(src)
nfs-y 			:= client.o dir.o file.o getroot.o inode.o super.o \
			   io.o direct.o pagelist.o read.o symlink.o unlink.o \
			   write.o namespace.o mount_clnt.o nfstrace.o \
			   export.o sysfs.o fs_context.o
nfs-$(CONFIG_ROOT_NFS)	+= nfsroot.o
nfs-$(CONFIG_SYSCTL)	+= sysctl.o
nfs-$(CONFIG_NFS_FSCACHE) += fscache.o fscache-index.o

obj-$(CONFIG_NFS_V2) += nfsv2.o
nfsv2-y := nfs2super.o proc.o nfs2xdr.o

obj-$(CONFIG_NFS_V3) += nfsv3.o
nfsv3-y := nfs3super.o nfs3client.o nfs3proc.o nfs3xdr.o
nfsv3-$(CONFIG_NFS_V3_ACL) += nfs3acl.o

obj-$(CONFIG_NFS_V4) += nfsv4.o
CFLAGS_nfs4trace.o += -I$(src)
nfsv4-y := nfs4proc.o nfs4xdr.o nfs4state.o nfs4renewd.o nfs4super.o nfs4file.o \
	  delegation.o nfs4idmap.o callback.o callback_xdr.o callback_proc.o \
	  nfs4namespace.o nfs4getroot.o nfs4client.o nfs4session.o \
	  dns_resolve.o nfs4trace.o
nfsv4-$(CONFIG_NFS_USE_LEGACY_DNS) += cache_lib.o
nfsv4-$(CONFIG_SYSCTL)	+= nfs4sysctl.o
nfsv4-$(CONFIG_NFS_V4_1)	+= pnfs.o pnfs_dev.o pnfs_nfs.o
nfsv4-$(CONFIG_NFS_V4_2)	+= nfs42proc.o

obj-$(CONFIG_PNFS_FILE_LAYOUT) += filelayout/
obj-$(CONFIG_PNFS_BLOCK) += blocklayout/
obj-$(CONFIG_PNFS_FLEXFILE_LAYOUT) += flexfilelayout/
