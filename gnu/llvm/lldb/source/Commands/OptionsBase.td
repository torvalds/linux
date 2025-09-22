
// The fields below describe how the fields of `OptionDefinition` struct are
// initialized by different definitions in the Options.td and this file.
////////////////////////////////////////////////////////////////////////////////
// Field: usage_mask
// Default value: LLDB_OPT_SET_ALL (Option allowed in all groups)
// Set by:
//  - `Group`: Sets a single group to this option.
//             Example: def foo : Option<"foo", "f">, Group<1>;
//  - `Groups`: Sets a given list of group numbers.
//              Example: def foo : Option<"foo", "f">, Groups<[1,4,6]>;
//  - `GroupRange`: Sets an interval of groups. Start and end are inclusive.
//                  Example: def foo : Option<"foo", "f">, GroupRange<1, 4>;
//                           Sets group 1, 2, 3, 4 for the option.
////////////////////////////////////////////////////////////////////////////////
// Field: required
// Default value: false (Not required)
// Set by:
//   - `Required`: Marks the option as required.
//              Example: def foo : Option<"foo", "f">, Required;
////////////////////////////////////////////////////////////////////////////////
// Field: long_option
// Default value: not available (has to be defined in Option)
// Set by:
//   - `Option` constructor: Already set by constructor.
//                           Example: def foo : Option<"long-option", "l">
//                                                           ^
//                                                    long option value
////////////////////////////////////////////////////////////////////////////////
// Field: short_option
// Default value: not available (has to be defined in Option)
// Set by:
//   - `Option` constructor: Already set by constructor.
//                           Example: def foo : Option<"long-option", "l">
//                                                                     ^
//                                                                short option
////////////////////////////////////////////////////////////////////////////////
// Field: option_has_arg
// Default value: OptionParser::eNoArgument (No argument allowed)
// Set by:
//  - `OptionalArg`: Sets the argument type and marks it as optional.
//  - `Arg`: Sets the argument type and marks it as required.
//  - `EnumArg`: Sets the argument type to an enum and marks it as required.
//  - `OptionalEnumArg`: Same as EnumArg but marks it as optional.
// See argument_type field for more info.
////////////////////////////////////////////////////////////////////////////////
// Field: validator
// Default value: 0 (No validator for option)
// Set by:
//  - `Validator`: Sets the value to a given validator (which has to exist in
//                 the surrounding code.
////////////////////////////////////////////////////////////////////////////////
// Field: enum_values
// Default value: {} (No enum associated with this option)
// Set by:
//  - `OptionalEnumArg`:
//  - `EnumArg`: Sets the argument type and assigns it a enum holding the valid
//               values. The enum needs to be a variable in the including code.
//               Marks the option as required (see option_has_arg).
//               Example: def foo : Option<"foo", "f">,
//                          EnumArg<"SortOrder",
//                          "OptionEnumValues(g_sort_option_enumeration)">;
////////////////////////////////////////////////////////////////////////////////
// Field: completion_type
// Default value: CommandCompletions::eNoCompletion (no tab completion)
// Set by:
//  - `Completion`: Gives the option a single completion kind.
//                  Example: def foo : Option<"foo", "f">,
//                             Completion<"DiskFile">;
//                           Sets the completion to eDiskFileCompletion
//
//  - `Completions`: Sets a given kinds of completions.
//                   Example: def foo : Option<"foo", "f">,
//                              Completions<["DiskFile", "DiskDirectory"]>;
//                            Sets the completion to
//                            `eDiskFileCompletion | eDiskDirectoryCompletion`.
////////////////////////////////////////////////////////////////////////////////
// Field: argument_type
// Default value: eArgTypeNone
// Set by:
//  - `OptionalArg`: Sets the argument type and marks it as optional.
//                   Example: def foo : Option<"foo", "f">, OptionalArg<"Pid">;
//                   Sets the argument type to eArgTypePid and marks option as
//                   optional (see option_has_arg).
//  - `Arg`: Sets the argument type and marks it as required.
//           Example: def foo : Option<"foo", "f">, Arg<"Pid">;
//                    Sets the argument type to eArgTypePid and marks option as
//                    required (see option_has_arg).
//  - `OptionalEnumArg`:
//  - `EnumArg`: Sets the argument type and assigns it a enum holding the valid
//               values. The enum needs to be a variable in the including code.
//               Marks the option as required (see option_has_arg).
//               Example: def foo : Option<"foo", "f">,
//                          EnumArg<"SortOrder",
//                          "OptionEnumValues(g_sort_option_enumeration)">;
//               Use `OptionalEnumArg` for having an option enum argument.
////////////////////////////////////////////////////////////////////////////////
// Field: usage_text
// Default value: ""
// Set by:
//  - `Desc`: Sets the description for the given option.
//            Example: def foo : Option<"foo", "f">, Desc<"does nothing.">;
//                     Sets the description to "does nothing.".

// Base class for all options.
class Option<string fullname, string shortname> {
  string FullName = fullname;
  string ShortName = shortname;
  // The full associated command/subcommand such as "settings set".
  string Command;
}

// Moves the option into a list of option groups.
class Groups<list<int> groups> {
  list<int> Groups = groups;
}

// Moves the option in all option groups in a range.
// Start and end values are inclusive.
class GroupRange<int start, int end> {
  int GroupStart = start;
  int GroupEnd = end;
}
// Moves the option in a single option group.
class Group<int group> {
  int GroupStart = group;
  int GroupEnd = group;
}

// Sets the description for the option that should be
// displayed to the user.
class Desc<string description> {
  string Description = description;
}

// Marks the option as required when calling the
// associated command.
class Required {
  bit Required = 1;
}

// Gives the option an optional argument.
class OptionalArg<string type> {
  string ArgType = type;
  bit OptionalArg = 1;
}

// Gives the option an required argument.
class Arg<string type> {
  string ArgType = type;
}

// Gives the option an required argument.
class EnumArg<string type> {
  string ArgType = type;
}

// Gives the option an required argument.
class OptionalEnumArg<string type> {
  string ArgType = type;
  bit OptionalArg = 1;
}

// Sets the available completions for the given option.
class Completions<list<string> completions> {
  list<string> Completions = completions;
}
// Sets a single completion for the given option.
class Completion<string completion> {
  list<string> Completions = [completion];
}

// Sets the validator for a given option.
class Validator<string validator> {
  string Validator = validator;
}
