# $FreeBSD$
.PATH:	${SRCTOP}/sys/dev/mlx5/mlx5_core \
	${SRCTOP}/sys/dev/mlx5/mlx5_lib \
	${SRCTOP}/sys/dev/mlx5/mlx5_fpga

KMOD=mlx5
SRCS= \
mlx5_alloc.c \
mlx5_cmd.c \
mlx5_cq.c \
mlx5_diagnostics.c \
mlx5_eq.c \
mlx5_fs_cmd.c \
mlx5_fs_tree.c \
mlx5_fw.c \
mlx5_fwdump.c \
mlx5_fwdump_regmaps.c \
mlx5_health.c \
mlx5_mad.c \
mlx5_main.c \
mlx5_mcg.c \
mlx5_mr.c \
mlx5_pagealloc.c \
mlx5_pd.c \
mlx5_port.c \
mlx5_qp.c \
mlx5_rl.c \
mlx5_srq.c \
mlx5_transobj.c \
mlx5_uar.c \
mlx5_vport.c \
mlx5_vsc.c \
mlx5_wq.c \
mlx5_gid.c \
device_if.h bus_if.h vnode_if.h pci_if.h \
opt_inet.h opt_inet6.h opt_rss.h opt_ratelimit.h

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

.if defined(CONFIG_BUILD_FPGA)
SRCS+= \
	mlx5fpga_cmd.c \
	mlx5fpga_core.c \
	mlx5fpga_sdk.c \
	mlx5fpga_trans.c \
	mlx5fpga_xfer.c \
	mlx5fpga_ipsec.c
.endif

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith ${GCC_MS_EXTENSIONS}
