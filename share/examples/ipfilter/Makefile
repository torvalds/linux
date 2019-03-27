# $FreeBSD$

PACKAGE=examples
FILES=	README

# dist sample files
.PATH: ${SRCTOP}/contrib/ipfilter/rules \
       ${SRCTOP}/contrib/ipfilter \
       ${SRCTOP}/contrib/ipfilter/man
FILES+=	BASIC.NAT BASIC_1.FW BASIC_2.FW \
	example.1 example.2 example.3 example.4 example.5 \
	example.6 example.7 example.8 example.9 example.10 \
	example.11 example.12 example.13 example.sr firewall \
	ftp-proxy ftppxy nat-setup nat.eg server tcpstate

# ftp://ftp.OpenBSD.org/pub/OpenBSD/src/share/ipf/ sample files.
FILES+=	example.14 firewall.1 firewall.2 \
	ipf.conf.permissive ipf.conf.restrictive \
	ipf.conf.sample ipnat.conf.sample

# http://www.obfuscation.org/ipf/ how-to
FILES+=	ipf-howto.txt

# http://coombs.anu.edu.au/~avalon/ sample files
FILES+=	examples.txt rules.txt

BINMODE=0755
SCRIPTS=	mkfilters
MAN=		mkfilters.1

SCRIPTSDIR=	${SHAREDIR}/examples/ipfilter
FILESDIR=	${SHAREDIR}/examples/ipfilter

.include <bsd.prog.mk>
