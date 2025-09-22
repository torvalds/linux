//===-- ValueObject.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECT_H
#define LLDB_CORE_VALUEOBJECT_H

#include "lldb/Core/Value.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/SharedCluster.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace lldb_private {
class Declaration;
class DumpValueObjectOptions;
class EvaluateExpressionOptions;
class ExecutionContextScope;
class Log;
class Scalar;
class Stream;
class SymbolContextScope;
class TypeFormatImpl;
class TypeSummaryImpl;
class TypeSummaryOptions;

/// ValueObject:
///
/// This abstract class provides an interface to a particular value, be it a
/// register, a local or global variable,
/// that is evaluated in some particular scope.  The ValueObject also has the
/// capability of being the "child" of
/// some other variable object, and in turn of having children.
/// If a ValueObject is a root variable object - having no parent - then it must
/// be constructed with respect to some
/// particular ExecutionContextScope.  If it is a child, it inherits the
/// ExecutionContextScope from its parent.
/// The ValueObject will update itself if necessary before fetching its value,
/// summary, object description, etc.
/// But it will always update itself in the ExecutionContextScope with which it
/// was originally created.

/// A brief note on life cycle management for ValueObjects.  This is a little
/// tricky because a ValueObject can contain
/// various other ValueObjects - the Dynamic Value, its children, the
/// dereference value, etc.  Any one of these can be
/// handed out as a shared pointer, but for that contained value object to be
/// valid, the root object and potentially other
/// of the value objects need to stay around.
/// We solve this problem by handing out shared pointers to the Value Object and
/// any of its dependents using a shared
/// ClusterManager.  This treats each shared pointer handed out for the entire
/// cluster as a reference to the whole
/// cluster.  The whole cluster will stay around until the last reference is
/// released.
///
/// The ValueObject mostly handle this automatically, if a value object is made
/// with a Parent ValueObject, then it adds
/// itself to the ClusterManager of the parent.

/// It does mean that external to the ValueObjects we should only ever make
/// available ValueObjectSP's, never ValueObjects
/// or pointers to them.  So all the "Root level" ValueObject derived
/// constructors should be private, and
/// should implement a Create function that new's up object and returns a Shared
/// Pointer that it gets from the GetSP() method.
///
/// However, if you are making an derived ValueObject that will be contained in
/// a parent value object, you should just
/// hold onto a pointer to it internally, and by virtue of passing the parent
/// ValueObject into its constructor, it will
/// be added to the ClusterManager for the parent.  Then if you ever hand out a
/// Shared Pointer to the contained ValueObject,
/// just do so by calling GetSP() on the contained object.

class ValueObject {
public:
  enum GetExpressionPathFormat {
    eGetExpressionPathFormatDereferencePointers = 1,
    eGetExpressionPathFormatHonorPointers
  };

  enum ValueObjectRepresentationStyle {
    eValueObjectRepresentationStyleValue = 1,
    eValueObjectRepresentationStyleSummary,
    eValueObjectRepresentationStyleLanguageSpecific,
    eValueObjectRepresentationStyleLocation,
    eValueObjectRepresentationStyleChildrenCount,
    eValueObjectRepresentationStyleType,
    eValueObjectRepresentationStyleName,
    eValueObjectRepresentationStyleExpressionPath
  };

  enum ExpressionPathScanEndReason {
    /// Out of data to parse.
    eExpressionPathScanEndReasonEndOfString = 1,
    /// Child element not found.
    eExpressionPathScanEndReasonNoSuchChild,
    /// (Synthetic) child  element not found.
    eExpressionPathScanEndReasonNoSuchSyntheticChild,
    /// [] only allowed for arrays.
    eExpressionPathScanEndReasonEmptyRangeNotAllowed,
    /// . used when -> should be used.
    eExpressionPathScanEndReasonDotInsteadOfArrow,
    /// -> used when . should be used.
    eExpressionPathScanEndReasonArrowInsteadOfDot,
    /// ObjC ivar expansion not allowed.
    eExpressionPathScanEndReasonFragileIVarNotAllowed,
    /// [] not allowed by options.
    eExpressionPathScanEndReasonRangeOperatorNotAllowed,
    /// [] not valid on objects  other than scalars, pointers or arrays.
    eExpressionPathScanEndReasonRangeOperatorInvalid,
    /// [] is good for arrays,  but I cannot parse it.
    eExpressionPathScanEndReasonArrayRangeOperatorMet,
    /// [] is good for bitfields, but I cannot parse after it.
    eExpressionPathScanEndReasonBitfieldRangeOperatorMet,
    /// Something is malformed in he expression.
    eExpressionPathScanEndReasonUnexpectedSymbol,
    /// Impossible to apply &  operator.
    eExpressionPathScanEndReasonTakingAddressFailed,
    /// Impossible to apply *  operator.
    eExpressionPathScanEndReasonDereferencingFailed,
    /// [] was expanded into a  VOList.
    eExpressionPathScanEndReasonRangeOperatorExpanded,
    /// getting the synthetic children failed.
    eExpressionPathScanEndReasonSyntheticValueMissing,
    eExpressionPathScanEndReasonUnknown = 0xFFFF
  };

  enum ExpressionPathEndResultType {
    /// Anything but...
    eExpressionPathEndResultTypePlain = 1,
    /// A bitfield.
    eExpressionPathEndResultTypeBitfield,
    /// A range [low-high].
    eExpressionPathEndResultTypeBoundedRange,
    /// A range [].
    eExpressionPathEndResultTypeUnboundedRange,
    /// Several items in a VOList.
    eExpressionPathEndResultTypeValueObjectList,
    eExpressionPathEndResultTypeInvalid = 0xFFFF
  };

  enum ExpressionPathAftermath {
    /// Just return it.
    eExpressionPathAftermathNothing = 1,
    /// Dereference the target.
    eExpressionPathAftermathDereference,
    /// Take target's address.
    eExpressionPathAftermathTakeAddress
  };

  enum ClearUserVisibleDataItems {
    eClearUserVisibleDataItemsNothing = 1u << 0,
    eClearUserVisibleDataItemsValue = 1u << 1,
    eClearUserVisibleDataItemsSummary = 1u << 2,
    eClearUserVisibleDataItemsLocation = 1u << 3,
    eClearUserVisibleDataItemsDescription = 1u << 4,
    eClearUserVisibleDataItemsSyntheticChildren = 1u << 5,
    eClearUserVisibleDataItemsAllStrings =
        eClearUserVisibleDataItemsValue | eClearUserVisibleDataItemsSummary |
        eClearUserVisibleDataItemsLocation |
        eClearUserVisibleDataItemsDescription,
    eClearUserVisibleDataItemsAll = 0xFFFF
  };

  struct GetValueForExpressionPathOptions {
    enum class SyntheticChildrenTraversal {
      None,
      ToSynthetic,
      FromSynthetic,
      Both
    };

    bool m_check_dot_vs_arrow_syntax;
    bool m_no_fragile_ivar;
    bool m_allow_bitfields_syntax;
    SyntheticChildrenTraversal m_synthetic_children_traversal;

    GetValueForExpressionPathOptions(
        bool dot = false, bool no_ivar = false, bool bitfield = true,
        SyntheticChildrenTraversal synth_traverse =
            SyntheticChildrenTraversal::ToSynthetic)
        : m_check_dot_vs_arrow_syntax(dot), m_no_fragile_ivar(no_ivar),
          m_allow_bitfields_syntax(bitfield),
          m_synthetic_children_traversal(synth_traverse) {}

    GetValueForExpressionPathOptions &DoCheckDotVsArrowSyntax() {
      m_check_dot_vs_arrow_syntax = true;
      return *this;
    }

    GetValueForExpressionPathOptions &DontCheckDotVsArrowSyntax() {
      m_check_dot_vs_arrow_syntax = false;
      return *this;
    }

    GetValueForExpressionPathOptions &DoAllowFragileIVar() {
      m_no_fragile_ivar = false;
      return *this;
    }

    GetValueForExpressionPathOptions &DontAllowFragileIVar() {
      m_no_fragile_ivar = true;
      return *this;
    }

    GetValueForExpressionPathOptions &DoAllowBitfieldSyntax() {
      m_allow_bitfields_syntax = true;
      return *this;
    }

    GetValueForExpressionPathOptions &DontAllowBitfieldSyntax() {
      m_allow_bitfields_syntax = false;
      return *this;
    }

    GetValueForExpressionPathOptions &
    SetSyntheticChildrenTraversal(SyntheticChildrenTraversal traverse) {
      m_synthetic_children_traversal = traverse;
      return *this;
    }

    static const GetValueForExpressionPathOptions DefaultOptions() {
      static GetValueForExpressionPathOptions g_default_options;

      return g_default_options;
    }
  };

  class EvaluationPoint {
  public:
    EvaluationPoint();

    EvaluationPoint(ExecutionContextScope *exe_scope,
                    bool use_selected = false);

    EvaluationPoint(const EvaluationPoint &rhs);

    ~EvaluationPoint();

    const ExecutionContextRef &GetExecutionContextRef() const {
      return m_exe_ctx_ref;
    }

    void SetIsConstant() {
      SetUpdated();
      m_mod_id.SetInvalid();
    }

    bool IsConstant() const { return !m_mod_id.IsValid(); }

    ProcessModID GetModID() const { return m_mod_id; }

    void SetUpdateID(ProcessModID new_id) { m_mod_id = new_id; }

    void SetNeedsUpdate() { m_needs_update = true; }

    void SetUpdated();

    bool NeedsUpdating(bool accept_invalid_exe_ctx) {
      SyncWithProcessState(accept_invalid_exe_ctx);
      return m_needs_update;
    }

    bool IsValid() {
      const bool accept_invalid_exe_ctx = false;
      if (!m_mod_id.IsValid())
        return false;
      else if (SyncWithProcessState(accept_invalid_exe_ctx)) {
        if (!m_mod_id.IsValid())
          return false;
      }
      return true;
    }

    void SetInvalid() {
      // Use the stop id to mark us as invalid, leave the thread id and the
      // stack id around for logging and history purposes.
      m_mod_id.SetInvalid();

      // Can't update an invalid state.
      m_needs_update = false;
    }

  private:
    bool SyncWithProcessState(bool accept_invalid_exe_ctx);

    ProcessModID m_mod_id; // This is the stop id when this ValueObject was last
                           // evaluated.
    ExecutionContextRef m_exe_ctx_ref;
    bool m_needs_update = true;
  };

  virtual ~ValueObject();

  const EvaluationPoint &GetUpdatePoint() const { return m_update_point; }

  EvaluationPoint &GetUpdatePoint() { return m_update_point; }

  const ExecutionContextRef &GetExecutionContextRef() const {
    return m_update_point.GetExecutionContextRef();
  }

  lldb::TargetSP GetTargetSP() const {
    return m_update_point.GetExecutionContextRef().GetTargetSP();
  }

  lldb::ProcessSP GetProcessSP() const {
    return m_update_point.GetExecutionContextRef().GetProcessSP();
  }

  lldb::ThreadSP GetThreadSP() const {
    return m_update_point.GetExecutionContextRef().GetThreadSP();
  }

  lldb::StackFrameSP GetFrameSP() const {
    return m_update_point.GetExecutionContextRef().GetFrameSP();
  }

  void SetNeedsUpdate();

  CompilerType GetCompilerType() { return MaybeCalculateCompleteType(); }

  // this vends a TypeImpl that is useful at the SB API layer
  virtual TypeImpl GetTypeImpl() { return TypeImpl(GetCompilerType()); }

  virtual bool CanProvideValue();

  // Subclasses must implement the functions below.
  virtual std::optional<uint64_t> GetByteSize() = 0;

  virtual lldb::ValueType GetValueType() const = 0;

  // Subclasses can implement the functions below.
  virtual ConstString GetTypeName() { return GetCompilerType().GetTypeName(); }

  virtual ConstString GetDisplayTypeName() { return GetTypeName(); }

  virtual ConstString GetQualifiedTypeName() {
    return GetCompilerType().GetTypeName();
  }

  lldb::LanguageType GetObjectRuntimeLanguage() {
    return GetCompilerType().GetMinimumLanguage();
  }

  uint32_t
  GetTypeInfo(CompilerType *pointee_or_element_compiler_type = nullptr) {
    return GetCompilerType().GetTypeInfo(pointee_or_element_compiler_type);
  }

  bool IsPointerType() { return GetCompilerType().IsPointerType(); }

  bool IsArrayType() { return GetCompilerType().IsArrayType(); }

  bool IsScalarType() { return GetCompilerType().IsScalarType(); }

  bool IsPointerOrReferenceType() {
    return GetCompilerType().IsPointerOrReferenceType();
  }

  bool IsPossibleDynamicType();

  bool IsNilReference();

  bool IsUninitializedReference();

  virtual bool IsBaseClass() { return false; }

  bool IsBaseClass(uint32_t &depth);

  virtual bool IsDereferenceOfParent() { return false; }

  bool IsIntegerType(bool &is_signed) {
    return GetCompilerType().IsIntegerType(is_signed);
  }

  virtual void GetExpressionPath(
      Stream &s,
      GetExpressionPathFormat = eGetExpressionPathFormatDereferencePointers);

  lldb::ValueObjectSP GetValueForExpressionPath(
      llvm::StringRef expression,
      ExpressionPathScanEndReason *reason_to_stop = nullptr,
      ExpressionPathEndResultType *final_value_type = nullptr,
      const GetValueForExpressionPathOptions &options =
          GetValueForExpressionPathOptions::DefaultOptions(),
      ExpressionPathAftermath *final_task_on_target = nullptr);

  virtual bool IsInScope() { return true; }

  virtual lldb::offset_t GetByteOffset() { return 0; }

  virtual uint32_t GetBitfieldBitSize() { return 0; }

  virtual uint32_t GetBitfieldBitOffset() { return 0; }

  bool IsBitfield() {
    return (GetBitfieldBitSize() != 0) || (GetBitfieldBitOffset() != 0);
  }

  virtual const char *GetValueAsCString();

  virtual bool GetValueAsCString(const lldb_private::TypeFormatImpl &format,
                                 std::string &destination);

  bool GetValueAsCString(lldb::Format format, std::string &destination);

  virtual uint64_t GetValueAsUnsigned(uint64_t fail_value,
                                      bool *success = nullptr);

  virtual int64_t GetValueAsSigned(int64_t fail_value, bool *success = nullptr);

  /// If the current ValueObject is of an appropriate type, convert the
  /// value to an APSInt and return that. Otherwise return an error.
  llvm::Expected<llvm::APSInt> GetValueAsAPSInt();

  /// If the current ValueObject is of an appropriate type, convert the
  /// value to an APFloat and return that. Otherwise return an error.
  llvm::Expected<llvm::APFloat> GetValueAsAPFloat();

  /// If the current ValueObject is of an appropriate type, convert the
  /// value to a boolean and return that. Otherwise return an error.
  llvm::Expected<bool> GetValueAsBool();

  /// Update an existing integer ValueObject with a new integer value. This
  /// is only intended to be used with 'temporary' ValueObjects, i.e. ones that
  /// are not associated with program variables. It does not update program
  /// memory, registers, stack, etc.
  void SetValueFromInteger(const llvm::APInt &value, Status &error);

  /// Update an existing integer ValueObject with an integer value created
  /// frome 'new_val_sp'.  This is only intended to be used with 'temporary'
  /// ValueObjects, i.e. ones that are not associated with program variables.
  /// It does not update program  memory, registers, stack, etc.
  void SetValueFromInteger(lldb::ValueObjectSP new_val_sp, Status &error);

  virtual bool SetValueFromCString(const char *value_str, Status &error);

  /// Return the module associated with this value object in case the value is
  /// from an executable file and might have its data in sections of the file.
  /// This can be used for variables.
  virtual lldb::ModuleSP GetModule();

  ValueObject *GetRoot();

  /// Given a ValueObject, loop over itself and its parent, and its parent's
  /// parent, .. until either the given callback returns false, or you end up at
  /// a null pointer
  ValueObject *FollowParentChain(std::function<bool(ValueObject *)>);

  virtual bool GetDeclaration(Declaration &decl);

  // The functions below should NOT be modified by subclasses
  const Status &GetError();

  ConstString GetName() const { return m_name; }

  /// Returns a unique id for this ValueObject.
  lldb::user_id_t GetID() const { return m_id.GetID(); }

  virtual lldb::ValueObjectSP GetChildAtIndex(uint32_t idx,
                                              bool can_create = true);

  // The method always creates missing children in the path, if necessary.
  lldb::ValueObjectSP GetChildAtNamePath(llvm::ArrayRef<llvm::StringRef> names);

  virtual lldb::ValueObjectSP GetChildMemberWithName(llvm::StringRef name,
                                                     bool can_create = true);

  virtual size_t GetIndexOfChildWithName(llvm::StringRef name);

  llvm::Expected<uint32_t> GetNumChildren(uint32_t max = UINT32_MAX);
  /// Like \c GetNumChildren but returns 0 on error.  You probably
  /// shouldn't be using this function. It exists primarily to ease the
  /// transition to more pervasive error handling while not all APIs
  /// have been updated.
  uint32_t GetNumChildrenIgnoringErrors(uint32_t max = UINT32_MAX);
  bool HasChildren() { return GetNumChildrenIgnoringErrors() > 0; }

  const Value &GetValue() const { return m_value; }

  Value &GetValue() { return m_value; }

  virtual bool ResolveValue(Scalar &scalar);

  // return 'false' whenever you set the error, otherwise callers may assume
  // true means everything is OK - this will break breakpoint conditions among
  // potentially a few others
  virtual bool IsLogicalTrue(Status &error);

  virtual const char *GetLocationAsCString() {
    return GetLocationAsCStringImpl(m_value, m_data);
  }

  const char *
  GetSummaryAsCString(lldb::LanguageType lang = lldb::eLanguageTypeUnknown);

  bool
  GetSummaryAsCString(TypeSummaryImpl *summary_ptr, std::string &destination,
                      lldb::LanguageType lang = lldb::eLanguageTypeUnknown);

  bool GetSummaryAsCString(std::string &destination,
                           const TypeSummaryOptions &options);

  bool GetSummaryAsCString(TypeSummaryImpl *summary_ptr,
                           std::string &destination,
                           const TypeSummaryOptions &options);

  llvm::Expected<std::string> GetObjectDescription();

  bool HasSpecialPrintableRepresentation(
      ValueObjectRepresentationStyle val_obj_display,
      lldb::Format custom_format);

  enum class PrintableRepresentationSpecialCases : bool {
    eDisable = false,
    eAllow = true
  };

  bool
  DumpPrintableRepresentation(Stream &s,
                              ValueObjectRepresentationStyle val_obj_display =
                                  eValueObjectRepresentationStyleSummary,
                              lldb::Format custom_format = lldb::eFormatInvalid,
                              PrintableRepresentationSpecialCases special =
                                  PrintableRepresentationSpecialCases::eAllow,
                              bool do_dump_error = true);
  bool GetValueIsValid() const { return m_flags.m_value_is_valid; }

  // If you call this on a newly created ValueObject, it will always return
  // false.
  bool GetValueDidChange() { return m_flags.m_value_did_change; }

  bool UpdateValueIfNeeded(bool update_format = true);

  bool UpdateFormatsIfNeeded();

  lldb::ValueObjectSP GetSP() { return m_manager->GetSharedPointer(this); }

  /// Change the name of the current ValueObject. Should *not* be used from a
  /// synthetic child provider as it would change the name of the non synthetic
  /// child as well.
  void SetName(ConstString name) { m_name = name; }

  virtual lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                                    AddressType *address_type = nullptr);

  lldb::addr_t GetPointerValue(AddressType *address_type = nullptr);

  lldb::ValueObjectSP GetSyntheticChild(ConstString key) const;

  lldb::ValueObjectSP GetSyntheticArrayMember(size_t index, bool can_create);

  lldb::ValueObjectSP GetSyntheticBitFieldChild(uint32_t from, uint32_t to,
                                                bool can_create);

  lldb::ValueObjectSP GetSyntheticExpressionPathChild(const char *expression,
                                                      bool can_create);

  virtual lldb::ValueObjectSP
  GetSyntheticChildAtOffset(uint32_t offset, const CompilerType &type,
                            bool can_create,
                            ConstString name_const_str = ConstString());

  virtual lldb::ValueObjectSP
  GetSyntheticBase(uint32_t offset, const CompilerType &type, bool can_create,
                   ConstString name_const_str = ConstString());

  virtual lldb::ValueObjectSP GetDynamicValue(lldb::DynamicValueType valueType);

  lldb::DynamicValueType GetDynamicValueType();

  virtual lldb::ValueObjectSP GetStaticValue() { return GetSP(); }

  virtual lldb::ValueObjectSP GetNonSyntheticValue() { return GetSP(); }

  lldb::ValueObjectSP GetSyntheticValue();

  virtual bool HasSyntheticValue();

  virtual bool IsSynthetic() { return false; }

  lldb::ValueObjectSP
  GetQualifiedRepresentationIfAvailable(lldb::DynamicValueType dynValue,
                                        bool synthValue);

  virtual lldb::ValueObjectSP CreateConstantValue(ConstString name);

  virtual lldb::ValueObjectSP Dereference(Status &error);

  /// Creates a copy of the ValueObject with a new name and setting the current
  /// ValueObject as its parent. It should be used when we want to change the
  /// name of a ValueObject without modifying the actual ValueObject itself
  /// (e.g. sythetic child provider).
  virtual lldb::ValueObjectSP Clone(ConstString new_name);

  virtual lldb::ValueObjectSP AddressOf(Status &error);

  virtual lldb::addr_t GetLiveAddress() { return LLDB_INVALID_ADDRESS; }

  virtual void SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                              AddressType address_type = eAddressTypeLoad) {}

  lldb::ValueObjectSP Cast(const CompilerType &compiler_type);

  virtual lldb::ValueObjectSP DoCast(const CompilerType &compiler_type);

  virtual lldb::ValueObjectSP CastPointerType(const char *name,
                                              CompilerType &ast_type);

  virtual lldb::ValueObjectSP CastPointerType(const char *name,
                                              lldb::TypeSP &type_sp);

  /// Return the target load address associated with this value object.
  lldb::addr_t GetLoadAddress();

  /// Take a ValueObject whose type is an inherited class, and cast it to
  /// 'type', which should be one of its base classes. 'base_type_indices'
  /// contains the indices of direct base classes on the path from the
  /// ValueObject's current type to 'type'
  llvm::Expected<lldb::ValueObjectSP>
  CastDerivedToBaseType(CompilerType type,
                        const llvm::ArrayRef<uint32_t> &base_type_indices);

  /// Take a ValueObject whose type is a base class, and cast it to 'type',
  /// which should be one of its derived classes. 'base_type_indices'
  /// contains the indices of direct base classes on the path from the
  /// ValueObject's current type to 'type'
  llvm::Expected<lldb::ValueObjectSP> CastBaseToDerivedType(CompilerType type,
                                                            uint64_t offset);

  // Take a ValueObject that contains a scalar, enum or pointer type, and
  // cast it to a "basic" type (integer, float or boolean).
  lldb::ValueObjectSP CastToBasicType(CompilerType type);

  // Take a ValueObject that contain an integer, float or enum, and cast it
  // to an enum.
  lldb::ValueObjectSP CastToEnumType(CompilerType type);

  /// If this object represents a C++ class with a vtable, return an object
  /// that represents the virtual function table. If the object isn't a class
  /// with a vtable, return a valid ValueObject with the error set correctly.
  lldb::ValueObjectSP GetVTable();
  // The backing bits of this value object were updated, clear any descriptive
  // string, so we know we have to refetch them.
  void ValueUpdated() {
    ClearUserVisibleData(eClearUserVisibleDataItemsValue |
                         eClearUserVisibleDataItemsSummary |
                         eClearUserVisibleDataItemsDescription);
  }

  virtual bool IsDynamic() { return false; }

  virtual bool DoesProvideSyntheticValue() { return false; }

  virtual bool IsSyntheticChildrenGenerated() {
    return m_flags.m_is_synthetic_children_generated;
  }

  virtual void SetSyntheticChildrenGenerated(bool b) {
    m_flags.m_is_synthetic_children_generated = b;
  }

  virtual SymbolContextScope *GetSymbolContextScope();

  llvm::Error Dump(Stream &s);

  llvm::Error Dump(Stream &s, const DumpValueObjectOptions &options);

  static lldb::ValueObjectSP
  CreateValueObjectFromExpression(llvm::StringRef name,
                                  llvm::StringRef expression,
                                  const ExecutionContext &exe_ctx);

  static lldb::ValueObjectSP
  CreateValueObjectFromExpression(llvm::StringRef name,
                                  llvm::StringRef expression,
                                  const ExecutionContext &exe_ctx,
                                  const EvaluateExpressionOptions &options);

  /// Given an address either create a value object containing the value at
  /// that address, or create a value object containing the address itself
  /// (pointer value), depending on whether the parameter 'do_deref' is true or
  /// false.
  static lldb::ValueObjectSP
  CreateValueObjectFromAddress(llvm::StringRef name, uint64_t address,
                               const ExecutionContext &exe_ctx,
                               CompilerType type, bool do_deref = true);

  static lldb::ValueObjectSP
  CreateValueObjectFromData(llvm::StringRef name, const DataExtractor &data,
                            const ExecutionContext &exe_ctx, CompilerType type);

  /// Create a value object containing the given APInt value.
  static lldb::ValueObjectSP CreateValueObjectFromAPInt(lldb::TargetSP target,
                                                        const llvm::APInt &v,
                                                        CompilerType type,
                                                        llvm::StringRef name);

  /// Create a value object containing the given APFloat value.
  static lldb::ValueObjectSP
  CreateValueObjectFromAPFloat(lldb::TargetSP target, const llvm::APFloat &v,
                               CompilerType type, llvm::StringRef name);

  /// Create a value object containing the given boolean value.
  static lldb::ValueObjectSP CreateValueObjectFromBool(lldb::TargetSP target,
                                                       bool value,
                                                       llvm::StringRef name);

  /// Create a nullptr value object with the specified type (must be a
  /// nullptr type).
  static lldb::ValueObjectSP CreateValueObjectFromNullptr(lldb::TargetSP target,
                                                          CompilerType type,
                                                          llvm::StringRef name);

  lldb::ValueObjectSP Persist();

  /// Returns true if this is a char* or a char[] if it is a char* and
  /// check_pointer is true, it also checks that the pointer is valid.
  bool IsCStringContainer(bool check_pointer = false);

  std::pair<size_t, bool>
  ReadPointedString(lldb::WritableDataBufferSP &buffer_sp, Status &error,
                    bool honor_array);

  virtual size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                                uint32_t item_count = 1);

  virtual uint64_t GetData(DataExtractor &data, Status &error);

  virtual bool SetData(DataExtractor &data, Status &error);

  virtual bool GetIsConstant() const { return m_update_point.IsConstant(); }

  bool NeedsUpdating() {
    const bool accept_invalid_exe_ctx =
        (CanUpdateWithInvalidExecutionContext() == eLazyBoolYes);
    return m_update_point.NeedsUpdating(accept_invalid_exe_ctx);
  }

  void SetIsConstant() { m_update_point.SetIsConstant(); }

  lldb::Format GetFormat() const;

  virtual void SetFormat(lldb::Format format) {
    if (format != m_format)
      ClearUserVisibleData(eClearUserVisibleDataItemsValue);
    m_format = format;
  }

  virtual lldb::LanguageType GetPreferredDisplayLanguage();

  void SetPreferredDisplayLanguage(lldb::LanguageType lt) {
    m_preferred_display_language = lt;
  }

  lldb::TypeSummaryImplSP GetSummaryFormat() {
    UpdateFormatsIfNeeded();
    return m_type_summary_sp;
  }

  void SetSummaryFormat(lldb::TypeSummaryImplSP format) {
    m_type_summary_sp = std::move(format);
    ClearUserVisibleData(eClearUserVisibleDataItemsSummary);
  }

  void SetDerefValobj(ValueObject *deref) { m_deref_valobj = deref; }

  ValueObject *GetDerefValobj() { return m_deref_valobj; }

  void SetValueFormat(lldb::TypeFormatImplSP format) {
    m_type_format_sp = std::move(format);
    ClearUserVisibleData(eClearUserVisibleDataItemsValue);
  }

  lldb::TypeFormatImplSP GetValueFormat() {
    UpdateFormatsIfNeeded();
    return m_type_format_sp;
  }

  void SetSyntheticChildren(const lldb::SyntheticChildrenSP &synth_sp) {
    if (synth_sp.get() == m_synthetic_children_sp.get())
      return;
    ClearUserVisibleData(eClearUserVisibleDataItemsSyntheticChildren);
    m_synthetic_children_sp = synth_sp;
  }

  lldb::SyntheticChildrenSP GetSyntheticChildren() {
    UpdateFormatsIfNeeded();
    return m_synthetic_children_sp;
  }

  // Use GetParent for display purposes, but if you want to tell the parent to
  // update itself then use m_parent.  The ValueObjectDynamicValue's parent is
  // not the correct parent for displaying, they are really siblings, so for
  // display it needs to route through to its grandparent.
  virtual ValueObject *GetParent() { return m_parent; }

  virtual const ValueObject *GetParent() const { return m_parent; }

  ValueObject *GetNonBaseClassParent();

  void SetAddressTypeOfChildren(AddressType at) {
    m_address_type_of_ptr_or_ref_children = at;
  }

  AddressType GetAddressTypeOfChildren();

  void SetHasCompleteType() {
    m_flags.m_did_calculate_complete_objc_class_type = true;
  }

  /// Find out if a ValueObject might have children.
  ///
  /// This call is much more efficient than CalculateNumChildren() as
  /// it doesn't need to complete the underlying type. This is designed
  /// to be used in a UI environment in order to detect if the
  /// disclosure triangle should be displayed or not.
  ///
  /// This function returns true for class, union, structure,
  /// pointers, references, arrays and more. Again, it does so without
  /// doing any expensive type completion.
  ///
  /// \return
  ///     Returns \b true if the ValueObject might have children, or \b
  ///     false otherwise.
  virtual bool MightHaveChildren();

  virtual lldb::VariableSP GetVariable() { return nullptr; }

  virtual bool IsRuntimeSupportValue();

  virtual uint64_t GetLanguageFlags() { return m_language_flags; }

  virtual void SetLanguageFlags(uint64_t flags) { m_language_flags = flags; }

protected:
  typedef ClusterManager<ValueObject> ValueObjectManager;

  class ChildrenManager {
  public:
    ChildrenManager() = default;

    bool HasChildAtIndex(size_t idx) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      return (m_children.find(idx) != m_children.end());
    }

    ValueObject *GetChildAtIndex(uint32_t idx) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      const auto iter = m_children.find(idx);
      return ((iter == m_children.end()) ? nullptr : iter->second);
    }

    void SetChildAtIndex(size_t idx, ValueObject *valobj) {
      // we do not need to be mutex-protected to make a pair
      ChildrenPair pair(idx, valobj);
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      m_children.insert(pair);
    }

    void SetChildrenCount(size_t count) { Clear(count); }

    size_t GetChildrenCount() { return m_children_count; }

    void Clear(size_t new_count = 0) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      m_children_count = new_count;
      m_children.clear();
    }

  private:
    typedef std::map<size_t, ValueObject *> ChildrenMap;
    typedef ChildrenMap::iterator ChildrenIterator;
    typedef ChildrenMap::value_type ChildrenPair;
    std::recursive_mutex m_mutex;
    ChildrenMap m_children;
    size_t m_children_count = 0;
  };

  // Classes that inherit from ValueObject can see and modify these

  /// The parent value object, or nullptr if this has no parent.
  ValueObject *m_parent = nullptr;
  /// The root of the hierarchy for this ValueObject (or nullptr if never
  /// calculated).
  ValueObject *m_root = nullptr;
  /// Stores both the stop id and the full context at which this value was last
  /// updated.  When we are asked to update the value object, we check whether
  /// the context & stop id are the same before updating.
  EvaluationPoint m_update_point;
  /// The name of this object.
  ConstString m_name;
  /// A data extractor that can be used to extract the value.
  DataExtractor m_data;
  Value m_value;
  /// An error object that can describe any errors that occur when updating
  /// values.
  Status m_error;
  /// Cached value string that will get cleared if/when the value is updated.
  std::string m_value_str;
  /// Cached old value string from the last time the value was gotten
  std::string m_old_value_str;
  /// Cached location string that will get cleared if/when the value is updated.
  std::string m_location_str;
  /// Cached summary string that will get cleared if/when the value is updated.
  std::string m_summary_str;
  /// Cached result of the "object printer". This differs from the summary
  /// in that the summary is consed up by us, the object_desc_string is builtin.
  std::string m_object_desc_str;
  /// If the type of the value object should be overridden, the type to impose.
  CompilerType m_override_type;

  /// This object is managed by the root object (any ValueObject that gets
  /// created without a parent.) The manager gets passed through all the
  /// generations of dependent objects, and will keep the whole cluster of
  /// objects alive as long as a shared pointer to any of them has been handed
  /// out. Shared pointers to value objects must always be made with the GetSP
  /// method.
  ValueObjectManager *m_manager = nullptr;

  ChildrenManager m_children;
  std::map<ConstString, ValueObject *> m_synthetic_children;

  ValueObject *m_dynamic_value = nullptr;
  ValueObject *m_synthetic_value = nullptr;
  ValueObject *m_deref_valobj = nullptr;

  /// We have to hold onto a shared  pointer to this one because it is created
  /// as an independent ValueObjectConstResult, which isn't managed by us.
  lldb::ValueObjectSP m_addr_of_valobj_sp;

  lldb::Format m_format = lldb::eFormatDefault;
  lldb::Format m_last_format = lldb::eFormatDefault;
  uint32_t m_last_format_mgr_revision = 0;
  lldb::TypeSummaryImplSP m_type_summary_sp;
  lldb::TypeFormatImplSP m_type_format_sp;
  lldb::SyntheticChildrenSP m_synthetic_children_sp;
  ProcessModID m_user_id_of_forced_summary;
  AddressType m_address_type_of_ptr_or_ref_children = eAddressTypeInvalid;

  llvm::SmallVector<uint8_t, 16> m_value_checksum;

  lldb::LanguageType m_preferred_display_language = lldb::eLanguageTypeUnknown;

  uint64_t m_language_flags = 0;

  /// Unique identifier for every value object.
  UserID m_id;

  // Utility class for initializing all bitfields in ValueObject's constructors.
  // FIXME: This could be done via default initializers once we have C++20.
  struct Bitflags {
    bool m_value_is_valid : 1, m_value_did_change : 1,
        m_children_count_valid : 1, m_old_value_valid : 1,
        m_is_deref_of_parent : 1, m_is_array_item_for_pointer : 1,
        m_is_bitfield_for_scalar : 1, m_is_child_at_offset : 1,
        m_is_getting_summary : 1, m_did_calculate_complete_objc_class_type : 1,
        m_is_synthetic_children_generated : 1;
    Bitflags() {
      m_value_is_valid = false;
      m_value_did_change = false;
      m_children_count_valid = false;
      m_old_value_valid = false;
      m_is_deref_of_parent = false;
      m_is_array_item_for_pointer = false;
      m_is_bitfield_for_scalar = false;
      m_is_child_at_offset = false;
      m_is_getting_summary = false;
      m_did_calculate_complete_objc_class_type = false;
      m_is_synthetic_children_generated = false;
    }
  } m_flags;

  friend class ValueObjectChild;
  friend class ExpressionVariable;     // For SetName
  friend class Target;                 // For SetName
  friend class ValueObjectConstResultImpl;
  friend class ValueObjectSynthetic; // For ClearUserVisibleData

  /// Use this constructor to create a "root variable object".  The ValueObject
  /// will be locked to this context through-out its lifespan.
  ValueObject(ExecutionContextScope *exe_scope, ValueObjectManager &manager,
              AddressType child_ptr_or_ref_addr_type = eAddressTypeLoad);

  /// Use this constructor to create a ValueObject owned by another ValueObject.
  /// It will inherit the ExecutionContext of its parent.
  ValueObject(ValueObject &parent);

  ValueObjectManager *GetManager() { return m_manager; }

  virtual bool UpdateValue() = 0;

  virtual LazyBool CanUpdateWithInvalidExecutionContext() {
    return eLazyBoolCalculate;
  }

  virtual void CalculateDynamicValue(lldb::DynamicValueType use_dynamic);

  virtual lldb::DynamicValueType GetDynamicValueTypeImpl() {
    return lldb::eNoDynamicValues;
  }

  virtual bool HasDynamicValueTypeInfo() { return false; }

  virtual void CalculateSyntheticValue();

  /// Should only be called by ValueObject::GetChildAtIndex().
  ///
  /// \return A ValueObject managed by this ValueObject's manager.
  virtual ValueObject *CreateChildAtIndex(size_t idx);

  /// Should only be called by ValueObject::GetSyntheticArrayMember().
  ///
  /// \return A ValueObject managed by this ValueObject's manager.
  virtual ValueObject *CreateSyntheticArrayMember(size_t idx);

  /// Should only be called by ValueObject::GetNumChildren().
  virtual llvm::Expected<uint32_t>
  CalculateNumChildren(uint32_t max = UINT32_MAX) = 0;

  void SetNumChildren(uint32_t num_children);

  void SetValueDidChange(bool value_changed) {
    m_flags.m_value_did_change = value_changed;
  }

  void SetValueIsValid(bool valid) { m_flags.m_value_is_valid = valid; }

  void ClearUserVisibleData(
      uint32_t items = ValueObject::eClearUserVisibleDataItemsAllStrings);

  void AddSyntheticChild(ConstString key, ValueObject *valobj);

  DataExtractor &GetDataExtractor();

  void ClearDynamicTypeInformation();

  // Subclasses must implement the functions below.

  virtual CompilerType GetCompilerTypeImpl() = 0;

  const char *GetLocationAsCStringImpl(const Value &value,
                                       const DataExtractor &data);

  bool IsChecksumEmpty() { return m_value_checksum.empty(); }

  void SetPreferredDisplayLanguageIfNeeded(lldb::LanguageType);

protected:
  virtual void DoUpdateChildrenAddressType(ValueObject &valobj){};

private:
  virtual CompilerType MaybeCalculateCompleteType();
  void UpdateChildrenAddressType() {
    GetRoot()->DoUpdateChildrenAddressType(*this);
  }

  lldb::ValueObjectSP GetValueForExpressionPath_Impl(
      llvm::StringRef expression_cstr,
      ExpressionPathScanEndReason *reason_to_stop,
      ExpressionPathEndResultType *final_value_type,
      const GetValueForExpressionPathOptions &options,
      ExpressionPathAftermath *final_task_on_target);

  ValueObject(const ValueObject &) = delete;
  const ValueObject &operator=(const ValueObject &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECT_H
