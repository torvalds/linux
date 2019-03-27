# $FreeBSD$

.include <src.opts.mk>

TELNETDIR=	${SRCTOP}/contrib/telnet
.PATH:		${TELNETDIR}/telnet

PROG=		telnet

SRCS=		commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOPIE \
		-I${TELNETDIR} -I${TELNETDIR}/libtelnet/

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=	-DINET6
.endif

WARNS?=		2

LIBADD=		telnet ncursesw

.if !defined(RELEASE_CRUNCH)
CFLAGS+=	-DIPSEC
LIBADD+=	ipsec
.else
.PATH:         ${TELNETDIR}/libtelnet
SRCS+=		genget.c getent.c misc.c
CFLAGS+=	-DHAS_CGETENT
.endif

.if ${MK_OPENSSL} != "no"
SRCS+=		authenc.c
CFLAGS+=	-DENCRYPTION -DAUTHENTICATION -DIPSEC
LIBADD+=	mp crypto ipsec pam
.endif

.if ${MK_KERBEROS_SUPPORT} != "no"
CFLAGS+=	-DKRB5 -DFORWARD -Dnet_write=telnet_net_write
LIBADD+=	krb5 roken
.endif

.include <bsd.prog.mk>
