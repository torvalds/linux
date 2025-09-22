//==--- DiagnosticParseKinds.td - libparse diagnostics --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Parser Diagnostics
//===----------------------------------------------------------------------===//

let Component = "Parse" in {

def err_asm_qualifier_ignored : Error<
  "expected 'volatile', 'inline', 'goto', or '('">, CatInlineAsm;
def err_global_asm_qualifier_ignored : Error<
  "meaningless '%0' on asm outside function">, CatInlineAsm;

let CategoryName = "Inline Assembly Issue" in {
def err_asm_empty : Error<"__asm used with no assembly instructions">;
def err_inline_ms_asm_parsing : Error<"%0">;
def err_msasm_unsupported_arch : Error<
  "unsupported architecture '%0' for MS-style inline assembly">;
def err_msasm_unable_to_create_target : Error<
  "MS-style inline assembly is not available: %0">;
def err_gnu_inline_asm_disabled : Error<
  "GNU-style inline assembly is disabled">;
def err_asm_duplicate_qual : Error<"duplicate asm qualifier '%0'">;
}

let CategoryName = "Parse Issue" in {

def ext_empty_translation_unit : Extension<
  "ISO C requires a translation unit to contain at least one declaration">,
  InGroup<DiagGroup<"empty-translation-unit">>;
def warn_cxx98_compat_top_level_semi : Warning<
  "extra ';' outside of a function is incompatible with C++98">,
  InGroup<CXX98CompatExtraSemi>, DefaultIgnore;
def ext_extra_semi : Extension<
  "extra ';' %select{"
  "outside of a function|"
  "inside a %1|"
  "inside instance variable list|"
  "after member function definition}0">,
  InGroup<ExtraSemi>;
def ext_extra_semi_cxx11 : Extension<
  "extra ';' outside of a function is a C++11 extension">,
  InGroup<CXX11ExtraSemi>;
def warn_extra_semi_after_mem_fn_def : Warning<
  "extra ';' after member function definition">,
  InGroup<ExtraSemi>, DefaultIgnore;
def warn_null_statement : Warning<
  "empty expression statement has no effect; "
  "remove unnecessary ';' to silence this warning">,
  InGroup<ExtraSemiStmt>, DefaultIgnore;

def warn_misleading_indentation : Warning<
  "misleading indentation; statement is not part of "
  "the previous '%select{if|else|for|while}0'">,
  InGroup<MisleadingIndentation>, DefaultIgnore;
def note_previous_statement : Note<
  "previous statement is here">;

def subst_compound_token_kind : TextSubstitution<
  "%select{%1 and |}0%2 tokens "
  "%select{introducing statement expression|terminating statement expression|"
  "introducing attribute|terminating attribute|"
  "forming pointer to member type}3">;
def warn_compound_token_split_by_macro : Warning<
  "%sub{subst_compound_token_kind}0,1,2,3 appear in different "
  "macro expansion contexts">, InGroup<CompoundTokenSplitByMacro>;
def note_compound_token_split_second_token_here : Note<
  "%select{|second }0%1 token is here">;
def warn_compound_token_split_by_whitespace : Warning<
  "%sub{subst_compound_token_kind}0,1,2,3 are separated by whitespace">,
  InGroup<CompoundTokenSplitBySpace>, DefaultIgnore;

def ext_thread_before : Extension<"'__thread' before '%0'">;
def ext_keyword_as_ident : ExtWarn<
  "keyword '%0' will be made available as an identifier "
  "%select{here|for the remainder of the translation unit}1">,
  InGroup<KeywordCompat>;

def ext_nullability : Extension<
  "type nullability specifier %0 is a Clang extension">,
  InGroup<DiagGroup<"nullability-extension">>;

def err_empty_enum : Error<"use of empty enum">;

def ext_ident_list_in_param : Extension<
  "type-less parameter names in function declaration">;
def ext_c99_variable_decl_in_for_loop : Extension<
  "variable declaration in for loop is a C99-specific feature">, InGroup<C99>;
def ext_c99_compound_literal : Extension<
  "compound literals are a C99-specific feature">, InGroup<C99>;
def ext_enumerator_list_comma_c : Extension<
  "commas at the end of enumerator lists are a C99-specific "
  "feature">, InGroup<C99>;
def ext_enumerator_list_comma_cxx : Extension<
  "commas at the end of enumerator lists are a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_enumerator_list_comma : Warning<
  "commas at the end of enumerator lists are incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def err_enumerator_list_missing_comma : Error<
  "missing ',' between enumerators">;
def err_enumerator_unnamed_no_def : Error<
  "unnamed enumeration must be a definition">;
def ext_cxx11_enum_fixed_underlying_type : Extension<
  "enumeration types with a fixed underlying type are a C++11 extension">,
  InGroup<CXX11>;
def ext_ms_c_enum_fixed_underlying_type : Extension<
  "enumeration types with a fixed underlying type are a Microsoft extension">,
  InGroup<MicrosoftFixedEnum>;
def ext_clang_c_enum_fixed_underlying_type : Extension<
  "enumeration types with a fixed underlying type are a Clang extension">,
  InGroup<DiagGroup<"fixed-enum-extension">>;
def warn_cxx98_compat_enum_fixed_underlying_type : Warning<
  "enumeration types with a fixed underlying type are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_enum_base_in_type_specifier : ExtWarn<
  "non-defining declaration of enumeration with a fixed underlying type is "
  "only permitted as a standalone declaration"
  "%select{|; missing list of enumerators?}0">,
  InGroup<DiagGroup<"elaborated-enum-base">>, DefaultError;
def ext_elaborated_enum_class : ExtWarn<
  "reference to enumeration must use 'enum' not 'enum %select{struct|class}0'">,
  InGroup<DiagGroup<"elaborated-enum-class">>, DefaultError;
def err_scoped_enum_missing_identifier : Error<
  "scoped enumeration requires a name">;
def ext_scoped_enum : ExtWarn<
  "scoped enumerations are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_scoped_enum : Warning<
  "scoped enumerations are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_anonymous_enum_bitfield : Error<
  "ISO C++ only allows ':' in member enumeration declaration to introduce "
  "a fixed underlying type, not an anonymous bit-field">;

def warn_cxx98_compat_alignof : Warning<
  "alignof expressions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_alignof_expr : ExtWarn<
  "%0 applied to an expression is a GNU extension">, InGroup<GNUAlignofExpression>;
def err_lambda_after_delete : Error<
  "'[]' after delete interpreted as 'delete[]'; add parentheses to treat this as a lambda-expression">;

def warn_microsoft_dependent_exists : Warning<
  "dependent %select{__if_not_exists|__if_exists}0 declarations are ignored">,
  InGroup<DiagGroup<"microsoft-exists">>;
def warn_microsoft_qualifiers_ignored : Warning<
  "qualifiers after comma in declarator list are ignored">,
  InGroup<IgnoredAttributes>;

def err_duplicate_default_assoc : Error<
  "duplicate default generic association">;
def note_previous_default_assoc : Note<
  "previous default generic association is here">;
def ext_c2y_generic_with_type_arg : Extension<
  "passing a type argument as the first operand to '_Generic' is a C2y "
  "extension">, InGroup<C2y>;
def warn_c2y_compat_generic_with_type_arg : Warning<
  "passing a type argument as the first operand to '_Generic' is incompatible "
  "with C standards before C2y">, InGroup<CPre2yCompat>, DefaultIgnore;

def ext_c99_feature : Extension<
  "'%0' is a C99 extension">, InGroup<C99>;
def ext_c11_feature : Extension<
  "'%0' is a C11 extension">, InGroup<C11>;
def warn_c11_compat_keyword : Warning<
  "'%0' is incompatible with C standards before C11">,
  InGroup<CPre11Compat>, DefaultIgnore;
def warn_c23_compat_keyword : Warning<
 "'%0' is incompatible with C standards before C23">,
 InGroup<CPre23Compat>, DefaultIgnore;

def err_c11_noreturn_misplaced : Error<
  "'_Noreturn' keyword must precede function declarator">;

def ext_gnu_indirect_goto : Extension<
  "use of GNU indirect-goto extension">, InGroup<GNULabelsAsValue>;
def ext_gnu_address_of_label : Extension<
  "use of GNU address-of-label extension">, InGroup<GNULabelsAsValue>;
def err_stmtexpr_file_scope : Error<
  "statement expression not allowed at file scope">;
def ext_gnu_statement_expr : Extension<
  "use of GNU statement expression extension">, InGroup<GNUStatementExpression>;
def ext_gnu_statement_expr_macro : Extension<
  "use of GNU statement expression extension from macro expansion">,
  InGroup<GNUStatementExpressionFromMacroExpansion>;
def ext_gnu_conditional_expr : Extension<
  "use of GNU ?: conditional expression extension, omitting middle operand">, InGroup<GNUConditionalOmittedOperand>;
def ext_gnu_array_range : Extension<"use of GNU array range extension">,
  InGroup<GNUDesignator>;
def ext_gnu_missing_equal_designator : ExtWarn<
  "use of GNU 'missing =' extension in designator">,
  InGroup<GNUDesignator>;
def err_expected_equal_designator : Error<"expected '=' or another designator">;
def ext_gnu_old_style_field_designator : ExtWarn<
  "use of GNU old-style field designator extension">,
  InGroup<GNUDesignator>;
def ext_gnu_case_range : Extension<"use of GNU case range extension">,
  InGroup<GNUCaseRange>;

// Generic errors.
def err_expected_expression : Error<"expected expression">;
def err_expected_type : Error<"expected a type">;
def err_expected_external_declaration : Error<"expected external declaration">;
def err_extraneous_closing_brace : Error<"extraneous closing brace ('}')">;
def err_expected_semi_declaration : Error<
  "expected ';' at end of declaration">;
def err_expected_semi_decl_list : Error<
  "expected ';' at end of declaration list">;
def ext_expected_semi_decl_list : ExtWarn<
  "expected ';' at end of declaration list">;
def err_expected_member_name_or_semi : Error<
  "expected member name or ';' after declaration specifiers">;
def err_function_declared_typedef : Error<
  "function definition declared 'typedef'">;
def err_at_defs_cxx : Error<"@defs is not supported in Objective-C++">;
def err_at_in_class : Error<"unexpected '@' in member specification">;
def err_unexpected_semi : Error<"unexpected ';' before %0">;
def err_postfix_after_unary_requires_parens : Error<
  "expression cannot be followed by a postfix %0 operator; add parentheses">;
def err_unparenthesized_non_primary_expr_in_requires_clause : Error<
  "parentheses are required around this expression in a requires clause">;
def note_unparenthesized_non_primary_expr_in_requires_clause : Note<
  "parentheses are required around this expression in a requires clause">;

def err_expected_fn_body : Error<
  "expected function body after function declarator">;
def warn_attribute_on_function_definition : Warning<
  "GCC does not allow %0 attribute in this position on a function definition">,
  InGroup<GccCompat>;
def warn_gcc_attribute_location : Warning<
  "GCC does not allow an attribute in this position on a function declaration">,
  InGroup<GccCompat>;
def warn_gcc_variable_decl_in_for_loop : Warning<
  "GCC does not allow variable declarations in for loop initializers before "
  "C99">, InGroup<GccCompat>;
def warn_attribute_no_decl : Warning<
  "attribute %0 ignored, because it is not attached to a declaration">,
  InGroup<IgnoredAttributes>;
def err_ms_attributes_not_enabled : Error<
  "'__declspec' attributes are not enabled; use '-fdeclspec' or "
  "'-fms-extensions' to enable support for __declspec attributes">;
def err_expected_method_body : Error<"expected method body">;
def err_declspec_after_virtspec : Error<
  "'%0' qualifier may not appear after the virtual specifier '%1'">;
def err_invalid_token_after_toplevel_declarator : Error<
  "expected ';' after top level declarator">;
def err_invalid_token_after_declarator_suggest_equal : Error<
  "invalid %0 at end of declaration; did you mean '='?">;
def err_expected_statement : Error<"expected statement">;
def err_expected_lparen_after : Error<"expected '(' after '%0'">;
def err_expected_rparen_after : Error<"expected ')' after '%0'">;
def err_expected_punc : Error<"expected ')' or ',' after '%0'">;
def err_expected_less_after : Error<"expected '<' after '%0'">;
def err_expected_lbrace_in_compound_literal : Error<
  "expected '{' in compound literal">;
def err_expected_while : Error<"expected 'while' in do/while loop">;

def err_expected_semi_after_stmt : Error<"expected ';' after %0 statement">;
def err_expected_semi_after_expr : Error<"expected ';' after expression">;
def err_extraneous_token_before_semi : Error<"extraneous '%0' before ';'">;

def err_expected_semi_after_method_proto : Error<
  "expected ';' after method prototype">;
def err_expected_semi_after_namespace_name : Error<
  "expected ';' after namespace name">;
def err_unexpected_namespace_attributes_alias : Error<
  "attributes cannot be specified on namespace alias">;
def err_unexpected_qualified_namespace_alias : Error<
  "namespace alias must be a single identifier">;
def err_unexpected_nested_namespace_attribute : Error<
  "attributes cannot be specified on a nested namespace definition">;
def err_inline_namespace_alias : Error<"namespace alias cannot be inline">;
def err_namespace_nonnamespace_scope : Error<
  "namespaces can only be defined in global or namespace scope">;
def ext_nested_namespace_definition : ExtWarn<
  "nested namespace definition is a C++17 extension; "
  "define each namespace separately">, InGroup<CXX17>;
def warn_cxx14_compat_nested_namespace_definition : Warning<
  "nested namespace definition is incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_inline_nested_namespace_definition : ExtWarn<
  "inline nested namespace definition is a C++20 extension">, InGroup<CXX20>;
def warn_cxx17_compat_inline_nested_namespace_definition : Warning<
  "inline nested namespace definition is incompatible with C++ standards before"
  " C++20">, InGroup<CXXPre20Compat>, DefaultIgnore;
def err_inline_nested_namespace_definition : Error<
  "nested namespace definition cannot be 'inline'">;
def err_expected_semi_after_attribute_list : Error<
  "expected ';' after attribute list">;
def err_expected_semi_after_static_assert : Error<
  "expected ';' after '%0'">;
def err_expected_semi_for : Error<"expected ';' in 'for' statement specifier">;
def err_single_decl_assign_in_for_range : Error<
  "range-based 'for' statement uses ':', not '='">;
def warn_missing_selector_name : Warning<
  "%0 used as the name of the previous parameter rather than as part "
  "of the selector">,
  InGroup<DiagGroup<"missing-selector-name">>;
def note_missing_selector_name : Note<
  "introduce a parameter name to make %0 part of the selector">;
def note_force_empty_selector_name : Note<
  "or insert whitespace before ':' to use %0 as parameter name "
  "and have an empty entry in the selector">;
def ext_c_label_followed_by_declaration : ExtWarn<
  "label followed by a declaration is a C23 extension">,
  InGroup<C23>;
def warn_c23_compat_label_followed_by_declaration : Warning<
  "label followed by a declaration is incompatible with C standards before "
  "C23">, InGroup<CPre23Compat>, DefaultIgnore;
def ext_c_label_end_of_compound_statement : ExtWarn<
  "label at end of compound statement is a C23 extension">,
   InGroup<C23>;
def ext_cxx_label_end_of_compound_statement : ExtWarn<
  "label at end of compound statement is a C++23 extension">,
   InGroup<CXX23>;
def warn_c23_compat_label_end_of_compound_statement : Warning<
  "label at end of compound statement is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def warn_cxx20_compat_label_end_of_compound_statement : Warning<
  "label at end of compound statement is incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;
def err_address_of_label_outside_fn : Error<
  "use of address-of-label extension outside of a function body">;
def err_asm_operand_wide_string_literal : Error<
  "cannot use %select{unicode|wide|an empty}0 string literal in 'asm'">;
def err_expected_selector_for_method : Error<
  "expected selector for Objective-C method">;
def err_expected_property_name : Error<"expected property name">;

def err_unexpected_at : Error<"unexpected '@' in program">;
def err_atimport : Error<
"use of '@import' when modules are disabled">;

def warn_atimport_in_framework_header : Warning<
  "use of '@import' in framework header is discouraged, "
  "including this header requires -fmodules">,
  InGroup<FrameworkHdrAtImport>;

def err_invalid_reference_qualifier_application : Error<
  "'%0' qualifier may not be applied to a reference">;
def err_illegal_decl_reference_to_reference : Error<
  "%0 declared as a reference to a reference">;
def ext_rvalue_reference : ExtWarn<
  "rvalue references are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_rvalue_reference : Warning<
  "rvalue references are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_ref_qualifier : ExtWarn<
  "reference qualifiers on functions are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_ref_qualifier : Warning<
  "reference qualifiers on functions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_inline_namespace : ExtWarn<
  "inline namespaces are a C++11 feature">, InGroup<CXX11InlineNamespace>;
def warn_cxx98_compat_inline_namespace : Warning<
  "inline namespaces are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_generalized_initializer_lists : ExtWarn<
  "generalized initializer lists are a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_generalized_initializer_lists : Warning<
  "generalized initializer lists are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_init_list_bin_op : Error<"initializer list cannot be used on the "
  "%select{left|right}0 hand side of operator '%1'">;
def warn_cxx98_compat_trailing_return_type : Warning<
  "trailing return types are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_requires_clause_must_appear_after_trailing_return : Error<
  "trailing return type must appear before trailing requires clause">;
def err_requires_clause_on_declarator_not_declaring_a_function : Error<
  "trailing requires clause can only be used when declaring a function">;
def err_requires_clause_inside_parens : Error<
  "trailing requires clause should be placed outside parentheses">;
def ext_auto_storage_class : ExtWarn<
  "'auto' storage class specifier is not permitted in C++11, and will not "
  "be supported in future releases">, InGroup<DiagGroup<"auto-storage-class">>;
def ext_decltype_auto_type_specifier : ExtWarn<
  "'decltype(auto)' type specifier is a C++14 extension">, InGroup<CXX14>;
def warn_cxx11_compat_decltype_auto_type_specifier : Warning<
  "'decltype(auto)' type specifier is incompatible with C++ standards before "
  "C++14">, InGroup<CXXPre14Compat>, DefaultIgnore;
def ext_auto_type : Extension<
  "'__auto_type' is a GNU extension">,
  InGroup<GNUAutoType>;
def ext_for_range : ExtWarn<
  "range-based for loop is a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_for_range : Warning<
  "range-based for loop is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_for_range_identifier : Error<
  "range-based for loop requires type for loop variable">;
def err_for_range_expected_decl : Error<
  "for range declaration must declare a variable">;
def err_argument_required_after_attribute : Error<
  "argument required after attribute">;
def err_missing_param : Error<"expected parameter declarator">;
def err_function_scope_depth_exceeded : Error<
  "function scope depth exceeded maximum of %0">, DefaultFatal;
def err_missing_comma_before_ellipsis : Error<
  "C requires a comma prior to the ellipsis in a variadic function type">;
def err_unexpected_typedef_ident : Error<
  "unexpected type name %0: expected identifier">;
def warn_cxx98_compat_decltype : Warning<
  "'decltype' type specifier is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_unexpected_scope_on_base_decltype : Error<
  "unexpected namespace scope prior to decltype">;
def err_expected_class_name : Error<"expected class name">;
def err_expected_class_name_not_template :
  Error<"'typename' is redundant; base classes are implicitly types">;
def err_unspecified_vla_size_with_static : Error<
  "'static' may not be used with an unspecified variable length array size">;
def err_unspecified_size_with_static : Error<
  "'static' may not be used without an array size">;
def err_expected_parentheses_around_typename : Error<
  "expected parentheses around type name in %0 expression">;

def err_expected_case_before_expression: Error<
  "expected 'case' keyword before expression">;

def ext_warn_gnu_final : ExtWarn<
  "__final is a GNU extension, consider using C++11 final">,
  InGroup<GccCompat>;

// Declarations.
def err_typename_requires_specqual : Error<
  "type name requires a specifier or qualifier">;
def err_typename_invalid_storageclass : Error<
  "type name does not allow storage class to be specified">;
def err_typename_invalid_functionspec : Error<
  "type name does not allow function specifier to be specified">;
def err_typename_invalid_constexpr : Error<
  "type name does not allow %sub{select_constexpr_spec_kind}0 specifier "
  "to be specified">;
def err_typename_identifiers_only : Error<
  "typename is allowed for identifiers only">;

def err_friend_invalid_in_context : Error<
  "'friend' used outside of class">;
def err_templated_using_directive_declaration : Error<
  "cannot template a using %select{directive|declaration}0">;
def err_unexpected_colon_in_nested_name_spec : Error<
  "unexpected ':' in nested name specifier; did you mean '::'?">;
def err_unexpected_token_in_nested_name_spec : Error<
  "'%0' cannot be a part of nested name specifier; did you mean ':'?">;
def err_bool_redeclaration : Error<
  "redeclaration of C++ built-in type 'bool'">;
def warn_cxx98_compat_static_assert : Warning<
  "'static_assert' declarations are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_ms_static_assert : ExtWarn<
  "use of 'static_assert' without inclusion of <assert.h> is a Microsoft "
  "extension">, InGroup<MicrosoftStaticAssert>;
def ext_cxx_static_assert_no_message : ExtWarn<
  "'static_assert' with no message is a C++17 extension">, InGroup<CXX17>;
def ext_c_static_assert_no_message : ExtWarn<
  "'_Static_assert' with no message is a C23 extension">, InGroup<C23>;
def warn_cxx14_compat_static_assert_no_message : Warning<
  "'static_assert' with no message is incompatible with C++ standards before "
  "C++17">,
  DefaultIgnore, InGroup<CXXPre17Compat>;
def warn_c17_compat_static_assert_no_message : Warning<
  "'_Static_assert' with no message is incompatible with C standards before "
  "C23">,
  DefaultIgnore, InGroup<CPre23Compat>;
def err_function_definition_not_allowed : Error<
  "function definition is not allowed here">;
def err_expected_end_of_enumerator : Error<
  "expected '= constant-expression' or end of enumerator definition">;
def err_expected_coloncolon_after_super : Error<
  "expected '::' after '__super'">;

def ext_decomp_decl_empty : ExtWarn<
  "ISO C++17 does not allow a decomposition group to be empty">,
  InGroup<DiagGroup<"empty-decomposition">>;

// C++26 structured bindings
def ext_decl_attrs_on_binding : ExtWarn<
  "an attribute specifier sequence attached to a structured binding declaration "
  "is a C++2c extension">, InGroup<CXX26>;
def warn_cxx23_compat_decl_attrs_on_binding : Warning<
  "an attribute specifier sequence attached to a structured binding declaration "
  "is incompatible with C++ standards before C++2c">,
  InGroup<CXXPre26Compat>, DefaultIgnore;

/// Objective-C parser diagnostics
def err_expected_minus_or_plus : Error<
  "method type specifier must start with '-' or '+'">;
def err_objc_missing_end : Error<"missing '@end'">;
def note_objc_container_start : Note<
  "%select{class|protocol|category|class extension|implementation"
  "|category implementation}0 started here">;
def warn_objc_protocol_qualifier_missing_id : Warning<
  "protocol has no object type specified; defaults to qualified 'id'">;
def err_objc_unknown_at : Error<"expected an Objective-C directive after '@'">;
def err_illegal_super_cast : Error<
  "cannot cast 'super' (it isn't an expression)">;
def err_nsnumber_nonliteral_unary : Error<
  "@%0 must be followed by a number to form an NSNumber object">;
def warn_cstyle_param : Warning<
  "use of C-style parameters in Objective-C method declarations"
  " is deprecated">, InGroup<DeprecatedDeclarations>;

let CategoryName = "ARC Parse Issue" in {
def err_arc_bridge_retain : Error<
  "unknown cast annotation __bridge_retain; did you mean __bridge_retained?">;
// To be default mapped to an error later.
def warn_arc_bridge_cast_nonarc : Warning<
  "'%0' casts have no effect when not using ARC">,
  InGroup<DiagGroup<"arc-bridge-casts-disallowed-in-nonarc">>;
}

def err_objc_illegal_visibility_spec : Error<
  "illegal visibility specification">;
def err_objc_illegal_interface_qual : Error<"illegal interface qualifier">;
def err_objc_expected_equal_for_getter : Error<
  "expected '=' for Objective-C getter">;
def err_objc_expected_equal_for_setter : Error<
  "expected '=' for Objective-C setter">;
def err_objc_expected_selector_for_getter_setter : Error<
  "expected selector for Objective-C %select{setter|getter}0">;
def err_objc_property_requires_field_name : Error<
  "property requires fields to be named">;
def err_objc_property_bitfield : Error<"property name cannot be a bit-field">;
def err_objc_expected_property_attr : Error<"unknown property attribute %0">;
def err_objc_unexpected_attr : Error<
  "prefix attribute must be followed by an interface, protocol, or implementation">;
def err_objc_postfix_attribute : Error <
  "postfix attributes are not allowed on Objective-C directives">;
def err_objc_postfix_attribute_hint : Error <
  "postfix attributes are not allowed on Objective-C directives, place"
  " them in front of '%select{@interface|@protocol}0'">;
def err_objc_directive_only_in_protocol : Error<
  "directive may only be specified in protocols only">;
def err_missing_catch_finally : Error<
  "@try statement without a @catch and @finally clause">;
def err_objc_concat_string : Error<"unexpected token after Objective-C string">;
def err_expected_objc_container : Error<
  "'@end' must appear in an Objective-C context">;
def err_unexpected_protocol_qualifier : Error<
  "@implementation declaration cannot be protocol qualified">;
def err_objc_unexpected_atend : Error<
  "'@end' appears where closing brace '}' is expected">;
def err_synthesized_property_name : Error<
  "expected a property name in @synthesize">;
def warn_semicolon_before_method_body : Warning<
  "semicolon before method body is ignored">,
  InGroup<SemiBeforeMethodBody>, DefaultIgnore;
def note_extra_comma_message_arg : Note<
  "comma separating Objective-C messaging arguments">;

def err_expected_field_designator : Error<
  "expected a field designator, such as '.field = 4'">;

def err_declaration_does_not_declare_param : Error<
  "declaration does not declare a parameter">;
def err_no_matching_param : Error<"parameter named %0 is missing">;

/// Objective-C++ parser diagnostics
def err_expected_token_instead_of_objcxx_keyword : Error<
  "expected %0; %1 is a keyword in Objective-C++">;
def err_expected_member_name_or_semi_objcxx_keyword : Error<
  "expected member name or ';' after declaration specifiers; "
  "%0 is a keyword in Objective-C++">;

/// C++ parser diagnostics
def err_invalid_operator_on_type : Error<
  "cannot use %select{dot|arrow}0 operator on a type">;
def err_expected_unqualified_id : Error<
  "expected %select{identifier|unqualified-id}0">;
def err_while_loop_outside_of_a_function : Error<
  "while loop outside of a function">;
def err_brackets_go_after_unqualified_id : Error<
  "brackets are not allowed here; to declare an array, "
  "place the brackets after the %select{identifier|name}0">;
def err_unexpected_unqualified_id : Error<"type-id cannot have a name">;
def err_func_def_no_params : Error<
  "function definition does not declare parameters">;
def err_expected_lparen_after_type : Error<
  "expected '(' for function-style cast or type construction">;
def err_expected_init_in_condition : Error<
  "variable declaration in condition must have an initializer">;
def err_expected_init_in_condition_lparen : Error<
  "variable declaration in condition cannot have a parenthesized initializer">;
def err_extraneous_rparen_in_condition : Error<
  "extraneous ')' after condition, expected a statement">;
def ext_alias_in_init_statement : ExtWarn<
  "alias declaration in this context is a C++23 extension">,
  InGroup<CXX23>;
def warn_cxx20_alias_in_init_statement  : Warning<
  "alias declaration in this context is incompatible with C++ standards before C++23">,
  DefaultIgnore, InGroup<CXXPre23Compat>;
def warn_dangling_else : Warning<
  "add explicit braces to avoid dangling else">,
  InGroup<DanglingElse>;
def err_expected_member_or_base_name : Error<
  "expected class member or base class name">;
def err_expected_lbrace_after_base_specifiers : Error<
  "expected '{' after base class list">;
def err_missing_end_of_definition : Error<
  "missing '}' at end of definition of %q0">;
def note_missing_end_of_definition_before : Note<
  "still within definition of %q0 here">;
def ext_ellipsis_exception_spec : Extension<
  "exception specification of '...' is a Microsoft extension">,
  InGroup<MicrosoftExceptionSpec>;
def err_dynamic_and_noexcept_specification : Error<
  "cannot have both throw() and noexcept() clause on the same function">;
def err_except_spec_unparsed : Error<
  "unexpected end of exception specification">;
def ext_dynamic_exception_spec : ExtWarn<
  "ISO C++17 does not allow dynamic exception specifications">,
  InGroup<DynamicExceptionSpec>, DefaultError;
def warn_exception_spec_deprecated : Warning<
  "dynamic exception specifications are deprecated">,
  InGroup<DeprecatedDynamicExceptionSpec>, DefaultIgnore;
def note_exception_spec_deprecated : Note<"use '%0' instead">;
def warn_cxx98_compat_noexcept_decl : Warning<
  "noexcept specifications are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_expected_catch : Error<"expected catch">;
def err_using_namespace_in_class : Error<
  "'using namespace' is not allowed in classes">;
def warn_cxx17_compat_using_enum_declaration : Warning<
  "using enum declaration is incompatible with C++ standards before C++20">,
  InGroup<CXXPre20Compat>, DefaultIgnore;
def ext_using_enum_declaration : ExtWarn<
  "using enum declaration is a C++20 extension">,
  InGroup<CXX20>;
def err_using_enum_expect_identifier : Error<
  "using enum %select{requires an enum or typedef name|"
  "does not permit an elaborated enum specifier}0">;
def err_constructor_bad_name : Error<
  "missing return type for function %0; did you mean the constructor name %1?">;
def err_destructor_tilde_identifier : Error<
  "expected a class name after '~' to name a destructor">;
def err_destructor_tilde_scope : Error<
  "'~' in destructor name should be after nested name specifier">;
def err_destructor_template_id : Error<
  "destructor name %0 does not refer to a template">;
def err_default_arg_unparsed : Error<
  "unexpected end of default argument expression">;
def err_bracket_depth_exceeded : Error<
  "bracket nesting level exceeded maximum of %0">, DefaultFatal;
def note_bracket_depth : Note<
  "use -fbracket-depth=N to increase maximum nesting level">;
def err_misplaced_ellipsis_in_declaration : Error<
  "'...' must %select{immediately precede declared identifier|"
  "be innermost component of anonymous pack declaration}0">;
def warn_misplaced_ellipsis_vararg : Warning<
  "'...' in this location creates a C-style varargs function"
  "%select{, not a function parameter pack|}0">,
  InGroup<DiagGroup<"ambiguous-ellipsis">>;
def note_misplaced_ellipsis_vararg_existing_ellipsis : Note<
  "preceding '...' declares a function parameter pack">;
def note_misplaced_ellipsis_vararg_add_ellipsis : Note<
  "place '...' %select{immediately before declared identifier|here}0 "
  "to declare a function parameter pack">;
def note_misplaced_ellipsis_vararg_add_comma : Note<
  "insert ',' before '...' to silence this warning">;
def ext_abstract_pack_declarator_parens : ExtWarn<
  "ISO C++11 requires a parenthesized pack declaration to have a name">,
  InGroup<DiagGroup<"anonymous-pack-parens">>;
def err_function_is_not_record : Error<
  "unexpected %0 in function call; perhaps remove the %0?">;
def err_super_in_using_declaration : Error<
  "'__super' cannot be used with a using declaration">;
def ext_constexpr_if : ExtWarn<
  "constexpr if is a C++17 extension">, InGroup<CXX17>;
def warn_cxx14_compat_constexpr_if : Warning<
  "constexpr if is incompatible with C++ standards before C++17">,
  DefaultIgnore, InGroup<CXXPre17Compat>;
def ext_consteval_if : ExtWarn<
  "consteval if is a C++23 extension">,
   InGroup<CXX23>;
def warn_cxx20_compat_consteval_if : Warning<
  "consteval if is incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;

def ext_init_statement : ExtWarn<
  "'%select{if|switch}0' initialization statements are a C++17 extension">,
  InGroup<CXX17>;
def warn_cxx14_compat_init_statement : Warning<
  "%select{if|switch}0 initialization statements are incompatible with "
  "C++ standards before C++17">, DefaultIgnore, InGroup<CXXPre17Compat>;
def ext_for_range_init_stmt : ExtWarn<
  "range-based for loop initialization statements are a C++20 extension">,
  InGroup<CXX20>;
def warn_cxx17_compat_for_range_init_stmt : Warning<
  "range-based for loop initialization statements are incompatible with "
  "C++ standards before C++20">, DefaultIgnore, InGroup<CXXPre20Compat>;
def warn_empty_init_statement : Warning<
  "empty initialization statement of '%select{if|switch|range-based for}0' "
  "has no effect">, InGroup<EmptyInitStatement>, DefaultIgnore;
def err_keyword_as_parameter : Error <
  "invalid parameter name: '%0' is a keyword">;

// C++ derived classes
def err_dup_virtual : Error<"duplicate 'virtual' in base specifier">;

// C++ operator overloading
def err_literal_operator_string_prefix : Error<
  "string literal after 'operator' cannot have an encoding prefix">;
def err_literal_operator_string_not_empty : Error<
  "string literal after 'operator' must be '\"\"'">;
def warn_cxx98_compat_literal_operator : Warning<
  "literal operators are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

// Classes.
def err_anon_type_definition : Error<
  "declaration of anonymous %0 must be a definition">;
def err_default_delete_in_multiple_declaration : Error<
  "'= %select{default|delete}0' is a function definition and must occur in a "
  "standalone declaration">;

def warn_cxx98_compat_noexcept_expr : Warning<
  "noexcept expressions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx98_compat_nullptr : Warning<
  "'nullptr' is incompatible with C++98">, InGroup<CXX98Compat>, DefaultIgnore;
def ext_c_nullptr : Extension<
  "'nullptr' is a C23 extension">, InGroup<C23>;

def warn_wrong_clang_attr_namespace : Warning<
  "'__clang__' is a predefined macro name, not an attribute scope specifier; "
  "did you mean '_Clang' instead?">, InGroup<IgnoredAttributes>;
def ext_ns_enum_attribute : Extension<
  "attributes on %select{a namespace|an enumerator}0 declaration are "
  "a C++17 extension">, InGroup<CXX17>;
def warn_cxx14_compat_ns_enum_attribute : Warning<
  "attributes on %select{a namespace|an enumerator}0 declaration are "
  "incompatible with C++ standards before C++17">,
  InGroup<CXXPre17CompatPedantic>, DefaultIgnore;
def warn_cxx98_compat_alignas : Warning<"'alignas' is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx98_compat_attribute : Warning<
  "[[]] attributes are incompatible with C++ standards before C++11">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_ext_cxx11_attributes : Extension<
  "[[]] attributes are a C++11 extension">,
  InGroup<CXX11>;
def warn_pre_c23_compat_attributes : Warning<
  "[[]] attributes are incompatible with C standards before C23">,
  DefaultIgnore, InGroup<CPre23Compat>;
def warn_ext_c23_attributes : Extension<
  "[[]] attributes are a C23 extension">,
  InGroup<C23>;
def err_cxx11_attribute_forbids_arguments : Error<
  "attribute %0 cannot have an argument list">;
def err_attribute_requires_arguments : Error<
  "parentheses must be omitted if %0 attribute's argument list is empty">;
def err_cxx11_attribute_forbids_ellipsis : Error<
  "attribute %0 cannot be used as an attribute pack">;
def warn_cxx14_compat_using_attribute_ns : Warning<
  "default scope specifier for attributes is incompatible with C++ standards "
  "before C++17">, InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_using_attribute_ns : ExtWarn<
  "default scope specifier for attributes is a C++17 extension">,
  InGroup<CXX17>;
def err_using_attribute_ns_conflict : Error<
  "attribute with scope specifier cannot follow default scope specifier">;
def err_attributes_not_allowed : Error<"an attribute list cannot appear here">;
def err_keyword_not_allowed : Error<"%0 cannot appear here">;
def ext_cxx11_attr_placement : ExtWarn<
  "ISO C++ does not allow %select{an attribute list|%0}1 to appear here">,
  InGroup<DiagGroup<"cxx-attribute-extension">>;
def err_attributes_misplaced : Error<"misplaced attributes; expected attributes here">;
def err_keyword_misplaced : Error<"misplaced %0; expected %0 here">;
def err_l_square_l_square_not_attribute : Error<
  "C++11 only allows consecutive left square brackets when "
  "introducing an attribute">;
def err_attribute_argument_parm_pack_not_supported : Error<
  "attribute %0 does not support argument pack expansion">;
def err_ms_declspec_type : Error<
  "__declspec attributes must be an identifier or string literal">;
def err_ms_property_no_getter_or_putter : Error<
  "property does not specify a getter or a putter">;
def err_ms_property_unknown_accessor : Error<
  "expected 'get' or 'put' in property declaration">;
def err_ms_property_has_set_accessor : Error<
  "putter for property must be specified as 'put', not 'set'">;
def err_ms_property_missing_accessor_kind : Error<
  "missing 'get=' or 'put='">;
def err_ms_property_expected_equal : Error<
  "expected '=' after '%0'">;
def err_ms_property_duplicate_accessor : Error<
  "property declaration specifies '%0' accessor twice">;
def err_ms_property_expected_accessor_name : Error<
  "expected name of accessor method">;
def err_ms_property_expected_comma_or_rparen : Error<
  "expected ',' or ')' at end of property accessor list">;
def err_ms_property_initializer : Error<
  "property declaration cannot have a default member initializer">;

def err_assume_attr_expects_cond_expr : Error<
  "use of this expression in an %0 attribute requires parentheses">;

def warn_cxx20_compat_explicit_bool : Warning<
  "this expression will be parsed as explicit(bool) in C++20">,
  InGroup<CXX20Compat>, DefaultIgnore;
def warn_cxx17_compat_explicit_bool : Warning<
  "explicit(bool) is incompatible with C++ standards before C++20">,
  InGroup<CXXPre20Compat>, DefaultIgnore;
def ext_explicit_bool : ExtWarn<"explicit(bool) is a C++20 extension">,
  InGroup<CXX20>;

/// C++ Templates
def err_expected_template : Error<"expected template">;
def err_unknown_template_name : Error<
  "unknown template name %0">;
def err_expected_comma_greater : Error<
  "expected ',' or '>' in template-parameter-list">;
def err_class_on_template_template_param
    : Error<"template template parameter requires 'class'%select{| or "
            "'typename'}0 after the parameter list">;
def ext_template_template_param_typename : ExtWarn<
  "template template parameter using 'typename' is a C++17 extension">,
  InGroup<CXX17>;
def warn_cxx14_compat_template_template_param_typename : Warning<
  "template template parameter using 'typename' is "
  "incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def err_template_spec_syntax_non_template : Error<
  "identifier followed by '<' indicates a class template specialization but "
  "%0 %select{does not refer to a template|refers to a function template|"
  "<unused>|refers to a variable template|<unused>|<unused>|"
  "refers to a concept}1">;
def err_id_after_template_in_nested_name_spec : Error<
  "expected template name after 'template' keyword in nested name specifier">;
def err_unexpected_template_in_unqualified_id : Error<
  "'template' keyword not permitted here">;
def err_unexpected_template_in_destructor_name : Error<
  "'template' keyword not permitted in destructor name">;
def err_unexpected_template_after_using : Error<
  "'template' keyword not permitted after 'using' keyword">;
def err_two_right_angle_brackets_need_space : Error<
  "a space is required between consecutive right angle brackets (use '> >')">;
def err_right_angle_bracket_equal_needs_space : Error<
  "a space is required between a right angle bracket and an equals sign "
  "(use '> =')">;
def warn_cxx11_right_shift_in_template_arg : Warning<
  "use of right-shift operator ('>>') in template argument will require "
  "parentheses in C++11">, InGroup<CXX11Compat>;
def warn_cxx98_compat_two_right_angle_brackets : Warning<
  "consecutive right angle brackets are incompatible with C++98 (use '> >')">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_templated_invalid_declaration : Error<
  "a static_assert declaration cannot be a template">;
def err_multiple_template_declarators : Error<
  "%select{|a template declaration|an explicit template specialization|"
  "an explicit template instantiation}0 can "
  "only %select{|declare|declare|instantiate}0 a single entity">;
def err_explicit_instantiation_with_definition : Error<
  "explicit template instantiation cannot have a definition; if this "
  "definition is meant to be an explicit specialization, add '<>' after the "
  "'template' keyword">;
def err_template_defn_explicit_instantiation : Error<
  "%select{function|class|variable}0 cannot be defined in an explicit instantiation; if this "
  "declaration is meant to be a %select{function|class|variable}0 definition, remove the 'template' keyword">;
def err_friend_explicit_instantiation : Error<
  "friend cannot be declared in an explicit instantiation; if this "
  "declaration is meant to be a friend declaration, remove the 'template' keyword">;
def err_explicit_instantiation_enum : Error<
  "enumerations cannot be explicitly instantiated">;
def err_expected_template_parameter : Error<"expected template parameter">;
def err_empty_requires_expr : Error<
  "a requires expression must contain at least one requirement">;
def err_requires_expr_parameter_list_ellipsis : Error<
  "varargs not allowed in requires expression">;
def err_requires_expr_explicit_object_parameter: Error<
  "a requires expression cannot have an explicit object parameter">;
def err_expected_semi_requirement : Error<
  "expected ';' at end of requirement">;
def err_requires_expr_missing_arrow : Error<
  "expected '->' before expression type requirement">;
def err_requires_expr_expected_type_constraint : Error<
  "expected concept name with optional arguments">;
def err_requires_expr_simple_requirement_noexcept : Error<
  "'noexcept' can only be used in a compound requirement (with '{' '}' around "
  "the expression)">;
def err_requires_expr_in_simple_requirement : Error<
  "requires expression in requirement body; did "
  "you intend to place it in a nested requirement? (add another 'requires' "
  "before the expression)">;
def missing_template_arg_list_after_template_kw : Extension<
  "a template argument list is expected after a name prefixed by the template "
  "keyword">, InGroup<DiagGroup<"missing-template-arg-list-after-template-kw">>,
  DefaultError;

def err_missing_dependent_template_keyword : Error<
  "use 'template' keyword to treat '%0' as a dependent template name">;
def warn_missing_dependent_template_keyword : ExtWarn<
  "use 'template' keyword to treat '%0' as a dependent template name">;

def ext_extern_template : Extension<
  "extern templates are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_extern_template : Warning<
  "extern templates are incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def warn_static_inline_explicit_inst_ignored : Warning<
  "ignoring '%select{static|inline}0' keyword on explicit template "
  "instantiation">, InGroup<DiagGroup<"static-inline-explicit-instantiation">>;

// Constructor template diagnostics.
def err_out_of_line_constructor_template_id : Error<
  "out-of-line constructor for %0 cannot have template arguments">;

def err_expected_qualified_after_typename : Error<
  "expected a qualified name after 'typename'">;
def warn_expected_qualified_after_typename : ExtWarn<
  "expected a qualified name after 'typename'">;

def err_typename_refers_to_non_type_template : Error<
  "typename specifier refers to a non-type template">;
def err_expected_type_name_after_typename : Error<
  "expected an identifier or template-id after '::'">;
def err_explicit_spec_non_template : Error<
  "explicit %select{specialization|instantiation}0 of "
  "%select{non-|undeclared }3template %1 %2">;

def err_default_template_template_parameter_not_template : Error<
  "default template argument for a template template parameter must be a class "
  "template">;

def ext_fold_expression : ExtWarn<
  "pack fold expression is a C++17 extension">,
  InGroup<CXX17>;
def warn_cxx14_compat_fold_expression : Warning<
  "pack fold expression is incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def err_expected_fold_operator : Error<
  "expected a foldable binary operator in fold expression">;
def err_fold_operator_mismatch : Error<
  "operators in fold expression must be the same">;

def err_ctor_init_missing_comma : Error<
  "missing ',' between base or member initializers">;

// C++ declarations
def err_friend_decl_defines_type : Error<
  "cannot define a type in a friend declaration">;
def err_missing_whitespace_digraph : Error<
  "found '<::' after a "
  "%select{template name|addrspace_cast|const_cast|dynamic_cast|reinterpret_cast|static_cast}0"
  " which forms the digraph '<:' (aka '[') and a ':', did you mean '< ::'?">;

def ext_defaulted_deleted_function : ExtWarn<
  "%select{defaulted|deleted}0 function definitions are a C++11 extension">,
  InGroup<CXX11>;
def warn_cxx98_compat_defaulted_deleted_function : Warning<
  "%select{defaulted|deleted}0 function definitions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

def ext_delete_with_message : ExtWarn<
  "'= delete' with a message is a C++2c extension">, InGroup<CXX26>;
def warn_cxx23_delete_with_message : Warning<
  "'= delete' with a message is incompatible with C++ standards before C++2c">,
  DefaultIgnore, InGroup<CXXPre26Compat>;

// C++11 default member initialization
def ext_nonstatic_member_init : ExtWarn<
  "default member initializer for non-static data member is a C++11 "
  "extension">, InGroup<CXX11>;
def warn_cxx98_compat_nonstatic_member_init : Warning<
  "default member initializer for non-static data members is incompatible with "
  "C++98">, InGroup<CXX98Compat>, DefaultIgnore;
def ext_bitfield_member_init: ExtWarn<
  "default member initializer for bit-field is a C++20 extension">,
  InGroup<CXX20>;
def warn_cxx17_compat_bitfield_member_init: Warning<
  "default member initializer for bit-field is incompatible with "
  "C++ standards before C++20">, InGroup<CXXPre20Compat>, DefaultIgnore;
def err_anon_bitfield_member_init : Error<
  "anonymous bit-field cannot have a default member initializer">;
def err_incomplete_array_member_init: Error<
  "array bound cannot be deduced from a default member initializer">;

// C++11 alias-declaration
def ext_alias_declaration : ExtWarn<
  "alias declarations are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_alias_declaration : Warning<
  "alias declarations are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_alias_declaration_not_identifier : Error<
  "name defined in alias declaration must be an identifier">;
def err_alias_declaration_specialization : Error<
  "%select{partial specialization|explicit specialization|explicit instantiation}0 of alias templates is not permitted">;
def err_alias_declaration_pack_expansion : Error<
  "alias declaration cannot be a pack expansion">;

// C++17 using-declaration pack expansions
def ext_multi_using_declaration : ExtWarn<
  "use of multiple declarators in a single using declaration is "
  "a C++17 extension">, InGroup<CXX17>;
def warn_cxx17_compat_multi_using_declaration : Warning<
  "use of multiple declarators in a single using declaration is "
  "incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_using_declaration_pack : ExtWarn<
  "pack expansion of using declaration is a C++17 extension">, InGroup<CXX17>;
def warn_cxx17_compat_using_declaration_pack : Warning<
  "pack expansion using declaration is incompatible with C++ standards "
  "before C++17">, InGroup<CXXPre17Compat>, DefaultIgnore;

// C++11 override control
def ext_override_control_keyword : ExtWarn<
  "'%0' keyword is a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_override_control_keyword : Warning<
  "'%0' keyword is incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_override_control_interface : Error<
  "'%0' keyword not permitted with interface types">;
def ext_ms_sealed_keyword : ExtWarn<
  "'sealed' keyword is a Microsoft extension">,
  InGroup<MicrosoftSealed>;
def ext_ms_abstract_keyword : ExtWarn<
  "'abstract' keyword is a Microsoft extension">,
  InGroup<MicrosoftAbstract>;

def err_access_specifier_interface : Error<
  "interface types cannot specify '%select{private|protected}0' access">;

def err_duplicate_class_virt_specifier : Error<
  "class already marked '%0'">;

def err_duplicate_virt_specifier : Error<
  "class member already marked '%0'">;

def err_virt_specifier_outside_class : Error<
  "'%0' specifier is not allowed outside a class definition">;

def err_expected_parameter_pack : Error<
  "expected the name of a parameter pack">;
def err_paren_sizeof_parameter_pack : Error<
  "missing parentheses around the size of parameter pack %0">;
def err_sizeof_parameter_pack : Error<
  "expected parenthesized parameter pack name in 'sizeof...' expression">;

// C++11 lambda expressions
def err_expected_comma_or_rsquare : Error<
  "expected ',' or ']' in lambda capture list">;
def err_this_captured_by_reference : Error<
  "'this' cannot be captured by reference">;
def err_expected_capture : Error<
  "expected variable name or 'this' in lambda capture list">;
def err_expected_lambda_body : Error<"expected body of lambda expression">;
def warn_cxx98_compat_lambda : Warning<
  "lambda expressions are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def ext_lambda : ExtWarn<"lambdas are a C++11 extension">, InGroup<CXX11>;
def err_lambda_decl_specifier_repeated : Error<
  "%select{'mutable'|'static'|'constexpr'|'consteval'}0 cannot "
  "appear multiple times in a lambda declarator">;
def err_lambda_capture_misplaced_ellipsis : Error<
  "ellipsis in pack %select{|init-}0capture must appear %select{after|before}0 "
  "the name of the capture">;
def err_lambda_capture_multiple_ellipses : Error<
  "multiple ellipses in pack capture">;
def err_capture_default_first : Error<
  "capture default must be first">;
def ext_decl_attrs_on_lambda : ExtWarn<
  "%select{an attribute specifier sequence|%0}1 in this position "
  "is a C++23 extension">, InGroup<CXX23AttrsOnLambda>;
def ext_lambda_missing_parens : ExtWarn<
  "lambda without a parameter clause is a C++23 extension">,
  InGroup<CXX23>;
def warn_cxx20_compat_decl_attrs_on_lambda : Warning<
  "%select{an attribute specifier sequence|%1}0 in this position "
  "is incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;

// C++17 lambda expressions
def err_expected_star_this_capture : Error<
  "expected 'this' following '*' in lambda capture list">;

// C++17 constexpr lambda expressions
def warn_cxx14_compat_constexpr_on_lambda : Warning<
  "constexpr on lambda expressions is incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def ext_constexpr_on_lambda_cxx17 : ExtWarn<
  "'constexpr' on lambda expressions is a C++17 extension">, InGroup<CXX17>;

// C++20 template lambdas
def ext_lambda_template_parameter_list: ExtWarn<
  "explicit template parameter list for lambdas is a C++20 extension">,
  InGroup<CXX20>;
def warn_cxx17_compat_lambda_template_parameter_list: Warning<
  "explicit template parameter list for lambdas is incompatible with "
  "C++ standards before C++20">, InGroup<CXXPre20Compat>, DefaultIgnore;
def err_lambda_template_parameter_list_empty : Error<
  "lambda template parameter list cannot be empty">;

// C++23 static lambdas
def err_static_lambda: ExtWarn<
  "static lambdas are a C++23 extension">, InGroup<CXX23>;
def warn_cxx20_compat_static_lambda : Warning<
  "static lambdas are incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;
def err_static_mutable_lambda : Error<
  "lambda cannot be both mutable and static">;
def err_static_lambda_captures : Error<
  "a static lambda cannot have any captures">;

// Availability attribute
def err_expected_version : Error<
  "expected a version of the form 'major[.minor[.subminor]]'">;
def warn_expected_consistent_version_separator : Warning<
  "use same version number separators '_' or '.'; as in "
  "'major[.minor[.subminor]]'">, InGroup<Availability>;
def err_zero_version : Error<
  "version number must have non-zero major, minor, or sub-minor version">;
def err_availability_expected_platform : Error<
  "expected a platform name, e.g., 'macos'">;
def err_availability_expected_environment : Error<
  "expected an environment name, e.g., 'compute'">;

// objc_bridge_related attribute
def err_objcbridge_related_expected_related_class : Error<
  "expected a related Objective-C class name, e.g., 'NSColor'">;
def err_objcbridge_related_selector_name : Error<
  "expected a class method selector with single argument, e.g., 'colorWithCGColor:'">;

def err_availability_expected_change : Error<
  "expected 'introduced', 'deprecated', or 'obsoleted'">;
def err_availability_unknown_change : Error<
  "%0 is not an availability stage; use 'introduced', 'deprecated', or "
  "'obsoleted'">;
def err_availability_redundant : Error<
  "redundant %0 availability change; only the last specified change will "
  "be used">;
def warn_availability_and_unavailable : Warning<
  "'unavailable' availability overrides all other availability information">,
  InGroup<Availability>;

// @available(...)
def err_avail_query_expected_platform_name : Error<
  "expected a platform name here">;

def err_avail_query_unrecognized_platform_name : Error<
  "unrecognized platform name %0">;
def err_availability_query_wildcard_required: Error<
  "must handle potential future platforms with '*'">;
def err_availability_query_repeated_platform: Error<
  "version for '%0' already specified">;
def err_availability_query_repeated_star : Error<
  "'*' query has already been specified">;

// External source symbol attribute
def err_external_source_symbol_expected_keyword : Error<
  "expected 'language', 'defined_in', 'generated_declaration', or 'USR'">;
def err_external_source_symbol_duplicate_clause : Error<
  "duplicate %0 clause in an 'external_source_symbol' attribute">;

// Type safety attributes
def err_type_safety_unknown_flag : Error<
  "invalid comparison flag %0; use 'layout_compatible' or 'must_be_null'">;

// Type traits
def err_type_trait_arity : Error<
  "type trait requires %0%select{| or more}1 argument%select{|s}2; have "
  "%3 argument%s3">;

// Language specific pragmas
// - Generic warnings
def warn_pragma_expected_lparen : Warning<
  "missing '(' after '#pragma %0' - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_rparen : Warning<
  "missing ')' after '#pragma %0' - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_identifier : Warning<
  "expected identifier in '#pragma %0' - ignored">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_string : Warning<
  "expected string literal in '#pragma %0' - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_missing_argument : Warning<
  "missing argument to '#pragma %0'%select{|; expected %2}1">, InGroup<IgnoredPragmas>;
def warn_pragma_invalid_argument : Warning<
  "unexpected argument '%0' to '#pragma %1'%select{|; expected %3}2">, InGroup<IgnoredPragmas>;

def err_pragma_misplaced_in_decl : Error<"this pragma cannot appear in %0 declaration">;

// '#pragma clang section' related errors
def err_pragma_expected_clang_section_name : Error<
  "expected one of [bss|data|rodata|text|relro] section kind in '#pragma %0'">;
def err_pragma_clang_section_expected_equal : Error<
  "expected '=' following '#pragma clang section %select{invalid|bss|data|rodata|text|relro}0'">;
def warn_pragma_expected_section_name : Warning<
  "expected a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;

def warn_pragma_expected_section_push_pop_or_name : Warning<
  "expected push, pop or a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_section_label_or_name : Warning<
  "expected a stack label or a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_init_seg : Warning<
  "expected 'compiler', 'lib', 'user', or a string literal for the section name in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;

def err_pragma_expected_integer : Error<"expected an integer argument in '#pragma %0'">;
def warn_pragma_expected_integer : Warning<
  "expected integer between %0 and %1 inclusive in '#pragma %2' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_ms_struct : Warning<
  "incorrect use of '#pragma ms_struct on|off' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_ms_fenv_access : Warning<
  "incorrect use of '#pragma fenv_access (on|off)' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_extra_tokens_at_eol : Warning<
  "extra tokens at end of '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_comma : Warning<
  "expected ',' in '#pragma %0'">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_punc : Warning<
  "expected ')' or ',' in '#pragma %0'">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_non_wide_string : Warning<
  "expected non-wide string literal in '#pragma %0'">, InGroup<IgnoredPragmas>;
// - Generic errors
def err_pragma_missing_argument : Error<
  "missing argument to '#pragma %0'%select{|; expected %2}1">;
// - #pragma options
def warn_pragma_options_expected_align : Warning<
  "expected 'align' following '#pragma options' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_align_expected_equal : Warning<
  "expected '=' following '#pragma %select{align|options align}0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_align_invalid_option : Warning<
  "invalid alignment option in '#pragma %select{align|options align}0' - ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma pack
def warn_pragma_unsupported_action : Warning<
  "known but unsupported action '%1' for '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_invalid_specific_action : Warning<
  "unknown action '%1' for '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_expected_action_or_r_paren : Warning<
  "expected action or ')' in '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_invalid_action : Warning<
  "unknown action for '#pragma %0' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_pack_malformed : Warning<
  "expected integer or identifier in '#pragma pack' - ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma intrinsic
def warn_pragma_intrinsic_builtin : Warning<
  "%0 is not a recognized builtin%select{|; consider including <intrin.h> to access non-builtin intrinsics}1">,
  InGroup<IgnoredPragmaIntrinsic>;
// - #pragma unused
def warn_pragma_unused_expected_var : Warning<
  "expected '#pragma unused' argument to be a variable name">,
  InGroup<IgnoredPragmas>;
// - #pragma init_seg
def warn_pragma_init_seg_unsupported_target : Warning<
  "'#pragma init_seg' is only supported when targeting a "
  "Microsoft environment">,
  InGroup<IgnoredPragmas>;
// - #pragma restricted to file scope or start of compound statement
def err_pragma_file_or_compound_scope : Error<
  "'#pragma %0' can only appear at file scope or at the start of a "
  "compound statement">;
// - #pragma stdc unknown
def ext_stdc_pragma_ignored : ExtWarn<"unknown pragma in STDC namespace">,
   InGroup<UnknownPragmas>;
// The C standard 7.6.1p2 says "The [FENV_ACCESS] pragma shall occur either
// outside external declarations or preceding all explicit declarations and
// statements inside a compound statement.
def warn_stdc_fenv_round_not_supported :
   Warning<"pragma STDC FENV_ROUND is not supported">,
   InGroup<UnknownPragmas>;
def warn_stdc_unknown_rounding_mode : Warning<
  "invalid or unsupported rounding mode in '#pragma STDC FENV_ROUND' - ignored">,
  InGroup<IgnoredPragmas>;
def warn_pragma_fp_ignored : Warning<
  "'#pragma %0' is not supported on this target - ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma comment
def err_pragma_comment_malformed : Error<
  "pragma comment requires parenthesized identifier and optional string">;
def err_pragma_comment_unknown_kind : Error<"unknown kind of pragma comment">;
// PS4 recognizes only #pragma comment(lib)
def warn_pragma_comment_ignored : Warning<"'#pragma comment %0' ignored">,
  InGroup<IgnoredPragmas>;
// - #pragma detect_mismatch
def err_pragma_detect_mismatch_malformed : Error<
  "pragma detect_mismatch is malformed; it requires two comma-separated "
  "string literals">;
// - #pragma float_control
def err_pragma_float_control_malformed : Error<
  "pragma float_control is malformed; use 'float_control({push|pop})' or "
  "'float_control({precise|except}, {on|off} [,push])'">;
// - #pragma pointers_to_members
def err_pragma_pointers_to_members_unknown_kind : Error<
  "unexpected %0, expected to see one of %select{|'best_case', 'full_generality', }1"
  "'single_inheritance', 'multiple_inheritance', or 'virtual_inheritance'">;
// - #pragma clang optimize on/off
def err_pragma_optimize_invalid_argument : Error<
  "unexpected argument '%0' to '#pragma clang optimize'; "
  "expected 'on' or 'off'">;
def err_pragma_optimize_extra_argument : Error<
  "unexpected extra argument '%0' to '#pragma clang optimize'">;
// - #pragma clang attribute
def err_pragma_attribute_expected_push_pop_paren : Error<
  "expected 'push', 'pop', or '(' after '#pragma clang attribute'">;
def err_pragma_attribute_invalid_argument : Error<
  "unexpected argument '%0' to '#pragma clang attribute'; "
  "expected 'push' or 'pop'">;
def err_pragma_attribute_expected_attribute : Error<
  "expected an attribute after '('">;
def err_pragma_attribute_expected_attribute_name : Error<
  "expected identifier that represents an attribute name">;
def err_pragma_attribute_extra_tokens_after_attribute : Error<
  "extra tokens after attribute in a '#pragma clang attribute push'">;
def err_pragma_attribute_unsupported_attribute : Error<
  "attribute %0 is not supported by '#pragma clang attribute'">;
def err_pragma_attribute_expected_attribute_syntax : Error<
  "expected an attribute that is specified using the GNU, C++11 or '__declspec'"
  " syntax">;
def note_pragma_attribute_use_attribute_kw : Note<"use the GNU '__attribute__' "
  "syntax">;
def err_pragma_attribute_invalid_subject_set_specifier : Error<
  "expected attribute subject set specifier 'apply_to'">;
def err_pragma_attribute_expected_subject_identifier : Error<
  "expected an identifier that corresponds to an attribute subject rule">;
def err_pragma_attribute_unknown_subject_rule : Error<
  "unknown attribute subject rule '%0'">;
def err_pragma_attribute_expected_subject_sub_identifier : Error<
  "expected an identifier that corresponds to an attribute subject matcher "
  "sub-rule; '%0' matcher %select{does not support sub-rules|supports the "
  "following sub-rules: %2|}1">;
def err_pragma_attribute_unknown_subject_sub_rule : Error<
  "%select{invalid use of|unknown}2 attribute subject matcher sub-rule '%0'; "
  "'%1' matcher %select{does not support sub-rules|supports the following "
  "sub-rules: %3}2">;
def err_pragma_attribute_duplicate_subject : Error<
  "duplicate attribute subject matcher '%0'">;
def err_pragma_attribute_expected_period : Error<
  "expected '.' after pragma attribute namespace %0">;
def err_pragma_attribute_namespace_on_attribute : Error<
  "namespace can only apply to 'push' or 'pop' directives">;
def note_pragma_attribute_namespace_on_attribute : Note<
  "omit the namespace to add attributes to the most-recently"
  " pushed attribute group">;
def warn_no_support_for_eval_method_source_on_m32 : Warning<
  "setting the floating point evaluation method to `source` on a target "
  "without SSE is not supported">, InGroup<Pragmas>;
// - #pragma __debug
def warn_pragma_debug_dependent_argument : Warning<
  "%select{value|type}0-dependent expression passed as an argument to debug "
  "command">, InGroup<IgnoredPragmas>;

// OpenCL EXTENSION pragma (OpenCL 1.1 [9.1])
def warn_pragma_expected_colon : Warning<
  "missing ':' after %0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_expected_predicate : Warning<
  "expected %select{'enable', 'disable', 'begin' or 'end'|'disable'}0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_unknown_extension : Warning<
  "OpenCL extension %0 unknown or does not require pragma - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_unsupported_extension : Warning<
  "unsupported OpenCL extension %0 - ignoring">, InGroup<IgnoredPragmas>;
def warn_pragma_extension_is_core : Warning<
  "OpenCL extension %0 is core feature or supported optional core feature - ignoring">,
  InGroup<OpenCLCoreFeaturesDiagGroup>, DefaultIgnore;
def err_modifier_expected_colon : Error<"missing ':' after %0 modifier">;

// OpenCL errors.
def err_opencl_taking_function_address_parser : Error<
  "taking address of function is not allowed">;
def err_opencl_logical_exclusive_or : Error<
  "^^ is a reserved operator in OpenCL">;

// C++ for OpenCL.
def err_openclcxx_virtual_function : Error<
  "virtual functions are not supported in C++ for OpenCL">;

// OpenACC Support.
def warn_pragma_acc_ignored
    : Warning<"unexpected '#pragma acc ...' in program">,
      InGroup<SourceUsesOpenACC>,
      DefaultIgnore;
def err_acc_unexpected_directive
    : Error<"unexpected OpenACC directive %select{|'#pragma acc %1'}0">;
def err_acc_invalid_directive
    : Error<"invalid OpenACC directive %select{%1|'%1 %2'}0">;
def err_acc_invalid_clause : Error<"invalid OpenACC clause %0">;
def err_acc_missing_directive : Error<"expected OpenACC directive">;
def err_acc_invalid_open_paren
    : Error<"expected clause-list or newline in OpenACC directive">;
def err_acc_invalid_default_clause_kind
    : Error<"invalid value for 'default' clause; expected 'present' or 'none'">;
def err_acc_invalid_tag_kind
    : Error<"invalid tag %0 on '%1' %select{directive|clause}2">;
def err_acc_expected_reduction_operator
    : Error<"missing reduction operator, expected '+', '*', 'max', 'min', '&', "
            "'|', '^', '&&', or '||', follwed by a ':'">;
def err_acc_invalid_reduction_operator
    : Error<"invalid reduction operator,  expected '+', '*', 'max', 'min', "
            "'&', '|', '^', '&&', or '||'">;
def err_acc_incorrect_bind_arg : Error<"expected identifier or string literal">;

// OpenMP support.
def warn_pragma_omp_ignored : Warning<
  "unexpected '#pragma omp ...' in program">, InGroup<SourceUsesOpenMP>, DefaultIgnore;
def warn_omp_extra_tokens_at_eol : Warning<
  "extra tokens at the end of '#pragma omp %0' are ignored">,
  InGroup<ExtraTokens>;
def err_omp_multiple_step_or_linear_modifier : Error<
  "multiple %select{'step size'|'linear modifier'}0 found in linear clause">;
def err_omp_deprecate_old_syntax: Error<
  "old syntax '%0' on '%1' clause was deprecated, use new syntax '%2'">;
def warn_pragma_expected_colon_r_paren : Warning<
  "missing ':' or ')' after %0 - ignoring">, InGroup<IgnoredPragmas>;
def err_omp_unknown_directive : Error<
  "expected an OpenMP directive">;
def err_omp_unexpected_directive : Error<
  "unexpected OpenMP directive %select{|'#pragma omp %1'}0">;
def err_omp_expected_punc : Error<
  "expected ',' or ')' in '%0' %select{clause|directive}1">;
def warn_clause_expected_string : Warning<
  "expected string literal in 'clause %0' - ignoring">, InGroup<IgnoredPragmas>;
def err_omp_unexpected_clause : Error<
  "unexpected OpenMP clause '%0' in directive '#pragma omp %1'">;
def err_omp_unexpected_clause_extension_only : Error<
  "OpenMP clause '%0' is only available as extension, use '-fopenmp-extensions'">;
def err_omp_immediate_directive : Error<
  "'#pragma omp %0' %select{|with '%2' clause }1cannot be an immediate substatement">;
def err_omp_expected_identifier_for_critical : Error<
  "expected identifier specifying the name of the 'omp critical' directive">;
def err_omp_expected_reduction_identifier : Error<
  "expected identifier or one of the following operators: '+', '-', '*', '&', '|', '^', '&&', or '||'">;
def err_omp_expected_equal_in_iterator : Error<
  "expected '=' in iterator specifier">;
def err_omp_expected_punc_after_iterator : Error<
  "expected ',' or ')' after iterator specifier">;
def err_omp_decl_in_declare_simd_variant : Error<
  "function declaration is expected after 'declare %select{simd|variant}0' directive">;
def err_omp_sink_and_source_iteration_not_allowd: Error<" '%0 %select{sink:|source:}1' must be with '%select{omp_cur_iteration - 1|omp_cur_iteration}1'">;
def err_omp_unknown_map_type : Error<
  "incorrect map type, expected one of 'to', 'from', 'tofrom', 'alloc', 'release', or 'delete'">;
def err_omp_more_one_map_type : Error<"map type is already specified">;
def note_previous_map_type_specified_here
    : Note<"map type '%0' is previous specified here">;
def err_omp_unknown_map_type_modifier : Error<
  "incorrect map type modifier, expected one of: 'always', 'close', 'mapper'"
  "%select{|, 'present'|, 'present', 'iterator'}0%select{|, 'ompx_hold'}1">;
def err_omp_map_type_missing : Error<
  "missing map type">;
def err_omp_map_type_modifier_missing : Error<
  "missing map type modifier">;
def err_omp_map_modifier_specification_list : Error<
  "empty modifier-specification-list is not allowed">;
def err_omp_declare_simd_inbranch_notinbranch : Error<
  "unexpected '%0' clause, '%1' is specified already">;
def err_omp_expected_clause_argument
    : Error<"expected '%0' clause with an argument on '#pragma omp %1' "
            "construct">;
def err_expected_end_declare_target_or_variant : Error<
  "expected '#pragma omp end declare %select{target|variant}0'">;
def err_expected_begin_declare_variant
    : Error<"'#pragma omp end declare variant' with no matching '#pragma omp "
            "begin declare variant'">;
def err_expected_begin_assumes
    : Error<"'#pragma omp end assumes' with no matching '#pragma omp begin assumes'">;
def warn_omp_unknown_assumption_clause_missing_id
    : Warning<"valid %0 clauses start with %1; %select{token|tokens}2 will be ignored">,
      InGroup<OpenMPClauses>;
def warn_omp_unknown_assumption_clause_without_args
    : Warning<"%0 clause should not be followed by arguments; tokens will be ignored">,
      InGroup<OpenMPClauses>;
def note_omp_assumption_clause_continue_here
    : Note<"the ignored tokens spans until here">;
def err_omp_declare_target_unexpected_clause: Error<
  "unexpected '%0' clause, only %select{'device_type'|'to' or 'link'|'to', 'link' or 'device_type'|'device_type', 'indirect'|'to', 'link', 'device_type' or 'indirect'}1 clauses expected">;
def err_omp_declare_target_unexpected_clause_52: Error<
  "unexpected '%0' clause, only %select{'device_type'|'enter' or 'link'|'enter', 'link' or 'device_type'|'device_type', 'indirect'|'enter', 'link', 'device_type' or 'indirect'}1 clauses expected">;
def err_omp_begin_declare_target_unexpected_implicit_to_clause: Error<
  "unexpected '(', only 'to', 'link' or 'device_type' clauses expected for 'begin declare target' directive">;
def err_omp_declare_target_wrong_clause_after_implicit_to: Error<
  "unexpected clause after an implicit 'to' clause">;
def err_omp_declare_target_wrong_clause_after_implicit_enter: Error<
  "unexpected clause after an implicit 'enter' clause">;
def err_omp_declare_target_missing_to_or_link_clause: Error<
  "expected at least one %select{'to' or 'link'|'to', 'link' or 'indirect'}0 clause">;
def err_omp_declare_target_missing_enter_or_link_clause: Error<
  "expected at least one %select{'enter' or 'link'|'enter', 'link' or 'indirect'}0 clause">;
def err_omp_declare_target_unexpected_to_clause: Error<
  "unexpected 'to' clause, use 'enter' instead">;
def err_omp_declare_target_unexpected_enter_clause: Error<
  "unexpected 'enter' clause, use 'to' instead">;
def err_omp_declare_target_multiple : Error<
  "%0 appears multiple times in clauses on the same declare target directive">;
def err_omp_declare_target_indirect_device_type: Error<
  "only 'device_type(any)' clause is allowed with indirect clause">;
def err_omp_expected_clause: Error<
  "expected at least one clause on '#pragma omp %0' directive">;
def err_omp_mapper_illegal_identifier : Error<
  "illegal OpenMP user-defined mapper identifier">;
def err_omp_mapper_expected_declarator : Error<
  "expected declarator on 'omp declare mapper' directive">;
def err_omp_unexpected_append_op : Error<
  "unexpected operation specified in 'append_args' clause, expected 'interop'">;
def err_omp_unexpected_execution_modifier : Error<
  "unexpected 'execution' modifier in non-executable context">;
def err_omp_unknown_adjust_args_op : Error<
  "incorrect adjust_args type, expected 'need_device_ptr' or 'nothing'">;
def err_omp_declare_variant_wrong_clause : Error<
  "expected %select{'match'|'match', 'adjust_args', or 'append_args'}0 clause "
  "on 'omp declare variant' directive">;
def err_omp_declare_variant_duplicate_nested_trait : Error<
  "nested OpenMP context selector contains duplicated trait '%0'"
  " in selector '%1' and set '%2' with different score">;
def err_omp_declare_variant_nested_user_condition : Error<
  "nested user conditions in OpenMP context selector not supported (yet)">;
def warn_omp_declare_variant_string_literal_or_identifier
    : Warning<"expected identifier or string literal describing a context "
              "%select{set|selector|property}0; "
              "%select{set|selector|property}0 skipped">,
      InGroup<OpenMPClauses>;
def warn_unknown_declare_variant_isa_trait
    : Warning<"isa trait '%0' is not known to the current target; verify the "
              "spelling or consider restricting the context selector with the "
              "'arch' selector further">,
      InGroup<SourceUsesOpenMP>;
def note_ompx_bare_clause : Note<
  "OpenMP extension clause '%0' only allowed with '#pragma omp %1'">;
def note_omp_declare_variant_ctx_options
    : Note<"context %select{set|selector|property}0 options are: %1">;
def warn_omp_declare_variant_expected
    : Warning<"expected '%0' after the %1; '%0' assumed">,
      InGroup<OpenMPClauses>;
def warn_omp_declare_variant_ctx_not_a_property
    : Warning<"'%0' is not a valid context property for the context selector "
              "'%1' and the context set '%2'; property ignored">,
      InGroup<OpenMPClauses>;
def note_omp_declare_variant_ctx_is_a
    : Note<"'%0' is a context %select{set|selector|property}1 not a context "
           "%select{set|selector|property}2">;
def note_omp_declare_variant_ctx_try : Note<"try 'match(%0={%1%2})'">;
def warn_omp_declare_variant_ctx_not_a_selector
    : Warning<"'%0' is not a valid context selector for the context set '%1'; "
              "selector ignored">,
      InGroup<OpenMPClauses>;
def warn_omp_declare_variant_ctx_not_a_set
    : Warning<"'%0' is not a valid context set in a `declare variant`; set "
              "ignored">,
      InGroup<OpenMPClauses>;
def warn_omp_declare_variant_ctx_mutiple_use
    : Warning<"the context %select{set|selector|property}0 '%1' was used "
              "already in the same 'omp declare variant' directive; "
              "%select{set|selector|property}0 ignored">,
      InGroup<OpenMPClauses>;
def note_omp_declare_variant_ctx_used_here
    : Note<"the previous context %select{set|selector|property}0 '%1' used "
           "here">;
def note_omp_declare_variant_ctx_continue_here
    : Note<"the ignored %select{set|selector|property}0 spans until here">;
def warn_omp_ctx_incompatible_selector_for_set
    : Warning<"the context selector '%0' is not valid for the context set "
              "'%1'; selector ignored">,
      InGroup<OpenMPClauses>;
def note_omp_ctx_compatible_set_for_selector
    : Note<"the context selector '%0' can be nested in the context set '%1'; "
           "try 'match(%1={%0%select{|(property)}2})'">;
def warn_omp_ctx_selector_without_properties
    : Warning<"the context selector '%0' in context set '%1' requires a "
              "context property defined in parentheses; selector ignored">,
      InGroup<OpenMPClauses>;
def warn_omp_ctx_incompatible_property_for_selector
    : Warning<"the context property '%0' is not valid for the context selector "
              "'%1' and the context set '%2'; property ignored">,
      InGroup<OpenMPClauses>;
def note_omp_ctx_compatible_set_and_selector_for_property
    : Note<"the context property '%0' can be nested in the context selector "
           "'%1' which is nested in the context set '%2'; try "
           "'match(%2={%1(%0)})'">;
def warn_omp_ctx_incompatible_score_for_property
    : Warning<"the context selector '%0' in the context set '%1' cannot have a "
              "score ('%2'); score ignored">,
      InGroup<OpenMPClauses>;
def warn_omp_more_one_device_type_clause
    : Warning<"more than one 'device_type' clause is specified">,
      InGroup<OpenMPClauses>;
def err_omp_variant_ctx_second_match_extension : Error<
  "only a single match extension allowed per OpenMP context selector">;
def err_omp_invalid_dsa: Error<
  "data-sharing attribute '%0' in '%1' clause requires OpenMP version %2 or above">;
def err_omp_expected_punc_after_interop_mod : Error<
  "expected ',' after interop modifier">;
def err_omp_expected_interop_type : Error<
  "expected interop type: 'target' and/or 'targetsync'">;
def warn_omp_more_one_interop_type
  : Warning<"interop type '%0' cannot be specified more than once">,
    InGroup<OpenMPClauses>;
def err_expected_sequence_or_directive : Error<
  "expected an OpenMP 'directive' or 'sequence' attribute argument">;
def ext_omp_attributes : ExtWarn<
  "specifying OpenMP directives with [[]] is an OpenMP 5.1 extension">,
  InGroup<OpenMP51Ext>;
def warn_omp51_compat_attributes : Warning<
  "specifying OpenMP directives with [[]] is incompatible with OpenMP "
  "standards before OpenMP 5.1">,
  InGroup<OpenMPPre51Compat>, DefaultIgnore;
def err_omp_expected_colon : Error<"missing ':' in %0">;
def err_omp_missing_comma : Error< "missing ',' after %0">;
def err_omp_expected_context_selector
    : Error<"expected valid context selector in %0">;
def err_omp_requires_out_inout_depend_type : Error<
  "reserved locator 'omp_all_memory' requires 'out' or 'inout' "
  "dependency types">;
def warn_omp_more_one_omp_all_memory : Warning<
  "reserved locator 'omp_all_memory' cannot be specified more than once">,
  InGroup<OpenMPClauses>;
def warn_omp_depend_in_ordered_deprecated : Warning<"'depend' clause for"
  " 'ordered' is deprecated; use 'doacross' instead">, InGroup<Deprecated>;
def warn_omp_invalid_attribute_for_ompx_attributes : Warning<"'ompx_attribute' clause only allows "
  "'amdgpu_flat_work_group_size', 'amdgpu_waves_per_eu', and 'launch_bounds'; "
  "%0 is ignored">, InGroup<OpenMPExtensions>;

// Pragma loop support.
def err_pragma_loop_missing_argument : Error<
  "missing argument; expected %select{an integer value|"
  "'enable'%select{|, 'full'}1%select{|, 'assume_safety'}2 or 'disable'}0">;
def err_pragma_loop_invalid_option : Error<
  "%select{invalid|missing}0 option%select{ %1|}0; expected vectorize, "
  "vectorize_width, interleave, interleave_count, unroll, unroll_count, "
  "pipeline, pipeline_initiation_interval, vectorize_predicate, or distribute">;
def err_pragma_loop_invalid_vectorize_option : Error<
  "vectorize_width loop hint malformed; use vectorize_width(X, fixed) or "
  "vectorize_width(X, scalable) where X is an integer, or vectorize_width('fixed' or 'scalable')">;
def note_pragma_loop_invalid_vectorize_option : Note<
  "vectorize_width loop hint malformed; use vectorize_width(X, fixed) or "
  "vectorize_width(X, scalable) where X is an integer, or vectorize_width('fixed' or 'scalable')">;

def err_pragma_fp_invalid_option : Error<
  "%select{invalid|missing}0 option%select{ %1|}0; expected 'contract', 'reassociate', 'reciprocal', or 'exceptions'">;
def err_pragma_fp_invalid_argument : Error<
  "unexpected argument '%0' to '#pragma clang fp %1'; expected "
  "%select{"
  "'fast' or 'on' or 'off'|"
  "'on' or 'off'|"
  "'on' or 'off'|"
  "'ignore', 'maytrap' or 'strict'|"
  "'source', 'double' or 'extended'}2">;

def err_pragma_invalid_keyword : Error<
  "invalid argument; expected 'enable'%select{|, 'full'}0%select{|, 'assume_safety'}1 or 'disable'">;
def err_pragma_pipeline_invalid_keyword : Error<
    "invalid argument; expected 'disable'">;

// API notes.
def err_type_unparsed : Error<"unparsed tokens following type">;

// Pragma unroll support.
def warn_pragma_unroll_cuda_value_in_parens : Warning<
  "argument to '#pragma unroll' should not be in parentheses in CUDA C/C++">,
  InGroup<CudaCompat>;

def warn_cuda_attr_lambda_position : Warning<
  "nvcc does not allow '__%0__' to appear after the parameter list in lambdas">,
  InGroup<CudaCompat>;
def warn_pragma_force_cuda_host_device_bad_arg : Warning<
  "incorrect use of #pragma clang force_cuda_host_device begin|end">,
  InGroup<IgnoredPragmas>;
def err_pragma_cannot_end_force_cuda_host_device : Error<
  "force_cuda_host_device end pragma without matching "
  "force_cuda_host_device begin">;

def warn_ext_int_deprecated : Warning<
  "'_ExtInt' is deprecated; use '_BitInt' instead">, InGroup<DeprecatedType>;
def ext_bit_int : Extension<
  "'_BitInt' in %select{C17 and earlier|C++}0 is a Clang extension">,
  InGroup<BitIntExtension>;
} // end of Parse Issue category.

let CategoryName = "Modules Issue" in {
def err_unexpected_module_decl : Error<
  "module declaration can only appear at the top level">;
def err_module_expected_ident : Error<
  "expected a module name after '%select{module|import}0'">;
def err_attribute_not_module_attr : Error<
  "%0 attribute cannot be applied to a module">;
def err_keyword_not_module_attr : Error<
  "%0 cannot be applied to a module">;
def err_attribute_not_import_attr : Error<
  "%0 attribute cannot be applied to a module import">;
def err_keyword_not_import_attr : Error<
  "%0 cannot be applied to a module import">;
def err_module_expected_semi : Error<
  "expected ';' after module name">;
def err_global_module_introducer_not_at_start : Error<
  "'module;' introducing a global module fragment can appear only "
  "at the start of the translation unit">;
def err_module_fragment_exported : Error<
  "%select{global|private}0 module fragment cannot be exported">;
def err_private_module_fragment_expected_semi : Error<
  "expected ';' after private module fragment declaration">;
def err_missing_before_module_end : Error<"expected %0 at end of module">;
def err_unsupported_module_partition : Error<
  "module partitions are only supported for C++20 onwards">;
def err_import_not_allowed_here : Error<
  "imports must immediately follow the module declaration">;
def err_partition_import_outside_module : Error<
  "module partition imports must be within a module purview">;
def err_import_in_wrong_fragment : Error<
  "module%select{| partition}0 imports cannot be in the %select{global|private}1 module fragment">;

def err_export_empty : Error<"export declaration cannot be empty">;
}

let CategoryName = "Generics Issue" in {

def err_objc_expected_type_parameter : Error<
  "expected type parameter name">;

def err_objc_parameterized_implementation : Error<
  "@implementation cannot have type parameters">;

def err_objc_type_args_after_protocols : Error<
  "protocol qualifiers must precede type arguments">;

def note_meant_to_use_typename : Note<
  "did you mean to use 'typename'?">;
}

let CategoryName = "Coroutines Issue" in {
def err_for_co_await_not_range_for : Error<
  "'co_await' modifier can only be applied to range-based for loop">;
def warn_deprecated_for_co_await : Warning<
  "'for co_await' belongs to CoroutineTS instead of C++20, which is deprecated">,
  InGroup<DeprecatedCoroutine>;
}

let CategoryName = "Concepts Issue" in {
def err_concept_definition_not_identifier : Error<
  "name defined in concept definition must be an identifier">;
def err_concept_legacy_bool_keyword : Error<
  "ISO C++ does not permit the 'bool' keyword after 'concept'">;
def err_placeholder_expected_auto_or_decltype_auto : Error<
  "expected 'auto' or 'decltype(auto)' after concept name">;
}

def warn_max_tokens : Warning<
  "the number of preprocessor source tokens (%0) exceeds this token limit (%1)">,
  InGroup<MaxTokens>, DefaultIgnore;

def warn_max_tokens_total : Warning<
  "the total number of preprocessor source tokens (%0) exceeds the token limit (%1)">,
  InGroup<MaxTokens>, DefaultIgnore;

def note_max_tokens_total_override : Note<"total token limit set here">;

// HLSL Parser Diagnostics

def err_expected_semantic_identifier : Error<
  "expected HLSL Semantic identifier">;
def err_invalid_declaration_in_hlsl_buffer : Error<
  "invalid declaration inside %select{tbuffer|cbuffer}0">;
def err_unknown_hlsl_semantic : Error<"unknown HLSL semantic %0">;
def err_hlsl_separate_attr_arg_and_number : Error<"wrong argument format for hlsl attribute, use %0 instead">;
def ext_hlsl_access_specifiers : ExtWarn<
  "access specifiers are a clang HLSL extension">,
  InGroup<HLSLExtension>;
def err_hlsl_unsupported_component : Error<"invalid component '%0' used; expected 'x', 'y', 'z', or 'w'">;
def err_hlsl_packoffset_invalid_reg : Error<"invalid resource class specifier '%0' for packoffset, expected 'c'">;

} // end of Parser diagnostics
