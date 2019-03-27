#	from: @(#)Makefile	2.3 88/08/11 4.0 RPCSRC
#	$FreeBSD$

.SUFFIXES: .x

RPCCOM=	RPCGEN_CPP=${CPP:Q} rpcgen -C

HDRS=	rpcb_prot.h

XFILES= rpcb_prot.x

HFILES=	auth.h auth_unix.h clnt.h clnt_soc.h clnt_stat.h \
	nettype.h pmap_clnt.h pmap_prot.h pmap_rmt.h raw.h \
	rpc.h rpc_msg.h rpcb_clnt.h rpcent.h rpc_com.h rpcsec_gss.h \
	svc.h svc_auth.h svc_soc.h svc_dg.h xdr.h

# Secure RPC
HFILES+= auth_des.h des.h des_crypt.h

# Kerberos
HFILES+= auth_kerb.h

CLEANFILES+= ${HDRS}

INCS=	${HFILES} ${XFILES} ${HDRS}
INCSDIR=${INCLUDEDIR}/rpc

.x.h:
	${RPCCOM} -h -DWANT_NFS3 ${.IMPSRC} -o ${.TARGET}

.include <bsd.prog.mk>
