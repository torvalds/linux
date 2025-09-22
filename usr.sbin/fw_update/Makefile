#	$OpenBSD: Makefile,v 1.5 2022/01/06 13:18:36 anton Exp $

PROG=	patterns
MAN=	fw_update.8
CC=	${HOSTCC}
CFLAGS+=-I ${.CURDIR}/../../sys/dev/pci/drm/amd/amdgpu \
	-I ${.CURDIR}/../../sys/dev/pci/drm/i915 \
	-I ${.CURDIR}/../../sys/dev/pci/drm/radeon

CLEANFILES+=firmware_patterns

firmware_patterns: patterns
	${.OBJDIR}/patterns | sort | uniq > $@

realinstall:

all:	firmware_patterns

afterinstall: firmware_patterns
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/fw_update.sh ${DESTDIR}${BINDIR}/fw_update
	${INSTALL} ${INSTALL_COPY} -o ${SHAREOWN} -g ${SHAREGRP} \
		-m ${SHAREMODE} ${.OBJDIR}/firmware_patterns \
		${DESTDIR}${SHAREDIR}/misc/firmware_patterns

.include <bsd.prog.mk>
