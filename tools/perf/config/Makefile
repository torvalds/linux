uname_M := $(shell uname -m 2>/dev/null || echo not)

ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
				  -e s/arm.*/arm/ -e s/sa110/arm/ \
				  -e s/s390x/s390/ -e s/parisc64/parisc/ \
				  -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
				  -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ )
NO_PERF_REGS := 1

# Additional ARCH settings for x86
ifeq ($(ARCH),i386)
	override ARCH := x86
	NO_PERF_REGS := 0
	LIBUNWIND_LIBS = -lunwind -lunwind-x86
endif

ifeq ($(ARCH),x86_64)
	override ARCH := x86
	IS_X86_64 := 0
	ifeq (, $(findstring m32,$(EXTRA_CFLAGS)))
		IS_X86_64 := $(shell echo __x86_64__ | ${CC} -E -x c - | tail -n 1)
	endif
	ifeq (${IS_X86_64}, 1)
		RAW_ARCH := x86_64
		ARCH_CFLAGS := -DARCH_X86_64
		ARCH_INCLUDE = ../../arch/x86/lib/memcpy_64.S ../../arch/x86/lib/memset_64.S
	endif
	NO_PERF_REGS := 0
	LIBUNWIND_LIBS = -lunwind -lunwind-x86_64
endif

ifeq ($(NO_PERF_REGS),0)
        BASIC_CFLAGS += -DHAVE_PERF_REGS
endif

-include config/feature-tests.mak

ifeq ($(call get-executable,$(FLEX)),)
	dummy := $(error Error: $(FLEX) is missing on this system, please install it)
endif

ifeq ($(call get-executable,$(BISON)),)
	dummy := $(error Error: $(BISON) is missing on this system, please install it)
endif

# Treat warnings as errors unless directed not to
ifneq ($(WERROR),0)
	CFLAGS_WERROR := -Werror
endif

ifeq ("$(origin DEBUG)", "command line")
	PERF_DEBUG = $(DEBUG)
endif
ifndef PERF_DEBUG
	CFLAGS_OPTIMIZE = -O6
endif

ifdef PARSER_DEBUG
	PARSER_DEBUG_BISON  := -t
	PARSER_DEBUG_FLEX   := -d
	PARSER_DEBUG_CFLAGS := -DPARSER_DEBUG
endif

CFLAGS = -fno-omit-frame-pointer -ggdb3 -funwind-tables -Wall -Wextra -std=gnu99 $(CFLAGS_WERROR) $(CFLAGS_OPTIMIZE) $(EXTRA_WARNINGS) $(EXTRA_CFLAGS) $(PARSER_DEBUG_CFLAGS)
EXTLIBS = -lpthread -lrt -lelf -lm
ALL_CFLAGS = $(CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
ALL_LDFLAGS = $(LDFLAGS)

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -fstack-protector-all,-fstack-protector-all),y)
	CFLAGS := $(CFLAGS) -fstack-protector-all
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -Wstack-protector,-Wstack-protector),y)
	CFLAGS := $(CFLAGS) -Wstack-protector
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -Wvolatile-register-var,-Wvolatile-register-var),y)
	CFLAGS := $(CFLAGS) -Wvolatile-register-var
endif

ifndef PERF_DEBUG
	ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -D_FORTIFY_SOURCE=2,-D_FORTIFY_SOURCE=2),y)
		CFLAGS := $(CFLAGS) -D_FORTIFY_SOURCE=2
	endif
endif

BASIC_CFLAGS += \
	-Iutil/include \
	-Iarch/$(ARCH)/include \
	$(if $(objtree),-I$(objtree)/arch/$(ARCH)/include/generated/uapi) \
	-I$(srctree)/arch/$(ARCH)/include/uapi \
	-I$(srctree)/arch/$(ARCH)/include \
	$(if $(objtree),-I$(objtree)/include/generated/uapi) \
	-I$(srctree)/include/uapi \
	-I$(srctree)/include \
	-I$(OUTPUT)util \
	-Iutil \
	-I. \
	-I$(TRACE_EVENT_DIR) \
	-I../lib/ \
	-D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

BASIC_LDFLAGS =

ifeq ($(call try-cc,$(SOURCE_BIONIC),$(CFLAGS),bionic),y)
	BIONIC := 1
	EXTLIBS := $(filter-out -lrt,$(EXTLIBS))
	EXTLIBS := $(filter-out -lpthread,$(EXTLIBS))
	BASIC_CFLAGS += -I.
endif

ifdef NO_LIBELF
	NO_DWARF := 1
	NO_DEMANGLE := 1
	NO_LIBUNWIND := 1
else
FLAGS_LIBELF=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS)
ifneq ($(call try-cc,$(SOURCE_LIBELF),$(FLAGS_LIBELF),libelf),y)
	FLAGS_GLIBC=$(ALL_CFLAGS) $(ALL_LDFLAGS)
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

	FLAGS_DWARF=$(ALL_CFLAGS) $(LIBDW_CFLAGS) -ldw -lelf $(LIBDW_LDFLAGS) $(ALL_LDFLAGS) $(EXTLIBS)
	ifneq ($(call try-cc,$(SOURCE_DWARF),$(FLAGS_DWARF),libdw),y)
		msg := $(warning No libdw.h found or old libdw.h found or elfutils is older than 0.138, disables dwarf support. Please install new elfutils-devel/libdw-dev);
		NO_DWARF := 1
	endif # Dwarf support
endif # SOURCE_LIBELF
endif # NO_LIBELF

ifndef NO_LIBELF
BASIC_CFLAGS += -DLIBELF_SUPPORT
FLAGS_LIBELF=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS)
ifeq ($(call try-cc,$(SOURCE_ELF_MMAP),$(FLAGS_LIBELF),-DLIBELF_MMAP),y)
	BASIC_CFLAGS += -DLIBELF_MMAP
endif

# include ARCH specific config
-include arch/$(ARCH)/Makefile

ifndef NO_DWARF
ifeq ($(origin PERF_HAVE_DWARF_REGS), undefined)
	msg := $(warning DWARF register mappings have not been defined for architecture $(ARCH), DWARF support disabled);
	NO_DWARF := 1
else
	BASIC_CFLAGS := -DDWARF_SUPPORT $(LIBDW_CFLAGS) $(BASIC_CFLAGS)
	BASIC_LDFLAGS := $(LIBDW_LDFLAGS) $(BASIC_LDFLAGS)
	EXTLIBS += -lelf -ldw
endif # PERF_HAVE_DWARF_REGS
endif # NO_DWARF

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

FLAGS_UNWIND=$(LIBUNWIND_CFLAGS) $(ALL_CFLAGS) $(LIBUNWIND_LDFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) $(LIBUNWIND_LIBS)
ifneq ($(call try-cc,$(SOURCE_LIBUNWIND),$(FLAGS_UNWIND),libunwind),y)
	msg := $(warning No libunwind found, disabling post unwind support. Please install libunwind-dev[el] >= 0.99);
	NO_LIBUNWIND := 1
endif # Libunwind support
endif # NO_LIBUNWIND

ifndef NO_LIBUNWIND
	BASIC_CFLAGS += -DLIBUNWIND_SUPPORT
	EXTLIBS += $(LIBUNWIND_LIBS)
	BASIC_CFLAGS := $(LIBUNWIND_CFLAGS) $(BASIC_CFLAGS)
	BASIC_LDFLAGS := $(LIBUNWIND_LDFLAGS) $(BASIC_LDFLAGS)
endif # NO_LIBUNWIND

ifndef NO_LIBAUDIT
	FLAGS_LIBAUDIT = $(ALL_CFLAGS) $(ALL_LDFLAGS) -laudit
	ifneq ($(call try-cc,$(SOURCE_LIBAUDIT),$(FLAGS_LIBAUDIT),libaudit),y)
		msg := $(warning No libaudit.h found, disables 'trace' tool, please install audit-libs-devel or libaudit-dev);
		NO_LIBAUDIT := 1
	else
		BASIC_CFLAGS += -DLIBAUDIT_SUPPORT
		EXTLIBS += -laudit
	endif
endif

ifdef NO_NEWT
	NO_SLANG=1
endif

ifndef NO_SLANG
	FLAGS_SLANG=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) -I/usr/include/slang -lslang
	ifneq ($(call try-cc,$(SOURCE_SLANG),$(FLAGS_SLANG),libslang),y)
		msg := $(warning slang not found, disables TUI support. Please install slang-devel or libslang-dev);
		NO_SLANG := 1
	else
		# Fedora has /usr/include/slang/slang.h, but ubuntu /usr/include/slang.h
		BASIC_CFLAGS += -I/usr/include/slang
		BASIC_CFLAGS += -DSLANG_SUPPORT
		EXTLIBS += -lslang
	endif
endif

ifndef NO_GTK2
	FLAGS_GTK2=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) $(shell pkg-config --libs --cflags gtk+-2.0 2>/dev/null)
	ifneq ($(call try-cc,$(SOURCE_GTK2),$(FLAGS_GTK2),gtk2),y)
		msg := $(warning GTK2 not found, disables GTK2 support. Please install gtk2-devel or libgtk2.0-dev);
		NO_GTK2 := 1
	else
		ifeq ($(call try-cc,$(SOURCE_GTK2_INFOBAR),$(FLAGS_GTK2),-DHAVE_GTK_INFO_BAR),y)
			BASIC_CFLAGS += -DHAVE_GTK_INFO_BAR
		endif
		BASIC_CFLAGS += -DGTK2_SUPPORT
		BASIC_CFLAGS += $(shell pkg-config --cflags gtk+-2.0 2>/dev/null)
		EXTLIBS += $(shell pkg-config --libs gtk+-2.0 2>/dev/null)
	endif
endif

grep-libs  = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

ifdef NO_LIBPERL
	BASIC_CFLAGS += -DNO_LIBPERL
else
	PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
	PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
	PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
	PERL_EMBED_CCOPTS = `perl -MExtUtils::Embed -e ccopts 2>/dev/null`
	FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

	ifneq ($(call try-cc,$(SOURCE_PERL_EMBED),$(FLAGS_PERL_EMBED),perl),y)
		BASIC_CFLAGS += -DNO_LIBPERL
		NO_LIBPERL := 1
	else
		ALL_LDFLAGS += $(PERL_EMBED_LDFLAGS)
		EXTLIBS += $(PERL_EMBED_LIBADD)
	endif
endif

disable-python = $(eval $(disable-python_code))
define disable-python_code
  BASIC_CFLAGS += -DNO_LIBPYTHON
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
          ALL_LDFLAGS += $(PYTHON_EMBED_LDFLAGS)
          EXTLIBS += $(PYTHON_EMBED_LIBADD)
          LANG_BINDINGS += $(OUTPUT)python/perf.so
        endif
      endif
    endif
  endif
endif

ifdef NO_DEMANGLE
	BASIC_CFLAGS += -DNO_DEMANGLE
else
	ifdef HAVE_CPLUS_DEMANGLE
		EXTLIBS += -liberty
		BASIC_CFLAGS += -DHAVE_CPLUS_DEMANGLE
	else
		FLAGS_BFD=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) -DPACKAGE='perf' -lbfd
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
					FLAGS_CPLUS_DEMANGLE=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) -liberty
					has_cplus_demangle := $(call try-cc,$(SOURCE_CPLUS_DEMANGLE),$(FLAGS_CPLUS_DEMANGLE),demangle)
					ifeq ($(has_cplus_demangle),y)
						EXTLIBS += -liberty
						BASIC_CFLAGS += -DHAVE_CPLUS_DEMANGLE
					else
						msg := $(warning No bfd.h/libbfd found, install binutils-dev[el]/zlib-static to gain symbol demangling)
						BASIC_CFLAGS += -DNO_DEMANGLE
					endif
				endif
			endif
		endif
	endif
endif

ifndef NO_STRLCPY
	ifeq ($(call try-cc,$(SOURCE_STRLCPY),,-DHAVE_STRLCPY),y)
		BASIC_CFLAGS += -DHAVE_STRLCPY
	endif
endif

ifndef NO_ON_EXIT
	ifeq ($(call try-cc,$(SOURCE_ON_EXIT),,-DHAVE_ON_EXIT),y)
		BASIC_CFLAGS += -DHAVE_ON_EXIT
	endif
endif

ifndef NO_BACKTRACE
       ifeq ($(call try-cc,$(SOURCE_BACKTRACE),,-DBACKTRACE_SUPPORT),y)
		BASIC_CFLAGS += -DBACKTRACE_SUPPORT
       endif
endif
