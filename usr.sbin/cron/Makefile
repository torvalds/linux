#	$OpenBSD: Makefile,v 1.6 2015/11/12 21:12:05 millert Exp $

PROG=	cron
SRCS=	cron.c database.c user.c entry.c job.c do_command.c \
	misc.c env.c popen.c atrun.c
CFLAGS+=-I${.CURDIR}
MAN=	cron.8

.include <bsd.prog.mk>
