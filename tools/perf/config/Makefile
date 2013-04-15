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
    CFLAGS += -DARCH_X86_64
    ARCH_INCLUDE = ../../arch/x86/lib/memcpy_64.S ../../arch/x86/lib/memset_64.S
  endif
  NO_PERF_REGS := 0
  LIBUNWIND_LIBS = -lunwind -lunwind-x86_64
endif

ifeq ($(NO_PERF_REGS),0)
  CFLAGS += -DHAVE_PERF_REGS
endif

ifeq ($(src-perf),)
src-perf := $(srctree)/tools/perf
endif

ifeq ($(obj-perf),)
obj-perf := $(objtree)
endif

ifneq ($(obj-perf),)
obj-perf := $(abspath $(obj-perf))/
endif

# include ARCH specific config
-include $(src-perf)/arch/$(ARCH)/Makefile

include $(src-perf)/config/feature-tests.mak
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

ifeq ("$(origin DEBUG)", "command line")
  PERF_DEBUG = $(DEBUG)
endif
ifndef PERF_DEBUG
  CFLAGS += -O6
endif

ifdef PARSER_DEBUG
  PARSER_DEBUG_BISON := -t
  PARSER_DEBUG_FLEX  := -d
  CFLAGS             += -DPARSER_DEBUG
endif

CFLAGS += \
  -fno-omit-frame-pointer \
  -ggdb3 \
  -funwind-tables \
  -Wall \
  -Wextra \
  -std=gnu99

EXTLIBS = -lpthread -lrt -lelf -lm

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -fstack-protector-all,-fstack-protector-all),y)
  CFLAGS += -fstack-protector-all
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -Wstack-protector,-Wstack-protector),y)
  CFLAGS += -Wstack-protector
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -Wvolatile-register-var,-Wvolatile-register-var),y)
  CFLAGS += -Wvolatile-register-var
endif

ifndef PERF_DEBUG
  ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -D_FORTIFY_SOURCE=2,-D_FORTIFY_SOURCE=2),y)
    CFLAGS += -D_FORTIFY_SOURCE=2
  endif
endif

CFLAGS += \
  -I$(src-perf)/util/include \
  -I$(src-perf)/arch/$(ARCH)/include \
  -I$(srctree)/arch/$(ARCH)/include/uapi \
  -I$(srctree)/arch/$(ARCH)/include \
  -I$(srctree)/include/uapi \
  -I$(srctree)/include

# $(obj-perf)      for generated common-cmds.h
# $(obj-perf)/util for generated bison/flex headers
ifneq ($(OUTPUT),)
CFLAGS += \
  -I$(obj-perf)/util \
  -I$(obj-perf)
endif

CFLAGS += \
  -I$(src-perf)/util \
  -I$(src-perf) \
  -I$(TRACE_EVENT_DIR) \
  -I$(srctree)/tools/lib/

CFLAGS += \
  -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

ifndef NO_BIONIC
ifeq ($(call try-cc,$(SOURCE_BIONIC),$(CFLAGS),bionic),y)
  BIONIC := 1
  EXTLIBS := $(filter-out -lrt,$(EXTLIBS))
  EXTLIBS := $(filter-out -lpthread,$(EXTLIBS))
endif
endif # NO_BIONIC

ifdef NO_LIBELF
  NO_DWARF := 1
  NO_DEMANGLE := 1
  NO_LIBUNWIND := 1
else
FLAGS_LIBELF=$(CFLAGS) $(LDFLAGS) $(EXTLIBS)
ifneq ($(call try-cc,$(SOURCE_LIBELF),$(FLAGS_LIBELF),libelf),y)
  FLAGS_GLIBC=$(CFLAGS) $(LDFLAGS)
  ifeq ($(call try-cc,$(SOURCE_GLIBC),$(FLAGS_GLIBC),glibc),y)
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

  FLAGS_DWARF=$(CFLAGS) $(LIBDW_CFLAGS) -ldw -lelf $(LIBDW_LDFLAGS) $(LDFLAGS) $(EXTLIBS)
  ifneq ($(call try-cc,$(SOURCE_DWARF),$(FLAGS_DWARF),libdw),y)
    msg := $(warning No libdw.h found or old libdw.h found or elfutils is older than 0.138, disables dwarf support. Please install new elfutils-devel/libdw-dev);
    NO_DWARF := 1
  endif # Dwarf support
endif # SOURCE_LIBELF
endif # NO_LIBELF

ifndef NO_LIBELF
CFLAGS += -DLIBELF_SUPPORT
FLAGS_LIBELF=$(CFLAGS) $(LDFLAGS) $(EXTLIBS)
ifeq ($(call try-cc,$(SOURCE_ELF_MMAP),$(FLAGS_LIBELF),-DLIBELF_MMAP),y)
  CFLAGS += -DLIBELF_MMAP
endif

# include ARCH specific config
-include $(src-perf)/arch/$(ARCH)/Makefile

ifndef NO_DWARF
ifeq ($(origin PERF_HAVE_DWARF_REGS), undefined)
  msg := $(warning DWARF register mappings have not been defined for architecture $(ARCH), DWARF support disabled);
  NO_DWARF := 1
else
  CFLAGS += -DDWARF_SUPPORT $(LIBDW_CFLAGS)
  LDFLAGS += $(LIBDW_LDFLAGS)
  EXTLIBS += -lelf -ldw
endif # PERF_HAVE_DWARF_REGS
endif # NO_DWARF

endif # NO_LIBELF

ifndef NO_LIBELF
CFLAGS += -DLIBELF_SUPPORT
FLAGS_LIBELF=$(CFLAGS) $(LDFLAGS) $(EXTLIBS)
ifeq ($(call try-cc,$(SOURCE_ELF_MMAP),$(FLAGS_LIBELF),-DLIBELF_MMAP),y)
  CFLAGS += -DLIBELF_MMAP
endif # try-cc
endif # NO_LIBELF

# There's only x86 (both 32 and 64) support for CFI unwind so far
ifneq ($(ARCH),x86)
  NO_LIBUNWIND := 1
endif

ifndef NO_LIBUNWIND
# for linking with debug library, run like:
# make DEBUG=1 LIBUNWIND_DIR=/opt/libunwind/
ifdef LIBUNWIND_DIR
  LIBUNWIND_CFLAGS  := -I$(LIBUNWIND_DIR)/include
  LIBUNWIND_LDFLAGS := -L$(LIBUNWIND_DIR)/lib
endif

FLAGS_UNWIND=$(LIBUNWIND_CFLAGS) $(CFLAGS) $(LIBUNWIND_LDFLAGS) $(LDFLAGS) $(EXTLIBS) $(LIBUNWIND_LIBS)
ifneq ($(call try-cc,$(SOURCE_LIBUNWIND),$(FLAGS_UNWIND),libunwind),y)
  msg := $(warning No libunwind found, disabling post unwind support. Please install libunwind-dev[el] >= 0.99);
  NO_LIBUNWIND := 1
endif # Libunwind support
endif # NO_LIBUNWIND

ifndef NO_LIBUNWIND
  CFLAGS += -DLIBUNWIND_SUPPORT
  EXTLIBS += $(LIBUNWIND_LIBS)
  CFLAGS += $(LIBUNWIND_CFLAGS)
  LDFLAGS += $(LIBUNWIND_LDFLAGS)
endif # NO_LIBUNWIND

ifndef NO_LIBAUDIT
  FLAGS_LIBAUDIT = $(CFLAGS) $(LDFLAGS) -laudit
  ifneq ($(call try-cc,$(SOURCE_LIBAUDIT),$(FLAGS_LIBAUDIT),libaudit),y)
    msg := $(warning No libaudit.h found, disables 'trace' tool, please install audit-libs-devel or libaudit-dev);
    NO_LIBAUDIT := 1
  else
    CFLAGS += -DLIBAUDIT_SUPPORT
    EXTLIBS += -laudit
  endif
endif

ifdef NO_NEWT
  NO_SLANG=1
endif

ifndef NO_SLANG
  FLAGS_SLANG=$(CFLAGS) $(LDFLAGS) $(EXTLIBS) -I/usr/include/slang -lslang
  ifneq ($(call try-cc,$(SOURCE_SLANG),$(FLAGS_SLANG),libslang),y)
    msg := $(warning slang not found, disables TUI support. Please install slang-devel or libslang-dev);
    NO_SLANG := 1
  else
    # Fedora has /usr/include/slang/slang.h, but ubuntu /usr/include/slang.h
    CFLAGS += -I/usr/include/slang
    CFLAGS += -DSLANG_SUPPORT
    EXTLIBS += -lslang
  endif
endif

ifndef NO_GTK2
  FLAGS_GTK2=$(CFLAGS) $(LDFLAGS) $(EXTLIBS) $(shell pkg-config --libs --cflags gtk+-2.0 2>/dev/null)
  ifneq ($(call try-cc,$(SOURCE_GTK2),$(FLAGS_GTK2),gtk2),y)
    msg := $(warning GTK2 not found, disables GTK2 support. Please install gtk2-devel or libgtk2.0-dev);
    NO_GTK2 := 1
  else
    ifeq ($(call try-cc,$(SOURCE_GTK2_INFOBAR),$(FLAGS_GTK2),-DHAVE_GTK_INFO_BAR),y)
      CFLAGS += -DHAVE_GTK_INFO_BAR
    endif
    CFLAGS += -DGTK2_SUPPORT
    CFLAGS += $(shell pkg-config --cflags gtk+-2.0 2>/dev/null)
    EXTLIBS += $(shell pkg-config --libs gtk+-2.0 2>/dev/null)
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

  ifneq ($(call try-cc,$(SOURCE_PERL_EMBED),$(FLAGS_PERL_EMBED),perl),y)
    CFLAGS += -DNO_LIBPERL
    NO_LIBPERL := 1
  else
    LDFLAGS += $(PERL_EMBED_LDFLAGS)
    EXTLIBS += $(PERL_EMBED_LIBADD)
  endif
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

      ifneq ($(call try-cc,$(SOURCE_PYTHON_EMBED),$(FLAGS_PYTHON_EMBED),python),y)
        $(call disable-python,Python.h (for Python 2.x))
      else

        ifneq ($(call try-cc,$(SOURCE_PYTHON_VERSION),$(FLAGS_PYTHON_EMBED),python version),y)
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

ifdef NO_DEMANGLE
  CFLAGS += -DNO_DEMANGLE
else
  ifdef HAVE_CPLUS_DEMANGLE
    EXTLIBS += -liberty
    CFLAGS += -DHAVE_CPLUS_DEMANGLE
  else
    FLAGS_BFD=$(CFLAGS) $(LDFLAGS) $(EXTLIBS) -DPACKAGE='perf' -lbfd
    has_bfd := $(call try-cc,$(SOURCE_BFD),$(FLAGS_BFD),libbfd)
    ifeq ($(has_bfd),y)
      EXTLIBS += -lbfd
    else
      FLAGS_BFD_IBERTY=$(FLAGS_BFD) -liberty
      has_bfd_iberty := $(call try-cc,$(SOURCE_BFD),$(FLAGS_BFD_IBERTY),liberty)
      ifeq ($(has_bfd_iberty),y)
        EXTLIBS += -lbfd -liberty
      else
        FLAGS_BFD_IBERTY_Z=$(FLAGS_BFD_IBERTY) -lz
        has_bfd_iberty_z := $(call try-cc,$(SOURCE_BFD),$(FLAGS_BFD_IBERTY_Z),libz)
        ifeq ($(has_bfd_iberty_z),y)
          EXTLIBS += -lbfd -liberty -lz
        else
          FLAGS_CPLUS_DEMANGLE=$(CFLAGS) $(LDFLAGS) $(EXTLIBS) -liberty
          has_cplus_demangle := $(call try-cc,$(SOURCE_CPLUS_DEMANGLE),$(FLAGS_CPLUS_DEMANGLE),demangle)
          ifeq ($(has_cplus_demangle),y)
            EXTLIBS += -liberty
            CFLAGS += -DHAVE_CPLUS_DEMANGLE
          else
            msg := $(warning No bfd.h/libbfd found, install binutils-dev[el]/zlib-static to gain symbol demangling)
            CFLAGS += -DNO_DEMANGLE
          endif
        endif
      endif
    endif
  endif
endif

ifndef NO_STRLCPY
  ifeq ($(call try-cc,$(SOURCE_STRLCPY),,-DHAVE_STRLCPY),y)
    CFLAGS += -DHAVE_STRLCPY
  endif
endif

ifndef NO_ON_EXIT
  ifeq ($(call try-cc,$(SOURCE_ON_EXIT),,-DHAVE_ON_EXIT),y)
    CFLAGS += -DHAVE_ON_EXIT
  endif
endif

ifndef NO_BACKTRACE
  ifeq ($(call try-cc,$(SOURCE_BACKTRACE),,-DBACKTRACE_SUPPORT),y)
    CFLAGS += -DBACKTRACE_SUPPORT
  endif
endif

ifndef NO_LIBNUMA
  FLAGS_LIBNUMA = $(CFLAGS) $(LDFLAGS) -lnuma
  ifneq ($(call try-cc,$(SOURCE_LIBNUMA),$(FLAGS_LIBNUMA),libnuma),y)
    msg := $(warning No numa.h found, disables 'perf bench numa mem' benchmark, please install numa-libs-devel or libnuma-dev);
    NO_LIBNUMA := 1
  else
    CFLAGS += -DLIBNUMA_SUPPORT
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
lib = lib

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

ifneq ($(filter /%,$(firstword $(perfexecdir))),)
perfexec_instdir = $(perfexecdir)
else
perfexec_instdir = $(prefix)/$(perfexecdir)
endif
perfexec_instdir_SQ = $(subst ','\'',$(perfexec_instdir))
