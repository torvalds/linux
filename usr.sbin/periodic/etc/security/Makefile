# $FreeBSD$

.include <src.opts.mk>

CONFGROUPS=	CONFS DATA

CONFS=	100.chksetuid \
	110.neggrpperm \
	200.chkmounts \
	300.chkuid0 \
	400.passwdless \
	410.logincheck \
	700.kernelmsg \
	800.loginfail
DATA=	security.functions
DATAMODE=	444

# NB: keep these sorted by MK_* knobs

.if ${MK_IPFILTER} != "no"
CONFS+=	510.ipfdenied
CONFS+=	610.ipf6denied
.endif

.if ${MK_IPFW} != "no"
CONFS+=	500.ipfwdenied \
	550.ipfwlimit
.endif

.if ${MK_PF} != "no"
CONFS+=	520.pfdenied
.endif

.if ${MK_INETD} != "no" && ${MK_TCP_WRAPPERS} != "no"
CONFS+=	900.tcpwrap
.endif

.include <bsd.prog.mk>
