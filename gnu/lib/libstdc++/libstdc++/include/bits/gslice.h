// The template and inlines for the -*- C++ -*- gslice class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
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

/** @file gslice.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_GSLICE_H
#define _CPP_BITS_GSLICE_H 1

#pragma GCC system_header

namespace std {
    
    class gslice
    {
    public:
        gslice ();
        gslice (size_t, const valarray<size_t>&, const valarray<size_t>&);
        // XXX: the IS says the copy-ctor and copy-assignment operators are
        //      synthetized by the compiler but they are just unsuitable
        //      for a ref-counted semantic
        gslice(const gslice&);
        ~gslice();

        // XXX: See the note above.
        gslice& operator= (const gslice&);
        
        size_t           start () const;
        valarray<size_t> size () const;
        valarray<size_t> stride () const;
        
    private:
        struct _Indexer {
            size_t _M_count;
            size_t _M_start;
            valarray<size_t> _M_size;
            valarray<size_t> _M_stride;
            valarray<size_t> _M_index;
            _Indexer(size_t, const valarray<size_t>&,
                     const valarray<size_t>&);
            void _M_increment_use() { ++_M_count; }
            size_t _M_decrement_use() { return --_M_count; }
        };

        _Indexer* _M_index;
        
        template<typename _Tp> friend class valarray;
    };
    
    inline size_t
    gslice::start () const
    { return _M_index ? _M_index->_M_start : 0; }
    
    inline valarray<size_t>
    gslice::size () const
    { return _M_index ? _M_index->_M_size : valarray<size_t>(); }
    
    inline valarray<size_t>
    gslice::stride () const
    { return _M_index ? _M_index->_M_stride : valarray<size_t>(); }
    
    inline gslice::gslice () : _M_index(0) {}

    inline
    gslice::gslice(size_t __o, const valarray<size_t>& __l,
                   const valarray<size_t>& __s)
            : _M_index(new gslice::_Indexer(__o, __l, __s)) {}

    inline
    gslice::gslice(const gslice& __g) : _M_index(__g._M_index)
    { if (_M_index) _M_index->_M_increment_use(); }
    
    inline
    gslice::~gslice()
    { if (_M_index && _M_index->_M_decrement_use() == 0) delete _M_index; }

    inline gslice&
    gslice::operator= (const gslice& __g)
    {
        if (__g._M_index) __g._M_index->_M_increment_use();
        if (_M_index && _M_index->_M_decrement_use() == 0) delete _M_index;
        _M_index = __g._M_index;
        return *this;
    }
            
    
} // std::


#endif /* _CPP_BITS_GSLICE_H */

// Local Variables:
// mode:c++
// End:
