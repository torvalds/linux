# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux nfs server
#

ccflags-y += -I$(src)			# needed for trace events

obj-$(CONFIG_NFSD)	+= nfsd.o

# this one should be compiled first, as the tracing macros can easily blow up
nfsd-y			+= trace.o

nfsd-y 			+= nfssvc.o nfsctl.o nfsfh.o vfs.o \
			   export.o auth.o lockd.o nfscache.o \
			   stats.o filecache.o nfs3proc.o nfs3xdr.o \
			   netlink.o
nfsd-$(CONFIG_NFSD_V2) += nfsproc.o nfsxdr.o
nfsd-$(CONFIG_NFSD_V2_ACL) += nfs2acl.o
nfsd-$(CONFIG_NFSD_V3_ACL) += nfs3acl.o
nfsd-$(CONFIG_NFSD_V4)	+= nfs4proc.o nfs4xdr.o nfs4state.o nfs4idmap.o \
			   nfs4acl.o nfs4callback.o nfs4recover.o nfs4xdr_gen.o
nfsd-$(CONFIG_NFSD_PNFS) += nfs4layouts.o
nfsd-$(CONFIG_NFSD_BLOCKLAYOUT) += blocklayout.o blocklayoutxdr.o
nfsd-$(CONFIG_NFSD_SCSILAYOUT) += blocklayout.o blocklayoutxdr.o
nfsd-$(CONFIG_NFSD_FLEXFILELAYOUT) += flexfilelayout.o flexfilelayoutxdr.o
nfsd-$(CONFIG_NFS_LOCALIO) += localio.o


.PHONY: xdrgen

xdrgen: ../../include/linux/sunrpc/xdrgen/nfs4_1.h nfs4xdr_gen.h nfs4xdr_gen.c

../../include/linux/sunrpc/xdrgen/nfs4_1.h: ../../Documentation/sunrpc/xdr/nfs4_1.x
	../../tools/net/sunrpc/xdrgen/xdrgen definitions $< > $@

nfs4xdr_gen.h: ../../Documentation/sunrpc/xdr/nfs4_1.x
	../../tools/net/sunrpc/xdrgen/xdrgen declarations $< > $@

nfs4xdr_gen.c: ../../Documentation/sunrpc/xdr/nfs4_1.x
	../../tools/net/sunrpc/xdrgen/xdrgen source $< > $@
