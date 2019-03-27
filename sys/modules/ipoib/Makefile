# $FreeBSD$
.PATH:  ${SRCTOP}/sys/ofed/drivers/infiniband/ulp/ipoib

KMOD=	ipoib
SRCS=	device_if.h bus_if.h vnode_if.h pci_if.h \
	opt_inet.h opt_inet6.h opt_ofed.h \
	ipoib_cm.c ipoib_ib.c ipoib_main.c \
	ipoib_multicast.c ipoib_verbs.c

CFLAGS+= -I${SRCTOP}/sys/ofed/drivers/infiniband/ulp/ipoib
CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/ofed/include/uapi
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith
