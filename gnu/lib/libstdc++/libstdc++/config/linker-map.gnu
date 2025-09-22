## Linker script for GNU ld 2.11.94+ only.
##
## Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
##
## This file is part of the libstdc++ version 3 distribution.
##
## This file is part of the GNU ISO C++ Library.  This library is free
## software; you can redistribute it and/or modify it under the
## terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2, or (at your option)
## any later version.
##
## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License along
## with this library; see the file COPYING.  If not, write to the Free
## Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
## USA.

GLIBCPP_3.2 {

  global:

    # Names inside the 'extern' block are demangled names.
    # All but the last are terminated with a semicolon.
    extern "C++"
    {
      std::[A-Za]*;
      std::ba[a-r]*;
      std::basic_[a-h]*;
      std::basic_ifstream*;
      std::basic_istringstream*;
      std::basic_istream*;
      std::basic_iostream*;
      std::basic_[j-r]*; 
      std::basic_streambuf*;
      std::basic_stringbuf*;
      std::basic_stringstream*;
      std::basic_[t-z]*;
      std::ba[t-z]*;
      std::b[b-z]*;
      std::c[a-n]*;
      std::co[a-c]*;
      std::codecvt_byname*;
      std::codecvt::[A-Za-b]*;
      std::codecvt::[A-Zd-z]*;
      std::codecvt_c;
      std::codecvt_w;
      std::co[e-z]*;
      std::c[p-z]*;
      std::c_[a-z]*;	
      std::[A-Zd-k]*;
      std::length_error*;
      std::logic_error*;
      std::locale::[A-Za-e]*;
      std::locale::facet::[A-Za-z]*;
      std::locale::facet::_M*;
      std::locale::facet::_S_c_locale;
      std::locale::facet::_S_clone_c_locale*;
      std::locale::facet::_S_create_c_locale*;
      std::locale::facet::_S_destroy_c_locale*;
      std::locale::[A-Zg-z]*;
      std::locale::_[A-Ra-z]*;
      std::locale::_S_classic;
      std::locale::_S_global;
      std::locale::_S_num_categories;
      std::locale::_S_normalize_category*;
      std::locale::_[T-Za-z]*;
      std::[A-Zm]*;
      std::n[a-t]*;
      std::num_put_[cw];
      std::numeric*;
      std::numpunct*;
      std::num_get*;
      std::num_get_[cw];
      std::n[v-z]*;
      std::ostrstream*;
      std::overflow_error*;
      std::out_of_range*;
      std::[A-Zp-z]*;
      std::__throw_*;
      std::__numeric_limits_base*;
      std::__timepunct*;
      std::_S_bit_count;
      std::_S_first_one
    };

    # Names not in an 'extern' block are mangled names.
    _ZSt7getline*;
    _ZStrs*;
    _ZNSo*;
    _ZNKSt9basic_ios*;
    _ZNSt9basic_iosI[cw]St11char_traitsI[cw]EE15_M_cache_facetsERKSt6locale;
    _ZNSt9basic_iosI[cw]St11char_traitsI[cw]EE[A-Z]*;
    _ZNSt9basic_iosI[cw]St11char_traitsI[cw]EE[0-9][A-Za-z]*;
    _ZNSt9basic_iosI[cw]St11char_traitsI[cw]EE[0-9][0-9][A-Za-z]*;

    _ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEEC*;
    _ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEED*;
    _ZNSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEEC*;
    _ZNSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEED*;

    _ZNKSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE6do_put*;

    _ZNKSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE3put*;
    _ZNSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE2idE;

    _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE14_M_convert_intI[lmxy]EES3_S3_RSt8ios_basecccT_;

    _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE14_M_convert_intI[lmxy]EES3_S3_RSt8ios_basewccT_;

    _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE16_M_convert_floatI[de]EES3_S3_RSt8ios_baseccT_;

    _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE16_M_convert_floatI[de]EES3_S3_RSt8ios_basewcT_;

    _ZNKSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE12_M_widen_int*;

    _ZNKSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE14_M_widen_float*;

    _ZNKSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE9_M_insert*;

    _ZSt9use_facetISt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEEERKT_RKSt6locale;

    # __num_base
    _ZNSt10__num_base13_S_format_intERKSt8ios_basePccc;
    _ZNSt10__num_base15_S_format_floatERKSt8ios_basePcc[il];
    _ZNSt10__num_base8_S_atomsE;

    # std::string minus binary operator plus
    _ZNKSs*;
    _ZNKSb*;
    _ZNSs[A-Za-z]*;
    _ZNSs[0-9][A-Za-z]*;
    _ZNSs[0-9][0-9][A-Za-z]*;
    _ZNSs[0-9]_[A-Ra-z]*;
    _ZNSs[0-9][0-9]_[A-Ra-z]*;
    _ZNSs12_S_empty_repEv;
    _ZNSs20_S_empty_rep_storageE;
    _ZNSbIwSt11char_traitsIwESaIwEE20_S_empty_rep_storageE;
    _ZNSs12_S_constructE*;
    _ZNSs13_S_copy_charsE*;
    _ZNSbIwSt11char_traitsIwESaIwEE[A-Ra-z]*;
    _ZNSbIwSt11char_traitsIwESaIwEE[0-9][A-Ra-z]*;
    _ZNSbIwSt11char_traitsIwESaIwEE[0-9][0-9][A-Ra-z]*;
    _ZNSbIwSt11char_traitsIwESaIwEE[0-9]_[A-Ra-z]*;
    _ZNSbIwSt11char_traitsIwESaIwEE[0-9][0-9]_[A-Ra-z]*;
    _ZNSbIwSt11char_traitsIwESaIwEE13_S_copy_chars*;
    _ZNSbIwSt11char_traitsIwESaIwEE12_S_constructE[jm]wRKS1_;
    _ZNSbIwSt11char_traitsIwESaIwEE12_S_empty_repEv;
    _ZSt24__uninitialized_copy_auxIN9*;
    _ZSt26__uninitialized_fill_n_aux*;
    _ZStplIcSt11char_traitsIcESaIcEESbIT_T0_T1_EPKS3_RKS6_;
    _ZStplIcSt11char_traitsIcESaIcEESbIT_T0_T1_ES3_RKS6_;
    _ZStplIwSt11char_traitsIwESaIwEESbIT_T0_T1_EPKS3_RKS6_;
    _ZStplIwSt11char_traitsIwESaIwEESbIT_T0_T1_ES3_RKS6_;

    # std::__basic_file minus showmanyc_helper
    _ZNSt12__basic_fileIcED*;
    _ZNSt12__basic_fileIcEC*;	
    _ZNSt12__basic_fileIcE8sys_open*;
    _ZNSt12__basic_fileIcE8sys_getc*;
    _ZNSt12__basic_fileIcE10sys_ungetc*;
    _ZNSt12__basic_fileIcE7seekpos*;
    _ZNSt12__basic_fileIcE7seekoff*;
    _ZNSt12__basic_fileIcE6xsputn*;
    _ZNSt12__basic_fileIcE6xsgetn*;
    _ZNSt12__basic_fileIcE5close*;
    _ZNSt12__basic_fileIcE4sync*;
    _ZNSt12__basic_fileIcE4open*;
    _ZNSt12__basic_fileIcE2fd*;
    _ZNSt12__basic_fileIcE12_M_open_modeE*;
    _ZNKSt12__basic_fileIcE7is_open*;

    # std::locale destructors
    _ZNSt6localeD*;
	
    # std::locale::facet destructors
    _ZNSt6locale5facetD*;
	 
    # std::codecvt<char> members.
    _ZNKSt7codecvtIcc11__mbstate_tE*;
    # std::codecvt<char>::~codecvt
    _ZNSt7codecvtIcc11__mbstate_tED*;
    # std::codecvt<char>::codecvt(size_t), where size_t variable.
    _ZNSt7codecvtIcc11__mbstate_tEC[12]E[jm];
    # std::codecvt<char>::id
    _ZNSt7codecvtIcc11__mbstate_tE2idE;

    # std::codecvt<wchar_t> members.
    _ZNKSt7codecvtIwc11__mbstate_tE*;
    # std::codecvt<wchar_t>::~codecvt
    _ZNSt7codecvtIwc11__mbstate_tED*;
    # std::codecvt<wchar_t>::codecvt(size_t), where size_t variable.
    _ZNSt7codecvtIwc11__mbstate_tEC[12]E[jm];
    # std::codecvt<wchar_t>::id
    _ZNSt7codecvtIwc11__mbstate_tE2idE;

     # std::use_facet<codecvt>
    _ZSt9use_facetISt7codecvtIcc11__mbstate_tEERKT_RKSt6locale;
    _ZSt9use_facetISt7codecvtIwc11__mbstate_tEERKT_RKSt6locale;

    # std::has_facet*
    _ZSt9has_facet*;

    # std::__default_alloc_template
    _ZNSt24__default_alloc_templateILb1ELi0EE10deallocate*;
    _ZNSt24__default_alloc_templateILb1ELi0EE8allocate*;
    _ZNSt24__default_alloc_templateILb1ELi0EE12_S_free_listE;
    _ZNSt24__default_alloc_templateILb1ELi0EE22_S_node_allocator_lockE;
    _ZNSt24__default_alloc_templateILb1ELi0EE9_S_refillE*;

    # std::__default_alloc_template to be removed in the future
    _ZNSt24__default_alloc_templateILb1ELi0EE10reallocateEPv*;
    _ZNSt24__default_alloc_templateILb1ELi0EE11_S_round_upE*;
    _ZNSt24__default_alloc_templateILb1ELi0EE14_S_chunk_allocE*;
    _ZNSt24__default_alloc_templateILb1ELi0EE17_S_freelist_indexE*;
    _ZNSt24__default_alloc_templateILb1ELi0EE11_S_end_freeE;
    _ZNSt24__default_alloc_templateILb1ELi0EE12_S_heap_sizeE;
    _ZNSt24__default_alloc_templateILb1ELi0EE13_S_start_freeE;
    _ZNSt24__default_alloc_templateILb1ELi0EE5_Lock*;

    # operator new(unsigned)
    _Znwj;
    # operator new(unsigned, std::nothrow_t const&)
    _ZnwjRKSt9nothrow_t;
    # operator new(unsigned long)
    _Znwm;
    # operator new(unsigned long, std::nothrow_t const&)
    _ZnwmRKSt9nothrow_t;

    # operator delete(void*)
    _ZdlPv;
    # operator delete(void*, std::nothrow_t const&)
    _ZdlPvRKSt9nothrow_t;

    # operator new[](unsigned)
    _Znaj;
    # operator new[](unsigned, std::nothrow_t const&)
    _ZnajRKSt9nothrow_t;
    # operator new[](unsigned long)
    _Znam;
    # operator new[](unsigned long, std::nothrow_t const&)
    _ZnamRKSt9nothrow_t;

    # operator delete[](void*)
    _ZdaPv;
    # operator delete[](void*, std::nothrow_t const&)
    _ZdaPvRKSt9nothrow_t;

    # vtable
    _ZTVS[a-z];
    _ZTVSt[0-9][A-Za-z]*;
    _ZTVSt[0-9][0-9][A-Za-z]*;
    _ZTTS[a-z];
    _ZTTSt[0-9][A-Za-z]*;
    _ZTTSt[0-9][0-9][A-Za-z]*;
    _ZTVN9__gnu_cxx*;
    _ZTVNSt6locale5facetE;
    _ZTVSt11__timepunctI[cw]E;
    _ZTVNSt8ios_base7failureE;
    _ZTVSt23__codecvt_abstract_baseI[cw]c11__mbstate_tE;
    _ZTVSt21__ctype_abstract_baseI[cw]E;

    # XXX
    _ZTVN10__cxxabi*;

    # typeinfo
    _ZTI[a-z];
    _ZTIP[a-z];
    _ZTIPK[a-z];
    _ZTIS[a-z];
    _ZTISt[0-9][A-Za-z]*;
    _ZTISt[0-9][0-9][A-Za-z]*;
    _ZTS[a-z];
    _ZTSS[a-z];
    _ZTSP[a-z];
    _ZTSPK[a-z];
    _ZTSSt[0-9][A-Za-z]*;
    _ZTSSt[0-9][0-9][A-Za-z]*;
    _ZTSN9__gnu_cxx*;
    _ZTIN9__gnu_cxx*;
    _ZTINSt8ios_base7failureE;
    _ZTSNSt8ios_base7failureE;
    _ZTINSt6locale5facetE;
    _ZTSNSt6locale5facetE;
    _ZTISt11__timepunctI[cw]E;
    _ZTSSt11__timepunctI[cw]E;
    _ZTSSt10__num_base;
    _ZTISt10__num_base;
    _ZTSSt21__ctype_abstract_baseI[cw]E;
    _ZTISt21__ctype_abstract_baseI[cw]E;
    _ZTISt23__codecvt_abstract_baseI[cw]c11__mbstate_tE;
    _ZTSSt23__codecvt_abstract_baseI[cw]c11__mbstate_tE;

    # XXX
    _ZTIN10__cxxabi*;
    _ZTSN10__cxxabi*;

    # function-scope static objects requires a guard variable.
    _ZGV*;

    # virtual function thunks
    _ZTh*;
    _ZTv*;
    _ZTc*;

    # std::__convert_to_v
    _ZSt14__convert_to_v*;

  local:
    *;
};

# Symbols added after GLIBCPP_3.2
GLIBCPP_3.2.1 {

  _ZNSt7codecvtIcc11__mbstate_tEC1EP15__locale_structj;
  _ZNSt7codecvtIcc11__mbstate_tEC2EP15__locale_structj;
  _ZNSt7codecvtIwc11__mbstate_tEC1EP15__locale_structj;
  _ZNSt7codecvtIwc11__mbstate_tEC2EP15__locale_structj;

  _ZStplIcSt11char_traitsIcESaIcEESbIT_T0_T1_ERKS6_S8_;
  _ZStplIwSt11char_traitsIwESaIwEESbIT_T0_T1_ERKS6_S8_;

  # stub functions from libmath
  sinf;
  sinl;
  sinhf;
  sinhl;
  cosf;
  cosl;
  coshf;
  coshl;
  tanf;
  tanl;
  tanhf;
  tanhl;
  atan2f;
  atan2l;
  expf;
  expl;
  hypotf;
  hypotl;
  hypot;
  logf;
  logl;
  log10f;
  log10l;
  powf;
  powl;
  sqrtf;
  sqrtl;
  copysignf;
  nan;
  __signbit;
  __signbitf;
  __signbitl;

} GLIBCPP_3.2;

GLIBCPP_3.2.2 {

  _ZNSt24__default_alloc_templateILb1ELi0EE12_S_force_newE;

} GLIBCPP_3.2.1;

GLIBCPP_3.2.3 {

  global:

    extern "C++"
    {
      # Needed only when generic cpu's atomicity.h is in use.
      __gnu_cxx::_Atomic_add_mutex;
      __gnu_cxx::_Atomic_add_mutex_once;
      __gnu_cxx::__gthread_atomic_add_mutex_once;
	
      std::__num_base::_S_atoms_in;
      std::__num_base::_S_atoms_out
    };

    _ZNKSt7num_putI[wc]St19ostreambuf_iteratorI[wc]St11char_traitsI[wc]EEE6_M_pad*;

    _ZNKSt7num_putI[cw]St19ostreambuf_iteratorI[cw]St11char_traitsI[cw]EEE14_M_convert_intI[yxml]EES3_S3_RSt8ios_base[cw]T_;

    _ZNKSt7num_putI[wc]St19ostreambuf_iteratorI[wc]St11char_traitsI[wc]EEE14_M_group_float*;

  _ZNKSt7num_putI[wc]St19ostreambuf_iteratorI[wc]St11char_traitsI[wc]EEE12_M_group_int*;

    # __basic_file::showmanyc_helper
    _ZNSt12__basic_fileIcE16showmanyc_helperEv;

} GLIBCPP_3.2.2;

GLIBCPP_3.2.4 {

  _ZNSt9basic_iosI[cw]St11char_traitsI[cw]EE11_M_setstateESt12_Ios_Iostate;

} GLIBCPP_3.2.3;

# Symbols in the support library (libsupc++) have their own tag.
CXXABI_1.2 {

  global:
    __cxa_allocate_exception;
    __cxa_bad_cast;
    __cxa_bad_typeid;
    __cxa_begin_catch;
    __cxa_call_unexpected;
    __cxa_current_exception_type;
    __cxa_demangle;
    __cxa_end_catch;
    __cxa_free_exception;
    __cxa_get_globals;
    __cxa_get_globals_fast;
    __cxa_pure_virtual;
    __cxa_rethrow;
    __cxa_throw;
    __cxa_vec_cctor;
    __cxa_vec_cleanup;
    __cxa_vec_ctor;
    __cxa_vec_delete2;
    __cxa_vec_delete3;
    __cxa_vec_delete;
    __cxa_vec_dtor;
    __cxa_vec_new2;
    __cxa_vec_new3;
    __cxa_vec_new;
    __gxx_personality_v0;
    __gxx_personality_sj0;
    __dynamic_cast;

    # __gnu_cxx::_verbose_terminate_handler()
    _ZN9__gnu_cxx27__verbose_terminate_handlerEv;

    # XXX Should not be exported.
    __cxa_dyn_string_append_char;
    __cxa_dyn_string_append_cstr;
    __cxa_dyn_string_append;
    __cxa_dyn_string_clear;
    __cxa_dyn_string_copy_cstr;
    __cxa_dyn_string_copy;
    __cxa_dyn_string_delete;
    __cxa_dyn_string_eq;
    __cxa_dyn_string_init;
    __cxa_dyn_string_insert_char;
    __cxa_dyn_string_insert_cstr;
    __cxa_dyn_string_insert;
    __cxa_dyn_string_new;
    __cxa_dyn_string_prepend_cstr;
    __cxa_dyn_string_prepend;
    __cxa_dyn_string_release;
    __cxa_dyn_string_resize;
    __cxa_dyn_string_substring;

  local:
    *;
};

# Symbols added after CXXABI_1.2
CXXABI_1.2.1 {

    __cxa_guard_acquire;
    __cxa_guard_release;
    __cxa_guard_abort;

} CXXABI_1.2;

CXXABI_1.2.2 {

    # *_type_info classes, ctor and dtor
    _ZN10__cxxabiv117__array_type_info*;
    _ZN10__cxxabiv117__class_type_info*;
    _ZN10__cxxabiv116__enum_type_info*;
    _ZN10__cxxabiv120__function_type_info*;
    _ZN10__cxxabiv123__fundamental_type_info*;
    _ZN10__cxxabiv117__pbase_type_info*;
    _ZN10__cxxabiv129__pointer_to_member_type_info*;
    _ZN10__cxxabiv119__pointer_type_info*;
    _ZN10__cxxabiv120__si_class_type_info*;
    _ZN10__cxxabiv121__vmi_class_type_info*;

    # *_type_info classes, member functions
    _ZNK10__cxxabiv117__class_type_info*;
    _ZNK10__cxxabiv120__function_type_info*;
    _ZNK10__cxxabiv117__pbase_type_info*;
    _ZNK10__cxxabiv129__pointer_to_member_type_info*;
    _ZNK10__cxxabiv119__pointer_type_info*;
    _ZNK10__cxxabiv120__si_class_type_info*;
    _ZNK10__cxxabiv121__vmi_class_type_info*;

} CXXABI_1.2.1;
