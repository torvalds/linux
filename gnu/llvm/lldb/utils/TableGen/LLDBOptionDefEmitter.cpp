//===- LLDBOptionDefEmitter.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emits LLDB's OptionDefinition values for different
// LLDB commands.
//
//===----------------------------------------------------------------------===//

#include "LLDBTableGenBackends.h"
#include "LLDBTableGenUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>

using namespace llvm;
using namespace lldb_private;

namespace {
struct CommandOption {
  std::vector<std::string> GroupsArg;
  bool Required = false;
  std::string FullName;
  std::string ShortName;
  std::string ArgType;
  bool OptionalArg = false;
  std::string Validator;
  std::vector<StringRef> Completions;
  std::string Description;

  CommandOption() = default;
  CommandOption(Record *Option) {
    if (Option->getValue("Groups")) {
      // The user specified a list of groups.
      auto Groups = Option->getValueAsListOfInts("Groups");
      for (int Group : Groups)
        GroupsArg.push_back("LLDB_OPT_SET_" + std::to_string(Group));
    } else if (Option->getValue("GroupStart")) {
      // The user specified a range of groups (with potentially only one
      // element).
      int GroupStart = Option->getValueAsInt("GroupStart");
      int GroupEnd = Option->getValueAsInt("GroupEnd");
      for (int i = GroupStart; i <= GroupEnd; ++i)
        GroupsArg.push_back("LLDB_OPT_SET_" + std::to_string(i));
    }

    // Check if this option is required.
    Required = Option->getValue("Required");

    // Add the full and short name for this option.
    FullName = std::string(Option->getValueAsString("FullName"));
    ShortName = std::string(Option->getValueAsString("ShortName"));

    if (auto A = Option->getValue("ArgType"))
      ArgType = A->getValue()->getAsUnquotedString();
    OptionalArg = Option->getValue("OptionalArg") != nullptr;

    if (Option->getValue("Validator"))
      Validator = std::string(Option->getValueAsString("Validator"));

    if (Option->getValue("Completions"))
      Completions = Option->getValueAsListOfStrings("Completions");

    if (auto D = Option->getValue("Description"))
      Description = D->getValue()->getAsUnquotedString();
  }
};
} // namespace

static void emitOption(const CommandOption &O, raw_ostream &OS) {
  OS << "  {";

  // If we have any groups, we merge them. Otherwise we move this option into
  // the all group.
  if (O.GroupsArg.empty())
    OS << "LLDB_OPT_SET_ALL";
  else
    OS << llvm::join(O.GroupsArg.begin(), O.GroupsArg.end(), " | ");

  OS << ", ";

  // Check if this option is required.
  OS << (O.Required ? "true" : "false");

  // Add the full and short name for this option.
  OS << ", \"" << O.FullName << "\", ";
  OS << '\'' << O.ShortName << "'";

  // Decide if we have either an option, required or no argument for this
  // option.
  OS << ", OptionParser::";
  if (!O.ArgType.empty()) {
    if (O.OptionalArg)
      OS << "eOptionalArgument";
    else
      OS << "eRequiredArgument";
  } else
    OS << "eNoArgument";
  OS << ", ";

  if (!O.Validator.empty())
    OS << O.Validator;
  else
    OS << "nullptr";
  OS << ", ";

  if (!O.ArgType.empty())
    OS << "g_argument_table[eArgType" << O.ArgType << "].enum_values";
  else
    OS << "{}";
  OS << ", ";

  // Read the tab completions we offer for this option (if there are any)
  if (!O.Completions.empty()) {
    std::vector<std::string> CompletionArgs;
    for (llvm::StringRef Completion : O.Completions)
      CompletionArgs.push_back("e" + Completion.str() + "Completion");

    OS << llvm::join(CompletionArgs.begin(), CompletionArgs.end(), " | ");
  } else
    OS << "CompletionType::eNoCompletion";

  // Add the argument type.
  OS << ", eArgType";
  if (!O.ArgType.empty()) {
    OS << O.ArgType;
  } else
    OS << "None";
  OS << ", ";

  // Add the description if there is any.
  if (!O.Description.empty()) {
    OS << "\"";
    llvm::printEscapedString(O.Description, OS);
    OS << "\"";
  } else
    OS << "\"\"";
  OS << "},\n";
}

/// Emits all option initializers to the raw_ostream.
static void emitOptions(std::string Command, std::vector<Record *> Records,
                        raw_ostream &OS) {
  std::vector<CommandOption> Options;
  for (Record *R : Records)
    Options.emplace_back(R);

  std::string ID = Command;
  std::replace(ID.begin(), ID.end(), ' ', '_');
  // Generate the macro that the user needs to define before including the
  // *.inc file.
  std::string NeededMacro = "LLDB_OPTIONS_" + ID;

  // All options are in one file, so we need put them behind macros and ask the
  // user to define the macro for the options that are needed.
  OS << "// Options for " << Command << "\n";
  OS << "#ifdef " << NeededMacro << "\n";
  OS << "constexpr static OptionDefinition g_" + ID + "_options[] = {\n";
  for (CommandOption &CO : Options)
    emitOption(CO, OS);
  // We undefine the macro for the user like Clang's include files are doing it.
  OS << "};\n";
  OS << "#undef " << NeededMacro << "\n";
  OS << "#endif // " << Command << " command\n\n";
}

void lldb_private::EmitOptionDefs(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Options for LLDB command line commands.", OS, Records);

  std::vector<Record *> Options = Records.getAllDerivedDefinitions("Option");
  for (auto &CommandRecordPair : getRecordsByName(Options, "Command")) {
    emitOptions(CommandRecordPair.first, CommandRecordPair.second, OS);
  }
}
