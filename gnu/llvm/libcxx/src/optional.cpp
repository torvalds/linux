//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <optional>
#include <stdexcept>

namespace std {

bad_optional_access::~bad_optional_access() noexcept = default;

const char* bad_optional_access::what() const noexcept { return "bad_optional_access"; }

} // namespace std

#include <experimental/__config>

//  Preserve std::experimental::bad_optional_access for ABI compatibility
//  Even though it no longer exists in a header file
_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL

class _LIBCPP_EXPORTED_FROM_ABI _LIBCPP_AVAILABILITY_BAD_OPTIONAL_ACCESS bad_optional_access : public std::logic_error {
public:
  bad_optional_access() : std::logic_error("Bad optional Access") {}

  // Get the key function ~bad_optional_access() into the dylib
  virtual ~bad_optional_access() noexcept;
};

bad_optional_access::~bad_optional_access() noexcept = default;

_LIBCPP_END_NAMESPACE_EXPERIMENTAL
