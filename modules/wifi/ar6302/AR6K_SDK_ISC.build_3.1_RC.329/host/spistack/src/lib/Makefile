
ifeq ($(CT_OS_TYPE),linux)

ifeq ($(CT_OS_SUB_TYPE),linux_2_4)
# EXTRA_FLAGS is not propogated from our top-level makefile, use the variable we setup
EXTRA_CFLAGS += $(CT_PASS_CFLAGS)
# define the target for the obj-y variable.
O_TARGET := sdio_lib.o 
# need export line for module SYMBOL exports
export-objs := linux/sdio_lib_os.o
# define object files that must be linked together
obj-y += sdio_lib_c.o sdio_lib_os.o
# define the objects to be created
obj-m += sdio_lib_c.o linux/sdio_lib_os.o sdio_lib.o 
#include rules make file 
include $(CT_OS_TOP_LEVEL_RULE)

else
# for all others
obj-m += sdio_lib.o
sdio_lib-objs := sdio_lib_c.o linux/sdio_lib_os.o
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


  