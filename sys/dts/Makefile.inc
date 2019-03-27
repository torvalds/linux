# $FreeBSD$

SYSDIR?=${SRCTOP}/sys

test-dts:
.for dts in ${DTS}
	@env MACHINE=`basename ${.CURDIR}` ${SYSDIR}/tools/fdt/make_dtb.sh ${SYSDIR} ${dts} /tmp
.endfor

test-dtso:
.for dtso in ${DTSO}
	@env MACHINE=`basename ${.CURDIR}` ${SYSDIR}/tools/fdt/make_dtbo.sh ${SYSDIR} ${dtso} /tmp
.endfor
