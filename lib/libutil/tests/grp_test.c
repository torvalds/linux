/*-
 * Copyright (c) 2008 Sean C. Farley <scf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libutil.h>


/*
 * Static values for building and testing an artificial group.
 */
static char grpName[] = "groupName";
static char grpPasswd[] = "groupPwd";
static gid_t grpGID = 1234;
static char *grpMems[] = { "mem1", "mem2", "mem3", NULL };
static const char *origStrGrp = "groupName:groupPwd:1234:mem1,mem2,mem3";


/*
 * Build a group to test against without depending on a real group to be found
 * within /etc/group.
 */
static void
build_grp(struct group *grp)
{
	grp->gr_name = grpName;
	grp->gr_passwd = grpPasswd;
	grp->gr_gid = grpGID;
	grp->gr_mem = grpMems;

	return;
}


int
main(void)
{
	char *strGrp;
	int testNdx;
	struct group *dupGrp;
	struct group *scanGrp;
	struct group origGrp;

	/* Setup. */
	printf("1..4\n");
	testNdx = 0;

	/* Manually build a group using static values. */
	build_grp(&origGrp);

	/* Copy the group. */
	testNdx++;
	if ((dupGrp = gr_dup(&origGrp)) == NULL)
		printf("not ");
	printf("ok %d - %s\n", testNdx, "gr_dup");

	/* Compare the original and duplicate groups. */
	testNdx++;
	if (! gr_equal(&origGrp, dupGrp))
		printf("not ");
	printf("ok %d - %s\n", testNdx, "gr_equal");

	/* Create group string from the duplicate group structure. */
	testNdx++;
	strGrp = gr_make(dupGrp);
	if (strcmp(strGrp, origStrGrp) != 0)
		printf("not ");
	printf("ok %d - %s\n", testNdx, "gr_make");

	/*
	 * Create group structure from string and compare it to the original
	 * group structure.
	 */
	testNdx++;
	if ((scanGrp = gr_scan(strGrp)) == NULL || ! gr_equal(&origGrp,
	    scanGrp))
		printf("not ");
	printf("ok %d - %s\n", testNdx, "gr_scan");

	/* Clean up. */
	free(scanGrp);
	free(strGrp);
	free(dupGrp);

	exit(EXIT_SUCCESS);
}
