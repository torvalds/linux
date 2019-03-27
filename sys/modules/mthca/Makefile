# $FreeBSD$
.PATH:	${SRCTOP}/sys/dev/mthca

KMOD=	mthca
SRCS= \
mthca_allocator.c \
mthca_av.c \
mthca_catas.c \
mthca_cmd.c \
mthca_cq.c \
mthca_eq.c \
mthca_mad.c \
mthca_main.c \
mthca_mcg.c \
mthca_memfree.c \
mthca_mr.c \
mthca_pd.c \
mthca_profile.c \
mthca_provider.c \
mthca_qp.c \
mthca_reset.c \
mthca_srq.c \
mthca_uar.c \
device_if.h bus_if.h vnode_if.h pci_if.h \
        opt_inet.h opt_inet6.h

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/ofed/include/uapi
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include
CFLAGS+= -DCONFIG_INFINIBAND_USER_MEM
CFLAGS+= -DINET6 -DINET

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith
