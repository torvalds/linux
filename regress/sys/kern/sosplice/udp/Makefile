#	$OpenBSD: Makefile,v 1.4 2020/12/17 00:51:12 bluhm Exp $

.include <bsd.regress.mk>

# The arg tests take a perl hash with arguments controlling the
# test parameters.  Generally they consist of client, relay, server.
# The relay.pl test fork these three processes locally.  The remote.pl
# test has local client and server but the relay process is running
# on a remote machine reachable with ssh.  All tests can run with
# a regular userland copy relay or with a kernel socket splicing
# relay.

.for a in ${ARGS}
run-$a: $a
.if empty (REMOTE_SSH)
	time perl ${PERLINC} ${PERLPATH}relay.pl copy ${PERLPATH}$a
	time perl ${PERLINC} ${PERLPATH}relay.pl splice ${PERLPATH}$a
.else
	time perl ${PERLINC} ${PERLPATH}remote.pl copy ${LOCAL_ADDR} ${REMOTE_ADDR} ${REMOTE_SSH} ${PERLPATH}$a
	time perl ${PERLINC} ${PERLPATH}remote.pl splice ${LOCAL_ADDR} ${REMOTE_ADDR} ${REMOTE_SSH} ${PERLPATH}$a
.endif
.endfor
