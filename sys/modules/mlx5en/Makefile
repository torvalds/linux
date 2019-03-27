# $FreeBSD$
.PATH:	${SRCTOP}/sys/dev/mlx5/mlx5_en

KMOD=mlx5en
SRCS= \
mlx5_en_ethtool.c \
mlx5_en_main.c \
mlx5_en_tx.c \
mlx5_en_flow_table.c \
mlx5_en_rx.c \
mlx5_en_rl.c \
mlx5_en_txrx.c \
device_if.h bus_if.h vnode_if.h pci_if.h \
opt_inet.h opt_inet6.h opt_rss.h opt_ratelimit.h

.if defined(HAVE_PER_CQ_EVENT_PACKET)
CFLAGS+= -DHAVE_PER_CQ_EVENT_PACKET
.endif

.if defined(HAVE_TCP_LRO_RX)
CFLAGS+= -DHAVE_TCP_LRO_RX
.endif

.if defined(CONFIG_BUILD_FPGA)
CFLAGS+= -DCONFIG_MLX5_FPGA
.endif

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith ${GCC_MS_EXTENSIONS}
