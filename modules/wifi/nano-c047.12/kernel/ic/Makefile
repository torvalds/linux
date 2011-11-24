# Common

SUBDIRS ?= $(shell pwd)
HOSTDRIVER_DIR = $(SUBDIRS)/../..
-include $(HOSTDRIVER_DIR)/platform/defs.inc
include $(HOSTDRIVER_DIR)/platform/$(PLATFORM)/platform.inc
include $(HOSTDRIVER_DIR)/common.inc
OBJ = $(WIFI_ENGINE_OBJ) $(CCX_OBJ) $(NANO_IF_OBJ)


# Invoked by kernel build system
ifneq ($(KERNELRELEASE),)

obj-m := nano_if.o
nano_if-objs = $(addprefix ../../, $(OBJ))


EXTRA_CFLAGS = $(addprefix -I$(HOSTDRIVER_DIR)/,$(WIFI_ENGINE_INC) $(NANO_SDIO_INC) $(NANO_IF_INC))
EXTRA_CFLAGS += $(WIFI_FLAGS) $(DEFS) $(DEBUG_FLAGS) $(PLATFORM_FLAGS)

# Invoked from command line
else

all::
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(SUBDIRS) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(SUBDIRS) clean
	rm -f *.ko *~ *.mod
	rm -f $(addprefix $(HOSTDRIVER_DIR)/, $(OBJ))
	rm -f $(patsubst %.o,%.d, $(addprefix $(HOSTDRIVER_DIR)/, $(OBJ)))

strip:
	$(STRIP) -g nano_if.ko

endif
