/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (secid) definitions
 *
 * Copyright 2009-2018 Canonical Ltd.
 */

#ifndef __AA_SECID_H
#define __AA_SECID_H

#include <linux/slab.h>
#include <linux/types.h>

struct aa_label;

/* secid value that will not be allocated */
#define AA_SECID_INVALID 0

/* secid value that matches any other secid */
#define AA_SECID_WILDCARD 1

/* sysctl to enable displaying mode when converting secid to secctx */
extern int apparmor_display_secid_mode;

struct aa_label *aa_secid_to_label(u32 secid);
int apparmor_secid_to_secctx(u32 secid, char **secdata, u32 *seclen);
int apparmor_lsmprop_to_secctx(struct lsm_prop *prop, char **secdata,
			       u32 *seclen);
int apparmor_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid);
void apparmor_release_secctx(char *secdata, u32 seclen);


int aa_alloc_secid(struct aa_label *label, gfp_t gfp);
void aa_free_secid(u32 secid);
void aa_secid_update(u32 secid, struct aa_label *label);

#endif /* __AA_SECID_H */
