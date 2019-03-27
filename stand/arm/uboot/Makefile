# $FreeBSD$

LOADER_UFS_SUPPORT?=	yes
LOADER_CD9660_SUPPORT?=	no
LOADER_MSDOS_SUPPORT?=	no
LOADER_EXT2FS_SUPPORT?=	no
LOADER_NET_SUPPORT?=	yes
LOADER_NFS_SUPPORT?=	yes
LOADER_TFTP_SUPPORT?=	no
LOADER_GZIP_SUPPORT?=	no
LOADER_BZIP2_SUPPORT?=	no

.include <bsd.init.mk>

FILES+=		ubldr ubldr.bin

NEWVERSWHAT=	"U-Boot loader" ${MACHINE_ARCH}
INSTALLFLAGS=	-b
WARNS?=		1
# Address at which ubldr will be loaded.
# This varies for different boards and SOCs.
UBLDR_LOADADDR?=	0x1000000

# Architecture-specific loader code
SRCS=		start.S conf.c self_reloc.c vers.c

.if ${COMPILER_TYPE} == "gcc" && ${COMPILER_VERSION} > 40201
CWARNFLAGS.self_reloc.c+=	-Wno-error=maybe-uninitialized
.endif

HELP_FILES=	 ${.CURDIR}/help.uboot ${BOOTSRC}/fdt/help.fdt

# Always add MI sources
.include	"${BOOTSRC}/loader.mk"

LDFLAGS=	-nostdlib -static -T ${.CURDIR}/ldscript.${MACHINE_CPUARCH}
LDFLAGS+=	-Wl,-znotext

# Pull in common loader code
.include	"${BOOTSRC}/uboot.mk"

CFLAGS+=	-fPIC

DPADD=		${LDR_INTERP} ${LIBUBOOT} ${LIBFDT} ${LIBUBOOT_FDT} ${LIBSA}
LDADD=		${LDR_INTERP} ${LIBUBOOT} ${LIBFDT} ${LIBUBOOT_FDT} ${LIBSA}

OBJS+=  ${SRCS:N*.h:R:S/$/.o/g}

ldscript.abs:
	echo "UBLDR_LOADADDR = ${UBLDR_LOADADDR};" >${.TARGET}

ldscript.pie:
	echo "UBLDR_LOADADDR = 0;" >${.TARGET}

ubldr: ${OBJS} ldscript.abs ${.CURDIR}/ldscript.${MACHINE_CPUARCH} ${DPADD}
	${CC} ${CFLAGS} -T ldscript.abs ${LDFLAGS} \
	    -o ${.TARGET} ${OBJS} ${LDADD}

ubldr.pie: ${OBJS} ldscript.pie ${.CURDIR}/ldscript.${MACHINE_CPUARCH} ${DPADD}
	${CC} ${CFLAGS} -T ldscript.pie ${LDFLAGS} -pie -Wl,-Bsymbolic \
	    -o ${.TARGET} ${OBJS} ${LDADD}

ubldr.bin: ubldr.pie
	${OBJCOPY} -S -O binary ubldr.pie ${.TARGET}

CLEANFILES+=	ldscript.abs ldscript.pie ubldr ubldr.pie ubldr.bin

.include <bsd.prog.mk>
