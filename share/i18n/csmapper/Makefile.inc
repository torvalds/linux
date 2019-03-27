# $FreeBSD$

.include <bsd.own.mk>

CSMAPPERDIR?= /usr/share/i18n/csmapper
.if ${MK_STAGING} == "yes"
MKCSMAPPER= ${STAGE_HOST_OBJTOP}/usr/bin/mkcsmapper_static
.endif
MKCSMAPPER?= ${.OBJDIR:H:H:H}/usr.bin/mkcsmapper_static/mkcsmapper_static
