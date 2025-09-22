/*===- llvm/Support/Solaris/sys/regset.h ------------------------*- C++ -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*
 *
 * This file works around excessive name space pollution from the system header
 * on Solaris hosts.
 *
 *===----------------------------------------------------------------------===*/

#ifndef LLVM_SUPPORT_SOLARIS_SYS_REGSET_H

#include_next <sys/regset.h>

#undef CS
#undef DS
#undef ES
#undef FS
#undef GS
#undef SS
#undef EAX
#undef ECX
#undef EDX
#undef EBX
#undef ESP
#undef EBP
#undef ESI
#undef EDI
#undef EIP
#undef UESP
#undef EFL
#undef ERR
#undef TRAPNO

#endif
