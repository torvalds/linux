# ex:ts=8
#
# Makefile for amd
# 	This file is under a "BSD" copyright (c) by David O'Brien 1998
#
# $FreeBSD$
#

.include <bsd.init.mk>

.PATH:	${SRCTOP}/contrib/amd/amd

CONFS=	amd.map
PROG=	amd
MAN=	amd.8
SRCS=	am_ops.c amd.c amfs_auto.c amfs_direct.c amfs_error.c amfs_generic.c
SRCS+=	amfs_host.c amfs_link.c amfs_linkx.c amfs_nfsl.c
SRCS+=	amfs_nfsx.c amfs_program.c amfs_root.c amfs_toplvl.c
SRCS+=	amfs_union.c amq_subr.c amq_svc.c autil.c clock.c conf.c
SRCS+=	get_args.c info_exec.c info_file.c info_ndbm.c info_passwd.c
SRCS+=	info_sun.c
SRCS+=	info_union.c map.c mapc.c mntfs.c nfs_prot_svc.c nfs_start.c
SRCS+=	nfs_subr.c ops_cdfs.c ops_lustre.c ops_mfs.c ops_nfs.c
SRCS+=	ops_nfs3.c ops_nfs4.c
SRCS+=	ops_nullfs.c ops_pcfs.c ops_tfs.c ops_tmpfs.c ops_udf.c ops_ufs.c
SRCS+=	ops_umapfs.c
SRCS+=	ops_unionfs.c opts.c readdir.c restart.c rpc_fwd.c sched.c
SRCS+=	srvr_amfs_auto.c srvr_nfs.c sun_map.c

CFLAGS+= -I${SRCTOP}/contrib/amd/amd \
	 -I${SRCTOP}/contrib/amd/include \
	 -I${OBJTOP}/include/rpcsvc

LIBADD=	amu

SRCS+=		conf_parse.c conf_parse.h conf_tok.c
SRCS+=		sun_map_parse.c sun_map_parse.h sun_map_tok.c
CLEANFILES+=	conf_parse.c conf_parse.h conf_tok.c
CLEANFILES+=	sun_map_parse.c sun_map_parse.h sun_map_tok.c

conf_tok.c:	conf_tok.l
	${LEX} ${LFLAGS} -Pconf_ -o${.TARGET} ${.ALLSRC}

.ORDER:	conf_parse.c conf_parse.h
conf_parse.h: .NOMETA
conf_parse.c conf_parse.h:	conf_parse.y
	${YACC} ${YFLAGS} -pconf_ -oconf_parse.c ${.ALLSRC}

sun_map_tok.c:	sun_map_tok.l
	${LEX} ${LFLAGS} -Psun_map_ -o${.TARGET} ${.ALLSRC}

.ORDER:	sun_map_parse.c sun_map_parse.h
sun_map_parse.h: .NOMETA
sun_map_parse.c sun_map_parse.h:	sun_map_parse.y
	${YACC} ${YFLAGS} -psun_map_ -osun_map_parse.c ${.ALLSRC}

conf_tok.o:	conf_parse.h

sun_map_tok.o: sun_map_parse.h

#		These are generated at compile time
SRCS+=		mount_xdr.c
CLEANFILES+=	mount_xdr.c

mount_xdr.c: ${MOUNT_X}
	${RPCCOM} -c -DWANT_NFS3 ${.ALLSRC} -o ${.TARGET}

.if ${MK_HESIOD} != "no"
SRCS+=	info_hesiod.c
CFLAGS+= -DHAVE_MAP_HESIOD
.endif

.if ${MK_NIS} != "no"
SRCS+=	info_nis.c
.endif

.if ${MK_TCP_WRAPPERS} != "no"
CFLAGS+=	-DHAVE_LIBWRAP -DHAVE_TCPD_H
LIBADD+=	wrap
.endif

.include <bsd.prog.mk>
