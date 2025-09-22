//===-- sanitizer_test_config.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of *Sanitizer runtime.
//
//===----------------------------------------------------------------------===//
#if !defined(INCLUDED_FROM_SANITIZER_TEST_UTILS_H)
# error "This file should be included into sanitizer_test_utils.h only"
#endif

#ifndef SANITIZER_TEST_CONFIG_H
#define SANITIZER_TEST_CONFIG_H

#include <vector>
#include <string>
#include <map>

#if SANITIZER_USE_DEJAGNU_GTEST
# include "dejagnu-gtest.h"
#else
# include "gtest/gtest.h"
#endif

#endif  // SANITIZER_TEST_CONFIG_H
