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

# Allow setting CC and AR and LD, or setting CROSS_COMPILE as a prefix.
$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,AR,$(CROSS_COMPILE)ar)
$(call allow-override,LD,$(CROSS_COMPILE)ld)

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

MAKEFLAGS += --no-print-directory

include ../../scripts/Makefile.include

# copy a bit from Linux kbuild

ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifndef VERBOSE
  VERBOSE = 0
endif

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(shell pwd)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

# Shell quotes
libdir_SQ = $(subst ','\'',$(libdir))
bindir_SQ = $(subst ','\'',$(bindir))

LIB_IN := $(OUTPUT)liblockdep-in.o

BIN_FILE = lockdep
LIB_FILE = $(OUTPUT)liblockdep.a $(OUTPUT)liblockdep.so.$(LIBLOCKDEP_VERSION)

CONFIG_INCLUDES =
CONFIG_LIBS	=
CONFIG_FLAGS	=

OBJ		= $@
N		=

export Q VERBOSE

INCLUDES = -I. -I./uinclude -I./include -I../../include $(CONFIG_INCLUDES)

# Set compile option CFLAGS if not set elsewhere
CFLAGS ?= -g -DCONFIG_LOCKDEP -DCONFIG_STACKTRACE -DCONFIG_PROVE_LOCKING -DBITS_PER_LONG=__WORDSIZE -DLIBLOCKDEP_VERSION='"$(LIBLOCKDEP_VERSION)"' -rdynamic -O0 -g
CFLAGS += -fPIC

override CFLAGS += $(CONFIG_FLAGS) $(INCLUDES) $(PLUGIN_DIR_SQ)

ifeq ($(VERBOSE),1)
  Q =
  print_shared_lib_compile =
  print_install =
else
  Q = @
  print_shared_lib_compile =	echo '  LD       '$(OBJ);
  print_static_lib_build =	echo '  LD       '$(OBJ);
  print_install =		echo '  INSTALL  '$1'	to	$(DESTDIR_SQ)$2';
endif

all:

export srctree OUTPUT CC LD CFLAGS V
include $(srctree)/tools/build/Makefile.include

do_compile_shared_library =			\
	($(print_shared_lib_compile)		\
	$(CC) --shared $^ -o $@ -lpthread -ldl -Wl,-soname='"$@"';$(shell ln -sf $@ liblockdep.so))

do_build_static_lib =				\
	($(print_static_lib_build)		\
	$(RM) $@;  $(AR) rcs $@ $^)

CMD_TARGETS = $(LIB_FILE)

TARGETS = $(CMD_TARGETS)


all: fixdep all_cmd

all_cmd: $(CMD_TARGETS)

$(LIB_IN): force
	$(Q)$(MAKE) $(build)=liblockdep

liblockdep.so.$(LIBLOCKDEP_VERSION): $(LIB_IN)
	$(Q)$(do_compile_shared_library)

liblockdep.a: $(LIB_IN)
	$(Q)$(do_build_static_lib)

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
	$(RM) *.o *~ $(TARGETS) *.a *liblockdep*.so* $(VERSION_FILES) .*.d .*.cmd
	$(RM) tags TAGS

PHONY += force
force:

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
