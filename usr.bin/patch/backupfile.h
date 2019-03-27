/*-
 * Copyright (C) 1990 Free Software Foundation, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * without restriction.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * backupfile.h -- declarations for making Emacs style backup file names
 *
 * $OpenBSD: backupfile.h,v 1.6 2003/07/28 18:35:36 otto Exp $
 * $FreeBSD$
 */

/* When to make backup files. */
enum backup_type {
	/* Never make backups. */
	none,

	/* Make simple backups of every file. */
	simple,

	/*
	 * Make numbered backups of files that already have numbered backups,
	 * and simple backups of the others.
	 */
	numbered_existing,

	/* Make numbered backups of every file. */
	numbered
};

extern enum backup_type backup_type;
extern const char	*simple_backup_suffix;

char		*find_backup_file_name(const char *file);
enum backup_type get_version(const char *version);
