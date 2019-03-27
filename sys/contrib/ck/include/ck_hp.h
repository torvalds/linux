/*
 * Copyright 2010-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_HP_H
#define CK_HP_H

#include <ck_cc.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stack.h>

#ifndef CK_HP_CACHE
#define CK_HP_CACHE 512
#endif

struct ck_hp_hazard;
typedef void (*ck_hp_destructor_t)(void *);

struct ck_hp {
	ck_stack_t subscribers;
	unsigned int n_subscribers;
	unsigned int n_free;
	unsigned int threshold;
	unsigned int degree;
	ck_hp_destructor_t destroy;
};
typedef struct ck_hp ck_hp_t;

struct ck_hp_hazard {
	void *pointer;
	void *data;
	ck_stack_entry_t pending_entry;
};
typedef struct ck_hp_hazard ck_hp_hazard_t;

enum {
	CK_HP_USED = 0,
	CK_HP_FREE = 1
};

struct ck_hp_record {
	int state;
	void **pointers;
	void *cache[CK_HP_CACHE];
	struct ck_hp *global;
	ck_stack_t pending;
	unsigned int n_pending;
	ck_stack_entry_t global_entry;
	unsigned int n_peak;
	uint64_t n_reclamations;
} CK_CC_CACHELINE;
typedef struct ck_hp_record ck_hp_record_t;

CK_CC_INLINE static void
ck_hp_set(struct ck_hp_record *record, unsigned int i, void *pointer)
{

	ck_pr_store_ptr(&record->pointers[i], pointer);
	return;
}

CK_CC_INLINE static void
ck_hp_set_fence(struct ck_hp_record *record, unsigned int i, void *pointer)
{

#ifdef CK_MD_TSO
	ck_pr_fas_ptr(&record->pointers[i], pointer);
#else
	ck_pr_store_ptr(&record->pointers[i], pointer);
	ck_pr_fence_memory();
#endif

	return;
}

CK_CC_INLINE static void
ck_hp_clear(struct ck_hp_record *record)
{
	void **pointers = record->pointers;
	unsigned int i;

	for (i = 0; i < record->global->degree; i++)
		*pointers++ = NULL;

	return;
}

void ck_hp_init(ck_hp_t *, unsigned int, unsigned int, ck_hp_destructor_t);
void ck_hp_set_threshold(ck_hp_t *, unsigned int);
void ck_hp_register(ck_hp_t *, ck_hp_record_t *, void **);
void ck_hp_unregister(ck_hp_record_t *);
ck_hp_record_t *ck_hp_recycle(ck_hp_t *);
void ck_hp_reclaim(ck_hp_record_t *);
void ck_hp_free(ck_hp_record_t *, ck_hp_hazard_t *, void *, void *);
void ck_hp_retire(ck_hp_record_t *, ck_hp_hazard_t *, void *, void *);
void ck_hp_purge(ck_hp_record_t *);

#endif /* CK_HP_H */
