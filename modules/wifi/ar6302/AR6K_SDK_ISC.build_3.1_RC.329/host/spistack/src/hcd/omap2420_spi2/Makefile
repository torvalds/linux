ifeq ($(CT_OS_TYPE),linux)
ifneq ($(CT_OS_SUB_TYPE),linux_2_4)
obj-m += athspi_omap2420_hcd.o
athspi_omap2420_hcd-objs := sdio_hcd_raw_spi.o linux/sdio_hcd_raw_spi_os.o linux/raw_spi_plat_linux.o
endif

ifeq ($(CT_OS_SUB_TYPE),linux_2_4)
EXTRA_CFLAGS += $(CT_PASS_CFLAGS) -DATH_HCD_REQ_PARAM_CHECK
EXTRA_CFLAGS += -I$(CT_SRC_BASE)/hcd/athspi_common
EXTRA_CFLAGS += -I$(CT_SRC_BASE)/hcd/athspi_common/linux
O_TARGET := athspi_omap2420_hcd.o 
# need export line for module export
export-objs := linux/ath_spi_omap2420_hw.o 
obj-y += ath_spi_omap2420_hw.o \
		 $(CT_SRC_BASE)/hcd/athspi_common/ath_spi_hcd.o \
		 $(CT_SRC_BASE)/hcd/athspi_common/linux/ath_spi_linux.o
obj-m += linux/ath_spi_omap2420_hw.o \
		 $(CT_SRC_BASE)/hcd/athspi_common/ath_spi_hcd.o \
		 $(CT_SRC_BASE)/hcd/athspi_common/linux/ath_spi_linux.o athspi_omap2420_hcd.o 

include $(CT_OS_TOP_LEVEL_RULE) 
endif

endif

  