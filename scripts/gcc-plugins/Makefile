GCC_PLUGINS_DIR := $(shell $(CC) -print-file-name=plugin)

ifeq ($(PLUGINCC),$(HOSTCC))
  HOSTLIBS := hostlibs
  HOST_EXTRACFLAGS += -I$(GCC_PLUGINS_DIR)/include -I$(src) -std=gnu99 -ggdb
  export HOST_EXTRACFLAGS
else
  HOSTLIBS := hostcxxlibs
  HOST_EXTRACXXFLAGS += -I$(GCC_PLUGINS_DIR)/include -I$(src) -std=gnu++98 -fno-rtti
  HOST_EXTRACXXFLAGS += -fno-exceptions -fasynchronous-unwind-tables -ggdb
  HOST_EXTRACXXFLAGS += -Wno-narrowing -Wno-unused-variable
  export HOST_EXTRACXXFLAGS
endif

export GCCPLUGINS_DIR HOSTLIBS

ifneq ($(CFLAGS_KCOV), $(SANCOV_PLUGIN))
  GCC_PLUGIN := $(filter-out $(SANCOV_PLUGIN), $(GCC_PLUGIN))
endif

$(HOSTLIBS)-y := $(GCC_PLUGIN)
always := $($(HOSTLIBS)-y)

cyc_complexity_plugin-objs := cyc_complexity_plugin.o
sancov_plugin-objs := sancov_plugin.o

clean-files += *.so
