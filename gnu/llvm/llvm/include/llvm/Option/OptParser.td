//===--- OptParser.td - Common Option Parsing Interfaces ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the common interfaces used by the option parsing TableGen
//  backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTPARSER_TD
#define LLVM_OPTION_OPTPARSER_TD

// Define the kinds of options.

class OptionKind<string name, int precedence = 0, bit sentinel = false> {
  string Name = name;
  // The kind precedence, kinds with lower precedence are matched first.
  int Precedence = precedence;
  // Indicate a sentinel option.
  bit Sentinel = sentinel;
}

// An option group.
def KIND_GROUP : OptionKind<"Group">;
// The input option kind.
def KIND_INPUT : OptionKind<"Input", 1, true>;
// The unknown option kind.
def KIND_UNKNOWN : OptionKind<"Unknown", 2, true>;
// A flag with no values.
def KIND_FLAG : OptionKind<"Flag">;
// An option which prefixes its (single) value.
def KIND_JOINED : OptionKind<"Joined", 1>;
// An option which is followed by its value.
def KIND_SEPARATE : OptionKind<"Separate">;
// An option followed by its values, which are separated by commas.
def KIND_COMMAJOINED : OptionKind<"CommaJoined">;
// An option which is which takes multiple (separate) arguments.
def KIND_MULTIARG : OptionKind<"MultiArg">;
// An option which is either joined to its (non-empty) value, or followed by its
// value.
def KIND_JOINED_OR_SEPARATE : OptionKind<"JoinedOrSeparate">;
// An option which is both joined to its (first) value, and followed by its
// (second) value.
def KIND_JOINED_AND_SEPARATE : OptionKind<"JoinedAndSeparate">;
// An option which consumes all remaining arguments if there are any.
def KIND_REMAINING_ARGS : OptionKind<"RemainingArgs">;
// An option which consumes an optional joined argument and any other remaining
// arguments.
def KIND_REMAINING_ARGS_JOINED : OptionKind<"RemainingArgsJoined">;

// Define the option flags.

class OptionFlag {}

// HelpHidden - The option should not be displayed in --help, even if it has
// help text. Clients *can* use this in conjunction with the OptTable::PrintHelp
// arguments to implement hidden help groups.
def HelpHidden : OptionFlag;

// RenderAsInput - The option should not render the name when rendered as an
// input (i.e., the option is rendered as values).
def RenderAsInput : OptionFlag;

// RenderJoined - The option should be rendered joined, even if separate (only
// sensible on single value separate options).
def RenderJoined : OptionFlag;

// RenderSeparate - The option should be rendered separately, even if joined
// (only sensible on joined options).
def RenderSeparate : OptionFlag;

// Define Visibility categories

class OptionVisibility {}

// Explicit specifier for default visibility
def DefaultVis : OptionVisibility;

// Define the option group class.

class OptionGroup<string name> {
  string EnumName = ?; // Uses the def name if undefined.
  string Name = name;
  string HelpText = ?;
  OptionGroup Group = ?;
  list<OptionFlag> Flags = [];
  list<OptionVisibility> Visibility = [];
}

// Define the option class.

class HelpTextVariant<list<OptionVisibility> visibilities, string text> {
  list<OptionVisibility> Visibilities = visibilities;
  string Text = text;
}

class Option<list<string> prefixes, string name, OptionKind kind> {
  string EnumName = ?; // Uses the def name if undefined.
  list<string> Prefixes = prefixes;
  string Name = name;
  OptionKind Kind = kind;
  // Used by MultiArg option kind.
  int NumArgs = 0;
  string HelpText = ?;
  list<HelpTextVariant> HelpTextsForVariants = [];
  string MetaVarName = ?;
  string Values = ?;
  code ValuesCode = ?;
  list<OptionFlag> Flags = [];
  list<OptionVisibility> Visibility = [DefaultVis];
  OptionGroup Group = ?;
  Option Alias = ?;
  list<string> AliasArgs = [];
  code MacroPrefix = "";
  code KeyPath = ?;
  code DefaultValue = ?;
  code ImpliedValue = ?;
  code ImpliedCheck = "false";
  code ShouldParse = "true";
  bit ShouldAlwaysEmit = false;
  code NormalizerRetTy = ?;
  code NormalizedValuesScope = "";
  code Normalizer = "";
  code Denormalizer = "";
  code ValueMerger = "mergeForwardValue";
  code ValueExtractor = "extractForwardValue";
  list<code> NormalizedValues = ?;
}

// Helpers for defining options.

class Flag<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_FLAG>;
class Joined<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_JOINED>;
class Separate<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_SEPARATE>;
class CommaJoined<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_COMMAJOINED>;
class MultiArg<list<string> prefixes, string name, int numargs>
  : Option<prefixes, name, KIND_MULTIARG> {
  int NumArgs = numargs;
}
class JoinedOrSeparate<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_JOINED_OR_SEPARATE>;
class JoinedAndSeparate<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_JOINED_AND_SEPARATE>;

// Mix-ins for adding optional attributes.

class Alias<Option alias> { Option Alias = alias; }
class AliasArgs<list<string> aliasargs> { list<string> AliasArgs = aliasargs; }
class EnumName<string name> { string EnumName = name; }
class Flags<list<OptionFlag> flags> { list<OptionFlag> Flags = flags; }
class Visibility<list<OptionVisibility> visibility> {
  list<OptionVisibility> Visibility = visibility;
}
class Group<OptionGroup group> { OptionGroup Group = group; }
class HelpText<string text> { string HelpText = text; }
class HelpTextForVariants<list<OptionVisibility> Visibilities, string text> {
  list<HelpTextVariant> HelpTextsForVariants = [
    HelpTextVariant<Visibilities, text>
  ];
}

class MetaVarName<string name> { string MetaVarName = name; }
class Values<string value> { string Values = value; }
class ValuesCode<code valuecode> { code ValuesCode = valuecode; }

// Helpers for defining marshalling information (typically used in Clang's -cc1
// frontend).

// The key path to the mapped field and the macro prefix for the resulting
// definition database.
class KeyPathAndMacro<string key_path_prefix, string key_path_base,
                      string macro_prefix = ""> {
  code KeyPath = !strconcat(key_path_prefix, key_path_base);
  code MacroPrefix = macro_prefix;
}

// Mixin that implies the specified value for the current option when any of the
// given key paths evaluates to true.
class ImpliedByAnyOf<list<string> key_paths, code value = "true"> {
  code ImpliedCheck = !foldl("false", key_paths, accumulator, key_path,
                             !strconcat(accumulator, " || ", key_path));
  code ImpliedValue = value;
}

// Parent class for marshalled options (typically used in Clang's -cc1 frontend).
class MarshallingInfo<KeyPathAndMacro kpm, code defaultvalue> {
  code KeyPath = kpm.KeyPath;
  code MacroPrefix = kpm.MacroPrefix;
  code DefaultValue = defaultvalue;
}

// Marshalled option accepting a string argument.
class MarshallingInfoString<KeyPathAndMacro kpm, code defaultvalue="std::string()">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeString";
  code Denormalizer = "denormalizeString";
}

// Marshalled option accepting an integer argument.
class MarshallingInfoInt<KeyPathAndMacro kpm, code defaultvalue="0", code type="unsigned">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeStringIntegral<"#type#">";
  code Denormalizer = "denormalizeString<"#type#">";
}

// Marshalled option accepting vector of strings.
class MarshallingInfoStringVector<KeyPathAndMacro kpm>
  : MarshallingInfo<kpm, "std::vector<std::string>({})"> {
  code Normalizer = "normalizeStringVector";
  code Denormalizer = "denormalizeStringVector";
}

// Marshalled option - single positive flag.
class MarshallingInfoFlag<KeyPathAndMacro kpm, code defaultvalue = "false">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeSimpleFlag";
  code Denormalizer = "denormalizeSimpleFlag";
}

// Marshalled option - single negative flag.
class MarshallingInfoNegativeFlag<KeyPathAndMacro kpm, code defaultvalue = "true">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeSimpleNegativeFlag";
  code Denormalizer = "denormalizeSimpleFlag";
}

// Marshalled option - single flag contributing to a bitfield.
class MarshallingInfoBitfieldFlag<KeyPathAndMacro kpm, code value>
  : MarshallingInfoFlag<kpm, "0u"> {
  code Normalizer = "makeFlagToValueNormalizer("#value#")";
  code ValueMerger = "mergeMaskValue";
  code ValueExtractor = "(extractMaskValue<unsigned, decltype("#value#"), "#value#">)";
}

// Implementation detail of BoolOption.
class MarshallingInfoBooleanFlag<KeyPathAndMacro kpm, code defaultvalue, code value,
                                 code other_value, code other_name>
  : MarshallingInfoFlag<kpm, defaultvalue> {
  code Normalizer = "makeBooleanOptionNormalizer("#value#", "#other_value#", OPT_"#other_name#")";
  code Denormalizer = "makeBooleanOptionDenormalizer("#value#")";
}

// Marshalled option accepting any of the specified enum values.
// Typically used with `Values`, `NormalizedValues` and `NormalizedValuesScope`.
class MarshallingInfoEnum<KeyPathAndMacro kpm, code defaultvalue>
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeSimpleEnum";
  code Denormalizer = "denormalizeSimpleEnum";
}

// Mixins for additional marshalling attributes.

class ShouldParseIf<code condition> { code ShouldParse = condition; }
class AlwaysEmit { bit ShouldAlwaysEmit = true; }
class Normalizer<code normalizer> { code Normalizer = normalizer; }
class Denormalizer<code denormalizer> { code Denormalizer = denormalizer; }
class NormalizedValuesScope<code scope> { code NormalizedValuesScope = scope; }
class NormalizedValues<list<code> definitions> { list<code> NormalizedValues = definitions; }
class ValueMerger<code merger> { code ValueMerger = merger; }
class ValueExtractor<code extractor> { code ValueExtractor = extractor; }

// Predefined options.

// FIXME: Have generator validate that these appear in correct position (and
// aren't duplicated).
def INPUT : Option<[], "<input>", KIND_INPUT>;
def UNKNOWN : Option<[], "<unknown>", KIND_UNKNOWN>;

#endif // LLVM_OPTION_OPTPARSER_TD
