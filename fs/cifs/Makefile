# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Linux CIFS/SMB2/SMB3 VFS client
#
ccflags-y += -I$(src)		# needed for trace events
obj-$(CONFIG_CIFS) += cifs.o

cifs-y := trace.o cifsfs.o cifssmb.o cifs_debug.o connect.o dir.o file.o \
	  inode.o link.o misc.o netmisc.o smbencrypt.o transport.o \
	  cifs_unicode.o nterr.o cifsencrypt.o \
	  readdir.o ioctl.o sess.o export.o smb1ops.o unc.o winucase.o \
	  smb2ops.o smb2maperror.o smb2transport.o \
	  smb2misc.o smb2pdu.o smb2inode.o smb2file.o cifsacl.o fs_context.o \
	  dns_resolve.o cifs_spnego_negtokeninit.asn1.o asn1.o

$(obj)/asn1.o: $(obj)/cifs_spnego_negtokeninit.asn1.h

$(obj)/cifs_spnego_negtokeninit.asn1.o: $(obj)/cifs_spnego_negtokeninit.asn1.c $(obj)/cifs_spnego_negtokeninit.asn1.h

cifs-$(CONFIG_CIFS_XATTR) += xattr.o

cifs-$(CONFIG_CIFS_UPCALL) += cifs_spnego.o

cifs-$(CONFIG_CIFS_DFS_UPCALL) += cifs_dfs_ref.o dfs_cache.o

cifs-$(CONFIG_CIFS_SWN_UPCALL) += netlink.o cifs_swn.o

cifs-$(CONFIG_CIFS_FSCACHE) += fscache.o

cifs-$(CONFIG_CIFS_SMB_DIRECT) += smbdirect.o

cifs-$(CONFIG_CIFS_ROOT) += cifsroot.o
