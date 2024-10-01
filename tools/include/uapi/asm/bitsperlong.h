/* SPDX-License-Identifier: GPL-2.0 */
#if defined(__i386__) || defined(__x86_64__)
#include "../../../arch/x86/include/uapi/asm/bitsperlong.h"
#elif defined(__aarch64__)
#include "../../../arch/arm64/include/uapi/asm/bitsperlong.h"
#elif defined(__powerpc__)
#include "../../../arch/powerpc/include/uapi/asm/bitsperlong.h"
#elif defined(__s390__)
#include "../../../arch/s390/include/uapi/asm/bitsperlong.h"
#elif defined(__sparc__)
#include "../../../arch/sparc/include/uapi/asm/bitsperlong.h"
#elif defined(__mips__)
#include "../../../arch/mips/include/uapi/asm/bitsperlong.h"
#elif defined(__ia64__)
#include "../../../arch/ia64/include/uapi/asm/bitsperlong.h"
#elif defined(__riscv)
#include "../../../arch/riscv/include/uapi/asm/bitsperlong.h"
#elif defined(__alpha__)
#include "../../../arch/alpha/include/uapi/asm/bitsperlong.h"
#elif defined(__loongarch__)
#include "../../../arch/loongarch/include/uapi/asm/bitsperlong.h"
#else
#include <asm-generic/bitsperlong.h>
#endif
