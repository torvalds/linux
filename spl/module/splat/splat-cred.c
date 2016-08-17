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
 *  Solaris Porting LAyer Tests (SPLAT) Credential Tests.
\*****************************************************************************/

#include <sys/cred.h>
#include <sys/random.h>
#include "splat-internal.h"

#define SPLAT_CRED_NAME			"cred"
#define SPLAT_CRED_DESC			"Kernel Cred Tests"

#define SPLAT_CRED_TEST1_ID		0x0e01
#define SPLAT_CRED_TEST1_NAME		"cred"
#define SPLAT_CRED_TEST1_DESC		"Task Credential Test"

#define SPLAT_CRED_TEST2_ID		0x0e02
#define SPLAT_CRED_TEST2_NAME		"kcred"
#define SPLAT_CRED_TEST2_DESC		"Kernel Credential Test"

#define SPLAT_CRED_TEST3_ID		0x0e03
#define SPLAT_CRED_TEST3_NAME		"groupmember"
#define SPLAT_CRED_TEST3_DESC		"Group Member Test"

#define GROUP_STR_SIZE			128
#define GROUP_STR_REDZONE		16

static int
splat_cred_test1(struct file *file, void *arg)
{
	char str[GROUP_STR_SIZE];
	uid_t uid, ruid, suid;
	gid_t gid, rgid, sgid, *groups;
	int ngroups, i, count = 0;
	cred_t *cr = CRED();

	uid  = crgetuid(cr);
	ruid = crgetruid(cr);
	suid = crgetsuid(cr);

	gid  = crgetgid(cr);
	rgid = crgetrgid(cr);
	sgid = crgetsgid(cr);

	ngroups = crgetngroups(cr);
	groups = crgetgroups(cr);

	memset(str, 0, GROUP_STR_SIZE);
	for (i = 0; i < ngroups; i++) {
		count += sprintf(str + count, "%d ", groups[i]);

		if (count > (GROUP_STR_SIZE - GROUP_STR_REDZONE)) {
			splat_vprint(file, SPLAT_CRED_TEST1_NAME,
				     "Failed too many group entries for temp "
				     "buffer: %d, %s\n", ngroups, str);
			return -ENOSPC;
		}
	}

	splat_vprint(file, SPLAT_CRED_TEST1_NAME,
		     "uid: %d ruid: %d suid: %d "
		     "gid: %d rgid: %d sgid: %d\n",
		     uid, ruid, suid, gid, rgid, sgid);
	splat_vprint(file, SPLAT_CRED_TEST1_NAME,
		     "ngroups: %d groups: %s\n", ngroups, str);

	if (uid || ruid || suid || gid || rgid || sgid) {
		splat_vprint(file, SPLAT_CRED_TEST1_NAME,
			     "Failed expected all uids+gids to be %d\n", 0);
		return -EIDRM;
	}

	if (ngroups > NGROUPS_MAX) {
		splat_vprint(file, SPLAT_CRED_TEST1_NAME,
			     "Failed ngroups must not exceed NGROUPS_MAX: "
			     "%d > %d\n", ngroups, NGROUPS_MAX);
		return -EIDRM;
	}

	splat_vprint(file, SPLAT_CRED_TEST1_NAME,
		     "Success sane CRED(): %d\n", 0);

        return 0;
} /* splat_cred_test1() */

static int
splat_cred_test2(struct file *file, void *arg)
{
	char str[GROUP_STR_SIZE];
	uid_t uid, ruid, suid;
	gid_t gid, rgid, sgid, *groups;
	int ngroups, i, count = 0;

	crhold(kcred);

	uid  = crgetuid(kcred);
	ruid = crgetruid(kcred);
	suid = crgetsuid(kcred);

	gid  = crgetgid(kcred);
	rgid = crgetrgid(kcred);
	sgid = crgetsgid(kcred);

	ngroups = crgetngroups(kcred);
	groups  = crgetgroups(kcred);

	memset(str, 0, GROUP_STR_SIZE);
	for (i = 0; i < ngroups; i++) {
		count += sprintf(str + count, "%d ", groups[i]);

		if (count > (GROUP_STR_SIZE - GROUP_STR_REDZONE)) {
			splat_vprint(file, SPLAT_CRED_TEST2_NAME,
				     "Failed too many group entries for temp "
				     "buffer: %d, %s\n", ngroups, str);
			crfree(kcred);
			return -ENOSPC;
		}
	}

	crfree(kcred);

	splat_vprint(file, SPLAT_CRED_TEST2_NAME,
		     "uid: %d ruid: %d suid: %d "
		     "gid: %d rgid: %d sgid: %d\n",
		     uid, ruid, suid, gid, rgid, sgid);
	splat_vprint(file, SPLAT_CRED_TEST2_NAME,
		     "ngroups: %d groups: %s\n", ngroups, str);

	if (uid || ruid || suid || gid || rgid || sgid) {
		splat_vprint(file, SPLAT_CRED_TEST2_NAME,
			     "Failed expected all uids+gids to be %d\n", 0);
		return -EIDRM;
	}

	if (ngroups > NGROUPS_MAX) {
		splat_vprint(file, SPLAT_CRED_TEST2_NAME,
			     "Failed ngroups must not exceed NGROUPS_MAX: "
			     "%d > %d\n", ngroups, NGROUPS_MAX);
		return -EIDRM;
	}

	splat_vprint(file, SPLAT_CRED_TEST2_NAME,
		     "Success sane kcred: %d\n", 0);

        return 0;
} /* splat_cred_test2() */

#define	SPLAT_NGROUPS	32
/*
 * Verify the groupmember() works correctly by constructing an interesting
 * CRED() and checking that the expected gids are part of it.
 */
static int
splat_cred_test3(struct file *file, void *arg)
{
	gid_t known_gid, missing_gid, tmp_gid;
	unsigned char rnd;
	struct group_info *gi;
	int i, rc;

	get_random_bytes((void *)&rnd, 1);
	known_gid = (rnd > 0) ? rnd : 1;
	missing_gid = 0;

	/*
	 * Create an interesting known set of gids for test purposes. The
	 * gids are pseudo randomly selected are will be in the range of
	 * 1:(NGROUPS_MAX-1).  Gid 0 is explicitly avoided so we can reliably
	 * test for its absence in the test cases.
	 */
	gi = groups_alloc(SPLAT_NGROUPS);
	if (gi == NULL) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Failed create "
		    "group_info for known gids: %d\n", -ENOMEM);
		rc = -ENOMEM;
		goto show_groups;
	}

	for (i = 0, tmp_gid = known_gid; i < SPLAT_NGROUPS; i++) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Adding gid %d "
		    "to current CRED() (%d/%d)\n", tmp_gid, i, gi->ngroups);
#ifdef HAVE_KUIDGID_T
		GROUP_AT(gi, i) = make_kgid(current_user_ns(), tmp_gid);
#else
		GROUP_AT(gi, i) = tmp_gid;
#endif /* HAVE_KUIDGID_T */
		tmp_gid = ((tmp_gid * 17) % (NGROUPS_MAX - 1)) + 1;
	}

	/* Set the new groups in the CRED() and release our reference. */
	rc = set_current_groups(gi);
	put_group_info(gi);

	if (rc) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Failed to add "
		    "gid %d to current group: %d\n", known_gid, rc);
		goto show_groups;
	}

	/* Verify groupmember() finds the known_gid in the CRED() */
	rc = groupmember(known_gid, CRED());
	if (!rc) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Failed to find "
		    "known gid %d in CRED()'s groups.\n", known_gid);
		rc = -EIDRM;
		goto show_groups;
	}

	/* Verify groupmember() does NOT finds the missing gid in the CRED() */
	rc = groupmember(missing_gid, CRED());
	if (rc) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Failed missing "
		    "gid %d was found in CRED()'s groups.\n", missing_gid);
		rc = -EIDRM;
		goto show_groups;
	}

	splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Success groupmember() "
	    "correctly detects expected gids in CRED(): %d\n", rc);

show_groups:
	if (rc) {
		int i, grps = crgetngroups(CRED());

		splat_vprint(file, SPLAT_CRED_TEST3_NAME, "%d groups: ", grps);
		for (i = 0; i < grps; i++)
			splat_print(file, "%d ", crgetgroups(CRED())[i]);
		splat_print(file, "%s", "\n");
	}


	return (rc);
} /* splat_cred_test3() */

splat_subsystem_t *
splat_cred_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_CRED_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_CRED_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_CRED;

        SPLAT_TEST_INIT(sub, SPLAT_CRED_TEST1_NAME, SPLAT_CRED_TEST1_DESC,
	              SPLAT_CRED_TEST1_ID, splat_cred_test1);
        SPLAT_TEST_INIT(sub, SPLAT_CRED_TEST2_NAME, SPLAT_CRED_TEST2_DESC,
	              SPLAT_CRED_TEST2_ID, splat_cred_test2);
        SPLAT_TEST_INIT(sub, SPLAT_CRED_TEST3_NAME, SPLAT_CRED_TEST3_DESC,
	              SPLAT_CRED_TEST3_ID, splat_cred_test3);

        return sub;
} /* splat_cred_init() */

void
splat_cred_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_CRED_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CRED_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CRED_TEST1_ID);

        kfree(sub);
} /* splat_cred_fini() */

int
splat_cred_id(void)
{
        return SPLAT_SUBSYSTEM_CRED;
} /* splat_cred_id() */
