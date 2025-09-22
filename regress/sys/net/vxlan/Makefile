#	$OpenBSD: Makefile,v 1.7 2024/02/28 06:33:39 anton Exp $

REGRESS_TARGETS=	vxlan_1 vxlan_2.inet vxlan_2.dyn
REGRESS_ROOT_TARGETS=	vxlan_1 vxlan_2.inet vxlan_2.dyn

N1=			11
N2=			12
N3=			13
N4=			14
N5=			15

vxlan_1:
	${SUDO} ksh ${.CURDIR}/$@.sh -R "${N1} ${N2}" -I "${N1} ${N2}"

vxlan_2.inet:
	${SUDO} ksh ${.CURDIR}/${@:R}.sh \
		-R "${N1} ${N2} ${N3} ${N4} ${N5}" -f inet

vxlan_2.dyn:
	env SUDO="${SUDO}" DYNAMIC=1 \
	    ksh ${.CURDIR}/${@:R}.sh -R "${N1} ${N2} ${N3} ${N4} ${N5}" -f inet

vxlan_2.inet6:
	${SUDO} ksh ${.CURDIR}/${@:R}.sh \
		-R "${N1} ${N2} ${N3} ${N4} ${N5}" -f inet6

vxlan_2.dyn6:
	env SUDO="${SUDO}" DYNAMIC=1 \
	    ksh ${.CURDIR}/${@:R}.sh -R "${N1} ${N2} ${N3} ${N4} ${N5}" -f inet6

.include <bsd.regress.mk>
