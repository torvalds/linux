/*-
 * Copyright (C) 2016 Bryan Drewery <bdrewery@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Helper for mlock(3) to avoid EAGAIN errors
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>

#include <atf-c.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>

#define	VM_MAX_WIRED "vm.max_wired"

static void
vm_max_wired_sysctl(int *old_value, int *new_value)
{
	size_t old_len;
	size_t new_len = (new_value == NULL ? 0 : sizeof(int));

	if (old_value == NULL)
		printf("Setting the new value to %d\n", *new_value);
	else {
		ATF_REQUIRE_MSG(sysctlbyname(VM_MAX_WIRED, NULL, &old_len,
		    new_value, new_len) == 0,
		    "sysctlbyname(%s) failed: %s", VM_MAX_WIRED, strerror(errno));
	}

	ATF_REQUIRE_MSG(sysctlbyname(VM_MAX_WIRED, old_value, &old_len,
	    new_value, new_len) == 0,
	    "sysctlbyname(%s) failed: %s", VM_MAX_WIRED, strerror(errno));

	if (old_value != NULL)
		printf("Saved the old value (%d)\n", *old_value);
}

void
set_vm_max_wired(int new_value)
{
	FILE *fp;
	int old_value;

	fp = fopen(VM_MAX_WIRED, "w");
	if (fp == NULL) {
		atf_tc_skip("could not open %s for writing: %s",
		    VM_MAX_WIRED, strerror(errno));
		return;
	}

	vm_max_wired_sysctl(&old_value, NULL);

	ATF_REQUIRE_MSG(fprintf(fp, "%d", old_value) > 0,
	    "saving %s failed", VM_MAX_WIRED);

	fclose(fp);

	vm_max_wired_sysctl(NULL, &new_value);
}

void
restore_vm_max_wired(void)
{
	FILE *fp;
	int saved_max_wired;

	fp = fopen(VM_MAX_WIRED, "r");
	if (fp == NULL) {
		perror("fopen failed\n");
		return;
	}

	if (fscanf(fp, "%d", &saved_max_wired) != 1) {
		perror("fscanf failed\n");
		fclose(fp);
		return;
	}

	fclose(fp);
	printf("old value in %s: %d\n", VM_MAX_WIRED, saved_max_wired);

	if (saved_max_wired == 0) /* This will cripple the test host */
		return;

	vm_max_wired_sysctl(NULL, &saved_max_wired);
}
