# SPDX-License-Identifier: GPL-2.0
# trace-cmd version
EP_VERSION = 1
EP_PATCHLEVEL = 1
EP_EXTRAVERSION = 0

# file format version
FILE_VERSION = 6

MAKEFLAGS += --no-print-directory


# Makefiles suck: This macro sets a default value of $(2) for the
# variable named by $(1), unless the variable has been set by
# environment or command line. This is necessary for CC and AR
# because make sets default values, so the simpler ?= approach
# won't work as expected.
define allow-override
  $(if $(or $(findstring environment,$(origin $(1))),\
            $(findstring command line,$(origin $(1)))),,\
    $(eval $(1) = $(2)))
endef

# Allow setting CC and AR, or setting CROSS_COMPILE as a prefix.
$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,AR,$(CROSS_COMPILE)ar)
$(call allow-override,NM,$(CROSS_COMPILE)nm)

EXT = -std=gnu99
INSTALL = install

# Use DESTDIR for installing into a different root directory.
# This is useful for building a package. The program will be
# installed in this directory as if it was the root directory.
# Then the build tool can move it later.
DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

LP64 := $(shell echo __LP64__ | ${CC} ${CFLAGS} -E -x c - | tail -n 1)
ifeq ($(LP64), 1)
  libdir_relative = lib64
else
  libdir_relative = lib
endif

prefix ?= /usr/local
libdir = $(prefix)/$(libdir_relative)
man_dir = $(prefix)/share/man
man_dir_SQ = '$(subst ','\'',$(man_dir))'

export man_dir man_dir_SQ INSTALL
export DESTDIR DESTDIR_SQ

set_plugin_dir := 1

# Set plugin_dir to preffered global plugin location
# If we install under $HOME directory we go under
# $(HOME)/.local/lib/traceevent/plugins
#
# We dont set PLUGIN_DIR in case we install under $HOME
# directory, because by default the code looks under:
# $(HOME)/.local/lib/traceevent/plugins by default.
#
ifeq ($(plugin_dir),)
ifeq ($(prefix),$(HOME))
override plugin_dir = $(HOME)/.local/lib/traceevent/plugins
set_plugin_dir := 0
else
override plugin_dir = $(libdir)/traceevent/plugins
endif
endif

ifeq ($(set_plugin_dir),1)
PLUGIN_DIR = -DPLUGIN_DIR="$(plugin_dir)"
PLUGIN_DIR_SQ = '$(subst ','\'',$(PLUGIN_DIR))'
endif

include ../../scripts/Makefile.include

# copy a bit from Linux kbuild

ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifndef VERBOSE
  VERBOSE = 0
endif

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

export prefix libdir src obj

# Shell quotes
libdir_SQ = $(subst ','\'',$(libdir))
libdir_relative_SQ = $(subst ','\'',$(libdir_relative))
plugin_dir_SQ = $(subst ','\'',$(plugin_dir))

CONFIG_INCLUDES = 
CONFIG_LIBS	=
CONFIG_FLAGS	=

VERSION		= $(EP_VERSION)
PATCHLEVEL	= $(EP_PATCHLEVEL)
EXTRAVERSION	= $(EP_EXTRAVERSION)

OBJ		= $@
N		=

EVENT_PARSE_VERSION = $(EP_VERSION).$(EP_PATCHLEVEL).$(EP_EXTRAVERSION)

LIB_TARGET  = libtraceevent.a libtraceevent.so.$(EVENT_PARSE_VERSION)
LIB_INSTALL = libtraceevent.a libtraceevent.so*

INCLUDES = -I. -I $(srctree)/tools/include $(CONFIG_INCLUDES)

# Set compile option CFLAGS
ifdef EXTRA_CFLAGS
  CFLAGS := $(EXTRA_CFLAGS)
else
  CFLAGS := -g -Wall
endif

# Append required CFLAGS
override CFLAGS += -fPIC
override CFLAGS += $(CONFIG_FLAGS) $(INCLUDES) $(PLUGIN_DIR_SQ)
override CFLAGS += $(udis86-flags) -D_GNU_SOURCE

ifeq ($(VERBOSE),1)
  Q =
else
  Q = @
endif

# Disable command line variables (CFLAGS) override from top
# level Makefile (perf), otherwise build Makefile will get
# the same command line setup.
MAKEOVERRIDES=

export srctree OUTPUT CC LD CFLAGS V
build := -f $(srctree)/tools/build/Makefile.build dir=. obj

PLUGINS  = plugin_jbd2.so
PLUGINS += plugin_hrtimer.so
PLUGINS += plugin_kmem.so
PLUGINS += plugin_kvm.so
PLUGINS += plugin_mac80211.so
PLUGINS += plugin_sched_switch.so
PLUGINS += plugin_function.so
PLUGINS += plugin_xen.so
PLUGINS += plugin_scsi.so
PLUGINS += plugin_cfg80211.so

PLUGINS    := $(addprefix $(OUTPUT),$(PLUGINS))
PLUGINS_IN := $(PLUGINS:.so=-in.o)

TE_IN      := $(OUTPUT)libtraceevent-in.o
LIB_TARGET := $(addprefix $(OUTPUT),$(LIB_TARGET))
DYNAMIC_LIST_FILE := $(OUTPUT)libtraceevent-dynamic-list

CMD_TARGETS = $(LIB_TARGET) $(PLUGINS) $(DYNAMIC_LIST_FILE)

TARGETS = $(CMD_TARGETS)

all: all_cmd

all_cmd: $(CMD_TARGETS)

$(TE_IN): force
	$(Q)$(MAKE) $(build)=libtraceevent

$(OUTPUT)libtraceevent.so.$(EVENT_PARSE_VERSION): $(TE_IN)
	$(QUIET_LINK)$(CC) --shared $^ -Wl,-soname,libtraceevent.so.$(EP_VERSION) -o $@
	@ln -sf $(@F) $(OUTPUT)libtraceevent.so
	@ln -sf $(@F) $(OUTPUT)libtraceevent.so.$(EP_VERSION)

$(OUTPUT)libtraceevent.a: $(TE_IN)
	$(QUIET_LINK)$(RM) $@; $(AR) rcs $@ $^

$(OUTPUT)libtraceevent-dynamic-list: $(PLUGINS)
	$(QUIET_GEN)$(call do_generate_dynamic_list_file, $(PLUGINS), $@)

plugins: $(PLUGINS)

__plugin_obj = $(notdir $@)
  plugin_obj = $(__plugin_obj:-in.o=)

$(PLUGINS_IN): force
	$(Q)$(MAKE) $(build)=$(plugin_obj)

$(OUTPUT)%.so: $(OUTPUT)%-in.o
	$(QUIET_LINK)$(CC) $(CFLAGS) -shared -nostartfiles -o $@ $^

define make_version.h
  (echo '/* This file is automatically generated. Do not modify. */';		\
   echo \#define VERSION_CODE $(shell						\
   expr $(VERSION) \* 256 + $(PATCHLEVEL));					\
   echo '#define EXTRAVERSION ' $(EXTRAVERSION);				\
   echo '#define VERSION_STRING "'$(VERSION).$(PATCHLEVEL).$(EXTRAVERSION)'"';	\
   echo '#define FILE_VERSION '$(FILE_VERSION);					\
  ) > $1
endef

define update_version.h
  ($(call make_version.h, $@.tmp);		\
    if [ -r $@ ] && cmp -s $@ $@.tmp; then	\
      rm -f $@.tmp;				\
    else					\
      echo '  UPDATE                 $@';	\
      mv -f $@.tmp $@;				\
    fi);
endef

ep_version.h: force
	$(Q)$(N)$(call update_version.h)

VERSION_FILES = ep_version.h

define update_dir
  (echo $1 > $@.tmp;				\
   if [ -r $@ ] && cmp -s $@ $@.tmp; then	\
     rm -f $@.tmp;				\
   else						\
     echo '  UPDATE                 $@';	\
     mv -f $@.tmp $@;				\
   fi);
endef

tags:	force
	$(RM) tags
	find . -name '*.[ch]' | xargs ctags --extra=+f --c-kinds=+px \
	--regex-c++='/_PE\(([^,)]*).*/TEP_ERRNO__\1/'

TAGS:	force
	$(RM) TAGS
	find . -name '*.[ch]' | xargs etags \
	--regex='/_PE(\([^,)]*\).*/TEP_ERRNO__\1/'

define do_install_mkdir
	if [ ! -d '$(DESTDIR_SQ)$1' ]; then		\
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$1';	\
	fi
endef

define do_install
	$(call do_install_mkdir,$2);			\
	$(INSTALL) $(if $3,-m $3,) $1 '$(DESTDIR_SQ)$2'
endef

define do_install_plugins
	for plugin in $1; do				\
	  $(call do_install,$$plugin,$(plugin_dir_SQ));	\
	done
endef

define do_generate_dynamic_list_file
	symbol_type=`$(NM) -u -D $1 | awk 'NF>1 {print $$1}' | \
	xargs echo "U w W" | tr 'w ' 'W\n' | sort -u | xargs echo`;\
	if [ "$$symbol_type" = "U W" ];then				\
		(echo '{';						\
		$(NM) -u -D $1 | awk 'NF>1 {print "\t"$$2";"}' | sort -u;\
		echo '};';						\
		) > $2;							\
	else								\
		(echo Either missing one of [$1] or bad version of $(NM)) 1>&2;\
	fi
endef

install_lib: all_cmd install_plugins
	$(call QUIET_INSTALL, $(LIB_TARGET)) \
		$(call do_install_mkdir,$(libdir_SQ)); \
		cp -fpR $(LIB_INSTALL) $(DESTDIR)$(libdir_SQ)

install_plugins: $(PLUGINS)
	$(call QUIET_INSTALL, trace_plugins) \
		$(call do_install_plugins, $(PLUGINS))

install_headers:
	$(call QUIET_INSTALL, headers) \
		$(call do_install,event-parse.h,$(prefix)/include/traceevent,644); \
		$(call do_install,event-utils.h,$(prefix)/include/traceevent,644); \
		$(call do_install,kbuffer.h,$(prefix)/include/traceevent,644)

install: install_lib

clean:
	$(call QUIET_CLEAN, libtraceevent) \
		$(RM) *.o *~ $(TARGETS) *.a *.so $(VERSION_FILES) .*.d .*.cmd \
		$(RM) TRACEEVENT-CFLAGS tags TAGS

PHONY += force plugins
force:

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
