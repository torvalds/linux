//===-- CommandOptionArgumentTable.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

namespace lldb_private {
llvm::StringRef RegisterNameHelpTextCallback() {
  return "Register names can be specified using the architecture specific "
         "names.  "
         "They can also be specified using generic names.  Not all generic "
         "entities have "
         "registers backing them on all architectures.  When they don't the "
         "generic name "
         "will return an error.\n"
         "The generic names defined in lldb are:\n"
         "\n"
         "pc       - program counter register\n"
         "ra       - return address register\n"
         "fp       - frame pointer register\n"
         "sp       - stack pointer register\n"
         "flags    - the flags register\n"
         "arg{1-6} - integer argument passing registers.\n";
}

llvm::StringRef BreakpointIDHelpTextCallback() {
  return "Breakpoints are identified using major and minor numbers; the major "
         "number corresponds to the single entity that was created with a "
         "'breakpoint "
         "set' command; the minor numbers correspond to all the locations that "
         "were "
         "actually found/set based on the major breakpoint.  A full breakpoint "
         "ID might "
         "look like 3.14, meaning the 14th location set for the 3rd "
         "breakpoint.  You "
         "can specify all the locations of a breakpoint by just indicating the "
         "major "
         "breakpoint number. A valid breakpoint ID consists either of just the "
         "major "
         "number, or the major number followed by a dot and the location "
         "number (e.g. "
         "3 or 3.2 could both be valid breakpoint IDs.)";
}

llvm::StringRef BreakpointIDRangeHelpTextCallback() {
  return "A 'breakpoint ID list' is a manner of specifying multiple "
         "breakpoints. "
         "This can be done through several mechanisms.  The easiest way is to "
         "just "
         "enter a space-separated list of breakpoint IDs.  To specify all the "
         "breakpoint locations under a major breakpoint, you can use the major "
         "breakpoint number followed by '.*', eg. '5.*' means all the "
         "locations under "
         "breakpoint 5.  You can also indicate a range of breakpoints by using "
         "<start-bp-id> - <end-bp-id>.  The start-bp-id and end-bp-id for a "
         "range can "
         "be any valid breakpoint IDs.  It is not legal, however, to specify a "
         "range "
         "using specific locations that cross major breakpoint numbers.  I.e. "
         "3.2 - 3.7"
         " is legal; 2 - 5 is legal; but 3.2 - 4.4 is not legal.";
}

llvm::StringRef BreakpointNameHelpTextCallback() {
  return "A name that can be added to a breakpoint when it is created, or "
         "later "
         "on with the \"breakpoint name add\" command.  "
         "Breakpoint names can be used to specify breakpoints in all the "
         "places breakpoint IDs "
         "and breakpoint ID ranges can be used.  As such they provide a "
         "convenient way to group breakpoints, "
         "and to operate on breakpoints you create without having to track the "
         "breakpoint number.  "
         "Note, the attributes you set when using a breakpoint name in a "
         "breakpoint command don't "
         "adhere to the name, but instead are set individually on all the "
         "breakpoints currently tagged with that "
         "name.  Future breakpoints "
         "tagged with that name will not pick up the attributes previously "
         "given using that name.  "
         "In order to distinguish breakpoint names from breakpoint IDs and "
         "ranges, "
         "names must start with a letter from a-z or A-Z and cannot contain "
         "spaces, \".\" or \"-\".  "
         "Also, breakpoint names can only be applied to breakpoints, not to "
         "breakpoint locations.";
}

llvm::StringRef GDBFormatHelpTextCallback() {
  return "A GDB format consists of a repeat count, a format letter and a size "
         "letter. "
         "The repeat count is optional and defaults to 1. The format letter is "
         "optional "
         "and defaults to the previous format that was used. The size letter "
         "is optional "
         "and defaults to the previous size that was used.\n"
         "\n"
         "Format letters include:\n"
         "o - octal\n"
         "x - hexadecimal\n"
         "d - decimal\n"
         "u - unsigned decimal\n"
         "t - binary\n"
         "f - float\n"
         "a - address\n"
         "i - instruction\n"
         "c - char\n"
         "s - string\n"
         "T - OSType\n"
         "A - float as hex\n"
         "\n"
         "Size letters include:\n"
         "b - 1 byte  (byte)\n"
         "h - 2 bytes (halfword)\n"
         "w - 4 bytes (word)\n"
         "g - 8 bytes (giant)\n"
         "\n"
         "Example formats:\n"
         "32xb - show 32 1 byte hexadecimal integer values\n"
         "16xh - show 16 2 byte hexadecimal integer values\n"
         "64   - show 64 2 byte hexadecimal integer values (format and size "
         "from the last format)\n"
         "dw   - show 1 4 byte decimal integer value\n";
}

llvm::StringRef FormatHelpTextCallback() {
  static std::string help_text;

  if (!help_text.empty())
    return help_text;

  StreamString sstr;
  sstr << "One of the format names (or one-character names) that can be used "
          "to show a variable's value:\n";
  for (Format f = eFormatDefault; f < kNumFormats; f = Format(f + 1)) {
    if (f != eFormatDefault)
      sstr.PutChar('\n');

    char format_char = FormatManager::GetFormatAsFormatChar(f);
    if (format_char)
      sstr.Printf("'%c' or ", format_char);

    sstr.Printf("\"%s\"", FormatManager::GetFormatAsCString(f));
  }

  sstr.Flush();

  help_text = std::string(sstr.GetString());

  return help_text;
}

llvm::StringRef LanguageTypeHelpTextCallback() {
  static std::string help_text;

  if (!help_text.empty())
    return help_text;

  StreamString sstr;
  sstr << "One of the following languages:\n";

  Language::PrintAllLanguages(sstr, "  ", "\n");

  sstr.Flush();

  help_text = std::string(sstr.GetString());

  return help_text;
}

llvm::StringRef SummaryStringHelpTextCallback() {
  return "A summary string is a way to extract information from variables in "
         "order to present them using a summary.\n"
         "Summary strings contain static text, variables, scopes and control "
         "sequences:\n"
         "  - Static text can be any sequence of non-special characters, i.e. "
         "anything but '{', '}', '$', or '\\'.\n"
         "  - Variables are sequences of characters beginning with ${, ending "
         "with } and that contain symbols in the format described below.\n"
         "  - Scopes are any sequence of text between { and }. Anything "
         "included in a scope will only appear in the output summary if there "
         "were no errors.\n"
         "  - Control sequences are the usual C/C++ '\\a', '\\n', ..., plus "
         "'\\$', '\\{' and '\\}'.\n"
         "A summary string works by copying static text verbatim, turning "
         "control sequences into their character counterpart, expanding "
         "variables and trying to expand scopes.\n"
         "A variable is expanded by giving it a value other than its textual "
         "representation, and the way this is done depends on what comes after "
         "the ${ marker.\n"
         "The most common sequence if ${var followed by an expression path, "
         "which is the text one would type to access a member of an aggregate "
         "types, given a variable of that type"
         " (e.g. if type T has a member named x, which has a member named y, "
         "and if t is of type T, the expression path would be .x.y and the way "
         "to fit that into a summary string would be"
         " ${var.x.y}). You can also use ${*var followed by an expression path "
         "and in that case the object referred by the path will be "
         "dereferenced before being displayed."
         " If the object is not a pointer, doing so will cause an error. For "
         "additional details on expression paths, you can type 'help "
         "expr-path'. \n"
         "By default, summary strings attempt to display the summary for any "
         "variable they reference, and if that fails the value. If neither can "
         "be shown, nothing is displayed."
         "In a summary string, you can also use an array index [n], or a "
         "slice-like range [n-m]. This can have two different meanings "
         "depending on what kind of object the expression"
         " path refers to:\n"
         "  - if it is a scalar type (any basic type like int, float, ...) the "
         "expression is a bitfield, i.e. the bits indicated by the indexing "
         "operator are extracted out of the number"
         " and displayed as an individual variable\n"
         "  - if it is an array or pointer the array items indicated by the "
         "indexing operator are shown as the result of the variable. if the "
         "expression is an array, real array items are"
         " printed; if it is a pointer, the pointer-as-array syntax is used to "
         "obtain the values (this means, the latter case can have no range "
         "checking)\n"
         "If you are trying to display an array for which the size is known, "
         "you can also use [] instead of giving an exact range. This has the "
         "effect of showing items 0 thru size - 1.\n"
         "Additionally, a variable can contain an (optional) format code, as "
         "in ${var.x.y%code}, where code can be any of the valid formats "
         "described in 'help format', or one of the"
         " special symbols only allowed as part of a variable:\n"
         "    %V: show the value of the object by default\n"
         "    %S: show the summary of the object by default\n"
         "    %@: show the runtime-provided object description (for "
         "Objective-C, it calls NSPrintForDebugger; for C/C++ it does "
         "nothing)\n"
         "    %L: show the location of the object (memory address or a "
         "register name)\n"
         "    %#: show the number of children of the object\n"
         "    %T: show the type of the object\n"
         "Another variable that you can use in summary strings is ${svar . "
         "This sequence works exactly like ${var, including the fact that "
         "${*svar is an allowed sequence, but uses"
         " the object's synthetic children provider instead of the actual "
         "objects. For instance, if you are using STL synthetic children "
         "providers, the following summary string would"
         " count the number of actual elements stored in an std::list:\n"
         "type summary add -s \"${svar%#}\" -x \"std::list<\"";
}

llvm::StringRef ExprPathHelpTextCallback() {
  return "An expression path is the sequence of symbols that is used in C/C++ "
         "to access a member variable of an aggregate object (class).\n"
         "For instance, given a class:\n"
         "  class foo {\n"
         "      int a;\n"
         "      int b; .\n"
         "      foo* next;\n"
         "  };\n"
         "the expression to read item b in the item pointed to by next for foo "
         "aFoo would be aFoo.next->b.\n"
         "Given that aFoo could just be any object of type foo, the string "
         "'.next->b' is the expression path, because it can be attached to any "
         "foo instance to achieve the effect.\n"
         "Expression paths in LLDB include dot (.) and arrow (->) operators, "
         "and most commands using expression paths have ways to also accept "
         "the star (*) operator.\n"
         "The meaning of these operators is the same as the usual one given to "
         "them by the C/C++ standards.\n"
         "LLDB also has support for indexing ([ ]) in expression paths, and "
         "extends the traditional meaning of the square brackets operator to "
         "allow bitfield extraction:\n"
         "for objects of native types (int, float, char, ...) saying '[n-m]' "
         "as an expression path (where n and m are any positive integers, e.g. "
         "[3-5]) causes LLDB to extract"
         " bits n thru m from the value of the variable. If n == m, [n] is "
         "also allowed as a shortcut syntax. For arrays and pointers, "
         "expression paths can only contain one index"
         " and the meaning of the operation is the same as the one defined by "
         "C/C++ (item extraction). Some commands extend bitfield-like syntax "
         "for arrays and pointers with the"
         " meaning of array slicing (taking elements n thru m inside the array "
         "or pointed-to memory).";
}

llvm::StringRef arch_helper() {
  static StreamString g_archs_help;
  if (g_archs_help.Empty()) {
    StringList archs;

    ArchSpec::ListSupportedArchNames(archs);
    g_archs_help.Printf("These are the supported architecture names:\n");
    archs.Join("\n", g_archs_help);
  }
  return g_archs_help.GetString();
}

template <int I> struct TableValidator : TableValidator<I + 1> {
  static_assert(
      g_argument_table[I].arg_type == I,
      "g_argument_table order doesn't match CommandArgumentType enumeration");
};

template <> struct TableValidator<eArgTypeLastArg> {};

TableValidator<0> validator;

} // namespace lldb_private
