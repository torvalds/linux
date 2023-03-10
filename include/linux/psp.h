/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PSP_H
#define __PSP_H

#ifdef CONFIG_X86
#include <linux/mem_encrypt.h>

#define __psp_pa(x)	__sme_pa(x)
#else
#define __psp_pa(x)	__pa(x)
#endif

/*
 * Fields and bits used by most PSP mailboxes
 *
 * Note: Some mailboxes (such as SEV) have extra bits or different meanings
 * and should include an appropriate local definition in their source file.
 */
#define PSP_CMDRESP_STS		GENMASK(15, 0)
#define PSP_CMDRESP_CMD		GENMASK(23, 16)
#define PSP_CMDRESP_RESERVED	GENMASK(29, 24)
#define PSP_CMDRESP_RECOVERY	BIT(30)
#define PSP_CMDRESP_RESP	BIT(31)

#endif /* __PSP_H */
