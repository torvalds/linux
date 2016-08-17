/*****************************************************************************\
 *  Copyright (C) 2011 Lawrence Livermore National Security, LLC.
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
 *  Solaris Porting LAyer Tests (SPLAT) Kernel Compatibility Tests.
\*****************************************************************************/

#include <sys/kmem.h>
#include <linux/mm_compat.h>
#include "splat-internal.h"

#define SPLAT_LINUX_NAME		"linux"
#define SPLAT_LINUX_DESC		"Kernel Compatibility Tests"

#define SPLAT_LINUX_TEST1_ID		0x1001
#define SPLAT_LINUX_TEST1_NAME		"shrinker"
#define SPLAT_LINUX_TEST1_DESC		"Shrinker test"

/*
 * Wait queue used to eliminate race between dropping of slab
 * and execution of the shrinker callback
 */
DECLARE_WAIT_QUEUE_HEAD(shrinker_wait);

SPL_SHRINKER_CALLBACK_FWD_DECLARE(splat_linux_shrinker_fn);
SPL_SHRINKER_DECLARE(splat_linux_shrinker, splat_linux_shrinker_fn, 1);
static unsigned long splat_linux_shrinker_size = 0;
static struct file *splat_linux_shrinker_file = NULL;

static spl_shrinker_t
__splat_linux_shrinker_fn(struct shrinker *shrink, struct shrink_control *sc)
{
	static int failsafe = 0;
	static unsigned long last_splat_linux_shrinker_size = 0;
	unsigned long size;
	spl_shrinker_t count;

	/*
	 * shrinker_size can only decrease or stay the same between callbacks
	 * in the same run, so Reset failsafe whenever shrinker increases
	 * as this indicates a new run.
	 */
	if (last_splat_linux_shrinker_size < splat_linux_shrinker_size)
		failsafe = 0;

	last_splat_linux_shrinker_size = splat_linux_shrinker_size;

	if (sc->nr_to_scan) {
		size = MIN(sc->nr_to_scan, splat_linux_shrinker_size);
		splat_linux_shrinker_size -= size;

		splat_vprint(splat_linux_shrinker_file, SPLAT_LINUX_TEST1_NAME,
		    "Reclaimed %lu objects, size now %lu\n",
		    size, splat_linux_shrinker_size);

#ifdef HAVE_SPLIT_SHRINKER_CALLBACK
		count = size;
#else
		count = splat_linux_shrinker_size;
#endif /* HAVE_SPLIT_SHRINKER_CALLBACK */

	} else {
		count = splat_linux_shrinker_size;
		splat_vprint(splat_linux_shrinker_file, SPLAT_LINUX_TEST1_NAME,
		    "Cache size is %lu\n", splat_linux_shrinker_size);
	}

	/* Far more calls than expected abort drop_slab as a failsafe */
	if (failsafe > 100) {
		splat_vprint(splat_linux_shrinker_file, SPLAT_LINUX_TEST1_NAME,
		    "Far more calls than expected (%d), size now %lu\n",
		   failsafe, splat_linux_shrinker_size);
		return (SHRINK_STOP);
	} else {
		/*
		 * We only increment failsafe if it doesn't trigger.  This
		 * makes any failsafe failure persistent until the next test.
		 */
		failsafe++;
	}

	/* Shrinker has run, so signal back to test. */
	wake_up(&shrinker_wait);

	return (count);
}

SPL_SHRINKER_CALLBACK_WRAPPER(splat_linux_shrinker_fn);

#define DROP_SLAB_CMD \
	"exec 0</dev/null " \
	"     1>/proc/sys/vm/drop_caches " \
	"     2>/dev/null; " \
	"echo 2"

static int
splat_linux_drop_slab(struct file *file)
{
	char *argv[] = { "/bin/sh",
	                 "-c",
	                 DROP_SLAB_CMD,
	                 NULL };
	char *envp[] = { "HOME=/",
	                 "TERM=linux",
	                 "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
	                 NULL };
	int rc;

	rc = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (rc)
		splat_vprint(file, SPLAT_LINUX_TEST1_NAME,
	            "Failed user helper '%s %s %s', rc = %d\n",
		    argv[0], argv[1], argv[2], rc);

	return rc;
}

/*
 * Verify correct shrinker functionality by registering a shrinker
 * with the required compatibility macros.  We then use a simulated
 * cache and force the systems caches to be dropped.  The shrinker
 * should be repeatedly called until it reports that the cache is
 * empty.  It is then cleanly unregistered and correct behavior is
 * verified.  There are now four slightly different supported shrinker
 * API and this test ensures the compatibility code is correct.
 */
static int
splat_linux_test1(struct file *file, void *arg)
{
	int rc = -EINVAL;

	/*
	 * Globals used by the shrinker, it is not safe to run this
	 * test concurrently this is a safe assumption for SPLAT tests.
	 * Regardless we do some minimal checking a bail if concurrent
	 * use is detected.
	 */
	if (splat_linux_shrinker_size || splat_linux_shrinker_file) {
		splat_vprint(file, SPLAT_LINUX_TEST1_NAME,
		    "Failed due to concurrent shrinker test, rc = %d\n", rc);
		return (rc);
	}

	splat_linux_shrinker_size = 1024;
	splat_linux_shrinker_file = file;

	spl_register_shrinker(&splat_linux_shrinker);
	rc = splat_linux_drop_slab(file);
	if (rc)
		goto out;

	/*
	 * By the time we get here, it is possible that the shrinker has not
	 * yet run. splat_linux_drop_slab sends a signal for it to run, but
	 * there is no guarantee of when it will actually run. We wait for it
	 * to run here, terminating when either the shrinker size is now 0 or
	 * we timeout after 1 second, which should be an eternity (error).
	 */
	rc = wait_event_timeout(shrinker_wait, !splat_linux_shrinker_size, HZ);
	if (!rc) {
		splat_vprint(file, SPLAT_LINUX_TEST1_NAME,
		    "Failed cache shrinking timed out, size now %lu",
		    splat_linux_shrinker_size);
		rc = -ETIMEDOUT;
	} else {
		rc = 0;
	}

	if (!rc && splat_linux_shrinker_size != 0) {
		splat_vprint(file, SPLAT_LINUX_TEST1_NAME,
		    "Failed cache was not shrunk to 0, size now %lu",
		    splat_linux_shrinker_size);
		rc = -EDOM;
	}
out:
	spl_unregister_shrinker(&splat_linux_shrinker);

	splat_linux_shrinker_size = 0;
	splat_linux_shrinker_file = NULL;

	return rc;
}

splat_subsystem_t *
splat_linux_init(void)
{
	splat_subsystem_t *sub;

	sub = kmalloc(sizeof(*sub), GFP_KERNEL);
	if (sub == NULL)
		return NULL;

	memset(sub, 0, sizeof(*sub));
	strncpy(sub->desc.name, SPLAT_LINUX_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_LINUX_DESC, SPLAT_DESC_SIZE);
	INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
	spin_lock_init(&sub->test_lock);
	sub->desc.id = SPLAT_SUBSYSTEM_LINUX;

	SPLAT_TEST_INIT(sub, SPLAT_LINUX_TEST1_NAME, SPLAT_LINUX_TEST1_DESC,
			SPLAT_LINUX_TEST1_ID, splat_linux_test1);

	return sub;
}

void
splat_linux_fini(splat_subsystem_t *sub)
{
	ASSERT(sub);
	SPLAT_TEST_FINI(sub, SPLAT_LINUX_TEST1_ID);

	kfree(sub);
}

int
splat_linux_id(void) {
	return SPLAT_SUBSYSTEM_LINUX;
}
