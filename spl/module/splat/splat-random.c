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
 *  Solaris Porting LAyer Tests (SPLAT) Random Number Generator Tests.
\*****************************************************************************/

#include <sys/random.h>
#include <sys/kmem.h>
#include "splat-internal.h"

#define SPLAT_KRNG_NAME			"krng"
#define SPLAT_KRNG_DESC			"Kernel Random Number Generator Tests"

#define SPLAT_KRNG_TEST1_ID		0x0301
#define SPLAT_KRNG_TEST1_NAME		"freq"
#define SPLAT_KRNG_TEST1_DESC		"Frequency Test"

#define KRNG_NUM_BITS			1048576
#define KRNG_NUM_BYTES			(KRNG_NUM_BITS >> 3)
#define KRNG_NUM_BITS_DIV2		(KRNG_NUM_BITS >> 1)
#define KRNG_ERROR_RANGE		2097

/* Random Number Generator Tests
   There can be meny more tests on quality of the
   random number generator.  For now we are only
   testing the frequency of particular bits.
   We could also test consecutive sequences,
   randomness within a particular block, etc.
   but is probably not necessary for our purposes */

static int
splat_krng_test1(struct file *file, void *arg)
{
	uint8_t *buf;
	int i, j, diff, num = 0, rc = 0;

	buf = kmalloc(sizeof(*buf) * KRNG_NUM_BYTES, GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	memset(buf, 0, sizeof(*buf) * KRNG_NUM_BYTES);

	/* Always succeeds */
	random_get_pseudo_bytes(buf, sizeof(uint8_t) * KRNG_NUM_BYTES);

	for (i = 0; i < KRNG_NUM_BYTES; i++) {
		uint8_t tmp = buf[i];
		for (j = 0; j < 8; j++) {
			uint8_t tmp2 = ((tmp >> j) & 0x01);
			if (tmp2 == 1) {
				num++;
			}
		}
	}

	kfree(buf);

	diff = KRNG_NUM_BITS_DIV2 - num;
	if (diff < 0)
		diff *= -1;

	splat_print(file, "Test 1 Number of ones: %d\n", num);
	splat_print(file, "Test 1 Difference from expected: %d Allowed: %d\n",
                  diff, KRNG_ERROR_RANGE);

	if (diff > KRNG_ERROR_RANGE)
		rc = -ERANGE;
out:
	return rc;
}

splat_subsystem_t *
splat_krng_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_KRNG_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_KRNG_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_KRNG;

        splat_test_init(sub, SPLAT_KRNG_TEST1_NAME, SPLAT_KRNG_TEST1_DESC,
	              SPLAT_KRNG_TEST1_ID, splat_krng_test1);

        return sub;
}

void
splat_krng_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        splat_test_fini(sub, SPLAT_KRNG_TEST1_ID);

        kfree(sub);
}

int
splat_krng_id(void) {
        return SPLAT_SUBSYSTEM_KRNG;
}
