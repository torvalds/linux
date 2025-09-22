//===------ extensible_rtti.h - Extensible RTTI for ORC RT ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
//
// Provides an extensible RTTI mechanism, that can be used regardless of whether
// the runtime is built with -frtti or not. This is predominantly used to
// support error handling.
//
// The RTTIRoot class defines methods for comparing type ids. Implementations
// of these methods can be injected into new classes using the RTTIExtends
// class template.
//
// E.g.
//
//   @code{.cpp}
//   class MyBaseClass : public RTTIExtends<MyBaseClass, RTTIRoot> {
//   public:
//     static char ID;
//     virtual void foo() = 0;
//   };
//
//   class MyDerivedClass1 : public RTTIExtends<MyDerivedClass1, MyBaseClass> {
//   public:
//     static char ID;
//     void foo() override {}
//   };
//
//   class MyDerivedClass2 : public RTTIExtends<MyDerivedClass2, MyBaseClass> {
//   public:
//     static char ID;
//     void foo() override {}
//   };
//
//   char MyBaseClass::ID = 0;
//   char MyDerivedClass1::ID = 0;
//   char MyDerivedClass2:: ID = 0;
//
//   void fn() {
//     std::unique_ptr<MyBaseClass> B = std::make_unique<MyDerivedClass1>();
//     outs() << isa<MyBaseClass>(B) << "\n"; // Outputs "1".
//     outs() << isa<MyDerivedClass1>(B) << "\n"; // Outputs "1".
//     outs() << isa<MyDerivedClass2>(B) << "\n"; // Outputs "0'.
//   }
//
//   @endcode
//
// Note:
//   This header was adapted from llvm/Support/ExtensibleRTTI.h, however the
// data structures are not shared and the code need not be kept in sync.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_EXTENSIBLE_RTTI_H
#define ORC_RT_EXTENSIBLE_RTTI_H

namespace __orc_rt {

template <typename ThisT, typename ParentT> class RTTIExtends;

/// Base class for the extensible RTTI hierarchy.
///
/// This class defines virtual methods, dynamicClassID and isA, that enable
/// type comparisons.
class RTTIRoot {
public:
  virtual ~RTTIRoot() = default;

  /// Returns the class ID for this type.
  static const void *classID() { return &ID; }

  /// Returns the class ID for the dynamic type of this RTTIRoot instance.
  virtual const void *dynamicClassID() const = 0;

  /// Returns true if this class's ID matches the given class ID.
  virtual bool isA(const void *const ClassID) const {
    return ClassID == classID();
  }

  /// Check whether this instance is a subclass of QueryT.
  template <typename QueryT> bool isA() const { return isA(QueryT::classID()); }

  static bool classof(const RTTIRoot *R) { return R->isA<RTTIRoot>(); }

private:
  virtual void anchor();

  static char ID;
};

/// Inheritance utility for extensible RTTI.
///
/// Supports single inheritance only: A class can only have one
/// ExtensibleRTTI-parent (i.e. a parent for which the isa<> test will work),
/// though it can have many non-ExtensibleRTTI parents.
///
/// RTTIExtents uses CRTP so the first template argument to RTTIExtends is the
/// newly introduced type, and the *second* argument is the parent class.
///
/// class MyType : public RTTIExtends<MyType, RTTIRoot> {
/// public:
///   static char ID;
/// };
///
/// class MyDerivedType : public RTTIExtends<MyDerivedType, MyType> {
/// public:
///   static char ID;
/// };
///
template <typename ThisT, typename ParentT> class RTTIExtends : public ParentT {
public:
  // Inherit constructors and isA methods from ParentT.
  using ParentT::isA;
  using ParentT::ParentT;

  static char ID;

  static const void *classID() { return &ThisT::ID; }

  const void *dynamicClassID() const override { return &ThisT::ID; }

  bool isA(const void *const ClassID) const override {
    return ClassID == classID() || ParentT::isA(ClassID);
  }

  static bool classof(const RTTIRoot *R) { return R->isA<ThisT>(); }
};

template <typename ThisT, typename ParentT>
char RTTIExtends<ThisT, ParentT>::ID = 0;

/// Returns true if the given value is an instance of the template type
/// parameter.
template <typename To, typename From> bool isa(const From &Value) {
  return To::classof(&Value);
}

} // end namespace __orc_rt

#endif // ORC_RT_EXTENSIBLE_RTTI_H
