ifeq ($(CT_OS_TYPE),linux)
##LINUX
ifneq ($(CT_OS_SUB_TYPE),linux_2_4)
obj-m += sdio_busdriver.o
sdio_busdriver-objs := sdio_bus.o sdio_function.o sdio_bus_misc.o sdio_bus_events.o linux/sdio_bus_os.o
endif

ifeq ($(CT_OS_SUB_TYPE),linux_2_4)
# reset CFLAGs, for some reason on 2.4.20, the EXTRA_FLAGS is not propogated from our top-level makefile
EXTRA_CFLAGS += $(CT_PASS_CFLAGS)
O_TARGET := sdio_busdriver.o 
# need export line for module export
export-objs := linux/sdio_bus_os.o
obj-y += sdio_bus.o sdio_function.o sdio_bus_misc.o sdio_bus_events.o sdio_bus_os.o
obj-m += sdio_bus.o sdio_function.o sdio_bus_misc.o sdio_bus_events.o linux/sdio_bus_os.o sdio_busdriver.o
include $(CT_OS_TOP_LEVEL_RULE)

endif

endif

ifeq ($(CT_OS_TYPE),qnx)
##QNX
LIST=VARIANT
ifndef QRECURSE
QRECURSE=recurse.mk
ifdef QCONFIG
QRDIR=$(dir $(QCONFIG))
endif
endif
include $(QRDIR)$(QRECURSE)
endif


  