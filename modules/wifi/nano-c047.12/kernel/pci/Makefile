# Common

SUBDIRS ?= $(shell pwd)
HOSTDRIVER_DIR = $(SUBDIRS)/../..
-include $(HOSTDRIVER_DIR)/platform/defs.inc
include $(HOSTDRIVER_DIR)/platform/$(PLATFORM)/platform.inc
include $(HOSTDRIVER_DIR)/common.inc


# Invoked by kernel build system
ifneq ($(KERNELRELEASE),)

obj-m += nano_pci.o
ifeq ($(shell test $(SUBLEVEL) -ge 18 && echo y),y)
obj-m += nano_pci_cdev.o
endif

#nano_pci-objs = $(addprefix ../../, $(NANO_PCI_OBJ) $(PLATFORM_OBJ))

EXTRA_CFLAGS = $(addprefix -I$(HOSTDRIVER_DIR)/,$(WIFI_ENGINE_INC) $(NANO_PCI_INC) $(NANO_IF_INC))
EXTRA_CFLAGS += $(WIFI_FLAGS) $(DEFS) $(DEBUG_FLAGS) $(PLATFORM_FLAGS)
EXTRA_CFLAGS += $(TRANSPORT_FLAGS)

# Invoked from command line
else

all::
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(SUBDIRS) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(SUBDIRS) clean
	rm -f *.ko *~ *.mod
	rm -f $(addprefix $(HOSTDRIVER_DIR)/, $(NANO_PCI_OBJ))
	rm -f $(patsubst %.o,%.d, $(addprefix $(HOSTDRIVER_DIR)/, $(NANO_PCI_OBJ)))

strip:
	$(STRIP) -g nano_pci.ko
endif
