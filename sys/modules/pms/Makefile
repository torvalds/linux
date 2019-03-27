# $FreeBSD$
# 5/31/2012
#PATH shall include all directories

.PATH: 	${SRCTOP}/sys/dev/pms/freebsd/driver/ini/src \
	${SRCTOP}/sys/dev/pms/RefTisa/sallsdk/api \
	${SRCTOP}/sys/dev/pms/RefTisa/sallsdk/spc \
	${SRCTOP}/sys/dev/pms/RefTisa/sallsdk/hda/64k \
	${SRCTOP}/sys/dev/pms/RefTisa/discovery/api \
	${SRCTOP}/sys/dev/pms/RefTisa/discovery/dm \
	${SRCTOP}/sys/dev/pms/RefTisa/sat/api \
	${SRCTOP}/sys/dev/pms/RefTisa/sat/src \
	${SRCTOP}/sys/dev/pms/RefTisa/tisa/api \
	${SRCTOP}/sys/dev/pms/RefTisa/tisa/sassata/common \
	${SRCTOP}/sys/dev/pms/RefTisa/tisa/sassata/sas/common \
	${SRCTOP}/sys/dev/pms/RefTisa/tisa/sassata/sas/ini \
	${SRCTOP}/sys/dev/pms/RefTisa/tisa/sassata/sata/host \
	${SRCTOP}/sys/dev/pms/freebsd/driver/common

CFLAGS+=-fno-builtin
CFLAGS+=-fms-extensions

CFLAGS+=-Wredundant-decls
CFLAGS+=-Wunused-variable
CFLAGS+=-Wnested-externs
CFLAGS+=-Woverflow
CFLAGS+=-Wmissing-prototypes
CFLAGS+=-Wparentheses
CFLAGS+=-w

#CFLAGS+=-g


#SUBDIR= tisa tisa/drivers tisa/drivers/FreeBSD tisa/drivers/FreeBSD/ini tisa/drivers/FreeBSD/ini/src
#SUBDIR= Hialeah_driver Hialeah_driver/freebsd Hialeah_driver/freebsd/ini Hialeah_driver/freebsd/ini/src 

KMOD=	pmspcv
#OS files
SRCS=agtiapi.c

#LL files
SRCS+= sadisc.c mpi.c saframe.c sahw.c sainit.c saint.c sampicmd.c sampirsp.c saphy.c saport.c
SRCS+= sasata.c sasmp.c sassp.c satimer.c sautil.c saioctlcmd.c mpidebug.c
#DM files
SRCS+= dminit.c dmsmp.c dmdisc.c dmport.c dmtimer.c dmmisc.c
#SM files
SRCS+= sminit.c smmisc.c smsat.c smsatcb.c smsathw.c smtimer.c
#TD files
SRCS+= tdinit.c tdmisc.c tdesgl.c tdport.c tdint.c tdioctl.c tdhw.c ossacmnapi.c tddmcmnapi.c tdsmcmnapi.c
SRCS+= tdtimers.c itdio.c itdcb.c itdinit.c itddisc.c sat.c ossasat.c sathw.c

#header files
SRCS+=	agtiapi.h agtiproto.h agdef.h
#
SRCS+=	osenv.h ostypes.h
SRCS+=  opt_scsi.h opt_cam.h
SRCS+=  device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
