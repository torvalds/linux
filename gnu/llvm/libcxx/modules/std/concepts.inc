// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  // [concepts.lang], language-related concepts
  // [concept.same], concept same_as
  using std::same_as;

  // [concept.derived], concept derived_from
  using std::derived_from;

  // [concept.convertible], concept convertible_to
  using std::convertible_to;

  // [concept.commonref], concept common_reference_with
  using std::common_reference_with;

  // [concept.common], concept common_with
  using std::common_with;

  // [concepts.arithmetic], arithmetic concepts
  using std::floating_point;
  using std::integral;
  using std::signed_integral;
  using std::unsigned_integral;

  // [concept.assignable], concept assignable_from
  using std::assignable_from;

  // [concept.swappable], concept swappable
  namespace ranges {
    inline namespace __cpo {
      using std::ranges::__cpo::swap;
    }
  } // namespace ranges

  using std::swappable;
  using std::swappable_with;

  // [concept.destructible], concept destructible
  using std::destructible;

  // [concept.constructible], concept constructible_from
  using std::constructible_from;

  // [concept.default.init], concept default_initializable
  using std::default_initializable;

  // [concept.moveconstructible], concept move_constructible
  using std::move_constructible;

  // [concept.copyconstructible], concept copy_constructible
  using std::copy_constructible;

  // [concepts.compare], comparison concepts
  // [concept.equalitycomparable], concept equality_comparable
  using std::equality_comparable;
  using std::equality_comparable_with;

  // [concept.totallyordered], concept totally_ordered
  using std::totally_ordered;
  using std::totally_ordered_with;

  // [concepts.object], object concepts
  using std::copyable;
  using std::movable;
  using std::regular;
  using std::semiregular;

  // [concepts.callable], callable concepts
  // [concept.invocable], concept invocable
  using std::invocable;

  // [concept.regularinvocable], concept regular_invocable
  using std::regular_invocable;

  // [concept.predicate], concept predicate
  using std::predicate;

  // [concept.relation], concept relation
  using std::relation;

  // [concept.equiv], concept equivalence_relation
  using std::equivalence_relation;

  // [concept.strictweakorder], concept strict_weak_order
  using std::strict_weak_order;

} // namespace std
