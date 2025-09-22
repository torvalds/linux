# $OpenBSD: Makefile,v 1.5 2021/09/30 17:57:44 anton Exp $

# Create chains and loops of spliced tcp and udp sockets.
# Send data through them and check that MAXLOOP is triggered in kernel.

# The following ports must be installed for the regression tests:
# p5-BSD-Socket-Splice	perl interface to OpenBSD socket splicing
#
# Check wether all required perl packages are installed.  If some
# are missing print a warning and skip the tests, but do not fail.

.if ! (make(clean) || make(cleandir) || make(obj))

PERL_REQUIRE !=	perl -Mstrict -Mwarnings -e ' \
    eval { require BSD::Socket::Splice } or print $@; \
'
.if ! empty (PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo Install these Perl modules for additional tests.
	@echo SKIPPED
.endif

.endif

TARGETS =
VERBOSE ?=

v =
.if !empty(VERBOSE) && ${VERBOSE:L} != "no"
v =	-v
.endif

.for i in 4 6
.for p in tcp udp
.for c in 1 2 3 127 128 129 200
REGRESS_TARGETS +=	run-chain-ipv$i-proto$p-count$c
run-chain-ipv$i-proto$p-count$c: chain.pl
	ulimit -n 500; \
	SUDO="${SUDO}" ${KTRACE} perl ${PERLPATH}chain.pl -$i -c $c -p $p $v
.endfor

REGRESS_TARGETS +=	run-loop-ipv$i-proto$p
run-loop-ipv$i-proto$p: loop.pl
	SUDO="${SUDO}" ${KTRACE} perl ${PERLPATH}loop.pl -$i -p $p $v
.endfor
.endfor

REGRESS_TARGETS +=	run-bcast
run-bcast: bcast.pl
	SUDO="${SUDO}" ${KTRACE} perl ${PERLPATH}bcast.pl $v

.include <bsd.regress.mk>
