# $FreeBSD$
# All the dts files for allwinner systems we support.

.if ${MACHINE_ARCH} == "armv7"
DTS=	\
	sun4i-a10-cubieboard.dts \
	sun4i-a10-olinuxino-lime.dts \
	sun6i-a31s-sinovoip-bpi-m2.dts \
	sun5i-a13-olinuxino.dts \
	sun5i-r8-chip.dts \
	sun7i-a20-bananapi.dts \
	sun7i-a20-cubieboard2.dts \
	sun7i-a20-lamobo-r1.dts \
	sun7i-a20-olimex-som-evb.dts \
	sun7i-a20-pcduino3.dts \
	sun8i-a83t-bananapi-m3.dts \
	sun8i-h2-plus-orangepi-r1.dts \
	sun8i-h2-plus-orangepi-zero.dts \
	sun8i-h3-nanopi-m1.dts \
	sun8i-h3-nanopi-m1-plus.dts \
	sun8i-h3-nanopi-neo.dts \
	sun8i-h3-orangepi-one.dts \
	sun8i-h3-orangepi-pc.dts \
	sun8i-h3-orangepi-plus2e.dts

DTSO=	sun8i-a83t-sid.dtso \
	sun8i-h3-sid.dtso

LINKS= \
	${DTBDIR}/sun4i-a10-cubieboard.dtb ${DTBDIR}/cubieboard.dtb \
	${DTBDIR}/sun4i-a10-olinuxino-lime.dtb ${DTBDIR}/olinuxino-lime.dtb \
	${DTBDIR}/sun6i-a31s-sinovoip-bpi-m2.dtb ${DTBDIR}/bananapim2.dtb \
	${DTBDIR}/sun7i-a20-bananapi.dtb ${DTBDIR}/bananapi.dtb \
	${DTBDIR}/sun7i-a20-cubieboard2.dtb ${DTBDIR}/cubieboard2.dtb \
	${DTBDIR}/sun7i-a20-olimex-som-evb.dtb ${DTBDIR}/olimex-a20-som-evb.dtb \
	${DTBDIR}/sun7i-a20-pcduino3.dtb ${DTBDIR}/pcduino3.dtb \
	${DTBDIR}/sun8i-a83t-bananapi-m3.dtb ${DTBDIR}/sinovoip-bpi-m3.dtb \
	${DTBDIR}/sun8i-a83t-bananapi-m3.dtb ${DTBDIR}/sun8i-a83t-sinovoip-bpi-m3.dtb
.elif ${MACHINE_ARCH} == "aarch64"
DTS=	\
	allwinner/sun50i-a64-nanopi-a64.dts \
	allwinner/sun50i-a64-olinuxino.dts \
	allwinner/sun50i-a64-pine64-lts.dts \
	allwinner/sun50i-a64-pine64-plus.dts \
	allwinner/sun50i-a64-pine64.dts \
	allwinner/sun50i-a64-sopine-baseboard.dts \
	allwinner/sun50i-h5-orangepi-pc2.dts

DTSO=	sun50i-a64-opp.dtso \
	sun50i-a64-pwm.dtso \
	sun50i-a64-rpwm.dtso \
	sun50i-a64-sid.dtso \
	sun50i-a64-ths.dtso \
	sun50i-a64-timer.dtso

.endif

.include <bsd.dtb.mk>
