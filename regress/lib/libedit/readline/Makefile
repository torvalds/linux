#	$OpenBSD: Makefile,v 1.2 2016/09/06 10:33:13 schwarze Exp $

# To test /usr/src/gnu/lib/libreadline, run "make READLINE=1".
# To test /usr/src/lib/libedit/readline, run just "make".

PROG =		history

.ifdef READLINE
CFLAGS +=	-DREADLINE
LDADD +=	-lcurses -lreadline
.else
CFLAGS +=	-I${BSDSRCDIR}/lib/libedit
LDADD +=	-lcurses -ledit
.endif

.include <bsd.regress.mk>
