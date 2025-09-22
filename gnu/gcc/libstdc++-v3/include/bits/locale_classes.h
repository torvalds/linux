// Locale support -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file locale_classes.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 22.1  Locales
//

#ifndef _LOCALE_CLASSES_H
#define _LOCALE_CLASSES_H 1

#pragma GCC system_header

#include <bits/localefwd.h>
#include <cstring>		// For strcmp.
#include <string>
#include <ext/atomicity.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // 22.1.1 Class locale
  /**
   *  @brief  Container class for localization functionality.
   *
   *  The locale class is first a class wrapper for C library locales.  It is
   *  also an extensible container for user-defined localization.  A locale is
   *  a collection of facets that implement various localization features such
   *  as money, time, and number printing.
   *
   *  Constructing C++ locales does not change the C library locale.
   *
   *  This library supports efficient construction and copying of locales
   *  through a reference counting implementation of the locale class.
  */
  class locale
  {
  public:
    // Types:
    /// Definition of locale::category.
    typedef int	category;

    // Forward decls and friends:
    class facet;
    class id;
    class _Impl;

    friend class facet;
    friend class _Impl;

    template<typename _Facet>
      friend bool
      has_facet(const locale&) throw();

    template<typename _Facet>
      friend const _Facet&
      use_facet(const locale&);

    template<typename _Cache>
      friend struct __use_cache;

    //@{
    /**
     *  @brief  Category values.
     *
     *  The standard category values are none, ctype, numeric, collate, time,
     *  monetary, and messages.  They form a bitmask that supports union and
     *  intersection.  The category all is the union of these values.
     *
     *  @if maint
     *  NB: Order must match _S_facet_categories definition in locale.cc
     *  @endif
    */
    static const category none		= 0;
    static const category ctype		= 1L << 0;
    static const category numeric	= 1L << 1;
    static const category collate	= 1L << 2;
    static const category time		= 1L << 3;
    static const category monetary	= 1L << 4;
    static const category messages	= 1L << 5;
    static const category all		= (ctype | numeric | collate |
					   time  | monetary | messages);
    //@}

    // Construct/copy/destroy:

    /**
     *  @brief  Default constructor.
     *
     *  Constructs a copy of the global locale.  If no locale has been
     *  explicitly set, this is the "C" locale.
    */
    locale() throw();

    /**
     *  @brief  Copy constructor.
     *
     *  Constructs a copy of @a other.
     *
     *  @param  other  The locale to copy.
    */
    locale(const locale& __other) throw();

    /**
     *  @brief  Named locale constructor.
     *
     *  Constructs a copy of the named C library locale.
     *
     *  @param  s  Name of the locale to construct.
     *  @throw  std::runtime_error if s is null or an undefined locale.
    */
    explicit
    locale(const char* __s);

    /**
     *  @brief  Construct locale with facets from another locale.
     *
     *  Constructs a copy of the locale @a base.  The facets specified by @a
     *  cat are replaced with those from the locale named by @a s.  If base is
     *  named, this locale instance will also be named.
     *
     *  @param  base  The locale to copy.
     *  @param  s  Name of the locale to use facets from.
     *  @param  cat  Set of categories defining the facets to use from s.
     *  @throw  std::runtime_error if s is null or an undefined locale.
    */
    locale(const locale& __base, const char* __s, category __cat);

    /**
     *  @brief  Construct locale with facets from another locale.
     *
     *  Constructs a copy of the locale @a base.  The facets specified by @a
     *  cat are replaced with those from the locale @a add.  If @a base and @a
     *  add are named, this locale instance will also be named.
     *
     *  @param  base  The locale to copy.
     *  @param  add  The locale to use facets from.
     *  @param  cat  Set of categories defining the facets to use from add.
    */
    locale(const locale& __base, const locale& __add, category __cat);

    /**
     *  @brief  Construct locale with another facet.
     *
     *  Constructs a copy of the locale @a other.  The facet @f is added to
     *  @other, replacing an existing facet of type Facet if there is one.  If
     *  @f is null, this locale is a copy of @a other.
     *
     *  @param  other  The locale to copy.
     *  @param  f  The facet to add in.
    */
    template<typename _Facet>
      locale(const locale& __other, _Facet* __f);

    /// Locale destructor.
    ~locale() throw();

    /**
     *  @brief  Assignment operator.
     *
     *  Set this locale to be a copy of @a other.
     *
     *  @param  other  The locale to copy.
     *  @return  A reference to this locale.
    */
    const locale&
    operator=(const locale& __other) throw();

    /**
     *  @brief  Construct locale with another facet.
     *
     *  Constructs and returns a new copy of this locale.  Adds or replaces an
     *  existing facet of type Facet from the locale @a other into the new
     *  locale.
     *
     *  @param  Facet  The facet type to copy from other
     *  @param  other  The locale to copy from.
     *  @return  Newly constructed locale.
     *  @throw  std::runtime_error if other has no facet of type Facet.
    */
    template<typename _Facet>
      locale
      combine(const locale& __other) const;

    // Locale operations:
    /**
     *  @brief  Return locale name.
     *  @return  Locale name or "*" if unnamed.
    */
    string
    name() const;

    /**
     *  @brief  Locale equality.
     *
     *  @param  other  The locale to compare against.
     *  @return  True if other and this refer to the same locale instance, are
     *		 copies, or have the same name.  False otherwise.
    */
    bool
    operator==(const locale& __other) const throw ();

    /**
     *  @brief  Locale inequality.
     *
     *  @param  other  The locale to compare against.
     *  @return  ! (*this == other)
    */
    inline bool
    operator!=(const locale& __other) const throw ()
    { return !(this->operator==(__other));  }

    /**
     *  @brief  Compare two strings according to collate.
     *
     *  Template operator to compare two strings using the compare function of
     *  the collate facet in this locale.  One use is to provide the locale to
     *  the sort function.  For example, a vector v of strings could be sorted
     *  according to locale loc by doing:
     *  @code
     *  std::sort(v.begin(), v.end(), loc);
     *  @endcode
     *
     *  @param  s1  First string to compare.
     *  @param  s2  Second string to compare.
     *  @return  True if collate<Char> facet compares s1 < s2, else false.
    */
    template<typename _Char, typename _Traits, typename _Alloc>
      bool
      operator()(const basic_string<_Char, _Traits, _Alloc>& __s1,
		 const basic_string<_Char, _Traits, _Alloc>& __s2) const;

    // Global locale objects:
    /**
     *  @brief  Set global locale
     *
     *  This function sets the global locale to the argument and returns a
     *  copy of the previous global locale.  If the argument has a name, it
     *  will also call std::setlocale(LC_ALL, loc.name()).
     *
     *  @param  locale  The new locale to make global.
     *  @return  Copy of the old global locale.
    */
    static locale
    global(const locale&);

    /**
     *  @brief  Return reference to the "C" locale.
    */
    static const locale&
    classic();

  private:
    // The (shared) implementation
    _Impl*		_M_impl;

    // The "C" reference locale
    static _Impl*       _S_classic;

    // Current global locale
    static _Impl*	_S_global;

    // Names of underlying locale categories.
    // NB: locale::global() has to know how to modify all the
    // underlying categories, not just the ones required by the C++
    // standard.
    static const char* const* const _S_categories;

    // Number of standard categories. For C++, these categories are
    // collate, ctype, monetary, numeric, time, and messages. These
    // directly correspond to ISO C99 macros LC_COLLATE, LC_CTYPE,
    // LC_MONETARY, LC_NUMERIC, and LC_TIME. In addition, POSIX (IEEE
    // 1003.1-2001) specifies LC_MESSAGES.
    // In addition to the standard categories, the underlying
    // operating system is allowed to define extra LC_*
    // macros. For GNU systems, the following are also valid:
    // LC_PAPER, LC_NAME, LC_ADDRESS, LC_TELEPHONE, LC_MEASUREMENT,
    // and LC_IDENTIFICATION.
    enum { _S_categories_size = 6 + _GLIBCXX_NUM_CATEGORIES };

#ifdef __GTHREADS
    static __gthread_once_t _S_once;
#endif

    explicit
    locale(_Impl*) throw();

    static void
    _S_initialize();

    static void
    _S_initialize_once();

    static category
    _S_normalize_category(category);

    void
    _M_coalesce(const locale& __base, const locale& __add, category __cat);
  };


  // 22.1.1.1.2  Class locale::facet
  /**
   *  @brief  Localization functionality base class.
   *
   *  The facet class is the base class for a localization feature, such as
   *  money, time, and number printing.  It provides common support for facets
   *  and reference management.
   *
   *  Facets may not be copied or assigned.
  */
  class locale::facet
  {
  private:
    friend class locale;
    friend class locale::_Impl;

    mutable _Atomic_word		_M_refcount;

    // Contains data from the underlying "C" library for the classic locale.
    static __c_locale                   _S_c_locale;

    // String literal for the name of the classic locale.
    static const char			_S_c_name[2];

#ifdef __GTHREADS
    static __gthread_once_t		_S_once;
#endif

    static void
    _S_initialize_once();

  protected:
    /**
     *  @brief  Facet constructor.
     *
     *  This is the constructor provided by the standard.  If refs is 0, the
     *  facet is destroyed when the last referencing locale is destroyed.
     *  Otherwise the facet will never be destroyed.
     *
     *  @param refs  The initial value for reference count.
    */
    explicit
    facet(size_t __refs = 0) throw() : _M_refcount(__refs ? 1 : 0)
    { }

    /// Facet destructor.
    virtual
    ~facet();

    static void
    _S_create_c_locale(__c_locale& __cloc, const char* __s,
		       __c_locale __old = 0);

    static __c_locale
    _S_clone_c_locale(__c_locale& __cloc);

    static void
    _S_destroy_c_locale(__c_locale& __cloc);

    // Returns data from the underlying "C" library data for the
    // classic locale.
    static __c_locale
    _S_get_c_locale();

    static const char*
    _S_get_c_name();

  private:
    inline void
    _M_add_reference() const throw()
    { __gnu_cxx::__atomic_add_dispatch(&_M_refcount, 1); }

    inline void
    _M_remove_reference() const throw()
    {
      if (__gnu_cxx::__exchange_and_add_dispatch(&_M_refcount, -1) == 1)
	{
	  try
	    { delete this; }
	  catch (...)
	    { }
	}
    }

    facet(const facet&);  // Not defined.

    facet&
    operator=(const facet&);  // Not defined.
  };


  // 22.1.1.1.3 Class locale::id
  /**
   *  @brief  Facet ID class.
   *
   *  The ID class provides facets with an index used to identify them.
   *  Every facet class must define a public static member locale::id, or be
   *  derived from a facet that provides this member, otherwise the facet
   *  cannot be used in a locale.  The locale::id ensures that each class
   *  type gets a unique identifier.
  */
  class locale::id
  {
  private:
    friend class locale;
    friend class locale::_Impl;

    template<typename _Facet>
      friend const _Facet&
      use_facet(const locale&);

    template<typename _Facet>
      friend bool
      has_facet(const locale&) throw ();

    // NB: There is no accessor for _M_index because it may be used
    // before the constructor is run; the effect of calling a member
    // function (even an inline) would be undefined.
    mutable size_t		_M_index;

    // Last id number assigned.
    static _Atomic_word		_S_refcount;

    void
    operator=(const id&);  // Not defined.

    id(const id&);  // Not defined.

  public:
    // NB: This class is always a static data member, and thus can be
    // counted on to be zero-initialized.
    /// Constructor.
    id() { }

    size_t
    _M_id() const;
  };


  // Implementation object for locale.
  class locale::_Impl
  {
  public:
    // Friends.
    friend class locale;
    friend class locale::facet;

    template<typename _Facet>
      friend bool
      has_facet(const locale&) throw();

    template<typename _Facet>
      friend const _Facet&
      use_facet(const locale&);

    template<typename _Cache>
      friend struct __use_cache;

  private:
    // Data Members.
    _Atomic_word			_M_refcount;
    const facet**			_M_facets;
    size_t				_M_facets_size;
    const facet**			_M_caches;
    char**				_M_names;
    static const locale::id* const	_S_id_ctype[];
    static const locale::id* const	_S_id_numeric[];
    static const locale::id* const	_S_id_collate[];
    static const locale::id* const	_S_id_time[];
    static const locale::id* const	_S_id_monetary[];
    static const locale::id* const	_S_id_messages[];
    static const locale::id* const* const _S_facet_categories[];

    inline void
    _M_add_reference() throw()
    { __gnu_cxx::__atomic_add_dispatch(&_M_refcount, 1); }

    inline void
    _M_remove_reference() throw()
    {
      if (__gnu_cxx::__exchange_and_add_dispatch(&_M_refcount, -1) == 1)
	{
	  try
	    { delete this; }
	  catch(...)
	    { }
	}
    }

    _Impl(const _Impl&, size_t);
    _Impl(const char*, size_t);
    _Impl(size_t) throw();

   ~_Impl() throw();

    _Impl(const _Impl&);  // Not defined.

    void
    operator=(const _Impl&);  // Not defined.

    inline bool
    _M_check_same_name()
    {
      bool __ret = true;
      if (_M_names[1])
	// We must actually compare all the _M_names: can be all equal!
	for (size_t __i = 0; __ret && __i < _S_categories_size - 1; ++__i)
	  __ret = std::strcmp(_M_names[__i], _M_names[__i + 1]) == 0;
      return __ret;
    }

    void
    _M_replace_categories(const _Impl*, category);

    void
    _M_replace_category(const _Impl*, const locale::id* const*);

    void
    _M_replace_facet(const _Impl*, const locale::id*);

    void
    _M_install_facet(const locale::id*, const facet*);

    template<typename _Facet>
      inline void
      _M_init_facet(_Facet* __facet)
      { _M_install_facet(&_Facet::id, __facet); }

    void
    _M_install_cache(const facet*, size_t);
  };

  template<typename _Facet>
    locale::locale(const locale& __other, _Facet* __f)
    {
      _M_impl = new _Impl(*__other._M_impl, 1);

      try
	{ _M_impl->_M_install_facet(&_Facet::id, __f); }
      catch(...)
	{
	  _M_impl->_M_remove_reference();
	  __throw_exception_again;
	}
      delete [] _M_impl->_M_names[0];
      _M_impl->_M_names[0] = 0;   // Unnamed.
    }

_GLIBCXX_END_NAMESPACE

#endif
