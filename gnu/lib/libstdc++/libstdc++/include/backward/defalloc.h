// Backward-compat support -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
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
 */

// Inclusion of this file is DEPRECATED.  This is the original HP
// default allocator.  It is provided only for backward compatibility.
// This file WILL BE REMOVED in a future release.
//
// DO NOT USE THIS FILE unless you have an old container implementation
// that requires an allocator with the HP-style interface.  
//
// Standard-conforming allocators have a very different interface.  The
// standard default allocator is declared in the header <memory>.

#ifndef _CPP_BACKWARD_DEFALLOC_H
#define _CPP_BACKWARD_DEFALLOC_H 1

#include "backward_warning.h"
#include "new.h"
#include <stddef.h>
#include <stdlib.h>
#include <limits.h> 
#include "iostream.h" 
#include "algobase.h"


template <class _Tp>
inline _Tp* allocate(ptrdiff_t __size, _Tp*) {
    set_new_handler(0);
    _Tp* __tmp = (_Tp*)(::operator new((size_t)(__size * sizeof(_Tp))));
    if (__tmp == 0) {
	cerr << "out of memory" << endl; 
	exit(1);
    }
    return __tmp;
}


template <class _Tp>
inline void deallocate(_Tp* __buffer) {
    ::operator delete(__buffer);
}

template <class _Tp>
class allocator {
public:
    typedef _Tp value_type;
    typedef _Tp* pointer;
    typedef const _Tp* const_pointer;
    typedef _Tp& reference;
    typedef const _Tp& const_reference;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    pointer allocate(size_type __n) { 
	return ::allocate((difference_type)__n, (pointer)0);
    }
    void deallocate(pointer __p) { ::deallocate(__p); }
    pointer address(reference __x) { return (pointer)&__x; }
    const_pointer const_address(const_reference __x) { 
	return (const_pointer)&__x; 
    }
    size_type init_page_size() { 
	return max(size_type(1), size_type(4096/sizeof(_Tp))); 
    }
    size_type max_size() const { 
	return max(size_type(1), size_type(UINT_MAX/sizeof(_Tp))); 
    }
};

class allocator<void> {
public:
    typedef void* pointer;
};



#endif /* _CPP_BACKWARD_DEFALLOC_H */
