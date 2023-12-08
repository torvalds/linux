/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor file mediation function definitions.
 *
 * Copyright 2017 Canonical Ltd.
 */

#ifndef __AA_MOUNT_H
#define __AA_MOUNT_H

#include <linux/fs.h>
#include <linux/path.h>

#include "domain.h"
#include "policy.h"

/* mount perms */
#define AA_MAY_PIVOTROOT	0x01
#define AA_MAY_MOUNT		0x02
#define AA_MAY_UMOUNT		0x04
#define AA_AUDIT_DATA		0x40
#define AA_MNT_CONT_MATCH	0x40

#define AA_MS_IGNORE_MASK (MS_KERNMOUNT | MS_NOSEC | MS_ACTIVE | MS_BORN)

int aa_remount(struct aa_label *label, const struct path *path,
	       unsigned long flags, void *data);

int aa_bind_mount(struct aa_label *label, const struct path *path,
		  const char *old_name, unsigned long flags);


int aa_mount_change_type(struct aa_label *label, const struct path *path,
			 unsigned long flags);

int aa_move_mount(struct aa_label *label, const struct path *path,
		  const char *old_name);

int aa_new_mount(struct aa_label *label, const char *dev_name,
		 const struct path *path, const char *type, unsigned long flags,
		 void *data);

int aa_umount(struct aa_label *label, struct vfsmount *mnt, int flags);

int aa_pivotroot(struct aa_label *label, const struct path *old_path,
		 const struct path *new_path);

#endif /* __AA_MOUNT_H */
