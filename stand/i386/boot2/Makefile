# $FreeBSD$

.include <bsd.init.mk>

FILES=		boot boot1 boot2

# A value of 0x80 enables LBA support.
BOOT_BOOT1_FLAGS?=	0x80

BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 9600
B2SIOFMT?=	0x3

REL1=	0x700
ORG1=	0x7c00
ORG2=	0x2000

# Decide level of UFS support.
BOOT2_UFS?=	UFS1_AND_UFS2
#BOOT2_UFS?=	UFS2_ONLY
#BOOT2_UFS?=	UFS1_ONLY

CFLAGS+=-fomit-frame-pointer \
	-mrtd \
	-mregparm=3 \
	-D${BOOT2_UFS} \
	-DFLAGS=${BOOT_BOOT1_FLAGS} \
	-DSIOPRT=${BOOT_COMCONSOLE_PORT} \
	-DSIOFMT=${B2SIOFMT} \
	-DSIOSPD=${BOOT_COMCONSOLE_SPEED} \
	-I${LDRSRC} \
	-Wall -Waggregate-return -Wbad-function-cast -Wno-cast-align \
	-Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
	-Wpointer-arith -Wshadow -Wstrict-prototypes -Wwrite-strings \
	-Winline

CFLAGS.gcc+=	-Os \
		-fno-asynchronous-unwind-tables \
		--param max-inline-insns-single=100
.if ${COMPILER_TYPE} == "gcc" && ${COMPILER_VERSION} <= 40201
CFLAGS.gcc+=   -mno-align-long-strings
.endif

CFLAGS.clang+=	-Oz ${CLANG_OPT_SMALL}

LD_FLAGS+=${LD_FLAGS_BIN}

CLEANFILES+=	boot

boot: boot1 boot2
	cat boot1 boot2 > boot

CLEANFILES+=	boot1 boot1.out boot1.o

boot1: boot1.out
	${OBJCOPY} -S -O binary boot1.out ${.TARGET}

boot1.out: boot1.o
	${LD} ${LD_FLAGS} -e start -Ttext ${ORG1} -o ${.TARGET} boot1.o

CLEANFILES+=	boot2 boot2.ld boot2.ldr boot2.bin boot2.out boot2.o \
		boot2.h sio.o

BOOT2SIZE=	7680

boot2: boot2.ld
	@set -- `ls -l ${.ALLSRC}`; x=$$((${BOOT2SIZE}-$$5)); \
	    echo "$$x bytes available"; test $$x -ge 0
	${DD} if=${.ALLSRC} of=${.TARGET} obs=${BOOT2SIZE} conv=osync

boot2.ld: boot2.ldr boot2.bin ${BTXKERN}
	btxld -v -E ${ORG2} -f bin -b ${BTXKERN} -l boot2.ldr \
	    -o ${.TARGET} -P 1 boot2.bin

boot2.ldr:
	${DD} if=/dev/zero of=${.TARGET} bs=512 count=1

boot2.bin: boot2.out
	${OBJCOPY} -S -O binary boot2.out ${.TARGET}

boot2.out: ${BTXCRT} boot2.o sio.o
	${LD} ${LD_FLAGS} -Ttext ${ORG2} -o ${.TARGET} ${.ALLSRC}

SRCS=	boot2.c boot2.h

boot2.h: boot1.out
	${NM} -t d ${.ALLSRC} | awk '/([0-9])+ T xread/ \
	    { x = $$1 - ORG1; \
	    printf("#define XREADORG %#x\n", REL1 + x) }' \
	    ORG1=`printf "%d" ${ORG1}` \
	    REL1=`printf "%d" ${REL1}` > ${.TARGET}

.include <bsd.prog.mk>

# XXX: clang integrated-as doesn't grok .codeNN directives yet
CFLAGS.boot1.S=		${CLANG_NO_IAS}
