// List implementation -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996,1997
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file stl_list.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_LIST_H
#define __GLIBCPP_INTERNAL_LIST_H

#include <bits/concept_check.h>

namespace std
{
  // Supporting structures are split into common and templated types; the
  // latter publicly inherits from the former in an effort to reduce code
  // duplication.  This results in some "needless" static_cast'ing later on,
  // but it's all safe downcasting.
  
  /// @if maint Common part of a node in the %list.  @endif
  struct _List_node_base
  {
    _List_node_base* _M_next;   ///< Self-explanatory
    _List_node_base* _M_prev;   ///< Self-explanatory
  };
  
  /// @if maint An actual node in the %list.  @endif
  template<typename _Tp>
    struct _List_node : public _List_node_base
  {
    _Tp _M_data;                ///< User's data.
  };
  
  
  /**
   *  @if maint
   *  @brief Common part of a list::iterator.
   *
   *  A simple type to walk a doubly-linked list.  All operations here should
   *  be self-explanatory after taking any decent introductory data structures
   *  course.
   *  @endif
  */
  struct _List_iterator_base
  {
    typedef size_t                        size_type;
    typedef ptrdiff_t                     difference_type;
    typedef bidirectional_iterator_tag    iterator_category;
  
    /// The only member points to the %list element.
    _List_node_base* _M_node;
  
    _List_iterator_base(_List_node_base* __x)
    : _M_node(__x)
    { }
  
    _List_iterator_base()
    { }
  
    /// Walk the %list forward.
    void
    _M_incr()
    { _M_node = _M_node->_M_next; }
  
    /// Walk the %list backward.
    void
    _M_decr()
    { _M_node = _M_node->_M_prev; }
  
    bool
    operator==(const _List_iterator_base& __x) const
    { return _M_node == __x._M_node; }
  
    bool
    operator!=(const _List_iterator_base& __x) const
    { return _M_node != __x._M_node; }
  };
  
  /**
   *  @brief A list::iterator.
   *
   *  In addition to being used externally, a list holds one of these
   *  internally, pointing to the sequence of data.
   *
   *  @if maint
   *  All the functions are op overloads.
   *  @endif
  */
  template<typename _Tp, typename _Ref, typename _Ptr>
    struct _List_iterator : public _List_iterator_base
  {
    typedef _List_iterator<_Tp,_Tp&,_Tp*>             iterator;
    typedef _List_iterator<_Tp,const _Tp&,const _Tp*> const_iterator;
    typedef _List_iterator<_Tp,_Ref,_Ptr>             _Self;
  
    typedef _Tp                                       value_type;
    typedef _Ptr                                      pointer;
    typedef _Ref                                      reference;
    typedef _List_node<_Tp>                           _Node;
  
    _List_iterator(_Node* __x)
    : _List_iterator_base(__x)
    { }
  
    _List_iterator()
    { }
  
    _List_iterator(const iterator& __x)
    : _List_iterator_base(__x._M_node)
    { }
  
    reference
    operator*() const
    { return static_cast<_Node*>(_M_node)->_M_data; }
    // Must downcast from List_node_base to _List_node to get to _M_data.
  
    pointer
    operator->() const
    { return &(operator*()); }
  
    _Self&
    operator++()
    {
      this->_M_incr();
      return *this;
    }
  
    _Self
    operator++(int)
    {
      _Self __tmp = *this;
      this->_M_incr();
      return __tmp;
    }
  
    _Self&
    operator--()
    {
      this->_M_decr();
      return *this;
    }
  
    _Self
    operator--(int)
    {
      _Self __tmp = *this;
      this->_M_decr();
      return __tmp;
    }
  };
  
  
  /// @if maint Primary default version.  @endif
  /**
   *  @if maint
   *  See bits/stl_deque.h's _Deque_alloc_base for an explanation.
   *  @endif
  */
  template<typename _Tp, typename _Allocator, bool _IsStatic>
    class _List_alloc_base
  {
  public:
    typedef typename _Alloc_traits<_Tp, _Allocator>::allocator_type
            allocator_type;
  
    allocator_type
    get_allocator() const { return _M_node_allocator; }
  
    _List_alloc_base(const allocator_type& __a)
    : _M_node_allocator(__a)
    { }
  
  protected:
    _List_node<_Tp>*
    _M_get_node()
    { return _M_node_allocator.allocate(1); }
  
    void
    _M_put_node(_List_node<_Tp>* __p)
    { _M_node_allocator.deallocate(__p, 1); }
  
    // NOTA BENE
    // The stored instance is not actually of "allocator_type"'s type.  Instead
    // we rebind the type to Allocator<List_node<Tp>>, which according to
    // [20.1.5]/4 should probably be the same.  List_node<Tp> is not the same
    // size as Tp (it's two pointers larger), and specializations on Tp may go
    // unused because List_node<Tp> is being bound instead.
    //
    // We put this to the test in get_allocator above; if the two types are
    // actually different, there had better be a conversion between them.
    //
    // None of the predefined allocators shipped with the library (as of 3.1)
    // use this instantiation anyhow; they're all instanceless.
    typename _Alloc_traits<_List_node<_Tp>, _Allocator>::allocator_type
             _M_node_allocator;
  
    _List_node<_Tp>* _M_node;
  };
  
  /// @if maint Specialization for instanceless allocators.  @endif
  template<typename _Tp, typename _Allocator>
    class _List_alloc_base<_Tp, _Allocator, true>
  {
  public:
    typedef typename _Alloc_traits<_Tp, _Allocator>::allocator_type
            allocator_type;
  
    allocator_type
    get_allocator() const { return allocator_type(); }
  
    _List_alloc_base(const allocator_type&)
    { }
  
  protected:
    // See comment in primary template class about why this is safe for the
    // standard predefined classes.
    typedef typename _Alloc_traits<_List_node<_Tp>, _Allocator>::_Alloc_type
            _Alloc_type;
  
    _List_node<_Tp>*
    _M_get_node()
    { return _Alloc_type::allocate(1); }
  
    void
    _M_put_node(_List_node<_Tp>* __p)
    { _Alloc_type::deallocate(__p, 1); }
  
    _List_node<_Tp>* _M_node;
  };
  
  
  /**
   *  @if maint
   *  See bits/stl_deque.h's _Deque_base for an explanation.
   *  @endif
  */
  template <typename _Tp, typename _Alloc>
    class _List_base
    : public _List_alloc_base<_Tp, _Alloc,
                              _Alloc_traits<_Tp, _Alloc>::_S_instanceless>
  {
  public:
    typedef _List_alloc_base<_Tp, _Alloc,
                             _Alloc_traits<_Tp, _Alloc>::_S_instanceless>
            _Base;
    typedef typename _Base::allocator_type allocator_type;
  
    _List_base(const allocator_type& __a)
    : _Base(__a)
    {
      _M_node = _M_get_node();
      _M_node->_M_next = _M_node;
      _M_node->_M_prev = _M_node;
    }
  
    // This is what actually destroys the list.
    ~_List_base()
    {
      __clear();
      _M_put_node(_M_node);
    }
  
    void
    __clear();
  };
  
  
  /**
   *  @brief  A standard container with linear time access to elements, and
   *  fixed time insertion/deletion at any point in the sequence.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  Meets the requirements of a <a href="tables.html#65">container</a>, a
   *  <a href="tables.html#66">reversible container</a>, and a
   *  <a href="tables.html#67">sequence</a>, including the
   *  <a href="tables.html#68">optional sequence requirements</a> with the
   *  %exception of @c at and @c operator[].
   *
   *  This is a @e doubly @e linked %list.  Traversal up and down the %list
   *  requires linear time, but adding and removing elements (or @e nodes) is
   *  done in constant time, regardless of where the change takes place.
   *  Unlike std::vector and std::deque, random-access iterators are not
   *  provided, so subscripting ( @c [] ) access is not allowed.  For algorithms
   *  which only need sequential access, this lack makes no difference.
   *
   *  Also unlike the other standard containers, std::list provides specialized 
   *  algorithms %unique to linked lists, such as splicing, sorting, and
   *  in-place reversal.
   *
   *  @if maint
   *  A couple points on memory allocation for list<Tp>:
   *
   *  First, we never actually allocate a Tp, we allocate List_node<Tp>'s
   *  and trust [20.1.5]/4 to DTRT.  This is to ensure that after elements from
   *  %list<X,Alloc1> are spliced into %list<X,Alloc2>, destroying the memory of
   *  the second %list is a valid operation, i.e., Alloc1 giveth and Alloc2
   *  taketh away.
   *
   *  Second, a %list conceptually represented as
   *  @code
   *    A <---> B <---> C <---> D
   *  @endcode
   *  is actually circular; a link exists between A and D.  The %list class
   *  holds (as its only data member) a private list::iterator pointing to
   *  @e D, not to @e A!  To get to the head of the %list, we start at the tail
   *  and move forward by one.  When this member iterator's next/previous
   *  pointers refer to itself, the %list is %empty.
   *  @endif
  */
  template<typename _Tp, typename _Alloc = allocator<_Tp> >
    class list : protected _List_base<_Tp, _Alloc>
  {
    // concept requirements
    __glibcpp_class_requires(_Tp, _SGIAssignableConcept)
  
    typedef _List_base<_Tp, _Alloc>                       _Base;
  
  public:
    typedef _Tp                                           value_type;
    typedef value_type*                                   pointer;
    typedef const value_type*                             const_pointer;
    typedef _List_iterator<_Tp,_Tp&,_Tp*>                 iterator;
    typedef _List_iterator<_Tp,const _Tp&,const _Tp*>     const_iterator;
    typedef std::reverse_iterator<const_iterator>     const_reverse_iterator;
    typedef std::reverse_iterator<iterator>                 reverse_iterator;
    typedef value_type&                                   reference;
    typedef const value_type&                             const_reference;
    typedef size_t                                        size_type;
    typedef ptrdiff_t                                     difference_type;
    typedef typename _Base::allocator_type                allocator_type;
  
  protected:
    // Note that pointers-to-_Node's can be ctor-converted to iterator types.
    typedef _List_node<_Tp>                               _Node;
  
    /** @if maint
     *  One data member plus two memory-handling functions.  If the _Alloc
     *  type requires separate instances, then one of those will also be
     *  included, accumulated from the topmost parent.
     *  @endif
    */
    using _Base::_M_node;
    using _Base::_M_put_node;
    using _Base::_M_get_node;
  
    /**
     *  @if maint
     *  @param  x  An instance of user data.
     *
     *  Allocates space for a new node and constructs a copy of @a x in it.
     *  @endif
    */
    _Node*
    _M_create_node(const value_type& __x)
    {
      _Node* __p = _M_get_node();
      try {
        _Construct(&__p->_M_data, __x);
      }
      catch(...)
      {
        _M_put_node(__p);
        __throw_exception_again;
      }
      return __p;
    }
  
    /**
     *  @if maint
     *  Allocates space for a new node and default-constructs a new instance
     *  of @c value_type in it.
     *  @endif
    */
    _Node*
    _M_create_node()
    {
      _Node* __p = _M_get_node();
      try {
        _Construct(&__p->_M_data);
      }
      catch(...)
      {
        _M_put_node(__p);
        __throw_exception_again;
      }
      return __p;
    }
  
  public:
    // [23.2.2.1] construct/copy/destroy
    // (assign() and get_allocator() are also listed in this section)
    /**
     *  @brief  Default constructor creates no elements.
    */
    explicit
    list(const allocator_type& __a = allocator_type())
    : _Base(__a) { }
  
    /**
     *  @brief  Create a %list with copies of an exemplar element.
     *  @param  n  The number of elements to initially create.
     *  @param  value  An element to copy.
     * 
     *  This constructor fills the %list with @a n copies of @a value.
    */
    list(size_type __n, const value_type& __value,
         const allocator_type& __a = allocator_type())
      : _Base(__a)
      { this->insert(begin(), __n, __value); }
  
    /**
     *  @brief  Create a %list with default elements.
     *  @param  n  The number of elements to initially create.
     * 
     *  This constructor fills the %list with @a n copies of a
     *  default-constructed element.
    */
    explicit
    list(size_type __n)
      : _Base(allocator_type())
      { this->insert(begin(), __n, value_type()); }
  
    /**
     *  @brief  %List copy constructor.
     *  @param  x  A %list of identical element and allocator types.
     * 
     *  The newly-created %list uses a copy of the allocation object used
     *  by @a x.
    */
    list(const list& __x)
      : _Base(__x.get_allocator())
      { this->insert(begin(), __x.begin(), __x.end()); }
  
    /**
     *  @brief  Builds a %list from a range.
     *  @param  first  An input iterator.
     *  @param  last  An input iterator.
     * 
     *  Create a %list consisting of copies of the elements from [first,last).
     *  This is linear in N (where N is distance(first,last)).
     *
     *  @if maint
     *  We don't need any dispatching tricks here, because insert does all of
     *  that anyway.
     *  @endif
    */
    template<typename _InputIterator>
      list(_InputIterator __first, _InputIterator __last,
           const allocator_type& __a = allocator_type())
      : _Base(__a)
      { this->insert(begin(), __first, __last); }
  
    /**
     *  The dtor only erases the elements, and note that if the elements
     *  themselves are pointers, the pointed-to memory is not touched in any
     *  way.  Managing the pointer is the user's responsibilty.
    */
    ~list() { }
  
    /**
     *  @brief  %List assignment operator.
     *  @param  x  A %list of identical element and allocator types.
     * 
     *  All the elements of @a x are copied, but unlike the copy constructor,
     *  the allocator object is not copied.
    */
    list&
    operator=(const list& __x);
  
    /**
     *  @brief  Assigns a given value to a %list.
     *  @param  n  Number of elements to be assigned.
     *  @param  val  Value to be assigned.
     *
     *  This function fills a %list with @a n copies of the given value.
     *  Note that the assignment completely changes the %list and that the
     *  resulting %list's size is the same as the number of elements assigned.
     *  Old data may be lost.
    */
    void
    assign(size_type __n, const value_type& __val) { _M_fill_assign(__n, __val); }
  
    /**
     *  @brief  Assigns a range to a %list.
     *  @param  first  An input iterator.
     *  @param  last   An input iterator.
     *
     *  This function fills a %list with copies of the elements in the
     *  range [first,last).
     *
     *  Note that the assignment completely changes the %list and that the
     *  resulting %list's size is the same as the number of elements assigned.
     *  Old data may be lost.
    */
    template<typename _InputIterator>
      void
      assign(_InputIterator __first, _InputIterator __last)
      {
        // Check whether it's an integral type.  If so, it's not an iterator.
        typedef typename _Is_integer<_InputIterator>::_Integral _Integral;
        _M_assign_dispatch(__first, __last, _Integral());
      }
  
    /// Get a copy of the memory allocation object.
    allocator_type
    get_allocator() const { return _Base::get_allocator(); }
  
    // iterators
    /**
     *  Returns a read/write iterator that points to the first element in the
     *  %list.  Iteration is done in ordinary element order.
    */
    iterator
    begin() { return static_cast<_Node*>(_M_node->_M_next); }
  
    /**
     *  Returns a read-only (constant) iterator that points to the first element
     *  in the %list.  Iteration is done in ordinary element order.
    */
    const_iterator
    begin() const { return static_cast<_Node*>(_M_node->_M_next); }
  
    /**
     *  Returns a read/write iterator that points one past the last element in
     *  the %list.  Iteration is done in ordinary element order.
    */
    iterator
    end() { return _M_node; }
  
    /**
     *  Returns a read-only (constant) iterator that points one past the last
     *  element in the %list.  Iteration is done in ordinary element order.
    */
    const_iterator
    end() const { return _M_node; }
  
    /**
     *  Returns a read/write reverse iterator that points to the last element in
     *  the %list.  Iteration is done in reverse element order.
    */
    reverse_iterator
    rbegin() { return reverse_iterator(end()); }
  
    /**
     *  Returns a read-only (constant) reverse iterator that points to the last
     *  element in the %list.  Iteration is done in reverse element order.
    */
    const_reverse_iterator
    rbegin() const { return const_reverse_iterator(end()); }
  
    /**
     *  Returns a read/write reverse iterator that points to one before the
     *  first element in the %list.  Iteration is done in reverse element
     *  order.
    */
    reverse_iterator
    rend() { return reverse_iterator(begin()); }
  
    /**
     *  Returns a read-only (constant) reverse iterator that points to one
     *  before the first element in the %list.  Iteration is done in reverse
     *  element order.
    */
    const_reverse_iterator
    rend() const
    { return const_reverse_iterator(begin()); }
  
    // [23.2.2.2] capacity
    /**
     *  Returns true if the %list is empty.  (Thus begin() would equal end().)
    */
    bool
    empty() const { return _M_node->_M_next == _M_node; }
  
    /**  Returns the number of elements in the %list.  */
    size_type
    size() const { return distance(begin(), end()); }
  
    /**  Returns the size() of the largest possible %list.  */
    size_type
    max_size() const { return size_type(-1); }
  
    /**
     *  @brief  Resizes the %list to the specified number of elements.
     *  @param  new_size  Number of elements the %list should contain.
     *  @param  x  Data with which new elements should be populated.
     *
     *  This function will %resize the %list to the specified number of
     *  elements.  If the number is smaller than the %list's current size the
     *  %list is truncated, otherwise the %list is extended and new elements
     *  are populated with given data.
    */
    void
    resize(size_type __new_size, const value_type& __x);
  
    /**
     *  @brief  Resizes the %list to the specified number of elements.
     *  @param  new_size  Number of elements the %list should contain.
     *
     *  This function will resize the %list to the specified number of
     *  elements.  If the number is smaller than the %list's current size the
     *  %list is truncated, otherwise the %list is extended and new elements
     *  are default-constructed.
    */
    void
    resize(size_type __new_size) { this->resize(__new_size, value_type()); }
  
    // element access
    /**
     *  Returns a read/write reference to the data at the first element of the
     *  %list.
    */
    reference
    front() { return *begin(); }
  
    /**
     *  Returns a read-only (constant) reference to the data at the first
     *  element of the %list.
    */
    const_reference
    front() const { return *begin(); }
  
    /**
     *  Returns a read/write reference to the data at the last element of the
     *  %list.
    */
    reference
    back() { return *(--end()); }
  
    /**
     *  Returns a read-only (constant) reference to the data at the last
     *  element of the %list.
    */
    const_reference
    back() const { return *(--end()); }
  
    // [23.2.2.3] modifiers
    /**
     *  @brief  Add data to the front of the %list.
     *  @param  x  Data to be added.
     *
     *  This is a typical stack operation.  The function creates an element at
     *  the front of the %list and assigns the given data to it.  Due to the
     *  nature of a %list this operation can be done in constant time, and
     *  does not invalidate iterators and references.
    */
    void
    push_front(const value_type& __x) { this->insert(begin(), __x); }
  
  #ifdef _GLIBCPP_DEPRECATED
    /**
     *  @brief  Add data to the front of the %list.
     *
     *  This is a typical stack operation.  The function creates a
     *  default-constructed element at the front of the %list.  Due to the
     *  nature of a %list this operation can be done in constant time.  You
     *  should consider using push_front(value_type()) instead.
     *
     *  @note This was deprecated in 3.2 and will be removed in 3.4.  You must
     *        define @c _GLIBCPP_DEPRECATED to make this visible in 3.2; see
     *        c++config.h.
    */
    void
    push_front() { this->insert(begin(), value_type()); }
  #endif
  
    /**
     *  @brief  Removes first element.
     *
     *  This is a typical stack operation.  It shrinks the %list by one.
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and only invalidates iterators/references to the element being
     *  removed.
     *
     *  Note that no data is returned, and if the first element's data is
     *  needed, it should be retrieved before pop_front() is called.
    */
    void
    pop_front() { this->erase(begin()); }
  
    /**
     *  @brief  Add data to the end of the %list.
     *  @param  x  Data to be added.
     *
     *  This is a typical stack operation.  The function creates an element at
     *  the end of the %list and assigns the given data to it.  Due to the
     *  nature of a %list this operation can be done in constant time, and
     *  does not invalidate iterators and references.
    */
    void
    push_back(const value_type& __x) { this->insert(end(), __x); }
  
  #ifdef _GLIBCPP_DEPRECATED
    /**
     *  @brief  Add data to the end of the %list.
     *
     *  This is a typical stack operation.  The function creates a
     *  default-constructed element at the end of the %list.  Due to the nature
     *  of a %list this operation can be done in constant time.  You should
     *  consider using push_back(value_type()) instead.
     *
     *  @note This was deprecated in 3.2 and will be removed in 3.4.  You must
     *        define @c _GLIBCPP_DEPRECATED to make this visible in 3.2; see
     *        c++config.h.
    */
    void
    push_back() { this->insert(end(), value_type()); }
  #endif
  
    /**
     *  @brief  Removes last element.
     *
     *  This is a typical stack operation.  It shrinks the %list by one.
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and only invalidates iterators/references to the element being
     *  removed.
     *
     *  Note that no data is returned, and if the last element's data is
     *  needed, it should be retrieved before pop_back() is called.
    */
    void
    pop_back()
    {
      iterator __tmp = end();
      this->erase(--__tmp);
    }
  
    /**
     *  @brief  Inserts given value into %list before specified iterator.
     *  @param  position  An iterator into the %list.
     *  @param  x  Data to be inserted.
     *  @return  An iterator that points to the inserted data.
     *
     *  This function will insert a copy of the given value before the specified
     *  location.
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and does not invalidate iterators and references.
    */
    iterator
    insert(iterator __position, const value_type& __x);
  
  #ifdef _GLIBCPP_DEPRECATED
    /**
     *  @brief  Inserts an element into the %list.
     *  @param  position  An iterator into the %list.
     *  @return  An iterator that points to the inserted element.
     *
     *  This function will insert a default-constructed element before the
     *  specified location.  You should consider using
     *  insert(position,value_type()) instead.
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and does not invalidate iterators and references.
     *
     *  @note This was deprecated in 3.2 and will be removed in 3.4.  You must
     *        define @c _GLIBCPP_DEPRECATED to make this visible in 3.2; see
     *        c++config.h.
    */
    iterator
    insert(iterator __position) { return insert(__position, value_type()); }
  #endif
  
    /**
     *  @brief  Inserts a number of copies of given data into the %list.
     *  @param  position  An iterator into the %list.
     *  @param  n  Number of elements to be inserted.
     *  @param  x  Data to be inserted.
     *
     *  This function will insert a specified number of copies of the given data
     *  before the location specified by @a position.
     *
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and does not invalidate iterators and references.
    */
    void
    insert(iterator __pos, size_type __n, const value_type& __x)
    { _M_fill_insert(__pos, __n, __x); }
  
    /**
     *  @brief  Inserts a range into the %list.
     *  @param  pos  An iterator into the %list.
     *  @param  first  An input iterator.
     *  @param  last   An input iterator.
     *
     *  This function will insert copies of the data in the range [first,last)
     *  into the %list before the location specified by @a pos.
     *
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and does not invalidate iterators and references.
    */
    template<typename _InputIterator>
      void
      insert(iterator __pos, _InputIterator __first, _InputIterator __last)
      {
        // Check whether it's an integral type.  If so, it's not an iterator.
        typedef typename _Is_integer<_InputIterator>::_Integral _Integral;
        _M_insert_dispatch(__pos, __first, __last, _Integral());
      }
  
    /**
     *  @brief  Remove element at given position.
     *  @param  position  Iterator pointing to element to be erased.
     *  @return  An iterator pointing to the next element (or end()).
     *
     *  This function will erase the element at the given position and thus
     *  shorten the %list by one.
     *
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and only invalidates iterators/references to the element being
     *  removed.
     *  The user is also cautioned that
     *  this function only erases the element, and that if the element is itself
     *  a pointer, the pointed-to memory is not touched in any way.  Managing
     *  the pointer is the user's responsibilty.
    */
    iterator
    erase(iterator __position);
  
    /**
     *  @brief  Remove a range of elements.
     *  @param  first  Iterator pointing to the first element to be erased.
     *  @param  last  Iterator pointing to one past the last element to be
     *                erased.
     *  @return  An iterator pointing to the element pointed to by @a last
     *           prior to erasing (or end()).
     *
     *  This function will erase the elements in the range [first,last) and
     *  shorten the %list accordingly.
     *
     *  Due to the nature of a %list this operation can be done in constant
     *  time, and only invalidates iterators/references to the element being
     *  removed.
     *  The user is also cautioned that
     *  this function only erases the elements, and that if the elements
     *  themselves are pointers, the pointed-to memory is not touched in any
     *  way.  Managing the pointer is the user's responsibilty.
    */
    iterator
    erase(iterator __first, iterator __last)
    {
      while (__first != __last)
        erase(__first++);
      return __last;
    }
  
    /**
     *  @brief  Swaps data with another %list.
     *  @param  x  A %list of the same element and allocator types.
     *
     *  This exchanges the elements between two lists in constant time.
     *  (It is only swapping a single pointer, so it should be quite fast.)
     *  Note that the global std::swap() function is specialized such that
     *  std::swap(l1,l2) will feed to this function.
    */
    void
    swap(list& __x) { std::swap(_M_node, __x._M_node); }
  
    /**
     *  Erases all the elements.  Note that this function only erases the
     *  elements, and that if the elements themselves are pointers, the
     *  pointed-to memory is not touched in any way.  Managing the pointer is
     *  the user's responsibilty.
    */
    void
    clear() { _Base::__clear(); }
  
    // [23.2.2.4] list operations
    /**
     *  @doctodo
    */
    void
    splice(iterator __position, list& __x)
    {
      if (!__x.empty())
        this->_M_transfer(__position, __x.begin(), __x.end());
    }
  
    /**
     *  @doctodo
    */
    void
    splice(iterator __position, list&, iterator __i)
    {
      iterator __j = __i;
      ++__j;
      if (__position == __i || __position == __j) return;
      this->_M_transfer(__position, __i, __j);
    }
  
    /**
     *  @doctodo
    */
    void
    splice(iterator __position, list&, iterator __first, iterator __last)
    {
      if (__first != __last)
        this->_M_transfer(__position, __first, __last);
    }
  
    /**
     *  @doctodo
    */
    void
    remove(const _Tp& __value);
  
    /**
     *  @doctodo
    */
    template<typename _Predicate>
      void
      remove_if(_Predicate);
  
    /**
     *  @doctodo
    */
    void
    unique();
  
    /**
     *  @doctodo
    */
    template<typename _BinaryPredicate>
      void
      unique(_BinaryPredicate);
  
    /**
     *  @doctodo
    */
    void
    merge(list& __x);
  
    /**
     *  @doctodo
    */
    template<typename _StrictWeakOrdering>
      void
      merge(list&, _StrictWeakOrdering);
  
    /**
     *  @doctodo
    */
    void
    reverse() { __List_base_reverse(this->_M_node); }
  
    /**
     *  @doctodo
    */
    void
    sort();
  
    /**
     *  @doctodo
    */
    template<typename _StrictWeakOrdering>
      void
      sort(_StrictWeakOrdering);
  
  protected:
    // Internal assign functions follow.
  
    // called by the range assign to implement [23.1.1]/9
    template<typename _Integer>
      void
      _M_assign_dispatch(_Integer __n, _Integer __val, __true_type)
      {
        _M_fill_assign(static_cast<size_type>(__n),
                       static_cast<value_type>(__val));
      }
  
    // called by the range assign to implement [23.1.1]/9
    template<typename _InputIter>
      void
      _M_assign_dispatch(_InputIter __first, _InputIter __last, __false_type);
  
    // Called by assign(n,t), and the range assign when it turns out to be the
    // same thing.
    void
    _M_fill_assign(size_type __n, const value_type& __val);
  
  
    // Internal insert functions follow.
  
    // called by the range insert to implement [23.1.1]/9
    template<typename _Integer>
      void
      _M_insert_dispatch(iterator __pos, _Integer __n, _Integer __x,
                         __true_type)
      {
        _M_fill_insert(__pos, static_cast<size_type>(__n),
                       static_cast<value_type>(__x));
      }
  
    // called by the range insert to implement [23.1.1]/9
    template<typename _InputIterator>
      void
      _M_insert_dispatch(iterator __pos,
                         _InputIterator __first, _InputIterator __last,
                         __false_type)
      {
        for ( ; __first != __last; ++__first)
          insert(__pos, *__first);
      }
  
    // Called by insert(p,n,x), and the range insert when it turns out to be
    // the same thing.
    void
    _M_fill_insert(iterator __pos, size_type __n, const value_type& __x)
    {
      for ( ; __n > 0; --__n)
        insert(__pos, __x);
    }
  
  
    // Moves the elements from [first,last) before position.
    void
    _M_transfer(iterator __position, iterator __first, iterator __last)
    {
      if (__position != __last) {
        // Remove [first, last) from its old position.
        __last._M_node->_M_prev->_M_next     = __position._M_node;
        __first._M_node->_M_prev->_M_next    = __last._M_node;
        __position._M_node->_M_prev->_M_next = __first._M_node;
  
        // Splice [first, last) into its new position.
        _List_node_base* __tmp      = __position._M_node->_M_prev;
        __position._M_node->_M_prev = __last._M_node->_M_prev;
        __last._M_node->_M_prev     = __first._M_node->_M_prev;
        __first._M_node->_M_prev    = __tmp;
      }
    }
  };
  
  
  /**
   *  @brief  List equality comparison.
   *  @param  x  A %list.
   *  @param  y  A %list of the same type as @a x.
   *  @return  True iff the size and elements of the lists are equal.
   *
   *  This is an equivalence relation.  It is linear in the size of the
   *  lists.  Lists are considered equivalent if their sizes are equal,
   *  and if corresponding elements compare equal.
  */
  template<typename _Tp, typename _Alloc>
  inline bool
    operator==(const list<_Tp,_Alloc>& __x, const list<_Tp,_Alloc>& __y)
    {
      typedef typename list<_Tp,_Alloc>::const_iterator const_iterator;
      const_iterator __end1 = __x.end();
      const_iterator __end2 = __y.end();
  
      const_iterator __i1 = __x.begin();
      const_iterator __i2 = __y.begin();
      while (__i1 != __end1 && __i2 != __end2 && *__i1 == *__i2) {
        ++__i1;
        ++__i2;
      }
      return __i1 == __end1 && __i2 == __end2;
    }
  
  /**
   *  @brief  List ordering relation.
   *  @param  x  A %list.
   *  @param  y  A %list of the same type as @a x.
   *  @return  True iff @a x is lexographically less than @a y.
   *
   *  This is a total ordering relation.  It is linear in the size of the
   *  lists.  The elements must be comparable with @c <.
   *
   *  See std::lexographical_compare() for how the determination is made.
  */
  template<typename _Tp, typename _Alloc>
    inline bool
    operator<(const list<_Tp,_Alloc>& __x, const list<_Tp,_Alloc>& __y)
    {
      return lexicographical_compare(__x.begin(), __x.end(),
                                     __y.begin(), __y.end());
    }
  
  /// Based on operator==
  template<typename _Tp, typename _Alloc>
    inline bool
    operator!=(const list<_Tp,_Alloc>& __x, const list<_Tp,_Alloc>& __y)
    { return !(__x == __y); }
  
  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator>(const list<_Tp,_Alloc>& __x, const list<_Tp,_Alloc>& __y)
    { return __y < __x; }
  
  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator<=(const list<_Tp,_Alloc>& __x, const list<_Tp,_Alloc>& __y)
    { return !(__y < __x); }
  
  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator>=(const list<_Tp,_Alloc>& __x, const list<_Tp,_Alloc>& __y)
    { return !(__x < __y); }
  
  /// See std::list::swap().
  template<typename _Tp, typename _Alloc>
    inline void
    swap(list<_Tp, _Alloc>& __x, list<_Tp, _Alloc>& __y)
    { __x.swap(__y); }
} // namespace std

#endif /* __GLIBCPP_INTERNAL_LIST_H */
