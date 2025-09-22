//===-- Options.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONS_H
#define LLDB_INTERPRETER_OPTIONS_H

#include <set>
#include <vector>

#include "lldb/Utility/Args.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/OptionDefinition.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace lldb_private {

struct Option;

typedef std::vector<std::tuple<std::string, int, std::string>> OptionArgVector;
typedef std::shared_ptr<OptionArgVector> OptionArgVectorSP;

struct OptionArgElement {
  enum { eUnrecognizedArg = -1, eBareDash = -2, eBareDoubleDash = -3 };

  OptionArgElement(int defs_index, int pos, int arg_pos)
      : opt_defs_index(defs_index), opt_pos(pos), opt_arg_pos(arg_pos) {}

  int opt_defs_index;
  int opt_pos;
  int opt_arg_pos;
};

typedef std::vector<OptionArgElement> OptionElementVector;

/// \class Options Options.h "lldb/Interpreter/Options.h"
/// A command line option parsing protocol class.
///
/// Options is designed to be subclassed to contain all needed options for a
/// given command. The options can be parsed by calling the Parse function.
///
/// The options are specified using the format defined for the libc options
/// parsing function getopt_long_only: \code
///     #include <getopt.h>
///     int getopt_long_only(int argc, char * const *argv, const char
///     *optstring, const struct option *longopts, int *longindex);
/// \endcode
///
class Options {
public:
  Options();

  virtual ~Options();

  void BuildGetoptTable();

  void BuildValidOptionSets();

  uint32_t NumCommandOptions();

  /// Get the option definitions to use when parsing Args options.
  ///
  /// \see Args::ParseOptions (Options&)
  /// \see man getopt_long_only
  Option *GetLongOptions();

  // This gets passed the short option as an integer...
  void OptionSeen(int short_option);

  bool VerifyOptions(CommandReturnObject &result);

  // Verify that the options given are in the options table and can be used
  // together, but there may be some required options that are missing (used to
  // verify options that get folded into command aliases).
  bool VerifyPartialOptions(CommandReturnObject &result);

  void OutputFormattedUsageText(Stream &strm,
                                const OptionDefinition &option_def,
                                uint32_t output_max_columns);

  void GenerateOptionUsage(Stream &strm, CommandObject &cmd,
                           uint32_t screen_width);

  bool SupportsLongOption(const char *long_option);

  // The following two pure virtual functions must be defined by every class
  // that inherits from this class.

  virtual llvm::ArrayRef<OptionDefinition> GetDefinitions() {
    return llvm::ArrayRef<OptionDefinition>();
  }

  // Call this prior to parsing any options. This call will call the subclass
  // OptionParsingStarting() and will avoid the need for all
  // OptionParsingStarting() function instances from having to call the
  // Option::OptionParsingStarting() like they did before. This was error prone
  // and subclasses shouldn't have to do it.
  void NotifyOptionParsingStarting(ExecutionContext *execution_context);

  /// Parse the provided arguments.
  ///
  /// The parsed options are set via calls to SetOptionValue. In case of a
  /// successful parse, the function returns a copy of the input arguments
  /// with the parsed options removed. Otherwise, it returns an error.
  ///
  /// param[in] platform_sp
  ///   The platform used for option validation.  This is necessary
  ///   because an empty execution_context is not enough to get us
  ///   to a reasonable platform.  If the platform isn't given,
  ///   we'll try to get it from the execution context.  If we can't
  ///   get it from the execution context, we'll skip validation.
  ///
  /// param[in] require_validation
  ///   When true, it will fail option parsing if validation could
  ///   not occur due to not having a platform.
  llvm::Expected<Args> Parse(const Args &args,
                             ExecutionContext *execution_context,
                             lldb::PlatformSP platform_sp,
                             bool require_validation);

  llvm::Expected<Args> ParseAlias(const Args &args,
                                  OptionArgVector *option_arg_vector,
                                  std::string &input_line);

  OptionElementVector ParseForCompletion(const Args &args,
                                         uint32_t cursor_index);

  Status NotifyOptionParsingFinished(ExecutionContext *execution_context);

  /// Set the value of an option.
  ///
  /// \param[in] option_idx
  ///     The index into the "struct option" array that was returned
  ///     by Options::GetLongOptions().
  ///
  /// \param[in] option_arg
  ///     The argument value for the option that the user entered, or
  ///     nullptr if there is no argument for the current option.
  ///
  /// \param[in] execution_context
  ///     The execution context to use for evaluating the option.
  ///     May be nullptr if the option is to be evaluated outside any
  ///     particular context.
  ///
  /// \see Args::ParseOptions (Options&)
  /// \see man getopt_long_only
  virtual Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                                ExecutionContext *execution_context) = 0;

  /// Handles the generic bits of figuring out whether we are in an option,
  /// and if so completing it.
  ///
  /// \param[in,out] request
  ///    The completion request that we need to act upon.
  ///
  /// \param[in] interpreter
  ///     The interpreter that's doing the completing.
  ///
  /// FIXME: This is the wrong return value, since we also need to
  /// make a distinction between total number of matches, and the window the
  /// user wants returned.
  ///
  /// \return
  ///     \b true if we were in an option, \b false otherwise.
  bool HandleOptionCompletion(lldb_private::CompletionRequest &request,
                              OptionElementVector &option_map,
                              CommandInterpreter &interpreter);

  /// Handles the generic bits of figuring out whether we are in an option,
  /// and if so completing it.
  ///
  /// \param[in,out] request
  ///    The completion request that we need to act upon.
  ///
  /// \param[in] interpreter
  ///    The command interpreter doing the completion.
  virtual void
  HandleOptionArgumentCompletion(lldb_private::CompletionRequest &request,
                                 OptionElementVector &opt_element_vector,
                                 int opt_element_index,
                                 CommandInterpreter &interpreter);

protected:
  // This is a set of options expressed as indexes into the options table for
  // this Option.
  typedef std::set<int> OptionSet;
  typedef std::vector<OptionSet> OptionSetVector;

  std::vector<Option> m_getopt_table;
  OptionSet m_seen_options;
  OptionSetVector m_required_options;
  OptionSetVector m_optional_options;

  OptionSetVector &GetRequiredOptions() {
    BuildValidOptionSets();
    return m_required_options;
  }

  OptionSetVector &GetOptionalOptions() {
    BuildValidOptionSets();
    return m_optional_options;
  }

  bool IsASubset(const OptionSet &set_a, const OptionSet &set_b);

  size_t OptionsSetDiff(const OptionSet &set_a, const OptionSet &set_b,
                        OptionSet &diffs);

  void OptionsSetUnion(const OptionSet &set_a, const OptionSet &set_b,
                       OptionSet &union_set);

  // Subclasses must reset their option values prior to starting a new option
  // parse. Each subclass must override this function and revert all option
  // settings to default values.
  virtual void OptionParsingStarting(ExecutionContext *execution_context) = 0;

  virtual Status OptionParsingFinished(ExecutionContext *execution_context) {
    // If subclasses need to know when the options are done being parsed they
    // can implement this function to do extra checking
    Status error;
    return error;
  }
};

class OptionGroup {
public:
  OptionGroup() = default;

  virtual ~OptionGroup() = default;

  virtual llvm::ArrayRef<OptionDefinition> GetDefinitions() = 0;

  virtual Status SetOptionValue(uint32_t option_idx,
                                llvm::StringRef option_value,
                                ExecutionContext *execution_context) = 0;

  virtual void OptionParsingStarting(ExecutionContext *execution_context) = 0;

  virtual Status OptionParsingFinished(ExecutionContext *execution_context) {
    // If subclasses need to know when the options are done being parsed they
    // can implement this function to do extra checking
    Status error;
    return error;
  }
};

class OptionGroupOptions : public Options {
public:
  OptionGroupOptions() = default;

  ~OptionGroupOptions() override = default;

  /// Append options from a OptionGroup class.
  ///
  /// Append all options from \a group using the exact same option groups that
  /// each option is defined with.
  ///
  /// \param[in] group
  ///     A group of options to take option values from and copy their
  ///     definitions into this class.
  void Append(OptionGroup *group);

  /// Append options from a OptionGroup class.
  ///
  /// Append options from \a group that have a usage mask that has any bits in
  /// "src_mask" set. After the option definition is copied into the options
  /// definitions in this class, set the usage_mask to "dst_mask".
  ///
  /// \param[in] group
  ///     A group of options to take option values from and copy their
  ///     definitions into this class.
  ///
  /// \param[in] src_mask
  ///     When copying options from \a group, you might only want some of
  ///     the options to be appended to this group. This mask allows you
  ///     to control which options from \a group get added. It also allows
  ///     you to specify the same options from \a group multiple times
  ///     for different option sets.
  ///
  /// \param[in] dst_mask
  ///     Set the usage mask for any copied options to \a dst_mask after
  ///     copying the option definition.
  void Append(OptionGroup *group, uint32_t src_mask, uint32_t dst_mask);

  /// Append selected options from a OptionGroup class.
  ///
  /// Append the subset of options from \a group, where the "long_option" value
  /// is _not_ in \a exclude_long_options.
  ///
  /// \param[in] group
  ///     A group of options to take option values from and copy their
  ///     definitions into this class.
  ///
  /// \param[in] exclude_long_options
  ///     A set of long option strings which indicate which option values values
  ///     to limit from \a group.
  void Append(OptionGroup *group,
              llvm::ArrayRef<llvm::StringRef> exclude_long_options);

  void Finalize();

  bool DidFinalize() { return m_did_finalize; }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  Status OptionParsingFinished(ExecutionContext *execution_context) override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    assert(m_did_finalize);
    return m_option_defs;
  }

  const OptionGroup *GetGroupWithOption(char short_opt);

  struct OptionInfo {
    OptionInfo(OptionGroup *g, uint32_t i) : option_group(g), option_index(i) {}
    OptionGroup *option_group; // The group that this option came from
    uint32_t option_index;     // The original option index from the OptionGroup
  };
  typedef std::vector<OptionInfo> OptionInfos;

  std::vector<OptionDefinition> m_option_defs;
  OptionInfos m_option_infos;
  bool m_did_finalize = false;
};

/// Creates an error that represents the failure to parse an command line option
/// argument. This creates an error containing all information needed to show
/// the developer what went wrong when parsing their command. It is recommended
/// to use this instead of writing an error by hand.
///
/// \param[in] option_arg
///   The argument that was attempted to be parsed.
///
/// \param[in] short_option
///   The short form of the option. For example, if the flag is -f, the short
///   option is "f".
///
/// \param[in] long_option
///   The long form of the option. This field is optional. If the flag is
///   --force, then the long option is "force".
///
/// \param[in] additional_context
///   This is extra context that will get included in the error. This field is
///   optional.
///
/// \return
///   An llvm::Error that contains a standardized format for what went wrong
///   when parsing and why.
llvm::Error CreateOptionParsingError(llvm::StringRef option_arg,
                                     const char short_option,
                                     llvm::StringRef long_option = {},
                                     llvm::StringRef additional_context = {});

static constexpr llvm::StringLiteral g_bool_parsing_error_message =
    "Failed to parse as boolean";
static constexpr llvm::StringLiteral g_int_parsing_error_message =
    "Failed to parse as integer";
static constexpr llvm::StringLiteral g_language_parsing_error_message =
    "Unknown language";

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONS_H
