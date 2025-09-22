#	$OpenBSD: Makefile,v 1.5 2024/02/29 21:47:02 bluhm Exp $

PROG=	nanosleep
SRCS=	nanosleep.c
WARNINGS=	yes

trivial: nanosleep
	./nanosleep -t

with_signal: nanosleep
	./nanosleep -s

time_elapsed: nanosleep
	./nanosleep -e

time_elapsed_with_signal: nanosleep
	./nanosleep -E

short_time: nanosleep
	./nanosleep -S

invalid_time: nanosleep
	./nanosleep -i

REGRESS_TARGETS=trivial with_signal time_elapsed time_elapsed_with_signal
REGRESS_TARGETS+=short_time invalid_time

.include <bsd.regress.mk>
