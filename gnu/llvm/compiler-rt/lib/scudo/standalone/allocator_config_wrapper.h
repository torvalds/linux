//===-- allocator_config_wrapper.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ALLOCATOR_CONFIG_WRAPPER_H_
#define SCUDO_ALLOCATOR_CONFIG_WRAPPER_H_

#include "condition_variable.h"
#include "internal_defs.h"
#include "secondary.h"

namespace {

template <typename T> struct removeConst {
  using type = T;
};
template <typename T> struct removeConst<const T> {
  using type = T;
};

// This is only used for SFINAE when detecting if a type is defined.
template <typename T> struct voidAdaptor {
  using type = void;
};

// This is used for detecting the case that defines the flag with wrong type and
// it'll be viewed as undefined optional flag.
template <typename L, typename R> struct assertSameType {
  template <typename, typename> struct isSame {
    static constexpr bool value = false;
  };
  template <typename T> struct isSame<T, T> {
    static constexpr bool value = true;
  };
  static_assert(isSame<L, R>::value, "Flag type mismatches");
  using type = R;
};

} // namespace

namespace scudo {

#define OPTIONAL_TEMPLATE(TYPE, NAME, DEFAULT, MEMBER)                         \
  template <typename Config, typename = TYPE> struct NAME##State {             \
    static constexpr removeConst<TYPE>::type getValue() { return DEFAULT; }    \
  };                                                                           \
  template <typename Config>                                                   \
  struct NAME##State<                                                          \
      Config, typename assertSameType<decltype(Config::MEMBER), TYPE>::type> { \
    static constexpr removeConst<TYPE>::type getValue() {                      \
      return Config::MEMBER;                                                   \
    }                                                                          \
  };

#define OPTIONAL_TYPE_TEMPLATE(NAME, DEFAULT, MEMBER)                          \
  template <typename Config, typename Void = void> struct NAME##Type {         \
    static constexpr bool enabled() { return false; }                          \
    using NAME = DEFAULT;                                                      \
  };                                                                           \
  template <typename Config>                                                   \
  struct NAME##Type<Config,                                                    \
                    typename voidAdaptor<typename Config::MEMBER>::type> {     \
    static constexpr bool enabled() { return true; }                           \
    using NAME = typename Config::MEMBER;                                      \
  };

template <typename AllocatorConfig> struct BaseConfig {
#define BASE_REQUIRED_TEMPLATE_TYPE(NAME)                                      \
  template <typename T> using NAME = typename AllocatorConfig::template NAME<T>;

#define BASE_OPTIONAL(TYPE, NAME, DEFAULT)                                     \
  OPTIONAL_TEMPLATE(TYPE, NAME, DEFAULT, NAME)                                 \
  static constexpr removeConst<TYPE>::type get##NAME() {                       \
    return NAME##State<AllocatorConfig>::getValue();                           \
  }

#include "allocator_config.def"
}; // BaseConfig

template <typename AllocatorConfig> struct PrimaryConfig {
  // TODO: Pass this flag through template argument to remove this hard-coded
  //       function.
  static constexpr bool getMaySupportMemoryTagging() {
    return BaseConfig<AllocatorConfig>::getMaySupportMemoryTagging();
  }

#define PRIMARY_REQUIRED_TYPE(NAME)                                            \
  using NAME = typename AllocatorConfig::Primary::NAME;

#define PRIMARY_REQUIRED(TYPE, NAME)                                           \
  static constexpr removeConst<TYPE>::type get##NAME() {                       \
    return AllocatorConfig::Primary::NAME;                                     \
  }

#define PRIMARY_OPTIONAL(TYPE, NAME, DEFAULT)                                  \
  OPTIONAL_TEMPLATE(TYPE, NAME, DEFAULT, NAME)                                 \
  static constexpr removeConst<TYPE>::type get##NAME() {                       \
    return NAME##State<typename AllocatorConfig::Primary>::getValue();         \
  }

#define PRIMARY_OPTIONAL_TYPE(NAME, DEFAULT)                                   \
  OPTIONAL_TYPE_TEMPLATE(NAME, DEFAULT, NAME)                                  \
  static constexpr bool has##NAME() {                                          \
    return NAME##Type<typename AllocatorConfig::Primary>::enabled();           \
  }                                                                            \
  using NAME = typename NAME##Type<typename AllocatorConfig::Primary>::NAME;

#include "allocator_config.def"

}; // PrimaryConfig

template <typename AllocatorConfig> struct SecondaryConfig {
  // TODO: Pass this flag through template argument to remove this hard-coded
  //       function.
  static constexpr bool getMaySupportMemoryTagging() {
    return BaseConfig<AllocatorConfig>::getMaySupportMemoryTagging();
  }

#define SECONDARY_REQUIRED_TEMPLATE_TYPE(NAME)                                 \
  template <typename T>                                                        \
  using NAME = typename AllocatorConfig::Secondary::template NAME<T>;
#include "allocator_config.def"

  struct CacheConfig {
    // TODO: Pass this flag through template argument to remove this hard-coded
    //       function.
    static constexpr bool getMaySupportMemoryTagging() {
      return BaseConfig<AllocatorConfig>::getMaySupportMemoryTagging();
    }

#define SECONDARY_CACHE_OPTIONAL(TYPE, NAME, DEFAULT)                          \
  OPTIONAL_TEMPLATE(TYPE, NAME, DEFAULT, Cache::NAME)                          \
  static constexpr removeConst<TYPE>::type get##NAME() {                       \
    return NAME##State<typename AllocatorConfig::Secondary>::getValue();       \
  }
#include "allocator_config.def"
  }; // CacheConfig
};   // SecondaryConfig

#undef OPTIONAL_TEMPLATE
#undef OPTIONAL_TEMPLATE_TYPE

} // namespace scudo

#endif // SCUDO_ALLOCATOR_CONFIG_WRAPPER_H_
