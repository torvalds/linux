uname_M := $(shell uname -m 2>/dev/null || echo not)

ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
                                  -e s/arm.*/arm/ -e s/sa110/arm/ \
                                  -e s/s390x/s390/ -e s/parisc64/parisc/ \
                                  -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
                                  -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ )
NO_PERF_REGS := 1
CFLAGS := $(EXTRA_CFLAGS) $(EXTRA_WARNINGS)

# Additional ARCH settings for x86
ifeq ($(ARCH),i386)
  override ARCH := x86
  NO_PERF_REGS := 0
  LIBUNWIND_LIBS = -lunwind -lunwind-x86
endif

ifeq ($(ARCH),x86_64)
  override ARCH := x86
  IS_X86_64 := 0
  ifeq (, $(findstring m32,$(CFLAGS)))
    IS_X86_64 := $(shell echo __x86_64__ | ${CC} -E -x c - | tail -n 1)
  endif
  ifeq (${IS_X86_64}, 1)
    RAW_ARCH := x86_64
    CFLAGS += -DHAVE_ARCH_X86_64_SUPPORT
    ARCH_INCLUDE = ../../arch/x86/lib/memcpy_64.S ../../arch/x86/lib/memset_64.S
    LIBUNWIND_LIBS = -lunwind -lunwind-x86_64
  else
    LIBUNWIND_LIBS = -lunwind -lunwind-x86
  endif
  NO_PERF_REGS := 0
endif
ifeq ($(ARCH),arm)
  NO_PERF_REGS := 0
  LIBUNWIND_LIBS = -lunwind -lunwind-arm
endif

ifeq ($(NO_PERF_REGS),0)
  CFLAGS += -DHAVE_PERF_REGS_SUPPORT
endif

ifeq ($(src-perf),)
src-perf := $(srctree)/tools/perf
endif

ifeq ($(obj-perf),)
obj-perf := $(OUTPUT)
endif

ifneq ($(obj-perf),)
obj-perf := $(abspath $(obj-perf))/
endif

LIB_INCLUDE := $(srctree)/tools/lib/

# include ARCH specific config
-include $(src-perf)/arch/$(ARCH)/Makefile

include $(src-perf)/config/utilities.mak

ifeq ($(call get-executable,$(FLEX)),)
  dummy := $(error Error: $(FLEX) is missing on this system, please install it)
endif

ifeq ($(call get-executable,$(BISON)),)
  dummy := $(error Error: $(BISON) is missing on this system, please install it)
endif

# Treat warnings as errors unless directed not to
ifneq ($(WERROR),0)
  CFLAGS += -Werror
endif

ifndef DEBUG
  DEBUG := 0
endif

ifeq ($(DEBUG),0)
  CFLAGS += -O6
endif

ifdef PARSER_DEBUG
  PARSER_DEBUG_BISON := -t
  PARSER_DEBUG_FLEX  := -d
  CFLAGS             += -DPARSER_DEBUG
endif

CFLAGS += -fno-omit-frame-pointer
CFLAGS += -ggdb3
CFLAGS += -funwind-tables
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -std=gnu99

EXTLIBS = -lelf -lpthread -lrt -lm -ldl

ifneq ($(OUTPUT),)
  OUTPUT_FEATURES = $(OUTPUT)config/feature-checks/
  $(shell mkdir -p $(OUTPUT_FEATURES))
endif

feature_check = $(eval $(feature_check_code))
define feature_check_code
  feature-$(1) := $(shell $(MAKE) OUTPUT=$(OUTPUT_FEATURES) CFLAGS="$(EXTRA_CFLAGS)" LDFLAGS="$(LDFLAGS)" LIBUNWIND_LIBS="$(LIBUNWIND_LIBS)" -C config/feature-checks test-$1 >/dev/null 2>/dev/null && echo 1 || echo 0)
endef

feature_set = $(eval $(feature_set_code))
define feature_set_code
  feature-$(1) := 1
endef

#
# Build the feature check binaries in parallel, ignore errors, ignore return value and suppress output:
#

#
# Note that this is not a complete list of all feature tests, just
# those that are typically built on a fully configured system.
#
# [ Feature tests not mentioned here have to be built explicitly in
#   the rule that uses them - an example for that is the 'bionic'
#   feature check. ]
#
CORE_FEATURE_TESTS =			\
	backtrace			\
	dwarf				\
	fortify-source			\
	glibc				\
	gtk2				\
	gtk2-infobar			\
	libaudit			\
	libbfd				\
	libelf				\
	libelf-getphdrnum		\
	libelf-mmap			\
	libnuma				\
	libperl				\
	libpython			\
	libpython-version		\
	libslang			\
	libunwind			\
	on-exit				\
	stackprotector			\
	stackprotector-all

#
# So here we detect whether test-all was rebuilt, to be able
# to skip the print-out of the long features list if the file
# existed before and after it was built:
#
ifeq ($(wildcard $(OUTPUT)config/feature-checks/test-all),)
  test-all-failed := 1
else
  test-all-failed := 0
endif

#
# Special fast-path for the 'all features are available' case:
#
$(call feature_check,all,$(MSG))

#
# Just in case the build freshly failed, make sure we print the
# feature matrix:
#
ifeq ($(feature-all), 0)
  test-all-failed := 1
endif

ifeq ($(test-all-failed),1)
  $(info )
  $(info Auto-detecting system features:)
endif

ifeq ($(feature-all), 1)
  #
  # test-all.c passed - just set all the core feature flags to 1:
  #
  $(foreach feat,$(CORE_FEATURE_TESTS),$(call feature_set,$(feat)))
else
  $(shell $(MAKE) OUTPUT=$(OUTPUT_FEATURES) CFLAGS="$(EXTRA_CFLAGS)" LDFLAGS=$(LDFLAGS) -i -j -C config/feature-checks $(CORE_FEATURE_TESTS) >/dev/null 2>&1)
  $(foreach feat,$(CORE_FEATURE_TESTS),$(call feature_check,$(feat)))
endif

#
# Print the result of the feature test:
#
feature_print = $(eval $(feature_print_code)) $(info $(MSG))

define feature_print_code
  ifeq ($(feature-$(1)), 1)
    MSG = $(shell printf '...%30s: [ \033[32mon\033[m  ]' $(1))
  else
    MSG = $(shell printf '...%30s: [ \033[31mOFF\033[m ]' $(1))
  endif
endef

#
# Only print out our features if we rebuilt the testcases or if a test failed:
#
ifeq ($(test-all-failed), 1)
  $(foreach feat,$(CORE_FEATURE_TESTS),$(call feature_print,$(feat)))
  $(info )
endif

ifeq ($(feature-stackprotector-all), 1)
  CFLAGS += -fstack-protector-all
endif

ifeq ($(feature-stackprotector), 1)
  CFLAGS += -Wstack-protector
endif

ifeq ($(DEBUG),0)
  ifeq ($(feature-fortify-source), 1)
    CFLAGS += -D_FORTIFY_SOURCE=2
  endif
endif

CFLAGS += -I$(src-perf)/util/include
CFLAGS += -I$(src-perf)/arch/$(ARCH)/include
CFLAGS += -I$(srctree)/arch/$(ARCH)/include/uapi
CFLAGS += -I$(srctree)/arch/$(ARCH)/include
CFLAGS += -I$(srctree)/include/uapi
CFLAGS += -I$(srctree)/include

# $(obj-perf)      for generated common-cmds.h
# $(obj-perf)/util for generated bison/flex headers
ifneq ($(OUTPUT),)
CFLAGS += -I$(obj-perf)/util
CFLAGS += -I$(obj-perf)
endif

CFLAGS += -I$(src-perf)/util
CFLAGS += -I$(src-perf)
CFLAGS += -I$(LIB_INCLUDE)

CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

ifndef NO_BIONIC
  $(call feature_check,bionic)
  ifeq ($(feature-bionic), 1)
    BIONIC := 1
    EXTLIBS := $(filter-out -lrt,$(EXTLIBS))
    EXTLIBS := $(filter-out -lpthread,$(EXTLIBS))
  endif
endif

ifdef NO_LIBELF
  NO_DWARF := 1
  NO_DEMANGLE := 1
  NO_LIBUNWIND := 1
else
  ifeq ($(feature-libelf), 0)
    ifeq ($(feature-glibc), 1)
      LIBC_SUPPORT := 1
    endif
    ifeq ($(BIONIC),1)
      LIBC_SUPPORT := 1
    endif
    ifeq ($(LIBC_SUPPORT),1)
      msg := $(warning No libelf found, disables 'probe' tool, please install elfutils-libelf-devel/libelf-dev);

      NO_LIBELF := 1
      NO_DWARF := 1
      NO_DEMANGLE := 1
    else
      msg := $(error No gnu/libc-version.h found, please install glibc-dev[el]/glibc-static);
    endif
  else
    # for linking with debug library, run like:
    # make DEBUG=1 LIBDW_DIR=/opt/libdw/
    ifdef LIBDW_DIR
      LIBDW_CFLAGS  := -I$(LIBDW_DIR)/include
      LIBDW_LDFLAGS := -L$(LIBDW_DIR)/lib
    endif

    ifneq ($(feature-dwarf), 1)
      msg := $(warning No libdw.h found or old libdw.h found or elfutils is older than 0.138, disables dwarf support. Please install new elfutils-devel/libdw-dev);
      NO_DWARF := 1
    endif # Dwarf support
  endif # libelf support
endif # NO_LIBELF

ifndef NO_LIBELF
  CFLAGS += -DHAVE_LIBELF_SUPPORT

  ifeq ($(feature-libelf-mmap), 1)
    CFLAGS += -DHAVE_LIBELF_MMAP_SUPPORT
  endif

  ifeq ($(feature-libelf-getphdrnum), 1)
    CFLAGS += -DHAVE_ELF_GETPHDRNUM_SUPPORT
  endif

  # include ARCH specific config
  -include $(src-perf)/arch/$(ARCH)/Makefile

  ifndef NO_DWARF
    ifeq ($(origin PERF_HAVE_DWARF_REGS), undefined)
      msg := $(warning DWARF register mappings have not been defined for architecture $(ARCH), DWARF support disabled);
      NO_DWARF := 1
    else
      CFLAGS += -DHAVE_DWARF_SUPPORT $(LIBDW_CFLAGS)
      LDFLAGS += $(LIBDW_LDFLAGS)
      EXTLIBS += -lelf -ldw
    endif # PERF_HAVE_DWARF_REGS
  endif # NO_DWARF
endif # NO_LIBELF

ifeq ($(LIBUNWIND_LIBS),)
  NO_LIBUNWIND := 1
endif

ifndef NO_LIBUNWIND
  #
  # For linking with debug library, run like:
  #
  #   make DEBUG=1 LIBUNWIND_DIR=/opt/libunwind/
  #
  ifdef LIBUNWIND_DIR
    LIBUNWIND_CFLAGS  := -I$(LIBUNWIND_DIR)/include
    LIBUNWIND_LDFLAGS := -L$(LIBUNWIND_DIR)/lib
  endif

  ifneq ($(feature-libunwind), 1)
    msg := $(warning No libunwind found, disabling post unwind support. Please install libunwind-dev[el] >= 1.1);
    NO_LIBUNWIND := 1
  else
    ifneq ($(feature-libunwind-debug-frame), 1)
      msg := $(warning No debug_frame support found in libunwind);
      CFLAGS += -DNO_LIBUNWIND_DEBUG_FRAME
    endif
  endif
endif

ifndef NO_LIBUNWIND
  CFLAGS += -DHAVE_LIBUNWIND_SUPPORT
  EXTLIBS += $(LIBUNWIND_LIBS)
  CFLAGS += $(LIBUNWIND_CFLAGS)
  LDFLAGS += $(LIBUNWIND_LDFLAGS)
endif

ifndef NO_LIBAUDIT
  ifneq ($(feature-libaudit), 1)
    msg := $(warning No libaudit.h found, disables 'trace' tool, please install audit-libs-devel or libaudit-dev);
    NO_LIBAUDIT := 1
  else
    CFLAGS += -DHAVE_LIBAUDIT_SUPPORT
    EXTLIBS += -laudit
  endif
endif

ifdef NO_NEWT
  NO_SLANG=1
endif

ifndef NO_SLANG
  ifneq ($(feature-libslang), 1)
    msg := $(warning slang not found, disables TUI support. Please install slang-devel or libslang-dev);
    NO_SLANG := 1
  else
    # Fedora has /usr/include/slang/slang.h, but ubuntu /usr/include/slang.h
    CFLAGS += -I/usr/include/slang
    CFLAGS += -DHAVE_SLANG_SUPPORT
    EXTLIBS += -lslang
  endif
endif

ifndef NO_GTK2
  FLAGS_GTK2=$(CFLAGS) $(LDFLAGS) $(EXTLIBS) $(shell pkg-config --libs --cflags gtk+-2.0 2>/dev/null)
  ifneq ($(feature-gtk2), 1)
    msg := $(warning GTK2 not found, disables GTK2 support. Please install gtk2-devel or libgtk2.0-dev);
    NO_GTK2 := 1
  else
    ifeq ($(feature-gtk2-infobar), 1)
      GTK_CFLAGS := -DHAVE_GTK_INFO_BAR_SUPPORT
    endif
    CFLAGS += -DHAVE_GTK2_SUPPORT
    GTK_CFLAGS += $(shell pkg-config --cflags gtk+-2.0 2>/dev/null)
    GTK_LIBS := $(shell pkg-config --libs gtk+-2.0 2>/dev/null)
    EXTLIBS += -ldl
  endif
endif

grep-libs  = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

ifdef NO_LIBPERL
  CFLAGS += -DNO_LIBPERL
else
  PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
  PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
  PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
  PERL_EMBED_CCOPTS = `perl -MExtUtils::Embed -e ccopts 2>/dev/null`
  FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

  ifneq ($(feature-libperl), 1)
    CFLAGS += -DNO_LIBPERL
    NO_LIBPERL := 1
  else
    LDFLAGS += $(PERL_EMBED_LDFLAGS)
    EXTLIBS += $(PERL_EMBED_LIBADD)
  endif
endif

$(call feature_check,timerfd)
ifeq ($(feature-timerfd), 1)
  CFLAGS += -DHAVE_TIMERFD_SUPPORT
else
  msg := $(warning No timerfd support. Disables 'perf kvm stat live');
endif

disable-python = $(eval $(disable-python_code))
define disable-python_code
  CFLAGS += -DNO_LIBPYTHON
  $(if $(1),$(warning No $(1) was found))
  $(warning Python support will not be built)
  NO_LIBPYTHON := 1
endef

override PYTHON := \
  $(call get-executable-or-default,PYTHON,python)

ifndef PYTHON
  $(call disable-python,python interpreter)
else

  PYTHON_WORD := $(call shell-wordify,$(PYTHON))

  ifdef NO_LIBPYTHON
    $(call disable-python)
  else

    override PYTHON_CONFIG := \
      $(call get-executable-or-default,PYTHON_CONFIG,$(PYTHON)-config)

    ifndef PYTHON_CONFIG
      $(call disable-python,python-config tool)
    else

      PYTHON_CONFIG_SQ := $(call shell-sq,$(PYTHON_CONFIG))

      PYTHON_EMBED_LDOPTS := $(shell $(PYTHON_CONFIG_SQ) --ldflags 2>/dev/null)
      PYTHON_EMBED_LDFLAGS := $(call strip-libs,$(PYTHON_EMBED_LDOPTS))
      PYTHON_EMBED_LIBADD := $(call grep-libs,$(PYTHON_EMBED_LDOPTS))
      PYTHON_EMBED_CCOPTS := $(shell $(PYTHON_CONFIG_SQ) --cflags 2>/dev/null)
      FLAGS_PYTHON_EMBED := $(PYTHON_EMBED_CCOPTS) $(PYTHON_EMBED_LDOPTS)

      ifneq ($(feature-libpython), 1)
        $(call disable-python,Python.h (for Python 2.x))
      else

        ifneq ($(feature-libpython-version), 1)
          $(warning Python 3 is not yet supported; please set)
          $(warning PYTHON and/or PYTHON_CONFIG appropriately.)
          $(warning If you also have Python 2 installed, then)
          $(warning try something like:)
          $(warning $(and ,))
          $(warning $(and ,)  make PYTHON=python2)
          $(warning $(and ,))
          $(warning Otherwise, disable Python support entirely:)
          $(warning $(and ,))
          $(warning $(and ,)  make NO_LIBPYTHON=1)
          $(warning $(and ,))
          $(error   $(and ,))
        else
          LDFLAGS += $(PYTHON_EMBED_LDFLAGS)
          EXTLIBS += $(PYTHON_EMBED_LIBADD)
          LANG_BINDINGS += $(obj-perf)python/perf.so
        endif
      endif
    endif
  endif
endif

ifeq ($(feature-libbfd), 1)
  EXTLIBS += -lbfd
endif

ifdef NO_DEMANGLE
  CFLAGS += -DNO_DEMANGLE
else
  ifdef HAVE_CPLUS_DEMANGLE_SUPPORT
    EXTLIBS += -liberty
    CFLAGS += -DHAVE_CPLUS_DEMANGLE_SUPPORT
  else
    ifneq ($(feature-libbfd), 1)
      $(call feature_check,liberty)
      ifeq ($(feature-liberty), 1)
        EXTLIBS += -lbfd -liberty
      else
        $(call feature_check,liberty-z)
        ifeq ($(feature-liberty-z), 1)
          EXTLIBS += -lbfd -liberty -lz
        else
          $(call feature_check,cplus-demangle)
          ifeq ($(feature-cplus-demangle), 1)
            EXTLIBS += -liberty
            CFLAGS += -DHAVE_CPLUS_DEMANGLE_SUPPORT
          else
            msg := $(warning No bfd.h/libbfd found, install binutils-dev[el]/zlib-static to gain symbol demangling)
            CFLAGS += -DNO_DEMANGLE
          endif
        endif
      endif
    endif
  endif
endif

ifneq ($(filter -lbfd,$(EXTLIBS)),)
  CFLAGS += -DHAVE_LIBBFD_SUPPORT
endif

ifndef NO_ON_EXIT
  ifeq ($(feature-on-exit), 1)
    CFLAGS += -DHAVE_ON_EXIT_SUPPORT
  endif
endif

ifndef NO_BACKTRACE
  ifeq ($(feature-backtrace), 1)
    CFLAGS += -DHAVE_BACKTRACE_SUPPORT
  endif
endif

ifndef NO_LIBNUMA
  ifeq ($(feature-libnuma), 0)
    msg := $(warning No numa.h found, disables 'perf bench numa mem' benchmark, please install numa-libs-devel or libnuma-dev);
    NO_LIBNUMA := 1
  else
    CFLAGS += -DHAVE_LIBNUMA_SUPPORT
    EXTLIBS += -lnuma
  endif
endif

# Among the variables below, these:
#   perfexecdir
#   template_dir
#   mandir
#   infodir
#   htmldir
#   ETC_PERFCONFIG (but not sysconfdir)
# can be specified as a relative path some/where/else;
# this is interpreted as relative to $(prefix) and "perf" at
# runtime figures out where they are based on the path to the executable.
# This can help installing the suite in a relocatable way.

# Make the path relative to DESTDIR, not to prefix
ifndef DESTDIR
prefix = $(HOME)
endif
bindir_relative = bin
bindir = $(prefix)/$(bindir_relative)
mandir = share/man
infodir = share/info
perfexecdir = libexec/perf-core
sharedir = $(prefix)/share
template_dir = share/perf-core/templates
htmldir = share/doc/perf-doc
ifeq ($(prefix),/usr)
sysconfdir = /etc
ETC_PERFCONFIG = $(sysconfdir)/perfconfig
else
sysconfdir = $(prefix)/etc
ETC_PERFCONFIG = etc/perfconfig
endif
ifeq ($(IS_X86_64),1)
lib = lib64
else
lib = lib
endif
libdir = $(prefix)/$(lib)

# Shell quote (do not use $(call) to accommodate ancient setups);
ETC_PERFCONFIG_SQ = $(subst ','\'',$(ETC_PERFCONFIG))
DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
mandir_SQ = $(subst ','\'',$(mandir))
infodir_SQ = $(subst ','\'',$(infodir))
perfexecdir_SQ = $(subst ','\'',$(perfexecdir))
template_dir_SQ = $(subst ','\'',$(template_dir))
htmldir_SQ = $(subst ','\'',$(htmldir))
prefix_SQ = $(subst ','\'',$(prefix))
sysconfdir_SQ = $(subst ','\'',$(sysconfdir))
libdir_SQ = $(subst ','\'',$(libdir))

ifneq ($(filter /%,$(firstword $(perfexecdir))),)
perfexec_instdir = $(perfexecdir)
else
perfexec_instdir = $(prefix)/$(perfexecdir)
endif
perfexec_instdir_SQ = $(subst ','\'',$(perfexec_instdir))
