# $FreeBSD$

# Do not define -DKLUDGELINEMODE, as it does not interact well with many
# telnet implementations.

.include <src.opts.mk>

TELNETDIR=	${SRCTOP}/contrib/telnet
.PATH:		${TELNETDIR}/telnetd

PROG=		telnetd
MAN=		telnetd.8

SRCS=		global.c slc.c state.c sys_term.c telnetd.c \
		termstat.c utility.c

WARNS?=		2
WFORMAT?=	0

CFLAGS+=	-DLINEMODE -DUSE_TERMIO -DDIAGNOSTICS -DOLD_ENVIRON \
		-DENV_HACK -DSTREAMSPTY

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=	-DINET6
.endif

CFLAGS+=	-I${TELNETDIR}
CFLAGS+=	-I${TELNETDIR}/telnet

LIBADD=		telnet util ncursesw

.if ${MK_OPENSSL} != "no"
SRCS+=		authenc.c
CFLAGS+=	-DAUTHENTICATION -DENCRYPTION
LIBADD+=	mp crypto pam
.endif

.if ${MK_KERBEROS_SUPPORT} != "no"
CFLAGS+=	-DKRB5 -DFORWARD -Dnet_write=telnet_net_write
LIBADD+=	krb5 roken
.endif

.include <bsd.prog.mk>
