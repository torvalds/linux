//==-- llvm/CodeGen/RegisterBank.h - Register Bank ---------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API of register banks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERBANK_H
#define LLVM_CODEGEN_REGISTERBANK_H

#include <cstdint>

namespace llvm {
// Forward declarations.
class RegisterBankInfo;
class raw_ostream;
class TargetRegisterClass;
class TargetRegisterInfo;

/// This class implements the register bank concept.
/// Two instances of RegisterBank must have different ID.
/// This property is enforced by the RegisterBankInfo class.
class RegisterBank {
private:
  unsigned ID;
  unsigned NumRegClasses;
  const char *Name;
  const uint32_t *CoveredClasses;

  /// Only the RegisterBankInfo can initialize RegisterBank properly.
  friend RegisterBankInfo;

public:
  constexpr RegisterBank(unsigned ID, const char *Name,
                         const uint32_t *CoveredClasses, unsigned NumRegClasses)
      : ID(ID), NumRegClasses(NumRegClasses), Name(Name),
        CoveredClasses(CoveredClasses) {}

  /// Get the identifier of this register bank.
  unsigned getID() const { return ID; }

  /// Get a user friendly name of this register bank.
  /// Should be used only for debugging purposes.
  const char *getName() const { return Name; }

  /// Check if this register bank is valid. In other words,
  /// if it has been properly constructed.
  ///
  /// \note This method does not check anything when assertions are disabled.
  ///
  /// \return True is the check was successful.
  bool verify(const RegisterBankInfo &RBI, const TargetRegisterInfo &TRI) const;

  /// Check whether this register bank covers \p RC.
  /// In other words, check if this register bank fully covers
  /// the registers that \p RC contains.
  bool covers(const TargetRegisterClass &RC) const;

  /// Check whether \p OtherRB is the same as this.
  bool operator==(const RegisterBank &OtherRB) const;
  bool operator!=(const RegisterBank &OtherRB) const {
    return !this->operator==(OtherRB);
  }

  /// Dump the register mask on dbgs() stream.
  /// The dump is verbose.
  void dump(const TargetRegisterInfo *TRI = nullptr) const;

  /// Print the register mask on OS.
  /// If IsForDebug is false, then only the name of the register bank
  /// is printed. Otherwise, all the fields are printing.
  /// TRI is then used to print the name of the register classes that
  /// this register bank covers.
  void print(raw_ostream &OS, bool IsForDebug = false,
             const TargetRegisterInfo *TRI = nullptr) const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const RegisterBank &RegBank) {
  RegBank.print(OS);
  return OS;
}
} // End namespace llvm.

#endif
