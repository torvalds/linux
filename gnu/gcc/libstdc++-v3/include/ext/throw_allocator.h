// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
// MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/** @file ext/vstring.h
 *  This file is a GNU extension to the Standard C++ Library.
 *
 *  Contains an exception-throwing allocator, useful for testing
 *  exception safety. In addition, allocation addresses are stored and
 *  sanity checked.
 */

/**
 * @file throw_allocator.h 
 */

#ifndef _THROW_ALLOCATOR_H
#define _THROW_ALLOCATOR_H 1

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <tr1/random>
#include <bits/functexcept.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  class twister_rand_gen
  {
  public:
    twister_rand_gen(unsigned int seed = 
		     static_cast<unsigned int>(std::time(0)));
    
    void
    init(unsigned int);
    
    double
    get_prob();
    
  private:
    std::tr1::mt19937 _M_generator;
  };

  struct forced_exception_error : public std::exception
  { };

  // Substitute for concurrence_error object in the case of -fno-exceptions.
  inline void
  __throw_forced_exception_error()
  {
#if __EXCEPTIONS
    throw forced_exception_error();
#else
    __builtin_abort();
#endif
  }

  class throw_allocator_base
  {
  public:
    void
    init(unsigned long seed);

    static void
    set_throw_prob(double throw_prob);

    static double
    get_throw_prob();

    static void
    set_label(size_t l);

    static bool
    empty();

    struct group_throw_prob_adjustor
    {
      group_throw_prob_adjustor(size_t size) 
      : _M_throw_prob_orig(_S_throw_prob)
      {
	_S_throw_prob =
	  1 - ::pow(double(1 - _S_throw_prob), double(0.5 / (size + 1)));
      }

      ~group_throw_prob_adjustor()
      { _S_throw_prob = _M_throw_prob_orig; }

    private:
      const double _M_throw_prob_orig;
    };

    struct zero_throw_prob_adjustor
    {
      zero_throw_prob_adjustor() : _M_throw_prob_orig(_S_throw_prob)
      { _S_throw_prob = 0; }

      ~zero_throw_prob_adjustor()
      { _S_throw_prob = _M_throw_prob_orig; }

    private:
      const double _M_throw_prob_orig;
    };

  protected:
    static void
    insert(void*, size_t);

    static void
    erase(void*, size_t);

    static void
    throw_conditionally();

    // See if a particular address and size has been allocated by this
    // allocator.
    static void
    check_allocated(void*, size_t);

    // See if a given label has been allocated by this allocator.
    static void
    check_allocated(size_t);

  private:
    typedef std::pair<size_t, size_t> 		alloc_data_type;
    typedef std::map<void*, alloc_data_type> 	map_type;
    typedef map_type::value_type 		entry_type;
    typedef map_type::const_iterator 		const_iterator;
    typedef map_type::const_reference 		const_reference;

    friend std::ostream& 
    operator<<(std::ostream&, const throw_allocator_base&);

    static entry_type
    make_entry(void*, size_t);

    static void
    print_to_string(std::string&);

    static void
    print_to_string(std::string&, const_reference);

    static twister_rand_gen 	_S_g;
    static map_type 		_S_map;
    static double 		_S_throw_prob;
    static size_t 		_S_label;
  };


  template<typename T>
    class throw_allocator : public throw_allocator_base
    {
    public:
      typedef size_t 				size_type;
      typedef ptrdiff_t 			difference_type;
      typedef T 				value_type;
      typedef value_type* 			pointer;
      typedef const value_type* 		const_pointer;
      typedef value_type& 			reference;
      typedef const value_type& 		const_reference;


      template<typename U>
      struct rebind
      {
        typedef throw_allocator<U> other;
      };

      throw_allocator() throw() { }

      throw_allocator(const throw_allocator&) throw() { }

      template<typename U>
      throw_allocator(const throw_allocator<U>&) throw() { }

      ~throw_allocator() throw() { }

      size_type
      max_size() const throw()
      { return std::allocator<value_type>().max_size(); }

      pointer
      allocate(size_type num, std::allocator<void>::const_pointer hint = 0)
      {
	throw_conditionally();
	value_type* const a = std::allocator<value_type>().allocate(num, hint);
	insert(a, sizeof(value_type) * num);
	return a;
      }

      void
      construct(pointer p, const T& val)
      { return std::allocator<value_type>().construct(p, val); }

      void
      destroy(pointer p)
      { std::allocator<value_type>().destroy(p); }

      void
      deallocate(pointer p, size_type num)
      {
	erase(p, sizeof(value_type) * num);
	std::allocator<value_type>().deallocate(p, num);
      }

      void
      check_allocated(pointer p, size_type num)
      { throw_allocator_base::check_allocated(p, sizeof(value_type) * num); }

      void
      check_allocated(size_type label)
      { throw_allocator_base::check_allocated(label); }
    };

  template<typename T>
    inline bool
    operator==(const throw_allocator<T>&, const throw_allocator<T>&)
    { return true; }

  template<typename T>
    inline bool
    operator!=(const throw_allocator<T>&, const throw_allocator<T>&)
    { return false; }

  std::ostream& 
  operator<<(std::ostream& os, const throw_allocator_base& alloc)
  {
    std::string error;
    throw_allocator_base::print_to_string(error);
    os << error;
    return os;
  }

  // XXX Should be in .cc.
  twister_rand_gen::
  twister_rand_gen(unsigned int seed) : _M_generator(seed)  { }

  void
  twister_rand_gen::
  init(unsigned int seed)
  { _M_generator.seed(seed); }

  double
  twister_rand_gen::
  get_prob()
  {
    const double eng_min = _M_generator.min();
    const double eng_range =
      static_cast<const double>(_M_generator.max() - eng_min);

    const double eng_res =
      static_cast<const double>(_M_generator() - eng_min);

    const double ret = eng_res / eng_range;
    _GLIBCXX_DEBUG_ASSERT(ret >= 0 && ret <= 1);
    return ret;
  }

  twister_rand_gen throw_allocator_base::_S_g;

  throw_allocator_base::map_type
  throw_allocator_base::_S_map;

  double throw_allocator_base::_S_throw_prob;

  size_t throw_allocator_base::_S_label = 0;

  throw_allocator_base::entry_type
  throw_allocator_base::make_entry(void* p, size_t size)
  { return std::make_pair(p, alloc_data_type(_S_label, size)); }

  void
  throw_allocator_base::init(unsigned long seed)
  { _S_g.init(seed); }

  void
  throw_allocator_base::set_throw_prob(double throw_prob)
  { _S_throw_prob = throw_prob; }

  double
  throw_allocator_base::get_throw_prob()
  { return _S_throw_prob; }

  void
  throw_allocator_base::set_label(size_t l)
  { _S_label = l; }

  void
  throw_allocator_base::insert(void* p, size_t size)
  {
    const_iterator found_it = _S_map.find(p);
    if (found_it != _S_map.end())
      {
	std::string error("throw_allocator_base::insert");
	error += "double insert!";
	error += '\n';
	print_to_string(error, make_entry(p, size));
	print_to_string(error, *found_it);
	std::__throw_logic_error(error.c_str());
      }
    _S_map.insert(make_entry(p, size));
  }

  bool
  throw_allocator_base::empty()
  { return _S_map.empty(); }

  void
  throw_allocator_base::erase(void* p, size_t size)
  {
    check_allocated(p, size);
    _S_map.erase(p);
  }

  void
  throw_allocator_base::check_allocated(void* p, size_t size)
  {
    const_iterator found_it = _S_map.find(p);
    if (found_it == _S_map.end())
      {
	std::string error("throw_allocator_base::check_allocated by value ");
	error += "null erase!";
	error += '\n';
	print_to_string(error, make_entry(p, size));
	std::__throw_logic_error(error.c_str());
      }

    if (found_it->second.second != size)
      {
	std::string error("throw_allocator_base::check_allocated by value ");
	error += "wrong-size erase!";
	error += '\n';
	print_to_string(error, make_entry(p, size));
	print_to_string(error, *found_it);
	std::__throw_logic_error(error.c_str());
      }
  }

  void
  throw_allocator_base::check_allocated(size_t label)
  {
    std::string found;
    const_iterator it = _S_map.begin();
    while (it != _S_map.end())
      {
	if (it->second.first == label)
	  print_to_string(found, *it);
	++it;
      }

    if (!found.empty())
      {
	std::string error("throw_allocator_base::check_allocated by label ");
	error += '\n';
	error += found;
	std::__throw_logic_error(error.c_str());
      }	
  }

  void
  throw_allocator_base::throw_conditionally()
  {
    if (_S_g.get_prob() < _S_throw_prob)
      __throw_forced_exception_error();
  }

  void
  throw_allocator_base::print_to_string(std::string& s)
  {
    const_iterator begin = throw_allocator_base::_S_map.begin();
    const_iterator end = throw_allocator_base::_S_map.end();
    for (; begin != end; ++begin)
      print_to_string(s, *begin);
  }

  void
  throw_allocator_base::print_to_string(std::string& s, const_reference ref)
  {
    char buf[40];
    const char tab('\t');
    s += "address: ";
    snprintf(buf, sizeof buf, "%p", ref.first);
    s += buf;
    s += tab;
    s += "label: ";
    snprintf(buf, sizeof buf, "%u", ref.second.first);
    s += buf;
    s += tab;
    s += "size: ";
    snprintf(buf, sizeof buf, "%u", ref.second.second);
    s += buf;
    s += '\n';
  }

_GLIBCXX_END_NAMESPACE

#endif 
