//===-- SBValue.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBVALUE_H
#define LLDB_API_SBVALUE_H

#include "lldb/API/SBData.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBType.h"

class ValueImpl;
class ValueLocker;

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBValue {
public:
  SBValue();

  SBValue(const lldb::SBValue &rhs);

  lldb::SBValue &operator=(const lldb::SBValue &rhs);

  ~SBValue();

  explicit operator bool() const;

  bool IsValid();

  void Clear();

  SBError GetError();

  lldb::user_id_t GetID();

  const char *GetName();

  const char *GetTypeName();

  const char *GetDisplayTypeName();

  size_t GetByteSize();

  bool IsInScope();

  lldb::Format GetFormat();

  void SetFormat(lldb::Format format);

  const char *GetValue();

  int64_t GetValueAsSigned(lldb::SBError &error, int64_t fail_value = 0);

  uint64_t GetValueAsUnsigned(lldb::SBError &error, uint64_t fail_value = 0);

  int64_t GetValueAsSigned(int64_t fail_value = 0);

  uint64_t GetValueAsUnsigned(uint64_t fail_value = 0);

  lldb::addr_t GetValueAsAddress();

  ValueType GetValueType();

  // If you call this on a newly created ValueObject, it will always return
  // false.
  bool GetValueDidChange();

  const char *GetSummary();

  const char *GetSummary(lldb::SBStream &stream,
                         lldb::SBTypeSummaryOptions &options);

  const char *GetObjectDescription();

  lldb::SBValue GetDynamicValue(lldb::DynamicValueType use_dynamic);

  lldb::SBValue GetStaticValue();

  lldb::SBValue GetNonSyntheticValue();

  lldb::SBValue GetSyntheticValue();

  lldb::DynamicValueType GetPreferDynamicValue();

  void SetPreferDynamicValue(lldb::DynamicValueType use_dynamic);

  bool GetPreferSyntheticValue();

  void SetPreferSyntheticValue(bool use_synthetic);

  bool IsDynamic();

  bool IsSynthetic();

  bool IsSyntheticChildrenGenerated();

  void SetSyntheticChildrenGenerated(bool);

  const char *GetLocation();

  LLDB_DEPRECATED_FIXME("Use the variant that takes an SBError &",
                        "SetValueFromCString(const char *, SBError &)")
  bool SetValueFromCString(const char *value_str);

  bool SetValueFromCString(const char *value_str, lldb::SBError &error);

  lldb::SBTypeFormat GetTypeFormat();

  lldb::SBTypeSummary GetTypeSummary();

  lldb::SBTypeFilter GetTypeFilter();

  lldb::SBTypeSynthetic GetTypeSynthetic();

  lldb::SBValue GetChildAtIndex(uint32_t idx);

  lldb::SBValue CreateChildAtOffset(const char *name, uint32_t offset,
                                    lldb::SBType type);

  LLDB_DEPRECATED("Use the expression evaluator to perform type casting")
  lldb::SBValue Cast(lldb::SBType type);

  lldb::SBValue CreateValueFromExpression(const char *name,
                                          const char *expression);

  lldb::SBValue CreateValueFromExpression(const char *name,
                                          const char *expression,
                                          SBExpressionOptions &options);

  lldb::SBValue CreateValueFromAddress(const char *name, lldb::addr_t address,
                                       lldb::SBType type);

  // this has no address! GetAddress() and GetLoadAddress() as well as
  // AddressOf() on the return of this call all return invalid
  lldb::SBValue CreateValueFromData(const char *name, lldb::SBData data,
                                    lldb::SBType type);

  /// Get a child value by index from a value.
  ///
  /// Structs, unions, classes, arrays and pointers have child
  /// values that can be access by index.
  ///
  /// Structs and unions access child members using a zero based index
  /// for each child member. For
  ///
  /// Classes reserve the first indexes for base classes that have
  /// members (empty base classes are omitted), and all members of the
  /// current class will then follow the base classes.
  ///
  /// Pointers differ depending on what they point to. If the pointer
  /// points to a simple type, the child at index zero
  /// is the only child value available, unless \a synthetic_allowed
  /// is \b true, in which case the pointer will be used as an array
  /// and can create 'synthetic' child values using positive or
  /// negative indexes. If the pointer points to an aggregate type
  /// (an array, class, union, struct), then the pointee is
  /// transparently skipped and any children are going to be the indexes
  /// of the child values within the aggregate type. For example if
  /// we have a 'Point' type and we have a SBValue that contains a
  /// pointer to a 'Point' type, then the child at index zero will be
  /// the 'x' member, and the child at index 1 will be the 'y' member
  /// (the child at index zero won't be a 'Point' instance).
  ///
  /// If you actually need an SBValue that represents the type pointed
  /// to by a SBValue for which GetType().IsPointeeType() returns true,
  /// regardless of the pointee type, you can do that with SBValue::Dereference.
  ///
  /// Arrays have a preset number of children that can be accessed by
  /// index and will returns invalid child values for indexes that are
  /// out of bounds unless the \a synthetic_allowed is \b true. In this
  /// case the array can create 'synthetic' child values for indexes
  /// that aren't in the array bounds using positive or negative
  /// indexes.
  ///
  /// \param[in] idx
  ///     The index of the child value to get
  ///
  /// \param[in] use_dynamic
  ///     An enumeration that specifies whether to get dynamic values,
  ///     and also if the target can be run to figure out the dynamic
  ///     type of the child value.
  ///
  /// \param[in] can_create_synthetic
  ///     If \b true, then allow child values to be created by index
  ///     for pointers and arrays for indexes that normally wouldn't
  ///     be allowed.
  ///
  /// \return
  ///     A new SBValue object that represents the child member value.
  lldb::SBValue GetChildAtIndex(uint32_t idx,
                                lldb::DynamicValueType use_dynamic,
                                bool can_create_synthetic);

  // Matches children of this object only and will match base classes and
  // member names if this is a clang typed object.
  uint32_t GetIndexOfChildWithName(const char *name);

  // Matches child members of this object and child members of any base
  // classes.
  lldb::SBValue GetChildMemberWithName(const char *name);

  // Matches child members of this object and child members of any base
  // classes.
  lldb::SBValue GetChildMemberWithName(const char *name,
                                       lldb::DynamicValueType use_dynamic);

  // Expands nested expressions like .a->b[0].c[1]->d
  lldb::SBValue GetValueForExpressionPath(const char *expr_path);

  lldb::SBValue AddressOf();

  lldb::addr_t GetLoadAddress();

  lldb::SBAddress GetAddress();

  /// Get an SBData wrapping what this SBValue points to.
  ///
  /// This method will dereference the current SBValue, if its
  /// data type is a T* or T[], and extract item_count elements
  /// of type T from it, copying their contents in an SBData.
  ///
  /// \param[in] item_idx
  ///     The index of the first item to retrieve. For an array
  ///     this is equivalent to array[item_idx], for a pointer
  ///     to *(pointer + item_idx). In either case, the measurement
  ///     unit for item_idx is the sizeof(T) rather than the byte
  ///
  /// \param[in] item_count
  ///     How many items should be copied into the output. By default
  ///     only one item is copied, but more can be asked for.
  ///
  /// \return
  ///     An SBData with the contents of the copied items, on success.
  ///     An empty SBData otherwise.
  lldb::SBData GetPointeeData(uint32_t item_idx = 0, uint32_t item_count = 1);

  /// Get an SBData wrapping the contents of this SBValue.
  ///
  /// This method will read the contents of this object in memory
  /// and copy them into an SBData for future use.
  ///
  /// \return
  ///     An SBData with the contents of this SBValue, on success.
  ///     An empty SBData otherwise.
  lldb::SBData GetData();

  bool SetData(lldb::SBData &data, lldb::SBError &error);

  /// Creates a copy of the SBValue with a new name and setting the current
  /// SBValue as its parent. It should be used when we want to change the
  /// name of a SBValue without modifying the actual SBValue itself
  /// (e.g. sythetic child provider).
  lldb::SBValue Clone(const char *new_name);

  lldb::SBDeclaration GetDeclaration();

  /// Find out if a SBValue might have children.
  ///
  /// This call is much more efficient than GetNumChildren() as it
  /// doesn't need to complete the underlying type. This is designed
  /// to be used in a UI environment in order to detect if the
  /// disclosure triangle should be displayed or not.
  ///
  /// This function returns true for class, union, structure,
  /// pointers, references, arrays and more. Again, it does so without
  /// doing any expensive type completion.
  ///
  /// \return
  ///     Returns \b true if the SBValue might have children, or \b
  ///     false otherwise.
  bool MightHaveChildren();

  bool IsRuntimeSupportValue();

  /// Return the number of children of this variable. Note that for some
  /// variables this operation can be expensive. If possible, prefer calling
  /// GetNumChildren(max) with the maximum number of children you are interested
  /// in.
  uint32_t GetNumChildren();

  /// Return the numer of children of this variable, with a hint that the
  /// caller is interested in at most \a max children. Use this function to
  /// avoid expensive child computations in some cases. For example, if you know
  /// you will only ever display 100 elements, calling GetNumChildren(100) can
  /// avoid enumerating all the other children. If the returned value is smaller
  /// than \a max, then it represents the true number of children, otherwise it
  /// indicates that their number is at least \a max. Do not assume the returned
  /// number will always be less than or equal to \a max, as the implementation
  /// may choose to return a larger (but still smaller than the actual number of
  /// children) value.
  uint32_t GetNumChildren(uint32_t max);

  LLDB_DEPRECATED("SBValue::GetOpaqueType() is deprecated.")
  void *GetOpaqueType();

  lldb::SBTarget GetTarget();

  lldb::SBProcess GetProcess();

  lldb::SBThread GetThread();

  lldb::SBFrame GetFrame();

  lldb::SBValue Dereference();

  LLDB_DEPRECATED("Use GetType().IsPointerType() instead")
  bool TypeIsPointerType();

  lldb::SBType GetType();

  lldb::SBValue Persist();

  bool GetDescription(lldb::SBStream &description);

  bool GetExpressionPath(lldb::SBStream &description);

  bool GetExpressionPath(lldb::SBStream &description,
                         bool qualify_cxx_base_classes);

  lldb::SBValue EvaluateExpression(const char *expr) const;
  lldb::SBValue EvaluateExpression(const char *expr,
                                   const SBExpressionOptions &options) const;
  lldb::SBValue EvaluateExpression(const char *expr,
                                   const SBExpressionOptions &options,
                                   const char *name) const;

  /// Watch this value if it resides in memory.
  ///
  /// Sets a watchpoint on the value.
  ///
  /// \param[in] resolve_location
  ///     Resolve the location of this value once and watch its address.
  ///     This value must currently be set to \b true as watching all
  ///     locations of a variable or a variable path is not yet supported,
  ///     though we plan to support it in the future.
  ///
  /// \param[in] read
  ///     Stop when this value is accessed.
  ///
  /// \param[in] write
  ///     Stop when this value is modified
  ///
  /// \param[out] error
  ///     An error object. Contains the reason if there is some failure.
  ///
  /// \return
  ///     An SBWatchpoint object. This object might not be valid upon
  ///     return due to a value not being contained in memory, too
  ///     large, or watchpoint resources are not available or all in
  ///     use.
  lldb::SBWatchpoint Watch(bool resolve_location, bool read, bool write,
                           SBError &error);

  // Backward compatibility fix in the interim.
  lldb::SBWatchpoint Watch(bool resolve_location, bool read, bool write);

  /// Watch this value that this value points to in memory
  ///
  /// Sets a watchpoint on the value.
  ///
  /// \param[in] resolve_location
  ///     Resolve the location of this value once and watch its address.
  ///     This value must currently be set to \b true as watching all
  ///     locations of a variable or a variable path is not yet supported,
  ///     though we plan to support it in the future.
  ///
  /// \param[in] read
  ///     Stop when this value is accessed.
  ///
  /// \param[in] write
  ///     Stop when this value is modified
  ///
  /// \param[out] error
  ///     An error object. Contains the reason if there is some failure.
  ///
  /// \return
  ///     An SBWatchpoint object. This object might not be valid upon
  ///     return due to a value not being contained in memory, too
  ///     large, or watchpoint resources are not available or all in
  ///     use.
  lldb::SBWatchpoint WatchPointee(bool resolve_location, bool read, bool write,
                                  SBError &error);

  /// If this value represents a C++ class that has a vtable, return an value
  /// that represents the virtual function table.
  ///
  /// SBValue::GetError() will be in the success state if this value represents
  /// a C++ class with a vtable, or an appropriate error describing that the
  /// object isn't a C++ class with a vtable or not a C++ class.
  ///
  /// SBValue::GetName() will be the demangled symbol name for the virtual
  /// function table like "vtable for <classname>".
  ///
  /// SBValue::GetValue() will be the address of the first vtable entry if the
  /// current SBValue is a class with a vtable, or nothing the current SBValue
  /// is not a C++ class or not a C++ class that has a vtable.
  ///
  /// SBValue::GetValueAtUnsigned(...) will return the address of the first
  /// vtable entry.
  ///
  /// SBValue::GetLoadAddress() will return the address of the vtable pointer
  /// found in the parent SBValue.
  ///
  /// SBValue::GetNumChildren() will return the number of virtual function
  /// pointers in the vtable, or zero on error.
  ///
  /// SBValue::GetChildAtIndex(...) will return each virtual function pointer
  /// as a SBValue object.
  ///
  /// The child SBValue objects will have the following values:
  ///
  /// SBValue::GetError() will indicate success if the vtable entry was
  /// successfully read from memory, or an error if not.
  ///
  /// SBValue::GetName() will be the vtable function index in the form "[%u]"
  /// where %u is the index.
  ///
  /// SBValue::GetValue() will be the virtual function pointer value as a
  /// string.
  ///
  /// SBValue::GetValueAtUnsigned(...) will return the virtual function
  /// pointer value.
  ///
  /// SBValue::GetLoadAddress() will return the address of the virtual function
  /// pointer.
  ///
  /// SBValue::GetNumChildren() returns 0
  lldb::SBValue GetVTable();

protected:
  friend class SBBlock;
  friend class SBFrame;
  friend class SBModule;
  friend class SBTarget;
  friend class SBThread;
  friend class SBTypeStaticField;
  friend class SBTypeSummary;
  friend class SBValueList;

  friend class lldb_private::python::SWIGBridge;

  SBValue(const lldb::ValueObjectSP &value_sp);

  /// Same as the protected version of GetSP that takes a locker, except that we
  /// make the
  /// locker locally in the function.  Since the Target API mutex is recursive,
  /// and the
  /// StopLocker is a read lock, you can call this function even if you are
  /// already
  /// holding the two above-mentioned locks.
  ///
  /// \return
  ///     A ValueObjectSP of the best kind (static, dynamic or synthetic) we
  ///     can cons up, in accordance with the SBValue's settings.
  lldb::ValueObjectSP GetSP() const;

  /// Get the appropriate ValueObjectSP from this SBValue, consulting the
  /// use_dynamic and use_synthetic options passed in to SetSP when the
  /// SBValue's contents were set.  Since this often requires examining memory,
  /// and maybe even running code, it needs to acquire the Target API and
  /// Process StopLock.
  /// Those are held in an opaque class ValueLocker which is currently local to
  /// SBValue.cpp.
  /// So you don't have to get these yourself just default construct a
  /// ValueLocker, and pass it into this.
  /// If we need to make a ValueLocker and use it in some other .cpp file, we'll
  /// have to move it to
  /// ValueObject.h/cpp or somewhere else convenient.  We haven't needed to so
  /// far.
  ///
  /// \param[in] value_locker
  ///     An object that will hold the Target API, and Process RunLocks, and
  ///     auto-destroy them when it goes out of scope.  Currently this is only
  ///     useful in
  ///     SBValue.cpp.
  ///
  /// \return
  ///     A ValueObjectSP of the best kind (static, dynamic or synthetic) we
  ///     can cons up, in accordance with the SBValue's settings.
  lldb::ValueObjectSP GetSP(ValueLocker &value_locker) const;

  // these calls do the right thing WRT adjusting their settings according to
  // the target's preferences
  void SetSP(const lldb::ValueObjectSP &sp);

  void SetSP(const lldb::ValueObjectSP &sp, bool use_synthetic);

  void SetSP(const lldb::ValueObjectSP &sp, lldb::DynamicValueType use_dynamic);

  void SetSP(const lldb::ValueObjectSP &sp, lldb::DynamicValueType use_dynamic,
             bool use_synthetic);

  void SetSP(const lldb::ValueObjectSP &sp, lldb::DynamicValueType use_dynamic,
             bool use_synthetic, const char *name);

private:
  typedef std::shared_ptr<ValueImpl> ValueImplSP;
  ValueImplSP m_opaque_sp;

  void SetSP(ValueImplSP impl_sp);
};

} // namespace lldb

#endif // LLDB_API_SBVALUE_H
