# $FreeBSD$

.include <src.opts.mk>

NO_OBJ=

CONFGROUPS=	CONFS
CONFS=	README \
	cron \
	imap \
	login \
	other \
	passwd pop3 \
	sshd su system \
	xdm

CONFDIR=	/etc/pam.d
CONFSMODE_README=	444

.if ${MK_AT} != "no"
CONFGROUPS+=	AT
AT+=	atrun
ATPACKAGE+=	at
.endif

.if ${MK_FTP} != "no"
CONFGROUPS+=	FTP
FTP+=	ftpd
FTPPACKAGE+=	ftp

afterinstallconfig:
	${INSTALL_LINK} ${TAG_ARGS} ${DESTDIR}${CONFDIR}/ftpd ${DESTDIR}${CONFDIR}/ftp
.endif

.if ${MK_TELNET} != "no"
CONFGROUPS+=	TELNET
TELNET+=	telnetd
TELNETPACKAGE+=	telnet
.endif

.include <bsd.prog.mk>
