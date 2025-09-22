//===-- SBStructuredData.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSTRUCTUREDDATA_H
#define LLDB_API_SBSTRUCTUREDDATA_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBScriptObject.h"

namespace lldb_private {
namespace python {
class SWIGBridge;
}
namespace lua {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class SBStructuredData {
public:
  SBStructuredData();

  SBStructuredData(const lldb::SBStructuredData &rhs);

  SBStructuredData(const lldb::SBScriptObject obj,
                   const lldb::SBDebugger &debugger);

  ~SBStructuredData();

  lldb::SBStructuredData &operator=(const lldb::SBStructuredData &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  lldb::SBError SetFromJSON(lldb::SBStream &stream);

  lldb::SBError SetFromJSON(const char *json);

  void Clear();

  lldb::SBError GetAsJSON(lldb::SBStream &stream) const;

  lldb::SBError GetDescription(lldb::SBStream &stream) const;

  /// Return the type of data in this data structure
  lldb::StructuredDataType GetType() const;

  /// Return the size (i.e. number of elements) in this data structure
  /// if it is an array or dictionary type. For other types, 0 will be
  //  returned.
  size_t GetSize() const;

  /// Fill keys with the keys in this object and return true if this data
  /// structure is a dictionary.  Returns false otherwise.
  bool GetKeys(lldb::SBStringList &keys) const;

  /// Return the value corresponding to a key if this data structure
  /// is a dictionary type.
  lldb::SBStructuredData GetValueForKey(const char *key) const;

  /// Return the value corresponding to an index if this data structure
  /// is array.
  lldb::SBStructuredData GetItemAtIndex(size_t idx) const;

  /// Return the integer value if this data structure is an integer type.
  uint64_t GetUnsignedIntegerValue(uint64_t fail_value = 0) const;
  /// Return the integer value if this data structure is an integer type.
  int64_t GetSignedIntegerValue(int64_t fail_value = 0) const;

  LLDB_DEPRECATED_FIXME(
      "Specify if the value is signed or unsigned",
      "uint64_t GetUnsignedIntegerValue(uint64_t fail_value = 0)")
  uint64_t GetIntegerValue(uint64_t fail_value = 0) const;

  /// Return the floating point value if this data structure is a floating
  /// type.
  double GetFloatValue(double fail_value = 0.0) const;

  /// Return the boolean value if this data structure is a boolean type.
  bool GetBooleanValue(bool fail_value = false) const;

  /// Provides the string value if this data structure is a string type.
  ///
  /// \param[out] dst
  ///     pointer where the string value will be written. In case it is null,
  ///     nothing will be written at \a dst.
  ///
  /// \param[in] dst_len
  ///     max number of characters that can be written at \a dst. In case it is
  ///     zero, nothing will be written at \a dst. If this length is not enough
  ///     to write the complete string value, (\a dst_len - 1) bytes of the
  ///     string value will be written at \a dst followed by a null character.
  ///
  /// \return
  ///     Returns the byte size needed to completely write the string value at
  ///     \a dst in all cases.
  size_t GetStringValue(char *dst, size_t dst_len) const;

  /// Return the generic pointer if this data structure is a generic type.
  lldb::SBScriptObject GetGenericValue() const;

protected:
  friend class SBAttachInfo;
  friend class SBLaunchInfo;
  friend class SBDebugger;
  friend class SBTarget;
  friend class SBProcess;
  friend class SBThread;
  friend class SBThreadPlan;
  friend class SBBreakpoint;
  friend class SBBreakpointLocation;
  friend class SBBreakpointName;
  friend class SBTrace;
  friend class lldb_private::python::SWIGBridge;
  friend class lldb_private::lua::SWIGBridge;
  friend class SBCommandInterpreter;

  SBStructuredData(const lldb_private::StructuredDataImpl &impl);

  SBStructuredData(const lldb::EventSP &event_sp);

  StructuredDataImplUP m_impl_up;
};
} // namespace lldb

#endif // LLDB_API_SBSTRUCTUREDDATA_H
