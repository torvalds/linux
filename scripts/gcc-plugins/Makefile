# SPDX-License-Identifier: GPL-2.0
GCC_PLUGINS_DIR := $(shell $(CC) -print-file-name=plugin)

HOST_EXTRACXXFLAGS += -I$(GCC_PLUGINS_DIR)/include -I$(src) -std=gnu++98 -fno-rtti
HOST_EXTRACXXFLAGS += -fno-exceptions -fasynchronous-unwind-tables -ggdb
HOST_EXTRACXXFLAGS += -Wno-narrowing -Wno-unused-variable -Wno-c++11-compat
HOST_EXTRACXXFLAGS += -Wno-format-diag

$(obj)/randomize_layout_plugin.o: $(objtree)/$(obj)/randomize_layout_seed.h
quiet_cmd_create_randomize_layout_seed = GENSEED $@
cmd_create_randomize_layout_seed = \
  $(CONFIG_SHELL) $(srctree)/$(src)/gen-random-seed.sh $@ $(objtree)/include/generated/randomize_layout_hash.h
$(objtree)/$(obj)/randomize_layout_seed.h: FORCE
	$(call if_changed,create_randomize_layout_seed)
targets = randomize_layout_seed.h randomize_layout_hash.h

hostcxxlibs-y := $(foreach p,$(GCC_PLUGIN),$(if $(findstring /,$(p)),,$(p)))
always-y := $(hostcxxlibs-y)

$(foreach p,$(hostcxxlibs-y:%.so=%),$(eval $(p)-objs := $(p).o))

clean-files += *.so
