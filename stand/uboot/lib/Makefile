# $FreeBSD$

.include <bsd.init.mk>

.PATH: ${LDRSRC}

LIB=		uboot
WARNS?=		2

SRCS=	console.c copy.c devicename.c elf_freebsd.c glue.c
SRCS+=	module.c net.c reboot.c time.c

.if ${LOADER_DISK_SUPPORT:Uyes} == "yes"
SRCS+= disk.c
.endif

.include	"${BOOTSRC}/fdt.mk"

# Pick up the bootstrap header for some interface items
CFLAGS+=	-I${LDRSRC}

.ifdef(BOOT_DISK_DEBUG)
# Make the disk code more talkative
CFLAGS+= -DDISK_DEBUG
.endif

.include <bsd.lib.mk>
