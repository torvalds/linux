/*
 * Copyright (c) 2014, Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/slab.h>
#include <linux/string.h>
#include "handle.h"

/*
 * Define the initial capacity of the database. It should be a low number
 * multiple of 2 since some databases a likely to only use a few handles.
 * Since the algorithm is to doubles up when growing it shouldn't cause a
 * noticable overhead on large databases.
 */
#define HANDLE_DB_INITIAL_MAX_PTRS	4

void handle_db_destroy(struct handle_db *db)
{
	if (db) {
		kfree(db->ptrs);
		db->ptrs = NULL;
		db->max_ptrs = 0;
	}
}

int handle_get(struct handle_db *db, void *ptr)
{
	unsigned n;
	void *p;
	unsigned new_max_ptrs;

	if (!db || !ptr)
		return -1;

	/* Try to find an empty location */
	for (n = 0; n < db->max_ptrs; n++) {
		if (!db->ptrs[n]) {
			db->ptrs[n] = ptr;
			return n;
		}
	}

	/* No location available, grow the ptrs array */
	if (db->max_ptrs)
		new_max_ptrs = db->max_ptrs * 2;
	else
		new_max_ptrs = HANDLE_DB_INITIAL_MAX_PTRS;
	p = krealloc(db->ptrs, new_max_ptrs * sizeof(void *), GFP_KERNEL);
	if (!p)
		return -1;
	db->ptrs = p;
	memset(db->ptrs + db->max_ptrs, 0,
	       (new_max_ptrs - db->max_ptrs) * sizeof(void *));
	db->max_ptrs = new_max_ptrs;

	/* Since n stopped at db->max_ptrs there is an empty location there */
	db->ptrs[n] = ptr;
	return n;
}

void *handle_put(struct handle_db *db, int handle)
{
	void *p;

	if (!db || handle < 0 || (unsigned)handle >= db->max_ptrs)
		return NULL;

	p = db->ptrs[handle];
	db->ptrs[handle] = NULL;
	return p;
}

void *handle_lookup(struct handle_db *db, int handle)
{
	if (!db || handle < 0 || (unsigned)handle >= db->max_ptrs)
		return NULL;

	return db->ptrs[handle];
}
