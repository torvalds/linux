# $FreeBSD$

.include <src.opts.mk>

CONFS=	340.noid \
	450.status-security \
	999.local

# NB: keep these sorted by MK_* knobs

.if ${MK_LOCATE} != "no"
CONFS+=	310.locate
.endif

.if ${MK_MAN_UTILS} != "no"
CONFS+=	320.whatis
.endif

.include <bsd.prog.mk>
