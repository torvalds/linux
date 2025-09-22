
//===-- StackFrame.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_STACKFRAME_H
#define LLDB_TARGET_STACKFRAME_H

#include <memory>
#include <mutex>

#include "lldb/Utility/Flags.h"

#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/StackID.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/UserID.h"

namespace lldb_private {

/// \class StackFrame StackFrame.h "lldb/Target/StackFrame.h"
///
/// This base class provides an interface to stack frames.
///
/// StackFrames may have a Canonical Frame Address (CFA) or not.
/// A frame may have a plain pc value or it may  indicate a specific point in
/// the debug session so the correct section load list is used for
/// symbolication.
///
/// Local variables may be available, or not.  A register context may be
/// available, or not.

class StackFrame : public ExecutionContextScope,
                   public std::enable_shared_from_this<StackFrame> {
public:
  enum ExpressionPathOption {
    eExpressionPathOptionCheckPtrVsMember = (1u << 0),
    eExpressionPathOptionsNoFragileObjcIvar = (1u << 1),
    eExpressionPathOptionsNoSyntheticChildren = (1u << 2),
    eExpressionPathOptionsNoSyntheticArrayRange = (1u << 3),
    eExpressionPathOptionsAllowDirectIVarAccess = (1u << 4),
    eExpressionPathOptionsInspectAnonymousUnions = (1u << 5)
  };

  enum class Kind {
    /// A regular stack frame with access to registers and local variables.
    Regular,

    /// A historical stack frame -- possibly without CFA or registers or
    /// local variables.
    History,

    /// An artificial stack frame (e.g. a synthesized result of inferring
    /// missing tail call frames from a backtrace) with limited support for
    /// local variables.
    Artificial
  };

  /// Construct a StackFrame object without supplying a RegisterContextSP.
  ///
  /// This is the one constructor that doesn't take a RegisterContext
  /// parameter.  This ctor may be called when creating a history StackFrame;
  /// these are used if we've collected a stack trace of pc addresses at some
  /// point in the past.  We may only have pc values. We may have a CFA,
  /// or more likely, we won't.
  ///
  /// \param [in] thread_sp
  ///   The Thread that this frame belongs to.
  ///
  /// \param [in] frame_idx
  ///   This StackFrame's frame index number in the Thread.  If inlined stack
  ///   frames are being created, this may differ from the concrete_frame_idx
  ///   which is the frame index without any inlined stack frames.
  ///
  /// \param [in] concrete_frame_idx
  ///   The StackFrame's frame index number in the Thread without any inlined
  ///   stack frames being included in the index.
  ///
  /// \param [in] cfa
  ///   The Canonical Frame Address (this terminology from DWARF) for this
  ///   stack frame.  The CFA for a stack frame does not change over the
  ///   span of the stack frame's existence.  It is often the value of the
  ///   caller's stack pointer before the call instruction into this frame's
  ///   function.  It is usually not the same as the frame pointer register's
  ///   value.
  ///
  /// \param [in] cfa_is_valid
  ///   A history stack frame may not have a CFA value collected.  We want to
  ///   distinguish between "no CFA available" and a CFA of
  ///   LLDB_INVALID_ADDRESS.
  ///
  /// \param [in] pc
  ///   The current pc value of this stack frame.
  ///
  /// \param [in] sc_ptr
  ///   Optionally seed the StackFrame with the SymbolContext information that
  ///   has
  ///   already been discovered.
  StackFrame(const lldb::ThreadSP &thread_sp, lldb::user_id_t frame_idx,
             lldb::user_id_t concrete_frame_idx, lldb::addr_t cfa,
             bool cfa_is_valid, lldb::addr_t pc, Kind frame_kind,
             bool behaves_like_zeroth_frame, const SymbolContext *sc_ptr);

  StackFrame(const lldb::ThreadSP &thread_sp, lldb::user_id_t frame_idx,
             lldb::user_id_t concrete_frame_idx,
             const lldb::RegisterContextSP &reg_context_sp, lldb::addr_t cfa,
             lldb::addr_t pc, bool behaves_like_zeroth_frame,
             const SymbolContext *sc_ptr);

  StackFrame(const lldb::ThreadSP &thread_sp, lldb::user_id_t frame_idx,
             lldb::user_id_t concrete_frame_idx,
             const lldb::RegisterContextSP &reg_context_sp, lldb::addr_t cfa,
             const Address &pc, bool behaves_like_zeroth_frame,
             const SymbolContext *sc_ptr);

  ~StackFrame() override;

  lldb::ThreadSP GetThread() const { return m_thread_wp.lock(); }

  StackID &GetStackID();

  /// Get an Address for the current pc value in this StackFrame.
  ///
  /// May not be the same as the actual PC value for inlined stack frames.
  ///
  /// \return
  ///   The Address object set to the current PC value.
  const Address &GetFrameCodeAddress();

  /// Get the current code Address suitable for symbolication,
  /// may not be the same as GetFrameCodeAddress().
  ///
  /// For a frame in the middle of the stack, the return-pc is the
  /// current code address, but for symbolication purposes the
  /// return address after a noreturn call may point to the next
  /// function, a DWARF location list entry that is a completely
  /// different code path, or the wrong source line.
  ///
  /// The address returned should be used for symbolication (source line,
  /// block, function, DWARF location entry selection) but should NOT
  /// be shown to the user.  It may not point to an actual instruction
  /// boundary.
  ///
  /// \return
  ///   The Address object set to the current PC value.
  Address GetFrameCodeAddressForSymbolication();

  /// Change the pc value for a given thread.
  ///
  /// Change the current pc value for the frame on this thread.
  ///
  /// \param[in] pc
  ///     The load address that the pc will be set to.
  ///
  /// \return
  ///     true if the pc was changed.  false if this failed -- possibly
  ///     because this frame is not a live StackFrame.
  bool ChangePC(lldb::addr_t pc);

  /// Provide a SymbolContext for this StackFrame's current pc value.
  ///
  /// The StackFrame maintains this SymbolContext and adds additional
  /// information to it on an as-needed basis.  This helps to avoid different
  /// functions looking up symbolic information for a given pc value multiple
  /// times.
  ///
  /// \param [in] resolve_scope
  ///   Flags from the SymbolContextItem enumerated type which specify what
  ///   type of symbol context is needed by this caller.
  ///
  /// \return
  ///   A SymbolContext reference which includes the types of information
  ///   requested by resolve_scope, if they are available.
  const SymbolContext &GetSymbolContext(lldb::SymbolContextItem resolve_scope);

  /// Return the Canonical Frame Address (DWARF term) for this frame.
  ///
  /// The CFA is typically the value of the stack pointer register before the
  /// call invocation is made.  It will not change during the lifetime of a
  /// stack frame.  It is often not the same thing as the frame pointer
  /// register value.
  ///
  /// Live StackFrames will always have a CFA but other types of frames may
  /// not be able to supply one.
  ///
  /// \param [out] value
  ///   The address of the CFA for this frame, if available.
  ///
  /// \param [out] error_ptr
  ///   If there is an error determining the CFA address, this may contain a
  ///   string explaining the failure.
  ///
  /// \return
  ///   Returns true if the CFA value was successfully set in value.  Some
  ///   frames may be unable to provide this value; they will return false.
  bool GetFrameBaseValue(Scalar &value, Status *error_ptr);

  /// Get the DWARFExpressionList corresponding to the Canonical Frame Address.
  ///
  /// Often a register (bp), but sometimes a register + offset.
  ///
  /// \param [out] error_ptr
  ///   If there is an error determining the CFA address, this may contain a
  ///   string explaining the failure.
  ///
  /// \return
  ///   Returns the corresponding DWARF expression, or NULL.
  DWARFExpressionList *GetFrameBaseExpression(Status *error_ptr);

  /// Get the current lexical scope block for this StackFrame, if possible.
  ///
  /// If debug information is available for this stack frame, return a pointer
  /// to the innermost lexical Block that the frame is currently executing.
  ///
  /// \return
  ///   A pointer to the current Block.  nullptr is returned if this can
  ///   not be provided.
  Block *GetFrameBlock();

  /// Get the RegisterContext for this frame, if possible.
  ///
  /// Returns a shared pointer to the RegisterContext for this stack frame.
  /// Only a live StackFrame object will be able to return a RegisterContext -
  /// callers must be prepared for an empty shared pointer being returned.
  ///
  /// Even a live StackFrame RegisterContext may not be able to provide all
  /// registers.  Only the currently executing frame (frame 0) can reliably
  /// provide every register in the register context.
  ///
  /// \return
  ///   The RegisterContext shared point for this frame.
  lldb::RegisterContextSP GetRegisterContext();

  const lldb::RegisterContextSP &GetRegisterContextSP() const {
    return m_reg_context_sp;
  }

  /// Retrieve the list of variables that are in scope at this StackFrame's
  /// pc.
  ///
  /// A frame that is not live may return an empty VariableList for a given
  /// pc value even though variables would be available at this point if it
  /// were a live stack frame.
  ///
  /// \param[in] get_file_globals
  ///     Whether to also retrieve compilation-unit scoped variables
  ///     that are visible to the entire compilation unit (e.g. file
  ///     static in C, globals that are homed in this CU).
  ///
  /// \param [out] error_ptr
  ///   If there is an error in the debug information that prevents variables
  ///   from being fetched. \see SymbolFile::GetFrameVariableError() for full
  ///   details.
  ///
  /// \return
  ///     A pointer to a list of variables.
  VariableList *GetVariableList(bool get_file_globals, Status *error_ptr);

  /// Retrieve the list of variables that are in scope at this StackFrame's
  /// pc.
  ///
  /// A frame that is not live may return an empty VariableListSP for a
  /// given pc value even though variables would be available at this point if
  /// it were a live stack frame.
  ///
  /// \param[in] get_file_globals
  ///     Whether to also retrieve compilation-unit scoped variables
  ///     that are visible to the entire compilation unit (e.g. file
  ///     static in C, globals that are homed in this CU).
  ///
  /// \return
  ///     A pointer to a list of variables.
  lldb::VariableListSP
  GetInScopeVariableList(bool get_file_globals,
                         bool must_have_valid_location = false);

  /// Create a ValueObject for a variable name / pathname, possibly including
  /// simple dereference/child selection syntax.
  ///
  /// \param[in] var_expr
  ///     The string specifying a variable to base the VariableObject off
  ///     of.
  ///
  /// \param[in] use_dynamic
  ///     Whether the correct dynamic type of an object pointer should be
  ///     determined before creating the object, or if the static type is
  ///     sufficient.  One of the DynamicValueType enumerated values.
  ///
  /// \param[in] options
  ///     An unsigned integer of flags, values from
  ///     StackFrame::ExpressionPathOption
  ///     enum.
  /// \param[in] var_sp
  ///     A VariableSP that will be set to the variable described in the
  ///     var_expr path.
  ///
  /// \param[in] error
  ///     Record any errors encountered while evaluating var_expr.
  ///
  /// \return
  ///     A shared pointer to the ValueObject described by var_expr.
  lldb::ValueObjectSP GetValueForVariableExpressionPath(
      llvm::StringRef var_expr, lldb::DynamicValueType use_dynamic,
      uint32_t options, lldb::VariableSP &var_sp, Status &error);

  /// Determine whether this StackFrame has debug information available or not.
  ///
  /// \return
  ///    true if debug information is available for this frame (function,
  ///    compilation unit, block, etc.)
  bool HasDebugInformation();

  /// Return the disassembly for the instructions of this StackFrame's
  /// function as a single C string.
  ///
  /// \return
  ///    C string with the assembly instructions for this function.
  const char *Disassemble();

  /// Print a description of this frame using the provided frame format.
  ///
  /// \param[out] strm
  ///   The Stream to print the description to.
  ///
  /// \param[in] frame_marker
  ///   Optional string that will be prepended to the frame output description.
  ///
  /// \return
  ///   \b true if and only if dumping with the given \p format worked.
  bool DumpUsingFormat(Stream &strm,
                       const lldb_private::FormatEntity::Entry *format,
                       llvm::StringRef frame_marker = {});

  /// Print a description for this frame using the frame-format formatter
  /// settings. If the current frame-format settings are invalid, then the
  /// default formatter will be used (see \a StackFrame::Dump()).
  ///
  /// \param [in] strm
  ///   The Stream to print the description to.
  ///
  /// \param [in] show_unique
  ///   Whether to print the function arguments or not for backtrace unique.
  ///
  /// \param [in] frame_marker
  ///   Optional string that will be prepended to the frame output description.
  void DumpUsingSettingsFormat(Stream *strm, bool show_unique = false,
                               const char *frame_marker = nullptr);

  /// Print a description for this frame using a default format.
  ///
  /// \param [in] strm
  ///   The Stream to print the description to.
  ///
  /// \param [in] show_frame_index
  ///   Whether to print the frame number or not.
  ///
  /// \param [in] show_fullpaths
  ///   Whether to print the full source paths or just the file base name.
  void Dump(Stream *strm, bool show_frame_index, bool show_fullpaths);

  /// Print a description of this stack frame and/or the source
  /// context/assembly for this stack frame.
  ///
  /// \param[in] strm
  ///   The Stream to send the output to.
  ///
  /// \param[in] show_frame_info
  ///   If true, print the frame info by calling DumpUsingSettingsFormat().
  ///
  /// \param[in] show_source
  ///   If true, print source or disassembly as per the user's settings.
  ///
  /// \param[in] show_unique
  ///   If true, print using backtrace unique style, without function
  ///            arguments as per the user's settings.
  ///
  /// \param[in] frame_marker
  ///   Passed to DumpUsingSettingsFormat() for the frame info printing.
  ///
  /// \return
  ///   Returns true if successful.
  bool GetStatus(Stream &strm, bool show_frame_info, bool show_source,
                 bool show_unique = false, const char *frame_marker = nullptr);

  /// Query whether this frame is a concrete frame on the call stack, or if it
  /// is an inlined frame derived from the debug information and presented by
  /// the debugger.
  ///
  /// \return
  ///   true if this is an inlined frame.
  bool IsInlined();

  /// Query whether this frame is part of a historical backtrace.
  bool IsHistorical() const;

  /// Query whether this frame is artificial (e.g a synthesized result of
  /// inferring missing tail call frames from a backtrace). Artificial frames
  /// may have limited support for inspecting variables.
  bool IsArtificial() const;

  /// Query this frame to find what frame it is in this Thread's
  /// StackFrameList.
  ///
  /// \return
  ///   StackFrame index 0 indicates the currently-executing function.  Inline
  ///   frames are included in this frame index count.
  uint32_t GetFrameIndex() const;

  /// Set this frame's synthetic frame index.
  void SetFrameIndex(uint32_t index) { m_frame_index = index; }

  /// Query this frame to find what frame it is in this Thread's
  /// StackFrameList, not counting inlined frames.
  ///
  /// \return
  ///   StackFrame index 0 indicates the currently-executing function.  Inline
  ///   frames are not included in this frame index count; their concrete
  ///   frame index will be the same as the concrete frame that they are
  ///   derived from.
  uint32_t GetConcreteFrameIndex() const { return m_concrete_frame_index; }

  /// Create a ValueObject for a given Variable in this StackFrame.
  ///
  /// \param [in] variable_sp
  ///   The Variable to base this ValueObject on
  ///
  /// \param [in] use_dynamic
  ///     Whether the correct dynamic type of the variable should be
  ///     determined before creating the ValueObject, or if the static type
  ///     is sufficient.  One of the DynamicValueType enumerated values.
  ///
  /// \return
  ///     A ValueObject for this variable.
  lldb::ValueObjectSP
  GetValueObjectForFrameVariable(const lldb::VariableSP &variable_sp,
                                 lldb::DynamicValueType use_dynamic);

  /// Query this frame to determine what the default language should be when
  /// parsing expressions given the execution context.
  ///
  /// \return   The language of the frame if known.
  SourceLanguage GetLanguage();

  /// Similar to GetLanguage(), but is allowed to take a potentially incorrect
  /// guess if exact information is not available.
  SourceLanguage GuessLanguage();

  /// Attempt to econstruct the ValueObject for a given raw address touched by
  /// the current instruction.  The ExpressionPath should indicate how to get
  /// to this value using "frame variable."
  ///
  /// \param [in] addr
  ///   The raw address.
  ///
  /// \return
  ///   The ValueObject if found.  If valid, it has a valid ExpressionPath.
  lldb::ValueObjectSP GuessValueForAddress(lldb::addr_t addr);

  /// Attempt to reconstruct the ValueObject for the address contained in a
  /// given register plus an offset.  The ExpressionPath should indicate how
  /// to get to this value using "frame variable."
  ///
  /// \param [in] reg
  ///   The name of the register.
  ///
  /// \param [in] offset
  ///   The offset from the register.  Particularly important for sp...
  ///
  /// \return
  ///   The ValueObject if found.  If valid, it has a valid ExpressionPath.
  lldb::ValueObjectSP GuessValueForRegisterAndOffset(ConstString reg,
                                                     int64_t offset);

  /// Attempt to reconstruct the ValueObject for a variable with a given \a name
  /// from within the current StackFrame, within the current block. The search
  /// for the variable starts in the deepest block corresponding to the current
  /// PC in the stack frame and traverse through all parent blocks stopping at
  /// inlined function boundaries.
  ///
  /// \param [in] name
  ///   The name of the variable.
  ///
  /// \return
  ///   The ValueObject if found.
  lldb::ValueObjectSP FindVariable(ConstString name);

  // lldb::ExecutionContextScope pure virtual functions
  lldb::TargetSP CalculateTarget() override;

  lldb::ProcessSP CalculateProcess() override;

  lldb::ThreadSP CalculateThread() override;

  lldb::StackFrameSP CalculateStackFrame() override;

  void CalculateExecutionContext(ExecutionContext &exe_ctx) override;

  lldb::RecognizedStackFrameSP GetRecognizedFrame();

protected:
  friend class StackFrameList;

  void SetSymbolContextScope(SymbolContextScope *symbol_scope);

  void UpdateCurrentFrameFromPreviousFrame(StackFrame &prev_frame);

  void UpdatePreviousFrameFromCurrentFrame(StackFrame &curr_frame);

  bool HasCachedData() const;

private:
  // For StackFrame only
  lldb::ThreadWP m_thread_wp;
  uint32_t m_frame_index;
  uint32_t m_concrete_frame_index;
  lldb::RegisterContextSP m_reg_context_sp;
  StackID m_id;
  Address m_frame_code_addr; // The frame code address (might not be the same as
                             // the actual PC for inlined frames) as a
                             // section/offset address
  SymbolContext m_sc;
  Flags m_flags;
  Scalar m_frame_base;
  Status m_frame_base_error;
  bool m_cfa_is_valid; // Does this frame have a CFA?  Different from CFA ==
                       // LLDB_INVALID_ADDRESS
  Kind m_stack_frame_kind;

  // Whether this frame behaves like the zeroth frame, in the sense
  // that its pc value might not immediately follow a call (and thus might
  // be the first address of its function). True for actual frame zero as
  // well as any other frame with the same trait.
  bool m_behaves_like_zeroth_frame;
  lldb::VariableListSP m_variable_list_sp;
  ValueObjectList m_variable_list_value_objects; // Value objects for each
                                                 // variable in
                                                 // m_variable_list_sp
  lldb::RecognizedStackFrameSP m_recognized_frame_sp;
  StreamString m_disassembly;
  std::recursive_mutex m_mutex;

  StackFrame(const StackFrame &) = delete;
  const StackFrame &operator=(const StackFrame &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_STACKFRAME_H
