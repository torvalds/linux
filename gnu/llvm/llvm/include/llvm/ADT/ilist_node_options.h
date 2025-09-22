//===- llvm/ADT/ilist_node_options.h - ilist_node Options -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_NODE_OPTIONS_H
#define LLVM_ADT_ILIST_NODE_OPTIONS_H

#include "llvm/Config/abi-breaking.h"

#include <type_traits>

namespace llvm {

template <bool EnableSentinelTracking, class ParentTy> class ilist_node_base;
template <bool EnableSentinelTracking, class ParentTy> class ilist_base;

/// Option to choose whether to track sentinels.
///
/// This option affects the ABI for the nodes.  When not specified explicitly,
/// the ABI depends on LLVM_ENABLE_ABI_BREAKING_CHECKS.  Specify explicitly to
/// enable \a ilist_node::isSentinel().
template <bool EnableSentinelTracking> struct ilist_sentinel_tracking {};

/// Option to specify a tag for the node type.
///
/// This option allows a single value type to be inserted in multiple lists
/// simultaneously.  See \a ilist_node for usage examples.
template <class Tag> struct ilist_tag {};

/// Option to add extra bits to the ilist_iterator.
///
/// Some use-cases (debug-info) need to know whether a position is intended
/// to be half-open or fully open, i.e. whether to include any immediately
/// adjacent debug-info in an operation. This option adds two bits to the
/// iterator class to store that information.
template <bool ExtraIteratorBits> struct ilist_iterator_bits {};

/// Option to add a pointer to this list's owner in every node.
///
/// This option causes the \a ilist_base_node for this list to contain a pointer
/// ParentTy *Parent, returned by \a ilist_base_node::getNodeBaseParent() and
/// set by \a ilist_base_node::setNodeBaseParent(ParentTy *Parent). The parent
/// value is not set automatically; the ilist owner should set itself as the
/// parent of the list sentinel, and the parent should be set on each node
/// inserted into the list. This value is also not used by
/// \a ilist_node_with_parent::getNodeParent(), but is used by \a
/// ilist_iterator::getNodeParent(), which allows the parent to be fetched from
/// any valid (non-null) iterator to this list, including the sentinel.
template <class ParentTy> struct ilist_parent {};

namespace ilist_detail {

/// Helper trait for recording whether an option is specified explicitly.
template <bool IsExplicit> struct explicitness {
  static const bool is_explicit = IsExplicit;
};
typedef explicitness<true> is_explicit;
typedef explicitness<false> is_implicit;

/// Check whether an option is valid.
///
/// The steps for adding and enabling a new ilist option include:
/// \li define the option, ilist_foo<Bar>, above;
/// \li add new parameters for Bar to \a ilist_detail::node_options;
/// \li add an extraction meta-function, ilist_detail::extract_foo;
/// \li call extract_foo from \a ilist_detail::compute_node_options and pass it
/// into \a ilist_detail::node_options; and
/// \li specialize \c is_valid_option<ilist_foo<Bar>> to inherit from \c
/// std::true_type to get static assertions passing in \a simple_ilist and \a
/// ilist_node.
template <class Option> struct is_valid_option : std::false_type {};

/// Extract sentinel tracking option.
///
/// Look through \p Options for the \a ilist_sentinel_tracking option, with the
/// default depending on LLVM_ENABLE_ABI_BREAKING_CHECKS.
template <class... Options> struct extract_sentinel_tracking;
template <bool EnableSentinelTracking, class... Options>
struct extract_sentinel_tracking<
    ilist_sentinel_tracking<EnableSentinelTracking>, Options...>
    : std::integral_constant<bool, EnableSentinelTracking>, is_explicit {};
template <class Option1, class... Options>
struct extract_sentinel_tracking<Option1, Options...>
    : extract_sentinel_tracking<Options...> {};
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
template <> struct extract_sentinel_tracking<> : std::true_type, is_implicit {};
#else
template <>
struct extract_sentinel_tracking<> : std::false_type, is_implicit {};
#endif
template <bool EnableSentinelTracking>
struct is_valid_option<ilist_sentinel_tracking<EnableSentinelTracking>>
    : std::true_type {};

/// Extract custom tag option.
///
/// Look through \p Options for the \a ilist_tag option, pulling out the
/// custom tag type, using void as a default.
template <class... Options> struct extract_tag;
template <class Tag, class... Options>
struct extract_tag<ilist_tag<Tag>, Options...> {
  typedef Tag type;
};
template <class Option1, class... Options>
struct extract_tag<Option1, Options...> : extract_tag<Options...> {};
template <> struct extract_tag<> {
  typedef void type;
};
template <class Tag> struct is_valid_option<ilist_tag<Tag>> : std::true_type {};

/// Extract iterator bits option.
///
/// Look through \p Options for the \a ilist_iterator_bits option. Defaults
/// to false.
template <class... Options> struct extract_iterator_bits;
template <bool IteratorBits, class... Options>
struct extract_iterator_bits<ilist_iterator_bits<IteratorBits>, Options...>
    : std::integral_constant<bool, IteratorBits> {};
template <class Option1, class... Options>
struct extract_iterator_bits<Option1, Options...>
    : extract_iterator_bits<Options...> {};
template <> struct extract_iterator_bits<> : std::false_type, is_implicit {};
template <bool IteratorBits>
struct is_valid_option<ilist_iterator_bits<IteratorBits>> : std::true_type {};

/// Extract node parent option.
///
/// Look through \p Options for the \a ilist_parent option, pulling out the
/// custom parent type, using void as a default.
template <class... Options> struct extract_parent;
template <class ParentTy, class... Options>
struct extract_parent<ilist_parent<ParentTy>, Options...> {
  typedef ParentTy type;
};
template <class Option1, class... Options>
struct extract_parent<Option1, Options...> : extract_parent<Options...> {};
template <> struct extract_parent<> { typedef void type; };
template <class ParentTy>
struct is_valid_option<ilist_parent<ParentTy>> : std::true_type {};

/// Check whether options are valid.
///
/// The conjunction of \a is_valid_option on each individual option.
template <class... Options> struct check_options;
template <> struct check_options<> : std::true_type {};
template <class Option1, class... Options>
struct check_options<Option1, Options...>
    : std::integral_constant<bool, is_valid_option<Option1>::value &&
                                       check_options<Options...>::value> {};

/// Traits for options for \a ilist_node.
///
/// This is usually computed via \a compute_node_options.
template <class T, bool EnableSentinelTracking, bool IsSentinelTrackingExplicit,
          class TagT, bool HasIteratorBits, class ParentTy>
struct node_options {
  typedef T value_type;
  typedef T *pointer;
  typedef T &reference;
  typedef const T *const_pointer;
  typedef const T &const_reference;

  static const bool enable_sentinel_tracking = EnableSentinelTracking;
  static const bool is_sentinel_tracking_explicit = IsSentinelTrackingExplicit;
  static const bool has_iterator_bits = HasIteratorBits;
  typedef TagT tag;
  typedef ParentTy parent_ty;
  typedef ilist_node_base<enable_sentinel_tracking, parent_ty> node_base_type;
  typedef ilist_base<enable_sentinel_tracking, parent_ty> list_base_type;
};

template <class T, class... Options> struct compute_node_options {
  typedef node_options<T, extract_sentinel_tracking<Options...>::value,
                       extract_sentinel_tracking<Options...>::is_explicit,
                       typename extract_tag<Options...>::type,
                       extract_iterator_bits<Options...>::value,
                       typename extract_parent<Options...>::type>
      type;
};

} // end namespace ilist_detail
} // end namespace llvm

#endif // LLVM_ADT_ILIST_NODE_OPTIONS_H
