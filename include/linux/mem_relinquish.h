/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google LLC
 * Author: Keir Fraser <keirf@google.com>
 */

#ifndef __MEM_RELINQUISH_H__
#define __MEM_RELINQUISH_H__

#ifdef CONFIG_ARCH_HAS_MEM_RELINQUISH

#include <asm/mem_relinquish.h>

#else	/* !CONFIG_ARCH_HAS_MEM_RELINQUISH */

static inline void page_relinquish(struct page *page) { }

#endif	/* CONFIG_ARCH_HAS_MEM_RELINQUISH */

#endif	/* __MEM_RELINQUISH_H__ */
