//===-- crtend.c - End of constructors and destructors --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

// Put 4-byte zero which is the length field in FDE at the end as a terminator.
const int32_t __EH_FRAME_LIST_END__[]
    __attribute__((section(".eh_frame"), aligned(sizeof(int32_t)),
                   visibility("hidden"), used)) = {0};

#ifndef CRT_HAS_INITFINI_ARRAY
typedef void (*fp)(void);
fp __CTOR_LIST_END__[]
    __attribute__((section(".ctors"), visibility("hidden"), used)) = {0};
fp __DTOR_LIST_END__[]
    __attribute__((section(".dtors"), visibility("hidden"), used)) = {0};
#endif
