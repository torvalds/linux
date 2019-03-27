# $FreeBSD$
.PATH:	${SRCTOP}/sys/dev/mlx5/mlx5_fpga_tools

KMOD=mlx5fpga_tools
SRCS= \
	mlx5fpga_tools_main.c \
	mlx5fpga_tools_char.c

SRCS+= \
	device_if.h bus_if.h vnode_if.h pci_if.h \
	opt_inet.h opt_inet6.h opt_rss.h opt_ratelimit.h

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith ${GCC_MS_EXTENSIONS}
CFLAGS+= -DCONFIG_MLX5_FPGA -DCONFIG_MLX5_ACCEL
