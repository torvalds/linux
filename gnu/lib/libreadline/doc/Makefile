#	$OpenBSD: Makefile,v 1.3 2003/04/28 19:02:54 miod Exp $

.include <bsd.own.mk>

INFO=	readline.info history.info
RLSRC = rlman.texinfo rluser.texinfo rltech.texinfo manvers.texinfo \
	rluserman.texinfo
HISTSRC = hist.texinfo hsuser.texinfo hstech.texinfo manvers.texinfo
MAN+=	readline.3

all: ${INFO}

readline.info: ${RLSRC}
	-makeinfo --no-split -I ${.CURDIR} -o ${.TARGET} $(.CURDIR)/rlman.texinfo

history.info: ${HISTSRC}
	-makeinfo --no-split -I ${.CURDIR} -o ${.TARGET} ${.CURDIR}/hist.texinfo

.ifdef NOMAN
maninstall:
	@echo NOMAN is set
.endif

install: ${INFO} maninstall
	@-for i in ${INFO}; do \
		echo ${INSTALL} ${INSTALL_COPY} -o ${DOCOWN} -g ${DOCGRP} \
		    -m ${DOCMODE} $$i ${DESTDIR}${SHAREDIR}/info/$$i; \
		${INSTALL} ${INSTALL_COPY} -o ${DOCOWN} -g ${DOCGRP} \
		    -m ${DOCMODE} $$i ${DESTDIR}${SHAREDIR}/info/$$i; \
	done
	install-info --info-dir=${DESTDIR}${SHAREDIR}/info readline.info

cleandir:
	-rm -f ${INFO}

.if !defined(NOMAN)
.include <bsd.man.mk>
.endif
.include <bsd.obj.mk>
.include <bsd.subdir.mk>
.include <bsd.sys.mk>
