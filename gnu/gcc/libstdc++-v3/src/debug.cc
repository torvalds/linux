// Debugging mode support code -*- C++ -*-

// Copyright (C) 2003, 2004, 2005, 2006
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

#include <debug/debug.h>
#include <debug/safe_sequence.h>
#include <debug/safe_iterator.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cctype>

using namespace std;

namespace
{
  __gnu_cxx::__mutex safe_base_mutex;
} // anonymous namespace

namespace __gnu_debug
{
  static const char* _S_debug_messages[] = 
  {
    "function requires a valid iterator range [%1.name;, %2.name;)",
    "attempt to insert into container with a singular iterator",
    "attempt to insert into container with an iterator"
    " from a different container",
    "attempt to erase from container with a %2.state; iterator",
    "attempt to erase from container with an iterator"
    " from a different container",
    "attempt to subscript container with out-of-bounds index %2;,"
    " but container only holds %3; elements",
    "attempt to access an element in an empty container",
    "elements in iterator range [%1.name;, %2.name;)"
    " are not partitioned by the value %3;",
    "elements in iterator range [%1.name;, %2.name;)"
    " are not partitioned by the predicate %3; and value %4;",
    "elements in iterator range [%1.name;, %2.name;) are not sorted",
    "elements in iterator range [%1.name;, %2.name;)"
    " are not sorted according to the predicate %3;",
    "elements in iterator range [%1.name;, %2.name;) do not form a heap",
    "elements in iterator range [%1.name;, %2.name;)"
    " do not form a heap with respect to the predicate %3;",
    "attempt to write through a singular bitset reference",
    "attempt to read from a singular bitset reference",
    "attempt to flip a singular bitset reference",
    "attempt to splice a list into itself",
    "attempt to splice lists with inequal allocators",
    "attempt to splice elements referenced by a %1.state; iterator",
    "attempt to splice an iterator from a different container",
    "splice destination %1.name;"
    " occurs within source range [%2.name;, %3.name;)",
    "attempt to initialize an iterator that will immediately become singular",
    "attempt to copy-construct an iterator from a singular iterator",
    "attempt to construct a constant iterator"
    " from a singular mutable iterator",
    "attempt to copy from a singular iterator",
    "attempt to dereference a %1.state; iterator",
    "attempt to increment a %1.state; iterator",
    "attempt to decrement a %1.state; iterator",
    "attempt to subscript a %1.state; iterator %2; step from"
    " its current position, which falls outside its dereferenceable range",
    "attempt to advance a %1.state; iterator %2; steps,"
    " which falls outside its valid range",
    "attempt to retreat a %1.state; iterator %2; steps,"
    " which falls outside its valid range",
    "attempt to compare a %1.state; iterator to a %2.state; iterator",
    "attempt to compare iterators from different sequences",
    "attempt to order a %1.state; iterator to a %2.state; iterator",
    "attempt to order iterators from different sequences",
    "attempt to compute the difference between a %1.state;"
    " iterator to a %2.state; iterator",
    "attempt to compute the different between two iterators"
    " from different sequences",
    "attempt to dereference an end-of-stream istream_iterator",
    "attempt to increment an end-of-stream istream_iterator",
    "attempt to output via an ostream_iterator with no associated stream",
    "attempt to dereference an end-of-stream istreambuf_iterator"
    " (this is a GNU extension)",
    "attempt to increment an end-of-stream istreambuf_iterator"
  };

  void
  _Safe_sequence_base::
  _M_detach_all()
  {
    __gnu_cxx::__scoped_lock sentry(safe_base_mutex);
    for (_Safe_iterator_base* __iter = _M_iterators; __iter;)
      {
	_Safe_iterator_base* __old = __iter;
	__iter = __iter->_M_next;
	__old->_M_detach_single();
      }
    
    for (_Safe_iterator_base* __iter2 = _M_const_iterators; __iter2;)
      {
	_Safe_iterator_base* __old = __iter2;
	__iter2 = __iter2->_M_next;
	__old->_M_detach_single();
      }
  }

  void
  _Safe_sequence_base::
  _M_detach_singular()
  {
    __gnu_cxx::__scoped_lock sentry(safe_base_mutex);
    for (_Safe_iterator_base* __iter = _M_iterators; __iter;)
      {
	_Safe_iterator_base* __old = __iter;
	__iter = __iter->_M_next;
	if (__old->_M_singular())
	  __old->_M_detach_single();
      }

    for (_Safe_iterator_base* __iter2 = _M_const_iterators; __iter2;)
      {
	_Safe_iterator_base* __old = __iter2;
	__iter2 = __iter2->_M_next;
	if (__old->_M_singular())
	  __old->_M_detach_single();
      }
  }

  void
  _Safe_sequence_base::
  _M_revalidate_singular()
  {
    __gnu_cxx::__scoped_lock sentry(safe_base_mutex);
    for (_Safe_iterator_base* __iter = _M_iterators; __iter;
	 __iter = __iter->_M_next)
      __iter->_M_version = _M_version;

    for (_Safe_iterator_base* __iter2 = _M_const_iterators; __iter2;
	 __iter2 = __iter2->_M_next)
      __iter2->_M_version = _M_version;
  }

  void
  _Safe_sequence_base::
  _M_swap(_Safe_sequence_base& __x)
  {
    __gnu_cxx::__scoped_lock sentry(safe_base_mutex);
    swap(_M_iterators, __x._M_iterators);
    swap(_M_const_iterators, __x._M_const_iterators);
    swap(_M_version, __x._M_version);
    _Safe_iterator_base* __iter;
    for (__iter = _M_iterators; __iter; __iter = __iter->_M_next)
      __iter->_M_sequence = this;
    for (__iter = __x._M_iterators; __iter; __iter = __iter->_M_next)
      __iter->_M_sequence = &__x;
    for (__iter = _M_const_iterators; __iter; __iter = __iter->_M_next)
      __iter->_M_sequence = this;
    for (__iter = __x._M_const_iterators; __iter; __iter = __iter->_M_next)
      __iter->_M_sequence = &__x;
  }

  __gnu_cxx::__mutex&
  _Safe_sequence_base::
  _M_get_mutex()
  { return safe_base_mutex; }

  void
  _Safe_iterator_base::
  _M_attach(_Safe_sequence_base* __seq, bool __constant)
  {
    __gnu_cxx::__scoped_lock sentry(safe_base_mutex);
    _M_attach_single(__seq, __constant);
  }
  
  void
  _Safe_iterator_base::
  _M_attach_single(_Safe_sequence_base* __seq, bool __constant)
  {
    _M_detach_single();
    
    // Attach to the new sequence (if there is one)
    if (__seq)
      {
	_M_sequence = __seq;
	_M_version = _M_sequence->_M_version;
	_M_prior = 0;
	if (__constant)
	  {
	    _M_next = _M_sequence->_M_const_iterators;
	    if (_M_next)
	      _M_next->_M_prior = this;
	    _M_sequence->_M_const_iterators = this;
	  }
	else
	  {
	    _M_next = _M_sequence->_M_iterators;
	    if (_M_next)
	      _M_next->_M_prior = this;
	    _M_sequence->_M_iterators = this;
	  }
      }
  }

  void
  _Safe_iterator_base::
  _M_detach()
  {
    __gnu_cxx::__scoped_lock sentry(safe_base_mutex);
    _M_detach_single();
  }

  void
  _Safe_iterator_base::
  _M_detach_single()
  {
    if (_M_sequence)
      {
	// Remove us from this sequence's list
	if (_M_prior) 
	  _M_prior->_M_next = _M_next;
	if (_M_next)  
	  _M_next->_M_prior = _M_prior;
	
	if (_M_sequence->_M_const_iterators == this)
	  _M_sequence->_M_const_iterators = _M_next;
	if (_M_sequence->_M_iterators == this)
	  _M_sequence->_M_iterators = _M_next;
      }

    _M_sequence = 0;
    _M_version = 0;
    _M_prior = 0;
    _M_next = 0;
  }

  bool
  _Safe_iterator_base::
  _M_singular() const
  { return !_M_sequence || _M_version != _M_sequence->_M_version; }
    
  bool
  _Safe_iterator_base::
  _M_can_compare(const _Safe_iterator_base& __x) const
  {
    return (!_M_singular() 
	    && !__x._M_singular() && _M_sequence == __x._M_sequence);
  }

  __gnu_cxx::__mutex&
  _Safe_iterator_base::
  _M_get_mutex()
  { return safe_base_mutex; }

  void
  _Error_formatter::_Parameter::
  _M_print_field(const _Error_formatter* __formatter, const char* __name) const
  {
    assert(this->_M_kind != _Parameter::__unused_param);
    const int __bufsize = 64;
    char __buf[__bufsize];
    
    if (_M_kind == __iterator)
      {
	if (strcmp(__name, "name") == 0)
	  {
	    assert(_M_variant._M_iterator._M_name);
	    __formatter->_M_print_word(_M_variant._M_iterator._M_name);
	  }
	else if (strcmp(__name, "address") == 0)
	  {
	    __formatter->_M_format_word(__buf, __bufsize, "%p", 
					_M_variant._M_iterator._M_address);
	    __formatter->_M_print_word(__buf);
	  }
	else if (strcmp(__name, "type") == 0)
	  {
	    assert(_M_variant._M_iterator._M_type);
	    // TBD: demangle!
	    __formatter->_M_print_word(_M_variant._M_iterator._M_type->name());
	  }
	else if (strcmp(__name, "constness") == 0)
	  {
	    static const char* __constness_names[__last_constness] =
	      {
		"<unknown>",
		"constant",
		"mutable"
	      };
	    __formatter->_M_print_word(__constness_names[_M_variant._M_iterator._M_constness]);
	  }
	else if (strcmp(__name, "state") == 0)
	  {
	    static const char* __state_names[__last_state] = 
	      {
		"<unknown>",
		"singular",
		"dereferenceable (start-of-sequence)",
		"dereferenceable",
		"past-the-end"
	      };
	    __formatter->_M_print_word(__state_names[_M_variant._M_iterator._M_state]);
	  }
	else if (strcmp(__name, "sequence") == 0)
	  {
	    assert(_M_variant._M_iterator._M_sequence);
	    __formatter->_M_format_word(__buf, __bufsize, "%p", 
					_M_variant._M_iterator._M_sequence);
	    __formatter->_M_print_word(__buf);
	  }
	else if (strcmp(__name, "seq_type") == 0)
	  {
	    // TBD: demangle!
	    assert(_M_variant._M_iterator._M_seq_type);
	    __formatter->_M_print_word(_M_variant._M_iterator._M_seq_type->name());
	  }
	else
	  assert(false);
      }
    else if (_M_kind == __sequence)
      {
	if (strcmp(__name, "name") == 0)
	  {
	    assert(_M_variant._M_sequence._M_name);
	    __formatter->_M_print_word(_M_variant._M_sequence._M_name);
	  }
	else if (strcmp(__name, "address") == 0)
	  {
	    assert(_M_variant._M_sequence._M_address);
	    __formatter->_M_format_word(__buf, __bufsize, "%p", 
					_M_variant._M_sequence._M_address);
	    __formatter->_M_print_word(__buf);
	  }
	else if (strcmp(__name, "type") == 0)
	  {
	    // TBD: demangle!
	    assert(_M_variant._M_sequence._M_type);
	    __formatter->_M_print_word(_M_variant._M_sequence._M_type->name());
	  }
	else
	  assert(false);
      }
    else if (_M_kind == __integer)
      {
	if (strcmp(__name, "name") == 0)
	  {
	    assert(_M_variant._M_integer._M_name);
	    __formatter->_M_print_word(_M_variant._M_integer._M_name);
	  }
	else
	assert(false);
      }
    else if (_M_kind == __string)
      {
	if (strcmp(__name, "name") == 0)
	  {
	    assert(_M_variant._M_string._M_name);
	    __formatter->_M_print_word(_M_variant._M_string._M_name);
	  }
	else
	  assert(false);
      }
    else
      {
	assert(false);
      }
  }
  
  void
  _Error_formatter::_Parameter::
  _M_print_description(const _Error_formatter* __formatter) const
  {
    const int __bufsize = 128;
    char __buf[__bufsize];
    
    if (_M_kind == __iterator)
      {
	__formatter->_M_print_word("iterator ");
	if (_M_variant._M_iterator._M_name)
	  {
	    __formatter->_M_format_word(__buf, __bufsize, "\"%s\" ", 
					_M_variant._M_iterator._M_name);
	    __formatter->_M_print_word(__buf);
	  }
	
	__formatter->_M_format_word(__buf, __bufsize, "@ 0x%p {\n", 
				    _M_variant._M_iterator._M_address);
	__formatter->_M_print_word(__buf);
	if (_M_variant._M_iterator._M_type)
	  {
	    __formatter->_M_print_word("type = ");
	    _M_print_field(__formatter, "type");
	    
	    if (_M_variant._M_iterator._M_constness != __unknown_constness)
	      {
		__formatter->_M_print_word(" (");
		_M_print_field(__formatter, "constness");
		__formatter->_M_print_word(" iterator)");
	      }
	    __formatter->_M_print_word(";\n");
	  }
	
	if (_M_variant._M_iterator._M_state != __unknown_state)
	  {
	    __formatter->_M_print_word("  state = ");
	    _M_print_field(__formatter, "state");
	    __formatter->_M_print_word(";\n");
	  }
	
	if (_M_variant._M_iterator._M_sequence)
	  {
	    __formatter->_M_print_word("  references sequence ");
	    if (_M_variant._M_iterator._M_seq_type)
	      {
		__formatter->_M_print_word("with type `");
		_M_print_field(__formatter, "seq_type");
		__formatter->_M_print_word("' ");
	      }
	    
	    __formatter->_M_format_word(__buf, __bufsize, "@ 0x%p\n", 
					_M_variant._M_sequence._M_address);
	    __formatter->_M_print_word(__buf);
	  }
	__formatter->_M_print_word("}\n");
      }
    else if (_M_kind == __sequence)
      {
	__formatter->_M_print_word("sequence ");
	if (_M_variant._M_sequence._M_name)
	  {
	    __formatter->_M_format_word(__buf, __bufsize, "\"%s\" ", 
					_M_variant._M_sequence._M_name);
	    __formatter->_M_print_word(__buf);
	  }
	
	__formatter->_M_format_word(__buf, __bufsize, "@ 0x%p {\n", 
				    _M_variant._M_sequence._M_address);
	__formatter->_M_print_word(__buf);
	
	if (_M_variant._M_sequence._M_type)
	  {
	    __formatter->_M_print_word("  type = ");
	    _M_print_field(__formatter, "type");
	    __formatter->_M_print_word(";\n");
	  }	  
	__formatter->_M_print_word("}\n");
      }
  }

  const _Error_formatter&
  _Error_formatter::_M_message(_Debug_msg_id __id) const
  { return this->_M_message(_S_debug_messages[__id]); }
  
  void
  _Error_formatter::_M_error() const
  {
    const int __bufsize = 128;
    char __buf[__bufsize];
    
    // Emit file & line number information
    _M_column = 1;
    _M_wordwrap = false;
    if (_M_file)
      {
	_M_format_word(__buf, __bufsize, "%s:", _M_file);
	_M_print_word(__buf);
	_M_column += strlen(__buf);
      }
    
    if (_M_line > 0)
      {
	_M_format_word(__buf, __bufsize, "%u:", _M_line);
	_M_print_word(__buf);
	_M_column += strlen(__buf);
      }
    
    _M_wordwrap = true;
    _M_print_word("error: ");
    
    // Print the error message
    assert(_M_text);
    _M_print_string(_M_text);
    _M_print_word(".\n");
    
    // Emit descriptions of the objects involved in the operation
    _M_wordwrap = false;
    bool __has_noninteger_parameters = false;
    for (unsigned int __i = 0; __i < _M_num_parameters; ++__i)
      {
	if (_M_parameters[__i]._M_kind == _Parameter::__iterator
	    || _M_parameters[__i]._M_kind == _Parameter::__sequence)
	  {
	    if (!__has_noninteger_parameters)
	      {
		_M_first_line = true;
		_M_print_word("\nObjects involved in the operation:\n");
		__has_noninteger_parameters = true;
	      }
	    _M_parameters[__i]._M_print_description(this);
	  }
      }
    
    abort();
  }

  template<typename _Tp>
    void
    _Error_formatter::_M_format_word(char* __buf, 
				     int __n __attribute__ ((__unused__)), 
				     const char* __fmt, _Tp __s) const
    {
#ifdef _GLIBCXX_USE_C99
      std::snprintf(__buf, __n, __fmt, __s);
#else
      std::sprintf(__buf, __fmt, __s);
#endif
    }

  
  void 
  _Error_formatter::_M_print_word(const char* __word) const
  {
    if (!_M_wordwrap) 
      {
	fprintf(stderr, "%s", __word);
	return;
      }
    
    size_t __length = strlen(__word);
    if (__length == 0)
      return;
    
    if ((_M_column + __length < _M_max_length)
	|| (__length >= _M_max_length && _M_column == 1)) 
      {
	// If this isn't the first line, indent
	if (_M_column == 1 && !_M_first_line)
	  {
	    char __spacing[_M_indent + 1];
	    for (int i = 0; i < _M_indent; ++i)
	      __spacing[i] = ' ';
	    __spacing[_M_indent] = '\0';
	    fprintf(stderr, "%s", __spacing);
	    _M_column += _M_indent;
	  }
	
	fprintf(stderr, "%s", __word);
	_M_column += __length;
	
	if (__word[__length - 1] == '\n') 
	  {
	    _M_first_line = false;
	    _M_column = 1;
	  }
      }
    else
      {
	_M_column = 1;
	_M_print_word("\n");
	_M_print_word(__word);
      }
  }
  
  void
  _Error_formatter::
  _M_print_string(const char* __string) const
  {
    const char* __start = __string;
    const char* __finish = __start;
    const int __bufsize = 128;
    char __buf[__bufsize];

    while (*__start)
      {
	if (*__start != '%')
	  {
	    // [__start, __finish) denotes the next word
	    __finish = __start;
	    while (isalnum(*__finish))
	      ++__finish;
	    if (__start == __finish)
	      ++__finish;
	    if (isspace(*__finish))
	      ++__finish;
	    
	    const ptrdiff_t __len = __finish - __start;
	    assert(__len < __bufsize);
	    memcpy(__buf, __start, __len);
	    __buf[__len] = '\0';
	    _M_print_word(__buf);
	    __start = __finish;
	    
	    // Skip extra whitespace
	    while (*__start == ' ') 
	      ++__start;
	    
	    continue;
	  } 
	
	++__start;
	assert(*__start);
	if (*__start == '%')
	  {
	    _M_print_word("%");
	    ++__start;
	    continue;
	  }
	
	// Get the parameter number
	assert(*__start >= '1' && *__start <= '9');
	size_t __param = *__start - '0';
	--__param;
	assert(__param < _M_num_parameters);
      
	// '.' separates the parameter number from the field
	// name, if there is one.
	++__start;
	if (*__start != '.')
	  {
	    assert(*__start == ';');
	    ++__start;
	    __buf[0] = '\0';
	    if (_M_parameters[__param]._M_kind == _Parameter::__integer)
	      {
		_M_format_word(__buf, __bufsize, "%ld", 
			       _M_parameters[__param]._M_variant._M_integer._M_value);
		_M_print_word(__buf);
	      }
	    else if (_M_parameters[__param]._M_kind == _Parameter::__string)
	      _M_print_string(_M_parameters[__param]._M_variant._M_string._M_value);
	    continue;
	  }
	
	// Extract the field name we want
	enum { __max_field_len = 16 };
	char __field[__max_field_len];
	int __field_idx = 0;
	++__start;
	while (*__start != ';')
	  {
	    assert(*__start);
	    assert(__field_idx < __max_field_len-1);
	    __field[__field_idx++] = *__start++;
	  }
	++__start;
	__field[__field_idx] = 0;
	
	_M_parameters[__param]._M_print_field(this, __field);		  
      }
  }

  // Instantiations.
  template
    void
    _Error_formatter::_M_format_word(char*, int, const char*, 
				     const void*) const;

  template
    void
    _Error_formatter::_M_format_word(char*, int, const char*, long) const;

  template
    void
    _Error_formatter::_M_format_word(char*, int, const char*, 
				     std::size_t) const;

  template
    void
    _Error_formatter::_M_format_word(char*, int, const char*, 
				     const char*) const;
} // namespace __gnu_debug
