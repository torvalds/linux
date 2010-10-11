/*
 * AppArmor security module
 *
 * This file contains basic common functions used in AppArmor
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "include/audit.h"


/**
 * aa_split_fqname - split a fqname into a profile and namespace name
 * @fqname: a full qualified name in namespace profile format (NOT NULL)
 * @ns_name: pointer to portion of the string containing the ns name (NOT NULL)
 *
 * Returns: profile name or NULL if one is not specified
 *
 * Split a namespace name from a profile name (see policy.c for naming
 * description).  If a portion of the name is missing it returns NULL for
 * that portion.
 *
 * NOTE: may modify the @fqname string.  The pointers returned point
 *       into the @fqname string.
 */
char *aa_split_fqname(char *fqname, char **ns_name)
{
	char *name = strim(fqname);

	*ns_name = NULL;
	if (name[0] == ':') {
		char *split = strchr(&name[1], ':');
		*ns_name = skip_spaces(&name[1]);
		if (split) {
			/* overwrite ':' with \0 */
			*split = 0;
			name = skip_spaces(split + 1);
		} else
			/* a ns name without a following profile is allowed */
			name = NULL;
	}
	if (name && *name == 0)
		name = NULL;

	return name;
}

/**
 * aa_info_message - log a none profile related status message
 * @str: message to log
 */
void aa_info_message(const char *str)
{
	if (audit_enabled) {
		struct common_audit_data sa;
		COMMON_AUDIT_DATA_INIT(&sa, NONE);
		sa.aad.info = str;
		aa_audit_msg(AUDIT_APPARMOR_STATUS, &sa, NULL);
	}
	printk(KERN_INFO "AppArmor: %s\n", str);
}

/**
 * kvmalloc - do allocation preferring kmalloc but falling back to vmalloc
 * @size: size of allocation
 *
 * Return: allocated buffer or NULL if failed
 *
 * It is possible that policy being loaded from the user is larger than
 * what can be allocated by kmalloc, in those cases fall back to vmalloc.
 */
void *kvmalloc(size_t size)
{
	void *buffer = NULL;

	if (size == 0)
		return NULL;

	/* do not attempt kmalloc if we need more than 16 pages at once */
	if (size <= (16*PAGE_SIZE))
		buffer = kmalloc(size, GFP_NOIO | __GFP_NOWARN);
	if (!buffer) {
		/* see kvfree for why size must be at least work_struct size
		 * when allocated via vmalloc
		 */
		if (size < sizeof(struct work_struct))
			size = sizeof(struct work_struct);
		buffer = vmalloc(size);
	}
	return buffer;
}

/**
 * do_vfree - workqueue routine for freeing vmalloced memory
 * @work: data to be freed
 *
 * The work_struct is overlaid to the data being freed, as at the point
 * the work is scheduled the data is no longer valid, be its freeing
 * needs to be delayed until safe.
 */
static void do_vfree(struct work_struct *work)
{
	vfree(work);
}

/**
 * kvfree - free an allocation do by kvmalloc
 * @buffer: buffer to free (MAYBE_NULL)
 *
 * Free a buffer allocated by kvmalloc
 */
void kvfree(void *buffer)
{
	if (is_vmalloc_addr(buffer)) {
		/* Data is no longer valid so just use the allocated space
		 * as the work_struct
		 */
		struct work_struct *work = (struct work_struct *) buffer;
		INIT_WORK(work, do_vfree);
		schedule_work(work);
	} else
		kfree(buffer);
}
