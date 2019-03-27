# $FreeBSD$

.include <src.opts.mk>

GCCVER=	4.2
GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs
SRCDIR=	${SRCTOP}/contrib/libstdc++
SUPDIR=	${SRCDIR}/libsupc++

.PATH: ${SRCDIR}/src ${SRCDIR}/libmath ${SRCDIR}/config/io \
       ${SRCDIR}/config/locale/darwin ${SRCDIR}/config/locale/generic \
       ${SRCDIR}/include ${SUPDIR} ${GCCDIR} ${GCCLIB}/libiberty

LIB=		stdc++
SHLIB_MAJOR=	6

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CFLAGS+=	-I${.CURDIR} -I${SUPDIR} -I${GCCDIR} -I${SRCDIR}/include
CFLAGS+=	-I${GCCLIB}/include -I${SRCDIR}/include -I.
CFLAGS+=	-frandom-seed=RepeatabilityConsideredGood
CXXFLAGS+=	-fno-implicit-templates -ffunction-sections -fdata-sections \
		-Wno-deprecated
PO_CXXFLAGS=	${CXXFLAGS:N-ffunction-sections}

LIBADD+=	m
LDADD=		-Wl,-f,libsupc++.so.1

# libstdc++ sources
SRCS+=	bitmap_allocator.cc pool_allocator.cc \
	mt_allocator.cc codecvt.cc compatibility.cc complex_io.cc \
	ctype.cc debug.cc debug_list.cc functexcept.cc globals_io.cc \
	ios.cc ios_failure.cc ios_init.cc ios_locale.cc limits.cc \
	list.cc locale.cc locale_init.cc locale_facets.cc \
	localename.cc stdexcept.cc strstream.cc tree.cc \
	allocator-inst.cc concept-inst.cc fstream-inst.cc ext-inst.cc \
	ios-inst.cc iostream-inst.cc istream-inst.cc istream.cc \
	locale-inst.cc misc-inst.cc ostream-inst.cc sstream-inst.cc \
	streambuf-inst.cc streambuf.cc string-inst.cc valarray-inst.cc \
	wlocale-inst.cc wstring-inst.cc atomicity.cc \
	codecvt_members.cc collate_members.cc ctype_members.cc \
	messages_members.cc monetary_members.cc numeric_members.cc \
	time_members.cc basic_file_stdio.cc c_locale.cc \
#	compatibility-ldbl.cc

# target sources
SRCS+=  atomicity.cc codecvt_members.cc collate_members.cc ctype_members.cc \
	messages_members.cc monetary_members.cc numeric_members.cc \
	time_members.cc

# target sources extra
SRCS+=  basic_file_stdio.cc c_locale.cc

# C parts of math
SRCS+=	stubs.c

# Embedded copy of libsupc++
SRCS+=	del_op.cc del_opnt.cc del_opv.cc del_opvnt.cc eh_alloc.cc eh_arm.cc \
	eh_aux_runtime.cc eh_call.cc eh_catch.cc eh_exception.cc eh_globals.cc \
	eh_personality.cc eh_term_handler.cc eh_terminate.cc eh_throw.cc \
	eh_type.cc eh_unex_handler.cc guard.cc new_handler.cc new_op.cc \
	new_opnt.cc new_opv.cc new_opvnt.cc pure.cc tinfo.cc tinfo2.cc \
	vec.cc vterminate.cc

# from libiberty:
SRCS+=	cp-demangle.c

# MD headers location
.if ${MACHINE_CPUARCH} == "sparc64"
MARCHDIR=	sparc
.elif ${MACHINE_CPUARCH} == "i386" && ${MACHINE_CPU} != 'i386'
MARCHDIR=	i486
.elif ${MACHINE_CPUARCH} == "amd64"
MARCHDIR=	i486
.else
MARCHDIR=	${MACHINE_CPUARCH}
.endif

.if exists(${SRCDIR}/config/cpu/${MARCHDIR}/atomicity.h)
ATOMICITY_H=	${SRCDIR}/config/cpu/${MARCHDIR}/atomicity.h
.else
ATOMICITY_H=	${SRCDIR}/config/cpu/generic/atomicity_mutex/atomicity.h
.endif

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
.if exists(${SRCDIR}/config/cpu/generic/atomicity_builtins/atomicity.h)
ATOMICITY_H=	${SRCDIR}/config/cpu/generic/atomicity_builtins/atomicity.h
.endif
.endif

atomicity.cc:	${ATOMICITY_H} .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}
CLEANFILES+=	atomicity.cc

INCSGROUPS=	BITSHDRS BKWHDRS EXTHDRS BASEHDRS BASEXHDRS STDHDRS \
		TARGETHDRS THRHDRS DEBUGHDRS TR1HDRS PBHDRS0 PBHDRS1
CXXINCLUDEDIR=	${INCLUDEDIR}/c++/${GCCVER}

STDHDRS=	std_algorithm.h std_bitset.h std_complex.h std_deque.h \
		std_fstream.h std_functional.h std_iomanip.h std_ios.h \
		std_iosfwd.h std_iostream.h std_istream.h std_iterator.h \
		std_limits.h std_list.h std_locale.h std_map.h std_memory.h \
		std_numeric.h std_ostream.h std_queue.h std_set.h \
		std_sstream.h std_stack.h std_stdexcept.h std_streambuf.h \
		std_string.h std_utility.h std_valarray.h std_vector.h
.for h in ${STDHDRS}
STDHDRSNAME_$h=${h:R:S;^std_;;}
.endfor
STDHDRS:=	${STDHDRS:S;^;${SRCDIR}/include/std/;}
STDHDRSDIR=	${CXXINCLUDEDIR}

BITSHDRS=	allocator.h basic_ios.h basic_ios.tcc basic_string.h \
		basic_string.tcc boost_concept_check.h char_traits.h codecvt.h \
		concept_check.h cpp_type_traits.h deque.tcc fstream.tcc \
		functexcept.h gslice.h gslice_array.h indirect_array.h \
		ios_base.h istream.tcc list.tcc locale_classes.h \
		locale_facets.h locale_facets.tcc localefwd.h mask_array.h \
		ostream.tcc ostream_insert.h postypes.h stream_iterator.h \
		streambuf_iterator.h slice_array.h sstream.tcc stl_algo.h \
		stl_algobase.h stl_bvector.h stl_construct.h stl_deque.h \
		stl_function.h stl_heap.h stl_iterator.h \
		stl_iterator_base_funcs.h stl_iterator_base_types.h stl_list.h \
		stl_map.h stl_multimap.h stl_multiset.h stl_numeric.h \
		stl_pair.h stl_queue.h stl_raw_storage_iter.h stl_relops.h \
		stl_set.h stl_stack.h stl_tempbuf.h stl_tree.h \
		stl_uninitialized.h stl_vector.h streambuf.tcc stringfwd.h \
		valarray_array.h valarray_array.tcc valarray_before.h \
		valarray_after.h vector.tcc
BITSHDRS:=	${BITSHDRS:S;^;${SRCDIR}/include/bits/;}
BITSHDRSDIR=	${CXXINCLUDEDIR}/bits

BKWHDRS=	complex.h iomanip.h istream.h ostream.h stream.h streambuf.h \
		algo.h algobase.h alloc.h bvector.h defalloc.h deque.h \
		function.h hash_map.h hash_set.h hashtable.h heap.h iostream.h \
		iterator.h list.h map.h multimap.h new.h multiset.h pair.h \
		queue.h rope.h set.h slist.h stack.h tempbuf.h tree.h \
		vector.h fstream.h strstream backward_warning.h
BKWHDRS:=	${BKWHDRS:S;^;${SRCDIR}/include/backward/;}
BKWHDRSDIR=	${CXXINCLUDEDIR}/backward

EXTHDRS=	algorithm atomicity.h array_allocator.h bitmap_allocator.h \
		codecvt_specializations.h concurrence.h debug_allocator.h \
		stdio_filebuf.h stdio_sync_filebuf.h functional \
		hash_map hash_set hash_fun.h hashtable.h iterator \
		malloc_allocator.h memory mt_allocator.h new_allocator.h \
		numeric numeric_traits.h pod_char_traits.h pool_allocator.h \
		rb_tree rope ropeimpl.h slist throw_allocator.h typelist.h \
		type_traits.h rc_string_base.h sso_string_base.h vstring.h \
		vstring.tcc vstring_fwd.h vstring_util.h

EXTHDRS:=	${EXTHDRS:S;^;${SRCDIR}/include/ext/;}
EXTHDRSDIR=	${CXXINCLUDEDIR}/ext

TR1HDRS=	array bind_repeat.h bind_iterate.h boost_shared_ptr.h cctype \
		cfenv cfloat cinttypes climits cmath common.h complex cstdarg \
		cstdbool cstdint cstdio cstdlib ctgmath ctime ctype.h cwchar \
		cwctype fenv.h float.h functional functional_hash.h \
		functional_iterate.h hashtable hashtable_policy.h inttypes.h \
		limits.h math.h memory mu_iterate.h random random.tcc \
		ref_fwd.h ref_wrap_iterate.h repeat.h stdarg.h stdbool.h \
		stdint.h stdio.h stdlib.h tgmath.h tuple tuple_defs.h \
		tuple_iterate.h type_traits type_traits_fwd.h unordered_set \
		unordered_map utility wchar.h wctype.h

TR1HDRS:=	${TR1HDRS:S;^;${SRCDIR}/include/tr1/;}
TR1HDRSDIR=	${CXXINCLUDEDIR}/tr1

# This is the common subset of files that all three "C" header models use.
BASEHDRS=	std_cassert.h std_cctype.h std_cerrno.h std_cfloat.h \
		std_ciso646.h std_climits.h std_clocale.h std_cmath.h \
		std_csetjmp.h std_csignal.h std_cstdarg.h std_cstddef.h \
		std_cstdio.h std_cstdlib.h std_cstring.h std_ctime.h \
		std_cwchar.h std_cwctype.h
.for h in ${BASEHDRS}
BASEHDRSNAME_$h=${h:R:S;^std_;;}
.endfor
BASEHDRS:=	${BASEHDRS:S;^;${SRCDIR}/include/c_std/;}
BASEHDRSDIR=	${CXXINCLUDEDIR}

# Some of the different "C" header models need extra files.
BASEXHDRS=	${SRCDIR}/include/c_std/cmath.tcc
BASEXHDRSDIR=	${CXXINCLUDEDIR}/bits

DEBUGHDRS=	bitset debug.h deque formatter.h functions.h hash_map \
		hash_map.h hash_multimap.h hash_multiset.h hash_set hash_set.h \
		list macros.h map map.h multimap.h multiset.h safe_base.h \
		safe_iterator.h safe_iterator.tcc safe_sequence.h set set.h \
		string vector
DEBUGHDRS:=	${DEBUGHDRS:S;^;${SRCDIR}/include/debug/;}
DEBUGHDRSDIR=	${CXXINCLUDEDIR}/debug

# Not installed, but kept here for completeness
COMPATHDRS=	assert.h ctype.h errno.h float.h iso646.h limits.h locale.h \
		math.h setjmp.h signal.h stdarg.h stddef.h stdio.h stdlib.h \
		string.h time.h wchar.h wctype.h
COMPATHDRS:=	${COMPATHDRS:S;^;${SRCDIR}/include/debug/;}
COMPATHDRSDIR=	${CXXINCLUDEDIR}/c_compatibility

PBHDRS0 = \
	assoc_container.hpp \
	exception.hpp \
	hash_policy.hpp \
	list_update_policy.hpp \
	priority_queue.hpp \
	tag_and_trait.hpp \
	tree_policy.hpp \
	trie_policy.hpp
PBHDRS0:=	${PBHDRS0:S;^;${SRCDIR}/include/ext/pb_ds/;}
PBHDRS0DIR=	${CXXINCLUDEDIR}/ext/pb_ds

PBHDRS1 = \
	basic_types.hpp \
	cond_dealtor.hpp \
	constructors_destructor_fn_imps.hpp \
	container_base_dispatch.hpp \
	map_debug_base.hpp \
	priority_queue_base_dispatch.hpp \
	standard_policies.hpp \
	tree_trace_base.hpp \
	type_utils.hpp \
	types_traits.hpp
PBHDRS1:=	${PBHDRS1:S;^;${SRCDIR}/include/ext/pb_ds/detail/;}
PBHDRS1DIR=	${CXXINCLUDEDIR}/ext/pb_ds/detail

PBHDRS+= \
	basic_tree_policy/basic_tree_policy_base.hpp \
	basic_tree_policy/null_node_metadata.hpp \
	basic_tree_policy/traits.hpp \
	basic_types.hpp \
	binary_heap_/binary_heap_.hpp \
	binary_heap_/const_iterator.hpp \
	binary_heap_/const_point_iterator.hpp \
	binary_heap_/constructors_destructor_fn_imps.hpp \
	binary_heap_/debug_fn_imps.hpp \
	binary_heap_/entry_cmp.hpp \
	binary_heap_/entry_pred.hpp \
	binary_heap_/erase_fn_imps.hpp \
	binary_heap_/find_fn_imps.hpp \
	binary_heap_/info_fn_imps.hpp \
	binary_heap_/insert_fn_imps.hpp \
	binary_heap_/iterators_fn_imps.hpp \
	binary_heap_/policy_access_fn_imps.hpp \
	binary_heap_/resize_policy.hpp \
	binary_heap_/split_join_fn_imps.hpp \
	binary_heap_/trace_fn_imps.hpp \
	binomial_heap_base_/binomial_heap_base_.hpp \
	binomial_heap_base_/constructors_destructor_fn_imps.hpp \
	binomial_heap_base_/debug_fn_imps.hpp \
	binomial_heap_base_/erase_fn_imps.hpp \
	binomial_heap_base_/find_fn_imps.hpp \
	binomial_heap_base_/insert_fn_imps.hpp \
	binomial_heap_base_/split_join_fn_imps.hpp \
	binomial_heap_/binomial_heap_.hpp \
	binomial_heap_/constructors_destructor_fn_imps.hpp \
	binomial_heap_/debug_fn_imps.hpp \
	bin_search_tree_/bin_search_tree_.hpp \
	bin_search_tree_/cond_dtor_entry_dealtor.hpp \
	bin_search_tree_/cond_key_dtor_entry_dealtor.hpp

PBHDRS+= \
	bin_search_tree_/constructors_destructor_fn_imps.hpp \
	bin_search_tree_/debug_fn_imps.hpp \
	bin_search_tree_/erase_fn_imps.hpp \
	bin_search_tree_/find_fn_imps.hpp \
	bin_search_tree_/info_fn_imps.hpp \
	bin_search_tree_/insert_fn_imps.hpp \
	bin_search_tree_/iterators_fn_imps.hpp \
	bin_search_tree_/node_iterators.hpp \
	bin_search_tree_/point_iterators.hpp \
	bin_search_tree_/policy_access_fn_imps.hpp \
	bin_search_tree_/r_erase_fn_imps.hpp \
	bin_search_tree_/rotate_fn_imps.hpp \
	bin_search_tree_/split_join_fn_imps.hpp \
	bin_search_tree_/traits.hpp \
	cc_hash_table_map_/cc_ht_map_.hpp \
	cc_hash_table_map_/cmp_fn_imps.hpp \
	cc_hash_table_map_/cond_key_dtor_entry_dealtor.hpp \
	cc_hash_table_map_/constructor_destructor_fn_imps.hpp \
	cc_hash_table_map_/constructor_destructor_no_store_hash_fn_imps.hpp \
	cc_hash_table_map_/constructor_destructor_store_hash_fn_imps.hpp \
	cc_hash_table_map_/debug_fn_imps.hpp \
	cc_hash_table_map_/debug_no_store_hash_fn_imps.hpp \
	cc_hash_table_map_/debug_store_hash_fn_imps.hpp \
	cc_hash_table_map_/entry_list_fn_imps.hpp \
	cc_hash_table_map_/erase_fn_imps.hpp \
	cc_hash_table_map_/erase_no_store_hash_fn_imps.hpp \
	cc_hash_table_map_/erase_store_hash_fn_imps.hpp \
	cc_hash_table_map_/find_fn_imps.hpp \
	cc_hash_table_map_/find_store_hash_fn_imps.hpp \
	cc_hash_table_map_/info_fn_imps.hpp \
	cc_hash_table_map_/insert_fn_imps.hpp \
	cc_hash_table_map_/insert_no_store_hash_fn_imps.hpp \
	cc_hash_table_map_/insert_store_hash_fn_imps.hpp \
	cc_hash_table_map_/iterators_fn_imps.hpp \
	cc_hash_table_map_/policy_access_fn_imps.hpp \
	cc_hash_table_map_/resize_fn_imps.hpp \
	cc_hash_table_map_/resize_no_store_hash_fn_imps.hpp \
	cc_hash_table_map_/resize_store_hash_fn_imps.hpp \
	cc_hash_table_map_/size_fn_imps.hpp \
	cc_hash_table_map_/standard_policies.hpp

PBHDRS+= \
	cc_hash_table_map_/trace_fn_imps.hpp \
	cond_dealtor.hpp \
	constructors_destructor_fn_imps.hpp \
	container_base_dispatch.hpp \
	eq_fn/eq_by_less.hpp \
	eq_fn/hash_eq_fn.hpp \
	gp_hash_table_map_/constructor_destructor_fn_imps.hpp \
	gp_hash_table_map_/constructor_destructor_no_store_hash_fn_imps.hpp \
	gp_hash_table_map_/constructor_destructor_store_hash_fn_imps.hpp \
	gp_hash_table_map_/debug_fn_imps.hpp \
	gp_hash_table_map_/debug_no_store_hash_fn_imps.hpp \
	gp_hash_table_map_/debug_store_hash_fn_imps.hpp \
	gp_hash_table_map_/erase_fn_imps.hpp \
	gp_hash_table_map_/erase_no_store_hash_fn_imps.hpp \
	gp_hash_table_map_/erase_store_hash_fn_imps.hpp \
	gp_hash_table_map_/find_fn_imps.hpp \
	gp_hash_table_map_/find_no_store_hash_fn_imps.hpp \
	gp_hash_table_map_/find_store_hash_fn_imps.hpp \
	gp_hash_table_map_/gp_ht_map_.hpp \
	gp_hash_table_map_/info_fn_imps.hpp \
	gp_hash_table_map_/insert_fn_imps.hpp \
	gp_hash_table_map_/insert_no_store_hash_fn_imps.hpp \
	gp_hash_table_map_/insert_store_hash_fn_imps.hpp \
	gp_hash_table_map_/iterator_fn_imps.hpp \
	gp_hash_table_map_/policy_access_fn_imps.hpp \
	gp_hash_table_map_/resize_fn_imps.hpp \
	gp_hash_table_map_/resize_no_store_hash_fn_imps.hpp \
	gp_hash_table_map_/resize_store_hash_fn_imps.hpp \
	gp_hash_table_map_/standard_policies.hpp \
	gp_hash_table_map_/trace_fn_imps.hpp \
	hash_fn/direct_mask_range_hashing_imp.hpp \
	hash_fn/direct_mod_range_hashing_imp.hpp \
	hash_fn/linear_probe_fn_imp.hpp \
	hash_fn/mask_based_range_hashing.hpp \
	hash_fn/mod_based_range_hashing.hpp \
	hash_fn/probe_fn_base.hpp \
	hash_fn/quadratic_probe_fn_imp.hpp \
	hash_fn/ranged_hash_fn.hpp \
	hash_fn/ranged_probe_fn.hpp

PBHDRS+= \
	hash_fn/sample_probe_fn.hpp \
	hash_fn/sample_ranged_hash_fn.hpp \
	hash_fn/sample_ranged_probe_fn.hpp \
	hash_fn/sample_range_hashing.hpp \
	left_child_next_sibling_heap_/const_iterator.hpp \
	left_child_next_sibling_heap_/const_point_iterator.hpp \
	left_child_next_sibling_heap_/constructors_destructor_fn_imps.hpp \
	left_child_next_sibling_heap_/debug_fn_imps.hpp \
	left_child_next_sibling_heap_/erase_fn_imps.hpp \
	left_child_next_sibling_heap_/info_fn_imps.hpp \
	left_child_next_sibling_heap_/insert_fn_imps.hpp \
	left_child_next_sibling_heap_/iterators_fn_imps.hpp \
	left_child_next_sibling_heap_/left_child_next_sibling_heap_.hpp \
	left_child_next_sibling_heap_/node.hpp \
	left_child_next_sibling_heap_/null_metadata.hpp \
	left_child_next_sibling_heap_/policy_access_fn_imps.hpp \
	left_child_next_sibling_heap_/trace_fn_imps.hpp \
	list_update_map_/constructor_destructor_fn_imps.hpp \
	list_update_map_/debug_fn_imps.hpp \
	list_update_map_/entry_metadata_base.hpp \
	list_update_map_/erase_fn_imps.hpp \
	list_update_map_/find_fn_imps.hpp \
	list_update_map_/info_fn_imps.hpp \
	list_update_map_/insert_fn_imps.hpp \
	list_update_map_/iterators_fn_imps.hpp \
	list_update_map_/lu_map_.hpp \
	list_update_map_/trace_fn_imps.hpp \
	list_update_policy/counter_lu_metadata.hpp \
	list_update_policy/counter_lu_policy_imp.hpp \
	list_update_policy/mtf_lu_policy_imp.hpp \
	list_update_policy/sample_update_policy.hpp \
	map_debug_base.hpp \
	ov_tree_map_/cond_dtor.hpp \
	ov_tree_map_/constructors_destructor_fn_imps.hpp \
	ov_tree_map_/debug_fn_imps.hpp \
	ov_tree_map_/erase_fn_imps.hpp \
	ov_tree_map_/info_fn_imps.hpp \
	ov_tree_map_/insert_fn_imps.hpp \
	ov_tree_map_/iterators_fn_imps.hpp \
	ov_tree_map_/node_iterators.hpp \
	ov_tree_map_/ov_tree_map_.hpp

PBHDRS+= \
	ov_tree_map_/policy_access_fn_imps.hpp \
	ov_tree_map_/split_join_fn_imps.hpp \
	ov_tree_map_/traits.hpp \
	pairing_heap_/constructors_destructor_fn_imps.hpp \
	pairing_heap_/debug_fn_imps.hpp \
	pairing_heap_/erase_fn_imps.hpp \
	pairing_heap_/find_fn_imps.hpp \
	pairing_heap_/insert_fn_imps.hpp \
	pairing_heap_/pairing_heap_.hpp \
	pairing_heap_/split_join_fn_imps.hpp \
	pat_trie_/child_iterator.hpp \
	pat_trie_/cond_dtor_entry_dealtor.hpp \
	pat_trie_/const_child_iterator.hpp \
	pat_trie_/constructors_destructor_fn_imps.hpp \
	pat_trie_/debug_fn_imps.hpp \
	pat_trie_/erase_fn_imps.hpp \
	pat_trie_/find_fn_imps.hpp \
	pat_trie_/head.hpp \
	pat_trie_/info_fn_imps.hpp \
	pat_trie_/insert_join_fn_imps.hpp \
	pat_trie_/internal_node.hpp \
	pat_trie_/iterators_fn_imps.hpp \
	pat_trie_/leaf.hpp \
	pat_trie_/node_base.hpp \
	pat_trie_/node_iterators.hpp \
	pat_trie_/node_metadata_base.hpp \
	pat_trie_/pat_trie_.hpp \
	pat_trie_/point_iterators.hpp \
	pat_trie_/policy_access_fn_imps.hpp \
	pat_trie_/r_erase_fn_imps.hpp \
	pat_trie_/rotate_fn_imps.hpp \
	pat_trie_/split_fn_imps.hpp \
	pat_trie_/split_join_branch_bag.hpp \
	pat_trie_/synth_e_access_traits.hpp \
	pat_trie_/trace_fn_imps.hpp \
	pat_trie_/traits.hpp \
	pat_trie_/update_fn_imps.hpp \
	priority_queue_base_dispatch.hpp \
	rb_tree_map_/constructors_destructor_fn_imps.hpp \
	rb_tree_map_/debug_fn_imps.hpp

PBHDRS+= \
	rb_tree_map_/erase_fn_imps.hpp \
	rb_tree_map_/find_fn_imps.hpp \
	rb_tree_map_/info_fn_imps.hpp \
	rb_tree_map_/insert_fn_imps.hpp \
	rb_tree_map_/node.hpp \
	rb_tree_map_/rb_tree_.hpp \
	rb_tree_map_/split_join_fn_imps.hpp \
	rb_tree_map_/traits.hpp \
	rc_binomial_heap_/constructors_destructor_fn_imps.hpp \
	rc_binomial_heap_/debug_fn_imps.hpp \
	rc_binomial_heap_/erase_fn_imps.hpp \
	rc_binomial_heap_/insert_fn_imps.hpp \
	rc_binomial_heap_/rc_binomial_heap_.hpp \
	rc_binomial_heap_/rc.hpp \
	rc_binomial_heap_/split_join_fn_imps.hpp \
	rc_binomial_heap_/trace_fn_imps.hpp \
	resize_policy/cc_hash_max_collision_check_resize_trigger_imp.hpp \
	resize_policy/hash_exponential_size_policy_imp.hpp \
	resize_policy/hash_load_check_resize_trigger_imp.hpp \
	resize_policy/hash_load_check_resize_trigger_size_base.hpp \
	resize_policy/hash_prime_size_policy_imp.hpp \
	resize_policy/hash_standard_resize_policy_imp.hpp \
	resize_policy/sample_resize_policy.hpp \
	resize_policy/sample_resize_trigger.hpp \
	resize_policy/sample_size_policy.hpp \
	splay_tree_/constructors_destructor_fn_imps.hpp \
	splay_tree_/debug_fn_imps.hpp \
	splay_tree_/erase_fn_imps.hpp \
	splay_tree_/find_fn_imps.hpp \
	splay_tree_/info_fn_imps.hpp \
	splay_tree_/insert_fn_imps.hpp \
	splay_tree_/node.hpp \
	splay_tree_/splay_fn_imps.hpp \
	splay_tree_/splay_tree_.hpp \
	splay_tree_/split_join_fn_imps.hpp \
	splay_tree_/traits.hpp \
	standard_policies.hpp \
	thin_heap_/constructors_destructor_fn_imps.hpp \
	thin_heap_/debug_fn_imps.hpp \
	thin_heap_/erase_fn_imps.hpp

PBHDRS+= \
	thin_heap_/find_fn_imps.hpp \
	thin_heap_/insert_fn_imps.hpp \
	thin_heap_/split_join_fn_imps.hpp \
	thin_heap_/thin_heap_.hpp \
	thin_heap_/trace_fn_imps.hpp \
	tree_policy/node_metadata_selector.hpp \
	tree_policy/null_node_update_imp.hpp \
	tree_policy/order_statistics_imp.hpp \
	tree_policy/sample_tree_node_update.hpp \
	tree_trace_base.hpp \
	trie_policy/node_metadata_selector.hpp \
	trie_policy/null_node_update_imp.hpp \
	trie_policy/order_statistics_imp.hpp \
	trie_policy/prefix_search_node_update_imp.hpp \
	trie_policy/sample_trie_e_access_traits.hpp \
	trie_policy/sample_trie_node_update.hpp \
	trie_policy/string_trie_e_access_traits_imp.hpp \
	trie_policy/trie_policy_base.hpp \
	types_traits.hpp \
	type_utils.hpp \
	unordered_iterator/const_iterator.hpp \
	unordered_iterator/const_point_iterator.hpp \
	unordered_iterator/iterator.hpp \
	unordered_iterator/point_iterator.hpp

PBHDRSDIRS= \
	pairing_heap_ \
	splay_tree_ \
	list_update_map_ \
	basic_tree_policy \
	trie_policy \
	gp_hash_table_map_ \
	tree_policy \
	binomial_heap_base_ \
	resize_policy \
	bin_search_tree_ \
	binomial_heap_ \
	thin_heap_ \
	pat_trie_ \
	cc_hash_table_map_ \
	rc_binomial_heap_ \
	left_child_next_sibling_heap_ \
	unordered_iterator \
	binary_heap_ \
	ov_tree_map_ \
	hash_fn \
	eq_fn \
	rb_tree_map_ \
	list_update_policy

.for D in ${PBHDRSDIRS}
PHDRGRP$D:=	${PBHDRS:M$D/*:S;^;${SRCDIR}/include/ext/pb_ds/detail/;}
PHDRGRP$DDIR=	${CXXINCLUDEDIR}/ext/pb_ds/detail/$D
INCSGROUPS+=	PHDRGRP$D
.endfor

TARGETHDRS=	abi/compatibility.h \
		allocator/new_allocator_base.h \
		io/basic_file_stdio.h \
		io/c_io_stdio.h \
		locale/generic/c_locale.h \
		locale/generic/c++locale_internal.h \
		locale/generic/messages_members.h \
		locale/generic/time_members.h \
		os/bsd/freebsd/ctype_base.h \
		os/bsd/freebsd/ctype_inline.h \
		os/bsd/freebsd/ctype_noninline.h \
		os/bsd/freebsd/os_defines.h
TARGETHDRS:=	${TARGETHDRS:S;^;${SRCDIR}/config/;}
TARGETHDRS+=	c++config.h
TARGETHDRSNAME_basic_file_stdio.h= basic_file.h
TARGETHDRSNAME_c_io_stdio.h= c++io.h
TARGETHDRSNAME_c_locale.h= c++locale.h
TARGETHDRSNAME_new_allocator_base.h= c++allocator.h
TARGETHDRSDIR=	${CXXINCLUDEDIR}/bits

MARCHHDRS=	atomic_word.h cpu_defines.h cxxabi_tweaks.h
.for h in ${MARCHHDRS}
.if exists(${SRCDIR}/config/cpu/${MARCHDIR}/${h})
TARGETHDRS+=	${SRCDIR}/config/cpu/${MARCHDIR}/${h}
.else
TARGETHDRS+=	${SRCDIR}/config/cpu/generic/${h}
.endif
.endfor

THRHDRS=	gthr.h gthr-single.h gthr-posix.h gthr-tpf.h gthr-default.h
THRHDRSDIR=	${CXXINCLUDEDIR}/bits
uppercase =	[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]

gthr.h: ${GCCDIR}/gthr.h
	sed -e '/^#pragma/b' \
	    -e '/^#/s/\(${uppercase}${uppercase}*\)/_GLIBCXX_\1/g' \
	    -e 's/_GLIBCXX_SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${.ALLSRC} > ${.TARGET}

gthr-single.h: ${GCCDIR}/gthr-single.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCXX_\1/g' \
	    < ${.ALLSRC} > ${.TARGET}

gthr-posix.h: ${GCCDIR}/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCXX_\1/g' \
	    -e 's/SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/\(${uppercase}*USE_WEAK\)/_GLIBCXX_\1/g' \
	    < ${.ALLSRC} > ${.TARGET}

gthr-tpf.h: ${GCCDIR}/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCXX_\1/g' \
	    -e 's/SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/\(${uppercase}*USE_WEAK\)/_GLIBCXX_\1/g' \
	    < ${.ALLSRC} > ${.TARGET}

gthr-default.h: ${GCCDIR}/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC${uppercase}*_H\)/_GLIBCXX_\1/g' \
	    -e 's/SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/\(${uppercase}*USE_WEAK\)/_GLIBCXX_\1/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	${THRHDRS}

.if ${MACHINE_CPUARCH} == "arm"
unwind.h: ${GCCDIR}/config/arm/unwind-arm.h
.else
unwind.h: ${GCCDIR}/unwind-generic.h
.endif

unwind.h: .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

SRCS+=		unwind.h
CLEANFILES+=	unwind.h

DATESTAMP!=	cat ${GCCDIR}/DATESTAMP

c++config.h: ${.CURDIR}/config.h ${SRCDIR}/include/bits/c++config
	sed -e "s/\\(define __GLIBCXX__\\)/\1 ${DATESTAMP}/" \
	    -e 's/\(define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY\)/\1 1/' \
	    -e 's/\(define _GLIBCXX_NAMESPACE_ASSOCIATION_VERSION\)/\1 0/' \
	    < ${SRCDIR}/include/bits/c++config > ${.TARGET} && \
	sed -e 's/HAVE_/_GLIBCXX_HAVE_/g' \
	    -e 's/PACKAGE/_GLIBCXX_PACKAGE/g' \
	    -e 's/VERSION/_GLIBCXX_VERSION/g' \
	    -e 's/WORDS_/_GLIBCXX_WORDS_/g' \
	    < ${.CURDIR}/config.h >> ${.TARGET} && \
	echo "#endif // _CXXCONFIG_" >> ${.TARGET}
CLEANFILES+=	c++config.h

# Symbol versioning.

VERSION_MAP=  	libstdc++.map

${VERSION_MAP}: ${SRCDIR}/config/abi/pre/gnu.ver .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${VERSION_MAP}

CXXFLAGS.clang+= -stdlib=libstdc++

.include <bsd.lib.mk>

# Filter out libc++-specific flags, and -std= flags above c++98 or gnu++98.
CXXFLAGS:=	${CXXFLAGS:N-stdlib=libc++:N-std=c++[01][13x]:N-std=gnu++[01][13x]}
