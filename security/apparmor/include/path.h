/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor basic path manipulation function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#ifndef __AA_PATH_H
#define __AA_PATH_H

enum path_flags {
	PATH_IS_DIR = 0x1,		/* path is a directory */
	PATH_SOCK_COND = 0x2,
	PATH_CONNECT_PATH = 0x4,	/* connect disconnected paths to / */
	PATH_CHROOT_REL = 0x8,		/* do path lookup relative to chroot */
	PATH_CHROOT_NSCONNECT = 0x10,	/* connect paths that are at ns root */

	PATH_DELEGATE_DELETED = 0x10000, /* delegate deleted files */
	PATH_MEDIATE_DELETED = 0x20000,	 /* mediate deleted paths */
};

int aa_path_name(const struct path *path, int flags, char *buffer,
		 const char **name, const char **info,
		 const char *disconnected);

#define IN_ATOMIC true
char *aa_get_buffer(bool in_atomic);
void aa_put_buffer(char *buf);

#endif /* __AA_PATH_H */
