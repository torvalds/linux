.if !defined(BSD_OWN_MK)
.  include <bsd.own.mk>
.endif
PORTSDIR?=	/usr/ports
.include	"${PORTSDIR}/infrastructure/mk/bsd.port.arch.mk"
