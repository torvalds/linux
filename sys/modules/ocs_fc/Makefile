# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/ocs_fc
KMOD = ocs_fc

SRCS = \
	device_if.h \
	bus_if.h \
	pci_if.h \
	opt_scsi.h \
	opt_stack.h \
	opt_cam.h

# OS
SRCS += ocs_pci.c ocs_ioctl.c ocs_os.c ocs_utils.c

# hw
SRCS += ocs_hw.c ocs_hw_queues.c

# SLI
SRCS += sli4.c ocs_sm.c

# Transport
SRCS += \
	ocs_device.c \
	ocs_xport.c \
	ocs_domain.c \
	ocs_sport.c \
	ocs_els.c \
	ocs_fabric.c \
	ocs_io.c \
	ocs_node.c \
	ocs_scsi.c \
	ocs_unsol.c \
	ocs_ddump.c \
	ocs_mgmt.c 


# CAM initiator/target
SRCS += ocs_cam.c

CINCS = -I. 

CLEANFILES += ${PROG}.debug ${PROG}.symbols cscope.* .depend.*

.include <bsd.kmod.mk>
