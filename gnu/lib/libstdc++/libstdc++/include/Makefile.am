## Makefile for the include subdirectory of the GNU C++ Standard library.
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

AUTOMAKE_OPTIONS = 1.3 cygnus
MAINT_CHARSET = latin1

mkinstalldirs = $(SHELL) $(toplevel_srcdir)/mkinstalldirs

# Cross compiler and multilib support.
CXX = @glibcpp_CXX@
glibcpp_srcdir=@glibcpp_srcdir@
glibcpp_builddir=@glibcpp_builddir@

GLIBCPP_INCLUDES = @GLIBCPP_INCLUDES@
LIBSUPCXX_INCLUDES = @LIBSUPCXX_INCLUDES@
INCLUDES = -nostdinc++ $(GLIBCPP_INCLUDES) $(LIBSUPCXX_INCLUDES)

# Standard C++ includes.
std_srcdir = ${glibcpp_srcdir}/include/std
std_builddir = .
std_headers = \
	${std_srcdir}/std_algorithm.h \
	${std_srcdir}/std_bitset.h \
	${std_srcdir}/std_complex.h \
	${std_srcdir}/std_deque.h \
	${std_srcdir}/std_fstream.h \
	${std_srcdir}/std_functional.h \
	${std_srcdir}/std_iomanip.h \
	${std_srcdir}/std_ios.h \
	${std_srcdir}/std_iosfwd.h \
	${std_srcdir}/std_iostream.h \
	${std_srcdir}/std_istream.h \
	${std_srcdir}/std_iterator.h \
	${std_srcdir}/std_limits.h \
	${std_srcdir}/std_list.h \
	${std_srcdir}/std_locale.h \
	${std_srcdir}/std_map.h \
	${std_srcdir}/std_memory.h \
	${std_srcdir}/std_numeric.h \
	${std_srcdir}/std_ostream.h \
	${std_srcdir}/std_queue.h \
	${std_srcdir}/std_set.h \
	${std_srcdir}/std_sstream.h \
	${std_srcdir}/std_stack.h \
	${std_srcdir}/std_stdexcept.h \
	${std_srcdir}/std_streambuf.h \
	${std_srcdir}/std_string.h \
	${std_srcdir}/std_utility.h \
	${std_srcdir}/std_valarray.h \
	${std_srcdir}/std_vector.h
# Renamed at build time. 
std_headers_rename = \
	algorithm \
	bitset \
	complex \
	deque \
	fstream \
	functional \
	iomanip \
	ios \
	iosfwd \
	iostream \
	istream \
	iterator \
	limits \
	list \
	locale \
	map \
	memory \
	numeric \
	ostream \
	queue \
	set \
	sstream \
	stack \
	stdexcept \
	streambuf \
	string \
	utility \
	valarray \
	vector

bits_srcdir = ${glibcpp_srcdir}/include/bits
bits_builddir = ./bits
bits_headers = \
	${bits_srcdir}/basic_ios.h \
	${bits_srcdir}/basic_ios.tcc \
	${bits_srcdir}/basic_string.h \
	${bits_srcdir}/basic_string.tcc \
	${bits_srcdir}/boost_concept_check.h \
	${bits_srcdir}/char_traits.h \
	${bits_srcdir}/codecvt.h \
	${bits_srcdir}/concept_check.h \
	${bits_srcdir}/cpp_type_traits.h \
	${bits_srcdir}/deque.tcc \
	${bits_srcdir}/fpos.h \
	${bits_srcdir}/fstream.tcc \
	${bits_srcdir}/functexcept.h \
	${bits_srcdir}/gslice.h \
	${bits_srcdir}/gslice_array.h \
	${bits_srcdir}/indirect_array.h \
	${bits_srcdir}/ios_base.h \
	${bits_srcdir}/istream.tcc \
	${bits_srcdir}/list.tcc \
	${bits_srcdir}/locale_classes.h \
	${bits_srcdir}/locale_facets.h \
	${bits_srcdir}/locale_facets.tcc \
	${bits_srcdir}/localefwd.h \
	${bits_srcdir}/mask_array.h \
	${bits_srcdir}/ostream.tcc \
	${bits_srcdir}/pthread_allocimpl.h \
	${bits_srcdir}/stream_iterator.h \
	${bits_srcdir}/streambuf_iterator.h \
	${bits_srcdir}/slice_array.h \
	${bits_srcdir}/sstream.tcc \
	${bits_srcdir}/stl_algo.h \
	${bits_srcdir}/stl_algobase.h \
	${bits_srcdir}/stl_alloc.h \
	${bits_srcdir}/stl_bvector.h \
	${bits_srcdir}/stl_construct.h \
	${bits_srcdir}/stl_deque.h \
	${bits_srcdir}/stl_function.h \
	${bits_srcdir}/stl_heap.h \
	${bits_srcdir}/stl_iterator.h \
	${bits_srcdir}/stl_iterator_base_funcs.h \
	${bits_srcdir}/stl_iterator_base_types.h \
	${bits_srcdir}/stl_list.h \
	${bits_srcdir}/stl_map.h \
	${bits_srcdir}/stl_multimap.h \
	${bits_srcdir}/stl_multiset.h \
	${bits_srcdir}/stl_numeric.h \
	${bits_srcdir}/stl_pair.h \
	${bits_srcdir}/stl_pthread_alloc.h \
	${bits_srcdir}/stl_queue.h \
	${bits_srcdir}/stl_raw_storage_iter.h \
	${bits_srcdir}/stl_relops.h \
	${bits_srcdir}/stl_set.h \
	${bits_srcdir}/stl_stack.h \
	${bits_srcdir}/stl_tempbuf.h \
	${bits_srcdir}/stl_threads.h \
	${bits_srcdir}/stl_tree.h \
	${bits_srcdir}/stl_uninitialized.h \
	${bits_srcdir}/stl_vector.h \
	${bits_srcdir}/streambuf.tcc \
	${bits_srcdir}/stringfwd.h \
	${bits_srcdir}/type_traits.h \
	${bits_srcdir}/valarray_array.h \
	${bits_srcdir}/valarray_array.tcc \
	${bits_srcdir}/valarray_meta.h \
	${bits_srcdir}/vector.tcc

backward_srcdir = ${glibcpp_srcdir}/include/backward
backward_builddir = ./backward
backward_headers = \
	${backward_srcdir}/complex.h \
	${backward_srcdir}/iomanip.h \
	${backward_srcdir}/istream.h \
	${backward_srcdir}/ostream.h \
	${backward_srcdir}/stream.h \
	${backward_srcdir}/streambuf.h \
	${backward_srcdir}/algo.h \
	${backward_srcdir}/algobase.h \
	${backward_srcdir}/alloc.h \
	${backward_srcdir}/bvector.h \
	${backward_srcdir}/defalloc.h \
	${backward_srcdir}/deque.h \
	${backward_srcdir}/function.h \
	${backward_srcdir}/hash_map.h \
	${backward_srcdir}/hash_set.h \
	${backward_srcdir}/hashtable.h \
	${backward_srcdir}/heap.h \
	${backward_srcdir}/iostream.h \
	${backward_srcdir}/iterator.h \
	${backward_srcdir}/list.h \
	${backward_srcdir}/map.h \
	${backward_srcdir}/multimap.h \
	${backward_srcdir}/new.h \
	${backward_srcdir}/multiset.h \
	${backward_srcdir}/pair.h \
	${backward_srcdir}/queue.h \
	${backward_srcdir}/rope.h \
	${backward_srcdir}/set.h \
	${backward_srcdir}/slist.h \
	${backward_srcdir}/stack.h \
	${backward_srcdir}/tempbuf.h \
	${backward_srcdir}/tree.h \
	${backward_srcdir}/vector.h \
	${backward_srcdir}/fstream.h \
	${backward_srcdir}/strstream \
	${backward_srcdir}/backward_warning.h

ext_srcdir = ${glibcpp_srcdir}/include/ext
ext_builddir = ./ext
ext_headers = \
	${ext_srcdir}/algorithm \
	${ext_srcdir}/enc_filebuf.h \
	${ext_srcdir}/stdio_filebuf.h \
	${ext_srcdir}/functional \
	${ext_srcdir}/hash_map \
	${ext_srcdir}/hash_set \
	${ext_srcdir}/iterator \
	${ext_srcdir}/memory \
	${ext_srcdir}/numeric \
	${ext_srcdir}/rb_tree \
	${ext_srcdir}/rope \
	${ext_srcdir}/ropeimpl.h \
	${ext_srcdir}/slist \
	${ext_srcdir}/stl_hash_fun.h \
	${ext_srcdir}/stl_hashtable.h \
	${ext_srcdir}/stl_rope.h

# This is the common subset of files that all three "C" header models use.
c_base_srcdir = @C_INCLUDE_DIR@
c_base_builddir = .
c_base_headers = \
	${c_base_srcdir}/std_cassert.h \
	${c_base_srcdir}/std_cctype.h \
	${c_base_srcdir}/std_cerrno.h \
	${c_base_srcdir}/std_cfloat.h \
	${c_base_srcdir}/std_ciso646.h \
	${c_base_srcdir}/std_climits.h \
	${c_base_srcdir}/std_clocale.h \
	${c_base_srcdir}/std_cmath.h \
	${c_base_srcdir}/std_csetjmp.h \
	${c_base_srcdir}/std_csignal.h \
	${c_base_srcdir}/std_cstdarg.h \
	${c_base_srcdir}/std_cstddef.h \
	${c_base_srcdir}/std_cstdio.h \
	${c_base_srcdir}/std_cstdlib.h \
	${c_base_srcdir}/std_cstring.h \
	${c_base_srcdir}/std_ctime.h \
	${c_base_srcdir}/std_cwchar.h \
	${c_base_srcdir}/std_cwctype.h 
c_base_headers_rename = \
	cassert \
	cctype \
	cerrno \
	cfloat \
	ciso646 \
	climits \
	clocale \
	cmath \
	csetjmp \
	csignal \
	cstdarg \
	cstddef \
	cstdio \
	cstdlib \
	cstring \
	ctime \
	cwchar \
	cwctype 

# "C" compatibility headers.
c_compatibility_srcdir = ${glibcpp_srcdir}/include/c_compatibility
c_compatibility_builddir = .
c_compatibility_headers = \
	${c_compatibility_srcdir}/assert.h \
	${c_compatibility_srcdir}/ctype.h \
	${c_compatibility_srcdir}/errno.h \
	${c_compatibility_srcdir}/float.h \
	${c_compatibility_srcdir}/iso646.h \
	${c_compatibility_srcdir}/limits.h \
	${c_compatibility_srcdir}/locale.h \
	${c_compatibility_srcdir}/math.h \
	${c_compatibility_srcdir}/setjmp.h \
	${c_compatibility_srcdir}/signal.h \
	${c_compatibility_srcdir}/stdarg.h \
	${c_compatibility_srcdir}/stddef.h \
	${c_compatibility_srcdir}/stdio.h \
	${c_compatibility_srcdir}/stdlib.h \
	${c_compatibility_srcdir}/string.h \
	${c_compatibility_srcdir}/time.h \
	${c_compatibility_srcdir}/wchar.h \
	${c_compatibility_srcdir}/wctype.h 

# Some of the different "C" header models need extra files.
# Some "C" header schemes require the "C" compatibility headers.
# For --enable-cheaders=c_std
if GLIBCPP_C_HEADERS_C_STD
c_base_headers_extra = ${c_base_srcdir}/cmath.tcc 
else
c_base_headers_extra = 
endif

if GLIBCPP_C_HEADERS_COMPATIBILITY
c_compatibility_headers_extra = ${c_compatibility_headers}
else
c_compatibility_headers_extra = 
endif

target_srcdir = ${glibcpp_srcdir}/@OS_INC_SRCDIR@
target_builddir = ./${target_alias}/bits
target_headers = \
	${target_srcdir}/ctype_base.h \
	${target_srcdir}/ctype_inline.h \
	${target_srcdir}/ctype_noninline.h \
	${target_srcdir}/os_defines.h \
	${glibcpp_srcdir}/@ATOMICITY_INC_SRCDIR@/atomicity.h 

# Non-installed target_header files.
target_headers_noinst = \
	${glibcpp_srcdir}/@CLOCALE_INTERNAL_H@

# These target_headers_extra files are all built with ad hoc naming rules.
target_headers_extra = \
	${target_builddir}/basic_file.h \
	${target_builddir}/c++config.h \
	${target_builddir}/c++io.h \
	${target_builddir}/c++locale.h \
	${target_builddir}/messages_members.h \
	${target_builddir}/time_members.h \
	${target_builddir}/codecvt_specializations.h

thread_target_headers = \
	${target_builddir}/gthr.h \
	${target_builddir}/gthr-single.h \
	${target_builddir}/gthr-posix.h \
	${target_builddir}/gthr-default.h

# List of all timestamp files.  By keeping only one copy of this list, both
# CLEANFILES and all-local are kept up-to-date.
allstamps = \
	stamp-std stamp-bits stamp-c_base stamp-c_compatibility \
	stamp-backward stamp-ext stamp-target

# Here are the rules for building the headers
all-local: ${target_builddir}/c++config.h ${thread_target_headers} ${allstamps}

# This rule is slightly different, in that we must change the name of the
# local file from std_foo.h to foo.
stamp-std: ${std_headers}
	@if [ ! -d "${std_builddir}" ]; then \
	  mkdir -p ${std_builddir} ;\
	fi ;\
	(cd ${std_builddir} && for h in $?; do \
	  official_name=`echo $$h | sed -e 's,.*/std_,,' -e 's,\.h$$,,'` ;\
	  @LN_S@ $$h ./$${official_name} || true ;\
	done) ;\
	echo `date` > stamp-std 

stamp-std-precompile: stamp-std
	for h in ${std_headers_rename}; do \
	  $(CXX) -Winvalid-pch -x c++-header $(INCLUDES) $${h}; \
	done; \
	echo `date` > stamp-std-precompile

stamp-bits: ${bits_headers}
	@if [ ! -d "${bits_builddir}" ]; then \
	  mkdir -p ${bits_builddir} ;\
	fi ;\
	(cd ${bits_builddir} && @LN_S@ $? . || true) ;\
	echo `date` > stamp-bits

stamp-c_base: stamp-bits ${c_base_headers} ${c_base_headers_extra} 
	@if [ ! -d "${c_base_builddir}" ]; then \
	  mkdir -p ${c_base_builddir} ;\
	fi ;\
	(cd ${c_base_builddir} && for h in ${c_base_headers}; do \
	  official_name=`echo $$h | sed -e 's,.*/std_,,' -e 's,\.h$$,,'` ;\
	  @LN_S@ $$h ./$${official_name} || true ;\
	done) ;\
	if [ ! -z "${c_base_headers_extra}" ]; then \
	  (cd ${bits_builddir} && @LN_S@ ${c_base_headers_extra}  . || true) ;\
	fi ;\
	echo `date` > stamp-c_base

stamp-c_compatibility: ${c_compatibility_headers_extra}
	@if [ ! -d "${c_compatibility_builddir}" ]; then \
	  mkdir -p ${c_compatibility_builddir} ;\
	fi ;\
	if [ ! -z "${c_compatibility_headers_extra}" ]; then \
	  (cd ${c_compatibility_builddir} && @LN_S@ $? . || true) ;\
	fi ;\
	echo `date` > stamp-c_compatibility

stamp-backward: ${backward_headers}
	@if [ ! -d "${backward_builddir}" ]; then \
	  mkdir -p ${backward_builddir} ;\
	fi ;\
	(cd ${backward_builddir} && @LN_S@ $? . || true) ;\
	echo `date` > stamp-backward

stamp-ext: ${ext_headers}
	@if [ ! -d "${ext_builddir}" ]; then \
	  mkdir -p ${ext_builddir} ;\
	fi ;\
	(cd ${ext_builddir} && @LN_S@ $? . || true) ;\
	echo `date` > stamp-ext

stamp-${target_alias}:
	@if [ ! -d ${target_builddir} ]; then \
	  mkdir -p ${target_builddir} ;\
	  echo `date` > stamp-${target_alias} ;\
	fi

# Target includes static.
# XXX Missing dependency info for {target_headers_extra}
stamp-target: ${target_headers} ${target_headers_noinst} stamp-${target_alias}
	@if [ ! -f stamp-target ]; then \
	  (cd ${target_builddir} ;\
	  @LN_S@ ${target_headers} . || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@BASIC_FILE_H@ basic_file.h || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@CSTDIO_H@ c++io.h || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@CLOCALE_H@ c++locale.h || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@CLOCALE_INTERNAL_H@ . || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@CMESSAGES_H@ messages_members.h || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@CTIME_H@ time_members.h || true ;\
	  @LN_S@ ${glibcpp_srcdir}/@CCODECVT_H@ codecvt_specializations.h || true);\
	  echo `date` > stamp-target ; \
	fi

# Target includes dynamic.
${target_builddir}/c++config.h: ${CONFIG_HEADER} \
				${glibcpp_srcdir}/include/bits/c++config \
				stamp-${target_alias}
	@cat ${glibcpp_srcdir}/include/bits/c++config > $@ ;\
	sed -e 's/HAVE_/_GLIBCPP_HAVE_/g' \
	    -e 's/PACKAGE/_GLIBCPP_PACKAGE/g' \
	    -e 's/VERSION/_GLIBCPP_VERSION/g' \
	    -e 's/WORDS_/_GLIBCPP_WORDS_/g' \
	    < ${CONFIG_HEADER} >> $@ ;\
	echo "#endif // _CPP_CPPCONFIG_" >>$@

# Target includes for threads
glibcpp_thread_h = @glibcpp_thread_h@
uppercase = [ABCDEFGHIJKLMNOPQRSTUVWXYZ_]

${target_builddir}/gthr.h: ${toplevel_srcdir}/gcc/gthr.h stamp-${target_alias}
	sed -e '/^#/s/\(${uppercase}${uppercase}*\)/_GLIBCPP_\1/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${toplevel_srcdir}/gcc/gthr.h > $@

${target_builddir}/gthr-single.h: ${toplevel_srcdir}/gcc/gthr-single.h \
				  stamp-${target_alias}
	sed -e 's/\(UNUSED\)/_GLIBCPP_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCPP_\1/g' \
	    < ${toplevel_srcdir}/gcc/gthr-single.h > $@

${target_builddir}/gthr-posix.h: ${toplevel_srcdir}/gcc/gthr-posix.h \
				   stamp-${target_alias}
	sed -e 's/\(UNUSED\)/_GLIBCPP_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCPP_\1/g' \
	    -e 's/\(${uppercase}*WEAK\)/_GLIBCPP_\1/g' \
	    < ${toplevel_srcdir}/gcc/gthr-posix.h > $@

${target_builddir}/gthr-default.h: ${toplevel_srcdir}/gcc/${glibcpp_thread_h} \
				   stamp-${target_alias}
	sed -e 's/\(UNUSED\)/_GLIBCPP_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCPP_\1/g' \
	    -e 's/\(${uppercase}*WEAK\)/_GLIBCPP_\1/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${toplevel_srcdir}/gcc/${glibcpp_thread_h} > $@

# For robustness sake (in light of junk files or in-source
# configuration), copy from the build or source tree to the install
# tree using only the human-maintained file lists and directory
# components.  Yes, with minor differences, this is sheer duplication
# of the staging rules above using $(INSTALL_DATA) instead of LN_S and
# `$(mkinstalldirs)' instead of `mkdir -p'.  In particular,
# target_headers_extra are taken out of the build tree staging area;
# the rest are taken from the original source tree.
gxx_include_dir = @gxx_include_dir@
install-data-local:
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}/${bits_builddir}
	for file in ${bits_headers}; do \
	  $(INSTALL_DATA) $${file} $(DESTDIR)${gxx_include_dir}/${bits_builddir}; done
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}/${backward_builddir}
	for file in ${backward_headers}; do \
	  $(INSTALL_DATA) $${file} $(DESTDIR)${gxx_include_dir}/${backward_builddir}; done
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}/${ext_builddir}
	for file in ${ext_headers}; do \
	  $(INSTALL_DATA) $${file} $(DESTDIR)${gxx_include_dir}/${ext_builddir}; done
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}/${c_base_builddir}
	for file in ${c_base_headers_rename}; do \
	  $(INSTALL_DATA) ${c_base_builddir}/$${file} $(DESTDIR)${gxx_include_dir}/${c_base_builddir}; done
	c_base_headers_extra_install='$(c_base_headers_extra)';\
	for file in $$c_base_headers_extra_install; do \
	  $(INSTALL_DATA) $$file $(DESTDIR)${gxx_include_dir}/${bits_builddir}; done
	c_compatibility_headers_install='$(c_compatibility_headers_extra)';\
	for file in $$c_compatibility_headers_install; do \
	  $(INSTALL_DATA) $$file $(DESTDIR)${gxx_include_dir}; done
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}/${std_builddir}
	for file in ${std_headers_rename}; do \
	  $(INSTALL_DATA) ${std_builddir}/$${file} $(DESTDIR)${gxx_include_dir}/${std_builddir}; done
	$(mkinstalldirs) $(DESTDIR)${gxx_include_dir}/${target_builddir}
	for file in ${target_headers} ${target_headers_extra} \
	 ${thread_target_headers}; do \
	  $(INSTALL_DATA) $${file} $(DESTDIR)${gxx_include_dir}/${target_builddir}; done

# By adding these files here, automake will remove them for 'make clean'
CLEANFILES = *.pch stamp-std-precompile

# Stop implicit '.o' make rules from ever stomping on extensionless
# headers, in the improbable case where some foolish, crack-addled
# developer tries to create them via make in the include build
# directory. (This is more of an example of how this kind of rule can
# be made.)
.PRECIOUS: $(std_headers_rename) $(c_base_headers_rename)
$(std_headers_rename): ; @:
$(c_base_headers_rename): ; @:
