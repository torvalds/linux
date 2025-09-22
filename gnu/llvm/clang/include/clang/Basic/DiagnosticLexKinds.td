//==--- DiagnosticLexKinds.td - liblex diagnostics ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Lexer Diagnostics
//===----------------------------------------------------------------------===//

let Component = "Lex", CategoryName = "Lexical or Preprocessor Issue" in {

def null_in_char_or_string : Warning<
  "null character(s) preserved in %select{char|string}0 literal">,
  InGroup<NullCharacter>;
def null_in_file : Warning<"null character ignored">, InGroup<NullCharacter>;
def warn_nested_block_comment : Warning<"'/*' within block comment">,
  InGroup<Comment>;
def escaped_newline_block_comment_end : Warning<
  "escaped newline between */ characters at block comment end">,
  InGroup<Comment>;
def backslash_newline_space : Warning<
  "backslash and newline separated by space">,
  InGroup<DiagGroup<"backslash-newline-escape">>;

// Digraphs.
def warn_cxx98_compat_less_colon_colon : Warning<
  "'<::' is treated as digraph '<:' (aka '[') followed by ':' in C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

def warn_cxx17_compat_spaceship : Warning<
  "'<=>' operator is incompatible with C++ standards before C++20">,
  InGroup<CXXPre20Compat>, DefaultIgnore;
def warn_cxx20_compat_spaceship : Warning<
  "'<=>' is a single token in C++20; "
  "add a space to avoid a change in behavior">,
  InGroup<CXX20Compat>;

// Trigraphs.
def trigraph_ignored : Warning<"trigraph ignored">, InGroup<Trigraphs>;
def trigraph_ignored_block_comment : Warning<
  "ignored trigraph would end block comment">, InGroup<Trigraphs>;
def trigraph_ends_block_comment : Warning<"trigraph ends block comment">,
    InGroup<Trigraphs>;
def trigraph_converted : Warning<"trigraph converted to '%0' character">,
    InGroup<Trigraphs>;

def ext_multi_line_line_comment : Extension<"multi-line // comment">,
    InGroup<Comment>;
def ext_line_comment : Extension<
  "// comments are not allowed in this language">,
  InGroup<Comment>;
def ext_no_newline_eof : Extension<"no newline at end of file">,
  InGroup<NewlineEOF>;
def warn_no_newline_eof : Warning<"no newline at end of file">,
  InGroup<NewlineEOF>, DefaultIgnore;

def warn_cxx98_compat_no_newline_eof : Warning<
  "C++98 requires newline at end of file">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;

def ext_dollar_in_identifier : Extension<"'$' in identifier">,
  InGroup<DiagGroup<"dollar-in-identifier-extension">>;
def ext_charize_microsoft : Extension<
  "charizing operator #@ is a Microsoft extension">,
  InGroup<MicrosoftCharize>;
def ext_comment_paste_microsoft : Extension<
  "pasting two '/' tokens into a '//' comment is a Microsoft extension">,
  InGroup<MicrosoftCommentPaste>;
def ext_ctrl_z_eof_microsoft : Extension<
  "treating Ctrl-Z as end-of-file is a Microsoft extension">,
  InGroup<MicrosoftEndOfFile>;

def ext_token_used : Extension<"extension used">,
  InGroup<DiagGroup<"language-extension-token">>;

def warn_cxx11_keyword : Warning<"'%0' is a keyword in C++11">,
  InGroup<CXX11Compat>, DefaultIgnore;
def warn_cxx20_keyword : Warning<"'%0' is a keyword in C++20">,
  InGroup<CXX20Compat>, DefaultIgnore;
def warn_c99_keyword : Warning<"'%0' is a keyword in C99">,
  InGroup<C99Compat>, DefaultIgnore;
def warn_c23_keyword : Warning<"'%0' is a keyword in C23">,
  InGroup<C23Compat>, DefaultIgnore;

def ext_unterminated_char_or_string : ExtWarn<
  "missing terminating %select{'|'\"'}0 character">, InGroup<InvalidPPToken>;
def ext_empty_character : ExtWarn<"empty character constant">,
  InGroup<InvalidPPToken>;
def err_unterminated_block_comment : Error<"unterminated /* comment">;
def err_invalid_character_to_charify : Error<
  "invalid argument to convert to character">;
def err_unterminated___pragma : Error<"missing terminating ')' character">;

def err_conflict_marker : Error<"version control conflict marker in file">;

def err_raw_delim_too_long : Error<
  "raw string delimiter longer than 16 characters"
  "; use PREFIX( )PREFIX to delimit raw string">;
def err_invalid_char_raw_delim : Error<
  "invalid character '%0' in raw string delimiter"
  "; use PREFIX( )PREFIX to delimit raw string">;
def err_invalid_newline_raw_delim : Error<
  "invalid newline character in raw string delimiter"
  "; use PREFIX( )PREFIX to delimit raw string">;
def err_unterminated_raw_string : Error<
  "raw string missing terminating delimiter )%0\"">;
def warn_cxx98_compat_raw_string_literal : Warning<
  "raw string literals are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;

def warn_cxx26_compat_raw_string_literal_character_set : Warning<
  " '%0' in a raw string literal delimiter is incompatible "
  "with standards before C++2c">,
  InGroup<CXXPre26Compat>, DefaultIgnore;
def ext_cxx26_raw_string_literal_character_set : Extension<
  " '%0' in a raw string literal delimiter is a C++2c extension">,
  InGroup<CXX26>, DefaultIgnore;

def warn_multichar_character_literal : Warning<
  "multi-character character constant">, InGroup<MultiChar>;
def warn_four_char_character_literal : Warning<
  "multi-character character constant">, InGroup<FourByteMultiChar>, DefaultIgnore;


// Unicode and UCNs
def err_invalid_utf8 : Error<
  "source file is not valid UTF-8">;
def warn_invalid_utf8_in_comment : Extension<
  "invalid UTF-8 in comment">, InGroup<DiagGroup<"invalid-utf8">>;
def err_character_not_allowed : Error<
  "unexpected character <U+%0>">;
def err_character_not_allowed_identifier : Error<
  "character <U+%0> not allowed %select{in|at the start of}1 an identifier">;
def ext_unicode_whitespace : ExtWarn<
  "treating Unicode character as whitespace">,
  InGroup<DiagGroup<"unicode-whitespace">>;
def warn_utf8_symbol_homoglyph : Warning<
  "treating Unicode character <U+%0> as an identifier character rather than "
  "as '%1' symbol">, InGroup<DiagGroup<"unicode-homoglyph">>;
def warn_utf8_symbol_zero_width : Warning<
  "identifier contains Unicode character <U+%0> that is invisible in "
  "some environments">, InGroup<DiagGroup<"unicode-zero-width">>;
def ext_mathematical_notation : ExtWarn<
  "mathematical notation character <U+%0> in an identifier is a Clang extension">,
  InGroup<DiagGroup<"mathematical-notation-identifier-extension">>;

def ext_delimited_escape_sequence : Extension<
  "%select{delimited|named}0 escape sequences are a "
  "%select{Clang|C++23}1 extension">,
  InGroup<DiagGroup<"delimited-escape-sequence-extension">>;

def warn_cxx23_delimited_escape_sequence : Warning<
  "%select{delimited|named}0 escape sequences are "
  "incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;

def err_delimited_escape_empty : Error<
  "delimited escape sequence cannot be empty">;
def err_delimited_escape_missing_brace: Error<
  "expected '{' after '\\%0' escape sequence">;
def err_delimited_escape_invalid : Error<
  "invalid digit '%0' in escape sequence">;
def err_hex_escape_no_digits : Error<
  "\\%0 used with no following hex digits">;
def err_invalid_ucn_name : Error<
  "'%0' is not a valid Unicode character name">;
def note_invalid_ucn_name_loose_matching : Note<
  "characters names in Unicode escape sequences are sensitive to case and whitespaces">;
def note_invalid_ucn_name_candidate : Note<
  "did you mean %0 ('%2' U+%1)?">;

def warn_ucn_escape_no_digits : Warning<
  "\\%0 used with no following hex digits; "
  "treating as '\\' followed by identifier">, InGroup<Unicode>;
def err_ucn_escape_incomplete : Error<
  "incomplete universal character name">;
def warn_delimited_ucn_incomplete : Warning<
  "incomplete delimited universal character name; "
  "treating as '\\' '%0' '{' identifier">, InGroup<Unicode>;
def warn_delimited_ucn_empty : Warning<
  "empty delimited universal character name; "
  "treating as '\\' '%0' '{' '}'">, InGroup<Unicode>;
def warn_ucn_escape_incomplete : Warning<
  "incomplete universal character name; "
  "treating as '\\' followed by identifier">, InGroup<Unicode>;
def note_ucn_four_not_eight : Note<"did you mean to use '\\u'?">;

def err_ucn_escape_basic_scs : Error<
  "character '%0' cannot be specified by a universal character name">;
def err_ucn_control_character : Error<
  "universal character name refers to a control character">;
def err_ucn_escape_invalid : Error<"invalid universal character">;
def warn_ucn_escape_surrogate : Warning<
  "universal character name refers to a surrogate character">,
  InGroup<Unicode>;

def warn_c99_compat_unicode_id : Warning<
  "%select{using this character in an identifier|starting an identifier with "
  "this character}0 is incompatible with C99">,
  InGroup<C99Compat>, DefaultIgnore;

def warn_cxx98_compat_literal_ucn_escape_basic_scs : Warning<
  "specifying character '%0' with a universal character name "
  "is incompatible with C++98">, InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx98_compat_literal_ucn_control_character : Warning<
  "universal character name referring to a control character "
  "is incompatible with C++98">, InGroup<CXX98Compat>, DefaultIgnore;
def warn_c23_compat_literal_ucn_escape_basic_scs : Warning<
  "specifying character '%0' with a universal character name is "
  "incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def warn_c23_compat_literal_ucn_control_character : Warning<
  "universal character name referring to a control character "
  "is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def warn_ucn_not_valid_in_c89 : Warning<
  "universal character names are only valid in C99 or C++; "
  "treating as '\\' followed by identifier">, InGroup<Unicode>;
def warn_ucn_not_valid_in_c89_literal : ExtWarn<
  "universal character names are only valid in C99 or C++">, InGroup<Unicode>;


// Literal
def ext_nonstandard_escape : Extension<
  "use of non-standard escape character '\\%0'">;
def ext_unknown_escape : ExtWarn<"unknown escape sequence '\\%0'">,
  InGroup<DiagGroup<"unknown-escape-sequence">>;
def err_invalid_digit : Error<
  "invalid digit '%0' in %select{decimal|octal|binary}1 constant">;
def err_invalid_suffix_constant : Error<
  "invalid suffix '%0' on %select{integer|floating|fixed-point}1 constant">;
def warn_cxx11_compat_digit_separator : Warning<
  "digit separators are incompatible with C++ standards before C++14">,
  InGroup<CXXPre14Compat>, DefaultIgnore;
def warn_c23_compat_digit_separator : Warning<
  "digit separators are incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def err_digit_separator_not_between_digits : Error<
  "digit separator cannot appear at %select{start|end}0 of digit sequence">;
def warn_char_constant_too_large : Warning<
  "character constant too long for its type">;
def err_multichar_character_literal : Error<
  "%select{wide|Unicode}0 character literals may not contain multiple characters">;
def err_exponent_has_no_digits : Error<"exponent has no digits">;
def err_hex_constant_requires : Error<
  "hexadecimal floating %select{constant|literal}0 requires "
  "%select{an exponent|a significand}1">;
def ext_hex_constant_invalid : Extension<
  "hexadecimal floating constants are a C99 feature">, InGroup<C99>;
def ext_hex_literal_invalid : Extension<
  "hexadecimal floating literals are a C++17 feature">, InGroup<CXX17>;
def warn_cxx17_hex_literal : Warning<
  "hexadecimal floating literals are incompatible with "
  "C++ standards before C++17">,
  InGroup<CXXPre17CompatPedantic>, DefaultIgnore;
def ext_binary_literal : Extension<
  "binary integer literals are a C23 extension">, InGroup<C23>;
def warn_c23_compat_binary_literal : Warning<
  "binary integer literals are incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def ext_binary_literal_cxx14 : Extension<
  "binary integer literals are a C++14 extension">, InGroup<CXX14BinaryLiteral>;
def warn_cxx11_compat_binary_literal : Warning<
  "binary integer literals are incompatible with C++ standards before C++14">,
  InGroup<CXXPre14CompatBinaryLiteral>, DefaultIgnore;
def err_pascal_string_too_long : Error<"Pascal string is too long">;
def err_escape_too_large : Error<
  "%select{hex|octal}0 escape sequence out of range">;
def ext_string_too_long : Extension<"string literal of length %0 exceeds "
  "maximum length %1 that %select{C90|ISO C99|C++}2 compilers are required to "
  "support">, InGroup<OverlengthStrings>;
def err_character_too_large : Error<
  "character too large for enclosing character literal type">;
def warn_c99_compat_unicode_literal : Warning<
  "unicode literals are incompatible with C99">,
  InGroup<C99Compat>, DefaultIgnore;
def warn_cxx98_compat_unicode_literal : Warning<
  "unicode literals are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def warn_cxx14_compat_u8_character_literal : Warning<
  "unicode literals are incompatible with C++ standards before C++17">,
  InGroup<CXXPre17Compat>, DefaultIgnore;
def warn_cxx11_compat_user_defined_literal : Warning<
  "identifier after literal will be treated as a user-defined literal suffix "
  "in C++11">, InGroup<CXX11Compat>, DefaultIgnore;
def warn_cxx11_compat_reserved_user_defined_literal : Warning<
  "identifier after literal will be treated as a reserved user-defined literal "
  "suffix in C++11">,
  InGroup<CXX11CompatReservedUserDefinedLiteral>, DefaultIgnore;
def ext_reserved_user_defined_literal : ExtWarn<
  "invalid suffix on literal; C++11 requires a space between literal and "
  "identifier">, InGroup<ReservedUserDefinedLiteral>, DefaultError;
def ext_ms_reserved_user_defined_literal : ExtWarn<
  "invalid suffix on literal; C++11 requires a space between literal and "
  "identifier">, InGroup<ReservedUserDefinedLiteral>;
def err_unsupported_string_concat : Error<
  "unsupported non-standard concatenation of string literals">;

def warn_unevaluated_string_prefix : Warning<
  "encoding prefix '%0' on an unevaluated string literal has no effect"
  "%select{| and is incompatible with c++2c}1">,
  InGroup<DiagGroup<"invalid-unevaluated-string">>;
def err_unevaluated_string_prefix : Error<
  "an unevaluated string literal cannot have an encoding prefix">;
def err_unevaluated_string_udl : Error<
  "an unevaluated string literal cannot be a user-defined literal">;
def err_unevaluated_string_invalid_escape_sequence : Error<
  "invalid escape sequence '%0' in an unevaluated string literal">;
def err_string_concat_mixed_suffix : Error<
  "differing user-defined suffixes ('%0' and '%1') in string literal "
  "concatenation">;
def err_pp_invalid_udl : Error<
  "%select{character|integer}0 literal with user-defined suffix "
  "cannot be used in preprocessor constant expression">;
def err_bad_string_encoding : Error<
  "illegal character encoding in string literal">;
def warn_bad_string_encoding : ExtWarn<
  "illegal character encoding in string literal">,
  InGroup<InvalidSourceEncoding>;
def err_bad_character_encoding : Error<
  "illegal character encoding in character literal">;
def warn_bad_character_encoding : ExtWarn<
  "illegal character encoding in character literal">,
  InGroup<InvalidSourceEncoding>;
def err_lexing_string : Error<"failure when lexing a string literal">;
def err_lexing_char : Error<"failure when lexing a character literal">;
def err_lexing_numeric : Error<"failure when lexing a numeric literal">;
def err_placeholder_in_source : Error<"editor placeholder in source file">;

//===----------------------------------------------------------------------===//
// Preprocessor Diagnostics
//===----------------------------------------------------------------------===//

let CategoryName = "User-Defined Issue" in {
def pp_hash_warning : Warning<"%0">,
  InGroup<PoundWarning>, ShowInSystemHeader;
def err_pp_hash_error : Error<"%0">;
}

def pp_include_next_in_primary : Warning<
  "#include_next in primary source file; "
  "will search from start of include path">,
  InGroup<DiagGroup<"include-next-outside-header">>;
def pp_include_macros_out_of_predefines : Error<
  "the #__include_macros directive is only for internal use by -imacros">;
def pp_include_next_absolute_path : Warning<
  "#include_next in file found relative to primary source file or found by "
  "absolute path; will search from start of include path">,
  InGroup<DiagGroup<"include-next-absolute-path">>;
def ext_c99_whitespace_required_after_macro_name : ExtWarn<
  "ISO C99 requires whitespace after the macro name">, InGroup<C99>;
def ext_missing_whitespace_after_macro_name : ExtWarn<
  "whitespace required after macro name">;
def warn_missing_whitespace_after_macro_name : Warning<
  "whitespace recommended after macro name">;

class NonportablePath  : Warning<
  "non-portable path to file '%0'; specified path differs in case from file"
  " name on disk">;
def pp_nonportable_path : NonportablePath,
  InGroup<DiagGroup<"nonportable-include-path">>;
def pp_nonportable_system_path : NonportablePath, DefaultIgnore,
  InGroup<DiagGroup<"nonportable-system-include-path">>;

def pp_pragma_once_in_main_file : Warning<"#pragma once in main file">,
  InGroup<DiagGroup<"pragma-once-outside-header">>;
def pp_pragma_sysheader_in_main_file : Warning<
  "#pragma system_header ignored in main file">,
  InGroup<DiagGroup<"pragma-system-header-outside-header">>;

def err_illegal_use_of_flt_eval_macro : Error<
  "'__FLT_EVAL_METHOD__' cannot be expanded inside a scope containing "
  "'#pragma clang fp eval_method'">;

def pp_poisoning_existing_macro : Warning<"poisoning existing macro">;
def pp_out_of_date_dependency : Warning<
  "current file is older than dependency %0">;
def ext_pp_undef_builtin_macro : ExtWarn<"undefining builtin macro">,
  InGroup<BuiltinMacroRedefined>;
def ext_pp_redef_builtin_macro : ExtWarn<"redefining builtin macro">,
  InGroup<BuiltinMacroRedefined>;
def pp_disabled_macro_expansion : Warning<
  "disabled expansion of recursive macro">, DefaultIgnore,
  InGroup<DiagGroup<"disabled-macro-expansion">>;
def pp_macro_not_used : Warning<"macro is not used">, DefaultIgnore,
  InGroup<DiagGroup<"unused-macros">>;
def warn_pp_undef_identifier : Warning<
  "%0 is not defined, evaluates to 0">,
  InGroup<DiagGroup<"undef">>, DefaultIgnore;
def warn_pp_undef_prefix : Warning<
  "%0 is not defined, evaluates to 0">,
  InGroup<DiagGroup<"undef-prefix">>, DefaultIgnore;
def warn_pp_ambiguous_macro : Warning<
  "ambiguous expansion of macro %0">, InGroup<AmbiguousMacro>;
def note_pp_ambiguous_macro_chosen : Note<
  "expanding this definition of %0">;
def note_pp_ambiguous_macro_other : Note<
  "other definition of %0">;
def warn_pp_macro_hides_keyword : Extension<
  "keyword is hidden by macro definition">, InGroup<KeywordAsMacro>;
def warn_pp_macro_is_reserved_id : Warning<
  "macro name is a reserved identifier">, DefaultIgnore,
  InGroup<ReservedIdAsMacro>;
def warn_pp_objc_macro_redef_ignored : Warning<
  "ignoring redefinition of Objective-C qualifier macro">,
  InGroup<DiagGroup<"objc-macro-redefinition">>;

def pp_invalid_string_literal : Warning<
  "invalid string literal, ignoring final '\\'">;
def warn_pp_expr_overflow : Warning<
  "integer overflow in preprocessor expression">;
def warn_pp_convert_to_positive : Warning<
  "%select{left|right}0 side of operator converted from negative value to "
  "unsigned: %1">;

def ext_pp_import_directive : Extension<"#import is a language extension">,
  InGroup<DiagGroup<"import-preprocessor-directive-pedantic">>;
def err_pp_import_directive_ms : Error<
  "#import of type library is an unsupported Microsoft feature">;
def ext_pp_include_search_ms : ExtWarn<
  "#include resolved using non-portable Microsoft search rules as: %0">,
  InGroup<MicrosoftInclude>;

def ext_pp_ident_directive : Extension<"#ident is a language extension">;
def ext_pp_include_next_directive : Extension<
  "#include_next is a language extension">, InGroup<GNUIncludeNext>;

def ext_pp_warning_directive : Extension<
  "#warning is a %select{C23|C++23}0 extension">;
def warn_cxx23_compat_warning_directive : Warning<
  "#warning is incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;
def warn_c23_compat_warning_directive : Warning<
  "#warning is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def ext_pp_embed_directive : ExtWarn<
  "#embed is a %select{C23|Clang}0 extension">,
  InGroup<C23>;
def warn_compat_pp_embed_directive : Warning<
  "#embed is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def err_pp_embed_dup_params : Error<
  "cannot specify parameter '%0' twice in the same '#embed' directive">;

def ext_pp_extra_tokens_at_eol : ExtWarn<
  "extra tokens at end of #%0 directive">, InGroup<ExtraTokens>;

def ext_pp_comma_expr : Extension<"comma operator in operand of #if">;
def ext_pp_bad_vaargs_use : Extension<
  "__VA_ARGS__ can only appear in the expansion of a C99 variadic macro">;

def ext_pp_bad_vaopt_use
    : ExtWarn<
          "__VA_OPT__ can only appear in the expansion of a variadic macro">,
      InGroup<VariadicMacros>;

def err_pp_missing_lparen_in_vaopt_use : Error<
  "missing '(' following __VA_OPT__">;
def err_pp_vaopt_nested_use : Error<
  "__VA_OPT__ cannot be nested within its own replacement tokens">;

def err_vaopt_paste_at_start : Error<
  "'##' cannot appear at start of __VA_OPT__ argument">;

def err_vaopt_paste_at_end
    : Error<"'##' cannot appear at end of __VA_OPT__ argument">;

def ext_pp_macro_redef : ExtWarn<"%0 macro redefined">, InGroup<MacroRedefined>;
def ext_variadic_macro : Extension<"variadic macros are a C99 feature">,
  InGroup<VariadicMacros>;
def warn_cxx98_compat_variadic_macro : Warning<
  "variadic macros are incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def ext_named_variadic_macro : Extension<
  "named variadic macros are a GNU extension">, InGroup<VariadicMacros>;
def err_embedded_directive : Error<
  "embedding a #%0 directive within macro arguments is not supported">;
def ext_embedded_directive : Extension<
  "embedding a directive within macro arguments has undefined behavior">,
  InGroup<DiagGroup<"embedded-directive">>;
def ext_c_missing_varargs_arg : Extension<
  "passing no argument for the '...' parameter of a variadic macro is "
  "a C23 extension">, InGroup<C23>;
def ext_cxx_missing_varargs_arg : Extension<
  "passing no argument for the '...' parameter of a variadic macro is "
  "a C++20 extension">, InGroup<CXX20>;
def warn_c17_compat_missing_varargs_arg : Warning<
  "passing no argument for the '...' parameter of a variadic macro is "
  "incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def warn_cxx17_compat_missing_varargs_arg : Warning<
  "passing no argument for the '...' parameter of a variadic macro is "
  "incompatible with C++ standards before C++20">,
  InGroup<CXXPre20Compat>, DefaultIgnore;
def ext_empty_fnmacro_arg : Extension<
  "empty macro arguments are a C99 feature">, InGroup<C99>;
def warn_cxx98_compat_empty_fnmacro_arg : Warning<
  "empty macro arguments are incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def note_macro_here : Note<"macro %0 defined here">;
def note_macro_expansion_here : Note<"expansion of macro %0 requested here">;

def ext_pp_opencl_variadic_macros : Extension<
  "variadic macros are a Clang extension in OpenCL">;

def ext_pp_gnu_line_directive : Extension<
  "this style of line directive is a GNU extension">,
  InGroup<GNULineMarker>;
def err_pp_invalid_directive : Error<
  "invalid preprocessing directive%select{|, did you mean '#%1'?}0">;
def warn_pp_invalid_directive : Warning<
  err_pp_invalid_directive.Summary>, InGroup<DiagGroup<"unknown-directives">>;
def err_pp_unknown_parameter : Error<
  "unknown%select{ | embed}0 preprocessor parameter '%1'">;
def err_pp_directive_required : Error<
  "%0 must be used within a preprocessing directive">;
def err_pp_file_not_found : Error<"'%0' file not found">, DefaultFatal;
def err_pp_through_header_not_found : Error<
  "'%0' required for precompiled header not found">, DefaultFatal;
def err_pp_through_header_not_seen : Error<
  "#include of '%0' not seen while attempting to "
  "%select{create|use}1 precompiled header">, DefaultFatal;
def err_pp_pragma_hdrstop_not_seen : Error<
  "#pragma hdrstop not seen while attempting to use precompiled header">,
  DefaultFatal;
def warn_pp_macro_def_mismatch_with_pch : Warning<
  "definition of macro %0 does not match definition in precompiled header">,
  InGroup<ClangClPch>;
def warn_pp_hdrstop_filename_ignored : Warning<
  "#pragma hdrstop filename not supported, "
  "/Fp can be used to specify precompiled header filename">,
  InGroup<ClangClPch>;
def remark_pp_search_path_usage : Remark<
  "search path used: '%0'">,
  InGroup<UsedSearchPath>;
def err_pp_file_not_found_angled_include_not_fatal : Error<
  "'%0' file not found with <angled> %select{include|import}1; "
  "use \"quotes\" instead">;
def err_pp_file_not_found_typo_not_fatal
    : Error<"'%0' file not found, did you mean '%1'?">;
def note_pp_framework_without_header : Note<
  "did not find header '%0' in framework '%1' (loaded from '%2')">;
def err_pp_error_opening_file : Error<
  "error opening file '%0': %1">, DefaultFatal;
def err_pp_including_mainfile_in_preamble : Error<
  "main file cannot be included recursively when building a preamble">;
def err_pp_empty_filename : Error<"empty filename">;
def err_pp_include_too_deep : Error<"#include nested too deeply">;
def err_pp_expects_filename : Error<"expected \"FILENAME\" or <FILENAME>">;
def err_pp_macro_not_identifier : Error<"macro name must be an identifier">;
def err_pp_missing_macro_name : Error<"macro name missing">;
def err_pp_missing_rparen_in_macro_def : Error<
  "missing ')' in macro parameter list">;
def err_pp_invalid_tok_in_arg_list : Error<
  "invalid token in macro parameter list">;
def err_pp_expected_ident_in_arg_list : Error<
  "expected identifier in macro parameter list">;
def err_pp_expected_comma_in_arg_list : Error<
  "expected comma in macro parameter list">;
def err_pp_duplicate_name_in_arg_list : Error<
  "duplicate macro parameter name %0">;
def err_pp_stringize_not_parameter : Error<
  "'%select{#|#@}0' is not followed by a macro parameter">;
def err_pp_malformed_ident : Error<"invalid #ident directive">;
def err_pp_unterminated_conditional : Error<
  "unterminated conditional directive">;
def pp_err_else_after_else : Error<"#else after #else">;
def pp_err_elif_after_else : Error<
  "%select{#elif|#elifdef|#elifndef}0 after #else">;
def pp_err_else_without_if : Error<"#else without #if">;
def pp_err_elif_without_if : Error<
  "%select{#elif|#elifdef|#elifndef}0 without #if">;
def err_pp_endif_without_if : Error<"#endif without #if">;
def err_pp_expected_value_in_expr : Error<"expected value in expression">;
def err_pp_expected_rparen : Error<"expected ')' in preprocessor expression">;
def err_pp_expected_eol : Error<
  "expected end of line in preprocessor expression">;
def err_pp_expected_after : Error<"missing %1 after %0">;
def err_pp_nested_paren : Error<"nested parentheses not permitted in %0">;
def err_pp_colon_without_question : Error<"':' without preceding '?'">;
def err_pp_division_by_zero : Error<
  "division by zero in preprocessor expression">;
def err_pp_remainder_by_zero : Error<
  "remainder by zero in preprocessor expression">;
def err_pp_expr_bad_token_binop : Error<
  "token is not a valid binary operator in a preprocessor subexpression">;
def err_pp_expr_bad_token_lparen : Error<
  "function-like macro %0 is not defined">;
def err_pp_expr_bad_token_start_expr : Error<
  "invalid token at start of a preprocessor expression">;
def err_pp_invalid_poison : Error<"can only poison identifier tokens">;
def err_pp_used_poisoned_id : Error<"attempt to use a poisoned identifier">;

def err_feature_check_malformed : Error<
  "builtin feature check macro requires a parenthesized identifier">;

def warn_has_warning_invalid_option :
   ExtWarn<"__has_warning expected option name (e.g. \"-Wundef\")">,
   InGroup<MalformedWarningCheck>;

def err_pp_identifier_arg_not_identifier : Error<
  "cannot convert %0 token to an identifier">;

def warn_pragma_include_alias_mismatch_angle :
   ExtWarn<"angle-bracketed include <%0> cannot be aliased to double-quoted "
   "include \"%1\"">, InGroup<UnknownPragmas>;
def warn_pragma_include_alias_mismatch_quote :
   ExtWarn<"double-quoted include \"%0\" cannot be aliased to angle-bracketed "
   "include <%1>">, InGroup<UnknownPragmas>;
def warn_pragma_include_alias_expected :
   ExtWarn<"pragma include_alias expected '%0'">,
   InGroup<UnknownPragmas>;
def warn_pragma_include_alias_expected_filename :
   ExtWarn<"pragma include_alias expected include filename">,
   InGroup<UnknownPragmas>;

// - #pragma warning(...)
def warn_pragma_warning_expected :
  ExtWarn<"#pragma warning expected '%0'">,
  InGroup<UnknownPragmas>;
def warn_pragma_warning_spec_invalid :
  ExtWarn<"#pragma warning expected 'push', 'pop', 'default', 'disable',"
          " 'error', 'once', 'suppress', 1, 2, 3, or 4">,
  InGroup<UnknownPragmas>;
def warn_pragma_warning_push_level :
  ExtWarn<"#pragma warning(push, level) requires a level between 0 and 4">,
  InGroup<UnknownPragmas>;
def warn_pragma_warning_expected_number :
  ExtWarn<"#pragma warning expected a warning number">,
  InGroup<UnknownPragmas>;

// - #pragma deprecated(...)
def warn_pragma_deprecated_macro_use :
  ExtWarn<"macro %0 has been marked as deprecated%select{|: %2}1">,
  InGroup<DeprecatedPragma>;

// - #pragma clang restrict_expansion(...)
def warn_pragma_restrict_expansion_macro_use :
  ExtWarn<"macro %0 has been marked as unsafe for use in headers"
          "%select{|: %2}1">,
  InGroup<RestrictExpansionMacro>;

// - Note for macro annotations.
def note_pp_macro_annotation :
  Note<"macro marked '%select{deprecated|restrict_expansion|final}0' here">;

// - #pragma clang final(...)
def warn_pragma_final_macro :
  ExtWarn<"macro %0 has been marked as final and should not be "
          "%select{undefined|redefined}1">,
  InGroup<FinalMacro>,  ShowInSystemHeader;

// - #pragma execution_character_set(...)
def warn_pragma_exec_charset_expected :
  ExtWarn<"#pragma execution_character_set expected '%0'">,
  InGroup<UnknownPragmas>;
def warn_pragma_exec_charset_spec_invalid :
  ExtWarn<"#pragma execution_character_set expected 'push' or 'pop'">,
  InGroup<UnknownPragmas>;
def warn_pragma_exec_charset_push_invalid :
   ExtWarn<"#pragma execution_character_set invalid value '%0', only 'UTF-8' is supported">,
   InGroup<UnknownPragmas>;

def err__Pragma_malformed : Error<
  "_Pragma takes a parenthesized string literal">;
def err_pragma_message_malformed : Error<
  "pragma %select{message|warning|error}0 requires parenthesized string">;
def err_pragma_push_pop_macro_malformed : Error<
   "pragma %0 requires a parenthesized string">;
def warn_pragma_pop_macro_no_push : Warning<
   "pragma pop_macro could not pop '%0', no matching push_macro">,
  InGroup<IgnoredPragmas>;
def warn_pragma_message : Warning<"%0">,
   InGroup<PoundPragmaMessage>, DefaultWarnNoWerror;
def err_pragma_message : Error<"%0">;
def warn_pragma_ignored : Warning<"unknown pragma ignored">,
   InGroup<UnknownPragmas>, DefaultIgnore;
def ext_on_off_switch_syntax :
   ExtWarn<"expected 'ON' or 'OFF' or 'DEFAULT' in pragma">,
   InGroup<UnknownPragmas>;
def ext_pragma_syntax_eod :
   ExtWarn<"expected end of directive in pragma">,
   InGroup<UnknownPragmas>;
def warn_pragma_diagnostic_invalid :
   ExtWarn<"pragma diagnostic expected 'error', 'warning', 'ignored', 'fatal',"
            " 'push', or 'pop'">,
   InGroup<UnknownPragmas>;
def warn_pragma_diagnostic_cannot_pop :
   ExtWarn<"pragma diagnostic pop could not pop, no matching push">,
   InGroup<UnknownPragmas>;
def warn_pragma_diagnostic_invalid_option :
   ExtWarn<"pragma diagnostic expected option name (e.g. \"-Wundef\")">,
   InGroup<UnknownPragmas>;
def warn_pragma_diagnostic_invalid_token :
   ExtWarn<"unexpected token in pragma diagnostic">,
   InGroup<UnknownPragmas>;
def warn_pragma_diagnostic_unknown_warning :
   ExtWarn<"unknown warning group '%0', ignored">,
   InGroup<UnknownWarningOption>;
// - #pragma __debug
def warn_pragma_debug_missing_command : Warning<
  "missing debug command">, InGroup<IgnoredPragmas>;
def warn_pragma_debug_unexpected_command : Warning<
  "unexpected debug command '%0'">, InGroup<IgnoredPragmas>;
def warn_pragma_debug_unknown_module : Warning<
  "unknown module '%0'">, InGroup<IgnoredPragmas>;
// #pragma module
def err_pp_expected_module_name : Error<
  "expected %select{identifier after '.' in |}0module name">;
def err_pp_module_begin_wrong_module : Error<
  "must specify '-fmodule-name=%0' to enter %select{|submodule of }1"
  "this module%select{ (current module is %3)|}2">;
def err_pp_module_begin_no_module_map : Error<
  "no module map available for module %0">;
def err_pp_module_begin_no_submodule : Error<
  "submodule %0.%1 not declared in module map">;
def err_pp_module_begin_without_module_end : Error<
  "no matching '#pragma clang module end' for this "
  "'#pragma clang module begin'">;
def err_pp_module_end_without_module_begin : Error<
  "no matching '#pragma clang module begin' for this "
  "'#pragma clang module end'">;
def note_pp_module_begin_here : Note<
  "entering module '%0' due to this pragma">;
def err_pp_module_build_missing_end : Error<
  "no matching '#pragma clang module endbuild' for this '#pragma clang module build'">;

def err_defined_macro_name : Error<"'defined' cannot be used as a macro name">;
def err_defined_in_pp_embed : Error<
  "'defined' cannot appear within this context">;
def err_paste_at_start : Error<
  "'##' cannot appear at start of macro expansion">;
def err_paste_at_end : Error<"'##' cannot appear at end of macro expansion">;
def ext_paste_comma : Extension<
  "token pasting of ',' and __VA_ARGS__ is a GNU extension">, InGroup<GNUZeroVariadicMacroArguments>;
def err_unterm_macro_invoc : Error<
  "unterminated function-like macro invocation">;
def err_too_many_args_in_macro_invoc : Error<
  "too many arguments provided to function-like macro invocation">;
def note_suggest_parens_for_macro : Note<
  "parentheses are required around macro argument containing braced "
  "initializer list">;
def note_init_list_at_beginning_of_macro_argument : Note<
  "cannot use initializer list at the beginning of a macro argument">;
def err_too_few_args_in_macro_invoc : Error<
  "too few arguments provided to function-like macro invocation">;
def err_pp_bad_paste : Error<
  "pasting formed '%0', an invalid preprocessing token">;
def ext_pp_bad_paste_ms : ExtWarn<
  "pasting formed '%0', an invalid preprocessing token">, DefaultError,
  InGroup<DiagGroup<"invalid-token-paste">>;
def err_pp_operator_used_as_macro_name : Error<
  "C++ operator %0 (aka %1) used as a macro name">;
def ext_pp_operator_used_as_macro_name : Extension<
  err_pp_operator_used_as_macro_name.Summary>, InGroup<MicrosoftCppMacro>;
def err_pp_illegal_floating_literal : Error<
  "floating point literal in preprocessor expression">;
def err_pp_line_requires_integer : Error<
  "#line directive requires a positive integer argument">;
def ext_pp_line_zero : Extension<
  "#line directive with zero argument is a GNU extension">,
  InGroup<GNUZeroLineDirective>;
def err_pp_line_invalid_filename : Error<
  "invalid filename for #line directive">;
def warn_pp_line_decimal : Warning<
  "%select{#line|GNU line marker}0 directive interprets number as decimal, not octal">;
def err_pp_line_digit_sequence : Error<
  "%select{#line|GNU line marker}0 directive requires a simple digit sequence">;
def err_pp_linemarker_requires_integer : Error<
  "line marker directive requires a positive integer argument">;
def err_pp_linemarker_invalid_filename : Error<
  "invalid filename for line marker directive">;
def err_pp_linemarker_invalid_flag : Error<
  "invalid flag line marker directive">;
def err_pp_linemarker_invalid_pop : Error<
  "invalid line marker flag '2': cannot pop empty include stack">;
def ext_pp_line_too_big : Extension<
  "C requires #line number to be less than %0, allowed as extension">;
def warn_cxx98_compat_pp_line_too_big : Warning<
  "#line number greater than 32767 is incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;

def warn_c23_compat_pp_directive : Warning<
  "use of a '#%select{<BUG IF SEEN>|elifdef|elifndef}0' directive "
  "is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def ext_c23_pp_directive : ExtWarn<
  "use of a '#%select{<BUG IF SEEN>|elifdef|elifndef}0' directive "
  "is a C23 extension">,
  InGroup<C23>;
def warn_cxx23_compat_pp_directive : Warning<
  "use of a '#%select{<BUG IF SEEN>|elifdef|elifndef}0' directive "
  "is incompatible with C++ standards before C++23">,
  InGroup<CXXPre23Compat>, DefaultIgnore;
def ext_cxx23_pp_directive : ExtWarn<
  "use of a '#%select{<BUG IF SEEN>|elifdef|elifndef}0' directive "
  "is a C++23 extension">,
  InGroup<CXX23>;

def err_pp_visibility_non_macro : Error<"no macro named %0">;

def err_pp_arc_cf_code_audited_syntax : Error<"expected 'begin' or 'end'">;
def err_pp_double_begin_of_arc_cf_code_audited : Error<
  "already inside '#pragma clang arc_cf_code_audited'">;
def err_pp_unmatched_end_of_arc_cf_code_audited : Error<
  "not currently inside '#pragma clang arc_cf_code_audited'">;
def err_pp_include_in_arc_cf_code_audited : Error<
  "cannot %select{#include files|import headers}0 "
  "inside '#pragma clang arc_cf_code_audited'">;
def err_pp_eof_in_arc_cf_code_audited : Error<
  "'#pragma clang arc_cf_code_audited' was not ended within this file">;

def warn_pp_date_time : Warning<
  "expansion of date or time macro is not reproducible">,
  ShowInSystemHeader, DefaultIgnore, InGroup<DiagGroup<"date-time">>;

// Module map parsing
def err_mmap_unknown_token : Error<"skipping stray token">;
def err_mmap_expected_module : Error<"expected module declaration">;
def err_mmap_expected_module_name : Error<"expected module name">;
def err_mmap_expected_lbrace : Error<"expected '{' to start module '%0'">;
def err_mmap_expected_rbrace : Error<"expected '}'">;
def note_mmap_lbrace_match : Note<"to match this '{'">;
def err_mmap_expected_rsquare : Error<"expected ']' to close attribute">;
def note_mmap_lsquare_match : Note<"to match this ']'">;
def err_mmap_expected_member : Error<
  "expected umbrella, header, submodule, or module export">;
def err_mmap_expected_header : Error<"expected a header name after '%0'">;
def err_mmap_expected_mmap_file : Error<"expected a module map file name">;
def err_mmap_module_redefinition : Error<
  "redefinition of module '%0'">;
def note_mmap_prev_definition : Note<"previously defined here">;
def err_mmap_umbrella_clash : Error<
  "umbrella for module '%0' already covers this directory">;
def err_mmap_module_id : Error<
  "expected a module name or '*'">;
def err_mmap_expected_library_name : Error<
  "expected %select{library|framework}0 name as a string">;
def err_mmap_config_macro_submodule : Error<
  "configuration macros are only allowed in top-level modules">;
def err_mmap_use_decl_submodule : Error<
  "use declarations are only allowed in top-level modules">;
def err_mmap_expected_config_macro : Error<
  "expected configuration macro name after ','">;
def err_mmap_expected_conflicts_comma : Error<
  "expected ',' after conflicting module name">;
def err_mmap_expected_conflicts_message : Error<
  "expected a message describing the conflict with '%0'">;
def err_mmap_missing_module_unqualified : Error<
  "no module named '%0' visible from '%1'">;
def err_mmap_missing_module_qualified : Error<
  "no module named '%0' in '%1'">;
def err_mmap_missing_parent_module: Error<
  "no module named '%0' %select{found|in '%2'}1, "
  "parent module must be defined before the submodule">;
def err_mmap_top_level_inferred_submodule : Error<
  "only submodules and framework modules may be inferred with wildcard syntax">;
def err_mmap_inferred_no_umbrella : Error<
  "inferred submodules require a module with an umbrella">;
def err_mmap_inferred_framework_submodule : Error<
  "inferred submodule cannot be a framework submodule">;
def err_mmap_explicit_inferred_framework : Error<
  "inferred framework modules cannot be 'explicit'">;
def err_mmap_missing_exclude_name : Error<
  "expected excluded module name">;
def err_mmap_inferred_redef : Error<
  "redefinition of inferred submodule">;
def err_mmap_expected_lbrace_wildcard : Error<
  "expected '{' to start inferred submodule">;
def err_mmap_expected_inferred_member : Error<
  "expected %select{module exclusion with 'exclude'|'export *'}0">;
def err_mmap_expected_export_wildcard : Error<
  "only '*' can be exported from an inferred submodule">;
def err_mmap_explicit_top_level : Error<
  "'explicit' is not permitted on top-level modules">;
def err_mmap_nested_submodule_id : Error<
  "qualified module name can only be used to define modules at the top level">;
def err_mmap_expected_feature : Error<"expected a feature name">;
def err_mmap_expected_attribute : Error<"expected an attribute name">;
def warn_mmap_unknown_attribute : Warning<"unknown attribute '%0'">,
  InGroup<IgnoredAttributes>;
def warn_mmap_mismatched_private_submodule : Warning<
  "private submodule '%0' in private module map, expected top-level module">,
  InGroup<PrivateModule>;
def warn_mmap_mismatched_private_module_name : Warning<
  "expected canonical name for private module '%0'">,
  InGroup<PrivateModule>;
def note_mmap_rename_top_level_private_module : Note<
  "rename '%0' to ensure it can be found by name">;
def warn_mmap_incomplete_framework_module_declaration : Warning<
  "skipping '%0' because module declaration of '%1' lacks the 'framework' qualifier">,
  InGroup<IncompleteFrameworkModuleDeclaration>;
def note_mmap_add_framework_keyword : Note<
  "use 'framework module' to declare module '%0'">;

def err_mmap_duplicate_header_attribute : Error<
  "header attribute '%0' specified multiple times">;
def err_mmap_invalid_header_attribute_value : Error<
  "expected integer literal as value for header attribute '%0'">;
def err_mmap_expected_header_attribute : Error<
  "expected a header attribute name ('size' or 'mtime')">;
def err_mmap_conflicting_export_as : Error<
  "conflicting re-export of module '%0' as '%1' or '%2'">;
def warn_mmap_redundant_export_as : Warning<
  "module '%0' already re-exported as '%1'">,
  InGroup<PrivateModule>;
def err_mmap_submodule_export_as : Error<
  "only top-level modules can be re-exported as public">;

def warn_quoted_include_in_framework_header : Warning<
  "double-quoted include \"%0\" in framework header, "
  "expected angle-bracketed instead"
  >, InGroup<FrameworkHdrQuotedInclude>, DefaultIgnore;
def warn_framework_include_private_from_public : Warning<
  "public framework header includes private framework header '%0'"
  >, InGroup<FrameworkIncludePrivateFromPublic>;
def warn_deprecated_module_dot_map : Warning<
  "'%0' as a module map name is deprecated, rename it to %select{module.modulemap|module.private.modulemap}1%select{| in the 'Modules' directory of the framework}2">,
  InGroup<DeprecatedModuleDotMap>;

def remark_pp_include_directive_modular_translation : Remark<
  "treating #%select{include|import|include_next|__include_macros}0 as an "
  "import of module '%1'">, InGroup<ModuleIncludeDirectiveTranslation>;
def note_implicit_top_level_module_import_here : Note<
  "submodule of top-level module '%0' implicitly imported here">;
def warn_uncovered_module_header : Warning<
  "umbrella header for module '%0' does not include header '%1'">,
  InGroup<IncompleteUmbrella>;
def warn_mmap_umbrella_dir_not_found : Warning<
  "umbrella directory '%0' not found">,
  InGroup<IncompleteUmbrella>;
def err_expected_id_building_module : Error<
  "expected a module name in '__building_module' expression">;
def warn_use_of_private_header_outside_module : Warning<
  "use of private header from outside its module: '%0'">,
  InGroup<DiagGroup<"private-header">>, DefaultError;
def err_undeclared_use_of_module : Error<
  "module %0 does not depend on a module exporting '%1'">;
def err_undeclared_use_of_module_indirect : Error<
  "module %0 does not directly depend on a module exporting '%1', which is part of indirectly-used module %2">;
def warn_non_modular_include_in_framework_module : Warning<
  "include of non-modular header inside framework module '%0': '%1'">,
  InGroup<NonModularIncludeInFrameworkModule>, DefaultIgnore;
def warn_non_modular_include_in_module : Warning<
  "include of non-modular header inside module '%0': '%1'">,
  InGroup<NonModularIncludeInModule>, DefaultIgnore;
def warn_module_conflict : Warning<
  "module '%0' conflicts with already-imported module '%1': %2">,
  InGroup<ModuleConflict>;

// C++20 modules
def err_header_import_semi_in_macro : Error<
  "semicolon terminating header import declaration cannot be produced "
  "by a macro">;
def err_header_import_not_header_unit : Error<
  "header file %0 (aka '%1') cannot be imported because "
  "it is not known to be a header unit">;
def warn_pp_include_angled_in_module_purview : Warning<
  "'#include <filename>' attaches the declarations to the named module '%0'"
  ", which is not usually intended; consider moving that directive before "
  "the module declaration">,
  InGroup<DiagGroup<"include-angled-in-module-purview">>;

def warn_header_guard : Warning<
  "%0 is used as a header guard here, followed by #define of a different macro">,
  InGroup<DiagGroup<"header-guard">>;
def note_header_guard : Note<
  "%0 is defined here; did you mean %1?">;

def warn_defined_in_object_type_macro : Warning<
  "macro expansion producing 'defined' has undefined behavior">,
  InGroup<ExpansionToDefined>;
def warn_defined_in_function_type_macro : Extension<
  "macro expansion producing 'defined' has undefined behavior">,
  InGroup<ExpansionToDefined>;

let CategoryName = "Nullability Issue" in {

def err_pp_assume_nonnull_syntax : Error<"expected 'begin' or 'end'">;
def err_pp_double_begin_of_assume_nonnull : Error<
  "already inside '#pragma clang assume_nonnull'">;
def err_pp_unmatched_end_of_assume_nonnull : Error<
  "not currently inside '#pragma clang assume_nonnull'">;
def err_pp_include_in_assume_nonnull : Error<
  "cannot %select{#include files|import headers}0 "
  "inside '#pragma clang assume_nonnull'">;
def err_pp_eof_in_assume_nonnull : Error<
  "'#pragma clang assume_nonnull' was not ended within this file">;

}

let CategoryName = "Dependency Directive Source Scanner Issue" in {

def err_dep_source_scanner_missing_semi_after_at_import : Error<
  "could not find ';' after @import">;
def err_dep_source_scanner_unexpected_tokens_at_import : Error<
  "unexpected extra tokens at end of @import declaration">;

}

def err_pp_double_begin_pragma_unsafe_buffer_usage :
Error<"already inside '#pragma unsafe_buffer_usage'">;

def err_pp_unmatched_end_begin_pragma_unsafe_buffer_usage :
Error<"not currently inside '#pragma unsafe_buffer_usage'">;

def err_pp_unclosed_pragma_unsafe_buffer_usage :
Error<"'#pragma unsafe_buffer_usage' was not ended">;

def err_pp_pragma_unsafe_buffer_usage_syntax :
Error<"expected 'begin' or 'end'">;
}
