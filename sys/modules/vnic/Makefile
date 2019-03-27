# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

CFLAGS+=	-DFDT

SUBDIR = mrmlbus thunder_mdio thunder_bgx vnicpf vnicvf

.include <bsd.subdir.mk>
