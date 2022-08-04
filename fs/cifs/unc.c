// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              Suresh Jayaraman <sjayaraman@suse.de>
 *              Jeff Layton <jlayton@kernel.org>
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/inet.h>
#include <linux/ctype.h>
#include "cifsglob.h"
#include "cifsproto.h"

/* extract the host portion of the UNC string */
char *extract_hostname(const char *unc)
{
	const char *src;
	char *dst, *delim;
	unsigned int len;

	/* skip double chars at beginning of string */
	/* BB: check validity of these bytes? */
	if (strlen(unc) < 3)
		return ERR_PTR(-EINVAL);
	for (src = unc; *src && *src == '\\'; src++)
		;
	if (!*src)
		return ERR_PTR(-EINVAL);

	/* delimiter between hostname and sharename is always '\\' now */
	delim = strchr(src, '\\');
	if (!delim)
		return ERR_PTR(-EINVAL);

	len = delim - src;
	dst = kmalloc((len + 1), GFP_KERNEL);
	if (dst == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(dst, src, len);
	dst[len] = '\0';

	return dst;
}

char *extract_sharename(const char *unc)
{
	const char *src;
	char *delim, *dst;
	int len;

	/* skip double chars at the beginning */
	src = unc + 2;

	/* share name is always preceded by '\\' now */
	delim = strchr(src, '\\');
	if (!delim)
		return ERR_PTR(-EINVAL);
	delim++;
	len = strlen(delim);

	/* caller has to free the memory */
	dst = kstrndup(delim, len, GFP_KERNEL);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	return dst;
}
