//===-- Cloneable.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_CLONEABLE_H
#define LLDB_UTILITY_CLONEABLE_H

#include <memory>
#include <type_traits>

namespace lldb_private {

/// \class Cloneable Cloneable.h "lldb/Utility/Cloneable.h"
/// A class that implements CRTP-based "virtual constructor" idiom.
///
/// Example:
/// @code
/// class Base {
///   using TopmostBase = Base;
/// public:
///   virtual std::shared_ptr<Base> Clone() const = 0;
/// };
/// @endcode
///
/// To define a class derived from the Base with overridden Clone:
/// @code
/// class Intermediate : public Cloneable<Intermediate, Base> {};
/// @endcode
///
/// To define a class at the next level of inheritance with overridden Clone:
/// @code
/// class Derived : public Cloneable<Derived, Intermediate> {};
/// @endcode

template <typename Derived, typename Base>
class Cloneable : public Base {
public:
  using Base::Base;

  std::shared_ptr<typename Base::TopmostBase> Clone() const override {
    // std::is_base_of requires derived type to be complete, that's why class
    // scope static_assert cannot be used.
    static_assert(std::is_base_of<Cloneable, Derived>::value,
                  "Derived class must be derived from this.");

    return std::make_shared<Derived>(static_cast<const Derived &>(*this));
  }
};

} // namespace lldb_private

#endif // LLDB_UTILITY_CLONEABLE_H
