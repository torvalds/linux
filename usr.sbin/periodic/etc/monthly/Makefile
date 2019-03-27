# $FreeBSD$

.include <src.opts.mk>

CONFGROUPS=	CONFS

CONFS=	450.status-security \
	999.local

# NB: keep these sorted by MK_* knobs

.if ${MK_UTMPX} != "no"
CONFGROUPS+=	ACCT
ACCT+=		200.accounting
ACCTDIR=	/etc/periodic/monthly
ACCTMODE=	${BINMODE}
ACCTPACKAGE=	acct
.endif

.include <bsd.prog.mk>
