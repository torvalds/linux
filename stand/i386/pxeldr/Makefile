# $FreeBSD$

.include <bsd.init.mk>

PROG=	${LDR}
INTERNALPROG=
FILES=	${BOOT}
MAN=	${BOOT}.8
SRCS=	${LDR}.S
CLEANFILES+= ${BOOT}

BOOT=	pxeboot
LDR=	pxeldr
ORG=	0x7c00
LOADER=	loader

.if defined(BOOT_PXELDR_PROBE_KEYBOARD)
CFLAGS+=-DPROBE_KEYBOARD
.endif

.if defined(BOOT_PXELDR_ALWAYS_SERIAL)
CFLAGS+=-DALWAYS_SERIAL
.endif

CFLAGS+=-I${BOOTSRC}/i386/common

L=${LOADER_DEFAULT_INTERP}
LOADERBIN= ${BOOTOBJ}/i386/loader_${L}/loader_${L}.bin

CLEANFILES+= ${BOOT}.tmp

${BOOT}: ${LDR} ${LOADER}
	cat ${LDR} ${LOADER} > ${.TARGET}.tmp
	${DD} if=${.TARGET}.tmp of=${.TARGET} obs=2k conv=osync
	rm ${.TARGET}.tmp

LDFLAGS+=${LDFLAGS_BIN}

CLEANFILES+= ${LOADER}

${LOADER}: ${LOADERBIN} ${BTXLDR} ${BTXKERN}
	btxld -v -f aout -e ${LOADER_ADDRESS} -o ${.TARGET} -l ${BTXLDR} \
	    -b ${BTXKERN} ${LOADERBIN}

.include <bsd.prog.mk>

# XXX: clang integrated-as doesn't grok .codeNN directives yet
CFLAGS.pxeldr.S=	${CLANG_NO_IAS}
