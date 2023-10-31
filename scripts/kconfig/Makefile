# SPDX-License-Identifier: GPL-2.0
# ===========================================================================
# Kernel configuration targets
# These targets are used from top-level makefile

ifdef KBUILD_KCONFIG
Kconfig := $(KBUILD_KCONFIG)
else
Kconfig := Kconfig
endif

ifndef KBUILD_DEFCONFIG
KBUILD_DEFCONFIG := defconfig
endif

ifeq ($(quiet),silent_)
silent := -s
endif

export KCONFIG_DEFCONFIG_LIST :=
ifndef cross_compiling
kernel-release := $(shell uname -r)
KCONFIG_DEFCONFIG_LIST += \
	/lib/modules/$(kernel-release)/.config \
	/etc/kernel-config \
	/boot/config-$(kernel-release)
endif
KCONFIG_DEFCONFIG_LIST += arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG)

# We need this, in case the user has it in its environment
unexport CONFIG_

config-prog	:= conf
menuconfig-prog	:= mconf
nconfig-prog	:= nconf
gconfig-prog	:= gconf
xconfig-prog	:= qconf

define config_rule
PHONY += $(1)
$(1): $(obj)/$($(1)-prog)
	$(Q)$$< $(silent) $(Kconfig)

PHONY += build_$(1)
build_$(1): $(obj)/$($(1)-prog)
endef

$(foreach c, config menuconfig nconfig gconfig xconfig, $(eval $(call config_rule,$(c))))

PHONY += localmodconfig localyesconfig
localyesconfig localmodconfig: $(obj)/conf
	$(Q)$(PERL) $(srctree)/$(src)/streamline_config.pl --$@ $(srctree) $(Kconfig) > .tmp.config
	$(Q)if [ -f .config ]; then 				\
		cmp -s .tmp.config .config ||			\
		(mv -f .config .config.old.1;			\
		 mv -f .tmp.config .config;			\
		 $< $(silent) --oldconfig $(Kconfig);		\
		 mv -f .config.old.1 .config.old)		\
	else							\
		mv -f .tmp.config .config;			\
		$< $(silent) --oldconfig $(Kconfig);		\
	fi
	$(Q)rm -f .tmp.config

# These targets map 1:1 to the commandline options of 'conf'
#
# Note:
#  syncconfig has become an internal implementation detail and is now
#  deprecated for external use
simple-targets := oldconfig allnoconfig allyesconfig allmodconfig \
	alldefconfig randconfig listnewconfig olddefconfig syncconfig \
	helpnewconfig yes2modconfig mod2yesconfig mod2noconfig

PHONY += $(simple-targets)

$(simple-targets): $(obj)/conf
	$(Q)$< $(silent) --$@ $(Kconfig)

PHONY += savedefconfig defconfig

savedefconfig: $(obj)/conf
	$(Q)$< $(silent) --$@=defconfig $(Kconfig)

defconfig: $(obj)/conf
ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG)),)
	@$(kecho) "*** Default configuration is based on '$(KBUILD_DEFCONFIG)'"
	$(Q)$< $(silent) --defconfig=arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG) $(Kconfig)
else
	@$(kecho) "*** Default configuration is based on target '$(KBUILD_DEFCONFIG)'"
	$(Q)$(MAKE) -f $(srctree)/Makefile $(KBUILD_DEFCONFIG)
endif

%_defconfig: $(obj)/conf
	$(Q)$< $(silent) --defconfig=arch/$(SRCARCH)/configs/$@ $(Kconfig)

configfiles = $(wildcard $(srctree)/kernel/configs/$(1) $(srctree)/arch/$(SRCARCH)/configs/$(1))
all-config-fragments = $(call configfiles,*.config)
config-fragments = $(call configfiles,$@)

%.config: $(obj)/conf
	$(if $(config-fragments),, $(error $@ fragment does not exists on this architecture))
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/kconfig/merge_config.sh -m .config $(config-fragments)
	$(Q)$(MAKE) -f $(srctree)/Makefile olddefconfig

PHONY += tinyconfig
tinyconfig:
	$(Q)KCONFIG_ALLCONFIG=kernel/configs/tiny-base.config $(MAKE) -f $(srctree)/Makefile allnoconfig
	$(Q)$(MAKE) -f $(srctree)/Makefile tiny.config

# CHECK: -o cache_dir=<path> working?
PHONY += testconfig
testconfig: $(obj)/conf
	$(Q)$(PYTHON3) -B -m pytest $(srctree)/$(src)/tests \
	-o cache_dir=$(abspath $(obj)/tests/.cache) \
	$(if $(findstring 1,$(KBUILD_VERBOSE)),--capture=no)
clean-files += tests/.cache

# Help text used by make help
help:
	@echo  'Configuration targets:'
	@echo  '  config	  - Update current config utilising a line-oriented program'
	@echo  '  nconfig         - Update current config utilising a ncurses menu based program'
	@echo  '  menuconfig	  - Update current config utilising a menu based program'
	@echo  '  xconfig	  - Update current config utilising a Qt based front-end'
	@echo  '  gconfig	  - Update current config utilising a GTK+ based front-end'
	@echo  '  oldconfig	  - Update current config utilising a provided .config as base'
	@echo  '  localmodconfig  - Update current config disabling modules not loaded'
	@echo  '                    except those preserved by LMC_KEEP environment variable'
	@echo  '  localyesconfig  - Update current config converting local mods to core'
	@echo  '                    except those preserved by LMC_KEEP environment variable'
	@echo  '  defconfig	  - New config with default from ARCH supplied defconfig'
	@echo  '  savedefconfig   - Save current config as ./defconfig (minimal config)'
	@echo  '  allnoconfig	  - New config where all options are answered with no'
	@echo  '  allyesconfig	  - New config where all options are accepted with yes'
	@echo  '  allmodconfig	  - New config selecting modules when possible'
	@echo  '  alldefconfig    - New config with all symbols set to default'
	@echo  '  randconfig	  - New config with random answer to all options'
	@echo  '  yes2modconfig	  - Change answers from yes to mod if possible'
	@echo  '  mod2yesconfig	  - Change answers from mod to yes if possible'
	@echo  '  mod2noconfig	  - Change answers from mod to no if possible'
	@echo  '  listnewconfig   - List new options'
	@echo  '  helpnewconfig   - List new options and help text'
	@echo  '  olddefconfig	  - Same as oldconfig but sets new symbols to their'
	@echo  '                    default value without prompting'
	@echo  '  tinyconfig	  - Configure the tiniest possible kernel'
	@echo  '  testconfig	  - Run Kconfig unit tests (requires python3 and pytest)'
	@echo  ''
	@echo  'Configuration topic targets:'
	@$(foreach f, $(all-config-fragments), \
		if help=$$(grep -m1 '^# Help: ' $(f)); then \
			printf '  %-25s - %s\n' '$(notdir $(f))' "$${help#*: }"; \
		fi;)

# ===========================================================================
# object files used by all kconfig flavours
common-objs	:= confdata.o expr.o lexer.lex.o menu.o parser.tab.o \
		   preprocess.o symbol.o util.o

$(obj)/lexer.lex.o: $(obj)/parser.tab.h
HOSTCFLAGS_lexer.lex.o	:= -I $(srctree)/$(src)
HOSTCFLAGS_parser.tab.o	:= -I $(srctree)/$(src)

# conf: Used for defconfig, oldconfig and related targets
hostprogs	+= conf
conf-objs	:= conf.o $(common-objs)

# nconf: Used for the nconfig target based on ncurses
hostprogs	+= nconf
nconf-objs	:= nconf.o nconf.gui.o $(common-objs)

HOSTLDLIBS_nconf       = $(call read-file, $(obj)/nconf-libs)
HOSTCFLAGS_nconf.o     = $(call read-file, $(obj)/nconf-cflags)
HOSTCFLAGS_nconf.gui.o = $(call read-file, $(obj)/nconf-cflags)

$(obj)/nconf: | $(obj)/nconf-libs
$(obj)/nconf.o $(obj)/nconf.gui.o: | $(obj)/nconf-cflags

# mconf: Used for the menuconfig target based on lxdialog
hostprogs	+= mconf
lxdialog	:= $(addprefix lxdialog/, \
		     checklist.o inputbox.o menubox.o textbox.o util.o yesno.o)
mconf-objs	:= mconf.o $(lxdialog) $(common-objs)

HOSTLDLIBS_mconf = $(call read-file, $(obj)/mconf-libs)
$(foreach f, mconf.o $(lxdialog), \
  $(eval HOSTCFLAGS_$f = $$(call read-file, $(obj)/mconf-cflags)))

$(obj)/mconf: | $(obj)/mconf-libs
$(addprefix $(obj)/, mconf.o $(lxdialog)): | $(obj)/mconf-cflags

# qconf: Used for the xconfig target based on Qt
hostprogs	+= qconf
qconf-cxxobjs	:= qconf.o qconf-moc.o
qconf-objs	:= images.o $(common-objs)

HOSTLDLIBS_qconf         = $(call read-file, $(obj)/qconf-libs)
HOSTCXXFLAGS_qconf.o     = -std=c++11 -fPIC $(call read-file, $(obj)/qconf-cflags)
HOSTCXXFLAGS_qconf-moc.o = -std=c++11 -fPIC $(call read-file, $(obj)/qconf-cflags)
$(obj)/qconf: | $(obj)/qconf-libs
$(obj)/qconf.o $(obj)/qconf-moc.o: | $(obj)/qconf-cflags

quiet_cmd_moc = MOC     $@
      cmd_moc = $(call read-file, $(obj)/qconf-bin)/moc $< -o $@

$(obj)/qconf-moc.cc: $(src)/qconf.h FORCE | $(obj)/qconf-bin
	$(call if_changed,moc)

targets += qconf-moc.cc

# gconf: Used for the gconfig target based on GTK+
hostprogs	+= gconf
gconf-objs	:= gconf.o images.o $(common-objs)

HOSTLDLIBS_gconf   = $(call read-file, $(obj)/gconf-libs)
HOSTCFLAGS_gconf.o = $(call read-file, $(obj)/gconf-cflags)

$(obj)/gconf: | $(obj)/gconf-libs
$(obj)/gconf.o: | $(obj)/gconf-cflags

# check if necessary packages are available, and configure build flags
cmd_conf_cfg = $< $(addprefix $(obj)/$*conf-, cflags libs bin); touch $(obj)/$*conf-bin

$(obj)/%conf-cflags $(obj)/%conf-libs $(obj)/%conf-bin: $(src)/%conf-cfg.sh
	$(call cmd,conf_cfg)

clean-files += *conf-cflags *conf-libs *conf-bin
