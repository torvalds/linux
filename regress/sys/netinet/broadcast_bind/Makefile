# $OpenBSD: Makefile,v 1.2 2015/12/02 20:45:00 mpi Exp $

PROG	 = broadcast_bind
IFACE	 = vether11
INSFX	!= jot -s '.' -r 2 0 255
INADDR	 = 10.${INSFX}.11
BADADDR	 = 10.${INSFX}.22
BCADDR	 = 10.${INSFX}.255

run-regress-broadcast_bind: ${PROG}
	${SUDO} ifconfig ${IFACE} create
	${SUDO} ifconfig ${IFACE} inet ${INADDR}/24 up
	${.OBJDIR}/broadcast_bind ${INADDR} ${BADADDR} ${BCADDR}
	${SUDO} ifconfig ${IFACE} destroy

.include <bsd.regress.mk>
