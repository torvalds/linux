# $FreeBSD$
.PATH:  ${SRCTOP}/sys/dev/mlx4/mlx4_en

KMOD=	mlx4en
SRCS=	device_if.h bus_if.h pci_if.h vnode_if.h \
	opt_inet.h opt_inet6.h \
	mlx4_en_cq.c \
	mlx4_en_main.c \
	mlx4_en_netdev.c \
	mlx4_en_port.c \
	mlx4_en_resources.c \
	mlx4_en_rx.c \
	mlx4_en_tx.c

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

.include <bsd.kmod.mk>
