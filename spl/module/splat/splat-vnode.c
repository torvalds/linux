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
 *  Solaris Porting LAyer Tests (SPLAT) Vnode Tests.
\*****************************************************************************/

#include <sys/vnode.h>
#include "splat-internal.h"

#define SPLAT_VNODE_NAME		"vnode"
#define SPLAT_VNODE_DESC		"Kernel Vnode Tests"

#define SPLAT_VNODE_TEST1_ID		0x0901
#define SPLAT_VNODE_TEST1_NAME		"vn_open"
#define SPLAT_VNODE_TEST1_DESC		"Vn_open Test"

#define SPLAT_VNODE_TEST2_ID		0x0902
#define SPLAT_VNODE_TEST2_NAME		"vn_openat"
#define SPLAT_VNODE_TEST2_DESC		"Vn_openat Test"

#define SPLAT_VNODE_TEST3_ID		0x0903
#define SPLAT_VNODE_TEST3_NAME		"vn_rdwr"
#define SPLAT_VNODE_TEST3_DESC		"Vn_rdwrt Test"

#define SPLAT_VNODE_TEST4_ID		0x0904
#define SPLAT_VNODE_TEST4_NAME		"vn_rename"
#define SPLAT_VNODE_TEST4_DESC		"Vn_rename Test"

#define SPLAT_VNODE_TEST5_ID		0x0905
#define SPLAT_VNODE_TEST5_NAME		"vn_getattr"
#define SPLAT_VNODE_TEST5_DESC		"Vn_getattr Test"

#define SPLAT_VNODE_TEST6_ID		0x0906
#define SPLAT_VNODE_TEST6_NAME		"vn_sync"
#define SPLAT_VNODE_TEST6_DESC		"Vn_sync Test"

#define SPLAT_VNODE_TEST_FILE		"/etc/fstab"
#define SPLAT_VNODE_TEST_FILE_AT	"etc/fstab"
#define SPLAT_VNODE_TEST_FILE_RW	"/tmp/spl.vnode.tmp"
#define SPLAT_VNODE_TEST_FILE_RW1	"/tmp/spl.vnode.tmp.1"
#define SPLAT_VNODE_TEST_FILE_RW2	"/tmp/spl.vnode.tmp.2"

static int
splat_vnode_user_cmd(struct file *file, void *arg,
                     char *name, char *cmd)
{
	char sh_path[] = "/bin/sh";
	char *argv[] = { sh_path,
	                 "-c",
	                 cmd,
	                 NULL };
	char *envp[] = { "HOME=/",
	                 "TERM=linux",
	                 "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
	                 NULL };
	int rc;

	rc = call_usermodehelper(sh_path, argv, envp, UMH_WAIT_PROC);
	if (rc) {
		splat_vprint(file, name,
			     "Failed command: %s %s %s (%d)\n",
			     argv[0], argv[1], cmd, rc);
		return -EPERM;
	}

	return 0;
}

static int
splat_vnode_unlink_all(struct file *file, void *arg, char *name)
{
	char *cmds[] = { "rm -f " SPLAT_VNODE_TEST_FILE_RW,
	                 "rm -f " SPLAT_VNODE_TEST_FILE_RW1,
			 "rm -f " SPLAT_VNODE_TEST_FILE_RW2,
	                 NULL };
	int i = 0, rc = 0;

	while (cmds[i] != NULL) {
		if ((rc = splat_vnode_user_cmd(file, arg, name, cmds[i])))
			return rc;

		i++;
	}

	return rc;
}

static int
splat_vnode_test1(struct file *file, void *arg)
{
	vnode_t *vp;
	int rc;

	if ((rc = vn_open(SPLAT_VNODE_TEST_FILE, UIO_SYSSPACE,
			  FREAD, 0644, &vp, 0, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST1_NAME,
			     "Failed to vn_open test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE, rc);
		return -rc;
	}

        rc = VOP_CLOSE(vp, 0, 0, 0, 0, 0);

	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST1_NAME,
			     "Failed to vn_close test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE, rc);
		return -rc;
	}

	splat_vprint(file, SPLAT_VNODE_TEST1_NAME, "Successfully vn_open'ed "
		     "and vn_closed test file: %s\n", SPLAT_VNODE_TEST_FILE);

        return -rc;
} /* splat_vnode_test1() */

static int
splat_vnode_test2(struct file *file, void *arg)
{
	vnode_t *vp;
	int rc;

	if ((rc = vn_openat(SPLAT_VNODE_TEST_FILE_AT, UIO_SYSSPACE,
			    FREAD, 0644, &vp, 0, 0, rootdir, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST2_NAME,
			     "Failed to vn_openat test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE, rc);
		return -rc;
	}

        rc = VOP_CLOSE(vp, 0, 0, 0, 0, 0);

	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST2_NAME,
			     "Failed to vn_close test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE, rc);
		return -rc;
	}

	splat_vprint(file, SPLAT_VNODE_TEST2_NAME, "Successfully vn_openat'ed "
		     "and vn_closed test file: %s\n", SPLAT_VNODE_TEST_FILE);

        return -rc;
} /* splat_vnode_test2() */

static int
splat_vnode_test3(struct file *file, void *arg)
{
	vnode_t *vp;
	char buf1[32] = "SPL VNode Interface Test File\n";
	char buf2[32] = "";
	int rc;

	if ((rc = splat_vnode_unlink_all(file, arg, SPLAT_VNODE_TEST3_NAME)))
		return rc;

	if ((rc = vn_open(SPLAT_VNODE_TEST_FILE_RW, UIO_SYSSPACE,
			  FWRITE | FREAD | FCREAT | FEXCL,
			  0644, &vp, 0, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST3_NAME,
			     "Failed to vn_open test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW, rc);
		return -rc;
	}

        rc = vn_rdwr(UIO_WRITE, vp, buf1, strlen(buf1), 0,
                     UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST3_NAME,
			     "Failed vn_rdwr write of test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW, rc);
		goto out;
	}

        rc = vn_rdwr(UIO_READ, vp, buf2, strlen(buf1), 0,
                     UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST3_NAME,
			     "Failed vn_rdwr read of test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW, rc);
		goto out;
	}

	if (strncmp(buf1, buf2, strlen(buf1))) {
		rc = EINVAL;
		splat_vprint(file, SPLAT_VNODE_TEST3_NAME,
			     "Failed strncmp data written does not match "
			     "data read\nWrote: %sRead:  %s\n", buf1, buf2);
		goto out;
	}

	rc = 0;
	splat_vprint(file, SPLAT_VNODE_TEST3_NAME, "Wrote: %s", buf1);
	splat_vprint(file, SPLAT_VNODE_TEST3_NAME, "Read:  %s", buf2);
	splat_vprint(file, SPLAT_VNODE_TEST3_NAME, "Successfully wrote and "
		     "read expected data pattern to test file: %s\n",
		     SPLAT_VNODE_TEST_FILE_RW);

out:
        VOP_CLOSE(vp, 0, 0, 0, 0, 0);
	vn_remove(SPLAT_VNODE_TEST_FILE_RW, UIO_SYSSPACE, RMFILE);

        return -rc;
} /* splat_vnode_test3() */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,1,0)
static int
splat_vnode_test4(struct file *file, void *arg)
{
	vnode_t *vp;
	char buf1[32] = "SPL VNode Interface Test File\n";
	char buf2[32] = "";
	int rc;

	if ((rc = splat_vnode_unlink_all(file, arg, SPLAT_VNODE_TEST4_NAME)))
		return rc;

	if ((rc = vn_open(SPLAT_VNODE_TEST_FILE_RW1, UIO_SYSSPACE,
			  FWRITE | FREAD | FCREAT | FEXCL, 0644, &vp, 0, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST4_NAME,
			     "Failed to vn_open test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW1, rc);
		goto out;
	}

        rc = vn_rdwr(UIO_WRITE, vp, buf1, strlen(buf1), 0,
                     UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST4_NAME,
			     "Failed vn_rdwr write of test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW1, rc);
		goto out2;
	}

        VOP_CLOSE(vp, 0, 0, 0, 0, 0);

	rc = vn_rename(SPLAT_VNODE_TEST_FILE_RW1,SPLAT_VNODE_TEST_FILE_RW2,0);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST4_NAME, "Failed vn_rename "
			     "%s -> %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW1,
			     SPLAT_VNODE_TEST_FILE_RW2, rc);
		goto out;
	}

	if ((rc = vn_open(SPLAT_VNODE_TEST_FILE_RW2, UIO_SYSSPACE,
			  FREAD | FEXCL, 0644, &vp, 0, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST4_NAME,
			     "Failed to vn_open test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW2, rc);
		goto out;
	}

        rc = vn_rdwr(UIO_READ, vp, buf2, strlen(buf1), 0,
                     UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST4_NAME,
			     "Failed vn_rdwr read of test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW2, rc);
		goto out2;
	}

	if (strncmp(buf1, buf2, strlen(buf1))) {
		rc = EINVAL;
		splat_vprint(file, SPLAT_VNODE_TEST4_NAME,
			     "Failed strncmp data written does not match "
			     "data read\nWrote: %sRead:  %s\n", buf1, buf2);
		goto out2;
	}

	rc = 0;
	splat_vprint(file, SPLAT_VNODE_TEST4_NAME, "Wrote to %s:  %s",
		     SPLAT_VNODE_TEST_FILE_RW1, buf1);
	splat_vprint(file, SPLAT_VNODE_TEST4_NAME, "Read from %s: %s",
		     SPLAT_VNODE_TEST_FILE_RW2, buf2);
	splat_vprint(file, SPLAT_VNODE_TEST4_NAME, "Successfully renamed "
		     "test file %s -> %s and verified data pattern\n",
		     SPLAT_VNODE_TEST_FILE_RW1, SPLAT_VNODE_TEST_FILE_RW2);
out2:
        VOP_CLOSE(vp, 0, 0, 0, 0, 0);
out:
	vn_remove(SPLAT_VNODE_TEST_FILE_RW1, UIO_SYSSPACE, RMFILE);
	vn_remove(SPLAT_VNODE_TEST_FILE_RW2, UIO_SYSSPACE, RMFILE);

        return -rc;
} /* splat_vnode_test4() */
#endif

static int
splat_vnode_test5(struct file *file, void *arg)
{
	vnode_t *vp;
	vattr_t vap;
	int rc;

	if ((rc = vn_open(SPLAT_VNODE_TEST_FILE, UIO_SYSSPACE,
			  FREAD, 0644, &vp, 0, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST5_NAME,
			     "Failed to vn_open test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE, rc);
		return -rc;
	}

	rc = VOP_GETATTR(vp, &vap, 0, 0, NULL);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST5_NAME,
			     "Failed to vn_getattr test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE, rc);
		goto out;
	}

	if (vap.va_type != VREG) {
		rc = EINVAL;
		splat_vprint(file, SPLAT_VNODE_TEST5_NAME,
			     "Failed expected regular file type "
			     "(%d != VREG): %s (%d)\n", vap.va_type,
			     SPLAT_VNODE_TEST_FILE, rc);
		goto out;
	}

	splat_vprint(file, SPLAT_VNODE_TEST1_NAME, "Successfully "
		     "vn_getattr'ed test file: %s\n", SPLAT_VNODE_TEST_FILE);

out:
        VOP_CLOSE(vp, 0, 0, 0, 0, 0);

        return -rc;
} /* splat_vnode_test5() */

static int
splat_vnode_test6(struct file *file, void *arg)
{
	vnode_t *vp;
	char buf[32] = "SPL VNode Interface Test File\n";
	int rc;

	if ((rc = splat_vnode_unlink_all(file, arg, SPLAT_VNODE_TEST6_NAME)))
		return rc;

	if ((rc = vn_open(SPLAT_VNODE_TEST_FILE_RW, UIO_SYSSPACE,
			  FWRITE | FCREAT | FEXCL, 0644, &vp, 0, 0))) {
		splat_vprint(file, SPLAT_VNODE_TEST6_NAME,
			     "Failed to vn_open test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW, rc);
		return -rc;
	}

        rc = vn_rdwr(UIO_WRITE, vp, buf, strlen(buf), 0,
                     UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST6_NAME,
			     "Failed vn_rdwr write of test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW, rc);
		goto out;
	}

	rc = vn_fsync(vp, 0, 0, 0);
	if (rc) {
		splat_vprint(file, SPLAT_VNODE_TEST6_NAME,
			     "Failed vn_fsync of test file: %s (%d)\n",
			     SPLAT_VNODE_TEST_FILE_RW, rc);
		goto out;
	}

	rc = 0;
	splat_vprint(file, SPLAT_VNODE_TEST6_NAME, "Successfully "
		     "fsync'ed test file %s\n", SPLAT_VNODE_TEST_FILE_RW);
out:
        VOP_CLOSE(vp, 0, 0, 0, 0, 0);
	vn_remove(SPLAT_VNODE_TEST_FILE_RW, UIO_SYSSPACE, RMFILE);

        return -rc;
} /* splat_vnode_test6() */

splat_subsystem_t *
splat_vnode_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_VNODE_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_VNODE_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_VNODE;

        SPLAT_TEST_INIT(sub, SPLAT_VNODE_TEST1_NAME, SPLAT_VNODE_TEST1_DESC,
	                SPLAT_VNODE_TEST1_ID, splat_vnode_test1);
        SPLAT_TEST_INIT(sub, SPLAT_VNODE_TEST2_NAME, SPLAT_VNODE_TEST2_DESC,
	                SPLAT_VNODE_TEST2_ID, splat_vnode_test2);
        SPLAT_TEST_INIT(sub, SPLAT_VNODE_TEST3_NAME, SPLAT_VNODE_TEST3_DESC,
	                SPLAT_VNODE_TEST3_ID, splat_vnode_test3);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,1,0)
        SPLAT_TEST_INIT(sub, SPLAT_VNODE_TEST4_NAME, SPLAT_VNODE_TEST4_DESC,
	                SPLAT_VNODE_TEST4_ID, splat_vnode_test4);
#endif
        SPLAT_TEST_INIT(sub, SPLAT_VNODE_TEST5_NAME, SPLAT_VNODE_TEST5_DESC,
	                SPLAT_VNODE_TEST5_ID, splat_vnode_test5);
        SPLAT_TEST_INIT(sub, SPLAT_VNODE_TEST6_NAME, SPLAT_VNODE_TEST6_DESC,
	                SPLAT_VNODE_TEST6_ID, splat_vnode_test6);

        return sub;
} /* splat_vnode_init() */

void
splat_vnode_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_VNODE_TEST6_ID);
        SPLAT_TEST_FINI(sub, SPLAT_VNODE_TEST5_ID);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,1,0)
        SPLAT_TEST_FINI(sub, SPLAT_VNODE_TEST4_ID);
#endif
        SPLAT_TEST_FINI(sub, SPLAT_VNODE_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_VNODE_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_VNODE_TEST1_ID);

        kfree(sub);
} /* splat_vnode_fini() */

int
splat_vnode_id(void)
{
        return SPLAT_SUBSYSTEM_VNODE;
} /* splat_vnode_id() */
