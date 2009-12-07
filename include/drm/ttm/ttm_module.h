/**************************************************************************
 *
 * Copyright 2008-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#ifndef _TTM_MODULE_H_
#define _TTM_MODULE_H_

#include <linux/kernel.h>
struct kobject;

#define TTM_PFX "[TTM] "

enum ttm_global_types {
	TTM_GLOBAL_TTM_MEM = 0,
	TTM_GLOBAL_TTM_BO,
	TTM_GLOBAL_TTM_OBJECT,
	TTM_GLOBAL_NUM
};

struct ttm_global_reference {
	enum ttm_global_types global_type;
	size_t size;
	void *object;
	int (*init) (struct ttm_global_reference *);
	void (*release) (struct ttm_global_reference *);
};

extern void ttm_global_init(void);
extern void ttm_global_release(void);
extern int ttm_global_item_ref(struct ttm_global_reference *ref);
extern void ttm_global_item_unref(struct ttm_global_reference *ref);
extern struct kobject *ttm_get_kobj(void);

#endif /* _TTM_MODULE_H_ */
