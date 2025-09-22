//===--- Visibility.h - Visibility enumeration and utilities ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::Visibility enumeration and various utility
/// functions.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_VISIBILITY_H
#define LLVM_CLANG_BASIC_VISIBILITY_H

#include "clang/Basic/Linkage.h"
#include "llvm/ADT/STLForwardCompat.h"
#include <cassert>
#include <cstdint>

namespace clang {

/// Describes the different kinds of visibility that a declaration
/// may have.
///
/// Visibility determines how a declaration interacts with the dynamic
/// linker.  It may also affect whether the symbol can be found by runtime
/// symbol lookup APIs.
///
/// Visibility is not described in any language standard and
/// (nonetheless) sometimes has odd behavior.  Not all platforms
/// support all visibility kinds.
enum Visibility {
  /// Objects with "hidden" visibility are not seen by the dynamic
  /// linker.
  HiddenVisibility,

  /// Objects with "protected" visibility are seen by the dynamic
  /// linker but always dynamically resolve to an object within this
  /// shared object.
  ProtectedVisibility,

  /// Objects with "default" visibility are seen by the dynamic linker
  /// and act like normal objects.
  DefaultVisibility
};

inline Visibility minVisibility(Visibility L, Visibility R) {
  return L < R ? L : R;
}

class LinkageInfo {
  LLVM_PREFERRED_TYPE(Linkage)
  uint8_t linkage_    : 3;
  LLVM_PREFERRED_TYPE(Visibility)
  uint8_t visibility_ : 2;
  LLVM_PREFERRED_TYPE(bool)
  uint8_t explicit_   : 1;

  void setVisibility(Visibility V, bool E) { visibility_ = V; explicit_ = E; }
public:
  LinkageInfo()
      : linkage_(llvm::to_underlying(Linkage::External)),
        visibility_(DefaultVisibility), explicit_(false) {}
  LinkageInfo(Linkage L, Visibility V, bool E)
      : linkage_(llvm::to_underlying(L)), visibility_(V), explicit_(E) {
    assert(getLinkage() == L && getVisibility() == V &&
           isVisibilityExplicit() == E && "Enum truncated!");
  }

  static LinkageInfo external() {
    return LinkageInfo();
  }
  static LinkageInfo internal() {
    return LinkageInfo(Linkage::Internal, DefaultVisibility, false);
  }
  static LinkageInfo uniqueExternal() {
    return LinkageInfo(Linkage::UniqueExternal, DefaultVisibility, false);
  }
  static LinkageInfo none() {
    return LinkageInfo(Linkage::None, DefaultVisibility, false);
  }
  static LinkageInfo visible_none() {
    return LinkageInfo(Linkage::VisibleNone, DefaultVisibility, false);
  }

  Linkage getLinkage() const { return static_cast<Linkage>(linkage_); }
  Visibility getVisibility() const { return (Visibility)visibility_; }
  bool isVisibilityExplicit() const { return explicit_; }

  void setLinkage(Linkage L) { linkage_ = llvm::to_underlying(L); }

  void mergeLinkage(Linkage L) {
    setLinkage(minLinkage(getLinkage(), L));
  }
  void mergeLinkage(LinkageInfo other) {
    mergeLinkage(other.getLinkage());
  }

  void mergeExternalVisibility(Linkage L) {
    Linkage ThisL = getLinkage();
    if (!isExternallyVisible(L)) {
      if (ThisL == Linkage::VisibleNone)
        ThisL = Linkage::None;
      else if (ThisL == Linkage::External)
        ThisL = Linkage::UniqueExternal;
    }
    setLinkage(ThisL);
  }
  void mergeExternalVisibility(LinkageInfo Other) {
    mergeExternalVisibility(Other.getLinkage());
  }

  /// Merge in the visibility 'newVis'.
  void mergeVisibility(Visibility newVis, bool newExplicit) {
    Visibility oldVis = getVisibility();

    // Never increase visibility.
    if (oldVis < newVis)
      return;

    // If the new visibility is the same as the old and the new
    // visibility isn't explicit, we have nothing to add.
    if (oldVis == newVis && !newExplicit)
      return;

    // Otherwise, we're either decreasing visibility or making our
    // existing visibility explicit.
    setVisibility(newVis, newExplicit);
  }
  void mergeVisibility(LinkageInfo other) {
    mergeVisibility(other.getVisibility(), other.isVisibilityExplicit());
  }

  /// Merge both linkage and visibility.
  void merge(LinkageInfo other) {
    mergeLinkage(other);
    mergeVisibility(other);
  }

  /// Merge linkage and conditionally merge visibility.
  void mergeMaybeWithVisibility(LinkageInfo other, bool withVis) {
    mergeLinkage(other);
    if (withVis) mergeVisibility(other);
  }
};
}

#endif // LLVM_CLANG_BASIC_VISIBILITY_H
