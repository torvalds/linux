# SPDX-License-Identifier: GPL-2.0

$(obj)/randomize_layout_plugin.so: $(objtree)/$(obj)/randomize_layout_seed.h
quiet_cmd_create_randomize_layout_seed = GENSEED $@
cmd_create_randomize_layout_seed = \
  $(CONFIG_SHELL) $(srctree)/$(src)/gen-random-seed.sh $@ $(objtree)/include/generated/randomize_layout_hash.h
$(objtree)/$(obj)/randomize_layout_seed.h: FORCE
	$(call if_changed,create_randomize_layout_seed)
targets += randomize_layout_seed.h randomize_layout_hash.h

# Build rules for plugins
#
# No extra code is needed for single-file plugins.
# For multi-file plugins, use *-objs syntax to list the objects.
#
# If the plugin foo.so is compiled from foo.c and foo2.c, you can do:
#
# foo-objs := foo.o foo2.o

always-y += $(GCC_PLUGIN)

GCC_PLUGINS_DIR = $(shell $(CC) -print-file-name=plugin)

plugin_cxxflags	= -Wp,-MMD,$(depfile) $(KBUILD_HOSTCXXFLAGS) -fPIC \
		   -I $(GCC_PLUGINS_DIR)/include -I $(obj) -std=gnu++98 \
		   -fno-rtti -fno-exceptions -fasynchronous-unwind-tables \
		   -ggdb -Wno-narrowing -Wno-unused-variable -Wno-c++11-compat \
		   -Wno-format-diag

plugin_ldflags	= -shared

plugin-single	:= $(foreach m, $(GCC_PLUGIN), $(if $($(m:%.so=%-objs)),,$(m)))
plugin-multi	:= $(filter-out $(plugin-single), $(GCC_PLUGIN))
plugin-objs	:= $(sort $(foreach m, $(plugin-multi), $($(m:%.so=%-objs))))

targets += $(plugin-single) $(plugin-multi) $(plugin-objs)
clean-files += *.so

plugin-single	:= $(addprefix $(obj)/, $(plugin-single))
plugin-multi	:= $(addprefix $(obj)/, $(plugin-multi))
plugin-objs	:= $(addprefix $(obj)/, $(plugin-objs))

quiet_cmd_plugin_cxx_so_c = HOSTCXX $@
      cmd_plugin_cxx_so_c = $(HOSTCXX) $(plugin_cxxflags) $(plugin_ldflags) -o $@ $<

$(plugin-single): $(obj)/%.so: $(src)/%.c FORCE
	$(call if_changed_dep,plugin_cxx_so_c)

quiet_cmd_plugin_ld_so_o = HOSTLD  $@
      cmd_plugin_ld_so_o = $(HOSTCXX) $(plugin_ldflags) -o $@ \
			   $(addprefix $(obj)/, $($(target-stem)-objs))

$(plugin-multi): FORCE
	$(call if_changed,plugin_ld_so_o)
$(foreach m, $(notdir $(plugin-multi)), $(eval $(obj)/$m: $(addprefix $(obj)/, $($(m:%.so=%-objs)))))

quiet_cmd_plugin_cxx_o_c = HOSTCXX $@
      cmd_plugin_cxx_o_c = $(HOSTCXX) $(plugin_cxxflags) -c -o $@ $<

$(plugin-objs): $(obj)/%.o: $(src)/%.c FORCE
	$(call if_changed_dep,plugin_cxx_o_c)
