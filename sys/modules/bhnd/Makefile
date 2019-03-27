# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/bhnd
.PATH: ${SRCTOP}/sys/dev/bhnd/cores/chipc
.PATH: ${SRCTOP}/sys/dev/bhnd/cores/chipc/pwrctl
.PATH: ${SRCTOP}/sys/dev/bhnd/cores/pmu
.PATH: ${SRCTOP}/sys/dev/bhnd/nvram

KMOD=	bhnd
SRCS=	bhnd.c bhnd_subr.c
SRCS+=	bhnd_bus_if.c bhnd_bus_if.h

SRCS+=	bhnd_erom.c
SRCS+=	bhnd_erom_if.c bhnd_erom_if.h

# ChipCommon
SRCS+=	chipc.c chipc_subr.c

SRCS+=	chipc_gpio.c
SRCS+=	gpio_if.h ofw_bus_if.h
SRCS+=	opt_platform.h

SRCS+=	bhnd_sprom_chipc.c \
	bhnd_pmu_chipc.c
	
SRCS+=	bhnd_pwrctl.c \
	bhnd_pwrctl_subr.c \
	bhnd_pwrctl_if.c \
	bhnd_pwrctl_if.h
SRCS+=	bhnd_pwrctl_hostb_if.c bhnd_pwrctl_hostb_if.h

SRCS+=	bhnd_chipc_if.c bhnd_chipc_if.h

# PMU
SRCS+=	bhnd_pmu.c \
	bhnd_pmu_core.c \
	bhnd_pmu_subr.c
SRCS+=	bhnd_pmu_if.c bhnd_pmu_if.h

# NVRAM/SPROM
SRCS+=	bhnd_nvram_data.c \
	bhnd_nvram_data_bcm.c \
	bhnd_nvram_data_bcmraw.c \
	bhnd_nvram_data_btxt.c \
	bhnd_nvram_data_sprom.c \
	bhnd_nvram_data_sprom_subr.c \
	bhnd_nvram_data_tlv.c \
	bhnd_nvram_io.c \
	bhnd_nvram_iobuf.c \
	bhnd_nvram_ioptr.c \
	bhnd_nvram_iores.c \
	bhnd_nvram_plist.c \
	bhnd_nvram_store.c \
	bhnd_nvram_store_subr.c \
	bhnd_nvram_subr.c \
	bhnd_nvram_value.c \
	bhnd_nvram_value_fmts.c \
	bhnd_nvram_value_prf.c \
	bhnd_nvram_value_subr.c \
	bhnd_sprom.c
SRCS+=	bhnd_nvram_map.h bhnd_nvram_map_data.h
SRCS+=	bhnd_nvram_if.c bhnd_nvram_if.h

SRCS+=	device_if.h bus_if.h

SUBDIR=	bcma \
	bcma_bhndb \
	bhndb \
	bhndb_pci \
	cores \
	siba \
	siba_bhndb

.include <bsd.kmod.mk>
.include <bsd.subdir.mk>
