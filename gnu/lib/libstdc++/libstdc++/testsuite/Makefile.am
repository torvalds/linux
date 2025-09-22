## Makefile for the testsuite subdirectory of the GNU C++ Standard library.
##
## Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
##
## This file is part of the libstdc++ version 3 distribution.
## Process this file with automake to produce Makefile.in.

## This file is part of the GNU ISO C++ Library.  This library is free
## software; you can redistribute it and/or modify it under the
## terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2, or (at your option)
## any later version.

## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.

## You should have received a copy of the GNU General Public License along
## with this library; see the file COPYING.  If not, write to the Free
## Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
## USA.

AUTOMAKE_OPTIONS =  cygnus dejagnu

mkinstalldirs = $(SHELL) $(toplevel_srcdir)/mkinstalldirs

DEJATOOL = libstdc++-v3

EXPECT = `if [ -f @glibcpp_builddir@/../../expect/expect ] ; then \
            echo @glibcpp_builddir@/../../expect/expect ; \
          else echo expect ; fi`

RUNTEST = `if [ -f @glibcpp_srcdir@/../dejagnu/runtest ] ; then \
	       echo @glibcpp_srcdir@/../dejagnu/runtest ; \
	    else echo runtest; fi`

AM_RUNTESTFLAGS =
RUNTESTFLAGS =

## CXX is actually a "C" compiler. These are real C++ programs.
## Do the same thing as `testsuite_flags --build-cxx`
testsuite_flags_script=${glibcpp_builddir}/testsuite_flags
CXX=`${testsuite_flags_script} --build-cxx`

GLIBGCC_DIR=`$(CC) -print-libgcc-file-name | sed 's,/[^/]*$$,,'`
GLIBCXX_DIR=${glibcpp_builddir}/src/.libs

CXXLINK = \
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) \
	-R $(GLIBGCC_DIR) -R $(GLIBCXX_DIR) \
	$(AM_CXXFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@

INCLUDES = \
	-nostdinc++ \
	@GLIBCPP_INCLUDES@ @LIBSUPCXX_INCLUDES@ @TOPLEVEL_INCLUDES@ 

## Build support library.
noinst_LIBRARIES = libv3test.a
libv3test_a_SOURCES = testsuite_hooks.cc testsuite_allocator.cc

## Build support utilities.
## Only build this as native, as need to find startup files and libc to link.
if GLIBCPP_TEST_ABI
noinst_PROGRAMS = abi_check
else
noinst_PROGRAMS = 
endif
abi_check_SOURCES = abi_check.cc

# Enable wchar_t tests if capable.
if GLIBCPP_TEST_WCHAR_T
all-local: stamp_wchar
else
all-local:
endif

# Override this so local rules are possible.
check-am: 
	$(MAKE) $(AM_MAKEFLAGS) check-DEJAGNU; \
	$(MAKE) $(AM_MAKEFLAGS) check-local 

check-local: check-abi

stamp_wchar:
	touch testsuite_wchar_t

baseline_dir = @baseline_dir@
baseline_file = ${baseline_dir}/baseline_symbols.txt
extract_symvers = @glibcpp_srcdir@/config/abi/extract_symvers

current_symbols.txt: ${extract_symvers} ../src/.libs/libstdc++.so
	  -@(${extract_symvers} ../src/.libs/libstdc++.so current_symbols.txt)

baseline_symbols:
	-@(output=${baseline_file}; \
	  if test ! -f $${output}; then \
	    echo "Baseline file doesn't exist."; \
	    echo "Try 'make new-abi-baseline' to create it."; \
	    exit 1; \
	  fi; \
	touch baseline_symbols)

new-abi-baseline: 
	-@$(mkinstalldirs) ${baseline_dir}
	-@(output=${baseline_file}; \
	  if test -f $${output}; then \
	    output=$${output}.new; \
	    t=`echo $${output} | sed 's=.*config/abi/=='`; \
	    echo "Baseline file already exists, writing to $${t} instead."; \
	  fi; \
	  ${extract_symvers} ../src/.libs/libstdc++.so $${output})

if GLIBCPP_TEST_ABI
# Use 'new-abi-baseline' to create an initial symbol file.  Then run
# 'check-abi' to test for changes against that file.
check-abi: abi_check baseline_symbols current_symbols.txt
	-@(./abi_check --check ./current_symbols.txt ${baseline_file} \
	2>&1 | tee libstdc++-v3-abi.sum)

check-abi-verbose: abi_check baseline_symbols current_symbols.txt 
	-@(./abi_check --check-verbose ./current_symbols.txt ${baseline_file} \
	2>&1 | tee libstdc++-v3-abi.sum)
else
check-abi:
check-abi-verbose:
endif

# By adding these files here, automake will remove them for 'make clean'
CLEANFILES = *.txt *.tst *.exe core* filebuf_* tmp* ostream_* *.log *.sum \
	     testsuite_files testsuite_wchar_t site.exp abi_check 
