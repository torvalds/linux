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

EXT = -std=gnu99
INSTALL = install

# Use DESTDIR for installing into a different root directory.
# This is useful for building a package. The program will be
# installed in this directory as if it was the root directory.
# Then the build tool can move it later.
DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

prefix ?= /usr/local
bindir_relative = bin
bindir = $(prefix)/$(bindir_relative)
man_dir = $(prefix)/share/man
man_dir_SQ = '$(subst ','\'',$(man_dir))'

export man_dir man_dir_SQ INSTALL
export DESTDIR DESTDIR_SQ

set_plugin_dir := 1

# Set plugin_dir to preffered global plugin location
# If we install under $HOME directory we go under
# $(HOME)/.traceevent/plugins
#
# We dont set PLUGIN_DIR in case we install under $HOME
# directory, because by default the code looks under:
# $(HOME)/.traceevent/plugins by default.
#
ifeq ($(plugin_dir),)
ifeq ($(prefix),$(HOME))
override plugin_dir = $(HOME)/.traceevent/plugins
set_plugin_dir := 0
else
override plugin_dir = $(prefix)/lib/traceevent/plugins
endif
endif

ifeq ($(set_plugin_dir),1)
PLUGIN_DIR = -DPLUGIN_DIR="$(DESTDIR)/$(plugin_dir)"
PLUGIN_DIR_SQ = '$(subst ','\'',$(PLUGIN_DIR))'
endif

include $(if $(BUILD_SRC),$(BUILD_SRC)/)../../scripts/Makefile.include

# copy a bit from Linux kbuild

ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifndef VERBOSE
  VERBOSE = 0
endif

ifeq ("$(origin O)", "command line")
  BUILD_OUTPUT := $(O)
endif

ifeq ($(BUILD_SRC),)
ifneq ($(OUTPUT),)

define build_output
  $(if $(VERBOSE:1=),@)+$(MAKE) -C $(OUTPUT) \
  BUILD_SRC=$(CURDIR)/ -f $(CURDIR)/Makefile $1
endef

all: sub-make

$(MAKECMDGOALS): sub-make

sub-make: force
	$(call build_output, $(MAKECMDGOALS))


# Leave processing to above invocation of make
skip-makefile := 1

endif # OUTPUT
endif # BUILD_SRC

# We process the rest of the Makefile if this is the final invocation of make
ifeq ($(skip-makefile),)

srctree		:= $(if $(BUILD_SRC),$(BUILD_SRC),$(CURDIR))
objtree		:= $(CURDIR)
src		:= $(srctree)
obj		:= $(objtree)

export prefix bindir src obj

# Shell quotes
bindir_SQ = $(subst ','\'',$(bindir))
bindir_relative_SQ = $(subst ','\'',$(bindir_relative))
plugin_dir_SQ = $(subst ','\'',$(plugin_dir))

LIB_FILE = libtraceevent.a libtraceevent.so

CONFIG_INCLUDES = 
CONFIG_LIBS	=
CONFIG_FLAGS	=

VERSION		= $(EP_VERSION)
PATCHLEVEL	= $(EP_PATCHLEVEL)
EXTRAVERSION	= $(EP_EXTRAVERSION)

OBJ		= $@
N		=

export Q VERBOSE

EVENT_PARSE_VERSION = $(EP_VERSION).$(EP_PATCHLEVEL).$(EP_EXTRAVERSION)

INCLUDES = -I. -I $(srctree)/../../include $(CONFIG_INCLUDES)

# Set compile option CFLAGS if not set elsewhere
CFLAGS ?= -g -Wall

# Append required CFLAGS
override CFLAGS += $(CONFIG_FLAGS) $(INCLUDES) $(PLUGIN_DIR_SQ)
override CFLAGS += $(udis86-flags) -D_GNU_SOURCE

ifeq ($(VERBOSE),1)
  Q =
else
  Q = @
endif

do_compile_shared_library =			\
	($(print_shared_lib_compile)		\
	$(CC) --shared $^ -o $@)

do_plugin_build =				\
	($(print_plugin_build)			\
	$(CC) $(CFLAGS) -shared -nostartfiles -o $@ $<)

do_build_static_lib =				\
	($(print_static_lib_build)		\
	$(RM) $@;  $(AR) rcs $@ $^)


do_compile = $(QUIET_CC)$(CC) -c $(CFLAGS) $(EXT) $< -o $(obj)/$@;

$(obj)/%.o: $(src)/%.c
	$(call do_compile)

%.o: $(src)/%.c
	$(call do_compile)

PEVENT_LIB_OBJS  = event-parse.o
PEVENT_LIB_OBJS += event-plugin.o
PEVENT_LIB_OBJS += trace-seq.o
PEVENT_LIB_OBJS += parse-filter.o
PEVENT_LIB_OBJS += parse-utils.o
PEVENT_LIB_OBJS += kbuffer-parse.o

PLUGIN_OBJS  = plugin_jbd2.o
PLUGIN_OBJS += plugin_hrtimer.o
PLUGIN_OBJS += plugin_kmem.o
PLUGIN_OBJS += plugin_kvm.o
PLUGIN_OBJS += plugin_mac80211.o
PLUGIN_OBJS += plugin_sched_switch.o
PLUGIN_OBJS += plugin_function.o
PLUGIN_OBJS += plugin_xen.o
PLUGIN_OBJS += plugin_scsi.o
PLUGIN_OBJS += plugin_cfg80211.o

PLUGINS := $(PLUGIN_OBJS:.o=.so)

ALL_OBJS = $(PEVENT_LIB_OBJS) $(PLUGIN_OBJS)

CMD_TARGETS = $(LIB_FILE) $(PLUGINS)

TARGETS = $(CMD_TARGETS)


all: all_cmd

all_cmd: $(CMD_TARGETS)

libtraceevent.so: $(PEVENT_LIB_OBJS)
	$(QUIET_LINK)$(CC) --shared $^ -o $@

libtraceevent.a: $(PEVENT_LIB_OBJS)
	$(QUIET_LINK)$(RM) $@; $(AR) rcs $@ $^

plugins: $(PLUGINS)

$(PEVENT_LIB_OBJS): %.o: $(src)/%.c TRACEEVENT-CFLAGS
	$(QUIET_CC_FPIC)$(CC) -c $(CFLAGS) $(EXT) -fPIC $< -o $@

$(PLUGIN_OBJS): %.o : $(src)/%.c
	$(QUIET_CC_FPIC)$(CC) -c $(CFLAGS) -fPIC -o $@ $<

$(PLUGINS): %.so: %.o
	$(QUIET_LINK)$(CC) $(CFLAGS) -shared -nostartfiles -o $@ $<

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

## make deps

all_objs := $(sort $(ALL_OBJS))
all_deps := $(all_objs:%.o=.%.d)

# let .d file also depends on the source and header files
define check_deps
  @set -e; $(RM) $@; \
  $(CC) -MM $(CFLAGS) $< > $@.$$$$; \
  sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
  $(RM) $@.$$$$
endef

$(all_deps): .%.d: $(src)/%.c
	$(Q)$(call check_deps)

$(all_objs) : %.o : .%.d

dep_includes := $(wildcard $(all_deps))

ifneq ($(dep_includes),)
 include $(dep_includes)
endif

### Detect environment changes
TRACK_CFLAGS = $(subst ','\'',$(CFLAGS)):$(ARCH):$(CROSS_COMPILE)

TRACEEVENT-CFLAGS: force
	@FLAGS='$(TRACK_CFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat TRACEEVENT-CFLAGS 2>/dev/null`" ; then \
		echo 1>&2 "  FLAGS:   * new build flags or cross compiler"; \
		echo "$$FLAGS" >TRACEEVENT-CFLAGS; \
            fi

tags:	force
	$(RM) tags
	find . -name '*.[ch]' | xargs ctags --extra=+f --c-kinds=+px \
	--regex-c++='/_PE\(([^,)]*).*/PEVENT_ERRNO__\1/'

TAGS:	force
	$(RM) TAGS
	find . -name '*.[ch]' | xargs etags \
	--regex='/_PE(\([^,)]*\).*/PEVENT_ERRNO__\1/'

define do_install
	if [ ! -d '$(DESTDIR_SQ)$2' ]; then		\
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$2';	\
	fi;						\
	$(INSTALL) $1 '$(DESTDIR_SQ)$2'
endef

define do_install_plugins
	for plugin in $1; do				\
	  $(call do_install,$$plugin,$(plugin_dir_SQ));	\
	done
endef

install_lib: all_cmd install_plugins
	$(call QUIET_INSTALL, $(LIB_FILE)) \
		$(call do_install,$(LIB_FILE),$(bindir_SQ))

install_plugins: $(PLUGINS)
	$(call QUIET_INSTALL, trace_plugins) \
		$(call do_install_plugins, $(PLUGINS))

install: install_lib

clean:
	$(call QUIET_CLEAN, libtraceevent) \
		$(RM) *.o *~ $(TARGETS) *.a *.so $(VERSION_FILES) .*.d \
		$(RM) TRACEEVENT-CFLAGS tags TAGS

endif # skip-makefile

PHONY += force plugins
force:

plugins:
	@echo > /dev/null

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
