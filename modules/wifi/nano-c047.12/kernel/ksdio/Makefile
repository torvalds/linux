# Common

SUBDIRS ?= $(shell pwd)
HOSTDRIVER_DIR = $(SUBDIRS)/../..
-include $(HOSTDRIVER_DIR)/platform/defs.inc
include $(HOSTDRIVER_DIR)/platform/$(PLATFORM)/platform.inc
include $(HOSTDRIVER_DIR)/common.inc

OBJ = kernel/ksdio/ksdio.o $(PLATFORM_OBJ)

# Invoked by kernel build system
ifneq ($(KERNELRELEASE),)

obj-m := nano_ksdio.o
nano_ksdio-objs = $(addprefix ../../, $(OBJ))

EXTRA_CFLAGS = $(addprefix -I$(HOSTDRIVER_DIR)/,$(WIFI_ENGINE_INC) kernel/ksdio $(NANO_IF_INC))
EXTRA_CFLAGS += $(WIFI_FLAGS) $(DEFS) $(DEBUG_FLAGS) $(PLATFORM_FLAGS)
EXTRA_CFLAGS += $(TRANSPORT_FLAGS)

# USE_WIFI should not be defined if we are building nano_ksdio.ko for loop back test.
ifneq ($(LOOPBACK_TEST), yes)
EXTRA_CFLAGS += -DUSE_WIFI
endif

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
	$(STRIP) -g nano_ksdio.ko
endif
