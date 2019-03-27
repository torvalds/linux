# $FreeBSD$

.include <bsd.init.mk>

.PATH:		${BOOTSRC}/i386/boot2 ${BOOTSRC}/i386/gptboot \
		${BOOTSRC}/i386/zfsboot ${BOOTSRC}/i386/common \
		${SASRC}

FILES=		gptzfsboot
MAN=		gptzfsboot.8

BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 9600
B2SIOFMT?=	0x3

REL1=	0x700
ORG1=	0x7c00
ORG2=	0x0

CFLAGS+=-DBOOTPROG=\"gptzfsboot\" \
	-O1 \
	-DGPT -DZFS -DBOOT2 \
	-DSIOPRT=${BOOT_COMCONSOLE_PORT} \
	-DSIOFMT=${B2SIOFMT} \
	-DSIOSPD=${BOOT_COMCONSOLE_SPEED} \
	-I${LDRSRC} \
	-I${BOOTSRC}/i386/common \
	-I${ZFSSRC} \
	-I${SYSDIR}/crypto/skein \
	-I${SYSDIR}/cddl/boot/zfs \
	-I${BOOTSRC}/i386/btx/lib \
	-I${BOOTSRC}/i386/boot2 \
	-Wall -Waggregate-return -Wbad-function-cast \
	-Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
	-Wpointer-arith -Wshadow -Wstrict-prototypes -Wwrite-strings \
	-Wno-pointer-sign

CFLAGS.clang+=	-Wno-tentative-definition-incomplete-type

NO_WCAST_ALIGN=

CFLAGS.gcc+=	--param max-inline-insns-single=100

LD_FLAGS+=${LD_FLAGS_BIN}

CLEANFILES+=	gptzfsboot

gptzfsboot: gptldr.bin gptzfsboot.bin ${BTXKERN}
	btxld -v -E ${ORG2} -f bin -b ${BTXKERN} -l gptldr.bin \
	    -o ${.TARGET} gptzfsboot.bin

CLEANFILES+=	gptldr.bin gptldr.out gptldr.o

gptldr.bin: gptldr.out
	${OBJCOPY} -S -O binary gptldr.out ${.TARGET}

gptldr.out: gptldr.o
	${LD} ${LD_FLAGS} -e start -Ttext ${ORG1} -o ${.TARGET} gptldr.o

CLEANFILES+=	gptzfsboot.bin gptzfsboot.out zfsboot.o sio.o cons.o \
		drv.o gpt.o ${OPENCRYPTO_XTS}

gptzfsboot.bin: gptzfsboot.out
	${OBJCOPY} -S -O binary gptzfsboot.out ${.TARGET}

gptzfsboot.out: ${BTXCRT} zfsboot.o sio.o gpt.o drv.o cons.o \
	${OPENCRYPTO_XTS}
	${LD} ${LD_FLAGS} -Ttext ${ORG2} -o ${.TARGET} ${.ALLSRC} ${LIBSA32}

zfsboot.o: ${ZFSSRC}/zfsimpl.c

.include <bsd.prog.mk>

# XXX: clang integrated-as doesn't grok .codeNN directives yet
CFLAGS.gptldr.S=	${CLANG_NO_IAS}
