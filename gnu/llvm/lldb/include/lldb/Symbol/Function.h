//===-- Function.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_FUNCTION_H
#define LLDB_SYMBOL_FUNCTION_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Utility/UserID.h"
#include "llvm/ADT/ArrayRef.h"

#include <mutex>

namespace lldb_private {

class ExecutionContext;

/// \class FunctionInfo Function.h "lldb/Symbol/Function.h"
/// A class that contains generic function information.
///
/// This provides generic function information that gets reused between inline
/// functions and function types.
class FunctionInfo {
public:
  /// Construct with the function method name and optional declaration
  /// information.
  ///
  /// \param[in] name
  ///     A C string name for the method name for this function. This
  ///     value should not be the mangled named, but the simple method
  ///     name.
  ///
  /// \param[in] decl_ptr
  ///     Optional declaration information that describes where the
  ///     function was declared. This can be NULL.
  FunctionInfo(const char *name, const Declaration *decl_ptr);

  /// Construct with the function method name and optional declaration
  /// information.
  ///
  /// \param[in] name
  ///     A name for the method name for this function. This value
  ///     should not be the mangled named, but the simple method name.
  ///
  /// \param[in] decl_ptr
  ///     Optional declaration information that describes where the
  ///     function was declared. This can be NULL.
  FunctionInfo(ConstString name, const Declaration *decl_ptr);

  /// Destructor.
  ///
  /// The destructor is virtual since classes inherit from this class.
  virtual ~FunctionInfo();

  /// Compare two function information objects.
  ///
  /// First compares the method names, and if equal, then compares the
  /// declaration information.
  ///
  /// \param[in] lhs
  ///     The Left Hand Side const FunctionInfo object reference.
  ///
  /// \param[in] rhs
  ///     The Right Hand Side const FunctionInfo object reference.
  ///
  /// \return
  ///     -1 if lhs < rhs
  ///     0 if lhs == rhs
  ///     1 if lhs > rhs
  static int Compare(const FunctionInfo &lhs, const FunctionInfo &rhs);

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s, bool show_fullpaths) const;

  /// Get accessor for the declaration information.
  ///
  /// \return
  ///     A reference to the declaration object.
  Declaration &GetDeclaration();

  /// Get const accessor for the declaration information.
  ///
  /// \return
  ///     A const reference to the declaration object.
  const Declaration &GetDeclaration() const;

  /// Get accessor for the method name.
  ///
  /// \return
  ///     A const reference to the method name object.
  ConstString GetName() const;

  /// Get the memory cost of this object.
  ///
  /// \return
  ///     The number of bytes that this object occupies in memory.
  ///     The returned value does not include the bytes for any
  ///     shared string values.
  virtual size_t MemorySize() const;

protected:
  /// Function method name (not a mangled name).
  ConstString m_name;

  /// Information describing where this function information was defined.
  Declaration m_declaration;
};

/// \class InlineFunctionInfo Function.h "lldb/Symbol/Function.h"
/// A class that describes information for an inlined function.
class InlineFunctionInfo : public FunctionInfo {
public:
  /// Construct with the function method name, mangled name, and optional
  /// declaration information.
  ///
  /// \param[in] name
  ///     A C string name for the method name for this function. This
  ///     value should not be the mangled named, but the simple method
  ///     name.
  ///
  /// \param[in] mangled
  ///     A C string name for the mangled name for this function. This
  ///     value can be NULL if there is no mangled information.
  ///
  /// \param[in] decl_ptr
  ///     Optional declaration information that describes where the
  ///     function was declared. This can be NULL.
  ///
  /// \param[in] call_decl_ptr
  ///     Optional calling location declaration information that
  ///     describes from where this inlined function was called.
  InlineFunctionInfo(const char *name, llvm::StringRef mangled,
                     const Declaration *decl_ptr,
                     const Declaration *call_decl_ptr);

  /// Construct with the function method name, mangled name, and optional
  /// declaration information.
  ///
  /// \param[in] name
  ///     A name for the method name for this function. This value
  ///     should not be the mangled named, but the simple method name.
  ///
  /// \param[in] mangled
  ///     A name for the mangled name for this function. This value
  ///     can be empty if there is no mangled information.
  ///
  /// \param[in] decl_ptr
  ///     Optional declaration information that describes where the
  ///     function was declared. This can be NULL.
  ///
  /// \param[in] call_decl_ptr
  ///     Optional calling location declaration information that
  ///     describes from where this inlined function was called.
  InlineFunctionInfo(ConstString name, const Mangled &mangled,
                     const Declaration *decl_ptr,
                     const Declaration *call_decl_ptr);

  /// Destructor.
  ~InlineFunctionInfo() override;

  /// Compare two inlined function information objects.
  ///
  /// First compares the FunctionInfo objects, and if equal, compares the
  /// mangled names.
  ///
  /// \param[in] lhs
  ///     The Left Hand Side const InlineFunctionInfo object
  ///     reference.
  ///
  /// \param[in] rhs
  ///     The Right Hand Side const InlineFunctionInfo object
  ///     reference.
  ///
  /// \return
  ///     -1 if lhs < rhs
  ///     0 if lhs == rhs
  ///     1 if lhs > rhs
  int Compare(const InlineFunctionInfo &lhs, const InlineFunctionInfo &rhs);

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s, bool show_fullpaths) const;

  void DumpStopContext(Stream *s) const;

  ConstString GetName() const;

  ConstString GetDisplayName() const;

  /// Get accessor for the call site declaration information.
  ///
  /// \return
  ///     A reference to the declaration object.
  Declaration &GetCallSite();

  /// Get const accessor for the call site declaration information.
  ///
  /// \return
  ///     A const reference to the declaration object.
  const Declaration &GetCallSite() const;

  /// Get accessor for the mangled name object.
  ///
  /// \return
  ///     A reference to the mangled name object.
  Mangled &GetMangled();

  /// Get const accessor for the mangled name object.
  ///
  /// \return
  ///     A const reference to the mangled name object.
  const Mangled &GetMangled() const;

  /// Get the memory cost of this object.
  ///
  /// \return
  ///     The number of bytes that this object occupies in memory.
  ///     The returned value does not include the bytes for any
  ///     shared string values.
  size_t MemorySize() const override;

private:
  /// Mangled inlined function name (can be empty if there is no mangled
  /// information).
  Mangled m_mangled;

  Declaration m_call_decl;
};

class Function;

/// \class CallSiteParameter Function.h "lldb/Symbol/Function.h"
///
/// Represent the locations of a parameter at a call site, both in the caller
/// and in the callee.
struct CallSiteParameter {
  DWARFExpressionList LocationInCallee;
  DWARFExpressionList LocationInCaller;
};

/// A vector of \c CallSiteParameter.
using CallSiteParameterArray = llvm::SmallVector<CallSiteParameter, 0>;

/// \class CallEdge Function.h "lldb/Symbol/Function.h"
///
/// Represent a call made within a Function. This can be used to find a path
/// in the call graph between two functions, or to evaluate DW_OP_entry_value.
class CallEdge {
public:
  enum class AddrType : uint8_t { Call, AfterCall };
  virtual ~CallEdge();

  /// Get the callee's definition.
  ///
  /// Note that this might lazily invoke the DWARF parser. A register context
  /// from the caller's activation is needed to find indirect call targets.
  virtual Function *GetCallee(ModuleList &images,
                              ExecutionContext &exe_ctx) = 0;

  /// Get the load PC address of the instruction which executes after the call
  /// returns. Returns LLDB_INVALID_ADDRESS iff this is a tail call. \p caller
  /// is the Function containing this call, and \p target is the Target which
  /// made the call.
  lldb::addr_t GetReturnPCAddress(Function &caller, Target &target) const;

  /// Return an address in the caller. This can either be the address of the
  /// call instruction, or the address of the instruction after the call.
  std::pair<AddrType, lldb::addr_t> GetCallerAddress(Function &caller,
                                                     Target &target) const {
    return {caller_address_type,
            GetLoadAddress(caller_address, caller, target)};
  }

  bool IsTailCall() const { return is_tail_call; }

  /// Get the call site parameters available at this call edge.
  llvm::ArrayRef<CallSiteParameter> GetCallSiteParameters() const {
    return parameters;
  }

  /// Non-tail-calls go first, sorted by the return address. They are followed
  /// by tail calls, which have no specific order.
  std::pair<bool, lldb::addr_t> GetSortKey() const {
    return {is_tail_call, GetUnresolvedReturnPCAddress()};
  }

protected:
  CallEdge(AddrType caller_address_type, lldb::addr_t caller_address,
           bool is_tail_call, CallSiteParameterArray &&parameters);

  /// Helper that finds the load address of \p unresolved_pc, a file address
  /// which refers to an instruction within \p caller.
  static lldb::addr_t GetLoadAddress(lldb::addr_t unresolved_pc,
                                     Function &caller, Target &target);

  /// Like \ref GetReturnPCAddress, but returns an unresolved file address.
  lldb::addr_t GetUnresolvedReturnPCAddress() const {
    return caller_address_type == AddrType::AfterCall && !is_tail_call
               ? caller_address
               : LLDB_INVALID_ADDRESS;
  }

private:
  lldb::addr_t caller_address;
  AddrType caller_address_type;
  bool is_tail_call;

  CallSiteParameterArray parameters;
};

/// A direct call site. Used to represent call sites where the address of the
/// callee is fixed (e.g. a function call in C in which the call target is not
/// a function pointer).
class DirectCallEdge : public CallEdge {
public:
  /// Construct a call edge using a symbol name to identify the callee, and a
  /// return PC within the calling function to identify a specific call site.
  DirectCallEdge(const char *symbol_name, AddrType caller_address_type,
                 lldb::addr_t caller_address, bool is_tail_call,
                 CallSiteParameterArray &&parameters);

  Function *GetCallee(ModuleList &images, ExecutionContext &exe_ctx) override;

private:
  void ParseSymbolFileAndResolve(ModuleList &images);

  // Used to describe a direct call.
  //
  // Either the callee's mangled name or its definition, discriminated by
  // \ref resolved.
  union {
    const char *symbol_name;
    Function *def;
  } lazy_callee;

  /// Whether or not an attempt was made to find the callee's definition.
  bool resolved = false;
};

/// An indirect call site. Used to represent call sites where the address of
/// the callee is not fixed, e.g. a call to a C++ virtual function (where the
/// address is loaded out of a vtable), or a call to a function pointer in C.
class IndirectCallEdge : public CallEdge {
public:
  /// Construct a call edge using a DWARFExpression to identify the callee, and
  /// a return PC within the calling function to identify a specific call site.
  IndirectCallEdge(DWARFExpressionList call_target,
                   AddrType caller_address_type, lldb::addr_t caller_address,
                   bool is_tail_call, CallSiteParameterArray &&parameters);

  Function *GetCallee(ModuleList &images, ExecutionContext &exe_ctx) override;

private:
  // Used to describe an indirect call.
  //
  // Specifies the location of the callee address in the calling frame.
  DWARFExpressionList call_target;
};

/// \class Function Function.h "lldb/Symbol/Function.h"
/// A class that describes a function.
///
/// Functions belong to CompileUnit objects (Function::m_comp_unit), have
/// unique user IDs (Function::UserID), know how to reconstruct their symbol
/// context (Function::SymbolContextScope), have a specific function type
/// (Function::m_type_uid), have a simple method name (FunctionInfo::m_name),
/// be declared at a specific location (FunctionInfo::m_declaration), possibly
/// have mangled names (Function::m_mangled), an optional return type
/// (Function::m_type), and contains lexical blocks (Function::m_blocks).
///
/// The function information is split into a few pieces:
///     \li The concrete instance information
///     \li The abstract information
///
/// The abstract information is found in the function type (Type) that
/// describes a function information, return type and parameter types.
///
/// The concrete information is the address range information and specific
/// locations for an instance of this function.
class Function : public UserID, public SymbolContextScope {
public:
  /// Construct with a compile unit, function UID, function type UID, optional
  /// mangled name, function type, and a section offset based address range.
  ///
  /// \param[in] comp_unit
  ///     The compile unit to which this function belongs.
  ///
  /// \param[in] func_uid
  ///     The UID for this function. This value is provided by the
  ///     SymbolFile plug-in and can be any value that allows
  ///     the plug-in to quickly find and parse more detailed
  ///     information when and if more information is needed.
  ///
  /// \param[in] func_type_uid
  ///     The type UID for the function Type to allow for lazy type
  ///     parsing from the debug information.
  ///
  /// \param[in] mangled
  ///     The optional mangled name for this function. If empty, there
  ///     is no mangled information.
  ///
  /// \param[in] func_type
  ///     The optional function type. If NULL, the function type will
  ///     be parsed on demand when accessed using the
  ///     Function::GetType() function by asking the SymbolFile
  ///     plug-in to get the type for \a func_type_uid.
  ///
  /// \param[in] range
  ///     The section offset based address for this function.
  Function(CompileUnit *comp_unit, lldb::user_id_t func_uid,
           lldb::user_id_t func_type_uid, const Mangled &mangled,
           Type *func_type, const AddressRange &range);

  /// Destructor.
  ~Function() override;

  /// \copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// \see SymbolContextScope
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  CompileUnit *CalculateSymbolContextCompileUnit() override;

  Function *CalculateSymbolContextFunction() override;

  const AddressRange &GetAddressRange() { return m_range; }

  lldb::LanguageType GetLanguage() const;
  /// Find the file and line number of the source location of the start of the
  /// function.  This will use the declaration if present and fall back on the
  /// line table if that fails.  So there may NOT be a line table entry for
  /// this source file/line combo.
  ///
  /// \param[out] source_file
  ///     The source file.
  ///
  /// \param[out] line_no
  ///     The line number.
  void GetStartLineSourceInfo(FileSpec &source_file, uint32_t &line_no);

  /// Find the file and line number of the source location of the end of the
  /// function.
  ///
  ///
  /// \param[out] source_file
  ///     The source file.
  ///
  /// \param[out] line_no
  ///     The line number.
  void GetEndLineSourceInfo(FileSpec &source_file, uint32_t &line_no);

  /// Get the outgoing call edges from this function, sorted by their return
  /// PC addresses (in increasing order).
  llvm::ArrayRef<std::unique_ptr<CallEdge>> GetCallEdges();

  /// Get the outgoing tail-calling edges from this function. If none exist,
  /// return std::nullopt.
  llvm::ArrayRef<std::unique_ptr<CallEdge>> GetTailCallingEdges();

  /// Get the outgoing call edge from this function which has the given return
  /// address \p return_pc, or return nullptr. Note that this will not return a
  /// tail-calling edge.
  CallEdge *GetCallEdgeForReturnAddress(lldb::addr_t return_pc, Target &target);

  /// Get accessor for the block list.
  ///
  /// \return
  ///     The block list object that describes all lexical blocks
  ///     in the function.
  ///
  /// \see BlockList
  Block &GetBlock(bool can_create);

  /// Get accessor for the compile unit that owns this function.
  ///
  /// \return
  ///     A compile unit object pointer.
  CompileUnit *GetCompileUnit();

  /// Get const accessor for the compile unit that owns this function.
  ///
  /// \return
  ///     A const compile unit object pointer.
  const CompileUnit *GetCompileUnit() const;

  void GetDescription(Stream *s, lldb::DescriptionLevel level, Target *target);

  /// Get accessor for the frame base location.
  ///
  /// \return
  ///     A location expression that describes the function frame
  ///     base.
  DWARFExpressionList &GetFrameBaseExpression() { return m_frame_base; }

  /// Get const accessor for the frame base location.
  ///
  /// \return
  ///     A const compile unit object pointer.
  const DWARFExpressionList &GetFrameBaseExpression() const { return m_frame_base; }

  ConstString GetName() const;

  ConstString GetNameNoArguments() const;

  ConstString GetDisplayName() const;

  const Mangled &GetMangled() const { return m_mangled; }

  /// Get the DeclContext for this function, if available.
  ///
  /// \return
  ///     The DeclContext, or NULL if none exists.
  CompilerDeclContext GetDeclContext();

  /// Get the CompilerContext for this function, if available.
  ///
  /// \return
  ///     The CompilerContext, or an empty vector if none is available.
  std::vector<CompilerContext> GetCompilerContext();

  /// Get accessor for the type that describes the function return value type,
  /// and parameter types.
  ///
  /// \return
  ///     A type object pointer.
  Type *GetType();

  /// Get const accessor for the type that describes the function return value
  /// type, and parameter types.
  ///
  /// \return
  ///     A const type object pointer.
  const Type *GetType() const;

  CompilerType GetCompilerType();

  /// Get the size of the prologue instructions for this function.  The
  /// "prologue" instructions include any instructions given line number 0
  /// immediately following the prologue end.
  ///
  /// \return
  ///     The size of the prologue.
  uint32_t GetPrologueByteSize();

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \param[in] show_context
  ///     If \b true, variables will dump their symbol context
  ///     information.
  void Dump(Stream *s, bool show_context) const;

  /// \copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// \see SymbolContextScope
  void DumpSymbolContext(Stream *s) override;

  /// Get the memory cost of this object.
  ///
  /// \return
  ///     The number of bytes that this object occupies in memory.
  ///     The returned value does not include the bytes for any
  ///     shared string values.
  size_t MemorySize() const;

  /// Get whether compiler optimizations were enabled for this function
  ///
  /// The debug information may provide information about whether this
  /// function was compiled with optimization or not.  In this case,
  /// "optimized" means that the debug experience may be difficult for the
  /// user to understand.  Variables may not be available when the developer
  /// would expect them, stepping through the source lines in the function may
  /// appear strange, etc.
  ///
  /// \return
  ///     Returns 'true' if this function was compiled with
  ///     optimization.  'false' indicates that either the optimization
  ///     is unknown, or this function was built without optimization.
  bool GetIsOptimized();

  /// Get whether this function represents a 'top-level' function
  ///
  /// The concept of a top-level function is language-specific, mostly meant
  /// to represent the notion of scripting-style code that has global
  /// visibility of the variables/symbols/functions/... defined within the
  /// containing file/module
  ///
  /// If stopped in a top-level function, LLDB will expose global variables
  /// as-if locals in the 'frame variable' command
  ///
  /// \return
  ///     Returns 'true' if this function is a top-level function,
  ///     'false' otherwise.
  bool IsTopLevelFunction();

  lldb::DisassemblerSP GetInstructions(const ExecutionContext &exe_ctx,
                                       const char *flavor,
                                       bool force_live_memory = false);

  bool GetDisassembly(const ExecutionContext &exe_ctx, const char *flavor,
                      Stream &strm, bool force_live_memory = false);

protected:
  enum {
    /// Whether we already tried to calculate the prologue size.
    flagsCalculatedPrologueSize = (1 << 0)
  };

  /// The compile unit that owns this function.
  CompileUnit *m_comp_unit;

  /// The user ID of for the prototype Type for this function.
  lldb::user_id_t m_type_uid;

  /// The function prototype type for this function that includes the function
  /// info (FunctionInfo), return type and parameters.
  Type *m_type;

  /// The mangled function name if any. If empty, there is no mangled
  /// information.
  Mangled m_mangled;

  /// All lexical blocks contained in this function.
  Block m_block;

  /// The function address range that covers the widest range needed to contain
  /// all blocks
  AddressRange m_range;

  /// The frame base expression for variables that are relative to the frame
  /// pointer.
  DWARFExpressionList m_frame_base;

  Flags m_flags;

  /// Compute the prologue size once and cache it.
  uint32_t m_prologue_byte_size;

  /// Exclusive lock that controls read/write access to m_call_edges and
  /// m_call_edges_resolved.
  std::mutex m_call_edges_lock;

  /// Whether call site info has been parsed.
  bool m_call_edges_resolved = false;

  /// Outgoing call edges.
  std::vector<std::unique_ptr<CallEdge>> m_call_edges;

private:
  Function(const Function &) = delete;
  const Function &operator=(const Function &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_FUNCTION_H
