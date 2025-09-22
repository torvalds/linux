#	$OpenBSD: Makefile,v 1.8 2015/01/21 09:47:15 guenther Exp $

INFO = amdref.info
SRCS = amdref.texinfo

all: ${INFO}

${INFO}: ${SRCS}
	-makeinfo --no-split ${.CURDIR}/${SRCS}

clean cleandir:
	-rm -f ${INFO}

install: ${INFO}
	 ${INSTALL} ${INSTALL_COPY} -o ${DOCOWN} -g ${DOCGRP} -m ${DOCMODE} ${INFO} ${DESTDIR}${SHAREDIR}/info
	 install-info --info-dir=${DESTDIR}${SHAREDIR}/info amdref.info

.include <bsd.own.mk>
.include <bsd.obj.mk>
.include <bsd.subdir.mk>
