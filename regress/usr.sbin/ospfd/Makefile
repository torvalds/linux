#	$OpenBSD: Makefile,v 1.14 2021/12/02 17:10:53 kn Exp $

# The following ports must be installed for the regression tests:
# p5-AnyEvent		provide framework for multiple event loops
# p5-Hash-Merge		merge associative arrays
# p5-YAML		YAML ain't a markup language
#
# Check wether all required perl packages are installed.  If some
# are missing print a warning and skip the tests, but do not fail.

PERL_REQUIRE != perl -Mstrict -Mwarnings -e ' \
    eval { require AnyEvent } or print $@; \
    eval { require Hash::Merge } or print $@; \
    eval { require YAML } or print $@; \
'
.if ! empty (PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo 'run "pkg_add p5-AnyEvent p5-Hash-Merge p5-YAML"'
	@echo SKIPPED
.endif

# Fill out these variables with your own system parameters
# You need a tap device and an unused /24 IPv4 network.

TAPNUM ?=		3
TAPIP ?=		10.188.6.17
RTRID ?=		10.188.0.17

# Automatically generate regress targets from test cases in directory.

ARGS !=			cd ${.CURDIR} && ls args-*.pl
CLEANFILES +=		*.log ospfd.conf ktrace.out stamp-* opentap opentap.d
CLEANFILES +=		*.ktrace PassFd.c PassFd.o PassFd.so
PERLHEADER !=		perl -MConfig -e 'print "$$Config{archlib}/CORE"'
CFLAGS =		-Wall

.MAIN: all

REGRESS_SETUP_ONCE +=	setup
setup:
	[ -c /dev/tap${TAPNUM} ]
	[ -z "${SUDO}" ] || ${SUDO} -C 4 true
	${SUDO} ifconfig tap${TAPNUM} ${TAPIP} netmask 255.255.255.0

REGRESS_CLEANUP +=	cleanup
cleanup:
	-${SUDO} ifconfig tap${TAPNUM} destroy

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.

.if ${.CURDIR} == ${.OBJDIR}
PERLINC =	-I.
PERLPATH =
.else
PERLINC =	-I${.CURDIR} -I${.OBJDIR}
PERLPATH =	${.CURDIR}/
.endif

# The arg tests take a perl hash with arguments controlling the
# test parameters.

.for a in ${ARGS}
REGRESS_TARGETS +=	run-$a
run-$a: $a opentap PassFd.so
	time TAPNUM=${TAPNUM} TAPIP=${TAPIP} RTRID=${RTRID} SUDO=${SUDO} KTRACE=${KTRACE} OSPFD=${OSPFD} perl ${PERLINC} ${PERLPATH}ospfd.pl ${PERLPATH}$a
.endfor

# make perl syntax check for all args files

.PHONY: syntax

syntax: stamp-syntax

stamp-syntax: ${ARGS} stamp-passfd
.for a in ${ARGS}
	@TAPNUM=${TAPNUM} TAPIP=${TAPIP} RTRID=${RTRID} perl ${PERLINC} -c ${PERLPATH}$a
.endfor
	@date >$@

# build and test file descriptor passing perl xs module

.PHONY: passfd

passfd: stamp-passfd

stamp-passfd: PassFd.so
	perl ${PERLINC} ${PERLPATH}testfd.pl
	@date >$@

.SUFFIXES: .xs .so

.xs.so:
	xsubpp -prototypes $> >${@:S/.so$/.c/}
	${CC} -shared -Wall -DNO_LOCALE_NUMERIC -DNO_LOCALE_COLLATE\
	    -I${PERLHEADER} -o $@ ${@:S/.so$/.c/}
	perl ${PERLINC} -M${@:R} -e ''

.include <bsd.regress.mk>
