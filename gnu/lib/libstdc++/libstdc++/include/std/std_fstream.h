// File based streams -*- C++ -*-

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
// ISO C++ 14882: 27.8  File-based streams
//

/** @file fstream
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _CPP_FSTREAM
#define _CPP_FSTREAM	1

#pragma GCC system_header

#include <istream>
#include <ostream>
#include <locale>	// For codecvt
#include <bits/basic_file.h>
#include <bits/gthr.h>

namespace std
{
  // [27.8.1.1] template class basic_filebuf
  /**
   *  @brief  The actual work of input and output (for files).
   *
   *  This class associates both its input and output sequence with an
   *  external disk file, and maintains a joint file position for both
   *  sequences.  Many of its sematics are described in terms of similar
   *  behavior in the Standard C Library's @c FILE streams.
  */
  template<typename _CharT, typename _Traits>
    class basic_filebuf : public basic_streambuf<_CharT, _Traits>
    {
    public:
      // Types:
      typedef _CharT                     	        char_type;
      typedef _Traits                    	        traits_type;
      typedef typename traits_type::int_type 		int_type;
      typedef typename traits_type::pos_type 		pos_type;
      typedef typename traits_type::off_type 		off_type;

      //@{
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      typedef basic_streambuf<char_type, traits_type>  	__streambuf_type;
      typedef basic_filebuf<char_type, traits_type>     __filebuf_type;
      typedef __basic_file<char>		        __file_type;
      typedef typename traits_type::state_type          __state_type;
      typedef codecvt<char_type, char, __state_type>    __codecvt_type;
      typedef ctype<char_type>                          __ctype_type;
      //@}

      friend class ios_base; // For sync_with_stdio.

    protected:
      // Data Members:
      // MT lock inherited from libio or other low-level io library.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      __c_lock          	_M_lock;

      // External buffer.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      __file_type 		_M_file;

      // Current and beginning state type for codecvt.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      __state_type		_M_state_cur;
      __state_type 		_M_state_beg;

      // Set iff _M_buf is allocated memory from _M_allocate_internal_buffer.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      bool			_M_buf_allocated;
      
      // XXX Needed?
      bool			_M_last_overflowed;

      // The position in the buffer corresponding to the external file
      // pointer.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      char_type*		_M_filepos;

    public:
      // Constructors/destructor:
      /**
       *  @brief  Does not open any files.
       *
       *  The default constructor initializes the parent class using its
       *  own default ctor.
      */
      basic_filebuf();

      /**
       *  @brief  The destructor closes the file first.
      */
      virtual
      ~basic_filebuf()
      {
	this->close();
	_M_last_overflowed = false;
      }

      // Members:
      /**
       *  @brief  Returns true if the external file is open.
      */
      bool
      is_open() const throw() { return _M_file.is_open(); }

      /**
       *  @brief  Opens an external file.
       *  @param  s  The name of the file.
       *  @param  mode  The open mode flags.
       *  @return  @c this on success, NULL on failure
       *
       *  If a file is already open, this function immediately fails.
       *  Otherwise it tries to open the file named @a s using the flags
       *  given in @a mode.
       *
       *  [Table 92 gives the relation between openmode combinations and the
       *  equivalent fopen() flags, but the table has not been copied yet.]
      */
      __filebuf_type*
      open(const char* __s, ios_base::openmode __mode);

      /**
       *  @brief  Closes the currently associated file.
       *  @return  @c this on success, NULL on failure
       *
       *  If no file is currently open, this function immediately fails.
       *
       *  If a "put buffer area" exists, @c overflow(eof) is called to flush
       *  all the characters.  The file is then closed.
       *
       *  If any operations fail, this function also fails.
      */
      __filebuf_type*
      close() throw();

    protected:
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      void
      _M_allocate_internal_buffer();

      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      void
      _M_destroy_internal_buffer() throw();

      // [27.8.1.4] overridden virtual functions
      // [documentation is inherited]
      virtual streamsize
      showmanyc();

      // Stroustrup, 1998, p. 628
      // underflow() and uflow() functions are called to get the next
      // charater from the real input source when the buffer is empty.
      // Buffered input uses underflow()

      // The only difference between underflow() and uflow() is that the
      // latter bumps _M_in_cur after the read.  In the sync_with_stdio
      // case, this is important, as we need to unget the read character in
      // the underflow() case in order to maintain synchronization.  So
      // instead of calling underflow() from uflow(), we create a common
      // subroutine to do the real work.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      int_type
      _M_underflow_common(bool __bump);

      // [documentation is inherited]
      virtual int_type
      underflow();

      // [documentation is inherited]
      virtual int_type
      uflow();

      // [documentation is inherited]
      virtual int_type
      pbackfail(int_type __c = _Traits::eof());

      // NB: For what the standard expects of the overflow function,
      // see _M_really_overflow(), below. Because basic_streambuf's
      // sputc/sputn call overflow directly, and the complications of
      // this implementation's setting of the initial pointers all
      // equal to _M_buf when initializing, it seems essential to have
      // this in actuality be a helper function that checks for the
      // eccentricities of this implementation, and then call
      // overflow() if indeed the buffer is full.

      // [documentation is inherited]
      virtual int_type
      overflow(int_type __c = _Traits::eof());

      // Stroustrup, 1998, p 648
      // The overflow() function is called to transfer characters to the
      // real output destination when the buffer is full. A call to
      // overflow(c) outputs the contents of the buffer plus the
      // character c.
      // 27.5.2.4.5
      // Consume some sequence of the characters in the pending sequence.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      int_type
      _M_really_overflow(int_type __c = _Traits::eof());

      // Convert internal byte sequence to external, char-based
      // sequence via codecvt.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      void
      _M_convert_to_external(char_type*, streamsize, streamsize&, streamsize&);

      /**
       *  @brief  Manipulates the buffer.
       *  @param  s  Pointer to a buffer area.
       *  @param  n  Size of @a s.
       *  @return  @c this
       *
       *  If no file has been opened, and both @a s and @a n are zero, then
       *  the stream becomes unbuffered.  Otherwise, @c s is used as a
       *  buffer; see
       *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#2
       *  for more.
      */
      virtual __streambuf_type*
      setbuf(char_type* __s, streamsize __n);

      // [documentation is inherited]
      virtual pos_type
      seekoff(off_type __off, ios_base::seekdir __way,
	      ios_base::openmode __mode = ios_base::in | ios_base::out);

      // [documentation is inherited]
      virtual pos_type
      seekpos(pos_type __pos,
	      ios_base::openmode __mode = ios_base::in | ios_base::out);

      // [documentation is inherited]
      virtual int
      sync()
      {
	int __ret = 0;
	bool __testput = _M_out_cur && _M_out_beg < _M_out_end;

	// Make sure that the internal buffer resyncs its idea of
	// the file position with the external file.
	if (__testput)
	  {
	    // Need to restore current position after the write.
	    off_type __off = _M_out_cur - _M_out_end;

	    // _M_file.sync() will be called within
	    if (traits_type::eq_int_type(_M_really_overflow(),
					 traits_type::eof()))
	      __ret = -1;
	    else if (__off)
	      _M_file.seekoff(__off, ios_base::cur);
	  }
	else
	  _M_file.sync();

	_M_last_overflowed = false;
	return __ret;
      }

      // [documentation is inherited]
      virtual void
      imbue(const locale& __loc);

      // [documentation is inherited]
      virtual streamsize
      xsgetn(char_type* __s, streamsize __n)
      {
	streamsize __ret = 0;
	// Clear out pback buffer before going on to the real deal...
	if (_M_pback_init)
	  {
	    while (__ret < __n && _M_in_cur < _M_in_end)
	      {
		*__s = *_M_in_cur;
		++__ret;
		++__s;
		++_M_in_cur;
	      }
	    _M_pback_destroy();
	  }
	if (__ret < __n)
	  __ret += __streambuf_type::xsgetn(__s, __n - __ret);
	return __ret;
      }

      // [documentation is inherited]
      virtual streamsize
      xsputn(const char_type* __s, streamsize __n)
      {
	_M_pback_destroy();
	return __streambuf_type::xsputn(__s, __n);
      }

      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      void
      _M_output_unshift();

      // These three functions are used to clarify internal buffer
      // maintenance. After an overflow, or after a seekoff call that
      // started at beg or end, or possibly when the stream becomes
      // unbuffered, and a myrid other obscure corner cases, the
      // internal buffer does not truly reflect the contents of the
      // external buffer. At this point, for whatever reason, it is in
      // an indeterminate state.
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      void
      _M_set_indeterminate(void)
      {
	if (_M_mode & ios_base::in)
	  this->setg(_M_buf, _M_buf, _M_buf);
	if (_M_mode & ios_base::out)
	  this->setp(_M_buf, _M_buf);
	_M_filepos = _M_buf;
      }

      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      void
      _M_set_determinate(off_type __off)
      {
	bool __testin = _M_mode & ios_base::in;
	bool __testout = _M_mode & ios_base::out;
	if (__testin)
	  this->setg(_M_buf, _M_buf, _M_buf + __off);
	if (__testout)
	  this->setp(_M_buf, _M_buf + __off);
	_M_filepos = _M_buf + __off;
      }

      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      bool
      _M_is_indeterminate(void)
      { 
	bool __ret = false;
	// Don't return true if unbuffered.
	if (_M_buf)
	  {
	    if (_M_mode & ios_base::in)
	      __ret = _M_in_beg == _M_in_cur && _M_in_cur == _M_in_end;
	    if (_M_mode & ios_base::out)
	      __ret = _M_out_beg == _M_out_cur && _M_out_cur == _M_out_end;
	  }
	return __ret;
      }
    };

  // Explicit specialization declarations, defined in src/fstream.cc.
  template<> 
    basic_filebuf<char>::int_type 
    basic_filebuf<char>::_M_underflow_common(bool __bump);

# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<> 
    basic_filebuf<wchar_t>::int_type 
    basic_filebuf<wchar_t>::_M_underflow_common(bool __bump);
# endif

  // Generic definitions.
  template <typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::int_type
    basic_filebuf<_CharT, _Traits>::underflow() 
    { return _M_underflow_common(false); }

  template <typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::int_type
    basic_filebuf<_CharT, _Traits>::uflow() 
    { return _M_underflow_common(true); }


  // [27.8.1.5] Template class basic_ifstream
  /**
   *  @brief  Controlling input for files.
   *
   *  This class supports reading from named files, using the inherited
   *  functions from std::basic_istream.  To control the associated
   *  sequence, an instance of std::basic_filebuf is used, which this page
   *  refers to as @c sb.
  */
  template<typename _CharT, typename _Traits>
    class basic_ifstream : public basic_istream<_CharT, _Traits>
    {
    public:
      // Types:
      typedef _CharT 					char_type;
      typedef _Traits 					traits_type;
      typedef typename traits_type::int_type 		int_type;
      typedef typename traits_type::pos_type 		pos_type;
      typedef typename traits_type::off_type 		off_type;

      // Non-standard types:
      typedef basic_filebuf<char_type, traits_type> 	__filebuf_type;
      typedef basic_istream<char_type, traits_type>	__istream_type;

    private:
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      __filebuf_type	_M_filebuf;

    public:
      // Constructors/Destructors:
      /**
       *  @brief  Default constructor.
       *
       *  Initializes @c sb using its default constructor, and passes
       *  @c &sb to the base class initializer.  Does not open any files
       *  (you haven't given it a filename to open).
      */
      basic_ifstream()
      : __istream_type(NULL), _M_filebuf()
      { this->init(&_M_filebuf); }

      /**
       *  @brief  Create an input file stream.
       *  @param  s  Null terminated string specifying the filename.
       *  @param  mode  Open file in specified mode (see std::ios_base).
       *
       *  @c ios_base::in is automatically included in @a mode.
       *
       *  Tip:  When using std::string to hold the filename, you must use
       *  .c_str() before passing it to this constructor.
      */
      explicit
      basic_ifstream(const char* __s, ios_base::openmode __mode = ios_base::in)
      : __istream_type(NULL), _M_filebuf()
      {
	this->init(&_M_filebuf);
	this->open(__s, __mode);
      }

      /**
       *  @brief  The destructor does nothing.
       *
       *  The file is closed by the filebuf object, not the formatting
       *  stream.
      */
      ~basic_ifstream()
      { }

      // Members:
      /**
       *  @brief  Accessing the underlying buffer.
       *  @return  The current basic_filebuf buffer.
       *
       *  This hides both signatures of std::basic_ios::rdbuf().
      */
      __filebuf_type*
      rdbuf() const
      { return const_cast<__filebuf_type*>(&_M_filebuf); }

      /**
       *  @brief  Wrapper to test for an open file.
       *  @return  @c rdbuf()->is_open()
      */
      bool
      is_open() { return _M_filebuf.is_open(); }

      /**
       *  @brief  Opens an external file.
       *  @param  s  The name of the file.
       *  @param  mode  The open mode flags.
       *
       *  Calls @c std::basic_filebuf::open(s,mode|in).  If that function
       *  fails, @c failbit is set in the stream's error state.
       *
       *  Tip:  When using std::string to hold the filename, you must use
       *  .c_str() before passing it to this constructor.
      */
      void
      open(const char* __s, ios_base::openmode __mode = ios_base::in)
      {
	if (!_M_filebuf.open(__s, __mode | ios_base::in))
	  this->setstate(ios_base::failbit);
      }

      /**
       *  @brief  Close the file.
       *
       *  Calls @c std::basic_filebuf::close().  If that function
       *  fails, @c failbit is set in the stream's error state.
      */
      void
      close()
      {
	if (!_M_filebuf.close())
	  this->setstate(ios_base::failbit);
      }
    };


  // [27.8.1.8] Template class basic_ofstream
  /**
   *  @brief  Controlling output for files.
   *
   *  This class supports reading from named files, using the inherited
   *  functions from std::basic_ostream.  To control the associated
   *  sequence, an instance of std::basic_filebuf is used, which this page
   *  refers to as @c sb.
  */
  template<typename _CharT, typename _Traits>
    class basic_ofstream : public basic_ostream<_CharT,_Traits>
    {
    public:
      // Types:
      typedef _CharT 					char_type;
      typedef _Traits 					traits_type;
      typedef typename traits_type::int_type 		int_type;
      typedef typename traits_type::pos_type 		pos_type;
      typedef typename traits_type::off_type 		off_type;

      // Non-standard types:
      typedef basic_filebuf<char_type, traits_type> 	__filebuf_type;
      typedef basic_ostream<char_type, traits_type>	__ostream_type;

    private:
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      __filebuf_type	_M_filebuf;

    public:
      // Constructors:
      /**
       *  @brief  Default constructor.
       *
       *  Initializes @c sb using its default constructor, and passes
       *  @c &sb to the base class initializer.  Does not open any files
       *  (you haven't given it a filename to open).
      */
      basic_ofstream()
      : __ostream_type(NULL), _M_filebuf()
      { this->init(&_M_filebuf); }

      /**
       *  @brief  Create an output file stream.
       *  @param  s  Null terminated string specifying the filename.
       *  @param  mode  Open file in specified mode (see std::ios_base).
       *
       *  @c ios_base::out|ios_base::trunc is automatically included in
       *  @a mode.
       *
       *  Tip:  When using std::string to hold the filename, you must use
       *  .c_str() before passing it to this constructor.
      */
      explicit
      basic_ofstream(const char* __s,
		     ios_base::openmode __mode = ios_base::out|ios_base::trunc)
      : __ostream_type(NULL), _M_filebuf()
      {
	this->init(&_M_filebuf);
	this->open(__s, __mode);
      }

      /**
       *  @brief  The destructor does nothing.
       *
       *  The file is closed by the filebuf object, not the formatting
       *  stream.
      */
      ~basic_ofstream()
      { }

      // Members:
      /**
       *  @brief  Accessing the underlying buffer.
       *  @return  The current basic_filebuf buffer.
       *
       *  This hides both signatures of std::basic_ios::rdbuf().
      */
      __filebuf_type*
      rdbuf() const
      { return const_cast<__filebuf_type*>(&_M_filebuf); }

      /**
       *  @brief  Wrapper to test for an open file.
       *  @return  @c rdbuf()->is_open()
      */
      bool
      is_open() { return _M_filebuf.is_open(); }

      /**
       *  @brief  Opens an external file.
       *  @param  s  The name of the file.
       *  @param  mode  The open mode flags.
       *
       *  Calls @c std::basic_filebuf::open(s,mode|out|trunc).  If that
       *  function fails, @c failbit is set in the stream's error state.
       *
       *  Tip:  When using std::string to hold the filename, you must use
       *  .c_str() before passing it to this constructor.
      */
      void
      open(const char* __s,
	   ios_base::openmode __mode = ios_base::out | ios_base::trunc)
      {
	if (!_M_filebuf.open(__s, __mode | ios_base::out))
	  this->setstate(ios_base::failbit);
      }

      /**
       *  @brief  Close the file.
       *
       *  Calls @c std::basic_filebuf::close().  If that function
       *  fails, @c failbit is set in the stream's error state.
      */
      void
      close()
      {
	if (!_M_filebuf.close())
	  this->setstate(ios_base::failbit);
      }
    };


  // [27.8.1.11] Template class basic_fstream
  /**
   *  @brief  Controlling intput and output for files.
   *
   *  This class supports reading from and writing to named files, using
   *  the inherited functions from std::basic_iostream.  To control the
   *  associated sequence, an instance of std::basic_filebuf is used, which
   *  this page refers to as @c sb.
  */
  template<typename _CharT, typename _Traits>
    class basic_fstream : public basic_iostream<_CharT, _Traits>
    {
    public:
      // Types:
      typedef _CharT 					char_type;
      typedef _Traits 					traits_type;
      typedef typename traits_type::int_type 		int_type;
      typedef typename traits_type::pos_type 		pos_type;
      typedef typename traits_type::off_type 		off_type;

      // Non-standard types:
      typedef basic_filebuf<char_type, traits_type> 	__filebuf_type;
      typedef basic_ios<char_type, traits_type>		__ios_type;
      typedef basic_iostream<char_type, traits_type>	__iostream_type;

    private:
      /**
       *  @if maint
       *  @doctodo
       *  @endif
      */
      __filebuf_type	_M_filebuf;

    public:
      // Constructors/destructor:
      /**
       *  @brief  Default constructor.
       *
       *  Initializes @c sb using its default constructor, and passes
       *  @c &sb to the base class initializer.  Does not open any files
       *  (you haven't given it a filename to open).
      */
      basic_fstream()
      : __iostream_type(NULL), _M_filebuf()
      { this->init(&_M_filebuf); }

      /**
       *  @brief  Create an input/output file stream.
       *  @param  s  Null terminated string specifying the filename.
       *  @param  mode  Open file in specified mode (see std::ios_base).
       *
       *  Tip:  When using std::string to hold the filename, you must use
       *  .c_str() before passing it to this constructor.
      */
      explicit
      basic_fstream(const char* __s,
		    ios_base::openmode __mode = ios_base::in | ios_base::out)
      : __iostream_type(NULL), _M_filebuf()
      {
	this->init(&_M_filebuf);
	this->open(__s, __mode);
      }

      /**
       *  @brief  The destructor does nothing.
       *
       *  The file is closed by the filebuf object, not the formatting
       *  stream.
      */
      ~basic_fstream()
      { }

      // Members:
      /**
       *  @brief  Accessing the underlying buffer.
       *  @return  The current basic_filebuf buffer.
       *
       *  This hides both signatures of std::basic_ios::rdbuf().
      */
      __filebuf_type*
      rdbuf() const
      { return const_cast<__filebuf_type*>(&_M_filebuf); }

      /**
       *  @brief  Wrapper to test for an open file.
       *  @return  @c rdbuf()->is_open()
      */
      bool
      is_open() { return _M_filebuf.is_open(); }

      /**
       *  @brief  Opens an external file.
       *  @param  s  The name of the file.
       *  @param  mode  The open mode flags.
       *
       *  Calls @c std::basic_filebuf::open(s,mode).  If that
       *  function fails, @c failbit is set in the stream's error state.
       *
       *  Tip:  When using std::string to hold the filename, you must use
       *  .c_str() before passing it to this constructor.
      */
      void
      open(const char* __s,
	   ios_base::openmode __mode = ios_base::in | ios_base::out)
      {
	if (!_M_filebuf.open(__s, __mode))
	  setstate(ios_base::failbit);
      }

      /**
       *  @brief  Close the file.
       *
       *  Calls @c std::basic_filebuf::close().  If that function
       *  fails, @c failbit is set in the stream's error state.
      */
      void
      close()
      {
	if (!_M_filebuf.close())
	  setstate(ios_base::failbit);
      }
    };
} // namespace std

#ifdef _GLIBCPP_NO_TEMPLATE_EXPORT
# define export
#endif
#ifdef  _GLIBCPP_FULLY_COMPLIANT_HEADERS
# include <bits/fstream.tcc>
#endif

#endif
