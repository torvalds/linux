# 5/10/2017
# $FreeBSD$

KMOD = smartpqi

.PATH:          ${SRCTOP}/sys/dev/${KMOD}

SRCS=smartpqi_mem.c smartpqi_intr.c smartpqi_main.c smartpqi_cam.c smartpqi_ioctl.c smartpqi_misc.c smartpqi_sis.c smartpqi_init.c smartpqi_queue.c smartpqi_tag.c smartpqi_cmd.c smartpqi_request.c smartpqi_response.c smartpqi_event.c smartpqi_helper.c smartpqi_discovery.c 

SRCS+=  device_if.h bus_if.h pci_if.h opt_scsi.h opt_cam.h

.include <bsd.kmod.mk>
