/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) Zlib Compression Tests.
\*****************************************************************************/

#include <sys/zmod.h>
#include <sys/random.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include "splat-internal.h"

#define SPLAT_ZLIB_NAME			"zlib"
#define SPLAT_ZLIB_DESC			"Zlib Compression Tests"

#define SPLAT_ZLIB_TEST1_ID		0x0f01
#define SPLAT_ZLIB_TEST1_NAME		"compress/uncompress"
#define SPLAT_ZLIB_TEST1_DESC		"Compress/Uncompress Test"

#define BUFFER_SIZE			(128 * 1024)

static int
splat_zlib_test1_check(struct file *file, void *src, void *dst, void *chk,
    int level)
{
	size_t dst_len = BUFFER_SIZE;
	size_t chk_len = BUFFER_SIZE;
	int rc;

	memset(dst, 0, BUFFER_SIZE);
	memset(chk, 0, BUFFER_SIZE);

	rc = z_compress_level(dst, &dst_len, src, BUFFER_SIZE, level);
	if (rc != Z_OK) {
		splat_vprint(file, SPLAT_ZLIB_TEST1_NAME,
		    "Failed level %d z_compress_level(), %d\n", level, rc);
		return -EINVAL;
	}

	rc = z_uncompress(chk, &chk_len, dst, dst_len);
	if (rc != Z_OK) {
		splat_vprint(file, SPLAT_ZLIB_TEST1_NAME,
		    "Failed level %d z_uncompress(), %d\n", level, rc);
		return -EINVAL;
	}

	rc = memcmp(src, chk, BUFFER_SIZE);
	if (rc) {
		splat_vprint(file, SPLAT_ZLIB_TEST1_NAME,
		    "Failed level %d memcmp()), %d\n", level, rc);
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_ZLIB_TEST1_NAME,
	    "Passed level %d, compressed %d bytes to %d bytes\n",
	    level, BUFFER_SIZE, (int)dst_len);

	return 0;
}

/*
 * Compress a buffer, uncompress the newly compressed buffer, then
 * compare it to the original.  Do this for all 9 compression levels.
 */
static int
splat_zlib_test1(struct file *file, void *arg)
{
	void *src = NULL, *dst = NULL, *chk = NULL;
	int i, rc, level;

	src = vmalloc(BUFFER_SIZE);
	if (src == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	dst = vmalloc(BUFFER_SIZE);
	if (dst == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	chk = vmalloc(BUFFER_SIZE);
	if (chk == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/* Source buffer is a repeating 1024 byte random pattern. */
	random_get_pseudo_bytes(src, sizeof(uint8_t) * 1024);
	for (i = 1; i < 128; i++)
		memcpy(src + (i * 1024), src, 1024);

	for (level = 1; level <= 9; level++)
		if ((rc = splat_zlib_test1_check(file, src, dst, chk, level)))
			break;
out:
	if (src)
		vfree(src);

	if (dst)
		vfree(dst);

	if (chk)
		vfree(chk);

	return rc;
}

splat_subsystem_t *
splat_zlib_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_ZLIB_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_ZLIB_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_ZLIB;

        SPLAT_TEST_INIT(sub, SPLAT_ZLIB_TEST1_NAME, SPLAT_ZLIB_TEST1_DESC,
	              SPLAT_ZLIB_TEST1_ID, splat_zlib_test1);

        return sub;
}

void
splat_zlib_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_ZLIB_TEST1_ID);

        kfree(sub);
}

int
splat_zlib_id(void) {
        return SPLAT_SUBSYSTEM_ZLIB;
}
