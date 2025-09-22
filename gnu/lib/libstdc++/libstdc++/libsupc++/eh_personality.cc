// -*- C++ -*- The GNU C++ exception personality routine.
// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
//
// This file is part of GNU CC.
//
// GNU CC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// GNU CC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GNU CC; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.


#include <bits/c++config.h>
#include <cstdlib>
#include <exception_defines.h>
#include "unwind-cxx.h"

using namespace __cxxabiv1;

#include "unwind-pe.h"


struct lsda_header_info
{
  _Unwind_Ptr Start;
  _Unwind_Ptr LPStart;
  _Unwind_Ptr ttype_base;
  const unsigned char *TType;
  const unsigned char *action_table;
  unsigned char ttype_encoding;
  unsigned char call_site_encoding;
};

static const unsigned char *
parse_lsda_header (_Unwind_Context *context, const unsigned char *p,
		   lsda_header_info *info)
{
  _Unwind_Word tmp;
  unsigned char lpstart_encoding;

  info->Start = (context ? _Unwind_GetRegionStart (context) : 0);

  // Find @LPStart, the base to which landing pad offsets are relative.
  lpstart_encoding = *p++;
  if (lpstart_encoding != DW_EH_PE_omit)
    p = read_encoded_value (context, lpstart_encoding, p, &info->LPStart);
  else
    info->LPStart = info->Start;

  // Find @TType, the base of the handler and exception spec type data.
  info->ttype_encoding = *p++;
  if (info->ttype_encoding != DW_EH_PE_omit)
    {
      p = read_uleb128 (p, &tmp);
      info->TType = p + tmp;
    }
  else
    info->TType = 0;

  // The encoding and length of the call-site table; the action table
  // immediately follows.
  info->call_site_encoding = *p++;
  p = read_uleb128 (p, &tmp);
  info->action_table = p + tmp;

  return p;
}

static const std::type_info *
get_ttype_entry (lsda_header_info *info, _Unwind_Word i)
{
  _Unwind_Ptr ptr;

  i *= size_of_encoded_value (info->ttype_encoding);
  read_encoded_value_with_base (info->ttype_encoding, info->ttype_base,
				info->TType - i, &ptr);

  return reinterpret_cast<const std::type_info *>(ptr);
}

// Given the thrown type THROW_TYPE, pointer to a variable containing a
// pointer to the exception object THROWN_PTR_P and a type CATCH_TYPE to
// compare against, return whether or not there is a match and if so,
// update *THROWN_PTR_P.

static bool
get_adjusted_ptr (const std::type_info *catch_type,
		  const std::type_info *throw_type,
		  void **thrown_ptr_p)
{
  void *thrown_ptr = *thrown_ptr_p;

  // Pointer types need to adjust the actual pointer, not
  // the pointer to pointer that is the exception object.
  // This also has the effect of passing pointer types
  // "by value" through the __cxa_begin_catch return value.
  if (throw_type->__is_pointer_p ())
    thrown_ptr = *(void **) thrown_ptr;

  if (catch_type->__do_catch (throw_type, &thrown_ptr, 1))
    {
      *thrown_ptr_p = thrown_ptr;
      return true;
    }

  return false;
}

// Return true if THROW_TYPE matches one if the filter types.

static bool
check_exception_spec (lsda_header_info *info, const std::type_info *throw_type,
		      void *thrown_ptr, _Unwind_Sword filter_value)
{
  const unsigned char *e = info->TType - filter_value - 1;

  while (1)
    {
      const std::type_info *catch_type;
      _Unwind_Word tmp;

      e = read_uleb128 (e, &tmp);

      // Zero signals the end of the list.  If we've not found
      // a match by now, then we've failed the specification.
      if (tmp == 0)
        return false;

      // Match a ttype entry.
      catch_type = get_ttype_entry (info, tmp);

      // ??? There is currently no way to ask the RTTI code about the
      // relationship between two types without reference to a specific
      // object.  There should be; then we wouldn't need to mess with
      // thrown_ptr here.
      if (get_adjusted_ptr (catch_type, throw_type, &thrown_ptr))
	return true;
    }
}

// Return true if the filter spec is empty, ie throw().

static bool
empty_exception_spec (lsda_header_info *info, _Unwind_Sword filter_value)
{
  const unsigned char *e = info->TType - filter_value - 1;
  _Unwind_Word tmp;

  e = read_uleb128 (e, &tmp);
  return tmp == 0;
}

// Using a different personality function name causes link failures
// when trying to mix code using different exception handling models.
#ifdef _GLIBCPP_SJLJ_EXCEPTIONS
#define PERSONALITY_FUNCTION	__gxx_personality_sj0
#define __builtin_eh_return_data_regno(x) x
#else
#define PERSONALITY_FUNCTION	__gxx_personality_v0
#endif

extern "C" _Unwind_Reason_Code
PERSONALITY_FUNCTION (int version,
		      _Unwind_Action actions,
		      _Unwind_Exception_Class exception_class,
		      struct _Unwind_Exception *ue_header,
		      struct _Unwind_Context *context)
{
  __cxa_exception *xh = __get_exception_header_from_ue (ue_header);

  enum found_handler_type
  {
    found_nothing,
    found_terminate,
    found_cleanup,
    found_handler
  } found_type;

  lsda_header_info info;
  const unsigned char *language_specific_data;
  const unsigned char *action_record;
  const unsigned char *p;
  _Unwind_Ptr landing_pad, ip;
  int handler_switch_value;
  void *thrown_ptr = xh + 1;

  // Interface version check.
  if (version != 1)
    return _URC_FATAL_PHASE1_ERROR;

  // Shortcut for phase 2 found handler for domestic exception.
  if (actions == (_UA_CLEANUP_PHASE | _UA_HANDLER_FRAME)
      && exception_class == __gxx_exception_class)
    {
      handler_switch_value = xh->handlerSwitchValue;
      language_specific_data = xh->languageSpecificData;
      landing_pad = (_Unwind_Ptr) xh->catchTemp;
      found_type = (landing_pad == 0 ? found_terminate : found_handler);
      goto install_context;
    }

  language_specific_data = (const unsigned char *)
    _Unwind_GetLanguageSpecificData (context);

  // If no LSDA, then there are no handlers or cleanups.
  if (! language_specific_data)
    return _URC_CONTINUE_UNWIND;

  // Parse the LSDA header.
  p = parse_lsda_header (context, language_specific_data, &info);
  info.ttype_base = base_of_encoded_value (info.ttype_encoding, context);
  ip = _Unwind_GetIP (context) - 1;
  landing_pad = 0;
  action_record = 0;
  handler_switch_value = 0;

#ifdef _GLIBCPP_SJLJ_EXCEPTIONS
  // The given "IP" is an index into the call-site table, with two
  // exceptions -- -1 means no-action, and 0 means terminate.  But
  // since we're using uleb128 values, we've not got random access
  // to the array.
  if ((int) ip < 0)
    return _URC_CONTINUE_UNWIND;
  else if (ip == 0)
    {
      // Fall through to set found_terminate.
    }
  else
    {
      _Unwind_Word cs_lp, cs_action;
      do
	{
	  p = read_uleb128 (p, &cs_lp);
	  p = read_uleb128 (p, &cs_action);
	}
      while (--ip);

      // Can never have null landing pad for sjlj -- that would have
      // been indicated by a -1 call site index.
      landing_pad = cs_lp + 1;
      if (cs_action)
	action_record = info.action_table + cs_action - 1;
      goto found_something;
    }
#else
  // Search the call-site table for the action associated with this IP.
  while (p < info.action_table)
    {
      _Unwind_Ptr cs_start, cs_len, cs_lp;
      _Unwind_Word cs_action;

      // Note that all call-site encodings are "absolute" displacements.
      p = read_encoded_value (0, info.call_site_encoding, p, &cs_start);
      p = read_encoded_value (0, info.call_site_encoding, p, &cs_len);
      p = read_encoded_value (0, info.call_site_encoding, p, &cs_lp);
      p = read_uleb128 (p, &cs_action);

      // The table is sorted, so if we've passed the ip, stop.
      if (ip < info.Start + cs_start)
	p = info.action_table;
      else if (ip < info.Start + cs_start + cs_len)
	{
	  if (cs_lp)
	    landing_pad = info.LPStart + cs_lp;
	  if (cs_action)
	    action_record = info.action_table + cs_action - 1;
	  goto found_something;
	}
    }
#endif // _GLIBCPP_SJLJ_EXCEPTIONS

  // If ip is not present in the table, call terminate.  This is for
  // a destructor inside a cleanup, or a library routine the compiler
  // was not expecting to throw.
  found_type = found_terminate;
  goto do_something;

 found_something:
  if (landing_pad == 0)
    {
      // If ip is present, and has a null landing pad, there are
      // no cleanups or handlers to be run.
      found_type = found_nothing;
    }
  else if (action_record == 0)
    {
      // If ip is present, has a non-null landing pad, and a null
      // action table offset, then there are only cleanups present.
      // Cleanups use a zero switch value, as set above.
      found_type = found_cleanup;
    }
  else
    {
      // Otherwise we have a catch handler or exception specification.

      _Unwind_Sword ar_filter, ar_disp;
      const std::type_info *throw_type, *catch_type;
      bool saw_cleanup = false;
      bool saw_handler = false;

      // During forced unwinding, we only run cleanups.  With a foreign
      // exception class, there's no exception type.
      // ??? What to do about GNU Java and GNU Ada exceptions.

      if ((actions & _UA_FORCE_UNWIND)
	  || exception_class != __gxx_exception_class)
	throw_type = 0;
      else
	throw_type = xh->exceptionType;

      while (1)
	{
	  p = action_record;
	  p = read_sleb128 (p, &ar_filter);
	  read_sleb128 (p, &ar_disp);

	  if (ar_filter == 0)
	    {
	      // Zero filter values are cleanups.
	      saw_cleanup = true;
	    }
	  else if (ar_filter > 0)
	    {
	      // Positive filter values are handlers.
	      catch_type = get_ttype_entry (&info, ar_filter);

	      // Null catch type is a catch-all handler; we can catch foreign
	      // exceptions with this.  Otherwise we must match types.
	      if (! catch_type
		  || (throw_type
		      && get_adjusted_ptr (catch_type, throw_type,
					   &thrown_ptr)))
		{
		  saw_handler = true;
		  break;
		}
	    }
	  else
	    {
	      // Negative filter values are exception specifications.
	      // ??? How do foreign exceptions fit in?  As far as I can
	      // see we can't match because there's no __cxa_exception
	      // object to stuff bits in for __cxa_call_unexpected to use.
	      // Allow them iff the exception spec is non-empty.  I.e.
	      // a throw() specification results in __unexpected.
	      if (throw_type
		  ? ! check_exception_spec (&info, throw_type, thrown_ptr,
					    ar_filter)
		  : empty_exception_spec (&info, ar_filter))
		{
		  saw_handler = true;
		  break;
		}
	    }

	  if (ar_disp == 0)
	    break;
	  action_record = p + ar_disp;
	}

      if (saw_handler)
	{
	  handler_switch_value = ar_filter;
	  found_type = found_handler;
	}
      else
	found_type = (saw_cleanup ? found_cleanup : found_nothing);
    }

 do_something:
   if (found_type == found_nothing)
     return _URC_CONTINUE_UNWIND;

  if (actions & _UA_SEARCH_PHASE)
    {
      if (found_type == found_cleanup)
	return _URC_CONTINUE_UNWIND;

      // For domestic exceptions, we cache data from phase 1 for phase 2.
      if (exception_class == __gxx_exception_class)
        {
          xh->handlerSwitchValue = handler_switch_value;
          xh->actionRecord = action_record;
          xh->languageSpecificData = language_specific_data;
          xh->adjustedPtr = thrown_ptr;

          // ??? Completely unknown what this field is supposed to be for.
          // ??? Need to cache TType encoding base for call_unexpected.
          xh->catchTemp = landing_pad;
	}
      return _URC_HANDLER_FOUND;
    }

 install_context:
  // We can't use any of the cxa routines with foreign exceptions,
  // because they all expect ue_header to be a struct __cxa_exception.
  // So in that case, call terminate or unexpected directly.
  if ((actions & _UA_FORCE_UNWIND)
      || exception_class != __gxx_exception_class)
    {
      if (found_type == found_terminate)
	std::terminate ();
      else if (handler_switch_value < 0)
	{
	  try 
	    { std::unexpected (); } 
	  catch(...) 
	    { std::terminate (); }
	}
    }
  else
    {
      if (found_type == found_terminate)
	{
	  __cxa_begin_catch (&xh->unwindHeader);
	  __terminate (xh->terminateHandler);
	}

      // Cache the TType base value for __cxa_call_unexpected, as we won't
      // have an _Unwind_Context then.
      if (handler_switch_value < 0)
	{
	  parse_lsda_header (context, language_specific_data, &info);
	  xh->catchTemp = base_of_encoded_value (info.ttype_encoding, context);
	}
    }

  _Unwind_SetGR (context, __builtin_eh_return_data_regno (0),
		 (_Unwind_Ptr) &xh->unwindHeader);
  _Unwind_SetGR (context, __builtin_eh_return_data_regno (1),
		 handler_switch_value);
  _Unwind_SetIP (context, landing_pad);
  return _URC_INSTALL_CONTEXT;
}

extern "C" void
__cxa_call_unexpected (void *exc_obj_in)
{
  _Unwind_Exception *exc_obj
    = reinterpret_cast <_Unwind_Exception *>(exc_obj_in);

  __cxa_begin_catch (exc_obj);

  // This function is a handler for our exception argument.  If we exit
  // by throwing a different exception, we'll need the original cleaned up.
  struct end_catch_protect
  {
    end_catch_protect() { }
    ~end_catch_protect() { __cxa_end_catch(); }
  } end_catch_protect_obj;

  lsda_header_info info;
  __cxa_exception *xh = __get_exception_header_from_ue (exc_obj);
  const unsigned char *xh_lsda;
  _Unwind_Sword xh_switch_value;
  std::terminate_handler xh_terminate_handler;

  // If the unexpectedHandler rethrows the exception (e.g. to categorize it),
  // it will clobber data about the current handler.  So copy the data out now.
  xh_lsda = xh->languageSpecificData;
  xh_switch_value = xh->handlerSwitchValue;
  xh_terminate_handler = xh->terminateHandler;
  info.ttype_base = (_Unwind_Ptr) xh->catchTemp;

  try 
    { __unexpected (xh->unexpectedHandler); } 
  catch(...) 
    {
      // Get the exception thrown from unexpected.

      __cxa_eh_globals *globals = __cxa_get_globals_fast ();
      __cxa_exception *new_xh = globals->caughtExceptions;
      void *new_ptr = new_xh + 1;

      // We don't quite have enough stuff cached; re-parse the LSDA.
      parse_lsda_header (0, xh_lsda, &info);

      // If this new exception meets the exception spec, allow it.
      if (check_exception_spec (&info, new_xh->exceptionType,
				new_ptr, xh_switch_value))
	__throw_exception_again;

      // If the exception spec allows std::bad_exception, throw that.
      // We don't have a thrown object to compare against, but since
      // bad_exception doesn't have virtual bases, that's OK; just pass 0.
#ifdef __EXCEPTIONS  
      const std::type_info &bad_exc = typeid (std::bad_exception);
      if (check_exception_spec (&info, &bad_exc, 0, xh_switch_value))
	throw std::bad_exception();
#endif   

      // Otherwise, die.
      __terminate (xh_terminate_handler);
    }
}
