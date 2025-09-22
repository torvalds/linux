// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  namespace ranges {
    // [algorithms.results], algorithm result types
    using std::ranges::in_found_result;
    using std::ranges::in_fun_result;
    using std::ranges::in_in_out_result;
    using std::ranges::in_in_result;
    using std::ranges::in_out_out_result;
    using std::ranges::in_out_result;
#if _LIBCPP_STD_VER >= 23
    using std::ranges::in_value_result;
#endif
    using std::ranges::min_max_result;
    // using std::ranges::out_value_result;
  } // namespace ranges

  // [alg.nonmodifying], non-modifying sequence operations
  // [alg.all.of], all of
  using std::all_of;
  namespace ranges {
    using std::ranges::all_of;
  }

  // [alg.any.of], any of
  using std::any_of;
  namespace ranges {
    using std::ranges::any_of;
  }

  // [alg.none.of], none of
  using std::none_of;
  namespace ranges {
    using std::ranges::none_of;
  }

#if _LIBCPP_STD_VER >= 23
  // [alg.contains], contains
  namespace ranges {
    using std::ranges::contains;
    using std::ranges::contains_subrange;
  } // namespace ranges
#endif // _LIBCPP_STD_VER >= 23

  // [alg.foreach], for each
  using std::for_each;

  namespace ranges {
    using std::ranges::for_each;
    using std::ranges::for_each_result;
  } // namespace ranges

  using std::for_each_n;

  namespace ranges {
    using std::ranges::for_each_n_result;

    using std::ranges::for_each_n;
  } // namespace ranges

  // [alg.find], find
  using std::find;
  using std::find_if;
  using std::find_if_not;

  namespace ranges {
    using std::ranges::find;
    using std::ranges::find_if;
    using std::ranges::find_if_not;
  } // namespace ranges

#if _LIBCPP_STD_VER >= 23
  // [alg.find.last], find last
  namespace ranges {
    using std::ranges::find_last;
    using std::ranges::find_last_if;
    using std::ranges::find_last_if_not;
  } // namespace ranges
#endif

  // [alg.find.end], find end
  using std::find_end;

  namespace ranges {
    using std::ranges::find_end;
  }

  // [alg.find.first.of], find first
  using std::find_first_of;

  namespace ranges {
    using std::ranges::find_first_of;
  }

  // [alg.adjacent.find], adjacent find
  using std::adjacent_find;

  namespace ranges {
    using std::ranges::adjacent_find;
  }

  // [alg.count], count
  using std::count;
  using std::count_if;

  namespace ranges {
    using std::ranges::count;
    using std::ranges::count_if;
  } // namespace ranges

  // [mismatch], mismatch
  using std::mismatch;

  namespace ranges {
    using std::ranges::mismatch_result;

    using std::ranges::mismatch;
  } // namespace ranges

  // [alg.equal], equal
  using std::equal;

  namespace ranges {
    using std::ranges::equal;
  }

  // [alg.is.permutation], is permutation
  using std::is_permutation;

  namespace ranges {
    using std::ranges::is_permutation;
  }

  // [alg.search], search
  using std::search;

  namespace ranges {
    using std::ranges::search;
  }

  using std::search_n;

  namespace ranges {
    using std::ranges::search_n;
  }

  namespace ranges {
#if _LIBCPP_STD_VER >= 23
    // [alg.starts.with], starts with
    using std::ranges::starts_with;

    // [alg.ends.with], ends with
    using std::ranges::ends_with;

    // [alg.fold], fold
    using std::ranges::fold_left;
    using std::ranges::fold_left_with_iter;
    using std::ranges::fold_left_with_iter_result;
#  if 0
    using std::ranges::fold_left_first;
    using std::ranges::fold_right;
    using std::ranges::fold_right_last;
    using std::ranges::fold_left_with_iter;
    using std::ranges::fold_left_first_with_iter;
    using std::ranges::fold_left_first_with_iter;
#  endif
#endif // _LIBCPP_STD_VER >= 23
  }    // namespace ranges

  // [alg.modifying.operations], mutating sequence operations
  // [alg.copy], copy
  using std::copy;

  namespace ranges {
    using std::ranges::copy;
    using std::ranges::copy_result;
  } // namespace ranges

  using std::copy_n;

  namespace ranges {
    using std::ranges::copy_n;
    using std::ranges::copy_n_result;
  } // namespace ranges

  using std::copy_if;

  namespace ranges {
    using std::ranges::copy_if;
    using std::ranges::copy_if_result;
  } // namespace ranges

  using std::copy_backward;

  namespace ranges {
    using std::ranges::copy_backward;
    using std::ranges::copy_backward_result;
  } // namespace ranges

  // [alg.move], move
  using std::move;

  namespace ranges {
    using std::ranges::move;
    using std::ranges::move_result;
  } // namespace ranges

  using std::move_backward;

  namespace ranges {
    using std::ranges::move_backward;
    using std::ranges::move_backward_result;
  } // namespace ranges

  // [alg.swap], swap
  using std::swap_ranges;

  namespace ranges {
    using std::ranges::swap_ranges;
    using std::ranges::swap_ranges_result;
  } // namespace ranges

  using std::iter_swap;

  // [alg.transform], transform
  using std::transform;

  namespace ranges {
    using std::ranges::binary_transform_result;
    using std::ranges::unary_transform_result;

    using std::ranges::transform;

  } // namespace ranges

  using std::replace;
  using std::replace_if;

  namespace ranges {
    using std::ranges::replace;
    using std::ranges::replace_if;
  } // namespace ranges

  using std::replace_copy;
  using std::replace_copy_if;

  namespace ranges {
    using std::ranges::replace_copy;
    using std::ranges::replace_copy_if;
    using std::ranges::replace_copy_if_result;
    using std::ranges::replace_copy_result;
  } // namespace ranges

  // [alg.fill], fill
  using std::fill;
  using std::fill_n;

  namespace ranges {
    using std::ranges::fill;
    using std::ranges::fill_n;
  } // namespace ranges

  // [alg.generate], generate
  using std::generate;
  using std::generate_n;

  namespace ranges {
    using std::ranges::generate;
    using std::ranges::generate_n;
  } // namespace ranges

  // [alg.remove], remove
  using std::remove;
  using std::remove_if;

  namespace ranges {
    using std::ranges::remove;
    using std::ranges::remove_if;
  } // namespace ranges

  using std::remove_copy;
  using std::remove_copy_if;
  namespace ranges {
    using std::ranges::remove_copy;
    using std::ranges::remove_copy_if;
    using std::ranges::remove_copy_if_result;
    using std::ranges::remove_copy_result;
  } // namespace ranges

  // [alg.unique], unique
  using std::unique;

  namespace ranges {
    using std::ranges::unique;
  }

  using std::unique_copy;

  namespace ranges {
    using std::ranges::unique_copy;
    using std::ranges::unique_copy_result;
  } // namespace ranges

  // [alg.reverse], reverse
  using std::reverse;

  namespace ranges {
    using std::ranges::reverse;
  }

  using std::reverse_copy;

  namespace ranges {
    using std::ranges::reverse_copy;
    using std::ranges::reverse_copy_result;
  } // namespace ranges

  // [alg.rotate], rotate
  using std::rotate;

  namespace ranges {
    using std::ranges::rotate;
  }

  using std::rotate_copy;

  namespace ranges {
    using std::ranges::rotate_copy;
    using std::ranges::rotate_copy_result;
  } // namespace ranges

  // [alg.random.sample], sample
  using std::sample;

  namespace ranges {
    using std::ranges::sample;
  }

  // [alg.random.shuffle], shuffle
  using std::shuffle;

  namespace ranges {
    using std::ranges::shuffle;
  }

  // [alg.shift], shift
  using std::shift_left;

  namespace ranges {
    // using std::ranges::shift_left;
  }

  using std::shift_right;

  namespace ranges {
    // using std::ranges::shift_right;
  }

  // [alg.sorting], sorting and related operations
  // [alg.sort], sorting
  using std::sort;

  namespace ranges {
    using std::ranges::sort;
  }

  using std::stable_sort;

  namespace ranges {
    using std::ranges::stable_sort;
  }

  using std::partial_sort;

  namespace ranges {
    using std::ranges::partial_sort;
  }
  using std::partial_sort_copy;

  namespace ranges {
    using std::ranges::partial_sort_copy;
    using std::ranges::partial_sort_copy_result;
  } // namespace ranges

  using std::is_sorted;
  using std::is_sorted_until;

  namespace ranges {
    using std::ranges::is_sorted;
    using std::ranges::is_sorted_until;
  } // namespace ranges

  // [alg.nth.element], Nth element
  using std::nth_element;

  namespace ranges {
    using std::ranges::nth_element;
  }

  // [alg.binary.search], binary search
  using std::lower_bound;

  namespace ranges {
    using std::ranges::lower_bound;
  }

  using std::upper_bound;

  namespace ranges {
    using std::ranges::upper_bound;
  }

  using std::equal_range;

  namespace ranges {
    using std::ranges::equal_range;
  }

  using std::binary_search;

  namespace ranges {
    using std::ranges::binary_search;
  }

  // [alg.partitions], partitions
  using std::is_partitioned;

  namespace ranges {
    using std::ranges::is_partitioned;
  }

  using std::partition;

  namespace ranges {
    using std::ranges::partition;
  }

  using std::stable_partition;

  namespace ranges {
    using std::ranges::stable_partition;
  }

  using std::partition_copy;

  namespace ranges {
    using std::ranges::partition_copy;
    using std::ranges::partition_copy_result;
  } // namespace ranges

  using std::partition_point;

  namespace ranges {
    using std::ranges::partition_point;
  }
  // [alg.merge], merge
  using std::merge;
  namespace ranges {
    using std::ranges::merge;
    using std::ranges::merge_result;
  } // namespace ranges

  using std::inplace_merge;

  namespace ranges {
    using std::ranges::inplace_merge;
  }

  // [alg.set.operations], set operations
  using std::includes;
  namespace ranges {
    using std::ranges::includes;
  }

  using std::set_union;

  namespace ranges {
    using std::ranges::set_union;
    using std::ranges::set_union_result;
  } // namespace ranges

  using std::set_intersection;
  namespace ranges {
    using std::ranges::set_intersection;
    using std::ranges::set_intersection_result;
  } // namespace ranges

  using std::set_difference;

  namespace ranges {
    using std::ranges::set_difference;
    using std::ranges::set_difference_result;
  } // namespace ranges

  using std::set_symmetric_difference;

  namespace ranges {
    using std::ranges::set_symmetric_difference_result;

    using std::ranges::set_symmetric_difference;
  } // namespace ranges

  // [alg.heap.operations], heap operations
  using std::push_heap;

  namespace ranges {
    using std::ranges::push_heap;
  }

  using std::pop_heap;

  namespace ranges {
    using std::ranges::pop_heap;
  }

  using std::make_heap;

  namespace ranges {
    using std::ranges::make_heap;
  }

  using std::sort_heap;

  namespace ranges {
    using std::ranges::sort_heap;
  }

  using std::is_heap;

  namespace ranges {
    using std::ranges::is_heap;
  }

  using std::is_heap_until;

  namespace ranges {
    using std::ranges::is_heap_until;
  }

  // [alg.min.max], minimum and maximum
  using std::min;

  namespace ranges {
    using std::ranges::min;
  }

  using std::max;

  namespace ranges {
    using std::ranges::max;
  }

  using std::minmax;

  namespace ranges {
    using std::ranges::minmax_result;

    using std::ranges::minmax;
  } // namespace ranges

  using std::min_element;

  namespace ranges {
    using std::ranges::min_element;
  }

  using std::max_element;

  namespace ranges {
    using std::ranges::max_element;
  }

  using std::minmax_element;

  namespace ranges {
    using std::ranges::minmax_element_result;

    using std::ranges::minmax_element;
  } // namespace ranges
    // [alg.clamp], bounded value
  using std::clamp;

  namespace ranges {
    using std::ranges::clamp;
  }

  // [alg.lex.comparison], lexicographical comparison
  using std::lexicographical_compare;

  namespace ranges {
    using std::ranges::lexicographical_compare;
  }

  // [alg.three.way], three-way comparison algorithms
  using std::lexicographical_compare_three_way;

  // [alg.permutation.generators], permutations
  using std::next_permutation;

  namespace ranges {
    using std::ranges::next_permutation_result;

    using std::ranges::next_permutation;
  } // namespace ranges

  using std::prev_permutation;

  namespace ranges {
    using std::ranges::prev_permutation_result;

    using std::ranges::prev_permutation;
  } // namespace ranges

} // namespace std
