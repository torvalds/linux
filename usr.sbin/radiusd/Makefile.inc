#	$OpenBSD: Makefile.inc,v 1.4 2024/07/14 16:09:23 yasuoka Exp $

.PATH:		${.CURDIR}/..
CFLAGS+=	-I${.CURDIR}/..
CFLAGS+=	-Wall -Wextra -Wshadow -Wno-unused-parameter
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations -Wpointer-arith

#DEGUG=		-g
#CFLAGS+=	-DRADIUSD_DEBUG -DEAP2MSCHAP_DEBUG
#CFLAGS+=	-Werror
