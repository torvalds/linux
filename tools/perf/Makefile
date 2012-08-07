include ../scripts/Makefile.include

# The default target of this Makefile is...
all:

include config/utilities.mak

# Define V to have a more verbose compile.
#
# Define O to save output files in a separate directory.
#
# Define ARCH as name of target architecture if you want cross-builds.
#
# Define CROSS_COMPILE as prefix name of compiler if you want cross-builds.
#
# Define NO_LIBPERL to disable perl script extension.
#
# Define NO_LIBPYTHON to disable python script extension.
#
# Define PYTHON to point to the python binary if the default
# `python' is not correct; for example: PYTHON=python2
#
# Define PYTHON_CONFIG to point to the python-config binary if
# the default `$(PYTHON)-config' is not correct.
#
# Define ASCIIDOC8 if you want to format documentation with AsciiDoc 8
#
# Define DOCBOOK_XSL_172 if you want to format man pages with DocBook XSL v1.72.
#
# Define LDFLAGS=-static to build a static binary.
#
# Define EXTRA_CFLAGS=-m64 or EXTRA_CFLAGS=-m32 as appropriate for cross-builds.
#
# Define NO_DWARF if you do not want debug-info analysis feature at all.
#
# Define WERROR=0 to disable treating any warnings as errors.
#
# Define NO_NEWT if you do not want TUI support.
#
# Define NO_DEMANGLE if you do not want C++ symbol demangling.

$(OUTPUT)PERF-VERSION-FILE: .FORCE-PERF-VERSION-FILE
	@$(SHELL_PATH) util/PERF-VERSION-GEN $(OUTPUT)
-include $(OUTPUT)PERF-VERSION-FILE

uname_M := $(shell uname -m 2>/dev/null || echo not)

ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
				  -e s/arm.*/arm/ -e s/sa110/arm/ \
				  -e s/s390x/s390/ -e s/parisc64/parisc/ \
				  -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
				  -e s/sh[234].*/sh/ )

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar

# Additional ARCH settings for x86
ifeq ($(ARCH),i386)
        ARCH := x86
endif
ifeq ($(ARCH),x86_64)
	ARCH := x86
	IS_X86_64 := 0
	ifeq (, $(findstring m32,$(EXTRA_CFLAGS)))
		IS_X86_64 := $(shell echo __x86_64__ | ${CC} -E -xc - | tail -n 1)
	endif
	ifeq (${IS_X86_64}, 1)
		RAW_ARCH := x86_64
		ARCH_CFLAGS := -DARCH_X86_64
		ARCH_INCLUDE = ../../arch/x86/lib/memcpy_64.S ../../arch/x86/lib/memset_64.S
	endif
endif

# Treat warnings as errors unless directed not to
ifneq ($(WERROR),0)
	CFLAGS_WERROR := -Werror
endif

ifeq ("$(origin DEBUG)", "command line")
  PERF_DEBUG = $(DEBUG)
endif
ifndef PERF_DEBUG
  CFLAGS_OPTIMIZE = -O6 -D_FORTIFY_SOURCE=2
endif

ifdef PARSER_DEBUG
	PARSER_DEBUG_BISON  := -t
	PARSER_DEBUG_FLEX   := -d
	PARSER_DEBUG_CFLAGS := -DPARSER_DEBUG
endif

CFLAGS = -fno-omit-frame-pointer -ggdb3 -Wall -Wextra -std=gnu99 $(CFLAGS_WERROR) $(CFLAGS_OPTIMIZE) $(EXTRA_WARNINGS) $(EXTRA_CFLAGS) $(PARSER_DEBUG_CFLAGS)
EXTLIBS = -lpthread -lrt -lelf -lm
ALL_CFLAGS = $(CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
ALL_LDFLAGS = $(LDFLAGS)
STRIP ?= strip

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

export prefix bindir sharedir sysconfdir

RM = rm -f
MKDIR = mkdir
FIND = find
INSTALL = install

# sparse is architecture-neutral, which means that we need to tell it
# explicitly what architecture to check for. Fix this up for yours..
SPARSE_FLAGS = -D__BIG_ENDIAN__ -D__powerpc__

-include config/feature-tests.mak

ifeq ($(call try-cc,$(SOURCE_HELLO),-Werror -fstack-protector-all),y)
	CFLAGS := $(CFLAGS) -fstack-protector-all
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),-Werror -Wstack-protector),y)
       CFLAGS := $(CFLAGS) -Wstack-protector
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),-Werror -Wvolatile-register-var),y)
       CFLAGS := $(CFLAGS) -Wvolatile-register-var
endif

### --- END CONFIGURATION SECTION ---

BASIC_CFLAGS = -Iutil/include -Iarch/$(ARCH)/include -I$(OUTPUT)util -I$(TRACE_EVENT_DIR) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
BASIC_LDFLAGS =

# Guard against environment variables
BUILTIN_OBJS =
LIB_H =
LIB_OBJS =
PYRF_OBJS =
SCRIPT_SH =

SCRIPT_SH += perf-archive.sh

grep-libs = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

PYTHON_EXT_SRCS := $(shell grep -v ^\# util/python-ext-sources)
PYTHON_EXT_DEPS := util/python-ext-sources util/setup.py

$(OUTPUT)python/perf.so: $(PYRF_OBJS) $(PYTHON_EXT_SRCS) $(PYTHON_EXT_DEPS)
	$(QUIET_GEN)CFLAGS='$(BASIC_CFLAGS)' $(PYTHON_WORD) util/setup.py \
	  --quiet build_ext; \
	mkdir -p $(OUTPUT)python && \
	cp $(PYTHON_EXTBUILD_LIB)perf.so $(OUTPUT)python/
#
# No Perl scripts right now:
#

SCRIPTS = $(patsubst %.sh,%,$(SCRIPT_SH))

TRACE_EVENT_DIR = ../lib/traceevent/

ifneq ($(OUTPUT),)
	TE_PATH=$(OUTPUT)
else
	TE_PATH=$(TRACE_EVENT_DIR)
endif

LIBTRACEEVENT = $(TE_PATH)libtraceevent.a
TE_LIB := -L$(TE_PATH) -ltraceevent

#
# Single 'perf' binary right now:
#
PROGRAMS += $(OUTPUT)perf

LANG_BINDINGS =

# what 'all' will build and 'install' will install, in perfexecdir
ALL_PROGRAMS = $(PROGRAMS) $(SCRIPTS)

# what 'all' will build but not install in perfexecdir
OTHER_PROGRAMS = $(OUTPUT)perf

# Set paths to tools early so that they can be used for version tests.
ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif

export PERL_PATH

FLEX = flex
BISON= bison

$(OUTPUT)util/parse-events-flex.c: util/parse-events.l
	$(QUIET_FLEX)$(FLEX) --header-file=$(OUTPUT)util/parse-events-flex.h $(PARSER_DEBUG_FLEX) -t util/parse-events.l > $(OUTPUT)util/parse-events-flex.c

$(OUTPUT)util/parse-events-bison.c: util/parse-events.y
	$(QUIET_BISON)$(BISON) -v util/parse-events.y -d $(PARSER_DEBUG_BISON) -o $(OUTPUT)util/parse-events-bison.c

$(OUTPUT)util/pmu-flex.c: util/pmu.l
	$(QUIET_FLEX)$(FLEX) --header-file=$(OUTPUT)util/pmu-flex.h -t util/pmu.l > $(OUTPUT)util/pmu-flex.c

$(OUTPUT)util/pmu-bison.c: util/pmu.y
	$(QUIET_BISON)$(BISON) -v util/pmu.y -d -o $(OUTPUT)util/pmu-bison.c

$(OUTPUT)util/parse-events.o: $(OUTPUT)util/parse-events-flex.c $(OUTPUT)util/parse-events-bison.c
$(OUTPUT)util/pmu.o: $(OUTPUT)util/pmu-flex.c $(OUTPUT)util/pmu-bison.c

LIB_FILE=$(OUTPUT)libperf.a

LIB_H += ../../include/linux/perf_event.h
LIB_H += ../../include/linux/rbtree.h
LIB_H += ../../include/linux/list.h
LIB_H += ../../include/linux/const.h
LIB_H += ../../include/linux/hash.h
LIB_H += ../../include/linux/stringify.h
LIB_H += util/include/linux/bitmap.h
LIB_H += util/include/linux/bitops.h
LIB_H += util/include/linux/compiler.h
LIB_H += util/include/linux/const.h
LIB_H += util/include/linux/ctype.h
LIB_H += util/include/linux/kernel.h
LIB_H += util/include/linux/list.h
LIB_H += util/include/linux/export.h
LIB_H += util/include/linux/poison.h
LIB_H += util/include/linux/prefetch.h
LIB_H += util/include/linux/rbtree.h
LIB_H += util/include/linux/string.h
LIB_H += util/include/linux/types.h
LIB_H += util/include/linux/linkage.h
LIB_H += util/include/asm/asm-offsets.h
LIB_H += util/include/asm/bug.h
LIB_H += util/include/asm/byteorder.h
LIB_H += util/include/asm/hweight.h
LIB_H += util/include/asm/swab.h
LIB_H += util/include/asm/system.h
LIB_H += util/include/asm/uaccess.h
LIB_H += util/include/dwarf-regs.h
LIB_H += util/include/asm/dwarf2.h
LIB_H += util/include/asm/cpufeature.h
LIB_H += util/include/asm/unistd_32.h
LIB_H += util/include/asm/unistd_64.h
LIB_H += perf.h
LIB_H += util/annotate.h
LIB_H += util/cache.h
LIB_H += util/callchain.h
LIB_H += util/build-id.h
LIB_H += util/debug.h
LIB_H += util/debugfs.h
LIB_H += util/sysfs.h
LIB_H += util/pmu.h
LIB_H += util/event.h
LIB_H += util/evsel.h
LIB_H += util/evlist.h
LIB_H += util/exec_cmd.h
LIB_H += util/types.h
LIB_H += util/levenshtein.h
LIB_H += util/map.h
LIB_H += util/parse-options.h
LIB_H += util/parse-events.h
LIB_H += util/quote.h
LIB_H += util/util.h
LIB_H += util/xyarray.h
LIB_H += util/header.h
LIB_H += util/help.h
LIB_H += util/session.h
LIB_H += util/strbuf.h
LIB_H += util/strlist.h
LIB_H += util/strfilter.h
LIB_H += util/svghelper.h
LIB_H += util/tool.h
LIB_H += util/run-command.h
LIB_H += util/sigchain.h
LIB_H += util/symbol.h
LIB_H += util/color.h
LIB_H += util/values.h
LIB_H += util/sort.h
LIB_H += util/hist.h
LIB_H += util/thread.h
LIB_H += util/thread_map.h
LIB_H += util/trace-event.h
LIB_H += util/probe-finder.h
LIB_H += util/dwarf-aux.h
LIB_H += util/probe-event.h
LIB_H += util/pstack.h
LIB_H += util/cpumap.h
LIB_H += util/top.h
LIB_H += $(ARCH_INCLUDE)
LIB_H += util/cgroup.h
LIB_H += $(TRACE_EVENT_DIR)event-parse.h
LIB_H += util/target.h
LIB_H += util/rblist.h
LIB_H += util/intlist.h

LIB_OBJS += $(OUTPUT)util/abspath.o
LIB_OBJS += $(OUTPUT)util/alias.o
LIB_OBJS += $(OUTPUT)util/annotate.o
LIB_OBJS += $(OUTPUT)util/build-id.o
LIB_OBJS += $(OUTPUT)util/config.o
LIB_OBJS += $(OUTPUT)util/ctype.o
LIB_OBJS += $(OUTPUT)util/debugfs.o
LIB_OBJS += $(OUTPUT)util/sysfs.o
LIB_OBJS += $(OUTPUT)util/pmu.o
LIB_OBJS += $(OUTPUT)util/environment.o
LIB_OBJS += $(OUTPUT)util/event.o
LIB_OBJS += $(OUTPUT)util/evlist.o
LIB_OBJS += $(OUTPUT)util/evsel.o
LIB_OBJS += $(OUTPUT)util/exec_cmd.o
LIB_OBJS += $(OUTPUT)util/help.o
LIB_OBJS += $(OUTPUT)util/levenshtein.o
LIB_OBJS += $(OUTPUT)util/parse-options.o
LIB_OBJS += $(OUTPUT)util/parse-events.o
LIB_OBJS += $(OUTPUT)util/parse-events-test.o
LIB_OBJS += $(OUTPUT)util/path.o
LIB_OBJS += $(OUTPUT)util/rbtree.o
LIB_OBJS += $(OUTPUT)util/bitmap.o
LIB_OBJS += $(OUTPUT)util/hweight.o
LIB_OBJS += $(OUTPUT)util/run-command.o
LIB_OBJS += $(OUTPUT)util/quote.o
LIB_OBJS += $(OUTPUT)util/strbuf.o
LIB_OBJS += $(OUTPUT)util/string.o
LIB_OBJS += $(OUTPUT)util/strlist.o
LIB_OBJS += $(OUTPUT)util/strfilter.o
LIB_OBJS += $(OUTPUT)util/top.o
LIB_OBJS += $(OUTPUT)util/usage.o
LIB_OBJS += $(OUTPUT)util/wrapper.o
LIB_OBJS += $(OUTPUT)util/sigchain.o
LIB_OBJS += $(OUTPUT)util/symbol.o
LIB_OBJS += $(OUTPUT)util/dso-test-data.o
LIB_OBJS += $(OUTPUT)util/color.o
LIB_OBJS += $(OUTPUT)util/pager.o
LIB_OBJS += $(OUTPUT)util/header.o
LIB_OBJS += $(OUTPUT)util/callchain.o
LIB_OBJS += $(OUTPUT)util/values.o
LIB_OBJS += $(OUTPUT)util/debug.o
LIB_OBJS += $(OUTPUT)util/map.o
LIB_OBJS += $(OUTPUT)util/pstack.o
LIB_OBJS += $(OUTPUT)util/session.o
LIB_OBJS += $(OUTPUT)util/thread.o
LIB_OBJS += $(OUTPUT)util/thread_map.o
LIB_OBJS += $(OUTPUT)util/trace-event-parse.o
LIB_OBJS += $(OUTPUT)util/parse-events-flex.o
LIB_OBJS += $(OUTPUT)util/parse-events-bison.o
LIB_OBJS += $(OUTPUT)util/pmu-flex.o
LIB_OBJS += $(OUTPUT)util/pmu-bison.o
LIB_OBJS += $(OUTPUT)util/trace-event-read.o
LIB_OBJS += $(OUTPUT)util/trace-event-info.o
LIB_OBJS += $(OUTPUT)util/trace-event-scripting.o
LIB_OBJS += $(OUTPUT)util/svghelper.o
LIB_OBJS += $(OUTPUT)util/sort.o
LIB_OBJS += $(OUTPUT)util/hist.o
LIB_OBJS += $(OUTPUT)util/probe-event.o
LIB_OBJS += $(OUTPUT)util/util.o
LIB_OBJS += $(OUTPUT)util/xyarray.o
LIB_OBJS += $(OUTPUT)util/cpumap.o
LIB_OBJS += $(OUTPUT)util/cgroup.o
LIB_OBJS += $(OUTPUT)util/target.o
LIB_OBJS += $(OUTPUT)util/rblist.o
LIB_OBJS += $(OUTPUT)util/intlist.o

BUILTIN_OBJS += $(OUTPUT)builtin-annotate.o

BUILTIN_OBJS += $(OUTPUT)builtin-bench.o

# Benchmark modules
BUILTIN_OBJS += $(OUTPUT)bench/sched-messaging.o
BUILTIN_OBJS += $(OUTPUT)bench/sched-pipe.o
ifeq ($(RAW_ARCH),x86_64)
BUILTIN_OBJS += $(OUTPUT)bench/mem-memcpy-x86-64-asm.o
BUILTIN_OBJS += $(OUTPUT)bench/mem-memset-x86-64-asm.o
endif
BUILTIN_OBJS += $(OUTPUT)bench/mem-memcpy.o
BUILTIN_OBJS += $(OUTPUT)bench/mem-memset.o

BUILTIN_OBJS += $(OUTPUT)builtin-diff.o
BUILTIN_OBJS += $(OUTPUT)builtin-evlist.o
BUILTIN_OBJS += $(OUTPUT)builtin-help.o
BUILTIN_OBJS += $(OUTPUT)builtin-sched.o
BUILTIN_OBJS += $(OUTPUT)builtin-buildid-list.o
BUILTIN_OBJS += $(OUTPUT)builtin-buildid-cache.o
BUILTIN_OBJS += $(OUTPUT)builtin-list.o
BUILTIN_OBJS += $(OUTPUT)builtin-record.o
BUILTIN_OBJS += $(OUTPUT)builtin-report.o
BUILTIN_OBJS += $(OUTPUT)builtin-stat.o
BUILTIN_OBJS += $(OUTPUT)builtin-timechart.o
BUILTIN_OBJS += $(OUTPUT)builtin-top.o
BUILTIN_OBJS += $(OUTPUT)builtin-script.o
BUILTIN_OBJS += $(OUTPUT)builtin-probe.o
BUILTIN_OBJS += $(OUTPUT)builtin-kmem.o
BUILTIN_OBJS += $(OUTPUT)builtin-lock.o
BUILTIN_OBJS += $(OUTPUT)builtin-kvm.o
BUILTIN_OBJS += $(OUTPUT)builtin-test.o
BUILTIN_OBJS += $(OUTPUT)builtin-inject.o

PERFLIBS = $(LIB_FILE) $(LIBTRACEEVENT)

# Files needed for the python binding, perf.so
# pyrf is just an internal name needed for all those wrappers.
# This has to be in sync with what is in the 'sources' variable in
# tools/perf/util/setup.py

PYRF_OBJS += $(OUTPUT)util/cpumap.o
PYRF_OBJS += $(OUTPUT)util/ctype.o
PYRF_OBJS += $(OUTPUT)util/evlist.o
PYRF_OBJS += $(OUTPUT)util/evsel.o
PYRF_OBJS += $(OUTPUT)util/python.o
PYRF_OBJS += $(OUTPUT)util/thread_map.o
PYRF_OBJS += $(OUTPUT)util/util.o
PYRF_OBJS += $(OUTPUT)util/xyarray.o

#
# Platform specific tweaks
#

# We choose to avoid "if .. else if .. else .. endif endif"
# because maintaining the nesting to match is a pain.  If
# we had "elif" things would have been much nicer...

-include config.mak.autogen
-include config.mak

ifndef NO_DWARF
FLAGS_DWARF=$(ALL_CFLAGS) -ldw -lelf $(ALL_LDFLAGS) $(EXTLIBS)
ifneq ($(call try-cc,$(SOURCE_DWARF),$(FLAGS_DWARF)),y)
	msg := $(warning No libdw.h found or old libdw.h found or elfutils is older than 0.138, disables dwarf support. Please install new elfutils-devel/libdw-dev);
	NO_DWARF := 1
endif # Dwarf support
endif # NO_DWARF

-include arch/$(ARCH)/Makefile

ifneq ($(OUTPUT),)
	BASIC_CFLAGS += -I$(OUTPUT)
endif

FLAGS_LIBELF=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS)
ifneq ($(call try-cc,$(SOURCE_LIBELF),$(FLAGS_LIBELF)),y)
	FLAGS_GLIBC=$(ALL_CFLAGS) $(ALL_LDFLAGS)
	ifneq ($(call try-cc,$(SOURCE_GLIBC),$(FLAGS_GLIBC)),y)
		msg := $(error No gnu/libc-version.h found, please install glibc-dev[el]/glibc-static);
	else
		msg := $(error No libelf.h/libelf found, please install libelf-dev/elfutils-libelf-devel);
	endif
endif

ifneq ($(call try-cc,$(SOURCE_ELF_MMAP),$(FLAGS_COMMON)),y)
	BASIC_CFLAGS += -DLIBELF_NO_MMAP
endif

ifndef NO_DWARF
ifeq ($(origin PERF_HAVE_DWARF_REGS), undefined)
	msg := $(warning DWARF register mappings have not been defined for architecture $(ARCH), DWARF support disabled);
else
	BASIC_CFLAGS += -DDWARF_SUPPORT
	EXTLIBS += -lelf -ldw
	LIB_OBJS += $(OUTPUT)util/probe-finder.o
	LIB_OBJS += $(OUTPUT)util/dwarf-aux.o
endif # PERF_HAVE_DWARF_REGS
endif # NO_DWARF

ifdef NO_NEWT
	BASIC_CFLAGS += -DNO_NEWT_SUPPORT
else
	FLAGS_NEWT=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) -lnewt
	ifneq ($(call try-cc,$(SOURCE_NEWT),$(FLAGS_NEWT)),y)
		msg := $(warning newt not found, disables TUI support. Please install newt-devel or libnewt-dev);
		BASIC_CFLAGS += -DNO_NEWT_SUPPORT
	else
		# Fedora has /usr/include/slang/slang.h, but ubuntu /usr/include/slang.h
		BASIC_CFLAGS += -I/usr/include/slang
		EXTLIBS += -lnewt -lslang
		LIB_OBJS += $(OUTPUT)ui/setup.o
		LIB_OBJS += $(OUTPUT)ui/browser.o
		LIB_OBJS += $(OUTPUT)ui/browsers/annotate.o
		LIB_OBJS += $(OUTPUT)ui/browsers/hists.o
		LIB_OBJS += $(OUTPUT)ui/browsers/map.o
		LIB_OBJS += $(OUTPUT)ui/helpline.o
		LIB_OBJS += $(OUTPUT)ui/progress.o
		LIB_OBJS += $(OUTPUT)ui/util.o
		LIB_OBJS += $(OUTPUT)ui/tui/setup.o
		LIB_OBJS += $(OUTPUT)ui/tui/util.o
		LIB_H += ui/browser.h
		LIB_H += ui/browsers/map.h
		LIB_H += ui/helpline.h
		LIB_H += ui/keysyms.h
		LIB_H += ui/libslang.h
		LIB_H += ui/progress.h
		LIB_H += ui/util.h
		LIB_H += ui/ui.h
	endif
endif

ifdef NO_GTK2
	BASIC_CFLAGS += -DNO_GTK2_SUPPORT
else
	FLAGS_GTK2=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) $(shell pkg-config --libs --cflags gtk+-2.0)
	ifneq ($(call try-cc,$(SOURCE_GTK2),$(FLAGS_GTK2)),y)
		msg := $(warning GTK2 not found, disables GTK2 support. Please install gtk2-devel or libgtk2.0-dev);
		BASIC_CFLAGS += -DNO_GTK2_SUPPORT
	else
		ifeq ($(call try-cc,$(SOURCE_GTK2_INFOBAR),$(FLAGS_GTK2)),y)
			BASIC_CFLAGS += -DHAVE_GTK_INFO_BAR
		endif
		BASIC_CFLAGS += $(shell pkg-config --cflags gtk+-2.0)
		EXTLIBS += $(shell pkg-config --libs gtk+-2.0)
		LIB_OBJS += $(OUTPUT)ui/gtk/browser.o
		LIB_OBJS += $(OUTPUT)ui/gtk/setup.o
		LIB_OBJS += $(OUTPUT)ui/gtk/util.o
		# Make sure that it'd be included only once.
		ifneq ($(findstring -DNO_NEWT_SUPPORT,$(BASIC_CFLAGS)),)
			LIB_OBJS += $(OUTPUT)ui/setup.o
			LIB_OBJS += $(OUTPUT)ui/util.o
		endif
	endif
endif

ifdef NO_LIBPERL
	BASIC_CFLAGS += -DNO_LIBPERL
else
       PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
       PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
       PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
	PERL_EMBED_CCOPTS = `perl -MExtUtils::Embed -e ccopts 2>/dev/null`
	FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

	ifneq ($(call try-cc,$(SOURCE_PERL_EMBED),$(FLAGS_PERL_EMBED)),y)
		BASIC_CFLAGS += -DNO_LIBPERL
	else
               ALL_LDFLAGS += $(PERL_EMBED_LDFLAGS)
               EXTLIBS += $(PERL_EMBED_LIBADD)
		LIB_OBJS += $(OUTPUT)util/scripting-engines/trace-event-perl.o
		LIB_OBJS += $(OUTPUT)scripts/perl/Perf-Trace-Util/Context.o
	endif
endif

disable-python = $(eval $(disable-python_code))
define disable-python_code
  BASIC_CFLAGS += -DNO_LIBPYTHON
  $(if $(1),$(warning No $(1) was found))
  $(warning Python support won't be built)
endef

override PYTHON := \
  $(call get-executable-or-default,PYTHON,python)

ifndef PYTHON
  $(call disable-python,python interpreter)
  python-clean :=
else

  PYTHON_WORD := $(call shell-wordify,$(PYTHON))

  # python extension build directories
  PYTHON_EXTBUILD     := $(OUTPUT)python_ext_build/
  PYTHON_EXTBUILD_LIB := $(PYTHON_EXTBUILD)lib/
  PYTHON_EXTBUILD_TMP := $(PYTHON_EXTBUILD)tmp/
  export PYTHON_EXTBUILD_LIB PYTHON_EXTBUILD_TMP

  python-clean := rm -rf $(PYTHON_EXTBUILD) $(OUTPUT)python/perf.so

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

      ifneq ($(call try-cc,$(SOURCE_PYTHON_EMBED),$(FLAGS_PYTHON_EMBED)),y)
        $(call disable-python,Python.h (for Python 2.x))
      else

        ifneq ($(call try-cc,$(SOURCE_PYTHON_VERSION),$(FLAGS_PYTHON_EMBED)),y)
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
          LIB_OBJS += $(OUTPUT)util/scripting-engines/trace-event-python.o
          LIB_OBJS += $(OUTPUT)scripts/python/Perf-Trace-Util/Context.o
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
		FLAGS_BFD=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) -lbfd
		has_bfd := $(call try-cc,$(SOURCE_BFD),$(FLAGS_BFD))
		ifeq ($(has_bfd),y)
			EXTLIBS += -lbfd
		else
			FLAGS_BFD_IBERTY=$(FLAGS_BFD) -liberty
			has_bfd_iberty := $(call try-cc,$(SOURCE_BFD),$(FLAGS_BFD_IBERTY))
			ifeq ($(has_bfd_iberty),y)
				EXTLIBS += -lbfd -liberty
			else
				FLAGS_BFD_IBERTY_Z=$(FLAGS_BFD_IBERTY) -lz
				has_bfd_iberty_z := $(call try-cc,$(SOURCE_BFD),$(FLAGS_BFD_IBERTY_Z))
				ifeq ($(has_bfd_iberty_z),y)
					EXTLIBS += -lbfd -liberty -lz
				else
					FLAGS_CPLUS_DEMANGLE=$(ALL_CFLAGS) $(ALL_LDFLAGS) $(EXTLIBS) -liberty
					has_cplus_demangle := $(call try-cc,$(SOURCE_CPLUS_DEMANGLE),$(FLAGS_CPLUS_DEMANGLE))
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


ifdef NO_STRLCPY
	BASIC_CFLAGS += -DNO_STRLCPY
else
	ifneq ($(call try-cc,$(SOURCE_STRLCPY),),y)
		BASIC_CFLAGS += -DNO_STRLCPY
	endif
endif

ifdef ASCIIDOC8
	export ASCIIDOC8
endif

# Shell quote (do not use $(call) to accommodate ancient setups);

ETC_PERFCONFIG_SQ = $(subst ','\'',$(ETC_PERFCONFIG))

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
bindir_relative_SQ = $(subst ','\'',$(bindir_relative))
mandir_SQ = $(subst ','\'',$(mandir))
infodir_SQ = $(subst ','\'',$(infodir))
perfexecdir_SQ = $(subst ','\'',$(perfexecdir))
template_dir_SQ = $(subst ','\'',$(template_dir))
htmldir_SQ = $(subst ','\'',$(htmldir))
prefix_SQ = $(subst ','\'',$(prefix))

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

LIBS = -Wl,--whole-archive $(PERFLIBS) -Wl,--no-whole-archive -Wl,--start-group $(EXTLIBS) -Wl,--end-group

ALL_CFLAGS += $(BASIC_CFLAGS)
ALL_CFLAGS += $(ARCH_CFLAGS)
ALL_LDFLAGS += $(BASIC_LDFLAGS)

export INSTALL SHELL_PATH


### Build rules

SHELL = $(SHELL_PATH)

all: shell_compatibility_test $(ALL_PROGRAMS) $(LANG_BINDINGS) $(OTHER_PROGRAMS)

please_set_SHELL_PATH_to_a_more_modern_shell:
	@$$(:)

shell_compatibility_test: please_set_SHELL_PATH_to_a_more_modern_shell

strip: $(PROGRAMS) $(OUTPUT)perf
	$(STRIP) $(STRIP_OPTS) $(PROGRAMS) $(OUTPUT)perf

$(OUTPUT)perf.o: perf.c $(OUTPUT)common-cmds.h $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -DPERF_VERSION='"$(PERF_VERSION)"' \
		'-DPERF_HTML_PATH="$(htmldir_SQ)"' \
		$(ALL_CFLAGS) -c $(filter %.c,$^) -o $@

$(OUTPUT)perf: $(OUTPUT)perf.o $(BUILTIN_OBJS) $(PERFLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) $(OUTPUT)perf.o \
               $(BUILTIN_OBJS) $(LIBS) -o $@

$(OUTPUT)builtin-help.o: builtin-help.c $(OUTPUT)common-cmds.h $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) \
		'-DPERF_HTML_PATH="$(htmldir_SQ)"' \
		'-DPERF_MAN_PATH="$(mandir_SQ)"' \
		'-DPERF_INFO_PATH="$(infodir_SQ)"' $<

$(OUTPUT)builtin-timechart.o: builtin-timechart.c $(OUTPUT)common-cmds.h $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) \
		'-DPERF_HTML_PATH="$(htmldir_SQ)"' \
		'-DPERF_MAN_PATH="$(mandir_SQ)"' \
		'-DPERF_INFO_PATH="$(infodir_SQ)"' $<

$(OUTPUT)common-cmds.h: util/generate-cmdlist.sh command-list.txt

$(OUTPUT)common-cmds.h: $(wildcard Documentation/perf-*.txt)
	$(QUIET_GEN). util/generate-cmdlist.sh > $@+ && mv $@+ $@

$(SCRIPTS) : % : %.sh
	$(QUIET_GEN)$(INSTALL) '$@.sh' '$(OUTPUT)$@'

# These can record PERF_VERSION
$(OUTPUT)perf.o perf.spec \
	$(SCRIPTS) \
	: $(OUTPUT)PERF-VERSION-FILE

.SUFFIXES:
.SUFFIXES: .o .c .S .s

# These two need to be here so that when O= is not used they take precedence
# over the general rule for .o

$(OUTPUT)util/%-flex.o: $(OUTPUT)util/%-flex.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -Iutil/ -w $<

$(OUTPUT)util/%-bison.o: $(OUTPUT)util/%-bison.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DYYENABLE_NLS=0 -DYYLTYPE_IS_TRIVIAL=0 -Iutil/ -w $<

$(OUTPUT)%.o: %.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $<
$(OUTPUT)%.i: %.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -E $(ALL_CFLAGS) $<
$(OUTPUT)%.s: %.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -S $(ALL_CFLAGS) $<
$(OUTPUT)%.o: %.S
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $<
$(OUTPUT)%.s: %.S
	$(QUIET_CC)$(CC) -o $@ -E $(ALL_CFLAGS) $<

$(OUTPUT)util/exec_cmd.o: util/exec_cmd.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) \
		'-DPERF_EXEC_PATH="$(perfexecdir_SQ)"' \
		'-DBINDIR="$(bindir_relative_SQ)"' \
		'-DPREFIX="$(prefix_SQ)"' \
		$<

$(OUTPUT)util/config.o: util/config.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DETC_PERFCONFIG='"$(ETC_PERFCONFIG_SQ)"' $<

$(OUTPUT)ui/browser.o: ui/browser.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DENABLE_SLFUTURE_CONST $<

$(OUTPUT)ui/browsers/annotate.o: ui/browsers/annotate.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DENABLE_SLFUTURE_CONST $<

$(OUTPUT)ui/browsers/hists.o: ui/browsers/hists.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DENABLE_SLFUTURE_CONST $<

$(OUTPUT)ui/browsers/map.o: ui/browsers/map.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DENABLE_SLFUTURE_CONST $<

$(OUTPUT)util/rbtree.o: ../../lib/rbtree.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -DETC_PERFCONFIG='"$(ETC_PERFCONFIG_SQ)"' $<

$(OUTPUT)util/parse-events.o: util/parse-events.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) -Wno-redundant-decls $<

$(OUTPUT)util/scripting-engines/trace-event-perl.o: util/scripting-engines/trace-event-perl.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $(PERL_EMBED_CCOPTS) -Wno-redundant-decls -Wno-strict-prototypes -Wno-unused-parameter -Wno-shadow $<

$(OUTPUT)scripts/perl/Perf-Trace-Util/Context.o: scripts/perl/Perf-Trace-Util/Context.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $(PERL_EMBED_CCOPTS) -Wno-redundant-decls -Wno-strict-prototypes -Wno-unused-parameter -Wno-nested-externs $<

$(OUTPUT)util/scripting-engines/trace-event-python.o: util/scripting-engines/trace-event-python.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $(PYTHON_EMBED_CCOPTS) -Wno-redundant-decls -Wno-strict-prototypes -Wno-unused-parameter -Wno-shadow $<

$(OUTPUT)scripts/python/Perf-Trace-Util/Context.o: scripts/python/Perf-Trace-Util/Context.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $(PYTHON_EMBED_CCOPTS) -Wno-redundant-decls -Wno-strict-prototypes -Wno-unused-parameter -Wno-nested-externs $<

$(OUTPUT)perf-%: %.o $(PERFLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

$(LIB_OBJS) $(BUILTIN_OBJS): $(LIB_H)
$(patsubst perf-%,%.o,$(PROGRAMS)): $(LIB_H) $(wildcard */*.h)

# we compile into subdirectories. if the target directory is not the source directory, they might not exists. So
# we depend the various files onto their directories.
DIRECTORY_DEPS = $(LIB_OBJS) $(BUILTIN_OBJS) $(OUTPUT)PERF-VERSION-FILE $(OUTPUT)common-cmds.h
$(DIRECTORY_DEPS): | $(sort $(dir $(DIRECTORY_DEPS)))
# In the second step, we make a rule to actually create these directories
$(sort $(dir $(DIRECTORY_DEPS))):
	$(QUIET_MKDIR)$(MKDIR) -p $@ 2>/dev/null

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(LIB_OBJS)

# libtraceevent.a
$(LIBTRACEEVENT):
	$(QUIET_SUBDIR0)$(TRACE_EVENT_DIR) $(QUIET_SUBDIR1) O=$(OUTPUT) libtraceevent.a

help:
	@echo 'Perf make targets:'
	@echo '  doc		- make *all* documentation (see below)'
	@echo '  man		- make manpage documentation (access with man <foo>)'
	@echo '  html		- make html documentation'
	@echo '  info		- make GNU info documentation (access with info <foo>)'
	@echo '  pdf		- make pdf documentation'
	@echo '  TAGS		- use etags to make tag information for source browsing'
	@echo '  tags		- use ctags to make tag information for source browsing'
	@echo '  cscope	- use cscope to make interactive browsing database'
	@echo ''
	@echo 'Perf install targets:'
	@echo '  NOTE: documentation build requires asciidoc, xmlto packages to be installed'
	@echo '  HINT: use "make prefix=<path> <install target>" to install to a particular'
	@echo '        path like make prefix=/usr/local install install-doc'
	@echo '  install	- install compiled binaries'
	@echo '  install-doc	- install *all* documentation'
	@echo '  install-man	- install manpage documentation'
	@echo '  install-html	- install html documentation'
	@echo '  install-info	- install GNU info documentation'
	@echo '  install-pdf	- install pdf documentation'
	@echo ''
	@echo '  quick-install-doc	- alias for quick-install-man'
	@echo '  quick-install-man	- install the documentation quickly'
	@echo '  quick-install-html	- install the html documentation quickly'
	@echo ''
	@echo 'Perf maintainer targets:'
	@echo '  clean			- clean all binary objects and build output'

doc:
	$(MAKE) -C Documentation all

man:
	$(MAKE) -C Documentation man

html:
	$(MAKE) -C Documentation html

info:
	$(MAKE) -C Documentation info

pdf:
	$(MAKE) -C Documentation pdf

TAGS:
	$(RM) TAGS
	$(FIND) . -name '*.[hcS]' -print | xargs etags -a

tags:
	$(RM) tags
	$(FIND) . -name '*.[hcS]' -print | xargs ctags -a

cscope:
	$(RM) cscope*
	$(FIND) . -name '*.[hcS]' -print | xargs cscope -b

### Detect prefix changes
TRACK_CFLAGS = $(subst ','\'',$(ALL_CFLAGS)):\
             $(bindir_SQ):$(perfexecdir_SQ):$(template_dir_SQ):$(prefix_SQ)

$(OUTPUT)PERF-CFLAGS: .FORCE-PERF-CFLAGS
	@FLAGS='$(TRACK_CFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat $(OUTPUT)PERF-CFLAGS 2>/dev/null`" ; then \
		echo 1>&2 "    * new build flags or prefix"; \
		echo "$$FLAGS" >$(OUTPUT)PERF-CFLAGS; \
            fi

### Testing rules

# GNU make supports exporting all variables by "export" without parameters.
# However, the environment gets quite big, and some programs have problems
# with that.

check: $(OUTPUT)common-cmds.h
	if sparse; \
	then \
		for i in *.c */*.c; \
		do \
			sparse $(ALL_CFLAGS) $(SPARSE_FLAGS) $$i || exit; \
		done; \
	else \
		exit 1; \
	fi

### Installation rules

ifneq ($(filter /%,$(firstword $(perfexecdir))),)
perfexec_instdir = $(perfexecdir)
else
perfexec_instdir = $(prefix)/$(perfexecdir)
endif
perfexec_instdir_SQ = $(subst ','\'',$(perfexec_instdir))

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) $(OUTPUT)perf '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/perl/Perf-Trace-Util/lib/Perf/Trace'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/perl/bin'
	$(INSTALL) $(OUTPUT)perf-archive -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)'
	$(INSTALL) scripts/perl/Perf-Trace-Util/lib/Perf/Trace/* -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/perl/Perf-Trace-Util/lib/Perf/Trace'
	$(INSTALL) scripts/perl/*.pl -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/perl'
	$(INSTALL) scripts/perl/bin/* -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/perl/bin'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/python/Perf-Trace-Util/lib/Perf/Trace'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/python/bin'
	$(INSTALL) scripts/python/Perf-Trace-Util/lib/Perf/Trace/* -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/python/Perf-Trace-Util/lib/Perf/Trace'
	$(INSTALL) scripts/python/*.py -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/python'
	$(INSTALL) scripts/python/bin/* -t '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/scripts/python/bin'

install-python_ext:
	$(PYTHON_WORD) util/setup.py --quiet install --root='/$(DESTDIR_SQ)'

install-doc:
	$(MAKE) -C Documentation install

install-man:
	$(MAKE) -C Documentation install-man

install-html:
	$(MAKE) -C Documentation install-html

install-info:
	$(MAKE) -C Documentation install-info

install-pdf:
	$(MAKE) -C Documentation install-pdf

quick-install-doc:
	$(MAKE) -C Documentation quick-install

quick-install-man:
	$(MAKE) -C Documentation quick-install-man

quick-install-html:
	$(MAKE) -C Documentation quick-install-html

### Cleaning rules

clean:
	$(RM) $(LIB_OBJS) $(BUILTIN_OBJS) $(LIB_FILE) $(OUTPUT)perf-archive $(OUTPUT)perf.o $(LANG_BINDINGS)
	$(RM) $(ALL_PROGRAMS) perf
	$(RM) *.spec *.pyc *.pyo */*.pyc */*.pyo $(OUTPUT)common-cmds.h TAGS tags cscope*
	$(MAKE) -C Documentation/ clean
	$(RM) $(OUTPUT)PERF-VERSION-FILE $(OUTPUT)PERF-CFLAGS
	$(RM) $(OUTPUT)util/*-bison*
	$(RM) $(OUTPUT)util/*-flex*
	$(python-clean)

.PHONY: all install clean strip $(LIBTRACEEVENT)
.PHONY: shell_compatibility_test please_set_SHELL_PATH_to_a_more_modern_shell
.PHONY: .FORCE-PERF-VERSION-FILE TAGS tags cscope .FORCE-PERF-CFLAGS
