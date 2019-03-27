# $FreeBSD$

# Makefile for the fwip(4) module (IP over IEEE1394)

.PATH: ${SRCTOP}/sys/dev/firewire ${SRCTOP}/sys/net

KMOD    = if_fwip
SRCS    = bus_if.h device_if.h \
          opt_inet.h opt_inet6.h opt_ofed.h \
          if_fwip.c if_fwipvar.h \
          firewire.h firewirereg.h \
          if_fwsubr.c

#CFLAGS += -DDEVICE_POLLING

.include <bsd.kmod.mk>
