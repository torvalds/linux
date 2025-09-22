// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#if defined(__STDCPP_FLOAT16_T__)
  using std::float16_t;
#endif
#if defined(__STDCPP_FLOAT32_T__)
  using std::float32_t;
#endif
#if defined(__STDCPP_FLOAT64_T__)
  using std::float64_t;
#endif
#if defined(__STDCPP_FLOAT128_T__)
  using std::float128_t;
#endif
#if defined(__STDCPP_BFLOAT16_T__)
  using std::bfloat16_t;
#endif
} // namespace std
