#
# $FreeBSD$
#

CXGBE=	${SRCTOP}/sys/dev/cxgbe
.PATH: ${CXGBE} ${CXGBE}/common ${CXGBE}/cudbg

KMOD=	if_cxgbe
SRCS=	bus_if.h
SRCS+=	device_if.h
SRCS+=	opt_ddb.h
SRCS+=	opt_inet.h
SRCS+=	opt_inet6.h
SRCS+=	opt_ofed.h
SRCS+=	opt_ratelimit.h
SRCS+=	opt_rss.h
SRCS+=	pci_if.h pci_iov_if.h
SRCS+=	t4_clip.c
SRCS+=	t4_filter.c
SRCS+=	t4_hw.c
SRCS+=	t4_if.c t4_if.h
SRCS+=	t4_iov.c
SRCS+=	t4_l2t.c
SRCS+=	t4_main.c
SRCS+=	t4_mp_ring.c
SRCS+=	t4_netmap.c
SRCS+=	t4_sched.c
SRCS+=	t4_sge.c
SRCS+=	t4_smt.c
SRCS+=	t4_tracer.c
SRCS+=  cudbg_common.c
SRCS+=  cudbg_flash_utils.c
SRCS+=  cudbg_lib.c
SRCS+=  cudbg_wtp.c
SRCS+=  fastlz_api.c
SRCS+=  fastlz.c

CFLAGS+= -I${CXGBE}

.include <bsd.kmod.mk>
