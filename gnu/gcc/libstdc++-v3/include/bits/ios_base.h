// Iostreams base classes -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

/** @file ios_base.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 27.4  Iostreams base classes
//

#ifndef _IOS_BASE_H
#define _IOS_BASE_H 1

#pragma GCC system_header

#include <ext/atomicity.h>
#include <bits/localefwd.h>
#include <bits/locale_classes.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // The following definitions of bitmask types are enums, not ints,
  // as permitted (but not required) in the standard, in order to provide
  // better type safety in iostream calls.  A side effect is that
  // expressions involving them are no longer compile-time constants.
  enum _Ios_Fmtflags 
    { 
      _S_boolalpha 	= 1L << 0,
      _S_dec 		= 1L << 1,
      _S_fixed 		= 1L << 2,
      _S_hex 		= 1L << 3,
      _S_internal 	= 1L << 4,
      _S_left 		= 1L << 5,
      _S_oct 		= 1L << 6,
      _S_right 		= 1L << 7,
      _S_scientific 	= 1L << 8,
      _S_showbase 	= 1L << 9,
      _S_showpoint 	= 1L << 10,
      _S_showpos 	= 1L << 11,
      _S_skipws 	= 1L << 12,
      _S_unitbuf 	= 1L << 13,
      _S_uppercase 	= 1L << 14,
      _S_adjustfield 	= _S_left | _S_right | _S_internal,
      _S_basefield 	= _S_dec | _S_oct | _S_hex,
      _S_floatfield 	= _S_scientific | _S_fixed,
      _S_ios_fmtflags_end = 1L << 16 
    };

  inline _Ios_Fmtflags
  operator&(_Ios_Fmtflags __a, _Ios_Fmtflags __b)
  { return _Ios_Fmtflags(static_cast<int>(__a) & static_cast<int>(__b)); }

  inline _Ios_Fmtflags
  operator|(_Ios_Fmtflags __a, _Ios_Fmtflags __b)
  { return _Ios_Fmtflags(static_cast<int>(__a) | static_cast<int>(__b)); }

  inline _Ios_Fmtflags
  operator^(_Ios_Fmtflags __a, _Ios_Fmtflags __b)
  { return _Ios_Fmtflags(static_cast<int>(__a) ^ static_cast<int>(__b)); }

  inline _Ios_Fmtflags&
  operator|=(_Ios_Fmtflags& __a, _Ios_Fmtflags __b)
  { return __a = __a | __b; }

  inline _Ios_Fmtflags&
  operator&=(_Ios_Fmtflags& __a, _Ios_Fmtflags __b)
  { return __a = __a & __b; }

  inline _Ios_Fmtflags&
  operator^=(_Ios_Fmtflags& __a, _Ios_Fmtflags __b)
  { return __a = __a ^ __b; }

  inline _Ios_Fmtflags
  operator~(_Ios_Fmtflags __a)
  { return _Ios_Fmtflags(~static_cast<int>(__a)); }


  enum _Ios_Openmode 
    { 
      _S_app 		= 1L << 0,
      _S_ate 		= 1L << 1,
      _S_bin 		= 1L << 2,
      _S_in 		= 1L << 3,
      _S_out 		= 1L << 4,
      _S_trunc 		= 1L << 5,
      _S_ios_openmode_end = 1L << 16 
    };

  inline _Ios_Openmode
  operator&(_Ios_Openmode __a, _Ios_Openmode __b)
  { return _Ios_Openmode(static_cast<int>(__a) & static_cast<int>(__b)); }

  inline _Ios_Openmode
  operator|(_Ios_Openmode __a, _Ios_Openmode __b)
  { return _Ios_Openmode(static_cast<int>(__a) | static_cast<int>(__b)); }

  inline _Ios_Openmode
  operator^(_Ios_Openmode __a, _Ios_Openmode __b)
  { return _Ios_Openmode(static_cast<int>(__a) ^ static_cast<int>(__b)); }

  inline _Ios_Openmode&
  operator|=(_Ios_Openmode& __a, _Ios_Openmode __b)
  { return __a = __a | __b; }

  inline _Ios_Openmode&
  operator&=(_Ios_Openmode& __a, _Ios_Openmode __b)
  { return __a = __a & __b; }

  inline _Ios_Openmode&
  operator^=(_Ios_Openmode& __a, _Ios_Openmode __b)
  { return __a = __a ^ __b; }

  inline _Ios_Openmode
  operator~(_Ios_Openmode __a)
  { return _Ios_Openmode(~static_cast<int>(__a)); }


  enum _Ios_Iostate
    { 
      _S_goodbit 		= 0,
      _S_badbit 		= 1L << 0,
      _S_eofbit 		= 1L << 1,
      _S_failbit		= 1L << 2,
      _S_ios_iostate_end = 1L << 16 
    };

  inline _Ios_Iostate
  operator&(_Ios_Iostate __a, _Ios_Iostate __b)
  { return _Ios_Iostate(static_cast<int>(__a) & static_cast<int>(__b)); }

  inline _Ios_Iostate
  operator|(_Ios_Iostate __a, _Ios_Iostate __b)
  { return _Ios_Iostate(static_cast<int>(__a) | static_cast<int>(__b)); }

  inline _Ios_Iostate
  operator^(_Ios_Iostate __a, _Ios_Iostate __b)
  { return _Ios_Iostate(static_cast<int>(__a) ^ static_cast<int>(__b)); }

  inline _Ios_Iostate&
  operator|=(_Ios_Iostate& __a, _Ios_Iostate __b)
  { return __a = __a | __b; }

  inline _Ios_Iostate&
  operator&=(_Ios_Iostate& __a, _Ios_Iostate __b)
  { return __a = __a & __b; }

  inline _Ios_Iostate&
  operator^=(_Ios_Iostate& __a, _Ios_Iostate __b)
  { return __a = __a ^ __b; }

  inline _Ios_Iostate
  operator~(_Ios_Iostate __a)
  { return _Ios_Iostate(~static_cast<int>(__a)); }

  enum _Ios_Seekdir 
    { 
      _S_beg = 0,
      _S_cur = SEEK_CUR,
      _S_end = SEEK_END,
      _S_ios_seekdir_end = 1L << 16 
    };

  // 27.4.2  Class ios_base
  /**
   *  @brief  The base of the I/O class hierarchy.
   *
   *  This class defines everything that can be defined about I/O that does
   *  not depend on the type of characters being input or output.  Most
   *  people will only see @c ios_base when they need to specify the full
   *  name of the various I/O flags (e.g., the openmodes).
  */
  class ios_base
  {
  public:

    // 27.4.2.1.1  Class ios_base::failure
    /// These are thrown to indicate problems.  Doc me.
    class failure : public exception
    {
    public:
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 48.  Use of non-existent exception constructor
      explicit
      failure(const string& __str) throw();

      // This declaration is not useless:
      // http://gcc.gnu.org/onlinedocs/gcc-3.0.2/gcc_6.html#SEC118
      virtual
      ~failure() throw();

      virtual const char*
      what() const throw();

    private:
      string _M_msg;
    };

    // 27.4.2.1.2  Type ios_base::fmtflags
    /**
     *  @brief This is a bitmask type.
     *
     *  @c "_Ios_Fmtflags" is implementation-defined, but it is valid to
     *  perform bitwise operations on these values and expect the Right
     *  Thing to happen.  Defined objects of type fmtflags are:
     *  - boolalpha
     *  - dec
     *  - fixed
     *  - hex
     *  - internal
     *  - left
     *  - oct
     *  - right
     *  - scientific
     *  - showbase
     *  - showpoint
     *  - showpos
     *  - skipws
     *  - unitbuf
     *  - uppercase
     *  - adjustfield
     *  - basefield
     *  - floatfield
    */
    typedef _Ios_Fmtflags fmtflags;

    /// Insert/extract @c bool in alphabetic rather than numeric format.
    static const fmtflags boolalpha =   _S_boolalpha;

    /// Converts integer input or generates integer output in decimal base.
    static const fmtflags dec =         _S_dec;

    /// Generate floating-point output in fixed-point notation.
    static const fmtflags fixed =       _S_fixed;

    /// Converts integer input or generates integer output in hexadecimal base.
    static const fmtflags hex =         _S_hex;

    /// Adds fill characters at a designated internal point in certain
    /// generated output, or identical to @c right if no such point is
    /// designated.
    static const fmtflags internal =    _S_internal;

    /// Adds fill characters on the right (final positions) of certain
    /// generated output.  (I.e., the thing you print is flush left.)
    static const fmtflags left =        _S_left;

    /// Converts integer input or generates integer output in octal base.
    static const fmtflags oct =         _S_oct;

    /// Adds fill characters on the left (initial positions) of certain
    /// generated output.  (I.e., the thing you print is flush right.)
    static const fmtflags right =       _S_right;

    /// Generates floating-point output in scientific notation.
    static const fmtflags scientific =  _S_scientific;

    /// Generates a prefix indicating the numeric base of generated integer
    /// output.
    static const fmtflags showbase =    _S_showbase;

    /// Generates a decimal-point character unconditionally in generated
    /// floating-point output.
    static const fmtflags showpoint =   _S_showpoint;

    /// Generates a + sign in non-negative generated numeric output.
    static const fmtflags showpos =     _S_showpos;

    /// Skips leading white space before certain input operations.
    static const fmtflags skipws =      _S_skipws;

    /// Flushes output after each output operation.
    static const fmtflags unitbuf =     _S_unitbuf;

    /// Replaces certain lowercase letters with their uppercase equivalents
    /// in generated output.
    static const fmtflags uppercase =   _S_uppercase;

    /// A mask of left|right|internal.  Useful for the 2-arg form of @c setf.
    static const fmtflags adjustfield = _S_adjustfield;

    /// A mask of dec|oct|hex.  Useful for the 2-arg form of @c setf.
    static const fmtflags basefield =   _S_basefield;

    /// A mask of scientific|fixed.  Useful for the 2-arg form of @c setf.
    static const fmtflags floatfield =  _S_floatfield;

    // 27.4.2.1.3  Type ios_base::iostate
    /**
     *  @brief This is a bitmask type.
     *
     *  @c "_Ios_Iostate" is implementation-defined, but it is valid to
     *  perform bitwise operations on these values and expect the Right
     *  Thing to happen.  Defined objects of type iostate are:
     *  - badbit
     *  - eofbit
     *  - failbit
     *  - goodbit
    */
    typedef _Ios_Iostate iostate;

    /// Indicates a loss of integrity in an input or output sequence (such
    /// as an irrecoverable read error from a file).
    static const iostate badbit =	_S_badbit;

    /// Indicates that an input operation reached the end of an input sequence.
    static const iostate eofbit =	_S_eofbit;

    /// Indicates that an input operation failed to read the expected
    /// characters, or that an output operation failed to generate the
    /// desired characters.
    static const iostate failbit =	_S_failbit;

    /// Indicates all is well.
    static const iostate goodbit =	_S_goodbit;

    // 27.4.2.1.4  Type ios_base::openmode
    /**
     *  @brief This is a bitmask type.
     *
     *  @c "_Ios_Openmode" is implementation-defined, but it is valid to
     *  perform bitwise operations on these values and expect the Right
     *  Thing to happen.  Defined objects of type openmode are:
     *  - app
     *  - ate
     *  - binary
     *  - in
     *  - out
     *  - trunc
    */
    typedef _Ios_Openmode openmode;

    /// Seek to end before each write.
    static const openmode app =		_S_app;

    /// Open and seek to end immediately after opening.
    static const openmode ate =		_S_ate;

    /// Perform input and output in binary mode (as opposed to text mode).
    /// This is probably not what you think it is; see
    /// http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#3 and
    /// http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#7 for more.
    static const openmode binary =	_S_bin;

    /// Open for input.  Default for @c ifstream and fstream.
    static const openmode in =		_S_in;

    /// Open for output.  Default for @c ofstream and fstream.
    static const openmode out =		_S_out;

    /// Open for input.  Default for @c ofstream.
    static const openmode trunc =	_S_trunc;

    // 27.4.2.1.5  Type ios_base::seekdir
    /**
     *  @brief This is an enumerated type.
     *
     *  @c "_Ios_Seekdir" is implementation-defined.  Defined values
     *  of type seekdir are:
     *  - beg
     *  - cur, equivalent to @c SEEK_CUR in the C standard library.
     *  - end, equivalent to @c SEEK_END in the C standard library.
    */
    typedef _Ios_Seekdir seekdir;

    /// Request a seek relative to the beginning of the stream.
    static const seekdir beg =		_S_beg;

    /// Request a seek relative to the current position within the sequence.
    static const seekdir cur =		_S_cur;

    /// Request a seek relative to the current end of the sequence.
    static const seekdir end =		_S_end;

    // Annex D.6
    typedef int io_state;
    typedef int open_mode;
    typedef int seek_dir;

    typedef std::streampos streampos;
    typedef std::streamoff streamoff;

    // Callbacks;
    /**
     *  @brief  The set of events that may be passed to an event callback.
     *
     *  erase_event is used during ~ios() and copyfmt().  imbue_event is used
     *  during imbue().  copyfmt_event is used during copyfmt().
    */
    enum event
    {
      erase_event,
      imbue_event,
      copyfmt_event
    };

    /**
     *  @brief  The type of an event callback function.
     *  @param  event  One of the members of the event enum.
     *  @param  ios_base  Reference to the ios_base object.
     *  @param  int  The integer provided when the callback was registered.
     *
     *  Event callbacks are user defined functions that get called during
     *  several ios_base and basic_ios functions, specifically imbue(),
     *  copyfmt(), and ~ios().
    */
    typedef void (*event_callback) (event, ios_base&, int);

    /**
     *  @brief  Add the callback __fn with parameter __index.
     *  @param  __fn  The function to add.
     *  @param  __index  The integer to pass to the function when invoked.
     *
     *  Registers a function as an event callback with an integer parameter to
     *  be passed to the function when invoked.  Multiple copies of the
     *  function are allowed.  If there are multiple callbacks, they are
     *  invoked in the order they were registered.
    */
    void
    register_callback(event_callback __fn, int __index);

  protected:
    //@{
    /**
     *  @if maint
     *  ios_base data members (doc me)
     *  @endif
    */
    streamsize		_M_precision;
    streamsize		_M_width;
    fmtflags		_M_flags;
    iostate		_M_exception;
    iostate		_M_streambuf_state;
    //@}

    // 27.4.2.6  Members for callbacks
    // 27.4.2.6  ios_base callbacks
    struct _Callback_list
    {
      // Data Members
      _Callback_list*		_M_next;
      ios_base::event_callback	_M_fn;
      int			_M_index;
      _Atomic_word		_M_refcount;  // 0 means one reference.

      _Callback_list(ios_base::event_callback __fn, int __index,
		     _Callback_list* __cb)
      : _M_next(__cb), _M_fn(__fn), _M_index(__index), _M_refcount(0) { }

      void
      _M_add_reference() { __gnu_cxx::__atomic_add_dispatch(&_M_refcount, 1); }

      // 0 => OK to delete.
      int
      _M_remove_reference() 
      { return __gnu_cxx::__exchange_and_add_dispatch(&_M_refcount, -1); }
    };

     _Callback_list*	_M_callbacks;

    void
    _M_call_callbacks(event __ev) throw();

    void
    _M_dispose_callbacks(void);

    // 27.4.2.5  Members for iword/pword storage
    struct _Words
    {
      void*	_M_pword;
      long	_M_iword;
      _Words() : _M_pword(0), _M_iword(0) { }
    };

    // Only for failed iword/pword calls.
    _Words		_M_word_zero;

    // Guaranteed storage.
    // The first 5 iword and pword slots are reserved for internal use.
    enum { _S_local_word_size = 8 };
    _Words		_M_local_word[_S_local_word_size];

    // Allocated storage.
    int			_M_word_size;
    _Words*		_M_word;

    _Words&
    _M_grow_words(int __index, bool __iword);

    // Members for locale and locale caching.
    locale		_M_ios_locale;

    void
    _M_init();

  public:

    // 27.4.2.1.6  Class ios_base::Init
    // Used to initialize standard streams. In theory, g++ could use
    // -finit-priority to order this stuff correctly without going
    // through these machinations.
    class Init
    {
      friend class ios_base;
    public:
      Init();
      ~Init();

    private:
      static _Atomic_word	_S_refcount;
      static bool		_S_synced_with_stdio;
    };

    // [27.4.2.2] fmtflags state functions
    /**
     *  @brief  Access to format flags.
     *  @return  The format control flags for both input and output.
    */
    inline fmtflags
    flags() const { return _M_flags; }

    /**
     *  @brief  Setting new format flags all at once.
     *  @param  fmtfl  The new flags to set.
     *  @return  The previous format control flags.
     *
     *  This function overwrites all the format flags with @a fmtfl.
    */
    inline fmtflags
    flags(fmtflags __fmtfl)
    {
      fmtflags __old = _M_flags;
      _M_flags = __fmtfl;
      return __old;
    }

    /**
     *  @brief  Setting new format flags.
     *  @param  fmtfl  Additional flags to set.
     *  @return  The previous format control flags.
     *
     *  This function sets additional flags in format control.  Flags that
     *  were previously set remain set.
    */
    inline fmtflags
    setf(fmtflags __fmtfl)
    {
      fmtflags __old = _M_flags;
      _M_flags |= __fmtfl;
      return __old;
    }

    /**
     *  @brief  Setting new format flags.
     *  @param  fmtfl  Additional flags to set.
     *  @param  mask  The flags mask for @a fmtfl.
     *  @return  The previous format control flags.
     *
     *  This function clears @a mask in the format flags, then sets
     *  @a fmtfl @c & @a mask.  An example mask is @c ios_base::adjustfield.
    */
    inline fmtflags
    setf(fmtflags __fmtfl, fmtflags __mask)
    {
      fmtflags __old = _M_flags;
      _M_flags &= ~__mask;
      _M_flags |= (__fmtfl & __mask);
      return __old;
    }

    /**
     *  @brief  Clearing format flags.
     *  @param  mask  The flags to unset.
     *
     *  This function clears @a mask in the format flags.
    */
    inline void
    unsetf(fmtflags __mask) { _M_flags &= ~__mask; }

    /**
     *  @brief  Flags access.
     *  @return  The precision to generate on certain output operations.
     *
     *  @if maint
     *  Be careful if you try to give a definition of "precision" here; see
     *  DR 189.
     *  @endif
    */
    inline streamsize
    precision() const { return _M_precision; }

    /**
     *  @brief  Changing flags.
     *  @param  prec  The new precision value.
     *  @return  The previous value of precision().
    */
    inline streamsize
    precision(streamsize __prec)
    {
      streamsize __old = _M_precision;
      _M_precision = __prec;
      return __old;
    }

    /**
     *  @brief  Flags access.
     *  @return  The minimum field width to generate on output operations.
     *
     *  "Minimum field width" refers to the number of characters.
    */
    inline streamsize
    width() const { return _M_width; }

    /**
     *  @brief  Changing flags.
     *  @param  wide  The new width value.
     *  @return  The previous value of width().
    */
    inline streamsize
    width(streamsize __wide)
    {
      streamsize __old = _M_width;
      _M_width = __wide;
      return __old;
    }

    // [27.4.2.4] ios_base static members
    /**
     *  @brief  Interaction with the standard C I/O objects.
     *  @param  sync  Whether to synchronize or not.
     *  @return  True if the standard streams were previously synchronized.
     *
     *  The synchronization referred to is @e only that between the standard
     *  C facilities (e.g., stdout) and the standard C++ objects (e.g.,
     *  cout).  User-declared streams are unaffected.  See
     *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#8 for more.
    */
    static bool
    sync_with_stdio(bool __sync = true);

    // [27.4.2.3] ios_base locale functions
    /**
     *  @brief  Setting a new locale.
     *  @param  loc  The new locale.
     *  @return  The previous locale.
     *
     *  Sets the new locale for this stream, and then invokes each callback
     *  with imbue_event.
    */
    locale
    imbue(const locale& __loc);

    /**
     *  @brief  Locale access
     *  @return  A copy of the current locale.
     *
     *  If @c imbue(loc) has previously been called, then this function
     *  returns @c loc.  Otherwise, it returns a copy of @c std::locale(),
     *  the global C++ locale.
    */
    inline locale
    getloc() const { return _M_ios_locale; }

    /**
     *  @brief  Locale access
     *  @return  A reference to the current locale.
     *
     *  Like getloc above, but returns a reference instead of
     *  generating a copy.
    */
    inline const locale&
    _M_getloc() const { return _M_ios_locale; }

    // [27.4.2.5] ios_base storage functions
    /**
     *  @brief  Access to unique indices.
     *  @return  An integer different from all previous calls.
     *
     *  This function returns a unique integer every time it is called.  It
     *  can be used for any purpose, but is primarily intended to be a unique
     *  index for the iword and pword functions.  The expectation is that an
     *  application calls xalloc in order to obtain an index in the iword and
     *  pword arrays that can be used without fear of conflict.
     *
     *  The implementation maintains a static variable that is incremented and
     *  returned on each invocation.  xalloc is guaranteed to return an index
     *  that is safe to use in the iword and pword arrays.
    */
    static int
    xalloc() throw();

    /**
     *  @brief  Access to integer array.
     *  @param  __ix  Index into the array.
     *  @return  A reference to an integer associated with the index.
     *
     *  The iword function provides access to an array of integers that can be
     *  used for any purpose.  The array grows as required to hold the
     *  supplied index.  All integers in the array are initialized to 0.
     *
     *  The implementation reserves several indices.  You should use xalloc to
     *  obtain an index that is safe to use.  Also note that since the array
     *  can grow dynamically, it is not safe to hold onto the reference.
    */
    inline long&
    iword(int __ix)
    {
      _Words& __word = (__ix < _M_word_size)
			? _M_word[__ix] : _M_grow_words(__ix, true);
      return __word._M_iword;
    }

    /**
     *  @brief  Access to void pointer array.
     *  @param  __ix  Index into the array.
     *  @return  A reference to a void* associated with the index.
     *
     *  The pword function provides access to an array of pointers that can be
     *  used for any purpose.  The array grows as required to hold the
     *  supplied index.  All pointers in the array are initialized to 0.
     *
     *  The implementation reserves several indices.  You should use xalloc to
     *  obtain an index that is safe to use.  Also note that since the array
     *  can grow dynamically, it is not safe to hold onto the reference.
    */
    inline void*&
    pword(int __ix)
    {
      _Words& __word = (__ix < _M_word_size)
			? _M_word[__ix] : _M_grow_words(__ix, false);
      return __word._M_pword;
    }

    // Destructor
    /**
     *  Invokes each callback with erase_event.  Destroys local storage.
     *
     *  Note that the ios_base object for the standard streams never gets
     *  destroyed.  As a result, any callbacks registered with the standard
     *  streams will not get invoked with erase_event (unless copyfmt is
     *  used).
    */
    virtual ~ios_base();

  protected:
    ios_base();

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // 50.  Copy constructor and assignment operator of ios_base
  private:
    ios_base(const ios_base&);

    ios_base&
    operator=(const ios_base&);
  };

  // [27.4.5.1] fmtflags manipulators
  /// Calls base.setf(ios_base::boolalpha).
  inline ios_base&
  boolalpha(ios_base& __base)
  {
    __base.setf(ios_base::boolalpha);
    return __base;
  }

  /// Calls base.unsetf(ios_base::boolalpha).
  inline ios_base&
  noboolalpha(ios_base& __base)
  {
    __base.unsetf(ios_base::boolalpha);
    return __base;
  }

  /// Calls base.setf(ios_base::showbase).
  inline ios_base&
  showbase(ios_base& __base)
  {
    __base.setf(ios_base::showbase);
    return __base;
  }

  /// Calls base.unsetf(ios_base::showbase).
  inline ios_base&
  noshowbase(ios_base& __base)
  {
    __base.unsetf(ios_base::showbase);
    return __base;
  }

  /// Calls base.setf(ios_base::showpoint).
  inline ios_base&
  showpoint(ios_base& __base)
  {
    __base.setf(ios_base::showpoint);
    return __base;
  }

  /// Calls base.unsetf(ios_base::showpoint).
  inline ios_base&
  noshowpoint(ios_base& __base)
  {
    __base.unsetf(ios_base::showpoint);
    return __base;
  }

  /// Calls base.setf(ios_base::showpos).
  inline ios_base&
  showpos(ios_base& __base)
  {
    __base.setf(ios_base::showpos);
    return __base;
  }

  /// Calls base.unsetf(ios_base::showpos).
  inline ios_base&
  noshowpos(ios_base& __base)
  {
    __base.unsetf(ios_base::showpos);
    return __base;
  }

  /// Calls base.setf(ios_base::skipws).
  inline ios_base&
  skipws(ios_base& __base)
  {
    __base.setf(ios_base::skipws);
    return __base;
  }

  /// Calls base.unsetf(ios_base::skipws).
  inline ios_base&
  noskipws(ios_base& __base)
  {
    __base.unsetf(ios_base::skipws);
    return __base;
  }

  /// Calls base.setf(ios_base::uppercase).
  inline ios_base&
  uppercase(ios_base& __base)
  {
    __base.setf(ios_base::uppercase);
    return __base;
  }

  /// Calls base.unsetf(ios_base::uppercase).
  inline ios_base&
  nouppercase(ios_base& __base)
  {
    __base.unsetf(ios_base::uppercase);
    return __base;
  }

  /// Calls base.setf(ios_base::unitbuf).
  inline ios_base&
  unitbuf(ios_base& __base)
  {
     __base.setf(ios_base::unitbuf);
     return __base;
  }

  /// Calls base.unsetf(ios_base::unitbuf).
  inline ios_base&
  nounitbuf(ios_base& __base)
  {
     __base.unsetf(ios_base::unitbuf);
     return __base;
  }

  // [27.4.5.2] adjustfield anipulators
  /// Calls base.setf(ios_base::internal, ios_base::adjustfield).
  inline ios_base&
  internal(ios_base& __base)
  {
     __base.setf(ios_base::internal, ios_base::adjustfield);
     return __base;
  }

  /// Calls base.setf(ios_base::left, ios_base::adjustfield).
  inline ios_base&
  left(ios_base& __base)
  {
    __base.setf(ios_base::left, ios_base::adjustfield);
    return __base;
  }

  /// Calls base.setf(ios_base::right, ios_base::adjustfield).
  inline ios_base&
  right(ios_base& __base)
  {
    __base.setf(ios_base::right, ios_base::adjustfield);
    return __base;
  }

  // [27.4.5.3] basefield anipulators
  /// Calls base.setf(ios_base::dec, ios_base::basefield).
  inline ios_base&
  dec(ios_base& __base)
  {
    __base.setf(ios_base::dec, ios_base::basefield);
    return __base;
  }

  /// Calls base.setf(ios_base::hex, ios_base::basefield).
  inline ios_base&
  hex(ios_base& __base)
  {
    __base.setf(ios_base::hex, ios_base::basefield);
    return __base;
  }

  /// Calls base.setf(ios_base::oct, ios_base::basefield).
  inline ios_base&
  oct(ios_base& __base)
  {
    __base.setf(ios_base::oct, ios_base::basefield);
    return __base;
  }

  // [27.4.5.4] floatfield anipulators
  /// Calls base.setf(ios_base::fixed, ios_base::floatfield).
  inline ios_base&
  fixed(ios_base& __base)
  {
    __base.setf(ios_base::fixed, ios_base::floatfield);
    return __base;
  }

  /// Calls base.setf(ios_base::scientific, ios_base::floatfield).
  inline ios_base&
  scientific(ios_base& __base)
  {
    __base.setf(ios_base::scientific, ios_base::floatfield);
    return __base;
  }

_GLIBCXX_END_NAMESPACE

#endif /* _IOS_BASE_H */

