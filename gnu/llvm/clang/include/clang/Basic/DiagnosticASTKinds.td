//==--- DiagnosticASTKinds.td - libast diagnostics ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let Component = "AST" in {

// Constant expression diagnostics. These (and their users) belong in Sema.
def note_expr_divide_by_zero : Note<"division by zero">;
def note_constexpr_invalid_cast : Note<
  "%select{reinterpret_cast|dynamic_cast|%select{this conversion|cast that"
  " performs the conversions of a reinterpret_cast}1|cast from %1}0"
  " is not allowed in a constant expression"
  "%select{| in C++ standards before C++20||}0">;
def note_constexpr_invalid_void_star_cast : Note<
  "cast from %0 is not allowed in a constant expression "
  "%select{in C++ standards before C++2c|because the pointed object "
  "type %2 is not similar to the target type %3}1">;
def note_constexpr_invalid_downcast : Note<
  "cannot cast object of dynamic type %0 to type %1">;
def note_constexpr_overflow : Note<
  "value %0 is outside the range of representable values of type %1">;
def note_constexpr_negative_shift : Note<"negative shift count %0">;
def note_constexpr_large_shift : Note<
  "shift count %0 >= width of type %1 (%2 bit%s2)">;
def note_constexpr_lshift_of_negative : Note<"left shift of negative value %0">;
def note_constexpr_lshift_discards : Note<"signed left shift discards bits">;
def note_constexpr_invalid_function : Note<
  "%select{non-constexpr|undefined}0 %select{function|constructor}1 %2 cannot "
  "be used in a constant expression">;
def note_constexpr_invalid_inhctor : Note<
  "constructor inherited from base class %0 cannot be used in a "
  "constant expression; derived class cannot be implicitly initialized">;
def note_constexpr_no_return : Note<
  "control reached end of constexpr function">;
def note_constexpr_virtual_call : Note<
  "cannot evaluate call to virtual function in a constant expression "
  "in C++ standards before C++20">;
def note_constexpr_pure_virtual_call : Note<
  "pure virtual function %q0 called">;
def note_constexpr_polymorphic_unknown_dynamic_type : Note<
  "%select{|||||virtual function called on|dynamic_cast applied to|"
  "typeid applied to|construction of|destruction of}0 object '%1' "
  "whose dynamic type is not constant">;
def note_constexpr_dynamic_cast_to_reference_failed : Note<
  "reference dynamic_cast failed: %select{"
  "static type %1 of operand is a non-public base class of dynamic type %2|"
  "dynamic type %2 of operand does not have a base class of type %3|"
  "%3 is an ambiguous base class of dynamic type %2 of operand|"
  "%3 is a non-public base class of dynamic type %2 of operand}0">;
def note_constexpr_virtual_base : Note<
  "cannot construct object of type %0 with virtual base class "
  "in a constant expression">;
def note_constexpr_nonliteral : Note<
  "non-literal type %0 cannot be used in a constant expression">;
def note_constexpr_non_global : Note<
  "%select{pointer|reference}0 to %select{|subobject of }1"
  "%select{temporary|%3}2 is not a constant expression">;
def note_constexpr_not_static : Note<
  "address of non-static constexpr variable %0 may differ on each invocation "
  "of the enclosing function; add 'static' to give it a constant address">;
def note_constexpr_dynamic_alloc : Note<
  "%select{pointer|reference}0 to %select{|subobject of }1"
  "heap-allocated object is not a constant expression">;
def note_consteval_address_accessible : Note<
  "%select{pointer|reference}0 to a consteval declaration "
  "is not a constant expression">;
def note_constexpr_uninitialized : Note<
  "subobject %select{of type |}0%1 is not initialized">;
def note_constexpr_uninitialized_base : Note<
  "constructor of base class %0 is not called">;
def note_constexpr_static_local : Note<
  "control flows through the definition of a %select{static|thread_local}0 variable">;
def note_constexpr_subobject_declared_here : Note<
  "subobject declared here">;
def note_constexpr_array_index : Note<"cannot refer to element %0 of "
  "%select{array of %2 element%plural{1:|:s}2|non-array object}1 "
  "in a constant expression">;
def note_constexpr_float_arithmetic : Note<
  "floating point arithmetic produces %select{an infinity|a NaN}0">;
def note_constexpr_dynamic_rounding : Note<
  "cannot evaluate this expression if rounding mode is dynamic">;
def note_constexpr_float_arithmetic_strict : Note<
  "compile time floating point arithmetic suppressed in strict evaluation modes">;
def note_constexpr_pointer_subtraction_not_same_array : Note<
  "subtracted pointers are not elements of the same array">;
def note_constexpr_pointer_subtraction_zero_size : Note<
  "subtraction of pointers to type %0 of zero size">;
def note_constexpr_pointer_comparison_unspecified : Note<
  "comparison between '%0' and '%1' has unspecified value">;
def note_constexpr_pointer_constant_comparison : Note<
  "comparison of numeric address '%0' with pointer '%1' can only be performed "
  "at runtime">;
def note_constexpr_literal_comparison : Note<
  "comparison of addresses of literals has unspecified value">;
def note_constexpr_pointer_weak_comparison : Note<
  "comparison against address of weak declaration '%0' can only be performed "
  "at runtime">;
def note_constexpr_mem_pointer_weak_comparison : Note<
  "comparison against pointer to weak member %q0 can only be performed "
  "at runtime">;
def note_constexpr_pointer_comparison_past_end : Note<
  "comparison against pointer '%0' that points past the end of a "
  "complete object has unspecified value">;
def note_constexpr_pointer_comparison_zero_sized : Note<
  "comparison of pointers '%0' and '%1' to unrelated zero-sized objects">;
def note_constexpr_pointer_comparison_base_classes : Note<
  "comparison of addresses of subobjects of different base classes "
  "has unspecified value">;
def note_constexpr_pointer_comparison_base_field : Note<
  "comparison of address of base class subobject %0 of class %1 to field %2 "
  "has unspecified value">;
def note_constexpr_pointer_comparison_differing_access : Note<
  "comparison of address of fields %0 and %2 of %4 with differing access "
  "specifiers (%1 vs %3) has unspecified value">;
def note_constexpr_compare_virtual_mem_ptr : Note<
  "comparison of pointer to virtual member function %0 has unspecified value">;
def note_constexpr_past_end : Note<
  "dereferenced pointer past the end of %select{|subobject of }0"
  "%select{temporary|%2}1 is not a constant expression">;
def note_constexpr_past_end_subobject : Note<
  "cannot %select{access base class of|access derived class of|access field of|"
  "access array element of|ERROR|"
  "access real component of|access imaginary component of}0 "
  "pointer past the end of object">;
def note_non_null_attribute_failed : Note<
  "null passed to a callee that requires a non-null argument">;
def note_constexpr_null_subobject : Note<
  "cannot %select{access base class of|access derived class of|access field of|"
  "access array element of|perform pointer arithmetic on|"
  "access real component of|"
  "access imaginary component of}0 null pointer">;
def note_constexpr_null_callee : Note<
  "'%0' evaluates to a null function pointer">;
def note_constexpr_function_param_value_unknown : Note<
  "function parameter %0 with unknown value cannot be used in a constant "
  "expression">;
def note_constexpr_var_init_unknown : Note<
  "initializer of %0 is unknown">;
def note_constexpr_var_init_non_constant : Note<
  "initializer of %0 is not a constant expression">;
def note_constexpr_var_init_weak : Note<
  "initializer of weak variable %0 is not considered constant because "
  "it may be different at runtime">;
def note_constexpr_typeid_polymorphic : Note<
  "typeid applied to expression of polymorphic type %0 is "
  "not allowed in a constant expression in C++ standards before C++20">;
def note_constexpr_void_comparison : Note<
  "comparison between unequal pointers to void has unspecified result">;
def note_constexpr_temporary_here : Note<"temporary created here">;
def note_constexpr_dynamic_alloc_here : Note<"heap allocation performed here">;
def note_constexpr_conditional_never_const : Note<
  "both arms of conditional operator are unable to produce a "
  "constant expression">;
def note_constexpr_depth_limit_exceeded : Note<
  "constexpr evaluation exceeded maximum depth of %0 calls">;
def note_constexpr_call_limit_exceeded : Note<
  "constexpr evaluation hit maximum call limit">;
def note_constexpr_step_limit_exceeded : Note<
  "constexpr evaluation hit maximum step limit; possible infinite loop?">;
def note_constexpr_heap_alloc_limit_exceeded : Note<
  "constexpr evaluation hit maximum heap allocation limit">;
def note_constexpr_this : Note<
  "%select{|implicit }0use of 'this' pointer is only allowed within the "
  "evaluation of a call to a 'constexpr' member function">;
def note_constexpr_lifetime_ended : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 %select{temporary|variable}1 whose "
  "%plural{8:storage duration|:lifetime}0 has ended">;
def note_constexpr_access_uninit : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|"
  "construction of subobject of|destruction of}0 "
  "%select{object outside its lifetime|uninitialized object}1 "
  "is not allowed in a constant expression">;
def note_constexpr_use_uninit_reference : Note<
  "use of reference outside its lifetime "
  "is not allowed in a constant expression">;
def note_constexpr_modify_const_type : Note<
  "modification of object of const-qualified type %0 is not allowed "
  "in a constant expression">;
def note_constexpr_access_volatile_type : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "<ERROR>|<ERROR>|<ERROR>|<ERROR>}0 "
  "volatile-qualified type %1 is not allowed in a constant expression">;
def note_constexpr_access_volatile_obj : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "<ERROR>|<ERROR>|<ERROR>|<ERROR>}0 "
  "volatile %select{temporary|object %2|member %2}1 is not allowed in "
  "a constant expression">;
def note_constexpr_volatile_here : Note<
  "volatile %select{temporary created|object declared|member declared}0 here">;
def note_constexpr_access_mutable : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 "
  "mutable member %1 is not allowed in a constant expression">;
def note_constexpr_ltor_non_const_int : Note<
  "read of non-const variable %0 is not allowed in a constant expression">;
def note_constexpr_ltor_non_integral : Note<
  "read of variable %0 of non-integral, non-enumeration type %1 "
  "is not allowed in a constant expression">;
def note_constexpr_ltor_non_constexpr : Note<
  "read of non-constexpr variable %0 is not allowed in a constant expression">;
def note_constexpr_ltor_incomplete_type : Note<
  "read of incomplete type %0 is not allowed in a constant expression">;
def note_constexpr_access_null : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 "
  "dereferenced null pointer is not allowed in a constant expression">;
def note_constexpr_access_past_end : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 "
  "dereferenced one-past-the-end pointer is not allowed "
  "in a constant expression">;
def note_constexpr_access_unsized_array : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 "
  "element of array without known bound "
  "is not allowed in a constant expression">;
def note_constexpr_access_inactive_union_member : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|"
  "construction of subobject of|destruction of}0 "
  "member %1 of union with %select{active member %3|no active member}2 "
  "is not allowed in a constant expression">;
def note_constexpr_union_member_change_during_init : Note<
  "assignment would change active union member during the initialization of "
  "a different member of the same union">;
def note_constexpr_access_static_temporary : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|reconstruction of|"
  "destruction of}0 temporary "
  "is not allowed in a constant expression outside the expression that "
  "created the temporary">;
def note_constexpr_access_unreadable_object : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 "
  "object '%1' whose value is not known">;
def note_constexpr_access_deleted_object : Note<
  "%select{read of|read of|assignment to|increment of|decrement of|"
  "member call on|dynamic_cast of|typeid applied to|construction of|"
  "destruction of}0 "
  "heap allocated object that has been deleted">;
def note_constexpr_modify_global : Note<
  "a constant expression cannot modify an object that is visible outside "
  "that expression">;
def note_constexpr_stmt_expr_unsupported : Note<
  "this use of statement expressions is not supported in a "
  "constant expression">;
def note_constexpr_calls_suppressed : Note<
  "(skipping %0 call%s0 in backtrace; use -fconstexpr-backtrace-limit=0 to "
  "see all)">;
def note_constexpr_call_here : Note<"in call to '%0'">;
def note_constexpr_inherited_ctor_call_here : Note<
  "in implicit initialization for inherited constructor of %0">;
def note_constexpr_baa_insufficient_alignment : Note<
  "%select{alignment of|offset of the aligned pointer from}0 the base pointee "
  "object (%1 %plural{1:byte|:bytes}1) is %select{less than|not a multiple of}0 the "
  "asserted %2 %plural{1:byte|:bytes}2">;
def note_constexpr_baa_value_insufficient_alignment : Note<
  "value of the aligned pointer (%0) is not a multiple of the asserted %1 "
  "%plural{1:byte|:bytes}1">;
def note_constexpr_invalid_alignment : Note<
  "requested alignment %0 is not a positive power of two">;
def note_constexpr_alignment_too_big : Note<
  "requested alignment must be %0 or less for type %1; %2 is invalid">;
def note_constexpr_alignment_compute : Note<
  "cannot constant evaluate whether run-time alignment is at least %0">;
def note_constexpr_alignment_adjust : Note<
  "cannot constant evaluate the result of adjusting alignment to %0">;
def note_constexpr_destroy_out_of_lifetime : Note<
  "destroying object '%0' whose lifetime has already ended">;
def note_constexpr_unsupported_destruction : Note<
  "non-trivial destruction of type %0 in a constant expression is not supported">;
def note_constexpr_unsupported_temporary_nontrivial_dtor : Note<
  "non-trivial destruction of lifetime-extended temporary with type %0 "
  "used in the result of a constant expression is not yet supported">;
def note_constexpr_unsupported_unsized_array : Note<
  "array-to-pointer decay of array member without known bound is not supported">;
def note_constexpr_unsized_array_indexed : Note<
  "indexing of array without known bound is not allowed "
  "in a constant expression">;
def note_constexpr_memcmp_unsupported : Note<
  "constant evaluation of %0 between arrays of types %1 and %2 "
  "is not supported; only arrays of narrow character types can be compared">;
def note_constexpr_memchr_unsupported : Note<
  "constant evaluation of %0 on array of type %1 "
  "is not supported; only arrays of narrow character types can be searched">;
def note_constexpr_memcpy_null : Note<
  "%select{source|destination}2 of "
  "'%select{%select{memcpy|wmemcpy}1|%select{memmove|wmemmove}1}0' "
  "is %3">;
def note_constexpr_memcpy_type_pun : Note<
  "cannot constant evaluate '%select{memcpy|memmove}0' from object of "
  "type %1 to object of type %2">;
def note_constexpr_memcpy_nontrivial : Note<
  "cannot constant evaluate '%select{memcpy|memmove}0' between objects of "
  "non-trivially-copyable type %1">;
def note_constexpr_memcpy_incomplete_type : Note<
  "cannot constant evaluate '%select{memcpy|memmove}0' between objects of "
  "incomplete type %1">;
def note_constexpr_memcpy_overlap : Note<
  "'%select{memcpy|wmemcpy}0' between overlapping memory regions">;
def note_constexpr_memcpy_unsupported : Note<
  "'%select{%select{memcpy|wmemcpy}1|%select{memmove|wmemmove}1}0' "
  "not supported: %select{"
  "size to copy (%4) is not a multiple of size of element type %3 (%5)|"
  "source is not a contiguous array of at least %4 elements of type %3|"
  "destination is not a contiguous array of at least %4 elements of type %3}2">;
def note_constexpr_bit_cast_unsupported_type : Note<
  "constexpr bit cast involving type %0 is not yet supported">;
def note_constexpr_bit_cast_unsupported_bitfield : Note<
  "constexpr bit_cast involving bit-field is not yet supported">;
def note_constexpr_bit_cast_invalid_type : Note<
  "bit_cast %select{from|to}0 a %select{|type with a }1"
  "%select{union|pointer|member pointer|volatile|reference}2 "
  "%select{type|member}1 is not allowed in a constant expression">;
def note_constexpr_bit_cast_invalid_subtype : Note<
  "invalid type %0 is a %select{member|base}1 of %2">;
def note_constexpr_bit_cast_invalid_vector : Note<
  "bit_cast involving type %0 is not allowed in a constant expression; "
  "element size %1 * element count %2 is not a multiple of the byte size %3">;
def note_constexpr_bit_cast_indet_dest : Note<
  "indeterminate value can only initialize an object of type 'unsigned char'"
  "%select{, 'char',|}1 or 'std::byte'; %0 is invalid">;
def note_constexpr_bit_cast_unrepresentable_value : Note<
  "value %1 cannot be represented in type %0">;
def note_constexpr_pseudo_destructor : Note<
  "pseudo-destructor call is not permitted in constant expressions "
  "until C++20">;
def note_constexpr_construct_complex_elem : Note<
  "construction of individual component of complex number is not yet supported "
  "in constant expressions">;
def note_constexpr_destroy_complex_elem : Note<
  "destruction of individual component of complex number is not yet supported "
  "in constant expressions">;
def note_constexpr_new : Note<
  "dynamic memory allocation is not permitted in constant expressions "
  "until C++20">;
def note_constexpr_new_non_replaceable : Note<
  "call to %select{placement|class-specific}0 %1">;
def note_constexpr_new_placement : Note<
  "this placement new expression is not yet supported in constant expressions">;
def note_constexpr_placement_new_wrong_type : Note<
  "placement new would change type of storage from %0 to %1">;
def note_constexpr_new_negative : Note<
  "cannot allocate array; evaluated array bound %0 is negative">;
def note_constexpr_new_too_large : Note<
  "cannot allocate array; evaluated array bound %0 is too large">;
def note_constexpr_new_exceeds_limits : Note<
  "cannot allocate array; evaluated array bound %0 exceeds the limit (%1); "
  "use '-fconstexpr-steps' to increase this limit">;
def note_constexpr_new_too_small : Note<
  "cannot allocate array; evaluated array bound %0 is too small to hold "
  "%1 explicitly initialized elements">;
def note_constexpr_new_untyped : Note<
  "cannot allocate untyped memory in a constant expression; "
  "use 'std::allocator<T>::allocate' to allocate memory of type 'T'">;
def note_constexpr_new_not_complete_object_type : Note<
  "cannot allocate memory of %select{incomplete|function}0 type %1">;
def note_constexpr_operator_new_bad_size : Note<
  "allocated size %0 is not a multiple of size %1 of element type %2">;
def note_constexpr_delete_not_heap_alloc : Note<
  "delete of pointer '%0' that does not point to a heap-allocated object">;
def note_constexpr_double_delete : Note<
  "delete of pointer that has already been deleted">;
def note_constexpr_double_destroy : Note<
  "destruction of object that is already being destroyed">;
def note_constexpr_new_delete_mismatch : Note<
  "%plural{2:'delete' used to delete pointer to object "
  "allocated with 'std::allocator<...>::allocate'|"
  ":%select{non-array delete|array delete|'std::allocator<...>::deallocate'}0 "
  "used to delete pointer to "
  "%select{array object of type %2|non-array object of type %2|"
  "object allocated with 'new'}0}1">;
def note_constexpr_deallocate_null : Note<
  "'std::allocator<...>::deallocate' used to delete a null pointer">;
def note_constexpr_delete_subobject : Note<
  "delete of pointer%select{ to subobject|}1 '%0' "
  "%select{|that does not point to complete object}1">;
def note_constexpr_delete_base_nonvirt_dtor : Note<
  "delete of object with dynamic type %1 through pointer to "
  "base class type %0 with non-virtual destructor">;
def note_constexpr_memory_leak : Note<
  "allocation performed here was not deallocated"
  "%plural{0:|: (along with %0 other memory leak%s0)}0">;
def note_constexpr_unsupported_layout : Note<
  "type %0 has unexpected layout">;
def note_constexpr_unsupported_flexible_array : Note<
  "flexible array initialization is not yet supported">;
def note_constexpr_non_const_vectorelements : Note<
  "cannot determine number of elements for sizeless vectors in a constant expression">;
def note_constexpr_assumption_failed : Note<
  "assumption evaluated to false">;
def err_experimental_clang_interp_failed : Error<
  "the experimental clang interpreter failed to evaluate an expression">;

def warn_integer_constant_overflow : Warning<
  "overflow in expression; result is %0 with type %1">,
  InGroup<DiagGroup<"integer-overflow">>;
def warn_fixedpoint_constant_overflow : Warning<
  "overflow in expression; result is %0 with type %1">,
  InGroup<DiagGroup<"fixed-point-overflow">>;
def warn_constexpr_unscoped_enum_out_of_range : Warning<
  "integer value %0 is outside the valid range of values [%1, %2] for the "
  "enumeration type %3">, DefaultError, ShowInSystemHeader, ShowInSystemMacro,
  InGroup<DiagGroup<"enum-constexpr-conversion">>;

// This is a temporary diagnostic, and shall be removed once our
// implementation is complete, and like the preceding constexpr notes belongs
// in Sema.
def note_unimplemented_constexpr_lambda_feature_ast : Note<
    "unimplemented constexpr lambda feature: %0 (coming soon!)">;

def warn_is_constant_evaluated_always_true_constexpr : Warning<
  "'%0' will always evaluate to 'true' in a manifestly constant-evaluated expression">,
  InGroup<DiagGroup<"constant-evaluated">>;

// inline asm related.
let CategoryName = "Inline Assembly Issue" in {
  def err_asm_invalid_escape : Error<
    "invalid %% escape in inline assembly string">;
  def err_asm_unknown_symbolic_operand_name : Error<
    "unknown symbolic operand name in inline assembly string">;

  def err_asm_unterminated_symbolic_operand_name : Error<
    "unterminated symbolic operand name in inline assembly string">;
  def err_asm_empty_symbolic_operand_name : Error<
    "empty symbolic operand name in inline assembly string">;
  def err_asm_invalid_operand_number : Error<
    "invalid operand number in inline asm string">;
}

// vtable related.
let CategoryName = "VTable ABI Issue" in {
  def err_vftable_ambiguous_component : Error<
    "ambiguous vftable component for %0 introduced via covariant thunks; "
    "this is an inherent limitation of the ABI">;
  def note_covariant_thunk : Note<
    "covariant thunk required by %0">;
}

// Importing ASTs
def err_odr_variable_type_inconsistent : Error<
  "external variable %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">;
def warn_odr_variable_type_inconsistent : Warning<
  "external variable %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">,
  InGroup<ODR>;
def err_odr_variable_multiple_def : Error<
  "external variable %0 defined in multiple translation units">;
def warn_odr_variable_multiple_def : Warning<
  "external variable %0 defined in multiple translation units">,
  InGroup<ODR>;
def note_odr_value_here : Note<"declared here with type %0">;
def err_odr_function_type_inconsistent : Error<
  "external function %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">;
def warn_odr_function_type_inconsistent : Warning<
  "external function %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">,
  InGroup<ODR>;
def err_odr_tag_type_inconsistent
    : Error<"type %0 has incompatible definitions in different translation "
            "units">;
def warn_odr_tag_type_inconsistent
    : Warning<"type %0 has incompatible definitions in different translation "
              "units">,
      InGroup<ODR>;
def note_odr_tag_kind_here: Note<
  "%0 is a %select{struct|interface|union|class|enum}1 here">;
def note_odr_field : Note<"field %0 has type %1 here">;
def note_odr_field_name : Note<"field has name %0 here">;
def note_odr_missing_field : Note<"no corresponding field here">;
def note_odr_base : Note<"class has base type %0">;
def note_odr_virtual_base : Note<
  "%select{non-virtual|virtual}0 derivation here">;
def note_odr_missing_base : Note<"no corresponding base class here">;
def note_odr_number_of_bases : Note<
  "class has %0 base %plural{1:class|:classes}0">;
def note_odr_enumerator : Note<"enumerator %0 with value %1 here">;
def note_odr_missing_enumerator : Note<"no corresponding enumerator here">;
def err_odr_field_type_inconsistent : Error<
  "field %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">;
def warn_odr_field_type_inconsistent : Warning<
  "field %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">,
  InGroup<ODR>;

// Importing Objective-C ASTs
def err_odr_ivar_type_inconsistent : Error<
  "instance variable %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">;
def warn_odr_ivar_type_inconsistent : Warning<
  "instance variable %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">,
  InGroup<ODR>;
def err_odr_objc_superclass_inconsistent : Error<
  "class %0 has incompatible superclasses">;
def warn_odr_objc_superclass_inconsistent : Warning<
  "class %0 has incompatible superclasses">,
  InGroup<ODR>;
def note_odr_objc_superclass : Note<"inherits from superclass %0 here">;
def note_odr_objc_missing_superclass : Note<"no corresponding superclass here">;
def err_odr_objc_method_result_type_inconsistent : Error<
  "%select{class|instance}0 method %1 has incompatible result types in "
  "different translation units (%2 vs. %3)">;
def warn_odr_objc_method_result_type_inconsistent : Warning<
  "%select{class|instance}0 method %1 has incompatible result types in "
  "different translation units (%2 vs. %3)">,
  InGroup<ODR>;
def err_odr_objc_method_num_params_inconsistent : Error<
  "%select{class|instance}0 method %1 has a different number of parameters in "
  "different translation units (%2 vs. %3)">;
def warn_odr_objc_method_num_params_inconsistent : Warning<
  "%select{class|instance}0 method %1 has a different number of parameters in "
  "different translation units (%2 vs. %3)">,
  InGroup<ODR>;
def err_odr_objc_method_param_type_inconsistent : Error<
  "%select{class|instance}0 method %1 has a parameter with a different types "
  "in different translation units (%2 vs. %3)">;
def warn_odr_objc_method_param_type_inconsistent : Warning<
  "%select{class|instance}0 method %1 has a parameter with a different types "
  "in different translation units (%2 vs. %3)">,
  InGroup<ODR>;
def err_odr_objc_method_variadic_inconsistent : Error<
  "%select{class|instance}0 method %1 is variadic in one translation unit "
  "and not variadic in another">;
def warn_odr_objc_method_variadic_inconsistent : Warning<
  "%select{class|instance}0 method %1 is variadic in one translation unit "
  "and not variadic in another">,
  InGroup<ODR>;
def note_odr_objc_method_here : Note<
  "%select{class|instance}0 method %1 also declared here">;
def err_odr_objc_property_type_inconsistent : Error<
  "property %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">;
def warn_odr_objc_property_type_inconsistent : Warning<
  "property %0 declared with incompatible types in different "
  "translation units (%1 vs. %2)">,
  InGroup<ODR>;
def err_odr_objc_property_impl_kind_inconsistent : Error<
  "property %0 is implemented with %select{@synthesize|@dynamic}1 in one "
  "translation but %select{@dynamic|@synthesize}1 in another translation unit">;
def warn_odr_objc_property_impl_kind_inconsistent : Warning<
  "property %0 is implemented with %select{@synthesize|@dynamic}1 in one "
  "translation but %select{@dynamic|@synthesize}1 in another translation unit">,
  InGroup<ODR>;
def note_odr_objc_property_impl_kind : Note<
  "property %0 is implemented with %select{@synthesize|@dynamic}1 here">;
def err_odr_objc_synthesize_ivar_inconsistent : Error<
  "property %0 is synthesized to different ivars in different translation "
  "units (%1 vs. %2)">;
def warn_odr_objc_synthesize_ivar_inconsistent : Warning<
  "property %0 is synthesized to different ivars in different translation "
  "units (%1 vs. %2)">,
  InGroup<ODR>;
def note_odr_objc_synthesize_ivar_here : Note<
  "property is synthesized to ivar %0 here">;

// Importing C++ ASTs
def note_odr_friend : Note<"friend declared here">;
def note_odr_missing_friend : Note<"no corresponding friend here">;
def err_odr_different_num_template_parameters : Error<
  "template parameter lists have a different number of parameters (%0 vs %1)">;
def warn_odr_different_num_template_parameters : Warning<
  "template parameter lists have a different number of parameters (%0 vs %1)">,
  InGroup<ODR>;
def note_odr_template_parameter_list : Note<
  "template parameter list also declared here">;
def err_odr_different_template_parameter_kind : Error<
  "template parameter has different kinds in different translation units">;
def warn_odr_different_template_parameter_kind : Warning<
  "template parameter has different kinds in different translation units">,
  InGroup<ODR>;
def note_odr_template_parameter_here : Note<
  "template parameter declared here">;
def err_odr_parameter_pack_non_pack : Error<
  "parameter kind mismatch; parameter is %select{not a|a}0 parameter pack">;
def warn_odr_parameter_pack_non_pack : Warning<
  "parameter kind mismatch; parameter is %select{not a|a}0 parameter pack">,
  InGroup<ODR>;
def note_odr_parameter_pack_non_pack : Note<
  "%select{parameter|parameter pack}0 declared here">;
def err_odr_non_type_parameter_type_inconsistent : Error<
  "non-type template parameter declared with incompatible types in different "
  "translation units (%0 vs. %1)">;
def warn_odr_non_type_parameter_type_inconsistent : Warning<
  "non-type template parameter declared with incompatible types in different "
  "translation units (%0 vs. %1)">,
  InGroup<ODR>;
def err_unsupported_ast_node: Error<"cannot import unsupported AST node %0">;

// Compare ODR hashes
def err_module_odr_violation_different_definitions : Error<
  "%q0 has different definitions in different modules; "
  "%select{definition in module '%2' is here|defined here}1">;
def note_first_module_difference : Note<
  "in first definition, possible difference is here">;
def note_module_odr_violation_different_definitions : Note<
  "definition in module '%0' is here">;
def note_second_module_difference : Note<
  "in second definition, possible difference is here">;

def err_module_odr_violation_definition_data : Error <
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "%4 base %plural{1:class|:classes}4|"
  "%4 virtual base %plural{1:class|:classes}4|"
  "%ordinal4 base class with type %5|"
  "%ordinal4 %select{non-virtual|virtual}5 base class %6|"
  "%ordinal4 base class %5 with "
  "%select{public|protected|private|no}6 access specifier}3">;

def note_module_odr_violation_definition_data : Note <
  "but in '%0' found "
  "%select{"
  "%2 base %plural{1:class|:classes}2|"
  "%2 virtual base %plural{1:class|:classes}2|"
  "%ordinal2 base class with different type %3|"
  "%ordinal2 %select{non-virtual|virtual}3 base class %4|"
  "%ordinal2 base class %3 with "
  "%select{public|protected|private|no}4 access specifier}1">;

def err_module_odr_violation_objc_interface : Error <
  "%0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "%select{no super class|super class with type %5}4|"
  "instance variable '%4' access control is "
    "%select{|@private|@protected|@public|@package}5"
  "}3">;
def note_module_odr_violation_objc_interface : Note <
  "but in %select{'%1'|definition here}0 found "
  "%select{"
  "%select{no super class|super class with type %4}3|"
  "instance variable '%3' access control is "
    "%select{|@private|@protected|@public|@package}4"
  "}2">;

def err_module_odr_violation_template_parameter : Error <
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "unnamed template parameter|"
  "template parameter %5|"
  "template parameter with %select{no |}4default argument|"
  "template parameter with default argument}3">;

def note_module_odr_violation_template_parameter : Note <
  "but in '%0' found "
  "%select{"
  "unnamed template parameter %2|"
  "template parameter %3|"
  "template parameter with %select{no |}2default argument|"
  "template parameter with different default argument}1">;

def err_module_odr_violation_mismatch_decl : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{end of class|public access specifier|private access specifier|"
  "protected access specifier|static assert|field|method|type alias|typedef|"
  "data member|friend declaration|function template|method|instance variable|"
  "property}3">;
def note_module_odr_violation_mismatch_decl : Note<
  "but in %select{'%1'|definition here}0 found "
  "%select{end of class|public access specifier|private access specifier|"
  "protected access specifier|static assert|field|method|type alias|typedef|"
  "data member|friend declaration|function template|method|instance variable|"
  "property}2">;

def err_module_odr_violation_record : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "static assert with condition|"
  "static assert with message|"
  "static assert with %select{|no }4message|"
  "%select{method %5|constructor|destructor}4|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{not deleted|deleted}6|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{not defaulted|defaulted}6|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{|pure }6%select{not virtual|virtual}7|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{not static|static}6|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{not volatile|volatile}6|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{not const|const}6|"
  "%select{method %5|constructor|destructor}4 "
    "is %select{not inline|inline}6|"
  "%select{method %5|constructor|destructor}4 "
    "with %ordinal6 parameter with%select{out|}7 a default argument|"
  "%select{method %5|constructor|destructor}4 "
    "with %ordinal6 parameter with a default argument|"
  "%select{method %5|constructor|destructor}4 "
    "with %select{no |}6template arguments|"
  "%select{method %5|constructor|destructor}4 "
    "with %6 template argument%s6|"
  "%select{method %5|constructor|destructor}4 "
    "with %6 for %ordinal7 template argument|"
  "%select{method %5|constructor|destructor}4 "
    "with %select{no body|body}6|"
  "%select{method %5|constructor|destructor}4 "
    "with body|"
  "friend %select{class|function}4|"
  "friend %4|"
  "friend function %4|"
  "function template %4 with %5 template parameter%s5|"
  "function template %4 with %ordinal5 template parameter being a "
    "%select{type|non-type|template}6 template parameter|"
  "function template %4 with %ordinal5 template parameter "
    "%select{with no name|named %7}6|"
  "function template %4 with %ordinal5 template parameter with "
    "%select{no |}6default argument|"
  "function template %4 with %ordinal5 template parameter with "
    "default argument %6|"
  "function template %4 with %ordinal5 template parameter with one type|"
  "function template %4 with %ordinal5 template parameter %select{not |}6"
    "being a template parameter pack|"
  "}3">;

def note_module_odr_violation_record : Note<"but in '%0' found "
  "%select{"
  "static assert with different condition|"
  "static assert with different message|"
  "static assert with %select{|no }2message|"
  "%select{method %3|constructor|destructor}2|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{not deleted|deleted}4|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{not defaulted|defaulted}4|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{|pure }4%select{not virtual|virtual}5|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{not static|static}4|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{not volatile|volatile}4|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{not const|const}4|"
  "%select{method %3|constructor|destructor}2 "
    "is %select{not inline|inline}4|"
  "%select{method %3|constructor|destructor}2 "
    "with %ordinal4 parameter with%select{out|}5 a default argument|"
  "%select{method %3|constructor|destructor}2 "
    "with %ordinal4 parameter with a different default argument|"
  "%select{method %3|constructor|destructor}2 "
    "with %select{no |}4template arguments|"
  "%select{method %3|constructor|destructor}2 "
    "with %4 template argument%s4|"
  "%select{method %3|constructor|destructor}2 "
    "with %4 for %ordinal5 template argument|"
  "%select{method %3|constructor|destructor}2 "
    "with %select{no body|body}4|"
  "%select{method %3|constructor|destructor}2 "
    "with different body|"
  "friend %select{class|function}2|"
  "friend %2|"
  "friend function %2|"
  "function template %2 with %3 template parameter%s3|"
  "function template %2 with %ordinal3 template paramter being a "
    "%select{type|non-type|template}4 template parameter|"
  "function template %2 with %ordinal3 template parameter "
    "%select{with no name|named %5}4|"
  "function template %2 with %ordinal3 template parameter with "
    "%select{no |}4default argument|"
  "function template %2 with %ordinal3 template parameter with "
    "default argument %4|"
  "function template %2 with %ordinal3 template parameter with different type|"
  "function template %2 with %ordinal3 template parameter %select{not |}4"
    "being a template parameter pack|"
  "}1">;

def err_module_odr_violation_field : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "field %4|"
  "field %4 with type %5|"
  "%select{non-|}5bitfield %4|"
  "bitfield %4 with one width expression|"
  "%select{non-|}5mutable field %4|"
  "field %4 with %select{no|an}5 initializer|"
  "field %4 with an initializer"
  "}3">;
def note_module_odr_violation_field : Note<
  "but in %select{'%1'|definition here}0 found "
  "%select{"
  "field %3|"
  "field %3 with type %4|"
  "%select{non-|}4bitfield %3|"
  "bitfield %3 with different width expression|"
  "%select{non-|}4mutable field %3|"
  "field %3 with %select{no|an}4 initializer|"
  "field %3 with a different initializer"
  "}2">;

def err_module_odr_violation_typedef : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "%select{typedef|type alias}4 name %5|"
  "%select{typedef|type alias}4 %5 with underlying type %6"
  "}3">;
def note_module_odr_violation_typedef : Note<"but in '%0' found "
  "%select{"
  "%select{typedef|type alias}2 name %3|"
  "%select{typedef|type alias}2 %3 with different underlying type %4"
  "}1">;

def err_module_odr_violation_variable : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "data member with name %4|"
  "data member %4 with type %5|"
  "data member %4 with%select{out|}5 an initializer|"
  "data member %4 with an initializer|"
  "data member %4 %select{is constexpr|is not constexpr}5"
  "}3">;
def note_module_odr_violation_variable : Note<"but in '%0' found "
  "%select{"
  "data member with name %2|"
  "data member %2 with different type %3|"
  "data member %2 with%select{out|}3 an initializer|"
  "data member %2 with a different initializer|"
  "data member %2 %select{is constexpr|is not constexpr}3"
  "}1">;

def err_module_odr_violation_function : Error<
  "%q0 has different definitions in different modules; "
  "%select{definition in module '%2'|defined here}1 "
  "first difference is "
  "%select{"
  "return type is %4|"
  "%ordinal4 parameter with name %5|"
  "%ordinal4 parameter with type %5%select{| decayed from %7}6|"
  "%ordinal4 parameter with%select{out|}5 a default argument|"
  "%ordinal4 parameter with a default argument|"
  "function body"
  "}3">;

def note_module_odr_violation_function : Note<"but in '%0' found "
  "%select{"
  "different return type %2|"
  "%ordinal2 parameter with name %3|"
  "%ordinal2 parameter with type %3%select{| decayed from %5}4|"
  "%ordinal2 parameter with%select{out|}3 a default argument|"
  "%ordinal2 parameter with a different default argument|"
  "a different body"
  "}1">;

def err_module_odr_violation_enum : Error<
  "%q0 has different definitions in different modules; "
  "%select{definition in module '%2'|defined here}1 "
  "first difference is "
  "%select{"
  "enum that is %select{not scoped|scoped}4|"
  "enum scoped with keyword %select{struct|class}4|"
  "enum %select{without|with}4 specified type|"
  "enum with specified type %4|"
  "enum with %4 element%s4|"
  "%ordinal4 element has name %5|"
  "%ordinal4 element %5 %select{has|does not have}6 an initializer|"
  "%ordinal4 element %5 has an initializer|"
  "}3">;

def note_module_odr_violation_enum : Note<"but in '%0' found "
  "%select{"
  "enum that is %select{not scoped|scoped}2|"
  "enum scoped with keyword %select{struct|class}2|"
  "enum %select{without|with}2 specified type|"
  "enum with specified type %2|"
  "enum with %2 element%s2|"
  "%ordinal2 element has name %3|"
  "%ordinal2 element %3 %select{has|does not have}4 an initializer|"
  "%ordinal2 element %3 has different initializer|"
  "}1">;

def err_module_odr_violation_referenced_protocols : Error <
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "%4 referenced %plural{1:protocol|:protocols}4|"
  "%ordinal4 referenced protocol with name %5"
  "}3">;
def note_module_odr_violation_referenced_protocols : Note <
  "but in %select{'%1'|definition here}0 found "
  "%select{"
  "%3 referenced %plural{1:protocol|:protocols}3|"
  "%ordinal3 referenced protocol with different name %4"
  "}2">;

def err_module_odr_violation_objc_method : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "method %4 with return type %5|"
  "%select{class|instance}5 method %4|"
  "%select{no|'required'|'optional'}4 method control|"
  "method %4 with %select{no designated initializer|designated initializer}5|"
  "%select{regular|direct}5 method %4|"
  "method %4"
  "}3">;
def note_module_odr_violation_objc_method : Note<
  "but in %select{'%1'|definition here}0 found "
  "%select{"
  "method %3 with different return type %4|"
  "method %3 as %select{class|instance}4 method|"
  "%select{no|'required'|'optional'}3 method control|"
  "method %3 with %select{no designated initializer|designated initializer}4|"
  "%select{regular|direct}4 method %3|"
  "different method %3"
  "}2">;

def err_module_odr_violation_method_params : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "%select{method %5|constructor|destructor}4 "
    "that has %6 parameter%s6|"
  "%select{method %5|constructor|destructor}4 "
    "with %ordinal6 parameter of type %7%select{| decayed from %9}8|"
  "%select{method %5|constructor|destructor}4 "
    "with %ordinal6 parameter named %7"
  "}3">;
def note_module_odr_violation_method_params : Note<
  "but in %select{'%1'|definition here}0 found "
  "%select{"
  "%select{method %4|constructor|destructor}3 "
    "that has %5 parameter%s5|"
  "%select{method %4|constructor|destructor}3 "
    "with %ordinal5 parameter of type %6%select{| decayed from %8}7|"
  "%select{method %4|constructor|destructor}3 "
    "with %ordinal5 parameter named %6"
  "}2">;

def err_module_odr_violation_objc_property : Error<
  "%q0 has different definitions in different modules; first difference is "
  "%select{definition in module '%2'|defined here}1 found "
  "%select{"
  "property %4|"
  "property %4 with type %5|"
  "%select{no|'required'|'optional'}4 property control|"
  "property %4 with %select{default |}6'%select{none|readonly|getter|assign|"
    "readwrite|retain|copy|nonatomic|setter|atomic|weak|strong|"
    "unsafe_unretained|nullability|null_resettable|class|direct}5' attribute"
  "}3">;
def note_module_odr_violation_objc_property : Note<
  "but in %select{'%1'|definition here}0 found "
  "%select{"
  "property %3|"
  "property %3 with type %4|"
  "%select{no|'required'|'optional'}3 property control|"
  "property %3 with different '%select{none|readonly|getter|assign|"
    "readwrite|retain|copy|nonatomic|setter|atomic|weak|strong|"
    "unsafe_unretained|nullability|null_resettable|class|direct}4' attribute"
  "}2">;

def err_module_odr_violation_mismatch_decl_unknown : Error<
  "%q0 %select{with definition in module '%2'|defined here}1 has different "
  "definitions in different modules; first difference is this "
  "%select{||||static assert|field|method|type alias|typedef|data member|"
  "friend declaration|function template|method|instance variable|"
  "property|unexpected decl}3">;
def note_module_odr_violation_mismatch_decl_unknown : Note<
  "but in %select{'%1'|definition here}0 found "
  "%select{||||different static assert|different field|different method|"
  "different type alias|different typedef|different data member|"
  "different friend declaration|different function template|different method|"
  "different instance variable|different property|another unexpected decl}2">;


def remark_sanitize_address_insert_extra_padding_accepted : Remark<
    "-fsanitize-address-field-padding applied to %0">, ShowInSystemHeader,
    InGroup<SanitizeAddressRemarks>;
def remark_sanitize_address_insert_extra_padding_rejected : Remark<
    "-fsanitize-address-field-padding ignored for %0 because it "
    "%select{is not C++|is packed|is a union|is trivially copyable|"
    "has trivial destructor|is standard layout|is in a ignorelisted file|"
    "is ignorelisted}1">, ShowInSystemHeader,
    InGroup<SanitizeAddressRemarks>;

def warn_npot_ms_struct : Warning<
  "ms_struct may not produce Microsoft-compatible layouts with fundamental "
  "data types with sizes that aren't a power of two">,
  DefaultError, InGroup<IncompatibleMSStruct>;

// -Wpadded-bitfield
def warn_padded_struct_bitfield : Warning<
  "padding %select{struct|interface|class}0 %1 with %2 "
  "%select{byte|bit}3%s2 to align %4">,
  InGroup<PaddedBitField>, DefaultIgnore;
def warn_padded_struct_anon_bitfield : Warning<
  "padding %select{struct|interface|class}0 %1 with %2 "
  "%select{byte|bit}3%s2 to align anonymous bit-field">,
  InGroup<PaddedBitField>, DefaultIgnore;

// -Wpadded, -Wpacked
def warn_padded_struct_field : Warning<
  "padding %select{struct|interface|class}0 %1 with %2 "
  "%select{byte|bit}3%s2 to align %4">,
  InGroup<Padded>, DefaultIgnore;
def warn_padded_struct_anon_field : Warning<
  "padding %select{struct|interface|class}0 %1 with %2 "
  "%select{byte|bit}3%s2 to align anonymous field">,
  InGroup<Padded>, DefaultIgnore;
def warn_padded_struct_size : Warning<
  "padding size of %0 with %1 %select{byte|bit}2%s1 to alignment boundary">,
  InGroup<Padded>, DefaultIgnore;
def warn_unnecessary_packed : Warning<
  "packed attribute is unnecessary for %0">, InGroup<Packed>, DefaultIgnore;
def warn_unpacked_field
    : Warning<
          "not packing field %0 as it is non-POD for the purposes of layout">,
      InGroup<PackedNonPod>,
      DefaultIgnore;

// -Wunaligned-access
def warn_unaligned_access : Warning<
  "field %1 within %0 is less aligned than %2 and is usually due to %0 being "
  "packed, which can lead to unaligned accesses">, InGroup<UnalignedAccess>, DefaultIgnore;
}
