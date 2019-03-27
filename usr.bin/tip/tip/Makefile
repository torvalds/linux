#	$OpenBSD: Makefile,v 1.11 2006/05/25 08:41:52 jmc Exp $
# $FreeBSD$
#
# Files are:
#	/etc/remote		remote host description file
#	/etc/phones		phone number file, owned by ${OWNER} and
#				mode 6??
#	/var/log/aculog		ACU accounting file, owned by ${OWNER} and
#				mode 6?? {if ACULOG defined}
# Presently supports:
#	BIZCOMP
#	DEC DF02-AC, DF03-AC
#	DEC DN-11/Able Quadracall
#	HAYES and Hayes emulators
#	USR COURIER (2400 baud)
#	VENTEL 212+
#	VADIC 831 RS232 adaptor
#	VADIC 3451
#	TELEBIT T3000
#
# Configuration defines:
#	DF02, DF03, DN11	ACU's supported
#	  BIZ1031, BIZ1022, VENTEL, V831, V3451, HAYES, COURIER, T3000
#	ACULOG			turn on tip logging of ACU use
#	PRISTINE		no phone #'s put in ACU log file
#	CONNECT			worthless command
#	DEFBR			default baud rate to make connection at
#	DEFFS			default frame size for FTP buffering of
#				writes on local side
#	BUFSIZ			buffer sizing from stdio, must be fed
#				explicitly to remcap.c if not 1024
#	CONNECT			enable ~C command (connect pgm to remote)

CONFS=	phones remote
PROG=	tip
LINKS=	${BINDIR}/tip ${BINDIR}/cu
MAN=	tip.1 cu.1
CFLAGS+=-I${.CURDIR} -DDEFBR=9600 -DDEFFS=BUFSIZ -DACULOG -DPRISTINE \
	-DCONNECT -DV831 -DVENTEL -DHAYES -DCOURIER -DT3000
WARNS?=	2
.PATH:	${.CURDIR}/../libacu
SRCS=	acu.c acutab.c cmds.c cmdtab.c cu.c hunt.c log.c partab.c \
	remote.c tip.c tipout.c uucplock.c value.c vars.c \
	biz22.c courier.c df.c dn11.c hayes.c t3000.c v3451.c v831.c ventel.c

# -- acutab is configuration dependent, and so depends on the Makefile
# -- remote.o depends on the Makefile because of DEFBR and DEFFS
# -- log.o depends on the Makefile because of ACULOG
acutab.o log.o remote.o: Makefile

# Don't rebuild based on remote.c
remote: .MADE

.include <bsd.prog.mk>

# Dirty, rotten hack. This can be removed when we are confident that there
# is no cu(1) with the schg-bit set.
beforeinstall:
.if exists(${DESTDIR}${BINDIR}/cu)
	-chflags noschg ${DESTDIR}${BINDIR}/cu
.endif
