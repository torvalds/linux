
ifeq ($(src-perf),)
src-perf := $(srctree)/tools/perf
endif

ifeq ($(obj-perf),)
obj-perf := $(OUTPUT)
endif

ifneq ($(obj-perf),)
obj-perf := $(abspath $(obj-perf))/
endif

$(shell echo -n > $(OUTPUT).config-detected)
detected     = $(shell echo "$(1)=y"       >> $(OUTPUT).config-detected)
detected_var = $(shell echo "$(1)=$($(1))" >> $(OUTPUT).config-detected)

CFLAGS := $(EXTRA_CFLAGS) $(EXTRA_WARNINGS)

include $(src-perf)/config/Makefile.arch

$(call detected_var,ARCH)

NO_PERF_REGS := 1

# Additional ARCH settings for x86
ifeq ($(ARCH),x86)
  $(call detected,CONFIG_X86)
  ifeq (${IS_64_BIT}, 1)
    CFLAGS += -DHAVE_ARCH_X86_64_SUPPORT
    ARCH_INCLUDE = ../../arch/x86/lib/memcpy_64.S ../../arch/x86/lib/memset_64.S
    LIBUNWIND_LIBS = -lunwind -lunwind-x86_64
    $(call detected,CONFIG_X86_64)
  else
    LIBUNWIND_LIBS = -lunwind-x86 -llzma -lunwind
  endif
  NO_PERF_REGS := 0
endif

ifeq ($(ARCH),arm)
  NO_PERF_REGS := 0
  LIBUNWIND_LIBS = -lunwind -lunwind-arm
endif

ifeq ($(ARCH),arm64)
  NO_PERF_REGS := 0
  LIBUNWIND_LIBS = -lunwind -lunwind-aarch64
endif

ifeq ($(NO_PERF_REGS),0)
  $(call detected,CONFIG_PERF_REGS)
endif

# So far there's only x86 and arm libdw unwind support merged in perf.
# Disable it on all other architectures in case libdw unwind
# support is detected in system. Add supported architectures
# to the check.
ifneq ($(ARCH),$(filter $(ARCH),x86 arm))
  NO_LIBDW_DWARF_UNWIND := 1
endif

ifeq ($(LIBUNWIND_LIBS),)
  NO_LIBUNWIND := 1
else
  #
  # For linking with debug library, run like:
  #
  #   make DEBUG=1 LIBUNWIND_DIR=/opt/libunwind/
  #
  ifdef LIBUNWIND_DIR
    LIBUNWIND_CFLAGS  = -I$(LIBUNWIND_DIR)/include
    LIBUNWIND_LDFLAGS = -L$(LIBUNWIND_DIR)/lib
  endif
  LIBUNWIND_LDFLAGS += $(LIBUNWIND_LIBS)

  # Set per-feature check compilation flags
  FEATURE_CHECK_CFLAGS-libunwind = $(LIBUNWIND_CFLAGS)
  FEATURE_CHECK_LDFLAGS-libunwind = $(LIBUNWIND_LDFLAGS)
  FEATURE_CHECK_CFLAGS-libunwind-debug-frame = $(LIBUNWIND_CFLAGS)
  FEATURE_CHECK_LDFLAGS-libunwind-debug-frame = $(LIBUNWIND_LDFLAGS)
endif

ifeq ($(NO_PERF_REGS),0)
  CFLAGS += -DHAVE_PERF_REGS_SUPPORT
endif

ifndef NO_LIBELF
  # for linking with debug library, run like:
  # make DEBUG=1 LIBDW_DIR=/opt/libdw/
  ifdef LIBDW_DIR
    LIBDW_CFLAGS  := -I$(LIBDW_DIR)/include
    LIBDW_LDFLAGS := -L$(LIBDW_DIR)/lib
  endif
  FEATURE_CHECK_CFLAGS-libdw-dwarf-unwind := $(LIBDW_CFLAGS)
  FEATURE_CHECK_LDFLAGS-libdw-dwarf-unwind := $(LIBDW_LDFLAGS) -ldw
endif

ifdef LIBBABELTRACE
  # for linking with debug library, run like:
  # make DEBUG=1 LIBBABELTRACE_DIR=/opt/libbabeltrace/
  ifdef LIBBABELTRACE_DIR
    LIBBABELTRACE_CFLAGS  := -I$(LIBBABELTRACE_DIR)/include
    LIBBABELTRACE_LDFLAGS := -L$(LIBBABELTRACE_DIR)/lib
  endif
  FEATURE_CHECK_CFLAGS-libbabeltrace := $(LIBBABELTRACE_CFLAGS)
  FEATURE_CHECK_LDFLAGS-libbabeltrace := $(LIBBABELTRACE_LDFLAGS) -lbabeltrace-ctf
endif

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
else
  CFLAGS += $(call cc-option,-Og,-O0)
endif

ifdef PARSER_DEBUG
  PARSER_DEBUG_BISON := -t
  PARSER_DEBUG_FLEX  := -d
  CFLAGS             += -DPARSER_DEBUG
  $(call detected_var,PARSER_DEBUG_BISON)
  $(call detected_var,PARSER_DEBUG_FLEX)
endif

ifndef NO_LIBPYTHON
  # Try different combinations to accommodate systems that only have
  # python[2][-config] in weird combinations but always preferring
  # python2 and python2-config as per pep-0394. If we catch a
  # python[-config] in version 3, the version check will kill it.
  PYTHON2 := $(if $(call get-executable,python2),python2,python)
  override PYTHON := $(call get-executable-or-default,PYTHON,$(PYTHON2))
  PYTHON2_CONFIG := \
    $(if $(call get-executable,$(PYTHON)-config),$(PYTHON)-config,python-config)
  override PYTHON_CONFIG := \
    $(call get-executable-or-default,PYTHON_CONFIG,$(PYTHON2_CONFIG))

  PYTHON_CONFIG_SQ := $(call shell-sq,$(PYTHON_CONFIG))

  PYTHON_EMBED_LDOPTS := $(shell $(PYTHON_CONFIG_SQ) --ldflags 2>/dev/null)
  PYTHON_EMBED_CCOPTS := $(shell $(PYTHON_CONFIG_SQ) --cflags 2>/dev/null)

  FEATURE_CHECK_CFLAGS-libpython := $(PYTHON_EMBED_CCOPTS)
  FEATURE_CHECK_LDFLAGS-libpython := $(PYTHON_EMBED_LDOPTS)
  FEATURE_CHECK_CFLAGS-libpython-version := $(PYTHON_EMBED_CCOPTS)
  FEATURE_CHECK_LDFLAGS-libpython-version := $(PYTHON_EMBED_LDOPTS)
endif

CFLAGS += -fno-omit-frame-pointer
CFLAGS += -ggdb3
CFLAGS += -funwind-tables
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -std=gnu99

# Enforce a non-executable stack, as we may regress (again) in the future by
# adding assembler files missing the .GNU-stack linker note.
LDFLAGS += -Wl,-z,noexecstack

EXTLIBS = -lpthread -lrt -lm -ldl

include $(srctree)/tools/build/Makefile.feature

ifeq ($(feature-stackprotector-all), 1)
  CFLAGS += -fstack-protector-all
endif

ifeq ($(DEBUG),0)
  ifeq ($(feature-fortify-source), 1)
    CFLAGS += -D_FORTIFY_SOURCE=2
  endif
endif

CFLAGS += -I$(src-perf)/util/include
CFLAGS += -I$(src-perf)/arch/$(ARCH)/include
CFLAGS += -I$(srctree)/tools/include/
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
CFLAGS += -I$(srctree)/tools/lib/

CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

ifeq ($(feature-sync-compare-and-swap), 1)
  CFLAGS += -DHAVE_SYNC_COMPARE_AND_SWAP_SUPPORT
endif

ifeq ($(feature-pthread-attr-setaffinity-np), 1)
  CFLAGS += -DHAVE_PTHREAD_ATTR_SETAFFINITY_NP
endif

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
  NO_LIBDW_DWARF_UNWIND := 1
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
      NO_LIBUNWIND := 1
      NO_LIBDW_DWARF_UNWIND := 1
    else
      ifneq ($(filter s% -static%,$(LDFLAGS),),)
        msg := $(error No static glibc found, please install glibc-static);
      else
        msg := $(error No gnu/libc-version.h found, please install glibc-dev[el]);
      endif
    endif
  else
    ifndef NO_LIBDW_DWARF_UNWIND
      ifneq ($(feature-libdw-dwarf-unwind),1)
        NO_LIBDW_DWARF_UNWIND := 1
        msg := $(warning No libdw DWARF unwind found, Please install elfutils-devel/libdw-dev >= 0.158 and/or set LIBDW_DIR);
      endif
    endif
    ifneq ($(feature-dwarf), 1)
      msg := $(warning No libdw.h found or old libdw.h found or elfutils is older than 0.138, disables dwarf support. Please install new elfutils-devel/libdw-dev);
      NO_DWARF := 1
    endif # Dwarf support
  endif # libelf support
endif # NO_LIBELF

ifdef NO_DWARF
  NO_LIBDW_DWARF_UNWIND := 1
endif

ifndef NO_LIBELF
  CFLAGS += -DHAVE_LIBELF_SUPPORT
  EXTLIBS += -lelf
  $(call detected,CONFIG_LIBELF)

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
      EXTLIBS += -ldw
      $(call detected,CONFIG_DWARF)
    endif # PERF_HAVE_DWARF_REGS
  endif # NO_DWARF
endif # NO_LIBELF

ifeq ($(ARCH),powerpc)
  ifndef NO_DWARF
    CFLAGS += -DHAVE_SKIP_CALLCHAIN_IDX
  endif
endif

ifndef NO_LIBUNWIND
  ifneq ($(feature-libunwind), 1)
    msg := $(warning No libunwind found. Please install libunwind-dev[el] >= 1.1 and/or set LIBUNWIND_DIR);
    NO_LIBUNWIND := 1
  endif
endif

dwarf-post-unwind := 1
dwarf-post-unwind-text := BUG

# setup DWARF post unwinder
ifdef NO_LIBUNWIND
  ifdef NO_LIBDW_DWARF_UNWIND
    msg := $(warning Disabling post unwind, no support found.);
    dwarf-post-unwind := 0
  else
    dwarf-post-unwind-text := libdw
    $(call detected,CONFIG_LIBDW_DWARF_UNWIND)
  endif
else
  dwarf-post-unwind-text := libunwind
  $(call detected,CONFIG_LIBUNWIND)
  # Enable libunwind support by default.
  ifndef NO_LIBDW_DWARF_UNWIND
    NO_LIBDW_DWARF_UNWIND := 1
  endif
endif

ifeq ($(dwarf-post-unwind),1)
  CFLAGS += -DHAVE_DWARF_UNWIND_SUPPORT
  $(call detected,CONFIG_DWARF_UNWIND)
else
  NO_DWARF_UNWIND := 1
endif

ifndef NO_LIBUNWIND
  ifeq ($(ARCH),$(filter $(ARCH),arm arm64))
    $(call feature_check,libunwind-debug-frame)
    ifneq ($(feature-libunwind-debug-frame), 1)
      msg := $(warning No debug_frame support found in libunwind);
      CFLAGS += -DNO_LIBUNWIND_DEBUG_FRAME
    endif
  else
    # non-ARM has no dwarf_find_debug_frame() function:
    CFLAGS += -DNO_LIBUNWIND_DEBUG_FRAME
  endif
  CFLAGS  += -DHAVE_LIBUNWIND_SUPPORT
  EXTLIBS += $(LIBUNWIND_LIBS)
  CFLAGS  += $(LIBUNWIND_CFLAGS)
  LDFLAGS += $(LIBUNWIND_LDFLAGS)
endif

ifndef NO_LIBAUDIT
  ifneq ($(feature-libaudit), 1)
    msg := $(warning No libaudit.h found, disables 'trace' tool, please install audit-libs-devel or libaudit-dev);
    NO_LIBAUDIT := 1
  else
    CFLAGS += -DHAVE_LIBAUDIT_SUPPORT
    EXTLIBS += -laudit
    $(call detected,CONFIG_AUDIT)
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
    $(call detected,CONFIG_SLANG)
  endif
endif

ifndef NO_GTK2
  FLAGS_GTK2=$(CFLAGS) $(LDFLAGS) $(EXTLIBS) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null)
  ifneq ($(feature-gtk2), 1)
    msg := $(warning GTK2 not found, disables GTK2 support. Please install gtk2-devel or libgtk2.0-dev);
    NO_GTK2 := 1
  else
    ifeq ($(feature-gtk2-infobar), 1)
      GTK_CFLAGS := -DHAVE_GTK_INFO_BAR_SUPPORT
    endif
    CFLAGS += -DHAVE_GTK2_SUPPORT
    GTK_CFLAGS += $(shell $(PKG_CONFIG) --cflags gtk+-2.0 2>/dev/null)
    GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-2.0 2>/dev/null)
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
    msg := $(warning Missing perl devel files. Disabling perl scripting support, please install perl-ExtUtils-Embed/libperl-dev);
  else
    LDFLAGS += $(PERL_EMBED_LDFLAGS)
    EXTLIBS += $(PERL_EMBED_LIBADD)
    $(call detected,CONFIG_LIBPERL)
  endif
endif

ifeq ($(feature-timerfd), 1)
  CFLAGS += -DHAVE_TIMERFD_SUPPORT
else
  msg := $(warning No timerfd support. Disables 'perf kvm stat live');
endif

disable-python = $(eval $(disable-python_code))
define disable-python_code
  CFLAGS += -DNO_LIBPYTHON
  $(warning $1)
  NO_LIBPYTHON := 1
endef

ifdef NO_LIBPYTHON
  $(call disable-python,Python support disabled by user)
else

  ifndef PYTHON
    $(call disable-python,No python interpreter was found: disables Python support - please install python-devel/python-dev)
  else
    PYTHON_WORD := $(call shell-wordify,$(PYTHON))

    ifndef PYTHON_CONFIG
      $(call disable-python,No 'python-config' tool was found: disables Python support - please install python-devel/python-dev)
    else

      PYTHON_CONFIG_SQ := $(call shell-sq,$(PYTHON_CONFIG))

      PYTHON_EMBED_LDOPTS := $(shell $(PYTHON_CONFIG_SQ) --ldflags 2>/dev/null)
      PYTHON_EMBED_LDFLAGS := $(call strip-libs,$(PYTHON_EMBED_LDOPTS))
      PYTHON_EMBED_LIBADD := $(call grep-libs,$(PYTHON_EMBED_LDOPTS))
      PYTHON_EMBED_CCOPTS := $(shell $(PYTHON_CONFIG_SQ) --cflags 2>/dev/null)
      FLAGS_PYTHON_EMBED := $(PYTHON_EMBED_CCOPTS) $(PYTHON_EMBED_LDOPTS)

      ifneq ($(feature-libpython), 1)
        $(call disable-python,No 'Python.h' (for Python 2.x support) was found: disables Python support - please install python-devel/python-dev)
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
          $(call detected,CONFIG_LIBPYTHON)
        endif
      endif
    endif
  endif
endif

ifeq ($(feature-libbfd), 1)
  EXTLIBS += -lbfd

  # call all detections now so we get correct
  # status in VF output
  $(call feature_check,liberty)
  $(call feature_check,liberty-z)
  $(call feature_check,cplus-demangle)

  ifeq ($(feature-liberty), 1)
    EXTLIBS += -liberty
  else
    ifeq ($(feature-liberty-z), 1)
      EXTLIBS += -liberty -lz
    endif
  endif
endif

ifdef NO_DEMANGLE
  CFLAGS += -DNO_DEMANGLE
else
  ifdef HAVE_CPLUS_DEMANGLE_SUPPORT
    EXTLIBS += -liberty
    CFLAGS += -DHAVE_CPLUS_DEMANGLE_SUPPORT
  else
    ifneq ($(feature-libbfd), 1)
      ifneq ($(feature-liberty), 1)
        ifneq ($(feature-liberty-z), 1)
          # we dont have neither HAVE_CPLUS_DEMANGLE_SUPPORT
          # or any of 'bfd iberty z' trinity
          ifeq ($(feature-cplus-demangle), 1)
            EXTLIBS += -liberty
            CFLAGS += -DHAVE_CPLUS_DEMANGLE_SUPPORT
          else
            msg := $(warning No bfd.h/libbfd found, please install binutils-dev[el]/zlib-static/libiberty-dev to gain symbol demangling)
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

ifndef NO_ZLIB
  ifeq ($(feature-zlib), 1)
    CFLAGS += -DHAVE_ZLIB_SUPPORT
    EXTLIBS += -lz
    $(call detected,CONFIG_ZLIB)
  else
    NO_ZLIB := 1
  endif
endif

ifndef NO_LZMA
  ifeq ($(feature-lzma), 1)
    CFLAGS += -DHAVE_LZMA_SUPPORT
    EXTLIBS += -llzma
    $(call detected,CONFIG_LZMA)
  else
    msg := $(warning No liblzma found, disables xz kernel module decompression, please install xz-devel/liblzma-dev);
    NO_LZMA := 1
  endif
endif

ifndef NO_BACKTRACE
  ifeq ($(feature-backtrace), 1)
    CFLAGS += -DHAVE_BACKTRACE_SUPPORT
  endif
endif

ifndef NO_LIBNUMA
  ifeq ($(feature-libnuma), 0)
    msg := $(warning No numa.h found, disables 'perf bench numa mem' benchmark, please install numactl-devel/libnuma-devel/libnuma-dev);
    NO_LIBNUMA := 1
  else
    CFLAGS += -DHAVE_LIBNUMA_SUPPORT
    EXTLIBS += -lnuma
    $(call detected,CONFIG_NUMA)
  endif
endif

ifdef HAVE_KVM_STAT_SUPPORT
    CFLAGS += -DHAVE_KVM_STAT_SUPPORT
endif

ifeq (${IS_64_BIT}, 1)
  ifndef NO_PERF_READ_VDSO32
    $(call feature_check,compile-32)
    ifeq ($(feature-compile-32), 1)
      CFLAGS += -DHAVE_PERF_READ_VDSO32
    else
      NO_PERF_READ_VDSO32 := 1
    endif
  endif
  ifneq ($(ARCH), x86)
    NO_PERF_READ_VDSOX32 := 1
  endif
  ifndef NO_PERF_READ_VDSOX32
    $(call feature_check,compile-x32)
    ifeq ($(feature-compile-x32), 1)
      CFLAGS += -DHAVE_PERF_READ_VDSOX32
    else
      NO_PERF_READ_VDSOX32 := 1
    endif
  endif
else
  NO_PERF_READ_VDSO32 := 1
  NO_PERF_READ_VDSOX32 := 1
endif

ifdef LIBBABELTRACE
  $(call feature_check,libbabeltrace)
  ifeq ($(feature-libbabeltrace), 1)
    CFLAGS += -DHAVE_LIBBABELTRACE_SUPPORT $(LIBBABELTRACE_CFLAGS)
    LDFLAGS += $(LIBBABELTRACE_LDFLAGS)
    EXTLIBS += -lbabeltrace-ctf
    $(call detected,CONFIG_LIBBABELTRACE)
  else
    msg := $(warning No libbabeltrace found, disables 'perf data' CTF format support, please install libbabeltrace-dev[el]/libbabeltrace-ctf-dev);
  endif
endif

ifndef NO_AUXTRACE
  $(call detected,CONFIG_AUXTRACE)
  CFLAGS += -DHAVE_AUXTRACE_SUPPORT
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
prefix ?= $(HOME)
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
ifndef lib
ifeq ($(ARCH)$(IS_64_BIT), x861)
lib = lib64
else
lib = lib
endif
endif # lib
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

# If we install to $(HOME) we keep the traceevent default:
# $(HOME)/.traceevent/plugins
# Otherwise we install plugins into the global $(libdir).
ifdef DESTDIR
plugindir=$(libdir)/traceevent/plugins
plugindir_SQ= $(subst ','\'',$(plugindir))
endif

print_var = $(eval $(print_var_code)) $(info $(MSG))
define print_var_code
    MSG = $(shell printf '...%30s: %s' $(1) $($(1)))
endef

ifeq ($(VF),1)
  $(call print_var,prefix)
  $(call print_var,bindir)
  $(call print_var,libdir)
  $(call print_var,sysconfdir)
  $(call print_var,LIBUNWIND_DIR)
  $(call print_var,LIBDW_DIR)
  $(info )
endif

$(call detected_var,bindir_SQ)
$(call detected_var,PYTHON_WORD)
ifneq ($(OUTPUT),)
$(call detected_var,OUTPUT)
endif
$(call detected_var,htmldir_SQ)
$(call detected_var,infodir_SQ)
$(call detected_var,mandir_SQ)
$(call detected_var,ETC_PERFCONFIG_SQ)
$(call detected_var,prefix_SQ)
$(call detected_var,perfexecdir_SQ)
$(call detected_var,LIBDIR)
$(call detected_var,GTK_CFLAGS)
$(call detected_var,PERL_EMBED_CCOPTS)
$(call detected_var,PYTHON_EMBED_CCOPTS)
