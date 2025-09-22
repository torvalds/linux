#	$OpenBSD: Makefile,v 1.2 2016/11/14 10:07:02 mpi Exp $

RDOMAIN?=	5

REGRESS_TARGETS=	create-default-loopback regress-ifalocal
REGRESS_ROOT_TARGETS=	$(REGRESS_TARGETS)

# Ensure that one can create the default loopback of a rdomain
create-default-loopback:
	${SUDO} ifconfig lo${RDOMAIN} rdomain ${RDOMAIN} up
	ifconfig lo${RDOMAIN} 2>&1 | head -n1 | sed 's,${RDOMAIN},RDO,g' | \
		diff -u ${.CURDIR}/lo.ok /dev/stdin

regress-ifalocal:
	${SUDO} sh ${.CURDIR}/ifalocal.sh


.include <bsd.regress.mk>
