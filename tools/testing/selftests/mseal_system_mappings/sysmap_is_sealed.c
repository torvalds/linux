// SPDX-License-Identifier: GPL-2.0-only
/*
 * test system mappings are sealed when
 * KCONFIG_MSEAL_SYSTEM_MAPPINGS=y
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "../kselftest.h"
#include "../kselftest_harness.h"

#define VMFLAGS "VmFlags:"
#define MSEAL_FLAGS "sl"
#define MAX_LINE_LEN 512

bool has_mapping(char *name, FILE *maps)
{
	char line[MAX_LINE_LEN];

	while (fgets(line, sizeof(line), maps)) {
		if (strstr(line, name))
			return true;
	}

	return false;
}

bool mapping_is_sealed(char *name, FILE *maps)
{
	char line[MAX_LINE_LEN];

	while (fgets(line, sizeof(line), maps)) {
		if (!strncmp(line, VMFLAGS, strlen(VMFLAGS))) {
			if (strstr(line, MSEAL_FLAGS))
				return true;

			return false;
		}
	}

	return false;
}

FIXTURE(basic) {
	FILE *maps;
};

FIXTURE_SETUP(basic)
{
	self->maps = fopen("/proc/self/smaps", "r");
	if (!self->maps)
		SKIP(return, "Could not open /proc/self/smap, errno=%d",
			errno);
};

FIXTURE_TEARDOWN(basic)
{
	if (self->maps)
		fclose(self->maps);
};

FIXTURE_VARIANT(basic)
{
	char *name;
	bool sealed;
};

FIXTURE_VARIANT_ADD(basic, vdso) {
	.name = "[vdso]",
	.sealed = true,
};

FIXTURE_VARIANT_ADD(basic, vvar) {
	.name = "[vvar]",
	.sealed = true,
};

FIXTURE_VARIANT_ADD(basic, vvar_vclock) {
	.name = "[vvar_vclock]",
	.sealed = true,
};

FIXTURE_VARIANT_ADD(basic, sigpage) {
	.name = "[sigpage]",
	.sealed = true,
};

FIXTURE_VARIANT_ADD(basic, vectors) {
	.name = "[vectors]",
	.sealed = true,
};

FIXTURE_VARIANT_ADD(basic, uprobes) {
	.name = "[uprobes]",
	.sealed = true,
};

FIXTURE_VARIANT_ADD(basic, stack) {
	.name = "[stack]",
	.sealed = false,
};

TEST_F(basic, check_sealed)
{
	if (!has_mapping(variant->name, self->maps)) {
		SKIP(return, "could not find the mapping, %s",
			variant->name);
	}

	EXPECT_EQ(variant->sealed,
		mapping_is_sealed(variant->name, self->maps));
};

TEST_HARNESS_MAIN
