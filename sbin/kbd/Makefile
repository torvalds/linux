#	$OpenBSD: Makefile,v 1.41 2016/09/26 21:19:02 kettenis Exp $

#
# Be sure to change src/distrib/special/kbd/Makefile as well if you add or
# remove architectures on this list!
#
.if ${MACHINE} != "octeon"
    
PROG=	kbd

SRCS=	main.c kbd_wscons.c

.else
NOPROG=	Yes
.endif

MAN=		kbd.8

.include <bsd.prog.mk>
