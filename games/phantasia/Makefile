#	$OpenBSD: Makefile,v 1.21 2024/02/08 20:30:54 miod Exp $

PROG=	phantasia
SRCS=	main.c fight.c io.c interplayer.c gamesupport.c misc.c phantglobs.c
CFLAGS+=-DTERMIOS
DPADD=	${LIBM} ${LIBCURSES}
LDADD=	-lm -lcurses
MAN=	phantasia.6
CLEANFILES+=map setup setup.d setup.o phantglobs.o.d phantglobs.o.bld

all: setup phantasia

setup.o: setup.c
	${HOSTCC} -c ${CFLAGS} -o ${.TARGET} ${.CURDIR}/setup.c

phantglobs.o.bld: phantglobs.c
	${HOSTCC} -c ${CFLAGS} -o ${.TARGET} ${.CURDIR}/phantglobs.c

setup: phantglobs.o.bld setup.o monsters.asc ${DPADD} 
	${HOSTCC} ${CFLAGS} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} \
		  phantglobs.o.bld setup.o ${LDADD}

beforeinstall: 
	./setup -m ${.CURDIR}/monsters.asc
	chown root:games ${DESTDIR}/var/games/phantasia/*

# Make Phantasia map.  Change the map commands reflect your installation.
# PLOTDEVICE is used for plotting the map.  Change as appropriate.

map: map.c
	${HOSTCC} -O ${.CURDIR}/map.c -lplot -o ${.TARGET}
	./map | plot > /dev/tty

.include <bsd.prog.mk>
