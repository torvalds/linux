# $FreeBSD$

.PATH: ${LDRSRC} ${BOOTSRC}/libsa

CFLAGS+=-I${LDRSRC}

SRCS+=	boot.c commands.c console.c devopen.c interp.c 
SRCS+=	interp_backslash.c interp_parse.c ls.c misc.c 
SRCS+=	module.c

.if ${MACHINE} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SRCS+=	load_elf32.c load_elf32_obj.c reloc_elf32.c
SRCS+=	load_elf64.c load_elf64_obj.c reloc_elf64.c
.elif ${MACHINE_CPUARCH} == "aarch64"
SRCS+=	load_elf64.c reloc_elf64.c
.elif ${MACHINE_CPUARCH} == "arm"
SRCS+=	load_elf32.c reloc_elf32.c
.elif ${MACHINE_CPUARCH} == "powerpc"
SRCS+=	load_elf32.c reloc_elf32.c
SRCS+=	load_elf64.c reloc_elf64.c
SRCS+=	metadata.c
.elif ${MACHINE_CPUARCH} == "sparc64"
SRCS+=	load_elf64.c reloc_elf64.c
SRCS+=	metadata.c
.elif ${MACHINE_ARCH:Mmips64*} != ""
SRCS+= load_elf64.c reloc_elf64.c
SRCS+=	metadata.c
.elif ${MACHINE} == "mips"
SRCS+=	load_elf32.c reloc_elf32.c
SRCS+=	metadata.c
.endif

.if ${LOADER_DISK_SUPPORT:Uyes} == "yes"
SRCS+=	disk.c part.c
.endif

.if ${LOADER_NET_SUPPORT:Uno} == "yes"
SRCS+= dev_net.c
.endif

.if defined(HAVE_BCACHE)
SRCS+=  bcache.c
.endif

.if defined(MD_IMAGE_SIZE)
CFLAGS+= -DMD_IMAGE_SIZE=${MD_IMAGE_SIZE}
SRCS+=	md.c
.else
CLEANFILES+=	md.o
.endif

# Machine-independant ISA PnP
.if defined(HAVE_ISABUS)
SRCS+=	isapnp.c
.endif
.if defined(HAVE_PNP)
SRCS+=	pnp.c
.endif

.if ${LOADER_INTERP} == "lua"
SRCS+=	interp_lua.c
.include "${BOOTSRC}/lua.mk"
LDR_INTERP=	${LIBLUA}
LDR_INTERP32=	${LIBLUA32}
.elif ${LOADER_INTERP} == "4th"
SRCS+=	interp_forth.c
.include "${BOOTSRC}/ficl.mk"
LDR_INTERP=	${LIBFICL}
LDR_INTERP32=	${LIBFICL32}
.elif ${LOADER_INTERP} == "simp"
SRCS+=	interp_simple.c
.else
.error Unknown interpreter ${LOADER_INTERP}
.endif

.if ${MK_LOADER_VERIEXEC} != "no"
CFLAGS+= -DLOADER_VERIEXEC -I${SRCTOP}/lib/libsecureboot/h
.endif

.if defined(BOOT_PROMPT_123)
CFLAGS+=	-DBOOT_PROMPT_123
.endif

.if defined(LOADER_INSTALL_SUPPORT)
SRCS+=	install.c
.endif

# Filesystem support
.if ${LOADER_CD9660_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_CD9660_SUPPORT
.endif
.if ${LOADER_EXT2FS_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_EXT2FS_SUPPORT
.endif
.if ${LOADER_MSDOS_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_MSDOS_SUPPORT
.endif
.if ${LOADER_NANDFS_SUPPORT:U${MK_NAND}} == "yes"
CFLAGS+=	-DLOADER_NANDFS_SUPPORT
.endif
.if ${LOADER_UFS_SUPPORT:Uyes} == "yes"
CFLAGS+=	-DLOADER_UFS_SUPPORT
.endif

# Compression
.if ${LOADER_GZIP_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_GZIP_SUPPORT
.endif
.if ${LOADER_BZIP2_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_BZIP2_SUPPORT
.endif

# Network related things
.if ${LOADER_NET_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_NET_SUPPORT
.endif
.if ${LOADER_NFS_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_NFS_SUPPORT
.endif
.if ${LOADER_TFTP_SUPPORT:Uno} == "yes"
CFLAGS+=	-DLOADER_TFTP_SUPPORT
.endif

# Partition support
.if ${LOADER_GPT_SUPPORT:Uyes} == "yes"
CFLAGS+= -DLOADER_GPT_SUPPORT
.endif
.if ${LOADER_MBR_SUPPORT:Uyes} == "yes"
CFLAGS+= -DLOADER_MBR_SUPPORT
.endif

.if ${HAVE_ZFS:Uno} == "yes"
CFLAGS+=	-DLOADER_ZFS_SUPPORT
CFLAGS+=	-I${ZFSSRC}
CFLAGS+=	-I${SYSDIR}/cddl/boot/zfs
SRCS+=		zfs_cmd.c
.endif

LIBFICL=	${BOOTOBJ}/ficl/libficl.a
.if ${MACHINE} == "i386"
LIBFICL32=	${LIBFICL}
.else
LIBFICL32=	${BOOTOBJ}/ficl32/libficl.a
.endif

LIBLUA=		${BOOTOBJ}/liblua/liblua.a
.if ${MACHINE} == "i386"
LIBLUA32=	${LIBLUA}
.else
LIBLUA32=	${BOOTOBJ}/liblua32/liblua.a
.endif

CLEANFILES+=	vers.c
VERSION_FILE?=	${.CURDIR}/version
.if ${MK_REPRODUCIBLE_BUILD} != no
REPRO_FLAG=	-r
.endif
vers.c: ${LDRSRC}/newvers.sh ${VERSION_FILE}
	sh ${LDRSRC}/newvers.sh ${REPRO_FLAG} ${VERSION_FILE} \
	    ${NEWVERSWHAT}

.if ${MK_LOADER_VERBOSE} != "no"
CFLAGS+=	-DELF_VERBOSE
.endif

.if !empty(HELP_FILES)
HELP_FILES+=	${LDRSRC}/help.common

CLEANFILES+=	loader.help
FILES+=		loader.help

loader.help: ${HELP_FILES}
	cat ${HELP_FILES} | awk -f ${LDRSRC}/merge_help.awk > ${.TARGET}
.endif
