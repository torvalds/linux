//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __PRIVATE_TYPEINFO_H_
#define __PRIVATE_TYPEINFO_H_

#include "__cxxabi_config.h"

#include <typeinfo>
#include <stddef.h>

namespace __cxxabiv1 {

class _LIBCXXABI_TYPE_VIS __shim_type_info : public std::type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__shim_type_info();

  _LIBCXXABI_HIDDEN virtual void noop1() const;
  _LIBCXXABI_HIDDEN virtual void noop2() const;
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *thrown_type,
                                           void *&adjustedPtr) const = 0;
};

class _LIBCXXABI_TYPE_VIS __fundamental_type_info : public __shim_type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__fundamental_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
};

class _LIBCXXABI_TYPE_VIS __array_type_info : public __shim_type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__array_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
};

class _LIBCXXABI_TYPE_VIS __function_type_info : public __shim_type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__function_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
};

class _LIBCXXABI_TYPE_VIS __enum_type_info : public __shim_type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__enum_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
};

enum
{
    unknown = 0,
    public_path,
    not_public_path,
    yes,
    no
};

class _LIBCXXABI_TYPE_VIS __class_type_info;

struct _LIBCXXABI_HIDDEN __dynamic_cast_info
{
// const data supplied to the search:

    const __class_type_info* dst_type;
    const void* static_ptr;
    const __class_type_info* static_type;
    ptrdiff_t src2dst_offset;

// Data that represents the answer:

    // pointer to a dst_type which has (static_ptr, static_type) above it
    const void* dst_ptr_leading_to_static_ptr;
    // pointer to a dst_type which does not have (static_ptr, static_type) above it
    const void* dst_ptr_not_leading_to_static_ptr;

    // The following three paths are either unknown, public_path or not_public_path.
    // access of path from dst_ptr_leading_to_static_ptr to (static_ptr, static_type)
    int path_dst_ptr_to_static_ptr;
    // access of path from (dynamic_ptr, dynamic_type) to (static_ptr, static_type)
    //    when there is no dst_type along the path
    int path_dynamic_ptr_to_static_ptr;
    // access of path from (dynamic_ptr, dynamic_type) to dst_type
    //    (not used if there is a (static_ptr, static_type) above a dst_type).
    int path_dynamic_ptr_to_dst_ptr;

    // Number of dst_types below (static_ptr, static_type)
    int number_to_static_ptr;
    // Number of dst_types not below (static_ptr, static_type)
    int number_to_dst_ptr;

// Data that helps stop the search before the entire tree is searched:

    // is_dst_type_derived_from_static_type is either unknown, yes or no.
    int is_dst_type_derived_from_static_type;
    // Number of dst_type in tree.  If 0, then that means unknown.
    int number_of_dst_type;
    // communicates to a dst_type node that (static_ptr, static_type) was found
    //    above it.
    bool found_our_static_ptr;
    // communicates to a dst_type node that a static_type was found
    //    above it, but it wasn't (static_ptr, static_type)
    bool found_any_static_type;
    // Set whenever a search can be stopped
    bool search_done;

    // Data that modifies the search mechanism.

    // There is no object (seen when we throw a null pointer to object).
    bool have_object;
    // Virtual base
    const void* vbase_cookie;
};

// Has no base class
class _LIBCXXABI_TYPE_VIS __class_type_info : public __shim_type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__class_type_info();

  _LIBCXXABI_HIDDEN void process_static_type_above_dst(__dynamic_cast_info *,
                                                       const void *,
                                                       const void *, int) const;
  _LIBCXXABI_HIDDEN void process_static_type_below_dst(__dynamic_cast_info *,
                                                       const void *, int) const;
  _LIBCXXABI_HIDDEN void process_found_base_class(__dynamic_cast_info *, void *,
                                                  int) const;
  _LIBCXXABI_HIDDEN virtual void search_above_dst(__dynamic_cast_info *,
                                                  const void *, const void *,
                                                  int, bool) const;
  _LIBCXXABI_HIDDEN virtual void
  search_below_dst(__dynamic_cast_info *, const void *, int, bool) const;
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
  _LIBCXXABI_HIDDEN virtual void
  has_unambiguous_public_base(__dynamic_cast_info *, void *, int) const;
};

// Has one non-virtual public base class at offset zero
class _LIBCXXABI_TYPE_VIS __si_class_type_info : public __class_type_info {
public:
  const __class_type_info *__base_type;

  _LIBCXXABI_HIDDEN virtual ~__si_class_type_info();

  _LIBCXXABI_HIDDEN virtual void search_above_dst(__dynamic_cast_info *,
                                                  const void *, const void *,
                                                  int, bool) const;
  _LIBCXXABI_HIDDEN virtual void
  search_below_dst(__dynamic_cast_info *, const void *, int, bool) const;
  _LIBCXXABI_HIDDEN virtual void
  has_unambiguous_public_base(__dynamic_cast_info *, void *, int) const;
};

struct _LIBCXXABI_HIDDEN __base_class_type_info
{
public:
    const __class_type_info* __base_type;
    long __offset_flags;

    enum __offset_flags_masks
    {
        __virtual_mask = 0x1,
        __public_mask  = 0x2, // base is public
        __offset_shift = 8
    };

    void search_above_dst(__dynamic_cast_info*, const void*, const void*, int, bool) const;
    void search_below_dst(__dynamic_cast_info*, const void*, int, bool) const;
    void has_unambiguous_public_base(__dynamic_cast_info*, void*, int) const;
};

// Has one or more base classes
class _LIBCXXABI_TYPE_VIS __vmi_class_type_info : public __class_type_info {
public:
  unsigned int __flags;
  unsigned int __base_count;
  __base_class_type_info __base_info[1];

  enum __flags_masks {
    __non_diamond_repeat_mask = 0x1, // has two or more distinct base class
                                     //    objects of the same type
    __diamond_shaped_mask = 0x2      // has base class object with two or
                                     //    more derived objects
  };

  _LIBCXXABI_HIDDEN virtual ~__vmi_class_type_info();

  _LIBCXXABI_HIDDEN virtual void search_above_dst(__dynamic_cast_info *,
                                                  const void *, const void *,
                                                  int, bool) const;
  _LIBCXXABI_HIDDEN virtual void
  search_below_dst(__dynamic_cast_info *, const void *, int, bool) const;
  _LIBCXXABI_HIDDEN virtual void
  has_unambiguous_public_base(__dynamic_cast_info *, void *, int) const;
};

class _LIBCXXABI_TYPE_VIS __pbase_type_info : public __shim_type_info {
public:
  unsigned int __flags;
  const __shim_type_info *__pointee;

  enum __masks {
    __const_mask = 0x1,
    __volatile_mask = 0x2,
    __restrict_mask = 0x4,
    __incomplete_mask = 0x8,
    __incomplete_class_mask = 0x10,
    __transaction_safe_mask = 0x20,
    // This implements the following proposal from cxx-abi-dev (not yet part of
    // the ABI document):
    //
    //   http://sourcerytools.com/pipermail/cxx-abi-dev/2016-October/002986.html
    //
    // This is necessary for support of http://wg21.link/p0012, which permits
    // throwing noexcept function and member function pointers and catching
    // them as non-noexcept pointers.
    __noexcept_mask = 0x40,

    // Flags that cannot be removed by a standard conversion.
    __no_remove_flags_mask = __const_mask | __volatile_mask | __restrict_mask,
    // Flags that cannot be added by a standard conversion.
    __no_add_flags_mask = __transaction_safe_mask | __noexcept_mask
  };

  _LIBCXXABI_HIDDEN virtual ~__pbase_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
};

class _LIBCXXABI_TYPE_VIS __pointer_type_info : public __pbase_type_info {
public:
  _LIBCXXABI_HIDDEN virtual ~__pointer_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
  _LIBCXXABI_HIDDEN bool can_catch_nested(const __shim_type_info *) const;
};

class _LIBCXXABI_TYPE_VIS __pointer_to_member_type_info
    : public __pbase_type_info {
public:
  const __class_type_info *__context;

  _LIBCXXABI_HIDDEN virtual ~__pointer_to_member_type_info();
  _LIBCXXABI_HIDDEN virtual bool can_catch(const __shim_type_info *,
                                           void *&) const;
  _LIBCXXABI_HIDDEN bool can_catch_nested(const __shim_type_info *) const;
};

}  // __cxxabiv1

#endif // __PRIVATE_TYPEINFO_H_
