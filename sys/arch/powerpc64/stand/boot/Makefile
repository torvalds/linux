#	$OpenBSD: Makefile,v 1.4 2023/04/28 08:45:26 krw Exp $

NOMAN=
#MAN=		boot.8

RDBOOT=		${.CURDIR}/../rdboot/obj/rdboot

MRDISKTYPE= rdroot
MRMAKEFSARGS= -o disklabel=${MRDISKTYPE},minfree=0,density=1024

.if ${MACHINE} == "powerpc64"
all: bsd rdboot

bsd:
	cd ${.CURDIR}/../../compile/BOOT && \
	    ${MAKE} config && ${MAKE} clean && ${MAKE}
	cp -p ${.CURDIR}/../../compile/BOOT/obj/bsd bsd

rdboot: ${RDBOOT}
	cp -p ${RDBOOT} rdboot
	strip rdboot

mr.fs: rdboot bsd
	rm -rf $@.d
	install -d -o root -g wheel $@.d/dev
	install -d -o root -g wheel $@.d/mnt
	install -d -o root -g wheel $@.d/sbin
	install -o ${BINOWN} -g ${BINGRP} -m 555 rdboot $@.d/sbin/init
	cd $@.d/dev && sh ${DESTDIR}/dev/MAKEDEV boot
	makefs ${MRMAKEFSARGS} $@ $@.d

boot: mr.fs
	cp bsd boot
	rdsetroot boot mr.fs

realinstall: boot
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 boot ${DESTDIR}/usr/mdec

clean:
	rm -f boot bsd mr.fs rdboot
	rm -rf mr.fs.d
.endif

.include <bsd.prog.mk>
