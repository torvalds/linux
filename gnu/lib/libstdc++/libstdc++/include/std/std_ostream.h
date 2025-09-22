// Output streams -*- C++ -*-

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
// ISO C++ 14882: 27.6.2  Output streams
//

/** @file ostream
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _CPP_OSTREAM
#define _CPP_OSTREAM	1

#pragma GCC system_header

#include <ios>

namespace std
{
  // [27.6.2.1] Template class basic_ostream
  /**
   *  @brief  Controlling output.
   *
   *  This is the base class for all output streams.  It provides text
   *  formatting of all builtin types, and communicates with any class
   *  derived from basic_streambuf to do the actual output.
  */
  template<typename _CharT, typename _Traits>
    class basic_ostream : virtual public basic_ios<_CharT, _Traits>
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
      typedef basic_ostream<_CharT, _Traits>		__ostream_type;
      typedef ostreambuf_iterator<_CharT, _Traits>	__ostreambuf_iter;
      typedef num_put<_CharT, __ostreambuf_iter>        __numput_type;
      typedef ctype<_CharT>           			__ctype_type;

      template<typename _CharT2, typename _Traits2>
        friend basic_ostream<_CharT2, _Traits2>&
        operator<<(basic_ostream<_CharT2, _Traits2>&, _CharT2);
 
      template<typename _Traits2>
        friend basic_ostream<char, _Traits2>&
        operator<<(basic_ostream<char, _Traits2>&, char);
 
      template<typename _CharT2, typename _Traits2>
        friend basic_ostream<_CharT2, _Traits2>&
        operator<<(basic_ostream<_CharT2, _Traits2>&, const _CharT2*);
 
      template<typename _Traits2>
        friend basic_ostream<char, _Traits2>&
        operator<<(basic_ostream<char, _Traits2>&, const char*);
 
      template<typename _CharT2, typename _Traits2>
        friend basic_ostream<_CharT2, _Traits2>&
        operator<<(basic_ostream<_CharT2, _Traits2>&, const char*);

      // [27.6.2.2] constructor/destructor
      /**
       *  @brief  Base constructor.
       *
       *  This ctor is almost never called by the user directly, rather from
       *  derived classes' initialization lists, which pass a pointer to
       *  their own stream buffer.
      */
      explicit 
      basic_ostream(__streambuf_type* __sb)
      { this->init(__sb); }

      /**
       *  @brief  Base destructor.
       *
       *  This does very little apart from providing a virtual base dtor.
      */
      virtual 
      ~basic_ostream() { }

      // [27.6.2.3] prefix/suffix
      class sentry;
      friend class sentry;
      
      // [27.6.2.5] formatted output
      // [27.6.2.5.3]  basic_ostream::operator<<
      //@{
      /**
       *  @brief  Interface for manipulators.
       *
       *  Manuipulators such as @c std::endl and @c std::hex use these
       *  functions in constructs like "std::cout << std::endl".  For more
       *  information, see the iomanip header.
      */
      __ostream_type&
      operator<<(__ostream_type& (*__pf)(__ostream_type&));
      
      __ostream_type&
      operator<<(__ios_type& (*__pf)(__ios_type&));
      
      __ostream_type&
      operator<<(ios_base& (*__pf) (ios_base&));
      //@}

      // [27.6.2.5.2] arithmetic inserters
      /**
       *  @name Arithmetic Inserters
       *
       *  All the @c operator<< functions (aka <em>formatted output
       *  functions</em>) have some common behavior.  Each starts by
       *  constructing a temporary object of type std::basic_ostream::sentry.
       *  This can have several effects, concluding with the setting of a
       *  status flag; see the sentry documentation for more.
       *
       *  If the sentry status is good, the function tries to generate
       *  whatever data is appropriate for the type of the argument.
       *
       *  If an exception is thrown during insertion, ios_base::badbit
       *  will be turned on in the stream's error state without causing an
       *  ios_base::failure to be thrown.  The original exception will then
       *  be rethrown.
      */
      //@{
      /**
       *  @brief  Basic arithmetic inserters
       *  @param  A variable of builtin type.
       *  @return  @c *this if successful
       *
       *  These functions use the stream's current locale (specifically, the
       *  @c num_get facet) to perform numeric formatting.
      */
      __ostream_type& 
      operator<<(long __n);
      
      __ostream_type& 
      operator<<(unsigned long __n);

      __ostream_type& 
      operator<<(bool __n);

      __ostream_type& 
      operator<<(short __n)
      { 
	ios_base::fmtflags __fmt = this->flags() & ios_base::basefield;
	if (__fmt & ios_base::oct || __fmt & ios_base::hex)
	  return this->operator<<(static_cast<unsigned long>
				  (static_cast<unsigned short>(__n)));
	else
	  return this->operator<<(static_cast<long>(__n));
      }

      __ostream_type& 
      operator<<(unsigned short __n)
      { return this->operator<<(static_cast<unsigned long>(__n)); }

      __ostream_type& 
      operator<<(int __n)
      { 
	ios_base::fmtflags __fmt = this->flags() & ios_base::basefield;
	if (__fmt & ios_base::oct || __fmt & ios_base::hex)
	  return this->operator<<(static_cast<unsigned long>
				  (static_cast<unsigned int>(__n)));
	else
	  return this->operator<<(static_cast<long>(__n));
      }

      __ostream_type& 
      operator<<(unsigned int __n)
      { return this->operator<<(static_cast<unsigned long>(__n)); }

#ifdef _GLIBCPP_USE_LONG_LONG
      __ostream_type& 
      operator<<(long long __n);

      __ostream_type& 
      operator<<(unsigned long long __n);
#endif

      __ostream_type& 
      operator<<(double __f);

      __ostream_type& 
      operator<<(float __f)
      { return this->operator<<(static_cast<double>(__f)); }

      __ostream_type& 
      operator<<(long double __f);

      __ostream_type& 
      operator<<(const void* __p);

      /**
       *  @brief  Extracting from another streambuf.
       *  @param  sb  A pointer to a streambuf
       *
       *  This function behaves like one of the basic arithmetic extractors,
       *  in that it also constructs a sentry onject and has the same error
       *  handling behavior.
       *
       *  If @a sb is NULL, the stream will set failbit in its error state.
       *
       *  Characters are extracted from @a sb and inserted into @c *this
       *  until one of the following occurs:
       *
       *  - the input stream reaches end-of-file,
       *  - insertion into the output sequence fails (in this case, the
       *    character that would have been inserted is not extracted), or
       *  - an exception occurs while getting a character from @a sb, which
       *    sets failbit in the error state
       *
       *  If the function inserts no characters, failbit is set.
      */
      __ostream_type& 
      operator<<(__streambuf_type* __sb);
      //@}

      // [27.6.2.6] unformatted output functions
      /**
       *  @name Unformatted Output Functions
       *
       *  All the unformatted output functions have some common behavior.
       *  Each starts by constructing a temporary object of type
       *  std::basic_ostream::sentry.  This has several effects, concluding
       *  with the setting of a status flag; see the sentry documentation
       *  for more.
       *
       *  If the sentry status is good, the function tries to generate
       *  whatever data is appropriate for the type of the argument.
       *
       *  If an exception is thrown during insertion, ios_base::badbit
       *  will be turned on in the stream's error state.  If badbit is on in
       *  the stream's exceptions mask, the exception will be rethrown
       *  without completing its actions.
      */
      //@{
      /**
       *  @brief  Simple insertion.
       *  @param  c  The character to insert.
       *  @return  *this
       *
       *  Tries to insert @a c.
       *
       *  @note  This function is not overloaded on signed char and
       *         unsigned char.
      */
      __ostream_type& 
      put(char_type __c);

      /**
       *  @brief  Character string insertion.
       *  @param  s  The array to insert.
       *  @param  n  Maximum number of characters to insert.
       *  @return  *this
       *
       *  Characters are copied from @a s and inserted into the stream until
       *  one of the following happens:
       *
       *  - @a n characters are inserted
       *  - inserting into the output sequence fails (in this case, badbit
       *    will be set in the stream's error state)
       *
       *  @note  This function is not overloaded on signed char and
       *         unsigned char.
      */
      __ostream_type& 
      write(const char_type* __s, streamsize __n);
      //@}

      /**
       *  @brief  Synchronizing the stream buffer.
       *  @return  *this
       *
       *  If @c rdbuf() is a null pointer, changes nothing.
       *
       *  Otherwise, calls @c rdbuf()->pubsync(), and if that returns -1,
       *  sets badbit.
      */
      __ostream_type& 
      flush();

      // [27.6.2.4] seek members
      /**
       *  @brief  Getting the current write position.
       *  @return  A file position object.
       *
       *  If @c fail() is not false, returns @c pos_type(-1) to indicate
       *  failure.  Otherwise returns @c rdbuf()->pubseekoff(0,cur,out).
      */
      pos_type 
      tellp();

      /**
       *  @brief  Changing the current write position.
       *  @param  pos  A file position object.
       *  @return  *this
       *
       *  If @c fail() is not true, calls @c rdbuf()->pubseekpos(pos).  If
       *  that function fails, sets failbit.
      */
      __ostream_type& 
      seekp(pos_type);

      /**
       *  @brief  Changing the current write position.
       *  @param  off  A file offset object.
       *  @param  dir  The direction in which to seek.
       *  @return  *this
       *
       *  If @c fail() is not true, calls @c rdbuf()->pubseekoff(off,dir).
       *  If that function fails, sets failbit.
      */
       __ostream_type& 
      seekp(off_type, ios_base::seekdir);
    };

  /**
   *  @brief  Performs setup work for output streams.
   *
   *  Objects of this class are created before all of the standard
   *  inserters are run.  It is responsible for "exception-safe prefix and
   *  suffix operations."  Additional actions may be added by the
   *  implementation, and we list them in
   *  http://gcc.gnu.org/onlinedocs/libstdc++/17_intro/howto.html#5
   *  under [27.6] notes.
  */
  template <typename _CharT, typename _Traits>
    class basic_ostream<_CharT, _Traits>::sentry
    {
      // Data Members:
      bool 				_M_ok;
      basic_ostream<_CharT,_Traits>& 	_M_os;
      
    public:
      /**
       *  @brief  The constructor performs preparatory work.
       *  @param  os  The output stream to guard.
       *
       *  If the stream state is good (@a os.good() is true), then if the
       *  stream is tied to another output stream, @c is.tie()->flush()
       *  is called to synchronize the output sequences.
       *
       *  If the stream state is still good, then the sentry state becomes
       *  true ("okay").
      */
      explicit
      sentry(basic_ostream<_CharT,_Traits>& __os);

      /**
       *  @brief  Possibly flushes the stream.
       *
       *  If @c ios_base::unitbuf is set in @c os.flags(), and
       *  @c std::uncaught_exception() is true, the sentry destructor calls
       *  @c flush() on the output stream.
      */
      ~sentry()
      {
	// XXX MT
	if (_M_os.flags() & ios_base::unitbuf && !uncaught_exception())
	  {
	    // Can't call flush directly or else will get into recursive lock.
	    if (_M_os.rdbuf() && _M_os.rdbuf()->pubsync() == -1)
	      _M_os.setstate(ios_base::badbit);
	  }
      }

      /**
       *  @brief  Quick status checking.
       *  @return  The sentry state.
       *
       *  For ease of use, sentries may be converted to booleans.  The
       *  return value is that of the sentry state (true == okay).
      */
      operator bool() 
      { return _M_ok; }
    };

  // [27.6.2.5.4] character insertion templates
  //@{
  /**
   *  @brief  Character inserters
   *  @param  out  An output stream.
   *  @param  c  A character.
   *  @return  out
   *
   *  Behaves like one of the formatted arithmetic inserters described in
   *  std::basic_ostream.  After constructing a sentry object with good
   *  status, this function inserts a single character and any required
   *  padding (as determined by [22.2.2.2.2]).  @c out.width(0) is then
   *  called.
   *
   *  If @a c is of type @c char and the character type of the stream is not
   *  @c char, the character is widened before insertion.
  */
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, _CharT __c);

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, char __c)
    { return (__out << __out.widen(__c)); }

  // Specialization
  template <class _Traits> 
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, char __c);

  // Signed and unsigned
  template<class _Traits>
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, signed char __c)
    { return (__out << static_cast<char>(__c)); }
  
  template<class _Traits>
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, unsigned char __c)
    { return (__out << static_cast<char>(__c)); }
  //@}
  
  //@{
  /**
   *  @brief  String inserters
   *  @param  out  An output stream.
   *  @param  s  A character string.
   *  @return  out
   *  @pre  @a s must be a non-NULL pointer
   *
   *  Behaves like one of the formatted arithmetic inserters described in
   *  std::basic_ostream.  After constructing a sentry object with good
   *  status, this function inserts @c traits::length(s) characters starting
   *  at @a s, widened if necessary, followed by any required padding (as
   *  determined by [22.2.2.2.2]).  @c out.width(0) is then called.
  */
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, const _CharT* __s);

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits> &
    operator<<(basic_ostream<_CharT, _Traits>& __out, const char* __s);

  // Partial specializationss
  template<class _Traits>
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, const char* __s);
 
  // Signed and unsigned
  template<class _Traits>
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, const signed char* __s)
    { return (__out << reinterpret_cast<const char*>(__s)); }

  template<class _Traits>
    basic_ostream<char, _Traits> &
    operator<<(basic_ostream<char, _Traits>& __out, const unsigned char* __s)
    { return (__out << reinterpret_cast<const char*>(__s)); }
  //@}

  // [27.6.2.7] standard basic_ostream manipulators
  /**
   *  @brief  Write a newline and flush the stream.
   *
   *  This manipulator is often mistakenly used when a simple newline is
   *  desired, leading to poor buffering performance.  See
   *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#2 for more
   *  on this subject.
  */
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    endl(basic_ostream<_CharT, _Traits>& __os)
    { return flush(__os.put(__os.widen('\n'))); }

  /**
   *  @brief  Write a null character into the output sequence.
   *
   *  "Null character" is @c CharT() by definition.  For CharT of @c char,
   *  this correctly writes the ASCII @c NUL character string terminator.
  */
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    ends(basic_ostream<_CharT, _Traits>& __os)
    { return __os.put(_CharT()); }
  
  /**
   *  @brief  Flushes the output stream.
   *
   *  This manipulator simply calls the stream's @c flush() member function.
  */
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    flush(basic_ostream<_CharT, _Traits>& __os)
    { return __os.flush(); }

} // namespace std

#ifdef _GLIBCPP_NO_TEMPLATE_EXPORT
# define export
#endif
#ifdef  _GLIBCPP_FULLY_COMPLIANT_HEADERS
# include <bits/ostream.tcc>
#endif

#endif	/* _CPP_OSTREAM */
