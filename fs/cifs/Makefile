#
# Makefile for Linux CIFS VFS client 
#
obj-$(CONFIG_CIFS) += cifs.o

cifs-y := cifsfs.o cifssmb.o cifs_debug.o connect.o dir.o file.o inode.o \
	  link.o misc.o netmisc.o smbencrypt.o transport.o asn1.o \
	  cifs_unicode.o nterr.o xattr.o cifsencrypt.o \
	  readdir.o ioctl.o sess.o export.o smb1ops.o

cifs-$(CONFIG_CIFS_ACL) += cifsacl.o

cifs-$(CONFIG_CIFS_UPCALL) += cifs_spnego.o

cifs-$(CONFIG_CIFS_DFS_UPCALL) += dns_resolve.o cifs_dfs_ref.o

cifs-$(CONFIG_CIFS_FSCACHE) += fscache.o cache.o

cifs-$(CONFIG_CIFS_SMB2) += smb2ops.o smb2maperror.o
