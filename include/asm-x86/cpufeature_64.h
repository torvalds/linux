/*
 * cpufeature_32.h
 *
 * Defines x86 CPU feature bits
 */

#ifndef __ASM_X8664_CPUFEATURE_H
#define __ASM_X8664_CPUFEATURE_H

#include "cpufeature_32.h"

#undef  cpu_has_vme
#define cpu_has_vme            0

#undef  cpu_has_pae
#define cpu_has_pae            ___BUG___

#undef  cpu_has_mp
#define cpu_has_mp             1 /* XXX */

#undef  cpu_has_k6_mtrr
#define cpu_has_k6_mtrr        0

#undef  cpu_has_cyrix_arr
#define cpu_has_cyrix_arr      0

#undef  cpu_has_centaur_mcr
#define cpu_has_centaur_mcr    0

#endif /* __ASM_X8664_CPUFEATURE_H */
