// Safe iterator implementation  -*- C++ -*-

// Copyright (C) 2003, 2004, 2005, 2006
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

/** @file debug/safe_iterator.h
 *  This file is a GNU debug extension to the Standard C++ Library.
 */

#ifndef _GLIBCXX_DEBUG_SAFE_ITERATOR_H
#define _GLIBCXX_DEBUG_SAFE_ITERATOR_H 1

#include <debug/debug.h>
#include <debug/macros.h>
#include <debug/functions.h>
#include <debug/formatter.h>
#include <debug/safe_base.h>
#include <bits/stl_pair.h>
#include <ext/type_traits.h>

namespace __gnu_debug
{
  /** Iterators that derive from _Safe_iterator_base but that aren't
   *  _Safe_iterators can be determined singular or non-singular via
   *  _Safe_iterator_base.
   */
  inline bool 
  __check_singular_aux(const _Safe_iterator_base* __x)
  { return __x->_M_singular(); }

  /** \brief Safe iterator wrapper.
   *
   *  The class template %_Safe_iterator is a wrapper around an
   *  iterator that tracks the iterator's movement among sequences and
   *  checks that operations performed on the "safe" iterator are
   *  legal. In additional to the basic iterator operations (which are
   *  validated, and then passed to the underlying iterator),
   *  %_Safe_iterator has member functions for iterator invalidation,
   *  attaching/detaching the iterator from sequences, and querying
   *  the iterator's state.
   */
  template<typename _Iterator, typename _Sequence>
    class _Safe_iterator : public _Safe_iterator_base
    {
      typedef _Safe_iterator _Self;

      /** The precision to which we can calculate the distance between
       *  two iterators.
       */
      enum _Distance_precision
	{
	  __dp_equality, //< Can compare iterator equality, only
	  __dp_sign,     //< Can determine equality and ordering
	  __dp_exact     //< Can determine distance precisely
	};

      /// The underlying iterator
      _Iterator _M_current;

      /// Determine if this is a constant iterator.
      bool
      _M_constant() const
      {
	typedef typename _Sequence::const_iterator const_iterator;
	return __is_same<const_iterator, _Safe_iterator>::value;
      }

      typedef std::iterator_traits<_Iterator> _Traits;

    public:
      typedef _Iterator                           _Base_iterator;
      typedef typename _Traits::iterator_category iterator_category;
      typedef typename _Traits::value_type        value_type;
      typedef typename _Traits::difference_type   difference_type;
      typedef typename _Traits::reference         reference;
      typedef typename _Traits::pointer           pointer;

      /// @post the iterator is singular and unattached
      _Safe_iterator() : _M_current() { }

      /**
       * @brief Safe iterator construction from an unsafe iterator and
       * its sequence.
       *
       * @pre @p seq is not NULL
       * @post this is not singular
       */
      _Safe_iterator(const _Iterator& __i, const _Sequence* __seq)
      : _Safe_iterator_base(__seq, _M_constant()), _M_current(__i)
      {
	_GLIBCXX_DEBUG_VERIFY(! this->_M_singular(),
			      _M_message(__msg_init_singular)
			      ._M_iterator(*this, "this"));
      }

      /**
       * @brief Copy construction.
       * @pre @p x is not singular
       */
      _Safe_iterator(const _Safe_iterator& __x)
      : _Safe_iterator_base(__x, _M_constant()), _M_current(__x._M_current)
      {
	_GLIBCXX_DEBUG_VERIFY(!__x._M_singular(),
			      _M_message(__msg_init_copy_singular)
			      ._M_iterator(*this, "this")
			      ._M_iterator(__x, "other"));
      }

      /**
       *  @brief Converting constructor from a mutable iterator to a
       *  constant iterator.
       *
       *  @pre @p x is not singular
      */
      template<typename _MutableIterator>
        _Safe_iterator(
          const _Safe_iterator<_MutableIterator,
          typename __gnu_cxx::__enable_if<(std::__are_same<_MutableIterator,
                      typename _Sequence::iterator::_Base_iterator>::__value),
                   _Sequence>::__type>& __x)
	: _Safe_iterator_base(__x, _M_constant()), _M_current(__x.base())
        {
	  _GLIBCXX_DEBUG_VERIFY(!__x._M_singular(),
				_M_message(__msg_init_const_singular)
				._M_iterator(*this, "this")
				._M_iterator(__x, "other"));
	}

      /**
       * @brief Copy assignment.
       * @pre @p x is not singular
       */
      _Safe_iterator&
      operator=(const _Safe_iterator& __x)
      {
	_GLIBCXX_DEBUG_VERIFY(!__x._M_singular(),
			      _M_message(__msg_copy_singular)
			      ._M_iterator(*this, "this")
			      ._M_iterator(__x, "other"));
	_M_current = __x._M_current;
	this->_M_attach(static_cast<_Sequence*>(__x._M_sequence));
	return *this;
      }

      /**
       *  @brief Iterator dereference.
       *  @pre iterator is dereferenceable
       */
      reference
      operator*() const
      {

	_GLIBCXX_DEBUG_VERIFY(this->_M_dereferenceable(),
			      _M_message(__msg_bad_deref)
			      ._M_iterator(*this, "this"));
	return *_M_current;
      }

      /**
       *  @brief Iterator dereference.
       *  @pre iterator is dereferenceable
       *  @todo Make this correct w.r.t. iterators that return proxies
       *  @todo Use addressof() instead of & operator
       */
      pointer
      operator->() const
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_dereferenceable(),
			      _M_message(__msg_bad_deref)
			      ._M_iterator(*this, "this"));
	return &*_M_current;
      }

      // ------ Input iterator requirements ------
      /**
       *  @brief Iterator preincrement
       *  @pre iterator is incrementable
       */
      _Safe_iterator&
      operator++()
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_incrementable(),
			      _M_message(__msg_bad_inc)
			      ._M_iterator(*this, "this"));
	++_M_current;
	return *this;
      }

      /**
       *  @brief Iterator postincrement
       *  @pre iterator is incrementable
       */
      _Safe_iterator
      operator++(int)
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_incrementable(),
			      _M_message(__msg_bad_inc)
			      ._M_iterator(*this, "this"));
	_Safe_iterator __tmp(*this);
	++_M_current;
	return __tmp;
      }

      // ------ Bidirectional iterator requirements ------
      /**
       *  @brief Iterator predecrement
       *  @pre iterator is decrementable
       */
      _Safe_iterator&
      operator--()
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_decrementable(),
			      _M_message(__msg_bad_dec)
			      ._M_iterator(*this, "this"));
	--_M_current;
	return *this;
      }

      /**
       *  @brief Iterator postdecrement
       *  @pre iterator is decrementable
       */
      _Safe_iterator
      operator--(int)
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_decrementable(),
			      _M_message(__msg_bad_dec)
			      ._M_iterator(*this, "this"));
	_Safe_iterator __tmp(*this);
	--_M_current;
	return __tmp;
      }

      // ------ Random access iterator requirements ------
      reference
      operator[](const difference_type& __n) const
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_can_advance(__n)
			      && this->_M_can_advance(__n+1),
			      _M_message(__msg_iter_subscript_oob)
			      ._M_iterator(*this)._M_integer(__n));

	return _M_current[__n];
      }

      _Safe_iterator&
      operator+=(const difference_type& __n)
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_can_advance(__n),
			      _M_message(__msg_advance_oob)
			      ._M_iterator(*this)._M_integer(__n));
	_M_current += __n;
	return *this;
      }

      _Safe_iterator
      operator+(const difference_type& __n) const
      {
	_Safe_iterator __tmp(*this);
	__tmp += __n;
	return __tmp;
      }

      _Safe_iterator&
      operator-=(const difference_type& __n)
      {
	_GLIBCXX_DEBUG_VERIFY(this->_M_can_advance(-__n),
			      _M_message(__msg_retreat_oob)
			      ._M_iterator(*this)._M_integer(__n));
	_M_current += -__n;
	return *this;
      }

      _Safe_iterator
      operator-(const difference_type& __n) const
      {
	_Safe_iterator __tmp(*this);
	__tmp -= __n;
	return __tmp;
      }

      // ------ Utilities ------
      /**
       * @brief Return the underlying iterator
       */
      _Iterator
      base() const { return _M_current; }

      /**
       * @brief Conversion to underlying non-debug iterator to allow
       * better interaction with non-debug containers.
       */
      operator _Iterator() const { return _M_current; }

      /** Attach iterator to the given sequence. */
      void
      _M_attach(const _Sequence* __seq)
      {
	_Safe_iterator_base::_M_attach(const_cast<_Sequence*>(__seq),
				       _M_constant());
      }

      /** Likewise, but not thread-safe. */
      void
      _M_attach_single(const _Sequence* __seq)
      {
	_Safe_iterator_base::_M_attach_single(const_cast<_Sequence*>(__seq),
					      _M_constant());
      }

      /** Invalidate the iterator, making it singular. */
      void
      _M_invalidate();

      /** Likewise, but not thread-safe. */
      void
      _M_invalidate_single();

      /// Is the iterator dereferenceable?
      bool
      _M_dereferenceable() const
      { return !this->_M_singular() && !_M_is_end(); }

      /// Is the iterator incrementable?
      bool
      _M_incrementable() const { return this->_M_dereferenceable(); }

      // Is the iterator decrementable?
      bool
      _M_decrementable() const { return !_M_singular() && !_M_is_begin(); }

      // Can we advance the iterator @p __n steps (@p __n may be negative)
      bool
      _M_can_advance(const difference_type& __n) const;

      // Is the iterator range [*this, __rhs) valid?
      template<typename _Other>
        bool
        _M_valid_range(const _Safe_iterator<_Other, _Sequence>& __rhs) const;

      // The sequence this iterator references.
      const _Sequence*
      _M_get_sequence() const
      { return static_cast<const _Sequence*>(_M_sequence); }

    /** Determine the distance between two iterators with some known
     *	precision.
    */
    template<typename _Iterator1, typename _Iterator2>
      static std::pair<difference_type, _Distance_precision>
      _M_get_distance(const _Iterator1& __lhs, const _Iterator2& __rhs)
      {
        typedef typename std::iterator_traits<_Iterator1>::iterator_category
	  _Category;
        return _M_get_distance(__lhs, __rhs, _Category());
      }

    template<typename _Iterator1, typename _Iterator2>
      static std::pair<difference_type, _Distance_precision>
      _M_get_distance(const _Iterator1& __lhs, const _Iterator2& __rhs,
		      std::random_access_iterator_tag)
      {
        return std::make_pair(__rhs.base() - __lhs.base(), __dp_exact);
      }

    template<typename _Iterator1, typename _Iterator2>
      static std::pair<difference_type, _Distance_precision>
      _M_get_distance(const _Iterator1& __lhs, const _Iterator2& __rhs,
		    std::forward_iterator_tag)
      {
        return std::make_pair(__lhs.base() == __rhs.base()? 0 : 1,
			      __dp_equality);
      }

      /// Is this iterator equal to the sequence's begin() iterator?
      bool _M_is_begin() const
      {	return *this == static_cast<const _Sequence*>(_M_sequence)->begin(); }

      /// Is this iterator equal to the sequence's end() iterator?
      bool _M_is_end() const
      {	return *this == static_cast<const _Sequence*>(_M_sequence)->end(); }
    };

  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline bool
    operator==(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	       const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_compare_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_compare_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() == __rhs.base();
    }

  template<typename _Iterator, typename _Sequence>
    inline bool
    operator==(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
               const _Safe_iterator<_Iterator, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_compare_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_compare_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() == __rhs.base();
    }

  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline bool
    operator!=(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	       const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_compare_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_compare_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() != __rhs.base();
    }

  template<typename _Iterator, typename _Sequence>
    inline bool
    operator!=(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
               const _Safe_iterator<_Iterator, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_compare_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_compare_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() != __rhs.base();
    }

  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline bool
    operator<(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	      const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() < __rhs.base();
    }

  template<typename _Iterator, typename _Sequence>
    inline bool
    operator<(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
	      const _Safe_iterator<_Iterator, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() < __rhs.base();
    }

  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline bool
    operator<=(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	       const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() <= __rhs.base();
    }

  template<typename _Iterator, typename _Sequence>
    inline bool
    operator<=(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
               const _Safe_iterator<_Iterator, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() <= __rhs.base();
    }

  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline bool
    operator>(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	      const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() > __rhs.base();
    }

  template<typename _Iterator, typename _Sequence>
    inline bool
    operator>(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
	      const _Safe_iterator<_Iterator, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() > __rhs.base();
    }

  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline bool
    operator>=(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	       const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() >= __rhs.base();
    }

  template<typename _Iterator, typename _Sequence>
    inline bool
    operator>=(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
               const _Safe_iterator<_Iterator, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_iter_order_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_order_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() >= __rhs.base();
    }

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // According to the resolution of DR179 not only the various comparison
  // operators but also operator- must accept mixed iterator/const_iterator
  // parameters.
  template<typename _IteratorL, typename _IteratorR, typename _Sequence>
    inline typename _Safe_iterator<_IteratorL, _Sequence>::difference_type
    operator-(const _Safe_iterator<_IteratorL, _Sequence>& __lhs,
	      const _Safe_iterator<_IteratorR, _Sequence>& __rhs)
    {
      _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			    _M_message(__msg_distance_bad)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			    _M_message(__msg_distance_different)
			    ._M_iterator(__lhs, "lhs")
			    ._M_iterator(__rhs, "rhs"));
      return __lhs.base() - __rhs.base();
    }

   template<typename _Iterator, typename _Sequence>
     inline typename _Safe_iterator<_Iterator, _Sequence>::difference_type
     operator-(const _Safe_iterator<_Iterator, _Sequence>& __lhs,
	       const _Safe_iterator<_Iterator, _Sequence>& __rhs)
     {
       _GLIBCXX_DEBUG_VERIFY(! __lhs._M_singular() && ! __rhs._M_singular(),
			     _M_message(__msg_distance_bad)
			     ._M_iterator(__lhs, "lhs")
			     ._M_iterator(__rhs, "rhs"));
       _GLIBCXX_DEBUG_VERIFY(__lhs._M_can_compare(__rhs),
			     _M_message(__msg_distance_different)
			     ._M_iterator(__lhs, "lhs")
			     ._M_iterator(__rhs, "rhs"));
       return __lhs.base() - __rhs.base();
     }

  template<typename _Iterator, typename _Sequence>
    inline _Safe_iterator<_Iterator, _Sequence>
    operator+(typename _Safe_iterator<_Iterator,_Sequence>::difference_type __n,
	      const _Safe_iterator<_Iterator, _Sequence>& __i)
    { return __i + __n; }
} // namespace __gnu_debug

#ifndef _GLIBCXX_EXPORT_TEMPLATE
#  include <debug/safe_iterator.tcc>
#endif

#endif
