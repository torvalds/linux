# $FreeBSD$

.include <bsd.init.mk>

.PATH:		${SYSDIR}/contrib/libfdt/

LIB=		fdt

# Vendor sources of libfdt.
SRCS+=		fdt.c fdt_ro.c fdt_wip.c fdt_sw.c fdt_rw.c fdt_strerror.c \
		fdt_empty_tree.c fdt_addresses.c fdt_overlay.c

# Loader's fdt commands extension sources.
SRCS+=		fdt_loader_cmd.c

CFLAGS+=	-I${SYSDIR}/contrib/libfdt/ -I${LDRSRC}

CFLAGS+=	-Wformat -Wall

.include <bsd.lib.mk>
