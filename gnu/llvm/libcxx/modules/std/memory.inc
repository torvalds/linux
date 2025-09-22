// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [pointer.traits], pointer traits
  using std::pointer_traits;

  // [pointer.conversion], pointer conversion
  using std::to_address;

  // [ptr.align], pointer alignment
  using std::align;
  using std::assume_aligned;

  // [obj.lifetime], explicit lifetime management
  //  using std::start_lifetime_as;
  //  using std::start_lifetime_as_array;

  // [allocator.tag], allocator argument tag
  using std::allocator_arg;
  using std::allocator_arg_t;

  // [allocator.uses], uses_allocator
  using std::uses_allocator;

  // [allocator.uses.trait], uses_allocator
  using std::uses_allocator_v;

  // [allocator.uses.construction], uses-allocator construction
  using std::uses_allocator_construction_args;

  using std::make_obj_using_allocator;
  using std::uninitialized_construct_using_allocator;

  // [allocator.traits], allocator traits
  using std::allocator_traits;

#if _LIBCPP_STD_VER >= 23
  using std::allocation_result;
#endif

  // [default.allocator], the default allocator
  using std::allocator;
  using std::operator==;

  // [specialized.addressof], addressof
  using std::addressof;

  // [specialized.algorithms], specialized algorithms
  // [special.mem.concepts], special memory concepts

  using std::uninitialized_default_construct;
  using std::uninitialized_default_construct_n;

  namespace ranges {
    using std::ranges::uninitialized_default_construct;
    using std::ranges::uninitialized_default_construct_n;
  } // namespace ranges

  using std::uninitialized_value_construct;
  using std::uninitialized_value_construct_n;

  namespace ranges {
    using std::ranges::uninitialized_value_construct;
    using std::ranges::uninitialized_value_construct_n;
  } // namespace ranges

  using std::uninitialized_copy;
  using std::uninitialized_copy_n;

  namespace ranges {
    using std::ranges::uninitialized_copy;
    using std::ranges::uninitialized_copy_result;

    using std::ranges::uninitialized_copy_n;
    using std::ranges::uninitialized_copy_n_result;
  } // namespace ranges

  using std::uninitialized_move;
  using std::uninitialized_move_n;

  namespace ranges {
    using std::ranges::uninitialized_move;
    using std::ranges::uninitialized_move_result;

    using std::ranges::uninitialized_move_n;
    using std::ranges::uninitialized_move_n_result;
  } // namespace ranges

  using std::uninitialized_fill;
  using std::uninitialized_fill_n;

  namespace ranges {
    using std::ranges::uninitialized_fill;
    using std::ranges::uninitialized_fill_n;
  } // namespace ranges

  // [specialized.construct], construct_at
  using std::construct_at;

  namespace ranges {
    using std::ranges::construct_at;
  }
  // [specialized.destroy], destroy
  using std::destroy;
  using std::destroy_at;
  using std::destroy_n;

  namespace ranges {
    using std::ranges::destroy;
    using std::ranges::destroy_at;
    using std::ranges::destroy_n;
  } // namespace ranges

  // [unique.ptr], class template unique_ptr
  using std::default_delete;
  using std::unique_ptr;

  using std::make_unique;
  using std::make_unique_for_overwrite;

  using std::operator<;
  using std::operator>;
  using std::operator<=;
  using std::operator>=;
  using std::operator<=>;

  using std::operator<<;

  // [util.smartptr.weak.bad], class bad_weak_ptr
  using std::bad_weak_ptr;

  // [util.smartptr.shared], class template shared_ptr
  using std::shared_ptr;

  // [util.smartptr.shared.create], shared_ptr creation
  using std::allocate_shared;
  using std::allocate_shared_for_overwrite;
  using std::make_shared;
  using std::make_shared_for_overwrite;

  // [util.smartptr.shared.spec], shared_ptr specialized algorithms
  using std::swap;

  // [util.smartptr.shared.cast], shared_ptr casts
  using std::const_pointer_cast;
  using std::dynamic_pointer_cast;
  using std::reinterpret_pointer_cast;
  using std::static_pointer_cast;

#ifndef _LIBCPP_HAS_NO_RTTI
  using std::get_deleter;
#endif // _LIBCPP_HAS_NO_RTTI

  // [util.smartptr.shared.io], shared_ptr I/O

  // [util.smartptr.weak], class template weak_ptr
  using std::weak_ptr;

  // [util.smartptr.weak.spec], weak_ptr specialized algorithms

  // [util.smartptr.ownerless], class template owner_less
  using std::owner_less;

  // [util.smartptr.enab], class template enable_shared_from_this
  using std::enable_shared_from_this;

  // [util.smartptr.hash], hash support
  using std::hash;

  // [util.smartptr.atomic], atomic smart pointers
  // using std::atomic;

#if _LIBCPP_STD_VER >= 23
  // [out.ptr.t], class template out_ptr_t
  using std::out_ptr_t;

  // [out.ptr], function template out_ptr
  using std::out_ptr;

  // [inout.ptr.t], class template inout_ptr_t
  using std::inout_ptr_t;

  // [inout.ptr], function template inout_ptr
  using std::inout_ptr;
#endif // _LIBCPP_STD_VER >= 23

#ifndef _LIBCPP_HAS_NO_THREADS
  // [depr.util.smartptr.shared.atomic]
  using std::atomic_is_lock_free;

  using std::atomic_load;
  using std::atomic_load_explicit;

  using std::atomic_store;
  using std::atomic_store_explicit;

  using std::atomic_exchange;
  using std::atomic_exchange_explicit;

  using std::atomic_compare_exchange_strong;
  using std::atomic_compare_exchange_strong_explicit;
  using std::atomic_compare_exchange_weak;
  using std::atomic_compare_exchange_weak_explicit;
#endif // _LIBCPP_HAS_NO_THREADS
} // namespace std
