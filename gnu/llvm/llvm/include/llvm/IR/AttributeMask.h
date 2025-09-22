//===- llvm/AttributeMask.h - Mask for Attributes ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
// This file declares the AttributeMask class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ATTRIBUTEMASK_H
#define LLVM_IR_ATTRIBUTEMASK_H

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Attributes.h"
#include <bitset>
#include <cassert>
#include <set>

namespace llvm {

//===----------------------------------------------------------------------===//
/// \class
/// This class stores enough information to efficiently remove some attributes
/// from an existing AttrBuilder, AttributeSet or AttributeList.
class AttributeMask {
  std::bitset<Attribute::EndAttrKinds> Attrs;
  std::set<SmallString<32>, std::less<>> TargetDepAttrs;

public:
  AttributeMask() = default;
  AttributeMask(const AttributeMask &) = delete;
  AttributeMask(AttributeMask &&) = default;

  AttributeMask(AttributeSet AS) {
    for (Attribute A : AS)
      addAttribute(A);
  }

  /// Add an attribute to the mask.
  AttributeMask &addAttribute(Attribute::AttrKind Val) {
    assert((unsigned)Val < Attribute::EndAttrKinds &&
           "Attribute out of range!");
    Attrs[Val] = true;
    return *this;
  }

  /// Add the Attribute object to the builder.
  AttributeMask &addAttribute(Attribute A) {
    if (A.isStringAttribute())
      addAttribute(A.getKindAsString());
    else
      addAttribute(A.getKindAsEnum());
    return *this;
  }

  /// Add the target-dependent attribute to the builder.
  AttributeMask &addAttribute(StringRef A) {
    TargetDepAttrs.insert(A);
    return *this;
  }

  /// Return true if the builder has the specified attribute.
  bool contains(Attribute::AttrKind A) const {
    assert((unsigned)A < Attribute::EndAttrKinds && "Attribute out of range!");
    return Attrs[A];
  }

  /// Return true if the builder has the specified target-dependent
  /// attribute.
  bool contains(StringRef A) const { return TargetDepAttrs.count(A); }

  /// Return true if the mask contains the specified attribute.
  bool contains(Attribute A) const {
    if (A.isStringAttribute())
      return contains(A.getKindAsString());
    return contains(A.getKindAsEnum());
  }
};

} // end namespace llvm

#endif // LLVM_IR_ATTRIBUTEMASK_H
