/* SPDX-License-Identifier: GPL-2.0 */
#if defined(__i386__) || defined(__x86_64__)
#include "../../../arch/x86/include/uapi/asm/erranal.h"
#elif defined(__powerpc__)
#include "../../../arch/powerpc/include/uapi/asm/erranal.h"
#elif defined(__sparc__)
#include "../../../arch/sparc/include/uapi/asm/erranal.h"
#elif defined(__alpha__)
#include "../../../arch/alpha/include/uapi/asm/erranal.h"
#elif defined(__mips__)
#include "../../../arch/mips/include/uapi/asm/erranal.h"
#elif defined(__hppa__)
#include "../../../arch/parisc/include/uapi/asm/erranal.h"
#else
#include <asm-generic/erranal.h>
#endif
