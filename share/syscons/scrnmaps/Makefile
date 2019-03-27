# $FreeBSD$

SCRMAPS = armscii8-2haik8.scm \
	  iso-8859-1_to_cp437.scm iso-8859-4_for_vga9.scm \
	  iso-8859-7_to_cp437.scm \
	  koi8-r2cp866.scm koi8-u2cp866u.scm \
	  us-ascii_to_cp437.scm

SCRMAPS_MK = ${SCRMAPS:R:S/$/.mk/g}
CLEANFILES+= ${SCRMAPS_MK} ${SCRMAPS}

FILES=	${SCRMAPS}
FILESDIR= ${SHAREDIR}/syscons/scrnmaps

build-tools: ${SCRMAPS_MK}

${SCRMAPS}: ${.TARGET:R}.mk
	${BTOOLSPATH:U.}/${.TARGET:R}.mk ${.TARGET:R}.tmp
	uuencode ${.TARGET:R}.tmp ${.TARGET:R} > ${.TARGET}
	rm -f ${.TARGET:R}.tmp

${SCRMAPS_MK}: ${.TARGET:R} mkscrfil.c ${BUILD_TOOLS_META}
	${CC:N${CCACHE_BIN}} ${CFLAGS} -I${.CURDIR} -DFIL=\"${.TARGET:R}\" \
	    ${LDFLAGS} -o ${.TARGET} ${.CURDIR}/mkscrfil.c

.include <bsd.prog.mk>
