// Components for manipulating sequences of characters -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002
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

//
// ISO C++ 14882: 21  Strings library
//

// Written by Jason Merrill based upon the specification by Takanori Adachi
// in ANSI X3J16/94-0013R2.  Rewritten by Nathan Myers.

#include <string>

// Instantiation configuration.
#ifndef C
# define C char
#endif

namespace std 
{
  typedef basic_string<C> S;

  template class basic_string<C>;
  template S operator+(const C*, const S&);
  template S operator+(C, const S&);
  template S operator+(const S&, const S&);
} // namespace std

namespace __gnu_cxx
{
  using std::S;
  template bool operator==(const S::iterator&, const S::iterator&);
  template bool operator==(const S::const_iterator&, const S::const_iterator&);
}

namespace std
{
  // Only one template keyword allowed here. 
  // See core issue #46 (NAD)
  // http://anubis.dkuug.dk/jtc1/sc22/wg21/docs/cwg_closed.html#46
  template
    S::basic_string(C*, C*, const allocator<C>&);

  template
    S::basic_string(const C*, const C*, const allocator<C>&);

  template 
    S::basic_string(S::iterator, S::iterator, const allocator<C>&);

  template 
    S::basic_string(S::const_iterator, S::const_iterator, const allocator<C>&);

  template 
    S& 
    S::_M_replace(S::iterator, S::iterator, S::iterator, S::iterator, 
		  input_iterator_tag);

  template 
    S& 
    S::_M_replace(S::iterator, S::iterator, S::const_iterator, 
		  S::const_iterator, input_iterator_tag);

  template
    S&
    S::_M_replace(S::iterator, S::iterator, C*, C*, input_iterator_tag); 

  template
    S&
    S::_M_replace(S::iterator, S::iterator, const C*, const C*, 
		  input_iterator_tag);  

  template 
    S& 
    S::_M_replace_safe(S::iterator, S::iterator, S::iterator, S::iterator);

  template 
    S& 
    S::_M_replace_safe(S::iterator, S::iterator, S::const_iterator, 
		  S::const_iterator);

  template
    S&
    S::_M_replace_safe(S::iterator, S::iterator, C*, C*); 

  template
    S&
    S::_M_replace_safe(S::iterator, S::iterator, const C*, const C*);  

  template 
    C* 
    S::_S_construct(S::iterator, S::iterator, 
		    const allocator<C>&, forward_iterator_tag);

  template 
    C* 
    S::_S_construct(S::const_iterator, S::const_iterator, 
		    const allocator<C>&, forward_iterator_tag);

  template
    C*
    S::_S_construct(C*, C*, const allocator<C>&, forward_iterator_tag);

  template
    C*
    S::_S_construct(const C*, const C*, const allocator<C>&,
		    forward_iterator_tag);

  template
    void
    __destroy_aux<S*>(S*, S*, __false_type);
} // namespace std
