// Stream buffer classes -*- C++ -*-

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

//
// ISO C++ 14882: 27.5  Stream buffers
//

/** @file streambuf
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _CPP_STREAMBUF
#define _CPP_STREAMBUF	1

#pragma GCC system_header

#include <bits/c++config.h>
#include <iosfwd>
#include <cstdio> 	// For SEEK_SET, SEEK_CUR, SEEK_END
#include <bits/localefwd.h>
#include <bits/ios_base.h>

namespace std
{
  /**
   *  @if maint
   *  Does stuff.
   *  @endif
  */
  template<typename _CharT, typename _Traits>
    streamsize
    __copy_streambufs(basic_ios<_CharT, _Traits>& _ios,
		      basic_streambuf<_CharT, _Traits>* __sbin,
		      basic_streambuf<_CharT, _Traits>* __sbout);
  
  /**
   *  @brief  The actual work of input and output (interface).
   *
   *  This is a base class.  Derived stream buffers each control a
   *  pair of character sequences:  one for input, and one for output.
   *
   *  Section [27.5.1] of the standard describes the requirements and
   *  behavior of stream buffer classes.  That section (three paragraphs)
   *  is reproduced here, for simplicity and accuracy.
   *
   *  -# Stream buffers can impose various constraints on the sequences
   *     they control.  Some constraints are:
   *     - The controlled input sequence can be not readable.
   *     - The controlled output sequence can be not writable.
   *     - The controlled sequences can be associated with the contents of
   *       other representations for character sequences, such as external
   *       files.
   *     - The controlled sequences can support operations @e directly to or
   *       from associated sequences.
   *     - The controlled sequences can impose limitations on how the
   *       program can read characters from a sequence, write characters to
   *       a sequence, put characters back into an input sequence, or alter
   *       the stream position.
   *     .
   *  -# Each sequence is characterized by three pointers which, if non-null,
   *     all point into the same @c charT array object.  The array object
   *     represents, at any moment, a (sub)sequence of characters from the
   *     sequence.  Operations performed on a sequence alter the values
   *     stored in these pointers, perform reads and writes directly to or
   *     from associated sequences, and alter "the stream position" and
   *     conversion state as needed to maintain this subsequence relationship.
   *     The three pointers are:
   *     - the <em>beginning pointer</em>, or lowest element address in the
   *       array (called @e xbeg here);
   *     - the <em>next pointer</em>, or next element address that is a
   *       current candidate for reading or writing (called @e xnext here);
   *     - the <em>end pointer</em>, or first element address beyond the
   *       end of the array (called @e xend here).
   *     .
   *  -# The following semantic constraints shall always apply for any set
   *     of three pointers for a sequence, using the pointer names given
   *     immediately above:
   *     - If @e xnext is not a null pointer, then @e xbeg and @e xend shall
   *       also be non-null pointers into the same @c charT array, as
   *       described above; otherwise, @e xbeg and @e xend shall also be null.
   *     - If @e xnext is not a null pointer and @e xnext < @e xend for an
   *       output sequence, then a <em>write position</em> is available.
   *       In this case, @e *xnext shall be assignable as the next element
   *       to write (to put, or to store a character value, into the sequence).
   *     - If @e xnext is not a null pointer and @e xbeg < @e xnext for an
   *       input sequence, then a <em>putback position</em> is available.
   *       In this case, @e xnext[-1] shall have a defined value and is the
   *       next (preceding) element to store a character that is put back
   *       into the input sequence.
   *     - If @e xnext is not a null pointer and @e xnext< @e xend for an
   *       input sequence, then a <em>read position</em> is available.
   *       In this case, @e *xnext shall have a defined value and is the
   *       next element to read (to get, or to obtain a character value,
   *       from the sequence).
  */
  template<typename _CharT, typename _Traits>
    class basic_streambuf 
    {
    public:
      //@{
      /**
       *  These are standard types.  They permit a standardized way of
       *  referring to names of (or names dependant on) the template
       *  parameters, which are specific to the implementation.
      */
      typedef _CharT 					char_type;
      typedef _Traits 					traits_type;
      typedef typename traits_type::int_type 		int_type;
      typedef typename traits_type::pos_type 		pos_type;
      typedef typename traits_type::off_type 		off_type;
      //@}

      //@{
      /**
       *  @if maint
       *  These are non-standard types.
       *  @endif
      */
      typedef ctype<char_type>           		__ctype_type;
      typedef basic_streambuf<char_type, traits_type>  	__streambuf_type;
      typedef typename traits_type::state_type 		__state_type;
      //@}
      
      friend class basic_ios<char_type, traits_type>;
      friend class basic_istream<char_type, traits_type>;
      friend class basic_ostream<char_type, traits_type>;
      friend class istreambuf_iterator<char_type, traits_type>;
      friend class ostreambuf_iterator<char_type, traits_type>;

      friend streamsize
      __copy_streambufs<>(basic_ios<char_type, traits_type>& __ios,
			  __streambuf_type* __sbin,__streambuf_type* __sbout);
      
    protected:
      /**
       *  @if maint
       *  Pointer to the beginning of internally-allocated space.  Filebuf
       *  manually allocates/deallocates this, whereas stringstreams attempt
       *  to use the built-in intelligence of the string class.  If you are
       *  managing memory, set this.  If not, leave it NULL.
       *  @endif
      */
      char_type*		_M_buf; 	

      /**
       *  @if maint
       *  Actual size of allocated internal buffer, in bytes.
       *  @endif
      */
      size_t			_M_buf_size;

      /**
       *  @if maint
       *  Optimal or preferred size of internal buffer, in bytes.
       *  @endif
      */
      size_t			_M_buf_size_opt;

      /**
       *  @if maint
       *  True iff _M_in_* and _M_out_* buffers should always point to
       *  the same place.  True for fstreams, false for sstreams.
       *  @endif
      */
      bool 			_M_buf_unified;	

      //@{
      /**
       *  @if maint
       *  This is based on _IO_FILE, just reordered to be more consistent,
       *  and is intended to be the most minimal abstraction for an
       *  internal buffer.
       *  -  get == input == read
       *  -  put == output == write
       *  @endif
      */
      char_type* 		_M_in_beg;  	// Start of get area. 
      char_type* 		_M_in_cur;	// Current read area. 
      char_type* 		_M_in_end;	// End of get area. 
      char_type* 		_M_out_beg; 	// Start of put area. 
      char_type* 		_M_out_cur;  	// Current put area. 
      char_type* 		_M_out_end;  	// End of put area. 
      //@}

      /**
       *  @if maint
       *  Place to stash in || out || in | out settings for current streambuf.
       *  @endif
      */
      ios_base::openmode 	_M_mode;	

      /**
       *  @if maint
       *  Current locale setting.
       *  @endif
      */
      locale 			_M_buf_locale;	

      /**
       *  @if maint
       *  True iff locale is initialized.
       *  @endif
      */
      bool 			_M_buf_locale_init;

      //@{
      /**
       *  @if maint
       *  Necessary bits for putback buffer management. Only used in
       *  the basic_filebuf class, as necessary for the standard
       *  requirements. The only basic_streambuf member function that
       *  needs access to these data members is in_avail...
       *  
       *  @note pbacks of over one character are not currently supported.
       *  @endif
      */
      static const size_t   	_S_pback_size = 1; 
      char_type			_M_pback[_S_pback_size]; 
      char_type*		_M_pback_cur_save;
      char_type*		_M_pback_end_save;
      bool			_M_pback_init; 
      //@}

      /**
       *  @if maint
       *  Yet unused.
       *  @endif
      */
      fpos<__state_type>	_M_pos;

      // Initializes pback buffers, and moves normal buffers to safety.
      // Assumptions:
      // _M_in_cur has already been moved back
      void
      _M_pback_create()
      {
	if (!_M_pback_init)
	  {
	    size_t __dist = _M_in_end - _M_in_cur;
	    size_t __len = min(_S_pback_size, __dist);
	    traits_type::copy(_M_pback, _M_in_cur, __len);
	    _M_pback_cur_save = _M_in_cur;
	    _M_pback_end_save = _M_in_end;
	    this->setg(_M_pback, _M_pback, _M_pback + __len);
	    _M_pback_init = true;
	  }
      }

      // Deactivates pback buffer contents, and restores normal buffer.
      // Assumptions:
      // The pback buffer has only moved forward.
      void
      _M_pback_destroy() throw()
      {
	if (_M_pback_init)
	  {
	    // Length _M_in_cur moved in the pback buffer.
	    size_t __off_cur = _M_in_cur - _M_pback;
	    
	    // For in | out buffers, the end can be pushed back...
	    size_t __off_end = 0;
	    size_t __pback_len = _M_in_end - _M_pback;
	    size_t __save_len = _M_pback_end_save - _M_buf;
	    if (__pback_len > __save_len)
	      __off_end = __pback_len - __save_len;

	    this->setg(_M_buf, _M_pback_cur_save + __off_cur, 
		       _M_pback_end_save + __off_end);
	    _M_pback_cur_save = NULL;
	    _M_pback_end_save = NULL;
	    _M_pback_init = false;
	  }
      }

      // Correctly sets the _M_in_cur pointer, and bumps the
      // _M_out_cur pointer as well if necessary.
      void 
      _M_in_cur_move(off_type __n) // argument needs to be +-
      {
	bool __testout = _M_out_cur;
	_M_in_cur += __n;
	if (__testout && _M_buf_unified)
	  _M_out_cur += __n;
      }

      // Correctly sets the _M_out_cur pointer, and bumps the
      // appropriate _M_*_end pointers as well. Necessary for the
      // un-tied stringbufs, in in|out mode.
      // Invariant:
      // __n + _M_out_[cur, end] <= _M_buf + _M_buf_size
      // Assuming all _M_*_[beg, cur, end] pointers are operating on
      // the same range:
      // _M_buf <= _M_*_ <= _M_buf + _M_buf_size
      void 
      _M_out_cur_move(off_type __n) // argument needs to be +-
      {
	bool __testin = _M_in_cur;

	_M_out_cur += __n;
	if (__testin && _M_buf_unified)
	  _M_in_cur += __n;
	if (_M_out_cur > _M_out_end)
	  {
	    _M_out_end = _M_out_cur;
	    // NB: in | out buffers drag the _M_in_end pointer along...
	    if (__testin)
	      _M_in_end += __n;
	  }
      }

      // Return the size of the output buffer.  This depends on the
      // buffer in use: allocated buffers have a stored size in
      // _M_buf_size and setbuf() buffers don't.
      off_type
      _M_out_buf_size()
      {
	off_type __ret = 0;
	if (_M_out_cur)
	  {
	    // Using allocated buffer.
	    if (_M_out_beg == _M_buf)
	      __ret = _M_out_beg + _M_buf_size - _M_out_cur;
	    // Using non-allocated buffer.
	    else
	      __ret = _M_out_end - _M_out_cur;
	  }
	return __ret;
      }

  public:
      /// Destructor deallocates no buffer space.
      virtual 
      ~basic_streambuf() 
      {
	_M_buf_unified = false;
	_M_buf_size = 0;
	_M_buf_size_opt = 0;
	_M_mode = ios_base::openmode(0);
      }

      // [27.5.2.2.1] locales
      /**
       *  @brief  Entry point for imbue().
       *  @param  loc  The new locale.
       *  @return  The previous locale.
       *
       *  Calls the derived imbue(loc).
      */
      locale 
      pubimbue(const locale &__loc)
      {
	locale __tmp(this->getloc());
	this->imbue(__loc);
	_M_buf_locale = __loc;
	return __tmp;
      }

      /**
       *  @brief  Locale access.
       *  @return  The current locale in effect.
       *
       *  If pubimbue(loc) has been called, then the most recent @c loc
       *  is returned.  Otherwise the global locale in effect at the time
       *  of construction is returned.
      */
      locale   
      getloc() const
      { return _M_buf_locale; } 

      // [27.5.2.2.2] buffer management and positioning
      //@{
      /**
       *  @brief  Entry points for derived buffer functions.
       *
       *  The public versions of @c pubfoo dispatch to the protected
       *  derived @c foo member functions, passing the arguments (if any)
       *  and returning the result unchanged.
      */
      __streambuf_type* 
      pubsetbuf(char_type* __s, streamsize __n) 
      { return this->setbuf(__s, __n); }

      pos_type 
      pubseekoff(off_type __off, ios_base::seekdir __way, 
		 ios_base::openmode __mode = ios_base::in | ios_base::out)
      { return this->seekoff(__off, __way, __mode); }

      pos_type 
      pubseekpos(pos_type __sp,
		 ios_base::openmode __mode = ios_base::in | ios_base::out)
      { return this->seekpos(__sp, __mode); }

      int 
      pubsync() { return this->sync(); }
      //@}

      // [27.5.2.2.3] get area
      /**
       *  @brief  Looking ahead into the stream.
       *  @return  The number of characters available.
       *
       *  If a read position is available, returns the number of characters
       *  available for reading before the buffer must be refilled.
       *  Otherwise returns the derived @c showmanyc().
      */
      streamsize 
      in_avail() 
      { 
	streamsize __ret;
	if (_M_in_cur && _M_in_cur < _M_in_end)
	  {
	    if (_M_pback_init)
	      {
		size_t __save_len =  _M_pback_end_save - _M_pback_cur_save;
		size_t __pback_len = _M_in_cur - _M_pback;
		__ret = __save_len - __pback_len;
	      }
	    else
	      __ret = this->egptr() - this->gptr();
	  }
	else
	  __ret = this->showmanyc();
	return __ret;
      }

      /**
       *  @brief  Getting the next character.
       *  @return  The next character, or eof.
       *
       *  Calls @c sbumpc(), and if that function returns
       *  @c traits::eof(), so does this function.  Otherwise, @c sgetc().
      */
      int_type 
      snextc()
      {
	int_type __eof = traits_type::eof();
	return (traits_type::eq_int_type(this->sbumpc(), __eof) 
		? __eof : this->sgetc());
      }

      /**
       *  @brief  Getting the next character.
       *  @return  The next character, or eof.
       *
       *  If the input read position is available, returns that character
       *  and increments the read pointer, otherwise calls and returns
       *  @c uflow().
      */
      int_type 
      sbumpc();

      /**
       *  @brief  Getting the next character.
       *  @return  The next character, or eof.
       *
       *  If the input read position is available, returns that character,
       *  otherwise calls and returns @c underflow().  Does not move the 
       *  read position after fetching the character.
      */
      int_type 
      sgetc()
      {
	int_type __ret;
	if (_M_in_cur && _M_in_cur < _M_in_end)
	  __ret = traits_type::to_int_type(*(this->gptr()));
	else 
	  __ret = this->underflow();
	return __ret;
      }

      /**
       *  @brief  Entry point for xsgetn.
       *  @param  s  A buffer area.
       *  @param  n  A count.
       *
       *  Returns xsgetn(s,n).  The effect is to fill @a s[0] through
       *  @a s[n-1] with characters from the input sequence, if possible.
      */
      streamsize 
      sgetn(char_type* __s, streamsize __n)
      { return this->xsgetn(__s, __n); }

      // [27.5.2.2.4] putback
      /**
       *  @brief  Pushing characters back into the input stream.
       *  @param  c  The character to push back.
       *  @return  The previous character, if possible.
       *
       *  Similar to sungetc(), but @a c is pushed onto the stream instead
       *  of "the previous character".  If successful, the next character
       *  fetched from the input stream will be @a c.
      */
      int_type 
      sputbackc(char_type __c);

      /**
       *  @brief  Moving backwards in the input stream.
       *  @return  The previous character, if possible.
       *
       *  If a putback position is available, this function decrements the
       *  input pointer and returns that character.  Otherwise, calls and
       *  returns pbackfail().  The effect is to "unget" the last character
       *  "gotten".
      */
      int_type 
      sungetc();

      // [27.5.2.2.5] put area
      /**
       *  @brief  Entry point for all single-character output functions.
       *  @param  c  A character to output.
       *  @return  @a c, if possible.
       *
       *  One of two public output functions.
       *
       *  If a write position is available for the output sequence (i.e.,
       *  the buffer is not full), stores @a c in that position, increments
       *  the position, and returns @c traits::to_int_type(c).  If a write
       *  position is not available, returns @c overflow(c).
      */
      int_type 
      sputc(char_type __c);

      /**
       *  @brief  Entry point for all single-character output functions.
       *  @param  s  A buffer read area.
       *  @param  n  A count.
       *
       *  One of two public output functions.
       *
       *
       *  Returns xsputn(s,n).  The effect is to write @a s[0] through
       *  @a s[n-1] to the output sequence, if possible.
      */
      streamsize 
      sputn(const char_type* __s, streamsize __n)
      { return this->xsputn(__s, __n); }

    protected:
      /**
       *  @brief  Base constructor.
       *
       *  Only called from derived constructors, and sets up all the
       *  buffer data to zero, including the pointers described in the
       *  basic_streambuf class description.  Note that, as a result,
       *  - the class starts with no read nor write positions available,
       *  - this is not an error
      */
      basic_streambuf()
      : _M_buf(NULL), _M_buf_size(0), _M_buf_size_opt(BUFSIZ), 
      _M_buf_unified(false), _M_in_beg(0), _M_in_cur(0), _M_in_end(0), 
      _M_out_beg(0), _M_out_cur(0), _M_out_end(0), 
      _M_mode(ios_base::openmode(0)), _M_buf_locale(locale()), 
      _M_pback_cur_save(0), _M_pback_end_save(0), 
      _M_pback_init(false)
      { }

      // [27.5.2.3.1] get area access
      //@{
      /**
       *  @brief  Access to the get area.
       *
       *  These functions are only available to other protected functions,
       *  including derived classes.
       *
       *  - eback() returns the beginning pointer for the input sequence
       *  - gptr() returns the next pointer for the input sequence
       *  - egptr() returns the end pointer for the input sequence
      */
      char_type* 
      eback() const { return _M_in_beg; }

      char_type* 
      gptr()  const { return _M_in_cur;  }

      char_type* 
      egptr() const { return _M_in_end; }
      //@}

      /**
       *  @brief  Moving the read position.
       *  @param  n  The delta by which to move.
       *
       *  This just advances the read position without returning any data.
      */
      void 
      gbump(int __n) { _M_in_cur += __n; }

      /**
       *  @brief  Setting the three read area pointers.
       *  @param  gbeg  A pointer.
       *  @param  gnext  A pointer.
       *  @param  gend  A pointer.
       *  @post  @a gbeg == @c eback(), @a gnext == @c gptr(), and
       *         @a gend == @c egptr()
      */
      void 
      setg(char_type* __gbeg, char_type* __gnext, char_type* __gend)
      {
	_M_in_beg = __gbeg;
	_M_in_cur = __gnext;
	_M_in_end = __gend;
	if (!(_M_mode & ios_base::in) && __gbeg && __gnext && __gend)
	  _M_mode = _M_mode | ios_base::in;
      }

      // [27.5.2.3.2] put area access
      //@{
      /**
       *  @brief  Access to the put area.
       *
       *  These functions are only available to other protected functions,
       *  including derived classes.
       *
       *  - pbase() returns the beginning pointer for the output sequence
       *  - pptr() returns the next pointer for the output sequence
       *  - epptr() returns the end pointer for the output sequence
      */
      char_type* 
      pbase() const { return _M_out_beg; }

      char_type* 
      pptr() const { return _M_out_cur; }

      char_type* 
      epptr() const { return _M_out_end; }
      //@}

      /**
       *  @brief  Moving the write position.
       *  @param  n  The delta by which to move.
       *
       *  This just advances the write position without returning any data.
      */
      void 
      pbump(int __n) { _M_out_cur += __n; }

      /**
       *  @brief  Setting the three write area pointers.
       *  @param  pbeg  A pointer.
       *  @param  pend  A pointer.
       *  @post  @a pbeg == @c pbase(), @a pbeg == @c pptr(), and
       *         @a pend == @c epptr()
      */
      void 
      setp(char_type* __pbeg, char_type* __pend)
      { 
	_M_out_beg = _M_out_cur = __pbeg; 
	_M_out_end = __pend; 
	if (!(_M_mode & ios_base::out) && __pbeg && __pend)
	  _M_mode = _M_mode | ios_base::out;
      }

      // [27.5.2.4] virtual functions
      // [27.5.2.4.1] locales
      /**
       *  @brief  Changes translations.
       *  @param  loc  A new locale.
       *
       *  Translations done during I/O which depend on the current locale
       *  are changed by this call.  The standard adds, "Between invocations
       *  of this function a class derived from streambuf can safely cache
       *  results of calls to locale functions and to members of facets
       *  so obtained."
       *
       *  @note  Base class version does nothing.
      */
      virtual void 
      imbue(const locale&) 
      { }

      // [27.5.2.4.2] buffer management and positioning
      /**
       *  @brief  Maniuplates the buffer.
       *
       *  Each derived class provides its own appropriate behavior.  See
       *  the next-to-last paragraph of 
       *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#2 for
       *  more on this function.
       *
       *  @note  Base class version does nothing, returns @c this.
      */
      virtual basic_streambuf<char_type,_Traits>* 
      setbuf(char_type*, streamsize)
      {	return this; }
      
      /**
       *  @brief  Alters the stream positions.
       *
       *  Each derived class provides its own appropriate behavior.
       *  @note  Base class version does nothing, returns a @c pos_type
       *         that represents an invalid stream position.
      */
      virtual pos_type 
      seekoff(off_type, ios_base::seekdir,
	      ios_base::openmode /*__mode*/ = ios_base::in | ios_base::out)
      { return pos_type(off_type(-1)); } 

      /**
       *  @brief  Alters the stream positions.
       *
       *  Each derived class provides its own appropriate behavior.
       *  @note  Base class version does nothing, returns a @c pos_type
       *         that represents an invalid stream position.
      */
      virtual pos_type 
      seekpos(pos_type, 
	      ios_base::openmode /*__mode*/ = ios_base::in | ios_base::out)
      { return pos_type(off_type(-1)); } 

      /**
       *  @brief  Synchronizes the buffer arrays with the controlled sequences.
       *  @return  -1 on failure.
       *
       *  Each derived class provides its own appropriate behavior,
       *  including the definition of "failure".
       *  @note  Base class version does nothing, returns zero.
      */
      virtual int 
      sync() { return 0; }

      // [27.5.2.4.3] get area
      /**
       *  @brief  Investigating the data available.
       *  @return  An estimate of the number of characters available in the
       *           input sequence, or -1.
       *
       *  "If it returns a positive value, then successive calls to
       *  @c underflow() will not return @c traits::eof() until at least that
       *  number of characters have been supplied.  If @c showmanyc()
       *  returns -1, then calls to @c underflow() or @c uflow() will fail."
       *  [27.5.2.4.3]/1
       *
       *  @note  Base class version does nothing, returns zero.
       *  @note  The standard adds that "the intention is not only that the
       *         calls [to underflow or uflow] will not return @c eof() but
       *         that they will return "immediately".
       *  @note  The standard adds that "the morphemes of @c showmanyc are
       *         "es-how-many-see", not "show-manic".
      */
      virtual streamsize 
      showmanyc() { return 0; }

      /**
       *  @brief  Multiple character extraction.
       *  @param  s  A buffer area.
       *  @param  n  Maximum number of characters to assign.
       *  @return  The number of characters assigned.
       *
       *  Fills @a s[0] through @a s[n-1] with characters from the input
       *  sequence, as if by @c sbumpc().  Stops when either @a n characters
       *  have been copied, or when @c traits::eof() would be copied.
       *
       *  It is expected that derived classes provide a more efficient
       *  implementation by overriding this definition.
      */
      virtual streamsize 
      xsgetn(char_type* __s, streamsize __n);

      /**
       *  @brief  Fetches more data from the controlled sequence.
       *  @return  The first character from the <em>pending sequence</em>.
       *
       *  Informally, this function is called when the input buffer is
       *  exhausted (or does not exist, as buffering need not actually be
       *  done).  If a buffer exists, it is "refilled".  In either case, the
       *  next available character is returned, or @c traits::eof() to
       *  indicate a null pending sequence.
       *
       *  For a formal definiton of the pending sequence, see a good text
       *  such as Langer & Kreft, or [27.5.2.4.3]/7-14.
       *
       *  A functioning input streambuf can be created by overriding only
       *  this function (no buffer area will be used).  For an example, see
       *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#6
       *
       *  @note  Base class version does nothing, returns eof().
      */
      virtual int_type 
      underflow()
      { return traits_type::eof(); }

      /**
       *  @brief  Fetches more data from the controlled sequence.
       *  @return  The first character from the <em>pending sequence</em>.
       *
       *  Informally, this function does the same thing as @c underflow(),
       *  and in fact is required to call that function.  It also returns
       *  the new character, like @c underflow() does.  However, this
       *  function also moves the read position forward by one.
      */
      virtual int_type 
      uflow() 
      {
	int_type __ret = traits_type::eof();
	bool __testeof = traits_type::eq_int_type(this->underflow(), __ret);
	bool __testpending = _M_in_cur && _M_in_cur < _M_in_end;
	if (!__testeof && __testpending)
	  {
	    __ret = traits_type::to_int_type(*_M_in_cur);
	    ++_M_in_cur;
	    if (_M_buf_unified && _M_mode & ios_base::out)
	      ++_M_out_cur;
	  }
	return __ret;    
      }

      // [27.5.2.4.4] putback
      /**
       *  @brief  Tries to back up the input sequence.
       *  @param  c  The character to be inserted back into the sequence.
       *  @return  eof() on failure, "some other value" on success
       *  @post  The constraints of @c gptr(), @c eback(), and @c pptr()
       *         are the same as for @c underflow().
       *
       *  @note  Base class version does nothing, returns eof().
      */
      virtual int_type 
      pbackfail(int_type /* __c */  = traits_type::eof())
      { return traits_type::eof(); }

      // Put area:
      /**
       *  @brief  Multiple character insertion.
       *  @param  s  A buffer area.
       *  @param  n  Maximum number of characters to write.
       *  @return  The number of characters written.
       *
       *  Writes @a s[0] through @a s[n-1] to the output sequence, as if
       *  by @c sputc().  Stops when either @a n characters have been
       *  copied, or when @c sputc() would return @c traits::eof().
       *
       *  It is expected that derived classes provide a more efficient
       *  implementation by overriding this definition.
      */
      virtual streamsize 
      xsputn(const char_type* __s, streamsize __n);

      /**
       *  @brief  Consumes data from the buffer; writes to the
       *          controlled sequence.
       *  @param  c  An additional character to consume.
       *  @return  eof() to indicate failure, something else (usually
       *           @a c, or not_eof())
       *
       *  Informally, this function is called when the output buffer is full
       *  (or does not exist, as buffering need not actually be done).  If a
       *  buffer exists, it is "consumed", with "some effect" on the
       *  controlled sequence.  (Typically, the buffer is written out to the
       *  sequence verbatim.)  In either case, the character @a c is also
       *  written out, if @a c is not @c eof().
       *
       *  For a formal definiton of this function, see a good text
       *  such as Langer & Kreft, or [27.5.2.4.5]/3-7.
       *
       *  A functioning output streambuf can be created by overriding only
       *  this function (no buffer area will be used).
       *
       *  @note  Base class version does nothing, returns eof().
      */
      virtual int_type 
      overflow(int_type /* __c */ = traits_type::eof())
      { return traits_type::eof(); }

#ifdef _GLIBCPP_DEPRECATED
    // Annex D.6
    public:
      /**
       *  @brief  Tosses a character.
       *
       *  Advances the read pointer, ignoring the character that would have
       *  been read.
       *
       *  See http://gcc.gnu.org/ml/libstdc++/2002-05/msg00168.html
       *
       *  @note  This function has been deprecated by the standard.  You
       *         must define @c _GLIBCPP_DEPRECATED to make this visible; see
       *         c++config.h.
      */
      void 
      stossc() 
      {
	if (_M_in_cur < _M_in_end) 
	  ++_M_in_cur;
	else 
	  this->uflow();
      }
#endif

#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
    // Side effect of DR 50. 
    private:
      basic_streambuf(const __streambuf_type&) { }; 

      __streambuf_type& 
      operator=(const __streambuf_type&) { return *this; };
#endif
    };
} // namespace std

#ifdef _GLIBCPP_NO_TEMPLATE_EXPORT
# define export
#endif
#ifdef  _GLIBCPP_FULLY_COMPLIANT_HEADERS
#include <bits/streambuf.tcc>
#endif

#endif	
