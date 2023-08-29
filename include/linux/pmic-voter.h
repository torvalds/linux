/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __PMIC_VOTER_H
#define __PMIC_VOTER_H

#include <linux/mutex.h>

struct votable;

enum votable_type {
	VOTE_MIN,
	VOTE_MAX,
	VOTE_SET_ANY,
	NUM_VOTABLE_TYPES,
};

extern bool is_client_vote_enabled(struct votable *votable, const char *client_str);
extern bool is_client_vote_enabled_locked(struct votable *votable,
							const char *client_str);
extern bool is_override_vote_enabled(struct votable *votable);
extern bool is_override_vote_enabled_locked(struct votable *votable);
extern int get_client_vote(struct votable *votable, const char *client_str);
extern int get_client_vote_locked(struct votable *votable, const char *client_str);
extern int get_effective_result(struct votable *votable);
extern int get_effective_result_locked(struct votable *votable);
extern const char *get_effective_client(struct votable *votable);
extern const char *get_effective_client_locked(struct votable *votable);
extern int vote(struct votable *votable, const char *client_str, bool state, int val);
extern int vote_override(struct votable *votable, const char *override_client,
		  bool state, int val);
extern int rerun_election(struct votable *votable);
extern struct votable *find_votable(const char *name);
extern struct votable *create_votable(const char *name,
				int votable_type,
				int (*callback)(struct votable *votable,
						void *data,
						int effective_result,
						const char *effective_client),
				void *data);
extern void destroy_votable(struct votable *votable);
extern void lock_votable(struct votable *votable);
extern void unlock_votable(struct votable *votable);

#endif /* __PMIC_VOTER_H */
