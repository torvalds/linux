#	$OpenBSD: Makefile.inc,v 1.5 2016/03/30 06:38:42 jmc Exp $

.PATH:	${LIBCSRCDIR}/termios

SRCS+=	cfgetispeed.c cfgetospeed.c cfmakeraw.c cfsetispeed.c cfsetospeed.c \
	cfsetspeed.c tcdrain.c tcflow.c tcflush.c tcgetattr.c tcgetpgrp.c \
	tcsendbreak.c tcsetattr.c tcsetpgrp.c tcgetsid.c

MAN+=	tcgetpgrp.3 tcsendbreak.3 tcsetattr.3 tcsetpgrp.3 tcgetsid.3
