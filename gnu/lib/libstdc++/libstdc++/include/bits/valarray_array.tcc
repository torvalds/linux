// The template and inlines for the -*- C++ -*- internal _Array helper class.

// Copyright (C) 1997, 1998, 1999 Free Software Foundation, Inc.
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

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@DPTMaths.ENS-Cachan.Fr>

#ifndef _CPP_BITS_VALARRAY_ARRAY_TCC 
#define _CPP_BITS_VALARRAY_ARRAY_TCC 1

namespace std
{

export template<typename _Tp>
void
__valarray_fill (_Array<_Tp> __a, size_t __n, _Array<bool> __m, const _Tp& __t)
{
    _Tp* __p = __a._M_data;
    bool* __ok (__m._M_data);
    for (size_t __i=0; __i<__n; ++__i, ++__ok, ++__p) {
        while (! *__ok) {
            ++__ok;
            ++__p;
        }
        *__p = __t;
    }
}

export template<typename _Tp>
void
__valarray_copy (_Array<_Tp> __a, _Array<bool> __m, _Array<_Tp> __b, size_t __n)
{
    _Tp* __p (__a._M_data);
    bool* __ok (__m._M_data);
    for (_Tp* __q=__b._M_data; __q<__b._M_data+__n; ++__q, ++__ok, ++__p) {
        while (! *__ok) {
            ++__ok;
            ++__p;
        }
        *__q = *__p;
    }
}

export template<typename _Tp>
void
__valarray_copy (_Array<_Tp> __a, size_t __n, _Array<_Tp> __b, _Array<bool> __m)
{
    _Tp* __q (__b._M_data);
    bool* __ok (__m._M_data);
    for (_Tp* __p=__a._M_data; __p<__a._M_data+__n; ++__p, ++__ok, ++__q) {
        while (! *__ok) {
            ++__ok;
            ++__q;
        }
        *__q = *__p;
    }
}

export template<typename _Tp, class _Dom>
void
__valarray_copy (const _Expr<_Dom, _Tp>& __e, size_t __n, _Array<_Tp> __a)
{
    _Tp* __p (__a._M_data);
    for (size_t __i=0; __i<__n; ++__i, ++__p) *__p = __e[__i];
}

export template<typename _Tp, class _Dom>
void
__valarray_copy (const _Expr<_Dom, _Tp>& __e, size_t __n, 
                 _Array<_Tp> __a, size_t __s)
{
    _Tp* __p (__a._M_data);
    for (size_t __i=0; __i<__n; ++__i, __p+=__s) *__p = __e[__i];
}

export template<typename _Tp, class _Dom>
void
__valarray_copy (const _Expr<_Dom, _Tp>& __e, size_t __n, 
                 _Array<_Tp> __a, _Array<size_t> __i)
{
    size_t* __j (__i._M_data);
    for (size_t __k=0; __k<__n; ++__k, ++__j) __a._M_data[*__j] = __e[__k];
}

export template<typename _Tp, class _Dom>
void
__valarray_copy (const _Expr<_Dom, _Tp>& __e, size_t __n, 
                 _Array<_Tp> __a, _Array<bool> __m)
{
    bool* __ok (__m._M_data);
    _Tp* __p (__a._M_data);
    for (size_t __i=0; __i<__n; ++__i, ++__ok, ++__p) {
        while (! *__ok) {
            ++__ok;
            ++__p;
        }
        *__p = __e[__i];
    }
}


export template<typename _Tp, class _Dom>
void
__valarray_copy_construct (const _Expr<_Dom, _Tp>& __e, size_t __n,
                           _Array<_Tp> __a)
{
    _Tp* __p (__a._M_data);
    for (size_t __i=0; __i<__n; ++__i, ++__p) new (__p) _Tp(__e[__i]);
}


export template<typename _Tp>
void
__valarray_copy_construct (_Array<_Tp> __a, _Array<bool> __m,
                           _Array<_Tp> __b, size_t __n)
{
    _Tp* __p (__a._M_data);
    bool* __ok (__m._M_data);
    for (_Tp* __q=__b._M_data; __q<__b._M_data+__n; ++__q, ++__ok, ++__p) {
        while (! *__ok) {
            ++__ok;
            ++__p;
        }
        new (__q) _Tp(*__p);
    }
}




} // std::

#endif /* _CPP_BITS_VALARRAY_ARRAY_TCC */

// Local Variables:
// mode:c++
// End:
