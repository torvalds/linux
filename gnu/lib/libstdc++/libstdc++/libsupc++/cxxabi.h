// new abi support -*- C++ -*-
  
// Copyright (C) 2000, 2002 Free Software Foundation, Inc.
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

// Written by Nathan Sidwell, Codesourcery LLC, <nathan@codesourcery.com>
 
/* This file declares the new abi entry points into the runtime. It is not
   normally necessary for user programs to include this header, or use the
   entry points directly. However, this header is available should that be
   needed.
   
   Some of the entry points are intended for both C and C++, thus this header
   is includable from both C and C++. Though the C++ specific parts are not
   available in C, naturally enough.  */

#ifndef __CXXABI_H
#define __CXXABI_H 1

#ifdef __cplusplus

// We use the compiler builtins __SIZE_TYPE__ and __PTRDIFF_TYPE__ instead of
// std::size_t and std::ptrdiff_t respectively. This makes us independent of
// the conformance level of <cstddef> and whether -fhonor-std was supplied.
// <cstddef> is not currently available during compiler building anyway.
// Including <stddef.h> would be wrong, as that would rudely place size_t in
// the global namespace.

#include <typeinfo>

namespace __cxxabiv1
{

/* type information for int, float etc */
class __fundamental_type_info
  : public std::type_info
{
public:
  virtual ~__fundamental_type_info ();
public:
  explicit __fundamental_type_info (const char *__n)
    : std::type_info (__n)
    { }
};

/* type information for array objects */
class __array_type_info
  : public std::type_info
{
/* abi defined member functions */
protected:
  virtual ~__array_type_info ();
public:
  explicit __array_type_info (const char *__n)
    : std::type_info (__n)
    { }
};

/* type information for functions (both member and non-member) */
class __function_type_info
  : public std::type_info
{
/* abi defined member functions */
public:
  virtual ~__function_type_info ();
public:
  explicit __function_type_info (const char *__n)
    : std::type_info (__n)
    { }
  
/* implementation defined member functions */
protected:
  virtual bool __is_function_p () const;
};

/* type information for enumerations */
class __enum_type_info
  : public std::type_info
{
/* abi defined member functions */
public:
  virtual ~__enum_type_info ();
public:
  explicit __enum_type_info (const char *__n)
    : std::type_info (__n)
    { }
};

/* common type information for simple pointers and pointers to member */
class __pbase_type_info
  : public std::type_info
{
/* abi defined member variables */
public:
  unsigned int __flags; /* qualification of the target object */
  const std::type_info *__pointee;   /* type of pointed to object */

/* abi defined member functions */
public:
  virtual ~__pbase_type_info ();
public:
  explicit __pbase_type_info (const char *__n,
                                int __quals,
                                const std::type_info *__type)
    : std::type_info (__n), __flags (__quals), __pointee (__type)
    { }

/* implementation defined types */
public:
  enum __masks {
    __const_mask = 0x1,
    __volatile_mask = 0x2,
    __restrict_mask = 0x4,
    __incomplete_mask = 0x8,
    __incomplete_class_mask = 0x10
  };

/* implementation defined member functions */
protected:
  virtual bool __do_catch (const std::type_info *__thr_type,
                           void **__thr_obj,
                           unsigned __outer) const;
protected:
  inline virtual bool __pointer_catch (const __pbase_type_info *__thr_type,
                                       void **__thr_obj,
                                       unsigned __outer) const;
};

/* type information for simple pointers */
class __pointer_type_info
  : public __pbase_type_info
{
/* abi defined member functions */
public:
  virtual ~__pointer_type_info ();
public:
  explicit __pointer_type_info (const char *__n,
                                int __quals,
                                const std::type_info *__type)
    : __pbase_type_info (__n, __quals, __type)
    { }

/* implementation defined member functions */
protected:
  virtual bool __is_pointer_p () const;

protected:
  virtual bool __pointer_catch (const __pbase_type_info *__thr_type,
                                void **__thr_obj,
                                unsigned __outer) const;
};

class __class_type_info;

/* type information for a pointer to member variable */
class __pointer_to_member_type_info
  : public __pbase_type_info
{
/* abi defined member variables */
public:
  __class_type_info *__context;   /* class of the member */

/* abi defined member functions */
public:
  virtual ~__pointer_to_member_type_info ();
public:
  explicit __pointer_to_member_type_info (const char *__n,
                                          int __quals,
                                          const std::type_info *__type,
                                          __class_type_info *__klass)
    : __pbase_type_info (__n, __quals, __type), __context (__klass)
    { }

/* implementation defined member functions */
protected:
  virtual bool __pointer_catch (const __pbase_type_info *__thr_type,
                                void **__thr_obj,
                                unsigned __outer) const;
};

/* helper class for __vmi_class_type */
class __base_class_type_info
{
/* abi defined member variables */
public:
  const __class_type_info* __base_type;    /* base class type */
  long __offset_flags;            /* offset and info */

/* implementation defined types */
public:
  enum __offset_flags_masks {
    __virtual_mask = 0x1,
    __public_mask = 0x2,
    __hwm_bit = 2,
    __offset_shift = 8          /* bits to shift offset by */
  };
  
/* implementation defined member functions */
public:
  bool __is_virtual_p () const
    { return __offset_flags & __virtual_mask; }
  bool __is_public_p () const
    { return __offset_flags & __public_mask; }
  __PTRDIFF_TYPE__ __offset () const
    { 
      // This shift, being of a signed type, is implementation defined. GCC
      // implements such shifts as arithmetic, which is what we want.
      return static_cast<__PTRDIFF_TYPE__> (__offset_flags) >> __offset_shift;
    }
};

/* type information for a class */
class __class_type_info
  : public std::type_info
{
/* abi defined member functions */
public:
  virtual ~__class_type_info ();
public:
  explicit __class_type_info (const char *__n)
    : type_info (__n)
    { }

/* implementation defined types */
public:
  /* sub_kind tells us about how a base object is contained within a derived
     object. We often do this lazily, hence the UNKNOWN value. At other times
     we may use NOT_CONTAINED to mean not publicly contained. */
  enum __sub_kind
  {
    __unknown = 0,              /* we have no idea */
    __not_contained,            /* not contained within us (in some */
                                /* circumstances this might mean not contained */
                                /* publicly) */
    __contained_ambig,          /* contained ambiguously */
    
    __contained_virtual_mask = __base_class_type_info::__virtual_mask, /* via a virtual path */
    __contained_public_mask = __base_class_type_info::__public_mask,   /* via a public path */
    __contained_mask = 1 << __base_class_type_info::__hwm_bit,         /* contained within us */
    
    __contained_private = __contained_mask,
    __contained_public = __contained_mask | __contained_public_mask
  };

public:  
  struct __upcast_result;
  struct __dyncast_result;

/* implementation defined member functions */
protected:
  virtual bool __do_upcast (const __class_type_info *__dst_type, void **__obj_ptr) const;

protected:
  virtual bool __do_catch (const type_info *__thr_type, void **__thr_obj,
                           unsigned __outer) const;


public:
  /* Helper for upcast. See if DST is us, or one of our bases. */
  /* Return false if not found, true if found. */
  virtual bool __do_upcast (const __class_type_info *__dst,
                            const void *__obj,
                            __upcast_result &__restrict __result) const;

public:
  /* Indicate whether SRC_PTR of type SRC_TYPE is contained publicly within
     OBJ_PTR. OBJ_PTR points to a base object of our type, which is the
     destination type. SRC2DST indicates how SRC objects might be contained
     within this type.  If SRC_PTR is one of our SRC_TYPE bases, indicate the
     virtuality. Returns not_contained for non containment or private
     containment. */
  inline __sub_kind __find_public_src (__PTRDIFF_TYPE__ __src2dst,
                                       const void *__obj_ptr,
                                       const __class_type_info *__src_type,
                                       const void *__src_ptr) const;

public:
  /* dynamic cast helper. ACCESS_PATH gives the access from the most derived
     object to this base. DST_TYPE indicates the desired type we want. OBJ_PTR
     points to a base of our type within the complete object. SRC_TYPE
     indicates the static type started from and SRC_PTR points to that base
     within the most derived object. Fill in RESULT with what we find. Return
     true if we have located an ambiguous match. */
  virtual bool __do_dyncast (__PTRDIFF_TYPE__ __src2dst,
                             __sub_kind __access_path,
                             const __class_type_info *__dst_type,
                             const void *__obj_ptr,
                             const __class_type_info *__src_type,
                             const void *__src_ptr,
                             __dyncast_result &__result) const;
public:
  /* Helper for find_public_subobj. SRC2DST indicates how SRC_TYPE bases are
     inherited by the type started from -- which is not necessarily the
     current type. The current type will be a base of the destination type.
     OBJ_PTR points to the current base. */
  virtual __sub_kind __do_find_public_src (__PTRDIFF_TYPE__ __src2dst,
                                           const void *__obj_ptr,
                                           const __class_type_info *__src_type,
                                           const void *__src_ptr) const;
};

/* type information for a class with a single non-virtual base */
class __si_class_type_info
  : public __class_type_info
{
/* abi defined member variables */
public:
  const __class_type_info *__base_type;

/* abi defined member functions */
public:
  virtual ~__si_class_type_info ();
public:
  explicit __si_class_type_info (const char *__n,
                                 const __class_type_info *__base)
    : __class_type_info (__n), __base_type (__base)
    { }

/* implementation defined member functions */
protected:
  virtual bool __do_dyncast (__PTRDIFF_TYPE__ __src2dst,
                             __sub_kind __access_path,
                             const __class_type_info *__dst_type,
                             const void *__obj_ptr,
                             const __class_type_info *__src_type,
                             const void *__src_ptr,
                             __dyncast_result &__result) const;
  virtual __sub_kind __do_find_public_src (__PTRDIFF_TYPE__ __src2dst,
                                           const void *__obj_ptr,
                                           const __class_type_info *__src_type,
                                           const void *__sub_ptr) const;
  virtual bool __do_upcast (const __class_type_info *__dst,
                            const void *__obj,
                            __upcast_result &__restrict __result) const;
};

/* type information for a class with multiple and/or virtual bases */
class __vmi_class_type_info : public __class_type_info {
/* abi defined member variables */
public:
  unsigned int __flags;         /* details about the class hierarchy */
  unsigned int __base_count;    /* number of direct bases */
  __base_class_type_info __base_info[1]; /* array of bases */
  /* The array of bases uses the trailing array struct hack
     so this class is not constructable with a normal constructor. It is
     internally generated by the compiler. */

/* abi defined member functions */
public:
  virtual ~__vmi_class_type_info ();
public:
  explicit __vmi_class_type_info (const char *__n,
                                  int ___flags)
    : __class_type_info (__n), __flags (___flags), __base_count (0)
    { }

/* implementation defined types */
public:
  enum __flags_masks {
    __non_diamond_repeat_mask = 0x1,   /* distinct instance of repeated base */
    __diamond_shaped_mask = 0x2,       /* diamond shaped multiple inheritance */
    non_public_base_mask = 0x4,      /* has non-public direct or indirect base */
    public_base_mask = 0x8,          /* has public base (direct) */
    
    __flags_unknown_mask = 0x10
  };

/* implementation defined member functions */
protected:
  virtual bool __do_dyncast (__PTRDIFF_TYPE__ __src2dst,
                             __sub_kind __access_path,
                             const __class_type_info *__dst_type,
                             const void *__obj_ptr,
                             const __class_type_info *__src_type,
                             const void *__src_ptr,
                             __dyncast_result &__result) const;
  virtual __sub_kind __do_find_public_src (__PTRDIFF_TYPE__ __src2dst,
                                           const void *__obj_ptr,
                                           const __class_type_info *__src_type,
                                           const void *__src_ptr) const;
  virtual bool __do_upcast (const __class_type_info *__dst,
                            const void *__obj,
                            __upcast_result &__restrict __result) const;
};

/* dynamic cast runtime */
extern "C"
void *__dynamic_cast (const void *__src_ptr,    /* object started from */
                      const __class_type_info *__src_type, /* static type of object */
                      const __class_type_info *__dst_type, /* desired target type */
                      __PTRDIFF_TYPE__ __src2dst); /* how src and dst are related */

    /* src2dst has the following possible values
       >= 0: src_type is a unique public non-virtual base of dst_type
             dst_ptr + src2dst == src_ptr
       -1: unspecified relationship
       -2: src_type is not a public base of dst_type
       -3: src_type is a multiple public non-virtual base of dst_type */

/* array ctor/dtor routines */

/* allocate and construct array */
extern "C"
void *__cxa_vec_new (__SIZE_TYPE__ __element_count,
                     __SIZE_TYPE__ __element_size,
                     __SIZE_TYPE__ __padding_size,
                     void (*__constructor) (void *),
                     void (*__destructor) (void *));

extern "C"
void *__cxa_vec_new2 (__SIZE_TYPE__ __element_count,
                      __SIZE_TYPE__ __element_size,
                      __SIZE_TYPE__ __padding_size,
                      void (*__constructor) (void *),
                      void (*__destructor) (void *),
                      void *(*__alloc) (__SIZE_TYPE__),
                      void (*__dealloc) (void *));

extern "C"
void *__cxa_vec_new3 (__SIZE_TYPE__ __element_count,
                      __SIZE_TYPE__ __element_size,
                      __SIZE_TYPE__ __padding_size,
                      void (*__constructor) (void *),
                      void (*__destructor) (void *),
                      void *(*__alloc) (__SIZE_TYPE__),
                      void (*__dealloc) (void *, __SIZE_TYPE__));

/* construct array */
extern "C"
void __cxa_vec_ctor (void *__array_address,
                     __SIZE_TYPE__ __element_count,
                     __SIZE_TYPE__ __element_size,
                     void (*__constructor) (void *),
                     void (*__destructor) (void *));

extern "C"
void __cxa_vec_cctor (void *dest_array,
		      void *src_array,
		      __SIZE_TYPE__ element_count,
		      __SIZE_TYPE__ element_size,
		      void (*constructor) (void *, void *),
		      void (*destructor) (void *));
 
/* destruct array */
extern "C"
void __cxa_vec_dtor (void *__array_address,
                     __SIZE_TYPE__ __element_count,
                     __SIZE_TYPE__ __element_size,
                     void (*__destructor) (void *));

/* destruct array */
extern "C"
void __cxa_vec_cleanup (void *__array_address,
			__SIZE_TYPE__ __element_count,
			__SIZE_TYPE__ __element_size,
			void (*__destructor) (void *));

/* destruct and release array */
extern "C"
void __cxa_vec_delete (void *__array_address,
                       __SIZE_TYPE__ __element_size,
                       __SIZE_TYPE__ __padding_size,
                       void (*__destructor) (void *));

extern "C"
void __cxa_vec_delete2 (void *__array_address,
                        __SIZE_TYPE__ __element_size,
                        __SIZE_TYPE__ __padding_size,
                        void (*__destructor) (void *),
                        void (*__dealloc) (void *));
                  
extern "C"
void __cxa_vec_delete3 (void *__array_address,
                        __SIZE_TYPE__ __element_size,
                        __SIZE_TYPE__ __padding_size,
                        void (*__destructor) (void *),
                        void (*__dealloc) (void *, __SIZE_TYPE__));

/* guard variables */

/* The ABI requires a 64-bit type.  */
__extension__ typedef int __guard __attribute__((mode (__DI__)));

extern "C"
int __cxa_guard_acquire (__guard *);

extern "C"
void __cxa_guard_release (__guard *);

extern "C"
void __cxa_guard_abort (__guard *);

/* pure virtual functions */

extern "C" void
__cxa_pure_virtual (void);

/* exception handling */

extern "C" void
__cxa_bad_cast ();

extern "C" void
__cxa_bad_typeid ();

/* DSO destruction */

extern "C" int
__cxa_atexit (void (*)(void *), void *, void *);

extern "C" int
__cxa_finalize (void *);

/* demangling routines */

extern "C" 
char *__cxa_demangle (const char *__mangled_name,
		      char *__output_buffer,
		      __SIZE_TYPE__ *__length,
		      int *__status);

// Returns the type_info for the currently handled exception [15.3/8], or
// null if there is none.
extern "C"
std::type_info *__cxa_current_exception_type ();

} /* namespace __cxxabiv1 */

/* User programs should use the alias `abi'. */
namespace abi = __cxxabiv1;

#else
#endif /* __cplusplus */


#endif /* __CXXABI_H */
