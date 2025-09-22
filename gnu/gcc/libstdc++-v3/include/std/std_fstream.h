// File based streams -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
// 2006, 2007
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

/** @file fstream
 *  This is a Standard C++ Library header.
 */

//
// ISO C++ 14882: 27.8  File-based streams
//

#ifndef _GLIBCXX_FSTREAM
#define _GLIBCXX_FSTREAM 1

#pragma GCC system_header

#include <istream>
#include <ostream>
#include <locale>	// For codecvt
#include <cstdio>       // For SEEK_SET, SEEK_CUR, SEEK_END, BUFSIZ     
#include <bits/basic_file.h>
#include <bits/gthr.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // [27.8.1.1] template class basic_filebuf
  /**
   *  @brief  The actual work of input and output (for files).
   *
   *  This class associates both its input and output sequence with an
   *  external disk file, and maintains a joint file position for both
   *  sequences.  Many of its sematics are described in terms of similar
   *  behavior in the Standard C Library's @c FILE streams.
  */
  // Requirements on traits_type, specific to this class:
  // traits_type::pos_type must be fpos<traits_type::state_type>
  // traits_type::off_type must be streamoff
  // traits_type::state_type must be Assignable and DefaultConstructable,
  // and traits_type::state_type() must be the initial state for codecvt.
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

      typedef basic_streambuf<char_type, traits_type>  	__streambuf_type;
      typedef basic_filebuf<char_type, traits_type>     __filebuf_type;
      typedef __basic_file<char>		        __file_type;
      typedef typename traits_type::state_type          __state_type;
      typedef codecvt<char_type, char, __state_type>    __codecvt_type;

      friend class ios_base; // For sync_with_stdio.

    protected:
      // Data Members:
      // MT lock inherited from libio or other low-level io library.
      __c_lock          	_M_lock;

      // External buffer.
      __file_type 		_M_file;

      /**
       *  @if maint
       *  Place to stash in || out || in | out settings for current filebuf.
       *  @endif
      */
      ios_base::openmode 	_M_mode;

      // Beginning state type for codecvt.
      __state_type 		_M_state_beg;

      // During output, the state that corresponds to pptr(),
      // during input, the state that corresponds to egptr() and
      // _M_ext_next.
      __state_type		_M_state_cur;

      // Not used for output. During input, the state that corresponds
      // to eback() and _M_ext_buf.
      __state_type		_M_state_last;

      /**
       *  @if maint
       *  Pointer to the beginning of internal buffer.
       *  @endif
      */
      char_type*		_M_buf; 	

      /**
       *  @if maint
       *  Actual size of internal buffer. This number is equal to the size
       *  of the put area + 1 position, reserved for the overflow char of
       *  a full area.
       *  @endif
      */
      size_t			_M_buf_size;

      // Set iff _M_buf is allocated memory from _M_allocate_internal_buffer.
      bool			_M_buf_allocated;

      /**
       *  @if maint
       *  _M_reading == false && _M_writing == false for 'uncommitted' mode;  
       *  _M_reading == true for 'read' mode;
       *  _M_writing == true for 'write' mode;
       *
       *  NB: _M_reading == true && _M_writing == true is unused.
       *  @endif
      */ 
      bool                      _M_reading;
      bool                      _M_writing;

      //@{
      /**
       *  @if maint
       *  Necessary bits for putback buffer management.
       *
       *  @note pbacks of over one character are not currently supported.
       *  @endif
      */
      char_type			_M_pback; 
      char_type*		_M_pback_cur_save;
      char_type*		_M_pback_end_save;
      bool			_M_pback_init; 
      //@}

      // Cached codecvt facet.
      const __codecvt_type* 	_M_codecvt;

      /**
       *  @if maint
       *  Buffer for external characters. Used for input when
       *  codecvt::always_noconv() == false. When valid, this corresponds
       *  to eback().
       *  @endif
      */ 
      char*			_M_ext_buf;

      /**
       *  @if maint
       *  Size of buffer held by _M_ext_buf.
       *  @endif
      */ 
      streamsize		_M_ext_buf_size;

      /**
       *  @if maint
       *  Pointers into the buffer held by _M_ext_buf that delimit a
       *  subsequence of bytes that have been read but not yet converted.
       *  When valid, _M_ext_next corresponds to egptr().
       *  @endif
      */ 
      const char*		_M_ext_next;
      char*			_M_ext_end;

      /**
       *  @if maint
       *  Initializes pback buffers, and moves normal buffers to safety.
       *  Assumptions:
       *  _M_in_cur has already been moved back
       *  @endif
      */
      void
      _M_create_pback()
      {
	if (!_M_pback_init)
	  {
	    _M_pback_cur_save = this->gptr();
	    _M_pback_end_save = this->egptr();
	    this->setg(&_M_pback, &_M_pback, &_M_pback + 1);
	    _M_pback_init = true;
	  }
      }

      /**
       *  @if maint
       *  Deactivates pback buffer contents, and restores normal buffer.
       *  Assumptions:
       *  The pback buffer has only moved forward.
       *  @endif
      */ 
      void
      _M_destroy_pback() throw()
      {
	if (_M_pback_init)
	  {
	    // Length _M_in_cur moved in the pback buffer.
	    _M_pback_cur_save += this->gptr() != this->eback();
	    this->setg(_M_buf, _M_pback_cur_save, _M_pback_end_save);
	    _M_pback_init = false;
	  }
      }

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
      { this->close(); }

      // Members:
      /**
       *  @brief  Returns true if the external file is open.
      */
      bool
      is_open() const throw()
      { return _M_file.is_open(); }

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
       *  Table 92, adapted here, gives the relation between openmode
       *  combinations and the equivalent fopen() flags.
       *  (NB: lines in|out|app and binary|in|out|app per DR 596)
       *  +---------------------------------------------------------+
       *  | ios_base Flag combination            stdio equivalent   |
       *  |binary  in  out  trunc  app                              |
       *  +---------------------------------------------------------+
       *  |             +                        "w"                |
       *  |             +           +            "a"                |
       *  |             +     +                  "w"                |
       *  |         +                            "r"                |
       *  |         +   +                        "r+"               |
       *  |         +   +     +                  "w+"               |
       *  |         +   +           +            "a+"               |
       *  +---------------------------------------------------------+
       *  |   +         +                        "wb"               |
       *  |   +         +           +            "ab"               |
       *  |   +         +     +                  "wb"               |
       *  |   +     +                            "rb"               |
       *  |   +     +   +                        "r+b"              |
       *  |   +     +   +     +                  "w+b"              |
       *  |   +     +   +           +            "a+b"              |
       *  +---------------------------------------------------------+
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
      void
      _M_allocate_internal_buffer();

      void
      _M_destroy_internal_buffer() throw();

      // [27.8.1.4] overridden virtual functions
      virtual streamsize
      showmanyc();

      // Stroustrup, 1998, p. 628
      // underflow() and uflow() functions are called to get the next
      // charater from the real input source when the buffer is empty.
      // Buffered input uses underflow()

      virtual int_type
      underflow();

      virtual int_type
      pbackfail(int_type __c = _Traits::eof());

      // Stroustrup, 1998, p 648
      // The overflow() function is called to transfer characters to the
      // real output destination when the buffer is full. A call to
      // overflow(c) outputs the contents of the buffer plus the
      // character c.
      // 27.5.2.4.5
      // Consume some sequence of the characters in the pending sequence.
      virtual int_type
      overflow(int_type __c = _Traits::eof());

      // Convert internal byte sequence to external, char-based
      // sequence via codecvt.
      bool
      _M_convert_to_external(char_type*, streamsize);

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

      virtual pos_type
      seekoff(off_type __off, ios_base::seekdir __way,
	      ios_base::openmode __mode = ios_base::in | ios_base::out);

      virtual pos_type
      seekpos(pos_type __pos,
	      ios_base::openmode __mode = ios_base::in | ios_base::out);

      // Common code for seekoff and seekpos
      pos_type
      _M_seek(off_type __off, ios_base::seekdir __way, __state_type __state);

      virtual int
      sync();

      virtual void
      imbue(const locale& __loc);

      virtual streamsize
      xsgetn(char_type* __s, streamsize __n);

      virtual streamsize
      xsputn(const char_type* __s, streamsize __n);

      // Flushes output buffer, then writes unshift sequence.
      bool
      _M_terminate_output();

      /**
       *  @if maint 
       *  This function sets the pointers of the internal buffer, both get
       *  and put areas. Typically:
       *
       *   __off == egptr() - eback() upon underflow/uflow ('read' mode);
       *   __off == 0 upon overflow ('write' mode);
       *   __off == -1 upon open, setbuf, seekoff/pos ('uncommitted' mode).
       * 
       *  NB: epptr() - pbase() == _M_buf_size - 1, since _M_buf_size
       *  reflects the actual allocated memory and the last cell is reserved
       *  for the overflow char of a full put area.
       *  @endif
      */
      void
      _M_set_buffer(streamsize __off)
      {
 	const bool __testin = _M_mode & ios_base::in;
 	const bool __testout = _M_mode & ios_base::out;
	
	if (__testin && __off > 0)
	  this->setg(_M_buf, _M_buf, _M_buf + __off);
	else
	  this->setg(_M_buf, _M_buf, _M_buf);

	if (__testout && __off == 0 && _M_buf_size > 1 )
	  this->setp(_M_buf, _M_buf + _M_buf_size - 1);
	else
	  this->setp(NULL, NULL);
      }
    };

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
      basic_ifstream() : __istream_type(), _M_filebuf()
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
      : __istream_type(), _M_filebuf()
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
      is_open()
      { return _M_filebuf.is_open(); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 365. Lack of const-qualification in clause 27
      bool
      is_open() const
      { return _M_filebuf.is_open(); }

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
	else
	  // _GLIBCXX_RESOLVE_LIB_DEFECTS
	  // 409. Closing an fstream should clear error state
	  this->clear();
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
      basic_ofstream(): __ostream_type(), _M_filebuf()
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
      : __ostream_type(), _M_filebuf()
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
      is_open()
      { return _M_filebuf.is_open(); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 365. Lack of const-qualification in clause 27
      bool
      is_open() const
      { return _M_filebuf.is_open(); }

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
	else
	  // _GLIBCXX_RESOLVE_LIB_DEFECTS
	  // 409. Closing an fstream should clear error state
	  this->clear();
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
      : __iostream_type(), _M_filebuf()
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
      is_open()
      { return _M_filebuf.is_open(); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 365. Lack of const-qualification in clause 27
      bool
      is_open() const
      { return _M_filebuf.is_open(); }

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
	  this->setstate(ios_base::failbit);
	else
	  // _GLIBCXX_RESOLVE_LIB_DEFECTS
	  // 409. Closing an fstream should clear error state
	  this->clear();
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

_GLIBCXX_END_NAMESPACE

#ifndef _GLIBCXX_EXPORT_TEMPLATE
# include <bits/fstream.tcc>
#endif

#endif /* _GLIBCXX_FSTREAM */
