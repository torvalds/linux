#	$OpenBSD: Makefile,v 1.25 2016/09/25 17:43:11 natano Exp $

MAN=	termcap.5
TIC=	/usr/bin/tic

all: terminfo termcap

terminfo: termtypes.master
	${TIC} -x -o ${.TARGET} ${.ALLSRC}
	@[ -d ${.TARGET} ] || exit 1

termcap: termtypes.master
	${TIC} -C -x ${.ALLSRC} > ${.TARGET}
	@[ -s ${.TARGET} ] || exit 1

realinstall:
	${INSTALL} -d -o root -g wheel ${DESTDIR}${BINDIR}/terminfo
	find terminfo -type f -exec \
	     ${INSTALL} -D ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	     {} ${DESTDIR}${BINDIR}/{} \;
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 termcap \
	     ${DESTDIR}${BINDIR}/misc/termcap
	ln -fs ${BINDIR}/misc/termcap ${DESTDIR}/etc/termcap
	chown -h root:wheel ${DESTDIR}/etc/termcap

clean:
	rm -f termcap
	rm -rf terminfo

.include <bsd.prog.mk>
