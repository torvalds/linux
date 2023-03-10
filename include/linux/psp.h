/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PSP_H
#define __PSP_H

#ifdef CONFIG_X86
#include <linux/mem_encrypt.h>

#define __psp_pa(x)	__sme_pa(x)
#else
#define __psp_pa(x)	__pa(x)
#endif

#endif /* __PSP_H */
