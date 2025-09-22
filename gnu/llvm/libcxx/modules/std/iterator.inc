// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [iterator.assoc.types], associated types
  // [incrementable.traits], incrementable traits
  using std::incrementable_traits;
  using std::iter_difference_t;

  using std::indirectly_readable_traits;
  using std::iter_value_t;

  // [iterator.traits], iterator traits
  using std::iterator_traits;

  using std::iter_reference_t;

  namespace ranges {
    // [iterator.cust], customization point objects
    inline namespace __cpo {
      // [iterator.cust.move], ranges::iter_move
      using std::ranges::__cpo::iter_move;

      // [iterator.cust.swap], ranges::iter_swap
      using std::ranges::__cpo::iter_swap;
    } // namespace __cpo
  }   // namespace ranges

  using std::iter_rvalue_reference_t;

  // [iterator.concepts], iterator concepts
  // [iterator.concept.readable], concept indirectly_readable
  using std::indirectly_readable;

  using std::iter_common_reference_t;

  // [iterator.concept.writable], concept indirectly_writable
  using std::indirectly_writable;

  // [iterator.concept.winc], concept weakly_incrementable
  using std::weakly_incrementable;

  // [iterator.concept.inc], concept incrementable
  using std::incrementable;

  // [iterator.concept.iterator], concept input_or_output_iterator
  using std::input_or_output_iterator;

  // [iterator.concept.sentinel], concept sentinel_for
  using std::sentinel_for;

  // [iterator.concept.sizedsentinel], concept sized_sentinel_for
  using std::disable_sized_sentinel_for;

  using std::sized_sentinel_for;

  // [iterator.concept.input], concept input_iterator
  using std::input_iterator;

  // [iterator.concept.output], concept output_iterator
  using std::output_iterator;

  // [iterator.concept.forward], concept forward_iterator
  using std::forward_iterator;

  // [iterator.concept.bidir], concept bidirectional_iterator
  using std::bidirectional_iterator;

  // [iterator.concept.random.access], concept random_access_iterator
  using std::random_access_iterator;

  // [iterator.concept.contiguous], concept contiguous_iterator
  using std::contiguous_iterator;

  // [indirectcallable], indirect callable requirements
  // [indirectcallable.indirectinvocable], indirect callables
  using std::indirectly_unary_invocable;

  using std::indirectly_regular_unary_invocable;

  using std::indirect_unary_predicate;

  using std::indirect_binary_predicate;

  using std::indirect_equivalence_relation;

  using std::indirect_strict_weak_order;

  using std::indirect_result_t;

  // [projected], projected
  using std::projected;

  // [alg.req], common algorithm requirements
  // [alg.req.ind.move], concept indirectly_movable
  using std::indirectly_movable;

  using std::indirectly_movable_storable;

  // [alg.req.ind.copy], concept indirectly_copyable
  using std::indirectly_copyable;

  using std::indirectly_copyable_storable;

  // [alg.req.ind.swap], concept indirectly_swappable
  using std::indirectly_swappable;

  // [alg.req.ind.cmp], concept indirectly_comparable
  using std::indirectly_comparable;

  // [alg.req.permutable], concept permutable
  using std::permutable;

  // [alg.req.mergeable], concept mergeable
  using std::mergeable;

  // [alg.req.sortable], concept sortable
  using std::sortable;

  // [iterator.primitives], primitives
  // [std.iterator.tags], iterator tags
  using std::bidirectional_iterator_tag;
  using std::contiguous_iterator_tag;
  using std::forward_iterator_tag;
  using std::input_iterator_tag;
  using std::output_iterator_tag;
  using std::random_access_iterator_tag;

  // [iterator.operations], iterator operations
  using std::advance;
  using std::distance;
  using std::next;
  using std::prev;

  // [range.iter.ops], range iterator operations
  namespace ranges {
    // [range.iter.op.advance], ranges​::​advance
    using std::ranges::advance;

    // [range.iter.op.distance], ranges​::​distance
    using std::ranges::distance;

    // [range.iter.op.next], ranges​::​next
    using std::ranges::next;

    // [range.iter.op.prev], ranges​::​prev
    using std::ranges::prev;
  } // namespace ranges

  // [predef.iterators], predefined iterators and sentinels
  // [reverse.iterators], reverse iterators
  using std::reverse_iterator;

  using std::operator==;
  using std::operator!=;
  using std::operator<;
  using std::operator>;
  using std::operator<=;
  using std::operator>=;
  using std::operator<=>;

  using std::operator-;
  using std::operator+;

  using std::make_reverse_iterator;

  // using std::disable_sized_sentinel_for;

  // [insert.iterators], insert iterators
  using std::back_insert_iterator;
  using std::back_inserter;

  using std::front_insert_iterator;
  using std::front_inserter;

  using std::insert_iterator;
  using std::inserter;

  // [const.iterators], constant iterators and sentinels
  // [const.iterators.alias], alias templates
  //  using std::const_iterator;
  //  using std::const_sentinel;
  //  using std::iter_const_reference_t;

  // [const.iterators.iterator], class template basic_const_iterator
  //  using std::basic_const_iterator;

  // using std::common_type;

  //  using std::make_const_iterator;

  // [move.iterators], move iterators and sentinels
  using std::move_iterator;

  using std::make_move_iterator;

  using std::move_sentinel;

  using std::common_iterator;

  // [default.sentinel], default sentinel
  using std::default_sentinel;
  using std::default_sentinel_t;

  // [iterators.counted], counted iterators
  using std::counted_iterator;

  // [unreachable.sentinel], unreachable sentinel
  using std::unreachable_sentinel;
  using std::unreachable_sentinel_t;

  // [stream.iterators], stream iterators
  using std::istream_iterator;

  using std::ostream_iterator;

  using std::istreambuf_iterator;
  using std::ostreambuf_iterator;

  // [iterator.range], range access
  using std::begin;
  using std::cbegin;
  using std::cend;
  using std::crbegin;
  using std::crend;
  using std::end;
  using std::rbegin;
  using std::rend;

  using std::empty;
  using std::size;
  using std::ssize;

  using std::data;

  // [depr.iterator]
  using std::iterator;
} // namespace std
