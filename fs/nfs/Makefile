#
# Makefile for the Linux nfs filesystem routines.
#

obj-$(CONFIG_NFS_FS) += nfs.o

nfs-y 			:= client.o dir.o file.o getroot.o inode.o super.o nfs2xdr.o \
			   direct.o pagelist.o proc.o read.o symlink.o unlink.o \
			   write.o namespace.o mount_clnt.o
nfs-$(CONFIG_ROOT_NFS)	+= nfsroot.o
nfs-$(CONFIG_NFS_V3)	+= nfs3proc.o nfs3xdr.o
nfs-$(CONFIG_NFS_V3_ACL)	+= nfs3acl.o
nfs-$(CONFIG_NFS_V4)	+= nfs4proc.o nfs4xdr.o nfs4state.o nfs4renewd.o \
			   delegation.o idmap.o \
			   callback.o callback_xdr.o callback_proc.o \
			   nfs4namespace.o
nfs-$(CONFIG_SYSCTL) += sysctl.o
