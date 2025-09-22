// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <clocale>
#include <cstring>
#include <locale>

namespace __gnu_cxx
{
  using namespace std;

  // Defined in globals.cc.
  extern locale::facet* facet_vec[_GLIBCPP_NUM_FACETS];
  extern locale::facet* facet_cache_vec[2 * _GLIBCPP_NUM_FACETS];
  extern char* facet_name[6 + _GLIBCPP_NUM_CATEGORIES];

  extern std::ctype<char>			ctype_c;
  extern std::collate<char> 			collate_c;
  extern numpunct<char> 			numpunct_c;
  extern num_get<char> 				num_get_c;
  extern num_put<char> 				num_put_c;
  extern codecvt<char, char, mbstate_t>		codecvt_c;
  extern moneypunct<char, false> 		moneypunct_fc;
  extern moneypunct<char, true> 		moneypunct_tc;
  extern money_get<char> 			money_get_c;
  extern money_put<char> 			money_put_c;
  extern __timepunct<char> 			timepunct_c;
  extern time_get<char> 			time_get_c;
  extern time_put<char> 			time_put_c;
  extern std::messages<char> 			messages_c;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern std::ctype<wchar_t>			ctype_w;
  extern std::collate<wchar_t> 			collate_w;
  extern numpunct<wchar_t> 			numpunct_w;
  extern num_get<wchar_t> 			num_get_w;
  extern num_put<wchar_t> 			num_put_w;
  extern codecvt<wchar_t, char, mbstate_t>	codecvt_w;
  extern moneypunct<wchar_t, false> 		moneypunct_fw;
  extern moneypunct<wchar_t, true> 		moneypunct_tw;
  extern money_get<wchar_t> 			money_get_w;
  extern money_put<wchar_t> 			money_put_w;
  extern __timepunct<wchar_t> 			timepunct_w;
  extern time_get<wchar_t> 			time_get_w;
  extern time_put<wchar_t> 			time_put_w;
  extern std::messages<wchar_t> 		messages_w;
#endif

  extern std::__locale_cache<numpunct<char> >	locale_cache_np_c;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern std::__locale_cache<numpunct<wchar_t> >	locale_cache_np_w;
#endif
} // namespace __gnu_cxx

namespace std
{
  using namespace __gnu_cxx;

  locale::_Impl::
  ~_Impl() throw()
  {
    // Clean up facets, then caches.  No cache refcounts for now.
    if (_M_facets)
      {
	for (size_t __i = 0; __i < _M_facets_size; ++__i)
	  if (_M_facets[__i])
	    _M_facets[__i]->_M_remove_reference();

	for (size_t __i = _M_facets_size; __i < 2*_M_facets_size; ++__i)
	  if (_M_facets[__i])
	    delete (__locale_cache_base*)_M_facets[__i];
      }
    delete [] _M_facets;

    for (size_t __i = 0; 
	 __i < _S_categories_size + _S_extra_categories_size; ++__i)
      delete [] _M_names[__i];
  }

  // Clone existing _Impl object.
  locale::_Impl::
  _Impl(const _Impl& __imp, size_t __refs)
  : _M_references(__refs), _M_facets_size(__imp._M_facets_size) // XXX
  {
    _M_facets = 0;
    for (size_t __i = 0; __i < _S_categories_size
	                 + _S_extra_categories_size; ++__i)
      _M_names[__i] = 0;
    try
      { 
	// Space for facets and matching caches
	_M_facets = new facet*[2*_M_facets_size]; 
	for (size_t __i = 0; __i < 2*_M_facets_size; ++__i)
	  _M_facets[__i] = 0;
	for (size_t __i = 0; __i < _M_facets_size; ++__i)
	  {
	    _M_facets[__i] = __imp._M_facets[__i];
	    if (_M_facets[__i])
	      _M_facets[__i]->_M_add_reference();
	  }
	for (size_t __i = 0; 
	     __i < _S_categories_size + _S_extra_categories_size; ++__i)
	  {
	    size_t __len = strlen(__imp._M_names[__i]) + 1;
	    char* __new = new char[__len];
	    strlcpy(__new, __imp._M_names[__i], __len);
	    _M_names[__i] = __new;
	  }
      }
    catch(...)
      {
	this->~_Impl();
	__throw_exception_again;	
      }
  }

  // Construct named _Impl.
  locale::_Impl::
  _Impl(const char* __s, size_t __refs) 
  : _M_references(__refs), _M_facets_size(_GLIBCPP_NUM_FACETS) 
  {
    // Initialize the underlying locale model, which also checks
    // to see if the given name is valid.
    __c_locale __cloc;
    locale::facet::_S_create_c_locale(__cloc, __s);

    _M_facets = 0;
    for (size_t __i = 0; __i < _S_categories_size
	                 + _S_extra_categories_size; ++__i)
      _M_names[__i] = 0;	
    try
      {
	// Space for facets and matching caches
	_M_facets = new facet*[2*_M_facets_size]; 
	for (size_t __i = 0; __i < 2*_M_facets_size; ++__i)
	  _M_facets[__i] = 0;

	// Name all the categories.
	size_t __len = strlen(__s);
	if (!strchr(__s, ';'))
	  {
	    for (size_t __i = 0; 
		 __i < _S_categories_size + _S_extra_categories_size; ++__i)
	      {
		_M_names[__i] = new char[__len + 1];
		strlcpy(_M_names[__i], __s, __len + 1);
	      }
	  }
	else
	  {
	    const char* __beg = __s;
	    for (size_t __i = 0; 
		 __i < _S_categories_size + _S_extra_categories_size; ++__i)
	      {
		__beg = strchr(__beg, '=') + 1;
		const char* __end = strchr(__beg, ';');
		if (!__end)
		  __end = __s + __len;
		char* __new = new char[__end - __beg + 1];
		memcpy(__new, __beg, __end - __beg);
		__new[__end - __beg] = '\0';
		_M_names[__i] = __new;
	      }
	  }

	// Construct all standard facets and add them to _M_facets.  
	_M_init_facet(new std::ctype<char>(__cloc, 0, false));
	_M_init_facet(new codecvt<char, char, mbstate_t>);
	_M_init_facet(new numpunct<char>(__cloc));
	_M_init_facet(new num_get<char>);
	_M_init_facet(new num_put<char>);
	_M_init_facet(new std::collate<char>(__cloc));
	_M_init_facet(new moneypunct<char, false>(__cloc, __s));
	_M_init_facet(new moneypunct<char, true>(__cloc, __s));
	_M_init_facet(new money_get<char>);
	_M_init_facet(new money_put<char>);
	_M_init_facet(new __timepunct<char>(__cloc, __s));
	_M_init_facet(new time_get<char>);
	_M_init_facet(new time_put<char>);
	_M_init_facet(new std::messages<char>(__cloc, __s));
	
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
	_M_init_facet(new std::ctype<wchar_t>(__cloc));
	_M_init_facet(new codecvt<wchar_t, char, mbstate_t>);
	_M_init_facet(new numpunct<wchar_t>(__cloc));
	_M_init_facet(new num_get<wchar_t>);
	_M_init_facet(new num_put<wchar_t>);
	_M_init_facet(new std::collate<wchar_t>(__cloc));
	_M_init_facet(new moneypunct<wchar_t, false>(__cloc, __s));
	_M_init_facet(new moneypunct<wchar_t, true>(__cloc, __s));
	_M_init_facet(new money_get<wchar_t>);
	_M_init_facet(new money_put<wchar_t>);
	_M_init_facet(new __timepunct<wchar_t>(__cloc, __s));
	_M_init_facet(new time_get<wchar_t>);
	_M_init_facet(new time_put<wchar_t>);
	_M_init_facet(new std::messages<wchar_t>(__cloc, __s));
#endif	  
	locale::facet::_S_destroy_c_locale(__cloc);
      }
    catch(...)
      {
	locale::facet::_S_destroy_c_locale(__cloc);
	this->~_Impl();
	__throw_exception_again;
      }    
  }

  // Construct "C" _Impl.
  locale::_Impl::
  _Impl(facet**, size_t __refs, bool) 
  : _M_references(__refs), _M_facets_size(_GLIBCPP_NUM_FACETS)
  {
    // Initialize the underlying locale model.
    locale::facet::_S_c_name[0] = 'C';
    locale::facet::_S_c_name[1] = '\0';
    locale::facet::_S_create_c_locale(locale::facet::_S_c_locale, 
				      locale::facet::_S_c_name);

    // Space for facets and matching caches
    _M_facets = new(&facet_cache_vec) facet*[2*_M_facets_size];
    for (size_t __i = 0; __i < 2*_M_facets_size; ++__i)
      _M_facets[__i] = 0;

    // Name all the categories.
    for (size_t __i = 0; 
	 __i < _S_categories_size + _S_extra_categories_size; ++__i)
      {
	_M_names[__i]  = new (&facet_name[__i]) char[2];
	strlcpy(_M_names[__i], locale::facet::_S_c_name, 2);
      }

    // This is needed as presently the C++ version of "C" locales
    // != data in the underlying locale model for __timepunct,
    // numpunct, and moneypunct. Also, the "C" locales must be
    // constructed in a way such that they are pre-allocated.
    // NB: Set locale::facets(ref) count to one so that each individual
    // facet is not destroyed when the locale (and thus locale::_Impl) is
    // destroyed.
    _M_init_facet(new (&ctype_c) std::ctype<char>(0, false, 1));
    _M_init_facet(new (&codecvt_c) codecvt<char, char, mbstate_t>(1));
    _M_init_facet(new (&numpunct_c) numpunct<char>(1));
    _M_init_facet(new (&num_get_c) num_get<char>(1));
    _M_init_facet(new (&num_put_c) num_put<char>(1));
    _M_init_facet(new (&collate_c) std::collate<char>(1));
    _M_init_facet(new (&moneypunct_fc) moneypunct<char, false>(1));
    _M_init_facet(new (&moneypunct_tc) moneypunct<char, true>(1));
    _M_init_facet(new (&money_get_c) money_get<char>(1));
    _M_init_facet(new (&money_put_c) money_put<char>(1));
    _M_init_facet(new (&timepunct_c) __timepunct<char>(1));
    _M_init_facet(new (&time_get_c) time_get<char>(1));
    _M_init_facet(new (&time_put_c) time_put<char>(1));
    _M_init_facet(new (&messages_c) std::messages<char>(1));	
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    _M_init_facet(new (&ctype_w) std::ctype<wchar_t>(1));
    _M_init_facet(new (&codecvt_w) codecvt<wchar_t, char, mbstate_t>(1));
    _M_init_facet(new (&numpunct_w) numpunct<wchar_t>(1));
    _M_init_facet(new (&num_get_w) num_get<wchar_t>(1));
    _M_init_facet(new (&num_put_w) num_put<wchar_t>(1));
    _M_init_facet(new (&collate_w) std::collate<wchar_t>(1));
    _M_init_facet(new (&moneypunct_fw) moneypunct<wchar_t, false>(1));
    _M_init_facet(new (&moneypunct_tw) moneypunct<wchar_t, true>(1));
    _M_init_facet(new (&money_get_w) money_get<wchar_t>(1));
    _M_init_facet(new (&money_put_w) money_put<wchar_t>(1));
    _M_init_facet(new (&timepunct_w) __timepunct<wchar_t>(1));
    _M_init_facet(new (&time_get_w) time_get<wchar_t>(1));
    _M_init_facet(new (&time_put_w) time_put<wchar_t>(1));
    _M_init_facet(new (&messages_w) std::messages<wchar_t>(1));
#endif 

    // Initialize the static locale caches for C locale.

    locale ltmp(this);		// Doesn't bump refcount
    _M_add_reference();		// Bump so destructor doesn't trash us

    // These need to be built in static allocated memory.  There must
    // be a better way to do this!
    __locale_cache<numpunct<char> >* __lc =
      new (&locale_cache_np_c) __locale_cache<numpunct<char> >(ltmp, true);
    _M_facets[numpunct<char>::id._M_id() + _M_facets_size] =
      reinterpret_cast<locale::facet*>(__lc);
      
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    __locale_cache<numpunct<wchar_t> >* __wlc =
      new (&locale_cache_np_w) __locale_cache<numpunct<wchar_t> >(ltmp, true);
    _M_facets[numpunct<wchar_t>::id._M_id() + _M_facets_size] =
      reinterpret_cast<locale::facet*>(__wlc);
#endif    
  }
  
  void
  locale::_Impl::
  _M_replace_categories(const _Impl* __imp, category __cat)
  {
    category __mask;
    for (size_t __ix = 0; __ix < _S_categories_size; ++__ix)
      {
	__mask = 1 << __ix;
	if (__mask & __cat)
	  {
	    // Need to replace entry in _M_facets with other locale's info.
	    _M_replace_category(__imp, _S_facet_categories[__ix]);
	    // If both have names, go ahead and mangle.
	    if (strcmp(_M_names[__ix], "*") != 0 
		&& strcmp(__imp->_M_names[__ix], "*") != 0)
	      {
	        size_t __len = strlen(__imp->_M_names[__ix]) + 1;
		char* __new = new char[__len];
		strlcpy(__new, __imp->_M_names[__ix], __len);
		delete [] _M_names[__ix];
		_M_names[__ix] = __new;
	      }
	  }
      }
  }

  void
  locale::_Impl::
  _M_replace_category(const _Impl* __imp, const locale::id* const* __idpp)
  {
    for (; *__idpp; ++__idpp)
      _M_replace_facet(__imp, *__idpp);
  }
  
  void
  locale::_Impl::
  _M_replace_facet(const _Impl* __imp, const locale::id* __idp)
  {
    size_t __index = __idp->_M_id();
    if ((__index > (__imp->_M_facets_size - 1)) || !__imp->_M_facets[__index])
      __throw_runtime_error("no locale facet");
    _M_install_facet(__idp, __imp->_M_facets[__index]); 
  }

  void
  locale::_Impl::
  _M_install_facet(const locale::id* __idp, facet* __fp)
  {
    if (__fp)
      {
	size_t __index = __idp->_M_id();

	// Check size of facet vector to ensure adequate room.
	if (__index > _M_facets_size - 1)
	  {
	    facet** __old = _M_facets;
	    facet** __new;
	    const size_t __new_size = __index + 4;
	    __new = new facet*[2 * __new_size]; 
	    for (size_t __i = 0; __i < _M_facets_size; ++__i)
	      __new[__i] = _M_facets[__i];
	    for (size_t __i2 = _M_facets_size; __i2 < __new_size; ++__i2)
	      __new[__i2] = 0;
	    // Also copy caches and clear extra space
	    for (size_t __i = 0; __i < _M_facets_size; ++__i)
	      __new[__i + __new_size] = _M_facets[__i + _M_facets_size];
	    for (size_t __i2 = _M_facets_size; __i2 < __new_size; ++__i2)
	      __new[__i2 + __new_size] = 0;

	    _M_facets_size = __new_size;
	    _M_facets = __new;
	    delete [] __old;
	  }

	__fp->_M_add_reference();
	facet*& __fpr = _M_facets[__index];
	if (__fpr)
	  {
	    // Replacing an existing facet. Order matters.
	    __fpr->_M_remove_reference();
	    __fpr = __fp;
	  }
	else
	  {
	    // Installing a newly created facet into an empty
	    // _M_facets container, say a newly-constructed,
	    // swanky-fresh _Impl.
	    _M_facets[__index] = __fp;
	  }
      }
  }
} // namespace std
