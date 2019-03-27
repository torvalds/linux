/*
 * Copyright 2011-2015 Samy Al Bahra.
 * Copyright 2011 David Joseph.
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

#ifndef CK_BARRIER_H
#define CK_BARRIER_H

#include <ck_spinlock.h>

struct ck_barrier_centralized {
	unsigned int value;
	unsigned int sense;
};
typedef struct ck_barrier_centralized ck_barrier_centralized_t;

struct ck_barrier_centralized_state {
	unsigned int sense;
};
typedef struct ck_barrier_centralized_state ck_barrier_centralized_state_t;

#define CK_BARRIER_CENTRALIZED_INITIALIZER 	 {0, 0}
#define CK_BARRIER_CENTRALIZED_STATE_INITIALIZER {0}

void ck_barrier_centralized(ck_barrier_centralized_t *,
    ck_barrier_centralized_state_t *, unsigned int);

struct ck_barrier_combining_group {
	unsigned int k;
	unsigned int count;
	unsigned int sense;
	struct ck_barrier_combining_group *parent;
	struct ck_barrier_combining_group *left;
	struct ck_barrier_combining_group *right;
	struct ck_barrier_combining_group *next;
} CK_CC_CACHELINE;
typedef struct ck_barrier_combining_group ck_barrier_combining_group_t;

struct ck_barrier_combining_state {
	unsigned int sense;
};
typedef struct ck_barrier_combining_state ck_barrier_combining_state_t;

#define CK_BARRIER_COMBINING_STATE_INITIALIZER {~0}

struct ck_barrier_combining {
	struct ck_barrier_combining_group *root;
	ck_spinlock_fas_t mutex;
};
typedef struct ck_barrier_combining ck_barrier_combining_t;

void ck_barrier_combining_init(ck_barrier_combining_t *, ck_barrier_combining_group_t *);

void ck_barrier_combining_group_init(ck_barrier_combining_t *,
    ck_barrier_combining_group_t *, unsigned int);

void ck_barrier_combining(ck_barrier_combining_t *,
    ck_barrier_combining_group_t *,
    ck_barrier_combining_state_t *);

struct ck_barrier_dissemination_flag {
	unsigned int tflag;
	unsigned int *pflag;
};
typedef struct ck_barrier_dissemination_flag ck_barrier_dissemination_flag_t;

struct ck_barrier_dissemination {
	unsigned int nthr;
	unsigned int size;
	unsigned int tid;
	struct ck_barrier_dissemination_flag *flags[2];
};
typedef struct ck_barrier_dissemination ck_barrier_dissemination_t;

struct ck_barrier_dissemination_state {
	int 		parity;
	unsigned int 	sense;
	unsigned int	tid;
};
typedef struct ck_barrier_dissemination_state ck_barrier_dissemination_state_t;

void ck_barrier_dissemination_init(ck_barrier_dissemination_t *,
    ck_barrier_dissemination_flag_t **, unsigned int);

void ck_barrier_dissemination_subscribe(ck_barrier_dissemination_t *,
    ck_barrier_dissemination_state_t *);

unsigned int ck_barrier_dissemination_size(unsigned int);

void ck_barrier_dissemination(ck_barrier_dissemination_t *,
    ck_barrier_dissemination_state_t *);

struct ck_barrier_tournament_round {
	int role;
	unsigned int *opponent;
	unsigned int flag;
};
typedef struct ck_barrier_tournament_round ck_barrier_tournament_round_t;

struct ck_barrier_tournament {
	unsigned int tid;
	unsigned int size;
	struct ck_barrier_tournament_round **rounds;
};
typedef struct ck_barrier_tournament ck_barrier_tournament_t;

struct ck_barrier_tournament_state {
	unsigned int sense;
	unsigned int vpid;
};
typedef struct ck_barrier_tournament_state ck_barrier_tournament_state_t;

void ck_barrier_tournament_subscribe(ck_barrier_tournament_t *,
				     ck_barrier_tournament_state_t *);
void ck_barrier_tournament_init(ck_barrier_tournament_t *,
				ck_barrier_tournament_round_t **,
				unsigned int);
unsigned int ck_barrier_tournament_size(unsigned int);
void ck_barrier_tournament(ck_barrier_tournament_t *, ck_barrier_tournament_state_t *);

struct ck_barrier_mcs {
	unsigned int tid;
	unsigned int *children[2];
	unsigned int childnotready[4];
	unsigned int dummy;
	unsigned int havechild[4];
	unsigned int *parent;
	unsigned int parentsense;
};
typedef struct ck_barrier_mcs ck_barrier_mcs_t;

struct ck_barrier_mcs_state {
	unsigned int sense;
	unsigned int vpid;
};
typedef struct ck_barrier_mcs_state ck_barrier_mcs_state_t;

void ck_barrier_mcs_init(ck_barrier_mcs_t *, unsigned int);
void ck_barrier_mcs_subscribe(ck_barrier_mcs_t *, ck_barrier_mcs_state_t *);
void ck_barrier_mcs(ck_barrier_mcs_t *, ck_barrier_mcs_state_t *);

#endif /* CK_BARRIER_H */
