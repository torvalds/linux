// Iostreams base classes -*- C++ -*-

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

/** @file ios_base.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_IOSBASE_H
#define _CPP_BITS_IOSBASE_H 1

#pragma GCC system_header

#include <bits/atomicity.h>
#include <bits/localefwd.h>
#include <bits/locale_classes.h>

namespace std
{
  // The following definitions of bitmask types are enums, not ints,
  // as permitted (but not required) in the standard, in order to provide
  // better type safety in iostream calls.  A side effect is that
  // expressions involving them are no longer compile-time constants.
  enum _Ios_Fmtflags { _M_ios_fmtflags_end = 1L << 16 };

  inline _Ios_Fmtflags 
  operator&(_Ios_Fmtflags __a, _Ios_Fmtflags __b)
  { return _Ios_Fmtflags(static_cast<int>(__a) & static_cast<int>(__b)); }

  inline _Ios_Fmtflags 
  operator|(_Ios_Fmtflags __a, _Ios_Fmtflags __b)
  { return _Ios_Fmtflags(static_cast<int>(__a) | static_cast<int>(__b)); }

  inline _Ios_Fmtflags 
  operator^(_Ios_Fmtflags __a, _Ios_Fmtflags __b)
  { return _Ios_Fmtflags(static_cast<int>(__a) ^ static_cast<int>(__b)); }

  inline _Ios_Fmtflags 
  operator|=(_Ios_Fmtflags& __a, _Ios_Fmtflags __b)
  { return __a = __a | __b; }

  inline _Ios_Fmtflags 
  operator&=(_Ios_Fmtflags& __a, _Ios_Fmtflags __b)
  { return __a = __a & __b; }

  inline _Ios_Fmtflags 
  operator^=(_Ios_Fmtflags& __a, _Ios_Fmtflags __b)
  { return __a = __a ^ __b; }

  inline _Ios_Fmtflags 
  operator~(_Ios_Fmtflags __a)
  { return _Ios_Fmtflags(~static_cast<int>(__a)); }


  enum _Ios_Openmode { _M_ios_openmode_end = 1L << 16 };

  inline _Ios_Openmode 
  operator&(_Ios_Openmode __a, _Ios_Openmode __b)
  { return _Ios_Openmode(static_cast<int>(__a) & static_cast<int>(__b)); }

  inline _Ios_Openmode 
  operator|(_Ios_Openmode __a, _Ios_Openmode __b)
  { return _Ios_Openmode(static_cast<int>(__a) | static_cast<int>(__b)); }

  inline _Ios_Openmode 
  operator^(_Ios_Openmode __a, _Ios_Openmode __b)
  { return _Ios_Openmode(static_cast<int>(__a) ^ static_cast<int>(__b)); }

  inline _Ios_Openmode 
  operator|=(_Ios_Openmode& __a, _Ios_Openmode __b)
  { return __a = __a | __b; }

  inline _Ios_Openmode 
  operator&=(_Ios_Openmode& __a, _Ios_Openmode __b)
  { return __a = __a & __b; }

  inline _Ios_Openmode 
  operator^=(_Ios_Openmode& __a, _Ios_Openmode __b)
  { return __a = __a ^ __b; }

  inline _Ios_Openmode 
  operator~(_Ios_Openmode __a)
  { return _Ios_Openmode(~static_cast<int>(__a)); }


  enum _Ios_Iostate { _M_ios_iostate_end = 1L << 16 };

  inline _Ios_Iostate 
  operator&(_Ios_Iostate __a, _Ios_Iostate __b)
  { return _Ios_Iostate(static_cast<int>(__a) & static_cast<int>(__b)); }

  inline _Ios_Iostate 
  operator|(_Ios_Iostate __a, _Ios_Iostate __b)
  { return _Ios_Iostate(static_cast<int>(__a) | static_cast<int>(__b)); }

  inline _Ios_Iostate 
  operator^(_Ios_Iostate __a, _Ios_Iostate __b)
  { return _Ios_Iostate(static_cast<int>(__a) ^ static_cast<int>(__b)); }

  inline _Ios_Iostate 
  operator|=(_Ios_Iostate& __a, _Ios_Iostate __b)
  { return __a = __a | __b; }

  inline _Ios_Iostate 
  operator&=(_Ios_Iostate& __a, _Ios_Iostate __b)
  { return __a = __a & __b; }

  inline _Ios_Iostate 
  operator^=(_Ios_Iostate& __a, _Ios_Iostate __b)
  { return __a = __a ^ __b; }

  inline _Ios_Iostate 
  operator~(_Ios_Iostate __a)
  { return _Ios_Iostate(~static_cast<int>(__a)); }

  enum _Ios_Seekdir { _M_ios_seekdir_end = 1L << 16 };

  // 27.4.2  Class ios_base
  /**
   *  @brief  The very top of the I/O class hierarchy.
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
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
      //48.  Use of non-existent exception constructor
      explicit 
      failure(const string& __str) throw();

      // This declaration is not useless:
      // http://gcc.gnu.org/onlinedocs/gcc-3.0.2/gcc_6.html#SEC118
      virtual 
      ~failure() throw();

      virtual const char*
      what() const throw();
      
    private:
      enum { _M_bufsize = 256 };
      char _M_name[_M_bufsize];
#endif
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
    static const fmtflags boolalpha =   fmtflags(__ios_flags::_S_boolalpha);
    /// Converts integer input or generates integer output in decimal base.
    static const fmtflags dec =         fmtflags(__ios_flags::_S_dec);
    /// Generate floating-point output in fixed-point notation.
    static const fmtflags fixed =       fmtflags(__ios_flags::_S_fixed);
    /// Converts integer input or generates integer output in hexadecimal base.
    static const fmtflags hex =         fmtflags(__ios_flags::_S_hex);
    /// Adds fill characters at a designated internal point in certain
    /// generated output, or identical to @c right if no such point is
    /// designated.
    static const fmtflags internal =    fmtflags(__ios_flags::_S_internal);
    /// Adds fill characters on the right (final positions) of certain
    /// generated output.  (I.e., the thing you print is flush left.)
    static const fmtflags left =        fmtflags(__ios_flags::_S_left);
    /// Converts integer input or generates integer output in octal base.
    static const fmtflags oct =         fmtflags(__ios_flags::_S_oct);
    /// Adds fill characters on the left (initial positions) of certain
    /// generated output.  (I.e., the thing you print is flush right.)
    static const fmtflags right =       fmtflags(__ios_flags::_S_right);
    /// Generates floating-point output in scientific notation.
    static const fmtflags scientific =  fmtflags(__ios_flags::_S_scientific);
    /// Generates a prefix indicating the numeric base of generated integer
    /// output.
    static const fmtflags showbase =    fmtflags(__ios_flags::_S_showbase);
    /// Generates a decimal-point character unconditionally in generated
    /// floating-point output.
    static const fmtflags showpoint =   fmtflags(__ios_flags::_S_showpoint);
    /// Generates a + sign in non-negative generated numeric output.
    static const fmtflags showpos =     fmtflags(__ios_flags::_S_showpos);
    /// Skips leading white space before certain input operations.
    static const fmtflags skipws =      fmtflags(__ios_flags::_S_skipws);
    /// Flushes output after each output operation.
    static const fmtflags unitbuf =     fmtflags(__ios_flags::_S_unitbuf);
    /// Replaces certain lowercase letters with their uppercase equivalents
    /// in generated output.
    static const fmtflags uppercase =   fmtflags(__ios_flags::_S_uppercase);
    /// A mask of left|right|internal.  Useful for the 2-arg form of @c setf.
    static const fmtflags adjustfield = fmtflags(__ios_flags::_S_adjustfield);
    /// A mask of dec|oct|hex.  Useful for the 2-arg form of @c setf.
    static const fmtflags basefield =   fmtflags(__ios_flags::_S_basefield);
    /// A mask of scientific|fixed.  Useful for the 2-arg form of @c setf.
    static const fmtflags floatfield =  fmtflags(__ios_flags::_S_floatfield);

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
    static const iostate badbit =  	iostate(__ios_flags::_S_badbit);
    /// Indicates that an input operation reached the end of an input sequence.
    static const iostate eofbit =  	iostate(__ios_flags::_S_eofbit);
    /// Indicates that an input operation failed to read the expected
    /// characters, or that an output operation failed to generate the
    /// desired characters.
    static const iostate failbit = 	iostate(__ios_flags::_S_failbit);
    /// Indicates all is well.
    static const iostate goodbit = 	iostate(0);

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
    static const openmode app =    	openmode(__ios_flags::_S_app);
    /// Open and seek to end immediately after opening.
    static const openmode ate =    	openmode(__ios_flags::_S_ate);
    /// Perform input and output in binary mode (as opposed to text mode).
    /// This is probably not what you think it is; see
    /// http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#3 and
    /// http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#7 for more.
    static const openmode binary = 	openmode(__ios_flags::_S_bin);
    /// Open for input.  Default for @c ifstream and fstream.
    static const openmode in =     	openmode(__ios_flags::_S_in);
    /// Open for output.  Default for @c ofstream and fstream.
    static const openmode out =    	openmode(__ios_flags::_S_out);
    /// Open for input.  Default for @c ofstream.
    static const openmode trunc =  	openmode(__ios_flags::_S_trunc);

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
    static const seekdir beg = 		seekdir(0);
    /// Request a seek relative to the current position within the sequence.
    static const seekdir cur = 		seekdir(SEEK_CUR);
    /// Request a seek relative to the current end of the sequence.
    static const seekdir end = 		seekdir(SEEK_END);

#ifdef _GLIBCPP_DEPRECATED
    // Annex D.6
    typedef int io_state;
    typedef int open_mode;
    typedef int seek_dir;
    
    typedef std::streampos streampos;
    typedef std::streamoff streamoff;
#endif

    // Callbacks;
    /**
     *  @doctodo
    */
    enum event
    {
      erase_event,
      imbue_event,
      copyfmt_event
    };

    /**
     *  @doctodo
    */
    typedef void (*event_callback) (event, ios_base&, int);

    /**
     *  @doctodo
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
    streamsize 		_M_precision;
    streamsize 		_M_width;
    fmtflags 		_M_flags;
    iostate 		_M_exception;
    iostate 	       	_M_streambuf_state;
    //@}

    // 27.4.2.6  Members for callbacks
    // 27.4.2.6  ios_base callbacks
    struct _Callback_list
    {
      // Data Members
      _Callback_list* 		_M_next;
      ios_base::event_callback 	_M_fn;
      int 			_M_index;
      _Atomic_word		_M_refcount;  // 0 means one reference.
    
      _Callback_list(ios_base::event_callback __fn, int __index, 
		     _Callback_list* __cb)
      : _M_next(__cb), _M_fn(__fn), _M_index(__index), _M_refcount(0) { }
      
      void 
      _M_add_reference() { __atomic_add(&_M_refcount, 1); }

      // 0 => OK to delete.
      int 
      _M_remove_reference() { return __exchange_and_add(&_M_refcount, -1); }
    };

     _Callback_list*  	_M_callbacks;

    void 
    _M_call_callbacks(event __ev) throw();

    void 
    _M_dispose_callbacks(void);

    // 27.4.2.5  Members for iword/pword storage
    struct _Words 
    { 
      void* 	_M_pword; 
      long 	_M_iword; 
      _Words() : _M_pword(0), _M_iword(0) { }
    };

    // Only for failed iword/pword calls.
    _Words  		_M_word_zero;    

    // Guaranteed storage.
    // The first 5 iword and pword slots are reserved for internal use.
    static const int 	_S_local_word_size = 8;
    _Words  		_M_local_word[_S_local_word_size];  

    // Allocated storage.
    int     		_M_word_size;
    _Words* 		_M_word;
 
    _Words& 
    _M_grow_words(int __index);

    // Members for locale and locale caching.
    locale 		_M_ios_locale;

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
      
      static void
      _S_ios_create(bool __sync);
      
      static void
      _S_ios_destroy();

      // NB: Allows debugger applications use of the standard streams
      // from operator new. _S_ios_base_init must be incremented in
      // _S_ios_create _after_ initialization is completed.
      static bool
      _S_initialized() { return _S_ios_base_init; }

    private:
      static int 	_S_ios_base_init;
      static bool	_S_synced_with_stdio;
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
     *  Sets the new locale for this stream, and
     *  [XXX does something with callbacks].
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
     *  @doctodo
    */
    static int 
    xalloc() throw();

    /**
     *  @doctodo
    */
    inline long& 
    iword(int __ix)
    {
      _Words& __word = (__ix < _M_word_size) 
			? _M_word[__ix] : _M_grow_words(__ix);
      return __word._M_iword;
    }

    /**
     *  @doctodo
    */
    inline void*& 
    pword(int __ix)
    {
      _Words& __word = (__ix < _M_word_size) 
			? _M_word[__ix] : _M_grow_words(__ix);
      return __word._M_pword;
    }

    // Destructor
    /**
     *  Destroys local storage and
     *  [XXX does something with callbacks].
    */
    ~ios_base();

  protected:
    ios_base();

#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
  //50.  Copy constructor and assignment operator of ios_base
  private:
    ios_base(const ios_base&);

    ios_base& 
    operator=(const ios_base&);
#endif
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

} // namespace std

#endif /* _CPP_BITS_IOSBASE_H */

