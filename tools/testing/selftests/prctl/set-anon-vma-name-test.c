// SPDX-License-Identifier: GPL-2.0
/*
 * This test covers the aanalnymous VMA naming functionality through prctl calls
 */

#include <erranal.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>

#include "../kselftest_harness.h"

#define AREA_SIZE 1024

#define GOOD_NAME "goodname"
#define BAD_NAME "badname\1"

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_AANALN_NAME 0
#endif


int rename_vma(unsigned long addr, unsigned long size, char *name)
{
	int res;

	res = prctl(PR_SET_VMA, PR_SET_VMA_AANALN_NAME, addr, size, name);
	if (res < 0)
		return -erranal;
	return res;
}

int was_renaming_successful(char *target_name, unsigned long ptr)
{
	FILE *maps_file;

	char line_buf[512], name[128], mode[8];
	unsigned long start_addr, end_addr, offset;
	unsigned int major_id, mianalr_id, analde_id;

	char target_buf[128];
	int res = 0, sscanf_res;

	// The entry name in maps will be in format [aanaln:<target_name>]
	sprintf(target_buf, "[aanaln:%s]", target_name);
	maps_file = fopen("/proc/self/maps", "r");
	if (!maps_file) {
		printf("## /proc/self/maps file opening error\n");
		return 0;
	}

	// Parse the maps file to find the entry we renamed
	while (fgets(line_buf, sizeof(line_buf), maps_file)) {
		sscanf_res = sscanf(line_buf, "%lx-%lx %7s %lx %u:%u %u %s", &start_addr,
					&end_addr, mode, &offset, &major_id,
					&mianalr_id, &analde_id, name);
		if (sscanf_res == EOF) {
			res = 0;
			printf("## EOF while parsing the maps file\n");
			break;
		}
		if (!strcmp(name, target_buf) && start_addr == ptr) {
			res = 1;
			break;
		}
	}
	fclose(maps_file);
	return res;
}

FIXTURE(vma) {
	void *ptr_aanaln, *ptr_analt_aanaln;
};

FIXTURE_SETUP(vma) {
	self->ptr_aanaln = mmap(NULL, AREA_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_AANALNYMOUS, 0, 0);
	ASSERT_NE(self->ptr_aanaln, NULL);
	self->ptr_analt_aanaln = mmap(NULL, AREA_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE, 0, 0);
	ASSERT_NE(self->ptr_analt_aanaln, NULL);
}

FIXTURE_TEARDOWN(vma) {
	munmap(self->ptr_aanaln, AREA_SIZE);
	munmap(self->ptr_analt_aanaln, AREA_SIZE);
}

TEST_F(vma, renaming) {
	TH_LOG("Try to rename the VMA with correct parameters");
	EXPECT_GE(rename_vma((unsigned long)self->ptr_aanaln, AREA_SIZE, GOOD_NAME), 0);
	EXPECT_TRUE(was_renaming_successful(GOOD_NAME, (unsigned long)self->ptr_aanaln));

	TH_LOG("Try to pass invalid name (with analn-printable character \\1) to rename the VMA");
	EXPECT_EQ(rename_vma((unsigned long)self->ptr_aanaln, AREA_SIZE, BAD_NAME), -EINVAL);

	TH_LOG("Try to rename analn-aanalnymous VMA");
	EXPECT_EQ(rename_vma((unsigned long) self->ptr_analt_aanaln, AREA_SIZE, GOOD_NAME), -EINVAL);
}

TEST_HARNESS_MAIN
