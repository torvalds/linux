/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MEM_ENCRYPT_H__
#define __MEM_ENCRYPT_H__

#ifndef __ASSEMBLY__

#ifdef CONFIG_ARCH_HAS_MEM_ENCRYPT

#include <asm/mem_encrypt.h>

#else	/* !CONFIG_ARCH_HAS_MEM_ENCRYPT */

#define sme_me_mask	0UL

#endif	/* CONFIG_ARCH_HAS_MEM_ENCRYPT */

static inline bool sme_active(void)
{
	return !!sme_me_mask;
}

#endif	/* __ASSEMBLY__ */

#endif	/* __MEM_ENCRYPT_H__ */
