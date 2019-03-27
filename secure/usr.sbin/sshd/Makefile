# $FreeBSD$

.include <src.opts.mk>

CONFS=	moduli sshd_config
CONFSDIR=	/etc/ssh
PROG=	sshd
SRCS=	sshd.c auth-rhosts.c auth-passwd.c \
	audit.c audit-bsm.c audit-linux.c platform.c \
	sshpty.c sshlogin.c servconf.c serverloop.c \
	auth.c auth2.c auth-options.c session.c \
	auth2-chall.c groupaccess.c \
	auth-bsdauth.c auth2-hostbased.c auth2-kbdint.c \
	auth2-none.c auth2-passwd.c auth2-pubkey.c \
	monitor.c monitor_wrap.c auth-krb5.c \
	auth2-gss.c gss-serv.c gss-serv-krb5.c \
	loginrec.c auth-pam.c auth-shadow.c auth-sia.c md5crypt.c \
	sftp-server.c sftp-common.c \
	sandbox-null.c sandbox-rlimit.c sandbox-systrace.c sandbox-darwin.c \
	sandbox-seccomp-filter.c sandbox-capsicum.c sandbox-pledge.c \
	sandbox-solaris.c uidswap.c
PACKAGE=	ssh

# gss-genr.c really belongs in libssh; see src/secure/lib/libssh/Makefile
SRCS+=	gss-genr.c

MAN=	sshd.8 sshd_config.5
CFLAGS+=-I${SSHDIR} -include ssh_namespace.h
SRCS+=	ssh_namespace.h

# Don't rebuild based on moduli.c
moduli: .MADE

# pam should always happen before ssh here for static linking
LIBADD=	pam ssh util

.if ${MK_LDNS} != "no"
CFLAGS+=	-DHAVE_LDNS=1
#DPADD+=	${LIBLDNS}
#LDADD+=	-lldns
.endif

.if ${MK_AUDIT} != "no"
CFLAGS+= -DUSE_BSM_AUDIT=1 -DHAVE_GETAUDIT_ADDR=1
LIBADD+=	bsm
.endif

.if ${MK_BLACKLIST_SUPPORT} != "no"
CFLAGS+= -DUSE_BLACKLIST=1 -I${SRCTOP}/contrib/blacklist/include
SRCS+=	blacklist.c
LIBADD+= blacklist
LDFLAGS+=-L${LIBBLACKLISTDIR}
.endif

.if ${MK_GSSAPI} != "no" && ${MK_KERBEROS_SUPPORT} != "no"
CFLAGS+= -include krb5_config.h
SRCS+=	krb5_config.h
LIBADD+=	gssapi_krb5 gssapi krb5
.endif

.if ${MK_TCP_WRAPPERS} != "no"
CFLAGS+=	-DLIBWRAP=1
LIBADD+=	wrap
.endif

LIBADD+=	crypto

.if defined(LOCALBASE)
CFLAGS+= -DXAUTH_PATH=\"${LOCALBASE}/bin/xauth\"
.endif

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
