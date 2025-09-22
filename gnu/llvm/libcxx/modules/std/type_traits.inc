// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [meta.help], helper class
  using std::integral_constant;

  using std::bool_constant;
  using std::false_type;
  using std::true_type;

  // [meta.unary.cat], primary type categories
  using std::is_array;
  using std::is_class;
  using std::is_enum;
  using std::is_floating_point;
  using std::is_function;
  using std::is_integral;
  using std::is_lvalue_reference;
  using std::is_member_function_pointer;
  using std::is_member_object_pointer;
  using std::is_null_pointer;
  using std::is_pointer;
  using std::is_rvalue_reference;
  using std::is_union;
  using std::is_void;

  // [meta.unary.comp], composite type categories
  using std::is_arithmetic;
  using std::is_compound;
  using std::is_fundamental;
  using std::is_member_pointer;
  using std::is_object;
  using std::is_reference;
  using std::is_scalar;

  // [meta.unary.prop], type properties
  using std::is_abstract;
  using std::is_aggregate;
  using std::is_const;
  using std::is_empty;
  using std::is_final;
  using std::is_polymorphic;
  using std::is_standard_layout;
  using std::is_trivial;
  using std::is_trivially_copyable;
  using std::is_volatile;

  using std::is_bounded_array;
#if _LIBCPP_STD_VER >= 23
  using std::is_scoped_enum;
#endif
  using std::is_signed;
  using std::is_unbounded_array;
  using std::is_unsigned;

  using std::is_constructible;
  using std::is_copy_constructible;
  using std::is_default_constructible;
  using std::is_move_constructible;

  using std::is_assignable;
  using std::is_copy_assignable;
  using std::is_move_assignable;

  using std::is_swappable;
  using std::is_swappable_with;

  using std::is_destructible;

  using std::is_trivially_constructible;
  using std::is_trivially_copy_constructible;
  using std::is_trivially_default_constructible;
  using std::is_trivially_move_constructible;

  using std::is_trivially_assignable;
  using std::is_trivially_copy_assignable;
  using std::is_trivially_destructible;
  using std::is_trivially_move_assignable;

  using std::is_nothrow_constructible;
  using std::is_nothrow_copy_constructible;
  using std::is_nothrow_default_constructible;
  using std::is_nothrow_move_constructible;

  using std::is_nothrow_assignable;
  using std::is_nothrow_copy_assignable;
  using std::is_nothrow_move_assignable;

  using std::is_nothrow_swappable;
  using std::is_nothrow_swappable_with;

  using std::is_nothrow_destructible;

  //  using std::is_implicit_lifetime;

  using std::has_virtual_destructor;

  using std::has_unique_object_representations;

  // using std::reference_constructs_from_temporary;
  // using std::reference_converts_from_temporary;

  // [meta.unary.prop.query], type property queries
  using std::alignment_of;
  using std::extent;
  using std::rank;

  // [meta.rel], type relations
  using std::is_base_of;
  using std::is_convertible;
  // using std::is_layout_compatible;
  using std::is_nothrow_convertible;
  // using std::is_pointer_interconvertible_base_of;
  using std::is_same;

  using std::is_invocable;
  using std::is_invocable_r;

  using std::is_nothrow_invocable;
  using std::is_nothrow_invocable_r;

  // [meta.trans.cv], const-volatile modifications
  using std::add_const;
  using std::add_cv;
  using std::add_volatile;
  using std::remove_const;
  using std::remove_cv;
  using std::remove_volatile;

  using std::add_const_t;
  using std::add_cv_t;
  using std::add_volatile_t;
  using std::remove_const_t;
  using std::remove_cv_t;
  using std::remove_volatile_t;

  // [meta.trans.ref], reference modifications
  using std::add_lvalue_reference;
  using std::add_rvalue_reference;
  using std::remove_reference;

  using std::add_lvalue_reference_t;
  using std::add_rvalue_reference_t;
  using std::remove_reference_t;

  // [meta.trans.sign], sign modifications
  using std::make_signed;
  using std::make_unsigned;

  using std::make_signed_t;
  using std::make_unsigned_t;

  // [meta.trans.arr], array modifications
  using std::remove_all_extents;
  using std::remove_extent;

  using std::remove_all_extents_t;
  using std::remove_extent_t;

  // [meta.trans.ptr], pointer modifications
  using std::add_pointer;
  using std::remove_pointer;

  using std::add_pointer_t;
  using std::remove_pointer_t;

  // [meta.trans.other], other transformations
  using std::basic_common_reference;
  using std::common_reference;
  using std::common_type;
  using std::conditional;
  using std::decay;
  using std::enable_if;
  using std::invoke_result;
  using std::remove_cvref;
  using std::type_identity;
  using std::underlying_type;
  using std::unwrap_ref_decay;
  using std::unwrap_reference;

  using std::common_reference_t;
  using std::common_type_t;
  using std::conditional_t;
  using std::decay_t;
  using std::enable_if_t;
  using std::invoke_result_t;
  using std::remove_cvref_t;
  using std::type_identity_t;
  using std::underlying_type_t;
  using std::unwrap_ref_decay_t;
  using std::unwrap_reference_t;
  using std::void_t;

  // [meta.logical], logical operator traits
  using std::conjunction;
  using std::disjunction;
  using std::negation;

  // [meta.unary.cat], primary type categories
  using std::is_array_v;
  using std::is_class_v;
  using std::is_enum_v;
  using std::is_floating_point_v;
  using std::is_function_v;
  using std::is_integral_v;
  using std::is_lvalue_reference_v;
  using std::is_member_function_pointer_v;
  using std::is_member_object_pointer_v;
  using std::is_null_pointer_v;
  using std::is_pointer_v;
  using std::is_rvalue_reference_v;
  using std::is_union_v;
  using std::is_void_v;

  // [meta.unary.comp], composite type categories
  using std::is_arithmetic_v;
  using std::is_compound_v;
  using std::is_fundamental_v;
  using std::is_member_pointer_v;
  using std::is_object_v;
  using std::is_reference_v;
  using std::is_scalar_v;

  // [meta.unary.prop], type properties
  using std::has_unique_object_representations_v;
  using std::has_virtual_destructor_v;
  using std::is_abstract_v;
  using std::is_aggregate_v;
  using std::is_assignable_v;
  using std::is_bounded_array_v;
  using std::is_const_v;
  using std::is_constructible_v;
  using std::is_copy_assignable_v;
  using std::is_copy_constructible_v;
  using std::is_default_constructible_v;
  using std::is_destructible_v;
  using std::is_empty_v;
  using std::is_final_v;
  // using std::is_implicit_lifetime_v;
  using std::is_move_assignable_v;
  using std::is_move_constructible_v;
  using std::is_nothrow_assignable_v;
  using std::is_nothrow_constructible_v;
  using std::is_nothrow_copy_assignable_v;
  using std::is_nothrow_copy_constructible_v;
  using std::is_nothrow_default_constructible_v;
  using std::is_nothrow_destructible_v;
  using std::is_nothrow_move_assignable_v;
  using std::is_nothrow_move_constructible_v;
  using std::is_nothrow_swappable_v;
  using std::is_nothrow_swappable_with_v;
  using std::is_polymorphic_v;
#if _LIBCPP_STD_VER >= 23
  using std::is_scoped_enum_v;
#endif
  using std::is_signed_v;
  using std::is_standard_layout_v;
  using std::is_swappable_v;
  using std::is_swappable_with_v;
  using std::is_trivial_v;
  using std::is_trivially_assignable_v;
  using std::is_trivially_constructible_v;
  using std::is_trivially_copy_assignable_v;
  using std::is_trivially_copy_constructible_v;
  using std::is_trivially_copyable_v;
  using std::is_trivially_default_constructible_v;
  using std::is_trivially_destructible_v;
  using std::is_trivially_move_assignable_v;
  using std::is_trivially_move_constructible_v;
  using std::is_unbounded_array_v;
  using std::is_unsigned_v;
  using std::is_volatile_v;
  // using std::reference_constructs_from_temporary_v;
  // using std::reference_converts_from_temporary_v;

  // [meta.unary.prop.query], type property queries
  using std::alignment_of_v;
  using std::extent_v;
  using std::rank_v;

  // [meta.rel], type relations
  using std::is_base_of_v;
  using std::is_convertible_v;
  using std::is_invocable_r_v;
  using std::is_invocable_v;
  // using std::is_layout_compatible_v;
  using std::is_nothrow_convertible_v;
  using std::is_nothrow_invocable_r_v;
  using std::is_nothrow_invocable_v;
  // using std::is_pointer_interconvertible_base_of_v;
  using std::is_same_v;

  // [meta.logical], logical operator traits
  using std::conjunction_v;
  using std::disjunction_v;
  using std::negation_v;

  // [meta.member], member relationships
  // using std::is_corresponding_member;
  // using std::is_pointer_interconvertible_with_class;

  // [meta.const.eval], constant evaluation context
  using std::is_constant_evaluated;

  // [depr.meta.types]
  using std::aligned_storage;
  using std::aligned_storage_t;
  using std::aligned_union;
  using std::aligned_union_t;
  using std::is_pod;
  using std::is_pod_v;
} // namespace std
