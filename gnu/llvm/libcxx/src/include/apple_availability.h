//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_SRC_INCLUDE_APPLE_AVAILABILITY_H
#define _LIBCPP_SRC_INCLUDE_APPLE_AVAILABILITY_H

#if defined(__APPLE__)

#  if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#    if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101500
#      define _LIBCPP_USE_ULOCK
#    endif
#  elif defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
#    if __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ >= 130000
#      define _LIBCPP_USE_ULOCK
#    endif
#  elif defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)
#    if __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ >= 130000
#      define _LIBCPP_USE_ULOCK
#    endif
#  elif defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__)
#    if __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ >= 60000
#      define _LIBCPP_USE_ULOCK
#    endif
#  endif // __ENVIRONMENT_.*_VERSION_MIN_REQUIRED__

#endif // __APPLE__

#endif // _LIBCPP_SRC_INCLUDE_APPLE_AVAILABILITY_H
