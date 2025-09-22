#	$OpenBSD: Makefile,v 1.15 2008/10/22 20:31:20 jmc Exp $

MAN=	Makefile.yp.8 ypinit.8

DISTFILES= Makefile.main Makefile.yp
SHFILES= ypinit.sh

realinstall:
.for script in ${SHFILES}
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/${script} ${DESTDIR}${BINDIR}/${script:R}
.endfor
.for script in ${DISTFILES}
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${NONBINMODE} \
	    ${.CURDIR}/${script} ${DESTDIR}/var/yp/${script}.dist
.endfor
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/README ${DESTDIR}/var/yp/README

distribution:
.for script in ${DISTFILES}
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${NONBINMODE} \
	    ${.CURDIR}/${script} ${DESTDIR}/var/yp/${script}
.endfor

.include <bsd.prog.mk>
