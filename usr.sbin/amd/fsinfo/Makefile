# ex:ts=8
#
# Makefile for amd
# 	This file is under a "BSD" copyright (c) by David O'Brien 1998
#
# $FreeBSD$

.PATH:	${SRCTOP}/contrib/amd/fsinfo

PROG=	fsinfo
MAN=	fsinfo.8
SRCS=	fsi_analyze.c fsi_dict.c fsi_util.c fsinfo.c
SRCS+=	wr_atab.c wr_bparam.c wr_dumpset.c wr_exportfs.c wr_fstab.c

CFLAGS+= -I${SRCTOP}/contrib/amd/fsinfo \
	 -I${SRCTOP}/contrib/amd/inculde

LIBADD=	amu

SRCS+=		fsi_gram.c fsi_gram.h fsi_lex.c
SRCS+=		null_gram.c null_gram.h null_lex.c
CLEANFILES+=	fsi_gram.c fsi_gram.h fsi_lex.c
CLEANFILES+=	null_gram.c null_gram.h null_lex.c

fsi_lex.c:	fsi_lex.l
	${LEX} ${LFLAGS} -Pfsi_ -o${.TARGET} ${.ALLSRC}

.ORDER:	fsi_gram.c fsi_gram.h
fsi_gram.h: .NOMETA
fsi_gram.c fsi_gram.h:	fsi_gram.y
	${YACC} ${YFLAGS} -pfsi_ -ofsi_gram.c ${.ALLSRC}

null_lex.c:	null_lex.l
	${LEX} ${LFLAGS} -Pnull_ -o${.TARGET} ${.ALLSRC}

.ORDER:	null_gram.c null_gram.h
null_gram.h: .NOMETA
null_gram.c null_gram.h:	null_gram.y
	${YACC} ${YFLAGS} -pnull_ -onull_gram.c ${.ALLSRC}

fsi_lex.o: fsi_gram.h

null_gram.o: null_gram.h

.include <bsd.prog.mk>
