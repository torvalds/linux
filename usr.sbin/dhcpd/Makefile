# $OpenBSD: Makefile,v 1.7 2017/04/12 19:19:11 krw Exp $

.include <bsd.own.mk>

SRCS=	bootp.c confpars.c db.c dhcp.c dhcpd.c bpf.c packet.c log.c \
	dispatch.c print.c memory.c options.c inet.c conflex.c parse.c \
	alloc.c tables.c tree.c hash.c convert.c icmp.c pfutils.c sync.c \
	udpsock.c
PROG=	dhcpd
MAN=	dhcpd.8 dhcpd.conf.5 dhcpd.leases.5 dhcp-options.5

LDADD+=-lcrypto
CFLAGS+=-Wall

.include <bsd.prog.mk>
CFLAGS+=-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=-Wmissing-declarations
CFLAGS+=-Wshadow -Wpointer-arith -Wcast-qual
