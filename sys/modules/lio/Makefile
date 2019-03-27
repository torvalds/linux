#$FreeBSD$

#.PATH:  ${.CURDIR}
.PATH: ${.CURDIR}/../../dev/liquidio
.PATH: ${.CURDIR}/../../dev/liquidio/base

KMOD = if_lio
CFLAGS += -I${.CURDIR}/../../dev/liquidio
CFLAGS += -I${.CURDIR}/../../dev/liquidio/base
CFLAGS += -DSMP

SRCS = device_if.h bus_if.h pci_if.h vnode_if.h opt_inet.h opt_inet6.h
SRCS += vnode_if_newproto.h vnode_if_typedef.h
SRCS += lio_main.c lio_sysctl.c lio_ioctl.c lio_core.c
SRCS += lio_rxtx.c lio_rss.c
SRCS += lio_device.c lio_ctrl.c
SRCS += lio_console.c cn23xx_pf_device.c
SRCS += lio_mem_ops.c lio_droq.c
SRCS += lio_response_manager.c lio_request_manager.c

FIRMWS = lio_23xx_nic.bin.fw:lio_23xx_nic.bin
CLEANFILES+= lio_23xx_nic.bin.fw

lio_23xx_nic.bin.fw: ${.CURDIR}/../../contrib/dev/liquidio/lio_23xx_nic.bin.uu
	uudecode -p ${.CURDIR}/../../contrib/dev/liquidio/lio_23xx_nic.bin.uu > ${.TARGET}

.include <bsd.kmod.mk>
