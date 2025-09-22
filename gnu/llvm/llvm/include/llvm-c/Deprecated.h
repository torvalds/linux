/*===-- llvm-c/Deprecated.h - Deprecation macro -------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares LLVM_ATTRIBUTE_C_DEPRECATED() macro, which can be     *|
|* used to deprecate functions in the C interface.                            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_DEPRECATED_H
#define LLVM_C_DEPRECATED_H

#ifndef __has_feature
# define __has_feature(x) 0
#endif

// This is a variant of LLVM_ATTRIBUTE_DEPRECATED() that is compatible with
// C compilers.
#if __has_feature(attribute_deprecated_with_message)
# define LLVM_ATTRIBUTE_C_DEPRECATED(decl, message) \
  decl __attribute__((deprecated(message)))
#elif defined(__GNUC__)
# define LLVM_ATTRIBUTE_C_DEPRECATED(decl, message) \
  decl __attribute__((deprecated))
#elif defined(_MSC_VER)
# define LLVM_ATTRIBUTE_C_DEPRECATED(decl, message) \
  __declspec(deprecated(message)) decl
#else
# define LLVM_ATTRIBUTE_C_DEPRECATED(decl, message) \
  decl
#endif

#endif /* LLVM_C_DEPRECATED_H */
