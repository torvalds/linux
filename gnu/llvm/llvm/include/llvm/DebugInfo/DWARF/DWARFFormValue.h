//===- DWARFFormValue.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFFORMVALUE_H
#define LLVM_DEBUGINFO_DWARF_DWARFFORMVALUE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>

namespace llvm {

class DWARFContext;
class DWARFObject;
class DWARFDataExtractor;
class DWARFUnit;
class raw_ostream;

class DWARFFormValue {
public:
  enum FormClass {
    FC_Unknown,
    FC_Address,
    FC_Block,
    FC_Constant,
    FC_String,
    FC_Flag,
    FC_Reference,
    FC_Indirect,
    FC_SectionOffset,
    FC_Exprloc
  };

  struct ValueType {
    ValueType() { uval = 0; }
    ValueType(int64_t V) : sval(V) {}
    ValueType(uint64_t V) : uval(V) {}
    ValueType(const char *V) : cstr(V) {}

    union {
      uint64_t uval;
      int64_t sval;
      const char *cstr;
    };
    const uint8_t *data = nullptr;
    uint64_t SectionIndex; /// Section index for reference forms.
  };

private:
  dwarf::Form Form; /// Form for this value.
  dwarf::DwarfFormat Format =
      dwarf::DWARF32;           /// Remember the DWARF format at extract time.
  ValueType Value;              /// Contains all data for the form.
  const DWARFUnit *U = nullptr; /// Remember the DWARFUnit at extract time.
  const DWARFContext *C = nullptr; /// Context for extract time.

  DWARFFormValue(dwarf::Form F, const ValueType &V) : Form(F), Value(V) {}

public:
  DWARFFormValue(dwarf::Form F = dwarf::Form(0)) : Form(F) {}

  static DWARFFormValue createFromSValue(dwarf::Form F, int64_t V);
  static DWARFFormValue createFromUValue(dwarf::Form F, uint64_t V);
  static DWARFFormValue createFromPValue(dwarf::Form F, const char *V);
  static DWARFFormValue createFromBlockValue(dwarf::Form F,
                                             ArrayRef<uint8_t> D);
  static DWARFFormValue createFromUnit(dwarf::Form F, const DWARFUnit *Unit,
                                       uint64_t *OffsetPtr);
  static std::optional<object::SectionedAddress>
  getAsSectionedAddress(const ValueType &Val, const dwarf::Form Form,
                        const DWARFUnit *U);

  dwarf::Form getForm() const { return Form; }
  uint64_t getRawUValue() const { return Value.uval; }

  bool isFormClass(FormClass FC) const;
  const DWARFUnit *getUnit() const { return U; }
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts = DIDumpOptions()) const;
  void dumpSectionedAddress(raw_ostream &OS, DIDumpOptions DumpOpts,
                            object::SectionedAddress SA) const;
  void dumpAddress(raw_ostream &OS, uint64_t Address) const;
  static void dumpAddress(raw_ostream &OS, uint8_t AddressSize,
                          uint64_t Address);
  static void dumpAddressSection(const DWARFObject &Obj, raw_ostream &OS,
                                 DIDumpOptions DumpOpts, uint64_t SectionIndex);

  /// Extracts a value in \p Data at offset \p *OffsetPtr. The information
  /// in \p FormParams is needed to interpret some forms. The optional
  /// \p Context and \p Unit allows extracting information if the form refers
  /// to other sections (e.g., .debug_str).
  bool extractValue(const DWARFDataExtractor &Data, uint64_t *OffsetPtr,
                    dwarf::FormParams FormParams,
                    const DWARFContext *Context = nullptr,
                    const DWARFUnit *Unit = nullptr);

  bool extractValue(const DWARFDataExtractor &Data, uint64_t *OffsetPtr,
                    dwarf::FormParams FormParams, const DWARFUnit *U) {
    return extractValue(Data, OffsetPtr, FormParams, nullptr, U);
  }

  /// getAsFoo functions below return the extracted value as Foo if only
  /// DWARFFormValue has form class is suitable for representing Foo.
  std::optional<uint64_t> getAsRelativeReference() const;
  std::optional<uint64_t> getAsDebugInfoReference() const;
  std::optional<uint64_t> getAsSignatureReference() const;
  std::optional<uint64_t> getAsSupplementaryReference() const;
  std::optional<uint64_t> getAsUnsignedConstant() const;
  std::optional<int64_t> getAsSignedConstant() const;
  Expected<const char *> getAsCString() const;
  std::optional<uint64_t> getAsAddress() const;
  std::optional<object::SectionedAddress> getAsSectionedAddress() const;
  std::optional<uint64_t> getAsSectionOffset() const;
  std::optional<ArrayRef<uint8_t>> getAsBlock() const;
  std::optional<uint64_t> getAsCStringOffset() const;
  std::optional<uint64_t> getAsReferenceUVal() const;
  /// Correctly extract any file paths from a form value.
  ///
  /// These attributes can be in the from DW_AT_decl_file or DW_AT_call_file
  /// attributes. We need to use the file index in the correct DWARFUnit's line
  /// table prologue, and each DWARFFormValue has the DWARFUnit the form value
  /// was extracted from.
  ///
  /// \param Kind The kind of path to extract.
  ///
  /// \returns A valid string value on success, or std::nullopt if the form
  /// class is not FC_Constant, or if the file index is not valid.
  std::optional<std::string>
  getAsFile(DILineInfoSpecifier::FileLineInfoKind Kind) const;

  /// Skip a form's value in \p DebugInfoData at the offset specified by
  /// \p OffsetPtr.
  ///
  /// Skips the bytes for the current form and updates the offset.
  ///
  /// \param DebugInfoData The data where we want to skip the value.
  /// \param OffsetPtr A reference to the offset that will be updated.
  /// \param Params DWARF parameters to help interpret forms.
  /// \returns true on success, false if the form was not skipped.
  bool skipValue(DataExtractor DebugInfoData, uint64_t *OffsetPtr,
                 const dwarf::FormParams Params) const {
    return DWARFFormValue::skipValue(Form, DebugInfoData, OffsetPtr, Params);
  }

  /// Skip a form's value in \p DebugInfoData at the offset specified by
  /// \p OffsetPtr.
  ///
  /// Skips the bytes for the specified form and updates the offset.
  ///
  /// \param Form The DW_FORM enumeration that indicates the form to skip.
  /// \param DebugInfoData The data where we want to skip the value.
  /// \param OffsetPtr A reference to the offset that will be updated.
  /// \param FormParams DWARF parameters to help interpret forms.
  /// \returns true on success, false if the form was not skipped.
  static bool skipValue(dwarf::Form Form, DataExtractor DebugInfoData,
                        uint64_t *OffsetPtr,
                        const dwarf::FormParams FormParams);

private:
  void dumpString(raw_ostream &OS) const;
};

namespace dwarf {

/// Take an optional DWARFFormValue and try to extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and was a string.
inline std::optional<const char *>
toString(const std::optional<DWARFFormValue> &V) {
  if (!V)
    return std::nullopt;
  Expected<const char*> E = V->getAsCString();
  if (!E) {
    consumeError(E.takeError());
    return std::nullopt;
  }
  return *E;
}

/// Take an optional DWARFFormValue and try to extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and was a string.
inline StringRef toStringRef(const std::optional<DWARFFormValue> &V,
                             StringRef Default = {}) {
  if (!V)
    return Default;
  auto S = V->getAsCString();
  if (!S) {
    consumeError(S.takeError());
    return Default;
  }
  if (!*S)
    return Default;
  return *S;
}

/// Take an optional DWARFFormValue and extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the string value or Default if the V doesn't have a value or the
/// form value's encoding wasn't a string.
inline const char *toString(const std::optional<DWARFFormValue> &V,
                            const char *Default) {
  if (auto E = toString(V))
    return *E;
  return Default;
}

/// Take an optional DWARFFormValue and try to extract an unsigned constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a unsigned constant form.
inline std::optional<uint64_t>
toUnsigned(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsUnsignedConstant();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a unsigned constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted unsigned value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an unsigned constant form.
inline uint64_t toUnsigned(const std::optional<DWARFFormValue> &V,
                           uint64_t Default) {
  return toUnsigned(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract a relative offset
/// reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a relative reference form.
inline std::optional<uint64_t>
toRelativeReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsRelativeReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a relative offset reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a relative offset reference form.
inline uint64_t toRelativeReference(const std::optional<DWARFFormValue> &V,
                                    uint64_t Default) {
  return toRelativeReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an absolute debug info
/// offset reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has an (absolute) debug info offset reference form.
inline std::optional<uint64_t>
toDebugInfoReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsDebugInfoReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract an absolute debug info offset
/// reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an absolute debug info offset
/// reference form.
inline uint64_t toDebugInfoReference(const std::optional<DWARFFormValue> &V,
                                     uint64_t Default) {
  return toDebugInfoReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract a signature reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a signature reference form.
inline std::optional<uint64_t>
toSignatureReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSignatureReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a signature reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a signature reference form.
inline uint64_t toSignatureReference(const std::optional<DWARFFormValue> &V,
                                     uint64_t Default) {
  return toSignatureReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract a supplementary debug
/// info reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a supplementary reference form.
inline std::optional<uint64_t>
toSupplementaryReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSupplementaryReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a supplementary debug info
/// reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a supplementary reference form.
inline uint64_t toSupplementaryReference(const std::optional<DWARFFormValue> &V,
                                         uint64_t Default) {
  return toSupplementaryReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an signed constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a signed constant form.
inline std::optional<int64_t> toSigned(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSignedConstant();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a signed integer.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted signed integer value or Default if the V doesn't
/// have a value or the form value's encoding wasn't a signed integer form.
inline int64_t toSigned(const std::optional<DWARFFormValue> &V,
                        int64_t Default) {
  return toSigned(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an address.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a address form.
inline std::optional<uint64_t>
toAddress(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsAddress();
  return std::nullopt;
}

inline std::optional<object::SectionedAddress>
toSectionedAddress(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSectionedAddress();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a address.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted address value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an address form.
inline uint64_t toAddress(const std::optional<DWARFFormValue> &V,
                          uint64_t Default) {
  return toAddress(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an section offset.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a section offset form.
inline std::optional<uint64_t>
toSectionOffset(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSectionOffset();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a section offset.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted section offset value or Default if the V doesn't
/// have a value or the form value's encoding wasn't a section offset form.
inline uint64_t toSectionOffset(const std::optional<DWARFFormValue> &V,
                                uint64_t Default) {
  return toSectionOffset(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract block data.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a block form.
inline std::optional<ArrayRef<uint8_t>>
toBlock(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsBlock();
  return std::nullopt;
}

/// Check whether specified \p Form belongs to the \p FC class.
/// \param Form an attribute form.
/// \param FC an attribute form class to check.
/// \param DwarfVersion the version of DWARF debug info keeping the attribute.
/// \returns true if specified \p Form belongs to the \p FC class.
bool doesFormBelongToClass(dwarf::Form Form, DWARFFormValue::FormClass FC,
                           uint16_t DwarfVersion);

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFFORMVALUE_H
