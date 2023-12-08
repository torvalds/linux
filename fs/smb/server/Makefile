# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile for Linux SMB3 kernel server
#
obj-$(CONFIG_SMB_SERVER) += ksmbd.o

ksmbd-y :=	unicode.o auth.o vfs.o vfs_cache.o server.o ndr.o \
		misc.o oplock.o connection.o ksmbd_work.o crypto_ctx.o \
		mgmt/ksmbd_ida.o mgmt/user_config.o mgmt/share_config.o \
		mgmt/tree_connect.o mgmt/user_session.o smb_common.o \
		transport_tcp.o transport_ipc.o smbacl.o smb2pdu.o \
		smb2ops.o smb2misc.o ksmbd_spnego_negtokeninit.asn1.o \
		ksmbd_spnego_negtokentarg.asn1.o asn1.o

$(obj)/asn1.o: $(obj)/ksmbd_spnego_negtokeninit.asn1.h $(obj)/ksmbd_spnego_negtokentarg.asn1.h

$(obj)/ksmbd_spnego_negtokeninit.asn1.o: $(obj)/ksmbd_spnego_negtokeninit.asn1.c $(obj)/ksmbd_spnego_negtokeninit.asn1.h
$(obj)/ksmbd_spnego_negtokentarg.asn1.o: $(obj)/ksmbd_spnego_negtokentarg.asn1.c $(obj)/ksmbd_spnego_negtokentarg.asn1.h

ksmbd-$(CONFIG_SMB_SERVER_SMBDIRECT) += transport_rdma.o
