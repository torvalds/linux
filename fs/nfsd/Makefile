#
# Makefile for the Linux nfs server
#

obj-$(CONFIG_NFSD)	+= nfsd.o

nfsd-y 			:= nfssvc.o nfsctl.o nfsproc.o nfsfh.o vfs.o \
			   export.o auth.o lockd.o nfscache.o nfsxdr.o stats.o
nfsd-$(CONFIG_NFSD_V2_ACL) += nfs2acl.o
nfsd-$(CONFIG_NFSD_V3)	+= nfs3proc.o nfs3xdr.o
nfsd-$(CONFIG_NFSD_V3_ACL) += nfs3acl.o
nfsd-$(CONFIG_NFSD_V4)	+= nfs4proc.o nfs4xdr.o nfs4state.o nfs4idmap.o \
			   nfs4acl.o nfs4callback.o nfs4recover.o
nfsd-objs		:= $(nfsd-y)
