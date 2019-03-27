
# $FreeBSD$

SUBDIR= ahc ahd

firmware: aicasm/aicasm ahdfirmware ahcfirmware

aicasm/aicasm:
	( cd ${.CURDIR}/aicasm ; ${MAKE} aicasm )

ahdfirmware:
	( cd ${.CURDIR}/ahd ; ${MAKE} ahdfirmware )

ahcfirmware:
	( cd ${.CURDIR}/ahc ; ${MAKE} ahcfirmware )

cleanfirmware:
	( cd .${CURDIR}/aicasm ; ${MAKE} clean )
	( cd .${CURDIR}/ahc ; ${MAKE} cleanfirmware )
	( cd .${CURDIR}/ahd ; ${MAKE} cleanfirmware )

.include <bsd.subdir.mk>
