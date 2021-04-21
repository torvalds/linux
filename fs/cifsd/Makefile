# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile for Linux SMB3 kernel server
#
obj-$(CONFIG_SMB_SERVER) += ksmbd.o

$(obj)/spnego_negtokeninit.asn1.o: $(obj)/spnego_negtokeninit.asn1.c $(obj)/spnego_negtokeninit.asn1.h
$(obj)/spnego_negtokentarg.asn1.o: $(obj)/spnego_negtokentarg.asn1.c $(obj)/spnego_negtokentarg.asn1.h

ksmbd-y :=	unicode.o auth.o vfs.o vfs_cache.o server.o buffer_pool.o \
		misc.o oplock.o connection.o ksmbd_work.o crypto_ctx.o \
		mgmt/ksmbd_ida.o mgmt/user_config.o mgmt/share_config.o \
		mgmt/tree_connect.o mgmt/user_session.o smb_common.o \
		transport_tcp.o transport_ipc.o smbacl.o smb2pdu.o \
		smb2ops.o smb2misc.o spnego_negtokeninit.asn1.o \
		spnego_negtokentarg.asn1.o asn1.o ndr.o
ksmbd-$(CONFIG_SMB_SERVER_SMBDIRECT) += transport_rdma.o
