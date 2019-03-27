/******************************************************************************
 * gnttab.h
 * 
 * Two sets of functionality:
 * 1. Granting foreign access to our memory reservation.
 * 2. Accessing others' memory reservations via grant references.
 * (i.e., mechanisms for both sender and recipient of grant references)
 * 
 * Copyright (c) 2004-2005, K A Fraser
 * Copyright (c) 2005, Christopher Clark
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __ASM_GNTTAB_H__
#define __ASM_GNTTAB_H__

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/features.h>

#include <xen/interface/grant_table.h>

#define GNTTAB_LIST_END GRANT_REF_INVALID

struct gnttab_free_callback {
	struct gnttab_free_callback *next;
	void (*fn)(void *);
	void *arg;
	uint16_t count;
};

/*
 * Allocate a grant table reference and return it in *result. Returns
 * zero on success or errno on error.
 */
int gnttab_grant_foreign_access(domid_t domid, unsigned long frame,
    int flags, grant_ref_t *result);

/*
 * End access through the given grant reference, iff the grant entry is no
 * longer in use.  Return 1 if the grant entry was freed, 0 if it is still in
 * use.
 */
int gnttab_end_foreign_access_ref(grant_ref_t ref);

/*
 * Eventually end access through the given grant reference, and once that
 * access has been ended, free the given page too.  Access will be ended
 * immediately iff the grant entry is not in use, otherwise it will happen
 * some time later.  page may be 0, in which case no freeing will occur.
 */
void gnttab_end_foreign_access(grant_ref_t ref, void *page);

/*
 * Eventually end access through the given array of grant references.
 * Access will be ended immediately iff the grant entry is not in use,
 * otherwise it will happen some time later
 */
void gnttab_end_foreign_access_references(u_int count, grant_ref_t *refs);

int gnttab_grant_foreign_transfer(domid_t domid, unsigned long pfn, grant_ref_t *result);

unsigned long gnttab_end_foreign_transfer_ref(grant_ref_t ref);
unsigned long gnttab_end_foreign_transfer(grant_ref_t ref);

int gnttab_query_foreign_access(grant_ref_t ref);

/*
 * operations on reserved batches of grant references
 */
int gnttab_alloc_grant_references(uint16_t count, grant_ref_t *pprivate_head);

void gnttab_free_grant_reference(grant_ref_t ref);

void gnttab_free_grant_references(grant_ref_t head);

int gnttab_empty_grant_references(const grant_ref_t *pprivate_head);

int gnttab_claim_grant_reference(grant_ref_t *pprivate_head);

void gnttab_release_grant_reference(grant_ref_t *private_head,
				    grant_ref_t release);

void gnttab_request_free_callback(struct gnttab_free_callback *callback,
				  void (*fn)(void *), void *arg, uint16_t count);
void gnttab_cancel_free_callback(struct gnttab_free_callback *callback);

void gnttab_grant_foreign_access_ref(grant_ref_t ref, domid_t domid,
				     unsigned long frame, int flags);

void gnttab_grant_foreign_transfer_ref(grant_ref_t, domid_t domid,
				       unsigned long pfn);

int gnttab_suspend(void);
int gnttab_resume(device_t);

#endif /* __ASM_GNTTAB_H__ */
