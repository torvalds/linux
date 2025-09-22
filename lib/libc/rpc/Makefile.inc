#	$OpenBSD: Makefile.inc,v 1.19 2024/01/22 16:18:06 deraadt Exp $

# librpc sources
.PATH:	${LIBCSRCDIR}/arch/${MACHINE}/rpc ${LIBCSRCDIR}/rpc

SRCS+=	auth_none.c auth_unix.c  authunix_prot.c bindresvport.c \
	clnt_generic.c clnt_perror.c clnt_raw.c clnt_simple.c clnt_tcp.c \
	clnt_udp.c clnt_udp_bufcreate.c get_myaddress.c getrpcent.c getrpcport.c \
	pmap_clnt.c pmap_getmaps.c pmap_getport.c pmap_prot.c \
	pmap_prot2.c pmap_rmt.c rpc_prot.c rpc_commondata.c rpc_callmsg.c \
	svc.c svc_auth.c svc_auth_unix.c svc_raw.c svc_run.c svc_simple.c \
	svc_tcp.c svc_udp.c xdr.c xdr_array.c xdr_float.c xdr_mem.c \
	xdr_rec.c xdr_reference.c xdr_stdio.c

MAN+=	authnone_create.3 bindresvport.3 getrpcent.3 getrpcport.3 rpc.3 xdr.3
