# $FreeBSD$
#
# Compile netmap as a module, useful if you want a netmap bridge
# or loadable drivers.

.include <bsd.own.mk> # FreeBSD 10 and earlier
# .include "${SYSDIR}/conf/kern.opts.mk"

.PATH: ${.CURDIR}/../../dev/netmap
.PATH.h: ${.CURDIR}/../../net
CFLAGS += -I${.CURDIR}/../../ -D INET -D VIMAGE
KMOD	= netmap
SRCS	= device_if.h bus_if.h pci_if.h opt_netmap.h
SRCS	+= netmap.c netmap.h netmap_kern.h
SRCS	+= netmap_mem2.c netmap_mem2.h
SRCS	+= netmap_generic.c
SRCS	+= netmap_mbq.c netmap_mbq.h
SRCS	+= netmap_vale.c
SRCS	+= netmap_freebsd.c
SRCS	+= netmap_offloadings.c
SRCS	+= netmap_pipe.c
SRCS	+= netmap_monitor.c
SRCS	+= netmap_kloop.c
SRCS	+= netmap_legacy.c
SRCS	+= netmap_bdg.c
SRCS	+= netmap_null.c
SRCS	+= if_ptnet.c
SRCS	+= opt_inet.h opt_inet6.h

.include <bsd.kmod.mk>
