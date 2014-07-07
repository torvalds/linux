# file format version
FILE_VERSION = 1

LIBLOCKDEP_VERSION=$(shell make --no-print-directory -sC ../../.. kernelversion)

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

INSTALL = install

# Use DESTDIR for installing into a different root directory.
# This is useful for building a package. The program will be
# installed in this directory as if it was the root directory.
# Then the build tool can move it later.
DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

prefix ?= /usr/local
libdir_relative = lib
libdir = $(prefix)/$(libdir_relative)
bindir_relative = bin
bindir = $(prefix)/$(bindir_relative)

export DESTDIR DESTDIR_SQ INSTALL

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
ifneq ($(BUILD_OUTPUT),)

define build_output
	$(if $(VERBOSE:1=),@)$(MAKE) -C $(BUILD_OUTPUT)	\
	BUILD_SRC=$(CURDIR) -f $(CURDIR)/Makefile $1
endef

saved-output := $(BUILD_OUTPUT)
BUILD_OUTPUT := $(shell cd $(BUILD_OUTPUT) && /bin/pwd)
$(if $(BUILD_OUTPUT),, \
     $(error output directory "$(saved-output)" does not exist))

all: sub-make

gui: force
	$(call build_output, all_cmd)

$(filter-out gui,$(MAKECMDGOALS)): sub-make

sub-make: force
	$(call build_output, $(MAKECMDGOALS))


# Leave processing to above invocation of make
skip-makefile := 1

endif # BUILD_OUTPUT
endif # BUILD_SRC

# We process the rest of the Makefile if this is the final invocation of make
ifeq ($(skip-makefile),)

srctree		:= $(realpath $(if $(BUILD_SRC),$(BUILD_SRC),$(CURDIR)))
objtree		:= $(realpath $(CURDIR))
src		:= $(srctree)
obj		:= $(objtree)

export prefix libdir bindir src obj

# Shell quotes
libdir_SQ = $(subst ','\'',$(libdir))
bindir_SQ = $(subst ','\'',$(bindir))

LIB_FILE = liblockdep.a liblockdep.so.$(LIBLOCKDEP_VERSION)
BIN_FILE = lockdep

CONFIG_INCLUDES =
CONFIG_LIBS	=
CONFIG_FLAGS	=

OBJ		= $@
N		=

export Q VERBOSE

INCLUDES = -I. -I/usr/local/include -I./uinclude -I./include -I../../include $(CONFIG_INCLUDES)

# Set compile option CFLAGS if not set elsewhere
CFLAGS ?= -g -DCONFIG_LOCKDEP -DCONFIG_STACKTRACE -DCONFIG_PROVE_LOCKING -DBITS_PER_LONG=__WORDSIZE -DLIBLOCKDEP_VERSION='"$(LIBLOCKDEP_VERSION)"' -rdynamic -O0 -g

override CFLAGS += $(CONFIG_FLAGS) $(INCLUDES) $(PLUGIN_DIR_SQ)

ifeq ($(VERBOSE),1)
  Q =
  print_compile =
  print_app_build =
  print_fpic_compile =
  print_shared_lib_compile =
  print_install =
else
  Q = @
  print_compile =		echo '  CC                 '$(OBJ);
  print_app_build =		echo '  BUILD              '$(OBJ);
  print_fpic_compile =		echo '  CC FPIC            '$(OBJ);
  print_shared_lib_compile =	echo '  BUILD SHARED LIB   '$(OBJ);
  print_static_lib_build =	echo '  BUILD STATIC LIB   '$(OBJ);
  print_install =		echo '  INSTALL     '$1'	to	$(DESTDIR_SQ)$2';
endif

do_fpic_compile =					\
	($(print_fpic_compile)				\
	$(CC) -c $(CFLAGS) $(EXT) -fPIC $< -o $@)

do_app_build =						\
	($(print_app_build)				\
	$(CC) $^ -rdynamic -o $@ $(CONFIG_LIBS) $(LIBS))

do_compile_shared_library =			\
	($(print_shared_lib_compile)		\
	$(CC) --shared $^ -o $@ -lpthread -ldl -Wl,-soname='"$@"';$(shell ln -s $@ liblockdep.so))

do_build_static_lib =				\
	($(print_static_lib_build)		\
	$(RM) $@;  $(AR) rcs $@ $^)


define do_compile
	$(print_compile)						\
	$(CC) -c $(CFLAGS) $(EXT) $< -o $(obj)/$@;
endef

$(obj)/%.o: $(src)/%.c
	$(Q)$(call do_compile)

%.o: $(src)/%.c
	$(Q)$(call do_compile)

PEVENT_LIB_OBJS = common.o lockdep.o preload.o rbtree.o

ALL_OBJS = $(PEVENT_LIB_OBJS)

CMD_TARGETS = $(LIB_FILE)

TARGETS = $(CMD_TARGETS)


all: all_cmd

all_cmd: $(CMD_TARGETS)

liblockdep.so.$(LIBLOCKDEP_VERSION): $(PEVENT_LIB_OBJS)
	$(Q)$(do_compile_shared_library)

liblockdep.a: $(PEVENT_LIB_OBJS)
	$(Q)$(do_build_static_lib)

$(PEVENT_LIB_OBJS): %.o: $(src)/%.c
	$(Q)$(do_fpic_compile)

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

tags:	force
	$(RM) tags
	find . -name '*.[ch]' | xargs ctags --extra=+f --c-kinds=+px \
	--regex-c++='/_PE\(([^,)]*).*/PEVENT_ERRNO__\1/'

TAGS:	force
	$(RM) TAGS
	find . -name '*.[ch]' | xargs etags \
	--regex='/_PE(\([^,)]*\).*/PEVENT_ERRNO__\1/'

define do_install
	$(print_install)				\
	if [ ! -d '$(DESTDIR_SQ)$2' ]; then		\
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$2';	\
	fi;						\
	$(INSTALL) $1 '$(DESTDIR_SQ)$2'
endef

install_lib: all_cmd
	$(Q)$(call do_install,$(LIB_FILE),$(libdir_SQ))
	$(Q)$(call do_install,$(BIN_FILE),$(bindir_SQ))

install: install_lib

clean:
	$(RM) *.o *~ $(TARGETS) *.a *liblockdep*.so* $(VERSION_FILES) .*.d
	$(RM) tags TAGS

endif # skip-makefile

PHONY += force
force:

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
