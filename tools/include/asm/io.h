/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_ASM_IO_H
#define _TOOLS_ASM_IO_H

#if defined(__i386__) || defined(__x86_64__)
#include "../../arch/x86/include/asm/io.h"
#else
#include <asm-generic/io.h>
#endif

#endif /* _TOOLS_ASM_IO_H */
