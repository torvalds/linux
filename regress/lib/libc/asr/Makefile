#	$OpenBSD: Makefile,v 1.4 2017/03/10 17:23:48 eric Exp $

#
# Notes on building and running the regression tests
#
# The regress suite builds two sets of static executables: the former linked
# against the system libc, and the latter against the libc found in /usr/obj
# The idea is to compare the output of all programs with internal changes in
# the libc (more specifically the resolver).  They will be run in a chrooted
# environment to test various /etc configurations without touching the local
# machine config files.
#
# First, build a libc that needs testing, then:
#
# $ make
# $ doas make install
# $ doas make regress
#
# Tests output goes into a $REGRESSDIR/tmp.XXXXXXXXXX directory, and a digest
# is displayed at the end.
#

REGRESSDIR?=	/tmp/regress-asr
REGRESS?=	regress.sh

A?=	.a
B?=	.b

DIRA?=	/usr/lib
DIRB?=	/usr/obj/lib/libc


all: build

build:
	cd bin && EXT=${A} LDFLAGS=-L${DIRA} make
	cd bin && EXT=${B} LDFLAGS=-L${DIRB} make

clean:
	cd bin && EXT=${A} make clean
	cd bin && EXT=${B} make clean

install:
	mkdir -p ${REGRESSDIR}/bin
	cd bin && BINDIR=${REGRESSDIR}/bin EXT=${A} make install
	cd bin && BINDIR=${REGRESSDIR}/bin EXT=${B} make install

uninstall:
	rm -rf ${REGRESSDIR}

regress:
	RUNDIR=${REGRESSDIR} A=${A} B=${B} sh ${.CURDIR}/${REGRESS}
