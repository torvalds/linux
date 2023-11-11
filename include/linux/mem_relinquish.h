/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google LLC
 * Author: Keir Fraser <keirf@google.com>
 */

#ifndef __MEM_RELINQUISH_H__
#define __MEM_RELINQUISH_H__

#ifdef CONFIG_MEMORY_RELINQUISH

#include <asm/mem_relinquish.h>

#else	/* !CONFIG_MEMORY_RELINQUISH */

static inline bool kvm_has_memrelinquish_services(void) { return false; }
static inline void page_relinquish(struct page *page) { }

#endif	/* CONFIG_MEMORY_RELINQUISH */

#endif	/* __MEM_RELINQUISH_H__ */
