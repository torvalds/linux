#	$OpenBSD: Makefile.inc,v 1.11 2017/09/05 03:16:14 schwarze Exp $

.PATH: ${LIBCSRCDIR}/time

SRCS+=	asctime.c difftime.c localtime.c strftime.c strftime_l.c \
	strptime.c wcsftime.c
MAN+=	ctime.3 strftime.3 time2posix.3 tzfile.5 tzset.3 strptime.3 wcsftime.3
