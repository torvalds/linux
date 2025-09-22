//===- llvm/Support/CommandLine.h - Command line handler --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements a command line argument processor that is useful when
// creating a tool.  It provides a simple, minimalistic interface that is easily
// extensible and supports nonlocal (library) command line options.
//
// Note that rather than trying to figure out what this code does, you should
// read the library documentation located in docs/CommandLine.html or looks at
// the many example usages in tools/*/*.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMMANDLINE_H
#define LLVM_SUPPORT_COMMANDLINE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <climits>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {

namespace vfs {
class FileSystem;
}

class StringSaver;

/// This namespace contains all of the command line option processing machinery.
/// It is intentionally a short name to make qualified usage concise.
namespace cl {

//===----------------------------------------------------------------------===//
// Command line option processing entry point.
//
// Returns true on success. Otherwise, this will print the error message to
// stderr and exit if \p Errs is not set (nullptr by default), or print the
// error message to \p Errs and return false if \p Errs is provided.
//
// If EnvVar is not nullptr, command-line options are also parsed from the
// environment variable named by EnvVar.  Precedence is given to occurrences
// from argv.  This precedence is currently implemented by parsing argv after
// the environment variable, so it is only implemented correctly for options
// that give precedence to later occurrences.  If your program supports options
// that give precedence to earlier occurrences, you will need to extend this
// function to support it correctly.
bool ParseCommandLineOptions(int argc, const char *const *argv,
                             StringRef Overview = "",
                             raw_ostream *Errs = nullptr,
                             const char *EnvVar = nullptr,
                             bool LongOptionsUseDoubleDash = false);

// Function pointer type for printing version information.
using VersionPrinterTy = std::function<void(raw_ostream &)>;

///===---------------------------------------------------------------------===//
/// Override the default (LLVM specific) version printer used to print out the
/// version when --version is given on the command line. This allows other
/// systems using the CommandLine utilities to print their own version string.
void SetVersionPrinter(VersionPrinterTy func);

///===---------------------------------------------------------------------===//
/// Add an extra printer to use in addition to the default one. This can be
/// called multiple times, and each time it adds a new function to the list
/// which will be called after the basic LLVM version printing is complete.
/// Each can then add additional information specific to the tool.
void AddExtraVersionPrinter(VersionPrinterTy func);

// Print option values.
// With -print-options print the difference between option values and defaults.
// With -print-all-options print all option values.
// (Currently not perfect, but best-effort.)
void PrintOptionValues();

// Forward declaration - AddLiteralOption needs to be up here to make gcc happy.
class Option;

/// Adds a new option for parsing and provides the option it refers to.
///
/// \param O pointer to the option
/// \param Name the string name for the option to handle during parsing
///
/// Literal options are used by some parsers to register special option values.
/// This is how the PassNameParser registers pass names for opt.
void AddLiteralOption(Option &O, StringRef Name);

//===----------------------------------------------------------------------===//
// Flags permitted to be passed to command line arguments
//

enum NumOccurrencesFlag { // Flags for the number of occurrences allowed
  Optional = 0x00,        // Zero or One occurrence
  ZeroOrMore = 0x01,      // Zero or more occurrences allowed
  Required = 0x02,        // One occurrence required
  OneOrMore = 0x03,       // One or more occurrences required

  // Indicates that this option is fed anything that follows the last positional
  // argument required by the application (it is an error if there are zero
  // positional arguments, and a ConsumeAfter option is used).
  // Thus, for example, all arguments to LLI are processed until a filename is
  // found.  Once a filename is found, all of the succeeding arguments are
  // passed, unprocessed, to the ConsumeAfter option.
  //
  ConsumeAfter = 0x04
};

enum ValueExpected { // Is a value required for the option?
  // zero reserved for the unspecified value
  ValueOptional = 0x01,  // The value can appear... or not
  ValueRequired = 0x02,  // The value is required to appear!
  ValueDisallowed = 0x03 // A value may not be specified (for flags)
};

enum OptionHidden {   // Control whether -help shows this option
  NotHidden = 0x00,   // Option included in -help & -help-hidden
  Hidden = 0x01,      // -help doesn't, but -help-hidden does
  ReallyHidden = 0x02 // Neither -help nor -help-hidden show this arg
};

// This controls special features that the option might have that cause it to be
// parsed differently...
//
// Prefix - This option allows arguments that are otherwise unrecognized to be
// matched by options that are a prefix of the actual value.  This is useful for
// cases like a linker, where options are typically of the form '-lfoo' or
// '-L../../include' where -l or -L are the actual flags.  When prefix is
// enabled, and used, the value for the flag comes from the suffix of the
// argument.
//
// AlwaysPrefix - Only allow the behavior enabled by the Prefix flag and reject
// the Option=Value form.
//

enum FormattingFlags {
  NormalFormatting = 0x00, // Nothing special
  Positional = 0x01,       // Is a positional argument, no '-' required
  Prefix = 0x02,           // Can this option directly prefix its value?
  AlwaysPrefix = 0x03      // Can this option only directly prefix its value?
};

enum MiscFlags {             // Miscellaneous flags to adjust argument
  CommaSeparated = 0x01,     // Should this cl::list split between commas?
  PositionalEatsArgs = 0x02, // Should this positional cl::list eat -args?
  Sink = 0x04,               // Should this cl::list eat all unknown options?

  // Can this option group with other options?
  // If this is enabled, multiple letter options are allowed to bunch together
  // with only a single hyphen for the whole group.  This allows emulation
  // of the behavior that ls uses for example: ls -la === ls -l -a
  Grouping = 0x08,

  // Default option
  DefaultOption = 0x10
};

//===----------------------------------------------------------------------===//
//
class OptionCategory {
private:
  StringRef const Name;
  StringRef const Description;

  void registerCategory();

public:
  OptionCategory(StringRef const Name,
                 StringRef const Description = "")
      : Name(Name), Description(Description) {
    registerCategory();
  }

  StringRef getName() const { return Name; }
  StringRef getDescription() const { return Description; }
};

// The general Option Category (used as default category).
OptionCategory &getGeneralCategory();

//===----------------------------------------------------------------------===//
//
class SubCommand {
private:
  StringRef Name;
  StringRef Description;

protected:
  void registerSubCommand();
  void unregisterSubCommand();

public:
  SubCommand(StringRef Name, StringRef Description = "")
      : Name(Name), Description(Description) {
        registerSubCommand();
  }
  SubCommand() = default;

  // Get the special subcommand representing no subcommand.
  static SubCommand &getTopLevel();

  // Get the special subcommand that can be used to put an option into all
  // subcommands.
  static SubCommand &getAll();

  void reset();

  explicit operator bool() const;

  StringRef getName() const { return Name; }
  StringRef getDescription() const { return Description; }

  SmallVector<Option *, 4> PositionalOpts;
  SmallVector<Option *, 4> SinkOpts;
  StringMap<Option *> OptionsMap;

  Option *ConsumeAfterOpt = nullptr; // The ConsumeAfter option if it exists.
};

class SubCommandGroup {
  SmallVector<SubCommand *, 4> Subs;

public:
  SubCommandGroup(std::initializer_list<SubCommand *> IL) : Subs(IL) {}

  ArrayRef<SubCommand *> getSubCommands() const { return Subs; }
};

//===----------------------------------------------------------------------===//
//
class Option {
  friend class alias;

  // Overriden by subclasses to handle the value passed into an argument. Should
  // return true if there was an error processing the argument and the program
  // should exit.
  //
  virtual bool handleOccurrence(unsigned pos, StringRef ArgName,
                                StringRef Arg) = 0;

  virtual enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  // Out of line virtual function to provide home for the class.
  virtual void anchor();

  uint16_t NumOccurrences; // The number of times specified
  // Occurrences, HiddenFlag, and Formatting are all enum types but to avoid
  // problems with signed enums in bitfields.
  uint16_t Occurrences : 3; // enum NumOccurrencesFlag
  // not using the enum type for 'Value' because zero is an implementation
  // detail representing the non-value
  uint16_t Value : 2;
  uint16_t HiddenFlag : 2; // enum OptionHidden
  uint16_t Formatting : 2; // enum FormattingFlags
  uint16_t Misc : 5;
  uint16_t FullyInitialized : 1; // Has addArgument been called?
  uint16_t Position;             // Position of last occurrence of the option
  uint16_t AdditionalVals;       // Greater than 0 for multi-valued option.

public:
  StringRef ArgStr;   // The argument string itself (ex: "help", "o")
  StringRef HelpStr;  // The descriptive text message for -help
  StringRef ValueStr; // String describing what the value of this option is
  SmallVector<OptionCategory *, 1>
      Categories;                    // The Categories this option belongs to
  SmallPtrSet<SubCommand *, 1> Subs; // The subcommands this option belongs to.

  inline enum NumOccurrencesFlag getNumOccurrencesFlag() const {
    return (enum NumOccurrencesFlag)Occurrences;
  }

  inline enum ValueExpected getValueExpectedFlag() const {
    return Value ? ((enum ValueExpected)Value) : getValueExpectedFlagDefault();
  }

  inline enum OptionHidden getOptionHiddenFlag() const {
    return (enum OptionHidden)HiddenFlag;
  }

  inline enum FormattingFlags getFormattingFlag() const {
    return (enum FormattingFlags)Formatting;
  }

  inline unsigned getMiscFlags() const { return Misc; }
  inline unsigned getPosition() const { return Position; }
  inline unsigned getNumAdditionalVals() const { return AdditionalVals; }

  // Return true if the argstr != ""
  bool hasArgStr() const { return !ArgStr.empty(); }
  bool isPositional() const { return getFormattingFlag() == cl::Positional; }
  bool isSink() const { return getMiscFlags() & cl::Sink; }
  bool isDefaultOption() const { return getMiscFlags() & cl::DefaultOption; }

  bool isConsumeAfter() const {
    return getNumOccurrencesFlag() == cl::ConsumeAfter;
  }

  //-------------------------------------------------------------------------===
  // Accessor functions set by OptionModifiers
  //
  void setArgStr(StringRef S);
  void setDescription(StringRef S) { HelpStr = S; }
  void setValueStr(StringRef S) { ValueStr = S; }
  void setNumOccurrencesFlag(enum NumOccurrencesFlag Val) { Occurrences = Val; }
  void setValueExpectedFlag(enum ValueExpected Val) { Value = Val; }
  void setHiddenFlag(enum OptionHidden Val) { HiddenFlag = Val; }
  void setFormattingFlag(enum FormattingFlags V) { Formatting = V; }
  void setMiscFlag(enum MiscFlags M) { Misc |= M; }
  void setPosition(unsigned pos) { Position = pos; }
  void addCategory(OptionCategory &C);
  void addSubCommand(SubCommand &S) { Subs.insert(&S); }

protected:
  explicit Option(enum NumOccurrencesFlag OccurrencesFlag,
                  enum OptionHidden Hidden)
      : NumOccurrences(0), Occurrences(OccurrencesFlag), Value(0),
        HiddenFlag(Hidden), Formatting(NormalFormatting), Misc(0),
        FullyInitialized(false), Position(0), AdditionalVals(0) {
    Categories.push_back(&getGeneralCategory());
  }

  inline void setNumAdditionalVals(unsigned n) { AdditionalVals = n; }

public:
  virtual ~Option() = default;

  // Register this argument with the commandline system.
  //
  void addArgument();

  /// Unregisters this option from the CommandLine system.
  ///
  /// This option must have been the last option registered.
  /// For testing purposes only.
  void removeArgument();

  // Return the width of the option tag for printing...
  virtual size_t getOptionWidth() const = 0;

  // Print out information about this option. The to-be-maintained width is
  // specified.
  //
  virtual void printOptionInfo(size_t GlobalWidth) const = 0;

  virtual void printOptionValue(size_t GlobalWidth, bool Force) const = 0;

  virtual void setDefault() = 0;

  // Prints the help string for an option.
  //
  // This maintains the Indent for multi-line descriptions.
  // FirstLineIndentedBy is the count of chars of the first line
  //      i.e. the one containing the --<option name>.
  static void printHelpStr(StringRef HelpStr, size_t Indent,
                           size_t FirstLineIndentedBy);

  // Prints the help string for an enum value.
  //
  // This maintains the Indent for multi-line descriptions.
  // FirstLineIndentedBy is the count of chars of the first line
  //      i.e. the one containing the =<value>.
  static void printEnumValHelpStr(StringRef HelpStr, size_t Indent,
                                  size_t FirstLineIndentedBy);

  virtual void getExtraOptionNames(SmallVectorImpl<StringRef> &) {}

  // Wrapper around handleOccurrence that enforces Flags.
  //
  virtual bool addOccurrence(unsigned pos, StringRef ArgName, StringRef Value,
                             bool MultiArg = false);

  // Prints option name followed by message.  Always returns true.
  bool error(const Twine &Message, StringRef ArgName = StringRef(), raw_ostream &Errs = llvm::errs());
  bool error(const Twine &Message, raw_ostream &Errs) {
    return error(Message, StringRef(), Errs);
  }

  inline int getNumOccurrences() const { return NumOccurrences; }
  void reset();
};

//===----------------------------------------------------------------------===//
// Command line option modifiers that can be used to modify the behavior of
// command line option parsers...
//

// Modifier to set the description shown in the -help output...
struct desc {
  StringRef Desc;

  desc(StringRef Str) : Desc(Str) {}

  void apply(Option &O) const { O.setDescription(Desc); }
};

// Modifier to set the value description shown in the -help output...
struct value_desc {
  StringRef Desc;

  value_desc(StringRef Str) : Desc(Str) {}

  void apply(Option &O) const { O.setValueStr(Desc); }
};

// Specify a default (initial) value for the command line argument, if the
// default constructor for the argument type does not give you what you want.
// This is only valid on "opt" arguments, not on "list" arguments.
template <class Ty> struct initializer {
  const Ty &Init;
  initializer(const Ty &Val) : Init(Val) {}

  template <class Opt> void apply(Opt &O) const { O.setInitialValue(Init); }
};

template <class Ty> struct list_initializer {
  ArrayRef<Ty> Inits;
  list_initializer(ArrayRef<Ty> Vals) : Inits(Vals) {}

  template <class Opt> void apply(Opt &O) const { O.setInitialValues(Inits); }
};

template <class Ty> initializer<Ty> init(const Ty &Val) {
  return initializer<Ty>(Val);
}

template <class Ty>
list_initializer<Ty> list_init(ArrayRef<Ty> Vals) {
  return list_initializer<Ty>(Vals);
}

// Allow the user to specify which external variable they want to store the
// results of the command line argument processing into, if they don't want to
// store it in the option itself.
template <class Ty> struct LocationClass {
  Ty &Loc;

  LocationClass(Ty &L) : Loc(L) {}

  template <class Opt> void apply(Opt &O) const { O.setLocation(O, Loc); }
};

template <class Ty> LocationClass<Ty> location(Ty &L) {
  return LocationClass<Ty>(L);
}

// Specify the Option category for the command line argument to belong to.
struct cat {
  OptionCategory &Category;

  cat(OptionCategory &c) : Category(c) {}

  template <class Opt> void apply(Opt &O) const { O.addCategory(Category); }
};

// Specify the subcommand that this option belongs to.
struct sub {
  SubCommand *Sub = nullptr;
  SubCommandGroup *Group = nullptr;

  sub(SubCommand &S) : Sub(&S) {}
  sub(SubCommandGroup &G) : Group(&G) {}

  template <class Opt> void apply(Opt &O) const {
    if (Sub)
      O.addSubCommand(*Sub);
    else if (Group)
      for (SubCommand *SC : Group->getSubCommands())
        O.addSubCommand(*SC);
  }
};

// Specify a callback function to be called when an option is seen.
// Can be used to set other options automatically.
template <typename R, typename Ty> struct cb {
  std::function<R(Ty)> CB;

  cb(std::function<R(Ty)> CB) : CB(CB) {}

  template <typename Opt> void apply(Opt &O) const { O.setCallback(CB); }
};

namespace detail {
template <typename F>
struct callback_traits : public callback_traits<decltype(&F::operator())> {};

template <typename R, typename C, typename... Args>
struct callback_traits<R (C::*)(Args...) const> {
  using result_type = R;
  using arg_type = std::tuple_element_t<0, std::tuple<Args...>>;
  static_assert(sizeof...(Args) == 1, "callback function must have one and only one parameter");
  static_assert(std::is_same_v<result_type, void>,
                "callback return type must be void");
  static_assert(std::is_lvalue_reference_v<arg_type> &&
                    std::is_const_v<std::remove_reference_t<arg_type>>,
                "callback arg_type must be a const lvalue reference");
};
} // namespace detail

template <typename F>
cb<typename detail::callback_traits<F>::result_type,
   typename detail::callback_traits<F>::arg_type>
callback(F CB) {
  using result_type = typename detail::callback_traits<F>::result_type;
  using arg_type = typename detail::callback_traits<F>::arg_type;
  return cb<result_type, arg_type>(CB);
}

//===----------------------------------------------------------------------===//

// Support value comparison outside the template.
struct GenericOptionValue {
  virtual bool compare(const GenericOptionValue &V) const = 0;

protected:
  GenericOptionValue() = default;
  GenericOptionValue(const GenericOptionValue&) = default;
  GenericOptionValue &operator=(const GenericOptionValue &) = default;
  ~GenericOptionValue() = default;

private:
  virtual void anchor();
};

template <class DataType> struct OptionValue;

// The default value safely does nothing. Option value printing is only
// best-effort.
template <class DataType, bool isClass>
struct OptionValueBase : public GenericOptionValue {
  // Temporary storage for argument passing.
  using WrapperType = OptionValue<DataType>;

  bool hasValue() const { return false; }

  const DataType &getValue() const { llvm_unreachable("no default value"); }

  // Some options may take their value from a different data type.
  template <class DT> void setValue(const DT & /*V*/) {}

  // Returns whether this instance matches the argument.
  bool compare(const DataType & /*V*/) const { return false; }

  bool compare(const GenericOptionValue & /*V*/) const override {
    return false;
  }

protected:
  ~OptionValueBase() = default;
};

// Simple copy of the option value.
template <class DataType> class OptionValueCopy : public GenericOptionValue {
  DataType Value;
  bool Valid = false;

protected:
  OptionValueCopy(const OptionValueCopy&) = default;
  OptionValueCopy &operator=(const OptionValueCopy &) = default;
  ~OptionValueCopy() = default;

public:
  OptionValueCopy() = default;

  bool hasValue() const { return Valid; }

  const DataType &getValue() const {
    assert(Valid && "invalid option value");
    return Value;
  }

  void setValue(const DataType &V) {
    Valid = true;
    Value = V;
  }

  // Returns whether this instance matches V.
  bool compare(const DataType &V) const { return Valid && (Value == V); }

  bool compare(const GenericOptionValue &V) const override {
    const OptionValueCopy<DataType> &VC =
        static_cast<const OptionValueCopy<DataType> &>(V);
    if (!VC.hasValue())
      return false;
    return compare(VC.getValue());
  }
};

// Non-class option values.
template <class DataType>
struct OptionValueBase<DataType, false> : OptionValueCopy<DataType> {
  using WrapperType = DataType;

protected:
  OptionValueBase() = default;
  OptionValueBase(const OptionValueBase&) = default;
  OptionValueBase &operator=(const OptionValueBase &) = default;
  ~OptionValueBase() = default;
};

// Top-level option class.
template <class DataType>
struct OptionValue final
    : OptionValueBase<DataType, std::is_class_v<DataType>> {
  OptionValue() = default;

  OptionValue(const DataType &V) { this->setValue(V); }

  // Some options may take their value from a different data type.
  template <class DT> OptionValue<DataType> &operator=(const DT &V) {
    this->setValue(V);
    return *this;
  }
};

// Other safe-to-copy-by-value common option types.
enum boolOrDefault { BOU_UNSET, BOU_TRUE, BOU_FALSE };
template <>
struct OptionValue<cl::boolOrDefault> final
    : OptionValueCopy<cl::boolOrDefault> {
  using WrapperType = cl::boolOrDefault;

  OptionValue() = default;

  OptionValue(const cl::boolOrDefault &V) { this->setValue(V); }

  OptionValue<cl::boolOrDefault> &operator=(const cl::boolOrDefault &V) {
    setValue(V);
    return *this;
  }

private:
  void anchor() override;
};

template <>
struct OptionValue<std::string> final : OptionValueCopy<std::string> {
  using WrapperType = StringRef;

  OptionValue() = default;

  OptionValue(const std::string &V) { this->setValue(V); }

  OptionValue<std::string> &operator=(const std::string &V) {
    setValue(V);
    return *this;
  }

private:
  void anchor() override;
};

//===----------------------------------------------------------------------===//
// Enum valued command line option
//

// This represents a single enum value, using "int" as the underlying type.
struct OptionEnumValue {
  StringRef Name;
  int Value;
  StringRef Description;
};

#define clEnumVal(ENUMVAL, DESC)                                               \
  llvm::cl::OptionEnumValue { #ENUMVAL, int(ENUMVAL), DESC }
#define clEnumValN(ENUMVAL, FLAGNAME, DESC)                                    \
  llvm::cl::OptionEnumValue { FLAGNAME, int(ENUMVAL), DESC }

// For custom data types, allow specifying a group of values together as the
// values that go into the mapping that the option handler uses.
//
class ValuesClass {
  // Use a vector instead of a map, because the lists should be short,
  // the overhead is less, and most importantly, it keeps them in the order
  // inserted so we can print our option out nicely.
  SmallVector<OptionEnumValue, 4> Values;

public:
  ValuesClass(std::initializer_list<OptionEnumValue> Options)
      : Values(Options) {}

  template <class Opt> void apply(Opt &O) const {
    for (const auto &Value : Values)
      O.getParser().addLiteralOption(Value.Name, Value.Value,
                                     Value.Description);
  }
};

/// Helper to build a ValuesClass by forwarding a variable number of arguments
/// as an initializer list to the ValuesClass constructor.
template <typename... OptsTy> ValuesClass values(OptsTy... Options) {
  return ValuesClass({Options...});
}

//===----------------------------------------------------------------------===//
// Parameterizable parser for different data types. By default, known data types
// (string, int, bool) have specialized parsers, that do what you would expect.
// The default parser, used for data types that are not built-in, uses a mapping
// table to map specific options to values, which is used, among other things,
// to handle enum types.

//--------------------------------------------------
// This class holds all the non-generic code that we do not need replicated for
// every instance of the generic parser.  This also allows us to put stuff into
// CommandLine.cpp
//
class generic_parser_base {
protected:
  class GenericOptionInfo {
  public:
    GenericOptionInfo(StringRef name, StringRef helpStr)
        : Name(name), HelpStr(helpStr) {}
    StringRef Name;
    StringRef HelpStr;
  };

public:
  generic_parser_base(Option &O) : Owner(O) {}

  virtual ~generic_parser_base() = default;
  // Base class should have virtual-destructor

  // Virtual function implemented by generic subclass to indicate how many
  // entries are in Values.
  //
  virtual unsigned getNumOptions() const = 0;

  // Return option name N.
  virtual StringRef getOption(unsigned N) const = 0;

  // Return description N
  virtual StringRef getDescription(unsigned N) const = 0;

  // Return the width of the option tag for printing...
  virtual size_t getOptionWidth(const Option &O) const;

  virtual const GenericOptionValue &getOptionValue(unsigned N) const = 0;

  // Print out information about this option. The to-be-maintained width is
  // specified.
  //
  virtual void printOptionInfo(const Option &O, size_t GlobalWidth) const;

  void printGenericOptionDiff(const Option &O, const GenericOptionValue &V,
                              const GenericOptionValue &Default,
                              size_t GlobalWidth) const;

  // Print the value of an option and it's default.
  //
  // Template definition ensures that the option and default have the same
  // DataType (via the same AnyOptionValue).
  template <class AnyOptionValue>
  void printOptionDiff(const Option &O, const AnyOptionValue &V,
                       const AnyOptionValue &Default,
                       size_t GlobalWidth) const {
    printGenericOptionDiff(O, V, Default, GlobalWidth);
  }

  void initialize() {}

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) {
    // If there has been no argstr specified, that means that we need to add an
    // argument for every possible option.  This ensures that our options are
    // vectored to us.
    if (!Owner.hasArgStr())
      for (unsigned i = 0, e = getNumOptions(); i != e; ++i)
        OptionNames.push_back(getOption(i));
  }

  enum ValueExpected getValueExpectedFlagDefault() const {
    // If there is an ArgStr specified, then we are of the form:
    //
    //    -opt=O2   or   -opt O2  or  -optO2
    //
    // In which case, the value is required.  Otherwise if an arg str has not
    // been specified, we are of the form:
    //
    //    -O2 or O2 or -la (where -l and -a are separate options)
    //
    // If this is the case, we cannot allow a value.
    //
    if (Owner.hasArgStr())
      return ValueRequired;
    else
      return ValueDisallowed;
  }

  // Return the option number corresponding to the specified
  // argument string.  If the option is not found, getNumOptions() is returned.
  //
  unsigned findOption(StringRef Name);

protected:
  Option &Owner;
};

// Default parser implementation - This implementation depends on having a
// mapping of recognized options to values of some sort.  In addition to this,
// each entry in the mapping also tracks a help message that is printed with the
// command line option for -help.  Because this is a simple mapping parser, the
// data type can be any unsupported type.
//
template <class DataType> class parser : public generic_parser_base {
protected:
  class OptionInfo : public GenericOptionInfo {
  public:
    OptionInfo(StringRef name, DataType v, StringRef helpStr)
        : GenericOptionInfo(name, helpStr), V(v) {}

    OptionValue<DataType> V;
  };
  SmallVector<OptionInfo, 8> Values;

public:
  parser(Option &O) : generic_parser_base(O) {}

  using parser_data_type = DataType;

  // Implement virtual functions needed by generic_parser_base
  unsigned getNumOptions() const override { return unsigned(Values.size()); }
  StringRef getOption(unsigned N) const override { return Values[N].Name; }
  StringRef getDescription(unsigned N) const override {
    return Values[N].HelpStr;
  }

  // Return the value of option name N.
  const GenericOptionValue &getOptionValue(unsigned N) const override {
    return Values[N].V;
  }

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, DataType &V) {
    StringRef ArgVal;
    if (Owner.hasArgStr())
      ArgVal = Arg;
    else
      ArgVal = ArgName;

    for (size_t i = 0, e = Values.size(); i != e; ++i)
      if (Values[i].Name == ArgVal) {
        V = Values[i].V.getValue();
        return false;
      }

    return O.error("Cannot find option named '" + ArgVal + "'!");
  }

  /// Add an entry to the mapping table.
  ///
  template <class DT>
  void addLiteralOption(StringRef Name, const DT &V, StringRef HelpStr) {
#ifndef NDEBUG
    if (findOption(Name) != Values.size())
      report_fatal_error("Option '" + Name + "' already exists!");
#endif
    OptionInfo X(Name, static_cast<DataType>(V), HelpStr);
    Values.push_back(X);
    AddLiteralOption(Owner, Name);
  }

  /// Remove the specified option.
  ///
  void removeLiteralOption(StringRef Name) {
    unsigned N = findOption(Name);
    assert(N != Values.size() && "Option not found!");
    Values.erase(Values.begin() + N);
  }
};

//--------------------------------------------------
// Super class of parsers to provide boilerplate code
//
class basic_parser_impl { // non-template implementation of basic_parser<t>
public:
  basic_parser_impl(Option &) {}

  virtual ~basic_parser_impl() = default;

  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueRequired;
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &) {}

  void initialize() {}

  // Return the width of the option tag for printing...
  size_t getOptionWidth(const Option &O) const;

  // Print out information about this option. The to-be-maintained width is
  // specified.
  //
  void printOptionInfo(const Option &O, size_t GlobalWidth) const;

  // Print a placeholder for options that don't yet support printOptionDiff().
  void printOptionNoValue(const Option &O, size_t GlobalWidth) const;

  // Overload in subclass to provide a better default value.
  virtual StringRef getValueName() const { return "value"; }

  // An out-of-line virtual method to provide a 'home' for this class.
  virtual void anchor();

protected:
  // A helper for basic_parser::printOptionDiff.
  void printOptionName(const Option &O, size_t GlobalWidth) const;
};

// The real basic parser is just a template wrapper that provides a typedef for
// the provided data type.
//
template <class DataType> class basic_parser : public basic_parser_impl {
public:
  using parser_data_type = DataType;
  using OptVal = OptionValue<DataType>;

  basic_parser(Option &O) : basic_parser_impl(O) {}
};

//--------------------------------------------------

extern template class basic_parser<bool>;

template <> class parser<bool> : public basic_parser<bool> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, bool &Val);

  void initialize() {}

  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  // Do not print =<value> at all.
  StringRef getValueName() const override { return StringRef(); }

  void printOptionDiff(const Option &O, bool V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<boolOrDefault>;

template <> class parser<boolOrDefault> : public basic_parser<boolOrDefault> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, boolOrDefault &Val);

  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  // Do not print =<value> at all.
  StringRef getValueName() const override { return StringRef(); }

  void printOptionDiff(const Option &O, boolOrDefault V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<int>;

template <> class parser<int> : public basic_parser<int> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, int &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "int"; }

  void printOptionDiff(const Option &O, int V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<long>;

template <> class parser<long> final : public basic_parser<long> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, long &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "long"; }

  void printOptionDiff(const Option &O, long V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<long long>;

template <> class parser<long long> : public basic_parser<long long> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, long long &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "long"; }

  void printOptionDiff(const Option &O, long long V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<unsigned>;

template <> class parser<unsigned> : public basic_parser<unsigned> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, unsigned &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "uint"; }

  void printOptionDiff(const Option &O, unsigned V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<unsigned long>;

template <>
class parser<unsigned long> final : public basic_parser<unsigned long> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, unsigned long &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "ulong"; }

  void printOptionDiff(const Option &O, unsigned long V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<unsigned long long>;

template <>
class parser<unsigned long long> : public basic_parser<unsigned long long> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg,
             unsigned long long &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "ulong"; }

  void printOptionDiff(const Option &O, unsigned long long V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<double>;

template <> class parser<double> : public basic_parser<double> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, double &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "number"; }

  void printOptionDiff(const Option &O, double V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<float>;

template <> class parser<float> : public basic_parser<float> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, float &Val);

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "number"; }

  void printOptionDiff(const Option &O, float V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<std::string>;

template <> class parser<std::string> : public basic_parser<std::string> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &, StringRef, StringRef Arg, std::string &Value) {
    Value = Arg.str();
    return false;
  }

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "string"; }

  void printOptionDiff(const Option &O, StringRef V, const OptVal &Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------

extern template class basic_parser<char>;

template <> class parser<char> : public basic_parser<char> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Return true on error.
  bool parse(Option &, StringRef, StringRef Arg, char &Value) {
    Value = Arg[0];
    return false;
  }

  // Overload in subclass to provide a better default value.
  StringRef getValueName() const override { return "char"; }

  void printOptionDiff(const Option &O, char V, OptVal Default,
                       size_t GlobalWidth) const;

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override;
};

//--------------------------------------------------
// This collection of wrappers is the intermediary between class opt and class
// parser to handle all the template nastiness.

// This overloaded function is selected by the generic parser.
template <class ParserClass, class DT>
void printOptionDiff(const Option &O, const generic_parser_base &P, const DT &V,
                     const OptionValue<DT> &Default, size_t GlobalWidth) {
  OptionValue<DT> OV = V;
  P.printOptionDiff(O, OV, Default, GlobalWidth);
}

// This is instantiated for basic parsers when the parsed value has a different
// type than the option value. e.g. HelpPrinter.
template <class ParserDT, class ValDT> struct OptionDiffPrinter {
  void print(const Option &O, const parser<ParserDT> &P, const ValDT & /*V*/,
             const OptionValue<ValDT> & /*Default*/, size_t GlobalWidth) {
    P.printOptionNoValue(O, GlobalWidth);
  }
};

// This is instantiated for basic parsers when the parsed value has the same
// type as the option value.
template <class DT> struct OptionDiffPrinter<DT, DT> {
  void print(const Option &O, const parser<DT> &P, const DT &V,
             const OptionValue<DT> &Default, size_t GlobalWidth) {
    P.printOptionDiff(O, V, Default, GlobalWidth);
  }
};

// This overloaded function is selected by the basic parser, which may parse a
// different type than the option type.
template <class ParserClass, class ValDT>
void printOptionDiff(
    const Option &O,
    const basic_parser<typename ParserClass::parser_data_type> &P,
    const ValDT &V, const OptionValue<ValDT> &Default, size_t GlobalWidth) {

  OptionDiffPrinter<typename ParserClass::parser_data_type, ValDT> printer;
  printer.print(O, static_cast<const ParserClass &>(P), V, Default,
                GlobalWidth);
}

//===----------------------------------------------------------------------===//
// This class is used because we must use partial specialization to handle
// literal string arguments specially (const char* does not correctly respond to
// the apply method). Because the syntax to use this is a pain, we have the
// 'apply' method below to handle the nastiness...
//
template <class Mod> struct applicator {
  template <class Opt> static void opt(const Mod &M, Opt &O) { M.apply(O); }
};

// Handle const char* as a special case...
template <unsigned n> struct applicator<char[n]> {
  template <class Opt> static void opt(StringRef Str, Opt &O) {
    O.setArgStr(Str);
  }
};
template <unsigned n> struct applicator<const char[n]> {
  template <class Opt> static void opt(StringRef Str, Opt &O) {
    O.setArgStr(Str);
  }
};
template <> struct applicator<StringRef > {
  template <class Opt> static void opt(StringRef Str, Opt &O) {
    O.setArgStr(Str);
  }
};

template <> struct applicator<NumOccurrencesFlag> {
  static void opt(NumOccurrencesFlag N, Option &O) {
    O.setNumOccurrencesFlag(N);
  }
};

template <> struct applicator<ValueExpected> {
  static void opt(ValueExpected VE, Option &O) { O.setValueExpectedFlag(VE); }
};

template <> struct applicator<OptionHidden> {
  static void opt(OptionHidden OH, Option &O) { O.setHiddenFlag(OH); }
};

template <> struct applicator<FormattingFlags> {
  static void opt(FormattingFlags FF, Option &O) { O.setFormattingFlag(FF); }
};

template <> struct applicator<MiscFlags> {
  static void opt(MiscFlags MF, Option &O) {
    assert((MF != Grouping || O.ArgStr.size() == 1) &&
           "cl::Grouping can only apply to single character Options.");
    O.setMiscFlag(MF);
  }
};

// Apply modifiers to an option in a type safe way.
template <class Opt, class Mod, class... Mods>
void apply(Opt *O, const Mod &M, const Mods &... Ms) {
  applicator<Mod>::opt(M, *O);
  apply(O, Ms...);
}

template <class Opt, class Mod> void apply(Opt *O, const Mod &M) {
  applicator<Mod>::opt(M, *O);
}

//===----------------------------------------------------------------------===//
// Default storage class definition: external storage.  This implementation
// assumes the user will specify a variable to store the data into with the
// cl::location(x) modifier.
//
template <class DataType, bool ExternalStorage, bool isClass>
class opt_storage {
  DataType *Location = nullptr; // Where to store the object...
  OptionValue<DataType> Default;

  void check_location() const {
    assert(Location && "cl::location(...) not specified for a command "
                       "line option with external storage, "
                       "or cl::init specified before cl::location()!!");
  }

public:
  opt_storage() = default;

  bool setLocation(Option &O, DataType &L) {
    if (Location)
      return O.error("cl::location(x) specified more than once!");
    Location = &L;
    Default = L;
    return false;
  }

  template <class T> void setValue(const T &V, bool initial = false) {
    check_location();
    *Location = V;
    if (initial)
      Default = V;
  }

  DataType &getValue() {
    check_location();
    return *Location;
  }
  const DataType &getValue() const {
    check_location();
    return *Location;
  }

  operator DataType() const { return this->getValue(); }

  const OptionValue<DataType> &getDefault() const { return Default; }
};

// Define how to hold a class type object, such as a string.  Since we can
// inherit from a class, we do so.  This makes us exactly compatible with the
// object in all cases that it is used.
//
template <class DataType>
class opt_storage<DataType, false, true> : public DataType {
public:
  OptionValue<DataType> Default;

  template <class T> void setValue(const T &V, bool initial = false) {
    DataType::operator=(V);
    if (initial)
      Default = V;
  }

  DataType &getValue() { return *this; }
  const DataType &getValue() const { return *this; }

  const OptionValue<DataType> &getDefault() const { return Default; }
};

// Define a partial specialization to handle things we cannot inherit from.  In
// this case, we store an instance through containment, and overload operators
// to get at the value.
//
template <class DataType> class opt_storage<DataType, false, false> {
public:
  DataType Value;
  OptionValue<DataType> Default;

  // Make sure we initialize the value with the default constructor for the
  // type.
  opt_storage() : Value(DataType()), Default() {}

  template <class T> void setValue(const T &V, bool initial = false) {
    Value = V;
    if (initial)
      Default = V;
  }
  DataType &getValue() { return Value; }
  DataType getValue() const { return Value; }

  const OptionValue<DataType> &getDefault() const { return Default; }

  operator DataType() const { return getValue(); }

  // If the datatype is a pointer, support -> on it.
  DataType operator->() const { return Value; }
};

//===----------------------------------------------------------------------===//
// A scalar command line option.
//
template <class DataType, bool ExternalStorage = false,
          class ParserClass = parser<DataType>>
class opt
    : public Option,
      public opt_storage<DataType, ExternalStorage, std::is_class_v<DataType>> {
  ParserClass Parser;

  bool handleOccurrence(unsigned pos, StringRef ArgName,
                        StringRef Arg) override {
    typename ParserClass::parser_data_type Val =
        typename ParserClass::parser_data_type();
    if (Parser.parse(*this, ArgName, Arg, Val))
      return true; // Parse error!
    this->setValue(Val);
    this->setPosition(pos);
    Callback(Val);
    return false;
  }

  enum ValueExpected getValueExpectedFlagDefault() const override {
    return Parser.getValueExpectedFlagDefault();
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) override {
    return Parser.getExtraOptionNames(OptionNames);
  }

  // Forward printing stuff to the parser...
  size_t getOptionWidth() const override {
    return Parser.getOptionWidth(*this);
  }

  void printOptionInfo(size_t GlobalWidth) const override {
    Parser.printOptionInfo(*this, GlobalWidth);
  }

  void printOptionValue(size_t GlobalWidth, bool Force) const override {
    if (Force || !this->getDefault().compare(this->getValue())) {
      cl::printOptionDiff<ParserClass>(*this, Parser, this->getValue(),
                                       this->getDefault(), GlobalWidth);
    }
  }

  template <class T, class = std::enable_if_t<std::is_assignable_v<T &, T>>>
  void setDefaultImpl() {
    const OptionValue<DataType> &V = this->getDefault();
    if (V.hasValue())
      this->setValue(V.getValue());
    else
      this->setValue(T());
  }

  template <class T, class = std::enable_if_t<!std::is_assignable_v<T &, T>>>
  void setDefaultImpl(...) {}

  void setDefault() override { setDefaultImpl<DataType>(); }

  void done() {
    addArgument();
    Parser.initialize();
  }

public:
  // Command line options should not be copyable
  opt(const opt &) = delete;
  opt &operator=(const opt &) = delete;

  // setInitialValue - Used by the cl::init modifier...
  void setInitialValue(const DataType &V) { this->setValue(V, true); }

  ParserClass &getParser() { return Parser; }

  template <class T> DataType &operator=(const T &Val) {
    this->setValue(Val);
    Callback(Val);
    return this->getValue();
  }

  template <class... Mods>
  explicit opt(const Mods &... Ms)
      : Option(llvm::cl::Optional, NotHidden), Parser(*this) {
    apply(this, Ms...);
    done();
  }

  void setCallback(
      std::function<void(const typename ParserClass::parser_data_type &)> CB) {
    Callback = CB;
  }

  std::function<void(const typename ParserClass::parser_data_type &)> Callback =
      [](const typename ParserClass::parser_data_type &) {};
};

extern template class opt<unsigned>;
extern template class opt<int>;
extern template class opt<std::string>;
extern template class opt<char>;
extern template class opt<bool>;

//===----------------------------------------------------------------------===//
// Default storage class definition: external storage.  This implementation
// assumes the user will specify a variable to store the data into with the
// cl::location(x) modifier.
//
template <class DataType, class StorageClass> class list_storage {
  StorageClass *Location = nullptr; // Where to store the object...
  std::vector<OptionValue<DataType>> Default =
      std::vector<OptionValue<DataType>>();
  bool DefaultAssigned = false;

public:
  list_storage() = default;

  void clear() {}

  bool setLocation(Option &O, StorageClass &L) {
    if (Location)
      return O.error("cl::location(x) specified more than once!");
    Location = &L;
    return false;
  }

  template <class T> void addValue(const T &V, bool initial = false) {
    assert(Location != nullptr &&
           "cl::location(...) not specified for a command "
           "line option with external storage!");
    Location->push_back(V);
    if (initial)
      Default.push_back(V);
  }

  const std::vector<OptionValue<DataType>> &getDefault() const {
    return Default;
  }

  void assignDefault() { DefaultAssigned = true; }
  void overwriteDefault() { DefaultAssigned = false; }
  bool isDefaultAssigned() { return DefaultAssigned; }
};

// Define how to hold a class type object, such as a string.
// Originally this code inherited from std::vector. In transitioning to a new
// API for command line options we should change this. The new implementation
// of this list_storage specialization implements the minimum subset of the
// std::vector API required for all the current clients.
//
// FIXME: Reduce this API to a more narrow subset of std::vector
//
template <class DataType> class list_storage<DataType, bool> {
  std::vector<DataType> Storage;
  std::vector<OptionValue<DataType>> Default;
  bool DefaultAssigned = false;

public:
  using iterator = typename std::vector<DataType>::iterator;

  iterator begin() { return Storage.begin(); }
  iterator end() { return Storage.end(); }

  using const_iterator = typename std::vector<DataType>::const_iterator;

  const_iterator begin() const { return Storage.begin(); }
  const_iterator end() const { return Storage.end(); }

  using size_type = typename std::vector<DataType>::size_type;

  size_type size() const { return Storage.size(); }

  bool empty() const { return Storage.empty(); }

  void push_back(const DataType &value) { Storage.push_back(value); }
  void push_back(DataType &&value) { Storage.push_back(value); }

  using reference = typename std::vector<DataType>::reference;
  using const_reference = typename std::vector<DataType>::const_reference;

  reference operator[](size_type pos) { return Storage[pos]; }
  const_reference operator[](size_type pos) const { return Storage[pos]; }

  void clear() {
    Storage.clear();
  }

  iterator erase(const_iterator pos) { return Storage.erase(pos); }
  iterator erase(const_iterator first, const_iterator last) {
    return Storage.erase(first, last);
  }

  iterator erase(iterator pos) { return Storage.erase(pos); }
  iterator erase(iterator first, iterator last) {
    return Storage.erase(first, last);
  }

  iterator insert(const_iterator pos, const DataType &value) {
    return Storage.insert(pos, value);
  }
  iterator insert(const_iterator pos, DataType &&value) {
    return Storage.insert(pos, value);
  }

  iterator insert(iterator pos, const DataType &value) {
    return Storage.insert(pos, value);
  }
  iterator insert(iterator pos, DataType &&value) {
    return Storage.insert(pos, value);
  }

  reference front() { return Storage.front(); }
  const_reference front() const { return Storage.front(); }

  operator std::vector<DataType> &() { return Storage; }
  operator ArrayRef<DataType>() const { return Storage; }
  std::vector<DataType> *operator&() { return &Storage; }
  const std::vector<DataType> *operator&() const { return &Storage; }

  template <class T> void addValue(const T &V, bool initial = false) {
    Storage.push_back(V);
    if (initial)
      Default.push_back(OptionValue<DataType>(V));
  }

  const std::vector<OptionValue<DataType>> &getDefault() const {
    return Default;
  }

  void assignDefault() { DefaultAssigned = true; }
  void overwriteDefault() { DefaultAssigned = false; }
  bool isDefaultAssigned() { return DefaultAssigned; }
};

//===----------------------------------------------------------------------===//
// A list of command line options.
//
template <class DataType, class StorageClass = bool,
          class ParserClass = parser<DataType>>
class list : public Option, public list_storage<DataType, StorageClass> {
  std::vector<unsigned> Positions;
  ParserClass Parser;

  enum ValueExpected getValueExpectedFlagDefault() const override {
    return Parser.getValueExpectedFlagDefault();
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) override {
    return Parser.getExtraOptionNames(OptionNames);
  }

  bool handleOccurrence(unsigned pos, StringRef ArgName,
                        StringRef Arg) override {
    typename ParserClass::parser_data_type Val =
        typename ParserClass::parser_data_type();
    if (list_storage<DataType, StorageClass>::isDefaultAssigned()) {
      clear();
      list_storage<DataType, StorageClass>::overwriteDefault();
    }
    if (Parser.parse(*this, ArgName, Arg, Val))
      return true; // Parse Error!
    list_storage<DataType, StorageClass>::addValue(Val);
    setPosition(pos);
    Positions.push_back(pos);
    Callback(Val);
    return false;
  }

  // Forward printing stuff to the parser...
  size_t getOptionWidth() const override {
    return Parser.getOptionWidth(*this);
  }

  void printOptionInfo(size_t GlobalWidth) const override {
    Parser.printOptionInfo(*this, GlobalWidth);
  }

  // Unimplemented: list options don't currently store their default value.
  void printOptionValue(size_t /*GlobalWidth*/, bool /*Force*/) const override {
  }

  void setDefault() override {
    Positions.clear();
    list_storage<DataType, StorageClass>::clear();
    for (auto &Val : list_storage<DataType, StorageClass>::getDefault())
      list_storage<DataType, StorageClass>::addValue(Val.getValue());
  }

  void done() {
    addArgument();
    Parser.initialize();
  }

public:
  // Command line options should not be copyable
  list(const list &) = delete;
  list &operator=(const list &) = delete;

  ParserClass &getParser() { return Parser; }

  unsigned getPosition(unsigned optnum) const {
    assert(optnum < this->size() && "Invalid option index");
    return Positions[optnum];
  }

  void clear() {
    Positions.clear();
    list_storage<DataType, StorageClass>::clear();
  }

  // setInitialValues - Used by the cl::list_init modifier...
  void setInitialValues(ArrayRef<DataType> Vs) {
    assert(!(list_storage<DataType, StorageClass>::isDefaultAssigned()) &&
           "Cannot have two default values");
    list_storage<DataType, StorageClass>::assignDefault();
    for (auto &Val : Vs)
      list_storage<DataType, StorageClass>::addValue(Val, true);
  }

  void setNumAdditionalVals(unsigned n) { Option::setNumAdditionalVals(n); }

  template <class... Mods>
  explicit list(const Mods &... Ms)
      : Option(ZeroOrMore, NotHidden), Parser(*this) {
    apply(this, Ms...);
    done();
  }

  void setCallback(
      std::function<void(const typename ParserClass::parser_data_type &)> CB) {
    Callback = CB;
  }

  std::function<void(const typename ParserClass::parser_data_type &)> Callback =
      [](const typename ParserClass::parser_data_type &) {};
};

// Modifier to set the number of additional values.
struct multi_val {
  unsigned AdditionalVals;
  explicit multi_val(unsigned N) : AdditionalVals(N) {}

  template <typename D, typename S, typename P>
  void apply(list<D, S, P> &L) const {
    L.setNumAdditionalVals(AdditionalVals);
  }
};

//===----------------------------------------------------------------------===//
// Default storage class definition: external storage.  This implementation
// assumes the user will specify a variable to store the data into with the
// cl::location(x) modifier.
//
template <class DataType, class StorageClass> class bits_storage {
  unsigned *Location = nullptr; // Where to store the bits...

  template <class T> static unsigned Bit(const T &V) {
    unsigned BitPos = static_cast<unsigned>(V);
    assert(BitPos < sizeof(unsigned) * CHAR_BIT &&
           "enum exceeds width of bit vector!");
    return 1 << BitPos;
  }

public:
  bits_storage() = default;

  bool setLocation(Option &O, unsigned &L) {
    if (Location)
      return O.error("cl::location(x) specified more than once!");
    Location = &L;
    return false;
  }

  template <class T> void addValue(const T &V) {
    assert(Location != nullptr &&
           "cl::location(...) not specified for a command "
           "line option with external storage!");
    *Location |= Bit(V);
  }

  unsigned getBits() { return *Location; }

  void clear() {
    if (Location)
      *Location = 0;
  }

  template <class T> bool isSet(const T &V) {
    return (*Location & Bit(V)) != 0;
  }
};

// Define how to hold bits.  Since we can inherit from a class, we do so.
// This makes us exactly compatible with the bits in all cases that it is used.
//
template <class DataType> class bits_storage<DataType, bool> {
  unsigned Bits{0}; // Where to store the bits...

  template <class T> static unsigned Bit(const T &V) {
    unsigned BitPos = static_cast<unsigned>(V);
    assert(BitPos < sizeof(unsigned) * CHAR_BIT &&
           "enum exceeds width of bit vector!");
    return 1 << BitPos;
  }

public:
  template <class T> void addValue(const T &V) { Bits |= Bit(V); }

  unsigned getBits() { return Bits; }

  void clear() { Bits = 0; }

  template <class T> bool isSet(const T &V) { return (Bits & Bit(V)) != 0; }
};

//===----------------------------------------------------------------------===//
// A bit vector of command options.
//
template <class DataType, class Storage = bool,
          class ParserClass = parser<DataType>>
class bits : public Option, public bits_storage<DataType, Storage> {
  std::vector<unsigned> Positions;
  ParserClass Parser;

  enum ValueExpected getValueExpectedFlagDefault() const override {
    return Parser.getValueExpectedFlagDefault();
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) override {
    return Parser.getExtraOptionNames(OptionNames);
  }

  bool handleOccurrence(unsigned pos, StringRef ArgName,
                        StringRef Arg) override {
    typename ParserClass::parser_data_type Val =
        typename ParserClass::parser_data_type();
    if (Parser.parse(*this, ArgName, Arg, Val))
      return true; // Parse Error!
    this->addValue(Val);
    setPosition(pos);
    Positions.push_back(pos);
    Callback(Val);
    return false;
  }

  // Forward printing stuff to the parser...
  size_t getOptionWidth() const override {
    return Parser.getOptionWidth(*this);
  }

  void printOptionInfo(size_t GlobalWidth) const override {
    Parser.printOptionInfo(*this, GlobalWidth);
  }

  // Unimplemented: bits options don't currently store their default values.
  void printOptionValue(size_t /*GlobalWidth*/, bool /*Force*/) const override {
  }

  void setDefault() override { bits_storage<DataType, Storage>::clear(); }

  void done() {
    addArgument();
    Parser.initialize();
  }

public:
  // Command line options should not be copyable
  bits(const bits &) = delete;
  bits &operator=(const bits &) = delete;

  ParserClass &getParser() { return Parser; }

  unsigned getPosition(unsigned optnum) const {
    assert(optnum < this->size() && "Invalid option index");
    return Positions[optnum];
  }

  template <class... Mods>
  explicit bits(const Mods &... Ms)
      : Option(ZeroOrMore, NotHidden), Parser(*this) {
    apply(this, Ms...);
    done();
  }

  void setCallback(
      std::function<void(const typename ParserClass::parser_data_type &)> CB) {
    Callback = CB;
  }

  std::function<void(const typename ParserClass::parser_data_type &)> Callback =
      [](const typename ParserClass::parser_data_type &) {};
};

//===----------------------------------------------------------------------===//
// Aliased command line option (alias this name to a preexisting name)
//

class alias : public Option {
  Option *AliasFor;

  bool handleOccurrence(unsigned pos, StringRef /*ArgName*/,
                        StringRef Arg) override {
    return AliasFor->handleOccurrence(pos, AliasFor->ArgStr, Arg);
  }

  bool addOccurrence(unsigned pos, StringRef /*ArgName*/, StringRef Value,
                     bool MultiArg = false) override {
    return AliasFor->addOccurrence(pos, AliasFor->ArgStr, Value, MultiArg);
  }

  // Handle printing stuff...
  size_t getOptionWidth() const override;
  void printOptionInfo(size_t GlobalWidth) const override;

  // Aliases do not need to print their values.
  void printOptionValue(size_t /*GlobalWidth*/, bool /*Force*/) const override {
  }

  void setDefault() override { AliasFor->setDefault(); }

  ValueExpected getValueExpectedFlagDefault() const override {
    return AliasFor->getValueExpectedFlag();
  }

  void done() {
    if (!hasArgStr())
      error("cl::alias must have argument name specified!");
    if (!AliasFor)
      error("cl::alias must have an cl::aliasopt(option) specified!");
    if (!Subs.empty())
      error("cl::alias must not have cl::sub(), aliased option's cl::sub() will be used!");
    Subs = AliasFor->Subs;
    Categories = AliasFor->Categories;
    addArgument();
  }

public:
  // Command line options should not be copyable
  alias(const alias &) = delete;
  alias &operator=(const alias &) = delete;

  void setAliasFor(Option &O) {
    if (AliasFor)
      error("cl::alias must only have one cl::aliasopt(...) specified!");
    AliasFor = &O;
  }

  template <class... Mods>
  explicit alias(const Mods &... Ms)
      : Option(Optional, Hidden), AliasFor(nullptr) {
    apply(this, Ms...);
    done();
  }
};

// Modifier to set the option an alias aliases.
struct aliasopt {
  Option &Opt;

  explicit aliasopt(Option &O) : Opt(O) {}

  void apply(alias &A) const { A.setAliasFor(Opt); }
};

// Provide additional help at the end of the normal help output. All occurrences
// of cl::extrahelp will be accumulated and printed to stderr at the end of the
// regular help, just before exit is called.
struct extrahelp {
  StringRef morehelp;

  explicit extrahelp(StringRef help);
};

void PrintVersionMessage();

/// This function just prints the help message, exactly the same way as if the
/// -help or -help-hidden option had been given on the command line.
///
/// \param Hidden if true will print hidden options
/// \param Categorized if true print options in categories
void PrintHelpMessage(bool Hidden = false, bool Categorized = false);

/// An array of optional enabled settings in the LLVM build configuration,
/// which may be of interest to compiler developers. For example, includes
/// "+assertions" if assertions are enabled. Used by printBuildConfig.
ArrayRef<StringRef> getCompilerBuildConfig();

/// Prints the compiler build configuration.
/// Designed for compiler developers, not compiler end-users.
/// Intended to be used in --version output when enabled.
void printBuildConfig(raw_ostream &OS);

//===----------------------------------------------------------------------===//
// Public interface for accessing registered options.
//

/// Use this to get a StringMap to all registered named options
/// (e.g. -help).
///
/// \return A reference to the StringMap used by the cl APIs to parse options.
///
/// Access to unnamed arguments (i.e. positional) are not provided because
/// it is expected that the client already has access to these.
///
/// Typical usage:
/// \code
/// main(int argc,char* argv[]) {
/// StringMap<llvm::cl::Option*> &opts = llvm::cl::getRegisteredOptions();
/// assert(opts.count("help") == 1)
/// opts["help"]->setDescription("Show alphabetical help information")
/// // More code
/// llvm::cl::ParseCommandLineOptions(argc,argv);
/// //More code
/// }
/// \endcode
///
/// This interface is useful for modifying options in libraries that are out of
/// the control of the client. The options should be modified before calling
/// llvm::cl::ParseCommandLineOptions().
///
/// Hopefully this API can be deprecated soon. Any situation where options need
/// to be modified by tools or libraries should be handled by sane APIs rather
/// than just handing around a global list.
StringMap<Option *> &
getRegisteredOptions(SubCommand &Sub = SubCommand::getTopLevel());

/// Use this to get all registered SubCommands from the provided parser.
///
/// \return A range of all SubCommand pointers registered with the parser.
///
/// Typical usage:
/// \code
/// main(int argc, char* argv[]) {
///   llvm::cl::ParseCommandLineOptions(argc, argv);
///   for (auto* S : llvm::cl::getRegisteredSubcommands()) {
///     if (*S) {
///       std::cout << "Executing subcommand: " << S->getName() << std::endl;
///       // Execute some function based on the name...
///     }
///   }
/// }
/// \endcode
///
/// This interface is useful for defining subcommands in libraries and
/// the dispatch from a single point (like in the main function).
iterator_range<typename SmallPtrSet<SubCommand *, 4>::iterator>
getRegisteredSubcommands();

//===----------------------------------------------------------------------===//
// Standalone command line processing utilities.
//

/// Tokenizes a command line that can contain escapes and quotes.
//
/// The quoting rules match those used by GCC and other tools that use
/// libiberty's buildargv() or expandargv() utilities, and do not match bash.
/// They differ from buildargv() on treatment of backslashes that do not escape
/// a special character to make it possible to accept most Windows file paths.
///
/// \param [in] Source The string to be split on whitespace with quotes.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [in] MarkEOLs true if tokenizing a response file and you want end of
/// lines and end of the response file to be marked with a nullptr string.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
void TokenizeGNUCommandLine(StringRef Source, StringSaver &Saver,
                            SmallVectorImpl<const char *> &NewArgv,
                            bool MarkEOLs = false);

/// Tokenizes a string of Windows command line arguments, which may contain
/// quotes and escaped quotes.
///
/// See MSDN docs for CommandLineToArgvW for information on the quoting rules.
/// http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
///
/// For handling a full Windows command line including the executable name at
/// the start, see TokenizeWindowsCommandLineFull below.
///
/// \param [in] Source The string to be split on whitespace with quotes.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [in] MarkEOLs true if tokenizing a response file and you want end of
/// lines and end of the response file to be marked with a nullptr string.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
void TokenizeWindowsCommandLine(StringRef Source, StringSaver &Saver,
                                SmallVectorImpl<const char *> &NewArgv,
                                bool MarkEOLs = false);

/// Tokenizes a Windows command line while attempting to avoid copies. If no
/// quoting or escaping was used, this produces substrings of the original
/// string. If a token requires unquoting, it will be allocated with the
/// StringSaver.
void TokenizeWindowsCommandLineNoCopy(StringRef Source, StringSaver &Saver,
                                      SmallVectorImpl<StringRef> &NewArgv);

/// Tokenizes a Windows full command line, including command name at the start.
///
/// This uses the same syntax rules as TokenizeWindowsCommandLine for all but
/// the first token. But the first token is expected to be parsed as the
/// executable file name in the way CreateProcess would do it, rather than the
/// way the C library startup code would do it: CreateProcess does not consider
/// that \ is ever an escape character (because " is not a valid filename char,
/// hence there's never a need to escape it to be used literally).
///
/// Parameters are the same as for TokenizeWindowsCommandLine. In particular,
/// if you set MarkEOLs = true, then the first word of every line will be
/// parsed using the special rules for command names, making this function
/// suitable for parsing a file full of commands to execute.
void TokenizeWindowsCommandLineFull(StringRef Source, StringSaver &Saver,
                                    SmallVectorImpl<const char *> &NewArgv,
                                    bool MarkEOLs = false);

/// String tokenization function type.  Should be compatible with either
/// Windows or Unix command line tokenizers.
using TokenizerCallback = void (*)(StringRef Source, StringSaver &Saver,
                                   SmallVectorImpl<const char *> &NewArgv,
                                   bool MarkEOLs);

/// Tokenizes content of configuration file.
///
/// \param [in] Source The string representing content of config file.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
/// \param [in] MarkEOLs Added for compatibility with TokenizerCallback.
///
/// It works like TokenizeGNUCommandLine with ability to skip comment lines.
///
void tokenizeConfigFile(StringRef Source, StringSaver &Saver,
                        SmallVectorImpl<const char *> &NewArgv,
                        bool MarkEOLs = false);

/// Contains options that control response file expansion.
class ExpansionContext {
  /// Provides persistent storage for parsed strings.
  StringSaver Saver;

  /// Tokenization strategy. Typically Unix or Windows.
  TokenizerCallback Tokenizer;

  /// File system used for all file access when running the expansion.
  vfs::FileSystem *FS;

  /// Path used to resolve relative rsp files. If empty, the file system
  /// current directory is used instead.
  StringRef CurrentDir;

  /// Directories used for search of config files.
  ArrayRef<StringRef> SearchDirs;

  /// True if names of nested response files must be resolved relative to
  /// including file.
  bool RelativeNames = false;

  /// If true, mark end of lines and the end of the response file with nullptrs
  /// in the Argv vector.
  bool MarkEOLs = false;

  /// If true, body of config file is expanded.
  bool InConfigFile = false;

  llvm::Error expandResponseFile(StringRef FName,
                                 SmallVectorImpl<const char *> &NewArgv);

public:
  ExpansionContext(BumpPtrAllocator &A, TokenizerCallback T);

  ExpansionContext &setMarkEOLs(bool X) {
    MarkEOLs = X;
    return *this;
  }

  ExpansionContext &setRelativeNames(bool X) {
    RelativeNames = X;
    return *this;
  }

  ExpansionContext &setCurrentDir(StringRef X) {
    CurrentDir = X;
    return *this;
  }

  ExpansionContext &setSearchDirs(ArrayRef<StringRef> X) {
    SearchDirs = X;
    return *this;
  }

  ExpansionContext &setVFS(vfs::FileSystem *X) {
    FS = X;
    return *this;
  }

  /// Looks for the specified configuration file.
  ///
  /// \param[in]  FileName Name of the file to search for.
  /// \param[out] FilePath File absolute path, if it was found.
  /// \return True if file was found.
  ///
  /// If the specified file name contains a directory separator, it is searched
  /// for by its absolute path. Otherwise looks for file sequentially in
  /// directories specified by SearchDirs field.
  bool findConfigFile(StringRef FileName, SmallVectorImpl<char> &FilePath);

  /// Reads command line options from the given configuration file.
  ///
  /// \param [in] CfgFile Path to configuration file.
  /// \param [out] Argv Array to which the read options are added.
  /// \return true if the file was successfully read.
  ///
  /// It reads content of the specified file, tokenizes it and expands "@file"
  /// commands resolving file names in them relative to the directory where
  /// CfgFilename resides. It also expands "<CFGDIR>" to the base path of the
  /// current config file.
  Error readConfigFile(StringRef CfgFile, SmallVectorImpl<const char *> &Argv);

  /// Expands constructs "@file" in the provided array of arguments recursively.
  Error expandResponseFiles(SmallVectorImpl<const char *> &Argv);
};

/// A convenience helper which concatenates the options specified by the
/// environment variable EnvVar and command line options, then expands
/// response files recursively.
/// \return true if all @files were expanded successfully or there were none.
bool expandResponseFiles(int Argc, const char *const *Argv, const char *EnvVar,
                         SmallVectorImpl<const char *> &NewArgv);

/// A convenience helper which supports the typical use case of expansion
/// function call.
bool ExpandResponseFiles(StringSaver &Saver, TokenizerCallback Tokenizer,
                         SmallVectorImpl<const char *> &Argv);

/// A convenience helper which concatenates the options specified by the
/// environment variable EnvVar and command line options, then expands response
/// files recursively. The tokenizer is a predefined GNU or Windows one.
/// \return true if all @files were expanded successfully or there were none.
bool expandResponseFiles(int Argc, const char *const *Argv, const char *EnvVar,
                         StringSaver &Saver,
                         SmallVectorImpl<const char *> &NewArgv);

/// Mark all options not part of this category as cl::ReallyHidden.
///
/// \param Category the category of options to keep displaying
///
/// Some tools (like clang-format) like to be able to hide all options that are
/// not specific to the tool. This function allows a tool to specify a single
/// option category to display in the -help output.
void HideUnrelatedOptions(cl::OptionCategory &Category,
                          SubCommand &Sub = SubCommand::getTopLevel());

/// Mark all options not part of the categories as cl::ReallyHidden.
///
/// \param Categories the categories of options to keep displaying.
///
/// Some tools (like clang-format) like to be able to hide all options that are
/// not specific to the tool. This function allows a tool to specify a single
/// option category to display in the -help output.
void HideUnrelatedOptions(ArrayRef<const cl::OptionCategory *> Categories,
                          SubCommand &Sub = SubCommand::getTopLevel());

/// Reset all command line options to a state that looks as if they have
/// never appeared on the command line.  This is useful for being able to parse
/// a command line multiple times (especially useful for writing tests).
void ResetAllOptionOccurrences();

/// Reset the command line parser back to its initial state.  This
/// removes
/// all options, categories, and subcommands and returns the parser to a state
/// where no options are supported.
void ResetCommandLineParser();

/// Parses `Arg` into the option handler `Handler`.
bool ProvidePositionalOption(Option *Handler, StringRef Arg, int i);

} // end namespace cl

} // end namespace llvm

#endif // LLVM_SUPPORT_COMMANDLINE_H
