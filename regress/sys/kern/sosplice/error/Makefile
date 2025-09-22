#	$OpenBSD: Makefile,v 1.4 2020/12/17 00:51:12 bluhm Exp $

.include <bsd.regress.mk>

# The error tests try to splice unsuitable sockets and check the
# kernel error code.

.for a in ${ARGS}
run-$a: $a
.if empty (REMOTE_SSH)
	time perl ${PERLINC} ${PERLPATH}error.pl ${PERLPATH}$a
.else
	time perl ${PERLINC} ${PERLPATH}remote.pl ${REMOTE_SSH} ${PERLPATH}$a
.endif
.endfor
