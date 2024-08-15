# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Linux kernel rpcsec_gss implementation
#

obj-$(CONFIG_SUNRPC_GSS) += auth_rpcgss.o

auth_rpcgss-y := auth_gss.o gss_generic_token.o \
	gss_mech_switch.o svcauth_gss.o \
	gss_rpc_upcall.o gss_rpc_xdr.o trace.o

obj-$(CONFIG_RPCSEC_GSS_KRB5) += rpcsec_gss_krb5.o

rpcsec_gss_krb5-y := gss_krb5_mech.o gss_krb5_seal.o gss_krb5_unseal.o \
	gss_krb5_seqnum.o gss_krb5_wrap.o gss_krb5_crypto.o gss_krb5_keys.o
