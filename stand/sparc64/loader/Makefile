# $FreeBSD$

HAVE_ZFS=		${MK_LOADER_ZFS}

LOADER_DISK_SUPPORT?=	yes
LOADER_UFS_SUPPORT?=	yes
LOADER_CD9660_SUPPORT?=	yes
LOADER_EXT2FS_SUPPORT?=	no
LOADER_MSDOS_SUPPORT?=	no
LOADER_NET_SUPPORT?=	yes
LOADER_NFS_SUPPORT?=	yes
LOADER_TFTP_SUPPORT?=	yes
LOADER_GZIP_SUPPORT?=	yes
LOADER_BZIP2_SUPPORT?=	no
LOADER_DEBUG?=		no

.include <bsd.init.mk>

PROG?=		loader
NEWVERSWHAT?=	"bootstrap loader" sparc64
VERSION_FILE=	${.CURDIR}/../loader/version
INSTALLFLAGS=	-b

.if ${MK_LOADER_ZFS} != "no"
HAVE_ZFS=	yes
.endif

# Architecture-specific loader code
.PATH:		${BOOTSRC}/sparc64/loader
SRCS=		locore.S main.c vers.c

.if ${LOADER_DEBUG} == "yes"
CFLAGS+=	-DLOADER_DEBUG
.endif

.if exists(${.CURDIR}/help.sparc64)
HELP_FILES=	${.CURDIR}/help.sparc64
.endif

# Always add MI sources
.include	"${BOOTSRC}/loader.mk"

LDFLAGS+=	-static

.if ${MK_LOADER_ZFS} == "yes"
LINKS=		${BINDIR}/loader ${BINDIR}/zfsloader
.endif

# Open Firmware standalone support library
LIBOFW=		${BOOTOBJ}/ofw/libofw/libofw.a
CFLAGS+=	-I${BOOTSRC}/ofw/libofw/

DPADD=		${LDR_INTERP} ${LIBOFW} ${LIBSA}
LDADD=		${LDR_INTERP} ${LIBOFW} ${LIBSA}

.include <bsd.prog.mk>
