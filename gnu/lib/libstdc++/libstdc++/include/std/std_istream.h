// Input streams -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2001, 2002 Free Software Foundation, Inc.
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
// ISO C++ 14882: 27.6.1  Input streams
//

/** @file istream
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _CPP_ISTREAM
#define _CPP_ISTREAM	1

#pragma GCC system_header

#include <ios>
#include <limits> // For numeric_limits

namespace std
{
  // [27.6.1.1] Template class basic_istream
  /**
   *  @brief  Controlling input.
   *
   *  This is the base class for all input streams.  It provides text
   *  formatting of all builtin types, and communicates with any class
   *  derived from basic_streambuf to do the actual input.
  */
  template<typename _CharT, typename _Traits>
    class basic_istream : virtual public basic_ios<_CharT, _Traits>
    {
    public:
      // Types (inherited from basic_ios (27.4.4)):
      typedef _CharT                     		char_type;
      typedef typename _Traits::int_type 		int_type;
      typedef typename _Traits::pos_type 		pos_type;
      typedef typename _Traits::off_type 		off_type;
      typedef _Traits                    		traits_type;
      
      // Non-standard Types:
      typedef basic_streambuf<_CharT, _Traits> 		__streambuf_type;
      typedef basic_ios<_CharT, _Traits>		__ios_type;
      typedef basic_istream<_CharT, _Traits>		__istream_type;
      typedef istreambuf_iterator<_CharT, _Traits>	__istreambuf_iter;
      typedef num_get<_CharT, __istreambuf_iter>        __numget_type;
      typedef ctype<_CharT>           			__ctype_type;

      template<typename _CharT2, typename _Traits2>
        friend basic_istream<_CharT2, _Traits2>&
        operator>>(basic_istream<_CharT2, _Traits2>&, _CharT2&);
 
      template<typename _CharT2, typename _Traits2>
        friend basic_istream<_CharT2, _Traits2>&
        operator>>(basic_istream<_CharT2, _Traits2>&, _CharT2*);
 
    protected:
      // Data Members:
      /**
       *  @if maint
       *  The number of characters extracted in the previous unformatted
       *  function; see gcount().
       *  @endif
      */
      streamsize 		_M_gcount;

    public:
      // [27.6.1.1.1] constructor/destructor
      /**
       *  @brief  Base constructor.
       *
       *  This ctor is almost never called by the user directly, rather from
       *  derived classes' initialization lists, which pass a pointer to
       *  their own stream buffer.
      */
      explicit 
      basic_istream(__streambuf_type* __sb)
      { 
	this->init(__sb);
	_M_gcount = streamsize(0);
      }

      /**
       *  @brief  Base destructor.
       *
       *  This does very little apart from providing a virtual base dtor.
      */
      virtual 
      ~basic_istream() 
      { _M_gcount = streamsize(0); }

      // [27.6.1.1.2] prefix/suffix
      class sentry;
      friend class sentry;

      // [27.6.1.2] formatted input
      // [27.6.1.2.3] basic_istream::operator>>
      //@{
      /**
       *  @brief  Interface for manipulators.
       *
       *  Manuipulators such as @c std::ws and @c std::dec use these
       *  functions in constructs like "std::cin >> std::ws".  For more
       *  information, see the iomanip header.
      */
      __istream_type&
      operator>>(__istream_type& (*__pf)(__istream_type&));

      __istream_type&
      operator>>(__ios_type& (*__pf)(__ios_type&));

      __istream_type&
      operator>>(ios_base& (*__pf)(ios_base&));
      //@}
      
      // [27.6.1.2.2] arithmetic extractors
      /**
       *  @name Arithmetic Extractors
       *
       *  All the @c operator>> functions (aka <em>formatted input
       *  functions</em>) have some common behavior.  Each starts by
       *  constructing a temporary object of type std::basic_istream::sentry
       *  with the second argument (noskipws) set to false.  This has several
       *  effects, concluding with the setting of a status flag; see the
       *  sentry documentation for more.
       *
       *  If the sentry status is good, the function tries to extract
       *  whatever data is appropriate for the type of the argument.
       *
       *  If an exception is thrown during extraction, ios_base::badbit
       *  will be turned on in the stream's error state without causing an
       *  ios_base::failure to be thrown.  The original exception will then
       *  be rethrown.
      */
      //@{
      /**
       *  @brief  Basic arithmetic extractors
       *  @param  A variable of builtin type.
       *  @return  @c *this if successful
       *
       *  These functions use the stream's current locale (specifically, the
       *  @c num_get facet) to parse the input data.
      */
      __istream_type& 
      operator>>(bool& __n);
      
      __istream_type& 
      operator>>(short& __n);
      
      __istream_type& 
      operator>>(unsigned short& __n);

      __istream_type& 
      operator>>(int& __n);
      
      __istream_type& 
      operator>>(unsigned int& __n);

      __istream_type& 
      operator>>(long& __n);
      
      __istream_type& 
      operator>>(unsigned long& __n);

#ifdef _GLIBCPP_USE_LONG_LONG
      __istream_type& 
      operator>>(long long& __n);

      __istream_type& 
      operator>>(unsigned long long& __n);
#endif

      __istream_type& 
      operator>>(float& __f);

      __istream_type& 
      operator>>(double& __f);

      __istream_type& 
      operator>>(long double& __f);

      __istream_type& 
      operator>>(void*& __p);

      /**
       *  @brief  Extracting into another streambuf.
       *  @param  sb  A pointer to a streambuf
       *
       *  This function behaves like one of the basic arithmetic extractors,
       *  in that it also constructs a sentry onject and has the same error
       *  handling behavior.
       *
       *  If @a sb is NULL, the stream will set failbit in its error state.
       *
       *  Characters are extracted from this stream and inserted into the
       *  @a sb streambuf until one of the following occurs:
       *
       *  - the input stream reaches end-of-file,
       *  - insertion into the output buffer fails (in this case, the
       *    character that would have been inserted is not extracted), or
       *  - an exception occurs (and in this case is caught)
       *
       *  If the function inserts no characters, failbit is set.
      */
      __istream_type& 
      operator>>(__streambuf_type* __sb);
      //@}
      
      // [27.6.1.3] unformatted input
      /**
       *  @brief  Character counting
       *  @return  The number of characters extracted by the previous
       *           unformatted input function dispatched for this stream.
      */
      inline streamsize 
      gcount() const 
      { return _M_gcount; }
      
      /**
       *  @name Unformatted Input Functions
       *
       *  All the unformatted input functions have some common behavior.
       *  Each starts by constructing a temporary object of type
       *  std::basic_istream::sentry with the second argument (noskipws)
       *  set to true.  This has several effects, concluding with the
       *  setting of a status flag; see the sentry documentation for more.
       *
       *  If the sentry status is good, the function tries to extract
       *  whatever data is appropriate for the type of the argument.
       *
       *  The number of characters extracted is stored for later retrieval
       *  by gcount().
       *
       *  If an exception is thrown during extraction, ios_base::badbit
       *  will be turned on in the stream's error state without causing an
       *  ios_base::failure to be thrown.  The original exception will then
       *  be rethrown.
      */
      //@{
      /**
       *  @brief  Simple extraction.
       *  @return  A character, or eof().
       *
       *  Tries to extract a character.  If none are available, sets failbit
       *  and returns traits::eof().
      */
      int_type 
      get();

      /**
       *  @brief  Simple extraction.
       *  @param  c  The character in which to store data.
       *  @return  *this
       *
       *  Tries to extract a character and store it in @a c.  If none are
       *  available, sets failbit and returns traits::eof().
       *
       *  @note  This function is not overloaded on signed char and
       *         unsigned char.
      */
      __istream_type& 
      get(char_type& __c);

      /**
       *  @brief  Simple multiple-character extraction.
       *  @param  s  Pointer to an array.
       *  @param  n  Maximum number of characters to store in @a s.
       *  @param  delim  A "stop" character.
       *  @return  *this
       *
       *  Characters are extracted and stored into @a s until one of the
       *  following happens:
       *
       *  - @c n-1 characters are stored
       *  - the input sequence reaches EOF
       *  - the next character equals @a delim, in which case the character
       *    is not extracted
       *
       * If no characters are stored, failbit is set in the stream's error
       * state.
       *
       * In any case, a null character is stored into the next location in
       * the array.
       *
       *  @note  This function is not overloaded on signed char and
       *         unsigned char.
      */
      __istream_type& 
      get(char_type* __s, streamsize __n, char_type __delim);

      /**
       *  @brief  Simple multiple-character extraction.
       *  @param  s  Pointer to an array.
       *  @param  n  Maximum number of characters to store in @a s.
       *  @return  *this
       *
       *  Returns @c get(s,n,widen('\n')).
      */
      inline __istream_type& 
      get(char_type* __s, streamsize __n)
      { return this->get(__s, __n, this->widen('\n')); }

      /**
       *  @brief  Extraction into another streambuf.
       *  @param  sb  A streambuf in which to store data.
       *  @param  delim  A "stop" character.
       *  @return  *this
       *
       *  Characters are extracted and inserted into @a sb until one of the
       *  following happens:
       *
       *  - the input sequence reaches EOF
       *  - insertion into the output buffer fails (in this case, the
       *    character that would have been inserted is not extracted)
       *  - the next character equals @a delim (in this case, the character
       *    is not extracted)
       *  - an exception occurs (and in this case is caught)
       *
       * If no characters are stored, failbit is set in the stream's error
       * state.
      */
      __istream_type&
      get(__streambuf_type& __sb, char_type __delim);

      /**
       *  @brief  Extraction into another streambuf.
       *  @param  sb  A streambuf in which to store data.
       *  @return  *this
       *
       *  Returns @c get(sb,widen('\n')).
      */
      inline __istream_type&
      get(__streambuf_type& __sb)
      { return this->get(__sb, this->widen('\n')); }

      /**
       *  @brief  String extraction.
       *  @param  s  A character array in which to store the data.
       *  @param  n  Maximum number of characters to extract.
       *  @param  delim  A "stop" character.
       *  @return  *this
       *
       *  Extracts and stores characters into @a s until one of the
       *  following happens.  Note that these criteria are required to be
       *  tested in the order listed here, to allow an input line to exactly
       *  fill the @a s array without setting failbit.
       *
       *  -# the input sequence reaches end-of-file, in which case eofbit
       *     is set in the stream error state
       *  -# the next character equals @c delim, in which case the character
       *     is extracted (and therefore counted in @c gcount()) but not stored
       *  -# @c n-1 characters are stored, in which case failbit is set
       *     in the stream error state
       *
       *  If no characters are extracted, failbit is set.  (An empty line of
       *  input should therefore not cause failbit to be set.)
       *
       *  In any case, a null character is stored in the next location in
       *  the array.
      */
      __istream_type& 
      getline(char_type* __s, streamsize __n, char_type __delim);

      /**
       *  @brief  String extraction.
       *  @param  s  A character array in which to store the data.
       *  @param  n  Maximum number of characters to extract.
       *  @return  *this
       *
       *  Returns @c getline(s,n,widen('\n')).
      */
      inline __istream_type& 
      getline(char_type* __s, streamsize __n)
      { return this->getline(__s, __n, this->widen('\n')); }

      /**
       *  @brief  Discarding characters
       *  @param  n  Number of characters to discard.
       *  @param  delim  A "stop" character.
       *  @return  *this
       *
       *  Extracts characters and throws them away until one of the
       *  following happens:
       *  - if @a n @c != @c std::numeric_limits<int>::max(), @a n
       *    characters are extracted
       *  - the input sequence reaches end-of-file
       *  - the next character equals @a delim (in this case, the character
       *    is extracted); note that this condition will never occur if
       *    @a delim equals @c traits::eof().
      */
      __istream_type& 
      ignore(streamsize __n = 1, int_type __delim = traits_type::eof());
      
      /**
       *  @brief  Looking ahead in the stream
       *  @return  The next character, or eof().
       *
       *  If, after constructing the sentry object, @c good() is false,
       *  returns @c traits::eof().  Otherwise reads but does not extract
       *  the next input character.
      */
      int_type 
      peek();
      
      /**
       *  @brief  Extraction without delimiters.
       *  @param  s  A character array.
       *  @param  n  Maximum number of characters to store.
       *  @return  *this
       *
       *  If the stream state is @c good(), extracts characters and stores
       *  them into @a s until one of the following happens:
       *  - @a n characters are stored
       *  - the input sequence reaches end-of-file, in which case the error
       *    state is set to @c failbit|eofbit.
       *
       *  @note  This function is not overloaded on signed char and
       *         unsigned char.
      */
      __istream_type& 
      read(char_type* __s, streamsize __n);

      /**
       *  @brief  Extraction until the buffer is exhausted, but no more.
       *  @param  s  A character array.
       *  @param  n  Maximum number of characters to store.
       *  @return  The number of characters extracted.
       *
       *  Extracts characters and stores them into @a s depending on the
       *  number of characters remaining in the streambuf's buffer,
       *  @c rdbuf()->in_avail(), called @c A here:
       *  - if @c A @c == @c -1, sets eofbit and extracts no characters
       *  - if @c A @c == @c 0, extracts no characters
       *  - if @c A @c > @c 0, extracts @c min(A,n)
       *
       *  The goal is to empty the current buffer, and to not request any
       *  more from the external input sequence controlled by the streambuf.
      */
      streamsize 
      readsome(char_type* __s, streamsize __n);
      
      /**
       *  @brief  Unextracting a single character.
       *  @param  c  The character to push back into the input stream.
       *  @return  *this
       *
       *  If @c rdbuf() is not null, calls @c rdbuf()->sputbackc(c).
       *
       *  If @c rdbuf() is null or if @c sputbackc() fails, sets badbit in
       *  the error state.
       *
       *  @note  Since no characters are extracted, the next call to
       *         @c gcount() will return 0, as required by DR 60.
      */
      __istream_type& 
      putback(char_type __c);

      /**
       *  @brief  Unextracting the previous character.
       *  @return  *this
       *
       *  If @c rdbuf() is not null, calls @c rdbuf()->sungetc(c).
       *
       *  If @c rdbuf() is null or if @c sungetc() fails, sets badbit in
       *  the error state.
       *
       *  @note  Since no characters are extracted, the next call to
       *         @c gcount() will return 0, as required by DR 60.
      */
      __istream_type& 
      unget();

      /**
       *  @brief  Synchronizing the stream buffer.
       *  @return  0 on success, -1 on failure
       *
       *  If @c rdbuf() is a null pointer, returns -1.
       *
       *  Otherwise, calls @c rdbuf()->pubsync(), and if that returns -1,
       *  sets badbit and returns -1.
       *
       *  Otherwise, returns 0.
       *
       *  @note  This function does not count the number of characters
       *         extracted, if any, and therefore does not affect the next
       *         call to @c gcount().
      */
      int 
      sync();

      /**
       *  @brief  Getting the current read position.
       *  @return  A file position object.
       *
       *  If @c fail() is not false, returns @c pos_type(-1) to indicate
       *  failure.  Otherwise returns @c rdbuf()->pubseekoff(0,cur,in).
       *
       *  @note  This function does not count the number of characters
       *         extracted, if any, and therefore does not affect the next
       *         call to @c gcount().
      */
      pos_type 
      tellg();

      /**
       *  @brief  Changing the current read position.
       *  @param  pos  A file position object.
       *  @return  *this
       *
       *  If @c fail() is not true, calls @c rdbuf()->pubseekpos(pos).  If
       *  that function fails, sets failbit.
       *
       *  @note  This function does not count the number of characters
       *         extracted, if any, and therefore does not affect the next
       *         call to @c gcount().
      */
      __istream_type& 
      seekg(pos_type);

      /**
       *  @brief  Changing the current read position.
       *  @param  off  A file offset object.
       *  @param  dir  The direction in which to seek.
       *  @return  *this
       *
       *  If @c fail() is not true, calls @c rdbuf()->pubseekoff(off,dir).
       *  If that function fails, sets failbit.
       *
       *  @note  This function does not count the number of characters
       *         extracted, if any, and therefore does not affect the next
       *         call to @c gcount().
      */
      __istream_type& 
      seekg(off_type, ios_base::seekdir);
      //@}
    };
  
  /**
   *  @brief  Performs setup work for input streams.
   *
   *  Objects of this class are created before all of the standard
   *  extractors are run.  It is responsible for "exception-safe prefix and
   *  suffix operations," although only prefix actions are currently required
   *  by the standard.  Additional actions may be added by the
   *  implementation, and we list them in
   *  http://gcc.gnu.org/onlinedocs/libstdc++/17_intro/howto.html#5
   *  under [27.6] notes.
  */
  template<typename _CharT, typename _Traits>
    class basic_istream<_CharT, _Traits>::sentry
    {
    public:
      /// Easy access to dependant types.
      typedef _Traits 					traits_type;
      typedef basic_streambuf<_CharT, _Traits> 		__streambuf_type;
      typedef basic_istream<_CharT, _Traits> 		__istream_type;
      typedef typename __istream_type::__ctype_type 	__ctype_type;
      typedef typename _Traits::int_type		__int_type;

      /**
       *  @brief  The constructor performs all the work.
       *  @param  is  The input stream to guard.
       *  @param  noskipws  Whether to consume whitespace or not.
       *
       *  If the stream state is good (@a is.good() is true), then the
       *  following actions are performed, otherwise the sentry state is
       *  false ("not okay") and failbit is set in the stream state.
       *
       *  The sentry's preparatory actions are:
       *
       *  -# if the stream is tied to an output stream, @c is.tie()->flush()
       *     is called to synchronize the output sequence
       *  -# if @a noskipws is false, and @c ios_base::skipws is set in
       *     @c is.flags(), the sentry extracts and discards whitespace
       *     characters from the stream.  The currently imbued locale is
       *     used to determine whether each character is whitespace.
       *
       *  If the stream state is still good, then the sentry state becomes
       *  true ("okay").
      */
      explicit 
      sentry(basic_istream<_CharT, _Traits>& __is, bool __noskipws = false);

      /**
       *  @brief  Quick status checking.
       *  @return  The sentry state.
       *
       *  For ease of use, sentries may be converted to booleans.  The
       *  return value is that of the sentry state (true == okay).
      */
      operator bool() { return _M_ok; }

    private:
      bool _M_ok;
    };

  // [27.6.1.2.3] character extraction templates
  //@{
  /**
   *  @brief  Character extractors
   *  @param  in  An input stream.
   *  @param  c  A character reference.
   *  @return  in
   *
   *  Behaves like one of the formatted arithmetic extractors described in
   *  std::basic_istream.  After constructing a sentry object with good
   *  status, this function extracts a character (if one is available) and
   *  stores it in @a c.  Otherwise, sets failbit in the input stream.
  */
  template<typename _CharT, typename _Traits>
    basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __in, _CharT& __c);

  template<class _Traits>
    basic_istream<char, _Traits>&
    operator>>(basic_istream<char, _Traits>& __in, unsigned char& __c)
    { return (__in >> reinterpret_cast<char&>(__c)); }

  template<class _Traits>
    basic_istream<char, _Traits>&
    operator>>(basic_istream<char, _Traits>& __in, signed char& __c)
    { return (__in >> reinterpret_cast<char&>(__c)); }
  //@}

  //@{
  /**
   *  @brief  Character string extractors
   *  @param  in  An input stream.
   *  @param  s  A pointer to a character array.
   *  @return  in
   *
   *  Behaves like one of the formatted arithmetic extractors described in
   *  std::basic_istream.  After constructing a sentry object with good
   *  status, this function extracts up to @c n characters and stores them
   *  into the array starting at @a s.  @c n is defined as:
   *
   *  - if @c width() is greater than zero, @c n is width()
   *  - otherwise @c n is "the number of elements of the largest array of
   *    @c char_type that can store a terminating @c eos." [27.6.1.2.3]/6
   *
   *  Characters are extracted and stored until one of the following happens:
   *  - @c n-1 characters are stored
   *  - EOF is reached
   *  - the next character is whitespace according to the current locale
   *  - the next character is a null byte (i.e., @c charT() )
   *
   *  @c width(0) is then called for the input stream.
   *
   *  If no characters are extracted, sets failbit.
  */
  template<typename _CharT, typename _Traits>
    basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __in, _CharT* __s);
  
  template<class _Traits>
    basic_istream<char,_Traits>&
    operator>>(basic_istream<char,_Traits>& __in, unsigned char* __s)
    { return (__in >> reinterpret_cast<char*>(__s)); }

  template<class _Traits>
    basic_istream<char,_Traits>&
    operator>>(basic_istream<char,_Traits>& __in, signed char* __s)
    { return (__in >> reinterpret_cast<char*>(__s)); }
  //@}

  // 27.6.1.5 Template class basic_iostream
  /**
   *  @brief  Merging istream and ostream capabilities.
   *
   *  This class multiply inherits from the input and output stream classes
   *  simply to provide a single interface.
  */
  template<typename _CharT, typename _Traits>
    class basic_iostream
    : public basic_istream<_CharT, _Traits>, 
      public basic_ostream<_CharT, _Traits>
    {
    public:
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
// 271. basic_iostream missing typedefs
      // Types (inherited):
      typedef _CharT                     		char_type;
      typedef typename _Traits::int_type 		int_type;
      typedef typename _Traits::pos_type 		pos_type;
      typedef typename _Traits::off_type 		off_type;
      typedef _Traits                    		traits_type;
#endif

      // Non-standard Types:
      typedef basic_istream<_CharT, _Traits>		__istream_type;
      typedef basic_ostream<_CharT, _Traits>		__ostream_type;

      /**
       *  @brief  Constructor does nothing.
       *
       *  Both of the parent classes are initialized with the same
       *  streambuf pointer passed to this constructor.
      */
      explicit 
      basic_iostream(basic_streambuf<_CharT, _Traits>* __sb)
      : __istream_type(__sb), __ostream_type(__sb)
      { }

      /**
       *  @brief  Destructor does nothing.
      */
      virtual 
      ~basic_iostream() { }
    };

  // [27.6.1.4] standard basic_istream manipulators
  /**
   *  @brief  Quick and easy way to eat whitespace
   *
   *  This manipulator extracts whitespace characters, stopping when the
   *  next character is non-whitespace, or when the input sequence is empty.
   *  If the sequence is empty, @c eofbit is set in the stream, but not
   *  @c failbit.
   *
   *  The current locale is used to distinguish whitespace characters.
   *
   *  Example:
   *  @code
   *     MyClass   mc;
   *
   *     std::cin >> std::ws >> mc;
   *  @endcode
   *  will skip leading whitespace before calling operator>> on cin and your
   *  object.  Note that the same effect can be achieved by creating a
   *  std::basic_istream::sentry inside your definition of operator>>.
  */
  template<typename _CharT, typename _Traits>
    basic_istream<_CharT, _Traits>& 
    ws(basic_istream<_CharT, _Traits>& __is);
} // namespace std

#ifdef _GLIBCPP_NO_TEMPLATE_EXPORT
# define export
#endif
#ifdef  _GLIBCPP_FULLY_COMPLIANT_HEADERS
# include <bits/istream.tcc>
#endif

#endif	/* _CPP_ISTREAM */
