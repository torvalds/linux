# The default target of this Makefile is...
all::

# Define V=1 to have a more verbose compile.
#
# Define SNPRINTF_RETURNS_BOGUS if your are on a system which snprintf()
# or vsnprintf() return -1 instead of number of characters which would
# have been written to the final string if enough space had been available.
#
# Define FREAD_READS_DIRECTORIES if your are on a system which succeeds
# when attempting to read from an fopen'ed directory.
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL.
# This also implies MOZILLA_SHA1.
#
# Define CURLDIR=/foo/bar if your curl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define EXPATDIR=/foo/bar if your expat header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define NO_D_INO_IN_DIRENT if you don't have d_ino in your struct dirent.
#
# Define NO_D_TYPE_IN_DIRENT if your platform defines DT_UNKNOWN but lacks
# d_type in struct dirent (latest Cygwin -- will be fixed soonish).
#
# Define NO_C99_FORMAT if your formatted IO functions (printf/scanf et.al.)
# do not support the 'size specifiers' introduced by C99, namely ll, hh,
# j, z, t. (representing long long int, char, intmax_t, size_t, ptrdiff_t).
# some C compilers supported these specifiers prior to C99 as an extension.
#
# Define NO_STRCASESTR if you don't have strcasestr.
#
# Define NO_MEMMEM if you don't have memmem.
#
# Define NO_STRTOUMAX if you don't have strtoumax in the C library.
# If your compiler also does not support long long or does not have
# strtoull, define NO_STRTOULL.
#
# Define NO_SETENV if you don't have setenv in the C library.
#
# Define NO_UNSETENV if you don't have unsetenv in the C library.
#
# Define NO_MKDTEMP if you don't have mkdtemp in the C library.
#
# Define NO_SYS_SELECT_H if you don't have sys/select.h.
#
# Define NO_SYMLINK_HEAD if you never want .perf/HEAD to be a symbolic link.
# Enable it on Windows.  By default, symrefs are still used.
#
# Define NO_SVN_TESTS if you want to skip time-consuming SVN interoperability
# tests.  These tests take up a significant amount of the total test time
# but are not needed unless you plan to talk to SVN repos.
#
# Define NO_FINK if you are building on Darwin/Mac OS X, have Fink
# installed in /sw, but don't want PERF to link against any libraries
# installed there.  If defined you may specify your own (or Fink's)
# include directories and library directories by defining CFLAGS
# and LDFLAGS appropriately.
#
# Define NO_DARWIN_PORTS if you are building on Darwin/Mac OS X,
# have DarwinPorts installed in /opt/local, but don't want PERF to
# link against any libraries installed there.  If defined you may
# specify your own (or DarwinPort's) include directories and
# library directories by defining CFLAGS and LDFLAGS appropriately.
#
# Define PPC_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for PowerPC.
#
# Define ARM_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for ARM.
#
# Define MOZILLA_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine coming from Mozilla. It is GPL'd and should be fast
# on non-x86 architectures (e.g. PowerPC), while the OpenSSL version (default
# choice) has very fast version optimized for i586.
#
# Define NEEDS_SSL_WITH_CRYPTO if you need -lcrypto with -lssl (Darwin).
#
# Define NEEDS_LIBICONV if linking with libc is not enough (Darwin).
#
# Define NEEDS_SOCKET if linking with libc is not enough (SunOS,
# Patrick Mauritz).
#
# Define NO_MMAP if you want to avoid mmap.
#
# Define NO_PTHREADS if you do not have or do not want to use Pthreads.
#
# Define NO_PREAD if you have a problem with pread() system call (e.g.
# cygwin.dll before v1.5.22).
#
# Define NO_FAST_WORKING_DIRECTORY if accessing objects in pack files is
# generally faster on your platform than accessing the working directory.
#
# Define NO_TRUSTABLE_FILEMODE if your filesystem may claim to support
# the executable mode bit, but doesn't really do so.
#
# Define NO_IPV6 if you lack IPv6 support and getaddrinfo().
#
# Define NO_SOCKADDR_STORAGE if your platform does not have struct
# sockaddr_storage.
#
# Define NO_ICONV if your libc does not properly support iconv.
#
# Define OLD_ICONV if your library has an old iconv(), where the second
# (input buffer pointer) parameter is declared with type (const char **).
#
# Define NO_DEFLATE_BOUND if your zlib does not have deflateBound.
#
# Define NO_R_TO_GCC_LINKER if your gcc does not like "-R/path/lib"
# that tells runtime paths to dynamic libraries;
# "-Wl,-rpath=/path/lib" is used instead.
#
# Define USE_NSEC below if you want perf to care about sub-second file mtimes
# and ctimes. Note that you need recent glibc (at least 2.2.4) for this, and
# it will BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely
# randomly break unless your underlying filesystem supports those sub-second
# times (my ext3 doesn't).
#
# Define USE_ST_TIMESPEC if your "struct stat" uses "st_ctimespec" instead of
# "st_ctim"
#
# Define NO_NSEC if your "struct stat" does not have "st_ctim.tv_nsec"
# available.  This automatically turns USE_NSEC off.
#
# Define USE_STDEV below if you want perf to care about the underlying device
# change being considered an inode change from the update-index perspective.
#
# Define NO_ST_BLOCKS_IN_STRUCT_STAT if your platform does not have st_blocks
# field that counts the on-disk footprint in 512-byte blocks.
#
# Define ASCIIDOC8 if you want to format documentation with AsciiDoc 8
#
# Define DOCBOOK_XSL_172 if you want to format man pages with DocBook XSL v1.72.
#
# Define NO_PERL_MAKEMAKER if you cannot use Makefiles generated by perl's
# MakeMaker (e.g. using ActiveState under Cygwin).
#
# Define NO_PERL if you do not want Perl scripts or libraries at all.
#
# Define INTERNAL_QSORT to use Git's implementation of qsort(), which
# is a simplified version of the merge sort used in glibc. This is
# recommended if Git triggers O(n^2) behavior in your platform's qsort().
#
# Define NO_EXTERNAL_GREP if you don't want "perf grep" to ever call
# your external grep (e.g., if your system lacks grep, if its grep is
# broken, or spawning external process is slower than built-in grep perf has).

PERF-VERSION-FILE: .FORCE-PERF-VERSION-FILE
	@$(SHELL_PATH) util/PERF-VERSION-GEN
-include PERF-VERSION-FILE

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
uname_O := $(shell sh -c 'uname -o 2>/dev/null || echo not')
uname_R := $(shell sh -c 'uname -r 2>/dev/null || echo not')
uname_P := $(shell sh -c 'uname -p 2>/dev/null || echo not')
uname_V := $(shell sh -c 'uname -v 2>/dev/null || echo not')

#
# Add -m32 for cross-builds:
#
ifdef NO_64BIT
  MBITS := -m32
else
  #
  # If we're on a 64-bit kernel, use -m64:
  #
  ifneq ($(patsubst %64,%,$(uname_M)),$(uname_M))
    MBITS := -m64
  endif
endif

# CFLAGS and LDFLAGS are for the users to override from the command line.

#
# Include saner warnings here, which can catch bugs:
#

EXTRA_WARNINGS := -Wformat
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wformat-security
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wformat-y2k
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wshadow
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Winit-self
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wpacked
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wredundant-decls
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wstack-protector
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wstrict-aliasing=3
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wswitch-default
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wswitch-enum
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wno-system-headers
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wundef
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wvolatile-register-var
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wwrite-strings
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wbad-function-cast
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wmissing-declarations
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wmissing-prototypes
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wnested-externs
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wold-style-definition
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wstrict-prototypes
EXTRA_WARNINGS := $(EXTRA_WARNINGS) -Wdeclaration-after-statement

CFLAGS = $(MBITS) -ggdb3 -Wall -Wextra -std=gnu99 -Werror -O6 -fstack-protector-all -D_FORTIFY_SOURCE=2 $(EXTRA_WARNINGS)
LDFLAGS = -lpthread -lrt -lelf -lm
ALL_CFLAGS = $(CFLAGS)
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

prefix = $(HOME)
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
# DESTDIR=

export prefix bindir sharedir sysconfdir

CC = gcc
AR = ar
RM = rm -f
TAR = tar
FIND = find
INSTALL = install
RPMBUILD = rpmbuild
PTHREAD_LIBS = -lpthread

# sparse is architecture-neutral, which means that we need to tell it
# explicitly what architecture to check for. Fix this up for yours..
SPARSE_FLAGS = -D__BIG_ENDIAN__ -D__powerpc__



### --- END CONFIGURATION SECTION ---

# Those must not be GNU-specific; they are shared with perl/ which may
# be built by a different compiler. (Note that this is an artifact now
# but it still might be nice to keep that distinction.)
BASIC_CFLAGS = -Iutil/include
BASIC_LDFLAGS =

# Guard against environment variables
BUILTIN_OBJS =
BUILT_INS =
COMPAT_CFLAGS =
COMPAT_OBJS =
LIB_H =
LIB_OBJS =
SCRIPT_PERL =
SCRIPT_SH =
TEST_PROGRAMS =

#
# No scripts right now:
#

# SCRIPT_SH += perf-am.sh

#
# No Perl scripts right now:
#

# SCRIPT_PERL += perf-add--interactive.perl

SCRIPTS = $(patsubst %.sh,%,$(SCRIPT_SH)) \
	  $(patsubst %.perl,%,$(SCRIPT_PERL))

# Empty...
EXTRA_PROGRAMS =

# ... and all the rest that could be moved out of bindir to perfexecdir
PROGRAMS += $(EXTRA_PROGRAMS)

#
# Single 'perf' binary right now:
#
PROGRAMS += perf

# List built-in command $C whose implementation cmd_$C() is not in
# builtin-$C.o but is linked in as part of some other command.
#
# None right now:
#
# BUILT_INS += perf-init $X

# what 'all' will build and 'install' will install, in perfexecdir
ALL_PROGRAMS = $(PROGRAMS) $(SCRIPTS)

# what 'all' will build but not install in perfexecdir
OTHER_PROGRAMS = perf$X

# Set paths to tools early so that they can be used for version tests.
ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif

export PERL_PATH

LIB_FILE=libperf.a

LIB_H += ../../include/linux/perf_event.h
LIB_H += ../../include/linux/rbtree.h
LIB_H += ../../include/linux/list.h
LIB_H += util/include/linux/list.h
LIB_H += perf.h
LIB_H += util/types.h
LIB_H += util/levenshtein.h
LIB_H += util/parse-options.h
LIB_H += util/parse-events.h
LIB_H += util/quote.h
LIB_H += util/util.h
LIB_H += util/help.h
LIB_H += util/strbuf.h
LIB_H += util/string.h
LIB_H += util/strlist.h
LIB_H += util/run-command.h
LIB_H += util/sigchain.h
LIB_H += util/symbol.h
LIB_H += util/module.h
LIB_H += util/color.h
LIB_H += util/values.h

LIB_OBJS += util/abspath.o
LIB_OBJS += util/alias.o
LIB_OBJS += util/config.o
LIB_OBJS += util/ctype.o
LIB_OBJS += util/environment.o
LIB_OBJS += util/exec_cmd.o
LIB_OBJS += util/help.o
LIB_OBJS += util/levenshtein.o
LIB_OBJS += util/parse-options.o
LIB_OBJS += util/parse-events.o
LIB_OBJS += util/path.o
LIB_OBJS += util/rbtree.o
LIB_OBJS += util/run-command.o
LIB_OBJS += util/quote.o
LIB_OBJS += util/strbuf.o
LIB_OBJS += util/string.o
LIB_OBJS += util/strlist.o
LIB_OBJS += util/usage.o
LIB_OBJS += util/wrapper.o
LIB_OBJS += util/sigchain.o
LIB_OBJS += util/symbol.o
LIB_OBJS += util/module.o
LIB_OBJS += util/color.o
LIB_OBJS += util/pager.o
LIB_OBJS += util/header.o
LIB_OBJS += util/callchain.o
LIB_OBJS += util/values.o
LIB_OBJS += util/debug.o
LIB_OBJS += util/map.o
LIB_OBJS += util/thread.o
LIB_OBJS += util/trace-event-parse.o
LIB_OBJS += util/trace-event-read.o
LIB_OBJS += util/trace-event-info.o
LIB_OBJS += util/svghelper.o

BUILTIN_OBJS += builtin-annotate.o
BUILTIN_OBJS += builtin-help.o
BUILTIN_OBJS += builtin-sched.o
BUILTIN_OBJS += builtin-list.o
BUILTIN_OBJS += builtin-record.o
BUILTIN_OBJS += builtin-report.o
BUILTIN_OBJS += builtin-stat.o
BUILTIN_OBJS += builtin-timechart.o
BUILTIN_OBJS += builtin-top.o
BUILTIN_OBJS += builtin-trace.o

PERFLIBS = $(LIB_FILE)

#
# Platform specific tweaks
#

# We choose to avoid "if .. else if .. else .. endif endif"
# because maintaining the nesting to match is a pain.  If
# we had "elif" things would have been much nicer...

-include config.mak.autogen
-include config.mak

ifeq ($(uname_S),Darwin)
	ifndef NO_FINK
		ifeq ($(shell test -d /sw/lib && echo y),y)
			BASIC_CFLAGS += -I/sw/include
			BASIC_LDFLAGS += -L/sw/lib
		endif
	endif
	ifndef NO_DARWIN_PORTS
		ifeq ($(shell test -d /opt/local/lib && echo y),y)
			BASIC_CFLAGS += -I/opt/local/include
			BASIC_LDFLAGS += -L/opt/local/lib
		endif
	endif
	PTHREAD_LIBS =
endif

ifeq ($(shell sh -c "(echo '\#include <libelf.h>'; echo 'int main(void) { Elf * elf = elf_begin(0, ELF_C_READ, 0); return (long)elf; }') | $(CC) -x c - $(ALL_CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -o /dev/null $(ALL_LDFLAGS) > /dev/null 2>&1 && echo y"), y)
	ifneq ($(shell sh -c "(echo '\#include <libelf.h>'; echo 'int main(void) { Elf * elf = elf_begin(0, ELF_C_READ_MMAP, 0); return (long)elf; }') | $(CC) -x c - $(ALL_CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -o /dev/null $(ALL_LDFLAGS) > /dev/null 2>&1 && echo y"), y)
		BASIC_CFLAGS += -DLIBELF_NO_MMAP
	endif
else
	msg := $(error No libelf.h/libelf found, please install libelf-dev/elfutils-libelf-devel and glibc-dev[el]);
endif

ifdef NO_DEMANGLE
	BASIC_CFLAGS += -DNO_DEMANGLE
else
	has_bfd := $(shell sh -c "(echo '\#include <bfd.h>'; echo 'int main(void) { bfd_demangle(0, 0, 0); return 0; }') | $(CC) -x c - $(ALL_CFLAGS) -o /dev/null $(ALL_LDFLAGS) -lbfd > /dev/null 2>&1 && echo y")

	ifeq ($(has_bfd),y)
		EXTLIBS += -lbfd
	else
		has_bfd_iberty := $(shell sh -c "(echo '\#include <bfd.h>'; echo 'int main(void) { bfd_demangle(0, 0, 0); return 0; }') | $(CC) -x c - $(ALL_CFLAGS) -o /dev/null $(ALL_LDFLAGS) -lbfd -liberty > /dev/null 2>&1 && echo y")
		ifeq ($(has_bfd_iberty),y)
			EXTLIBS += -lbfd -liberty
		else
			has_bfd_iberty_z := $(shell sh -c "(echo '\#include <bfd.h>'; echo 'int main(void) { bfd_demangle(0, 0, 0); return 0; }') | $(CC) -x c - $(ALL_CFLAGS) -o /dev/null $(ALL_LDFLAGS) -lbfd -liberty -lz > /dev/null 2>&1 && echo y")
			ifeq ($(has_bfd_iberty_z),y)
				EXTLIBS += -lbfd -liberty -lz
			else
				has_cplus_demangle := $(shell sh -c "(echo 'extern char *cplus_demangle(const char *, int);'; echo 'int main(void) { cplus_demangle(0, 0); return 0; }') | $(CC) -x c - $(ALL_CFLAGS) -o /dev/null $(ALL_LDFLAGS) -liberty > /dev/null 2>&1 && echo y")
				ifeq ($(has_cplus_demangle),y)
					EXTLIBS += -liberty
					BASIC_CFLAGS += -DHAVE_CPLUS_DEMANGLE
				else
					msg := $(warning No bfd.h/libbfd found, install binutils-dev[el] to gain symbol demangling)
					BASIC_CFLAGS += -DNO_DEMANGLE
				endif
			endif
		endif
	endif
endif

ifndef CC_LD_DYNPATH
	ifdef NO_R_TO_GCC_LINKER
		# Some gcc does not accept and pass -R to the linker to specify
		# the runtime dynamic library path.
		CC_LD_DYNPATH = -Wl,-rpath,
	else
		CC_LD_DYNPATH = -R
	endif
endif

ifdef NEEDS_SOCKET
	EXTLIBS += -lsocket
endif
ifdef NEEDS_NSL
	EXTLIBS += -lnsl
endif
ifdef NO_D_TYPE_IN_DIRENT
	BASIC_CFLAGS += -DNO_D_TYPE_IN_DIRENT
endif
ifdef NO_D_INO_IN_DIRENT
	BASIC_CFLAGS += -DNO_D_INO_IN_DIRENT
endif
ifdef NO_ST_BLOCKS_IN_STRUCT_STAT
	BASIC_CFLAGS += -DNO_ST_BLOCKS_IN_STRUCT_STAT
endif
ifdef USE_NSEC
	BASIC_CFLAGS += -DUSE_NSEC
endif
ifdef USE_ST_TIMESPEC
	BASIC_CFLAGS += -DUSE_ST_TIMESPEC
endif
ifdef NO_NSEC
	BASIC_CFLAGS += -DNO_NSEC
endif
ifdef NO_C99_FORMAT
	BASIC_CFLAGS += -DNO_C99_FORMAT
endif
ifdef SNPRINTF_RETURNS_BOGUS
	COMPAT_CFLAGS += -DSNPRINTF_RETURNS_BOGUS
	COMPAT_OBJS += compat/snprintf.o
endif
ifdef FREAD_READS_DIRECTORIES
	COMPAT_CFLAGS += -DFREAD_READS_DIRECTORIES
	COMPAT_OBJS += compat/fopen.o
endif
ifdef NO_SYMLINK_HEAD
	BASIC_CFLAGS += -DNO_SYMLINK_HEAD
endif
ifdef NO_STRCASESTR
	COMPAT_CFLAGS += -DNO_STRCASESTR
	COMPAT_OBJS += compat/strcasestr.o
endif
ifdef NO_STRTOUMAX
	COMPAT_CFLAGS += -DNO_STRTOUMAX
	COMPAT_OBJS += compat/strtoumax.o
endif
ifdef NO_STRTOULL
	COMPAT_CFLAGS += -DNO_STRTOULL
endif
ifdef NO_SETENV
	COMPAT_CFLAGS += -DNO_SETENV
	COMPAT_OBJS += compat/setenv.o
endif
ifdef NO_MKDTEMP
	COMPAT_CFLAGS += -DNO_MKDTEMP
	COMPAT_OBJS += compat/mkdtemp.o
endif
ifdef NO_UNSETENV
	COMPAT_CFLAGS += -DNO_UNSETENV
	COMPAT_OBJS += compat/unsetenv.o
endif
ifdef NO_SYS_SELECT_H
	BASIC_CFLAGS += -DNO_SYS_SELECT_H
endif
ifdef NO_MMAP
	COMPAT_CFLAGS += -DNO_MMAP
	COMPAT_OBJS += compat/mmap.o
else
	ifdef USE_WIN32_MMAP
		COMPAT_CFLAGS += -DUSE_WIN32_MMAP
		COMPAT_OBJS += compat/win32mmap.o
	endif
endif
ifdef NO_PREAD
	COMPAT_CFLAGS += -DNO_PREAD
	COMPAT_OBJS += compat/pread.o
endif
ifdef NO_FAST_WORKING_DIRECTORY
	BASIC_CFLAGS += -DNO_FAST_WORKING_DIRECTORY
endif
ifdef NO_TRUSTABLE_FILEMODE
	BASIC_CFLAGS += -DNO_TRUSTABLE_FILEMODE
endif
ifdef NO_IPV6
	BASIC_CFLAGS += -DNO_IPV6
endif
ifdef NO_UINTMAX_T
	BASIC_CFLAGS += -Duintmax_t=uint32_t
endif
ifdef NO_SOCKADDR_STORAGE
ifdef NO_IPV6
	BASIC_CFLAGS += -Dsockaddr_storage=sockaddr_in
else
	BASIC_CFLAGS += -Dsockaddr_storage=sockaddr_in6
endif
endif
ifdef NO_INET_NTOP
	LIB_OBJS += compat/inet_ntop.o
endif
ifdef NO_INET_PTON
	LIB_OBJS += compat/inet_pton.o
endif

ifdef NO_ICONV
	BASIC_CFLAGS += -DNO_ICONV
endif

ifdef OLD_ICONV
	BASIC_CFLAGS += -DOLD_ICONV
endif

ifdef NO_DEFLATE_BOUND
	BASIC_CFLAGS += -DNO_DEFLATE_BOUND
endif

ifdef PPC_SHA1
	SHA1_HEADER = "ppc/sha1.h"
	LIB_OBJS += ppc/sha1.o ppc/sha1ppc.o
else
ifdef ARM_SHA1
	SHA1_HEADER = "arm/sha1.h"
	LIB_OBJS += arm/sha1.o arm/sha1_arm.o
else
ifdef MOZILLA_SHA1
	SHA1_HEADER = "mozilla-sha1/sha1.h"
	LIB_OBJS += mozilla-sha1/sha1.o
else
	SHA1_HEADER = <openssl/sha.h>
	EXTLIBS += $(LIB_4_CRYPTO)
endif
endif
endif
ifdef NO_PERL_MAKEMAKER
	export NO_PERL_MAKEMAKER
endif
ifdef NO_HSTRERROR
	COMPAT_CFLAGS += -DNO_HSTRERROR
	COMPAT_OBJS += compat/hstrerror.o
endif
ifdef NO_MEMMEM
	COMPAT_CFLAGS += -DNO_MEMMEM
	COMPAT_OBJS += compat/memmem.o
endif
ifdef INTERNAL_QSORT
	COMPAT_CFLAGS += -DINTERNAL_QSORT
	COMPAT_OBJS += compat/qsort.o
endif
ifdef RUNTIME_PREFIX
	COMPAT_CFLAGS += -DRUNTIME_PREFIX
endif

ifdef DIR_HAS_BSD_GROUP_SEMANTICS
	COMPAT_CFLAGS += -DDIR_HAS_BSD_GROUP_SEMANTICS
endif
ifdef NO_EXTERNAL_GREP
	BASIC_CFLAGS += -DNO_EXTERNAL_GREP
endif

ifeq ($(PERL_PATH),)
NO_PERL=NoThanks
endif

QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring $(MAKEFLAGS),w),w)
PRINT_DIR = --no-print-directory
else # "make -w"
NO_SUBDIR = :
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	QUIET_CC       = @echo '   ' CC $@;
	QUIET_AR       = @echo '   ' AR $@;
	QUIET_LINK     = @echo '   ' LINK $@;
	QUIET_BUILT_IN = @echo '   ' BUILTIN $@;
	QUIET_GEN      = @echo '   ' GEN $@;
	QUIET_SUBDIR0  = +@subdir=
	QUIET_SUBDIR1  = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			 $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
	export QUIET_GEN
	export QUIET_BUILT_IN
endif
endif

ifdef ASCIIDOC8
	export ASCIIDOC8
endif

# Shell quote (do not use $(call) to accommodate ancient setups);

SHA1_HEADER_SQ = $(subst ','\'',$(SHA1_HEADER))
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
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))

LIBS = $(PERFLIBS) $(EXTLIBS)

BASIC_CFLAGS += -DSHA1_HEADER='$(SHA1_HEADER_SQ)' \
	$(COMPAT_CFLAGS)
LIB_OBJS += $(COMPAT_OBJS)

ALL_CFLAGS += $(BASIC_CFLAGS)
ALL_LDFLAGS += $(BASIC_LDFLAGS)

export TAR INSTALL DESTDIR SHELL_PATH


### Build rules

SHELL = $(SHELL_PATH)

all:: shell_compatibility_test $(ALL_PROGRAMS) $(BUILT_INS) $(OTHER_PROGRAMS) PERF-BUILD-OPTIONS
ifneq (,$X)
	$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_PROGRAMS) $(BUILT_INS) perf$X)), test '$p' -ef '$p$X' || $(RM) '$p';)
endif

all::

please_set_SHELL_PATH_to_a_more_modern_shell:
	@$$(:)

shell_compatibility_test: please_set_SHELL_PATH_to_a_more_modern_shell

strip: $(PROGRAMS) perf$X
	$(STRIP) $(STRIP_OPTS) $(PROGRAMS) perf$X

perf.o: perf.c common-cmds.h PERF-CFLAGS
	$(QUIET_CC)$(CC) -DPERF_VERSION='"$(PERF_VERSION)"' \
		'-DPERF_HTML_PATH="$(htmldir_SQ)"' \
		$(ALL_CFLAGS) -c $(filter %.c,$^)

perf$X: perf.o $(BUILTIN_OBJS) $(PERFLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ perf.o \
		$(BUILTIN_OBJS) $(ALL_LDFLAGS) $(LIBS)

builtin-help.o: builtin-help.c common-cmds.h PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) \
		'-DPERF_HTML_PATH="$(htmldir_SQ)"' \
		'-DPERF_MAN_PATH="$(mandir_SQ)"' \
		'-DPERF_INFO_PATH="$(infodir_SQ)"' $<

builtin-timechart.o: builtin-timechart.c common-cmds.h PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) \
		'-DPERF_HTML_PATH="$(htmldir_SQ)"' \
		'-DPERF_MAN_PATH="$(mandir_SQ)"' \
		'-DPERF_INFO_PATH="$(infodir_SQ)"' $<

$(BUILT_INS): perf$X
	$(QUIET_BUILT_IN)$(RM) $@ && \
	ln perf$X $@ 2>/dev/null || \
	ln -s perf$X $@ 2>/dev/null || \
	cp perf$X $@

common-cmds.h: util/generate-cmdlist.sh command-list.txt

common-cmds.h: $(wildcard Documentation/perf-*.txt)
	$(QUIET_GEN). util/generate-cmdlist.sh > $@+ && mv $@+ $@

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's|@SHELL_PATH@|$(SHELL_PATH_SQ)|' \
	    -e 's|@@PERL@@|$(PERL_PATH_SQ)|g' \
	    -e 's/@@PERF_VERSION@@/$(PERF_VERSION)/g' \
	    -e 's/@@NO_CURL@@/$(NO_CURL)/g' \
	    $@.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

configure: configure.ac
	$(QUIET_GEN)$(RM) $@ $<+ && \
	sed -e 's/@@PERF_VERSION@@/$(PERF_VERSION)/g' \
	    $< > $<+ && \
	autoconf -o $@ $<+ && \
	$(RM) $<+

# These can record PERF_VERSION
perf.o perf.spec \
	$(patsubst %.sh,%,$(SCRIPT_SH)) \
	$(patsubst %.perl,%,$(SCRIPT_PERL)) \
	: PERF-VERSION-FILE

%.o: %.c PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) $<
%.s: %.c PERF-CFLAGS
	$(QUIET_CC)$(CC) -S $(ALL_CFLAGS) $<
%.o: %.S
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) $<

util/exec_cmd.o: util/exec_cmd.c PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) \
		'-DPERF_EXEC_PATH="$(perfexecdir_SQ)"' \
		'-DBINDIR="$(bindir_relative_SQ)"' \
		'-DPREFIX="$(prefix_SQ)"' \
		$<

builtin-init-db.o: builtin-init-db.c PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) -DDEFAULT_PERF_TEMPLATE_DIR='"$(template_dir_SQ)"' $<

util/config.o: util/config.c PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $*.o -c $(ALL_CFLAGS) -DETC_PERFCONFIG='"$(ETC_PERFCONFIG_SQ)"' $<

util/rbtree.o: ../../lib/rbtree.c PERF-CFLAGS
	$(QUIET_CC)$(CC) -o util/rbtree.o -c $(ALL_CFLAGS) -DETC_PERFCONFIG='"$(ETC_PERFCONFIG_SQ)"' $<

perf-%$X: %.o $(PERFLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

$(LIB_OBJS) $(BUILTIN_OBJS): $(LIB_H)
$(patsubst perf-%$X,%.o,$(PROGRAMS)): $(LIB_H) $(wildcard */*.h)
builtin-revert.o wt-status.o: wt-status.h

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(LIB_OBJS)

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

PERF-CFLAGS: .FORCE-PERF-CFLAGS
	@FLAGS='$(TRACK_CFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat PERF-CFLAGS 2>/dev/null`" ; then \
		echo 1>&2 "    * new build flags or prefix"; \
		echo "$$FLAGS" >PERF-CFLAGS; \
            fi

# We need to apply sq twice, once to protect from the shell
# that runs PERF-BUILD-OPTIONS, and then again to protect it
# and the first level quoting from the shell that runs "echo".
PERF-BUILD-OPTIONS: .FORCE-PERF-BUILD-OPTIONS
	@echo SHELL_PATH=\''$(subst ','\'',$(SHELL_PATH_SQ))'\' >$@
	@echo TAR=\''$(subst ','\'',$(subst ','\'',$(TAR)))'\' >>$@
	@echo NO_CURL=\''$(subst ','\'',$(subst ','\'',$(NO_CURL)))'\' >>$@
	@echo NO_PERL=\''$(subst ','\'',$(subst ','\'',$(NO_PERL)))'\' >>$@

### Testing rules

#
# None right now:
#
# TEST_PROGRAMS += test-something$X

all:: $(TEST_PROGRAMS)

# GNU make supports exporting all variables by "export" without parameters.
# However, the environment gets quite big, and some programs have problems
# with that.

export NO_SVN_TESTS

check: common-cmds.h
	if sparse; \
	then \
		for i in *.c */*.c; \
		do \
			sparse $(ALL_CFLAGS) $(SPARSE_FLAGS) $$i || exit; \
		done; \
	else \
		echo 2>&1 "Did you mean 'make test'?"; \
		exit 1; \
	fi

remove-dashes:
	./fixup-builtins $(BUILT_INS) $(PROGRAMS) $(SCRIPTS)

### Installation rules

ifneq ($(filter /%,$(firstword $(template_dir))),)
template_instdir = $(template_dir)
else
template_instdir = $(prefix)/$(template_dir)
endif
export template_instdir

ifneq ($(filter /%,$(firstword $(perfexecdir))),)
perfexec_instdir = $(perfexecdir)
else
perfexec_instdir = $(prefix)/$(perfexecdir)
endif
perfexec_instdir_SQ = $(subst ','\'',$(perfexec_instdir))
export perfexec_instdir

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) perf$X '$(DESTDIR_SQ)$(bindir_SQ)'
ifdef BUILT_INS
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)'
	$(INSTALL) $(BUILT_INS) '$(DESTDIR_SQ)$(perfexec_instdir_SQ)'
ifneq (,$X)
	$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_PROGRAMS) $(BUILT_INS) perf$X)), $(RM) '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/$p';)
endif
endif

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


### Maintainer's dist rules
#
# None right now
#
#
# perf.spec: perf.spec.in
#	sed -e 's/@@VERSION@@/$(PERF_VERSION)/g' < $< > $@+
#	mv $@+ $@
#
# PERF_TARNAME=perf-$(PERF_VERSION)
# dist: perf.spec perf-archive$(X) configure
#	./perf-archive --format=tar \
#		--prefix=$(PERF_TARNAME)/ HEAD^{tree} > $(PERF_TARNAME).tar
#	@mkdir -p $(PERF_TARNAME)
#	@cp perf.spec configure $(PERF_TARNAME)
#	@echo $(PERF_VERSION) > $(PERF_TARNAME)/version
#	$(TAR) rf $(PERF_TARNAME).tar \
#		$(PERF_TARNAME)/perf.spec \
#		$(PERF_TARNAME)/configure \
#		$(PERF_TARNAME)/version
#	@$(RM) -r $(PERF_TARNAME)
#	gzip -f -9 $(PERF_TARNAME).tar
#
# htmldocs = perf-htmldocs-$(PERF_VERSION)
# manpages = perf-manpages-$(PERF_VERSION)
# dist-doc:
#	$(RM) -r .doc-tmp-dir
#	mkdir .doc-tmp-dir
#	$(MAKE) -C Documentation WEBDOC_DEST=../.doc-tmp-dir install-webdoc
#	cd .doc-tmp-dir && $(TAR) cf ../$(htmldocs).tar .
#	gzip -n -9 -f $(htmldocs).tar
#	:
#	$(RM) -r .doc-tmp-dir
#	mkdir -p .doc-tmp-dir/man1 .doc-tmp-dir/man5 .doc-tmp-dir/man7
#	$(MAKE) -C Documentation DESTDIR=./ \
#		man1dir=../.doc-tmp-dir/man1 \
#		man5dir=../.doc-tmp-dir/man5 \
#		man7dir=../.doc-tmp-dir/man7 \
#		install
#	cd .doc-tmp-dir && $(TAR) cf ../$(manpages).tar .
#	gzip -n -9 -f $(manpages).tar
#	$(RM) -r .doc-tmp-dir
#
# rpm: dist
#	$(RPMBUILD) -ta $(PERF_TARNAME).tar.gz

### Cleaning rules

distclean: clean
#	$(RM) configure

clean:
	$(RM) *.o */*.o $(LIB_FILE)
	$(RM) $(ALL_PROGRAMS) $(BUILT_INS) perf$X
	$(RM) $(TEST_PROGRAMS)
	$(RM) *.spec *.pyc *.pyo */*.pyc */*.pyo common-cmds.h TAGS tags cscope*
	$(RM) -r autom4te.cache
	$(RM) config.log config.mak.autogen config.mak.append config.status config.cache
	$(RM) -r $(PERF_TARNAME) .doc-tmp-dir
	$(RM) $(PERF_TARNAME).tar.gz perf-core_$(PERF_VERSION)-*.tar.gz
	$(RM) $(htmldocs).tar.gz $(manpages).tar.gz
	$(MAKE) -C Documentation/ clean
	$(RM) PERF-VERSION-FILE PERF-CFLAGS PERF-BUILD-OPTIONS

.PHONY: all install clean strip
.PHONY: shell_compatibility_test please_set_SHELL_PATH_to_a_more_modern_shell
.PHONY: .FORCE-PERF-VERSION-FILE TAGS tags cscope .FORCE-PERF-CFLAGS
.PHONY: .FORCE-PERF-BUILD-OPTIONS

### Make sure built-ins do not have dups and listed in perf.c
#
check-builtins::
	./check-builtins.sh

### Test suite coverage testing
#
# None right now
#
# .PHONY: coverage coverage-clean coverage-build coverage-report
#
# coverage:
#	$(MAKE) coverage-build
#	$(MAKE) coverage-report
#
# coverage-clean:
#	rm -f *.gcda *.gcno
#
# COVERAGE_CFLAGS = $(CFLAGS) -O0 -ftest-coverage -fprofile-arcs
# COVERAGE_LDFLAGS = $(CFLAGS)  -O0 -lgcov
#
# coverage-build: coverage-clean
#	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" all
#	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" \
#		-j1 test
#
# coverage-report:
#	gcov -b *.c */*.c
#	grep '^function.*called 0 ' *.c.gcov */*.c.gcov \
#		| sed -e 's/\([^:]*\)\.gcov: *function \([^ ]*\) called.*/\1: \2/' \
#		| tee coverage-untested-functions
