#	@(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD$

# reorder gives an editor command for most common terminals
# (in reverse order from n'th to 1'st most commonly used)
# to move them to the front of termcap
#
MAN=	termcap.5

FILES=		termcap termcap.db
FILESDIR=	${BINDIR}/misc
CLEANFILES+=	termcap.db

.include <bsd.endian.mk>

termcap.db: termcap
	cap_mkdb ${CAP_MKDB_ENDIAN} -f ${.TARGET:R} ${.ALLSRC}

etc-termcap:
	${INSTALL_SYMLINK} ${BINDIR}/misc/termcap ${DESTDIR}/etc/termcap

.include <bsd.prog.mk>
