# $FreeBSD$

.include <src.opts.mk>

.PATH: ${SRCTOP}/usr.bin/lastcomm

PROG=	sa
MAN=	sa.8
SRCS=	main.c db.c pdb.c usrdb.c readrec.c

PACKAGE=acct

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
