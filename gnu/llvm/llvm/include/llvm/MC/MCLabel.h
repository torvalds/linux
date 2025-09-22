//===- MCLabel.h - Machine Code Directional Local Labels --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCLabel class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCLABEL_H
#define LLVM_MC_MCLABEL_H

namespace llvm {

class raw_ostream;

/// Instances of this class represent a label name in the MC file,
/// and MCLabel are created and uniqued by the MCContext class.  MCLabel
/// should only be constructed for valid instances in the object file.
class MCLabel {
  // The instance number of this Directional Local Label.
  unsigned Instance;

private: // MCContext creates and uniques these.
  friend class MCContext;

  MCLabel(unsigned instance) : Instance(instance) {}

public:
  MCLabel(const MCLabel &) = delete;
  MCLabel &operator=(const MCLabel &) = delete;

  /// Get the current instance of this Directional Local Label.
  unsigned getInstance() const { return Instance; }

  /// Increment the current instance of this Directional Local Label.
  unsigned incInstance() { return ++Instance; }

  /// Print the value to the stream \p OS.
  void print(raw_ostream &OS) const;

  /// Print the value to stderr.
  void dump() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const MCLabel &Label) {
  Label.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_MC_MCLABEL_H
