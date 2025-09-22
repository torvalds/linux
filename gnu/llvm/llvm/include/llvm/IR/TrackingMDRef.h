//===- llvm/IR/TrackingMDRef.h - Tracking Metadata references ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// References to metadata that track RAUW.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TRACKINGMDREF_H
#define LLVM_IR_TRACKINGMDREF_H

#include "llvm/IR/Metadata.h"
#include <algorithm>
#include <cassert>

namespace llvm {

/// Tracking metadata reference.
///
/// This class behaves like \a TrackingVH, but for metadata.
class TrackingMDRef {
  Metadata *MD = nullptr;

public:
  TrackingMDRef() = default;
  explicit TrackingMDRef(Metadata *MD) : MD(MD) { track(); }

  TrackingMDRef(TrackingMDRef &&X) : MD(X.MD) { retrack(X); }
  TrackingMDRef(const TrackingMDRef &X) : MD(X.MD) { track(); }

  TrackingMDRef &operator=(TrackingMDRef &&X) {
    if (&X == this)
      return *this;

    untrack();
    MD = X.MD;
    retrack(X);
    return *this;
  }

  TrackingMDRef &operator=(const TrackingMDRef &X) {
    if (&X == this)
      return *this;

    untrack();
    MD = X.MD;
    track();
    return *this;
  }

  ~TrackingMDRef() { untrack(); }

  Metadata *get() const { return MD; }
  operator Metadata *() const { return get(); }
  Metadata *operator->() const { return get(); }
  Metadata &operator*() const { return *get(); }

  void reset() {
    untrack();
    MD = nullptr;
  }
  void reset(Metadata *MD) {
    untrack();
    this->MD = MD;
    track();
  }

  /// Check whether this has a trivial destructor.
  ///
  /// If \c MD isn't replaceable, the destructor will be a no-op.
  bool hasTrivialDestructor() const {
    return !MD || !MetadataTracking::isReplaceable(*MD);
  }

  bool operator==(const TrackingMDRef &X) const { return MD == X.MD; }
  bool operator!=(const TrackingMDRef &X) const { return MD != X.MD; }

private:
  void track() {
    if (MD)
      MetadataTracking::track(MD);
  }

  void untrack() {
    if (MD)
      MetadataTracking::untrack(MD);
  }

  void retrack(TrackingMDRef &X) {
    assert(MD == X.MD && "Expected values to match");
    if (X.MD) {
      MetadataTracking::retrack(X.MD, MD);
      X.MD = nullptr;
    }
  }
};

/// Typed tracking ref.
///
/// Track refererences of a particular type.  It's useful to use this for \a
/// MDNode and \a ValueAsMetadata.
template <class T> class TypedTrackingMDRef {
  TrackingMDRef Ref;

public:
  TypedTrackingMDRef() = default;
  explicit TypedTrackingMDRef(T *MD) : Ref(static_cast<Metadata *>(MD)) {}

  TypedTrackingMDRef(TypedTrackingMDRef &&X) : Ref(std::move(X.Ref)) {}
  TypedTrackingMDRef(const TypedTrackingMDRef &X) : Ref(X.Ref) {}

  TypedTrackingMDRef &operator=(TypedTrackingMDRef &&X) {
    Ref = std::move(X.Ref);
    return *this;
  }

  TypedTrackingMDRef &operator=(const TypedTrackingMDRef &X) {
    Ref = X.Ref;
    return *this;
  }

  T *get() const { return (T *)Ref.get(); }
  operator T *() const { return get(); }
  T *operator->() const { return get(); }
  T &operator*() const { return *get(); }

  bool operator==(const TypedTrackingMDRef &X) const { return Ref == X.Ref; }
  bool operator!=(const TypedTrackingMDRef &X) const { return Ref != X.Ref; }

  void reset() { Ref.reset(); }
  void reset(T *MD) { Ref.reset(static_cast<Metadata *>(MD)); }

  /// Check whether this has a trivial destructor.
  bool hasTrivialDestructor() const { return Ref.hasTrivialDestructor(); }
};

using TrackingMDNodeRef = TypedTrackingMDRef<MDNode>;
using TrackingValueAsMetadataRef = TypedTrackingMDRef<ValueAsMetadata>;

// Expose the underlying metadata to casting.
template <> struct simplify_type<TrackingMDRef> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(TrackingMDRef &MD) { return MD.get(); }
};

template <> struct simplify_type<const TrackingMDRef> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(const TrackingMDRef &MD) {
    return MD.get();
  }
};

template <class T> struct simplify_type<TypedTrackingMDRef<T>> {
  using SimpleType = T *;

  static SimpleType getSimplifiedValue(TypedTrackingMDRef<T> &MD) {
    return MD.get();
  }
};

template <class T> struct simplify_type<const TypedTrackingMDRef<T>> {
  using SimpleType = T *;

  static SimpleType getSimplifiedValue(const TypedTrackingMDRef<T> &MD) {
    return MD.get();
  }
};

} // end namespace llvm

#endif // LLVM_IR_TRACKINGMDREF_H
