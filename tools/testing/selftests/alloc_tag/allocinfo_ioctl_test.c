// SPDX-License-Identifier: GPL-2.0-only

/* kselftest for allocinfo ioctl
 * allocinfo ioctl retrieves allocinfo data through ioctl
 * Copyright (C) 2026 Google, Inc.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/alloc_tag.h>
#include "../kselftest.h"

#define MAX_LINE_LEN		512
#define ALLOCINFO_PROC		"/proc/allocinfo"

enum ioctl_ret {
	IOCTL_SUCCESS = 0,
	IOCTL_FAILURE = 1,
	IOCTL_INVALID_DATA = 2,
};

#define VEC_MAX_ENTRIES 32

struct allocinfo_tag_data_vec {
	struct allocinfo_tag_data tag[VEC_MAX_ENTRIES];
	__u64 count;
};

static inline int __allocinfo_get_content_id(int dev_fd, struct allocinfo_content_id *params)
{
	return ioctl(dev_fd, ALLOCINFO_IOC_CONTENT_ID, params);
}

static inline int __allocinfo_get_at(int dev_fd, struct allocinfo_get_at *params)
{
	return ioctl(dev_fd, ALLOCINFO_IOC_GET_AT, params);
}

static inline int __allocinfo_get_next(int dev_fd, struct allocinfo_tag_data *params)
{
	return ioctl(dev_fd, ALLOCINFO_IOC_GET_NEXT, params);
}

static bool match_entry(const struct allocinfo_tag_data *procfs_entry,
			const struct allocinfo_tag_data *tag_data,
			bool match_bytes, bool match_calls, bool match_lineno,
			bool match_function, bool match_filename)
{
	if (match_bytes && tag_data->counter.bytes != procfs_entry->counter.bytes) {
		ksft_print_msg("size retrieved through ioctl does not match procfs\n");
		return false;
	}

	if (match_calls && tag_data->counter.calls != procfs_entry->counter.calls) {
		ksft_print_msg("call count retrieved through ioctl does not match procfs\n");
		return false;
	}

	if (match_lineno && tag_data->tag.lineno != procfs_entry->tag.lineno) {
		ksft_print_msg("lineno retrieved through ioctl does not match procfs\n");
		return false;
	}

	if (match_function &&
	    strncmp(tag_data->tag.function, procfs_entry->tag.function, ALLOCINFO_STR_SIZE)) {
		ksft_print_msg("function retrieved through ioctl does not match procfs\n");
		return false;
	}

	if (match_filename &&
	    strncmp(tag_data->tag.filename, procfs_entry->tag.filename, ALLOCINFO_STR_SIZE)) {
		ksft_print_msg("filename retrieved through ioctl does not match procfs\n");
		return false;
	}
	return true;
}

static bool match_entries(const struct allocinfo_tag_data_vec *procfs_entries,
			  const struct allocinfo_tag_data_vec *tags,
			  bool match_bytes, bool match_calls, bool match_lineno,
			  bool match_function, bool match_filename)
{
	__u64 i;

	if (procfs_entries->count != tags->count) {
		ksft_print_msg("Entry count mismatch. ioctl entries: %llu, proc entries: %llu\n",
			       tags->count, procfs_entries->count);
		return false;
	}
	for (i = 0; i < procfs_entries->count; i++) {
		if (!match_entry(&procfs_entries->tag[i], &tags->tag[i],
				 match_bytes, match_calls, match_lineno,
				 match_function, match_filename)) {
			ksft_print_msg("%lluth entry does not match.\n", i);
			return false;
		}
	}
	return true;
}

static const char *allocinfo_str(const char *str)
{
	size_t len = strlen(str);

	if (len >= ALLOCINFO_STR_SIZE)
		str += (len - ALLOCINFO_STR_SIZE) + 1;
	return str;
}

static void allocinfo_copy_str(char *dest, const char *src)
{
	strncpy(dest, allocinfo_str(src), ALLOCINFO_STR_SIZE - 1);
	dest[ALLOCINFO_STR_SIZE - 1] = '\0';
}

static int get_filtered_procfs_entries(struct allocinfo_tag_data_vec *procfs_entries,
				       const struct allocinfo_filter *filter)
{
	FILE *fp = fopen(ALLOCINFO_PROC, "r");
	char line[MAX_LINE_LEN];
	int matches;
	struct allocinfo_tag_data procfs_entry;

	if (!fp) {
		ksft_print_msg("Failed to open " ALLOCINFO_PROC " for reading\n");
		return 1;
	}
	memset(procfs_entries, 0, sizeof(*procfs_entries));
	while (fgets(line, sizeof(line), fp) && procfs_entries->count < VEC_MAX_ENTRIES) {
		char filename[MAX_LINE_LEN];
		char function[MAX_LINE_LEN];

		memset(&procfs_entry, 0, sizeof(procfs_entry));
		matches = sscanf(line, "%llu %llu %[^:]:%llu func:%s",
				 &procfs_entry.counter.bytes,
				 &procfs_entry.counter.calls,
				 filename,
				 &procfs_entry.tag.lineno,
				 function);

		if (matches != 5)
			continue;

		allocinfo_copy_str(procfs_entry.tag.filename, filename);
		allocinfo_copy_str(procfs_entry.tag.function, function);

		if (filter->mask & ALLOCINFO_FILTER_MASK_FILENAME) {
			if (strncmp(procfs_entry.tag.filename,
				    filter->fields.filename, ALLOCINFO_STR_SIZE))
				continue;
		}
		if (filter->mask & ALLOCINFO_FILTER_MASK_FUNCTION) {
			if (strncmp(procfs_entry.tag.function,
				    filter->fields.function, ALLOCINFO_STR_SIZE))
				continue;
		}
		if (filter->mask & ALLOCINFO_FILTER_MASK_LINENO) {
			if (procfs_entry.tag.lineno != filter->fields.lineno)
				continue;
		}
		if (filter->mask & ALLOCINFO_FILTER_MASK_MIN_SIZE) {
			if (procfs_entry.counter.bytes < filter->min_size)
				continue;
		}
		if (filter->mask & ALLOCINFO_FILTER_MASK_MAX_SIZE) {
			if (procfs_entry.counter.bytes > filter->max_size)
				continue;
		}

		memcpy(&procfs_entries->tag[procfs_entries->count++], &procfs_entry,
		       sizeof(procfs_entry));
	}
	fclose(fp);
	return 0;
}

static enum ioctl_ret get_filtered_ioctl_entries(struct allocinfo_tag_data_vec *tags,
						 const struct allocinfo_filter *filter,
						 __u64 start_pos)
{
	int fd = open(ALLOCINFO_PROC, O_RDONLY);

	if (fd < 0) {
		ksft_print_msg("Failed to open " ALLOCINFO_PROC " for IOCTL\n");
		return IOCTL_FAILURE;
	}

	struct allocinfo_content_id start_cont_id, end_cont_id;
	struct allocinfo_get_at get_at_params;
	const int max_retries = 10;
	int retry_count = 0;
	int status;

	/*
	 * __allocinfo_get_content_id may return different values if a kernel module was loaded
	 * between the two calls. If that happens, the data gathered cannot be considered consistent
	 * and hence needs to be fetched again to avoid flakiness.
	 */
	do {
		if (__allocinfo_get_content_id(fd, &start_cont_id)) {
			ksft_print_msg("allocinfo_get_content_id failed\n");
			status = IOCTL_FAILURE;
			break;
		}

		memset(tags, 0, sizeof(*tags));
		memset(&get_at_params, 0, sizeof(get_at_params));
		memcpy(&get_at_params.filter, filter, sizeof(*filter));
		get_at_params.pos = start_pos;
		if (__allocinfo_get_at(fd, &get_at_params)) {
			ksft_print_msg("allocinfo_get_at failed\n");
			status = IOCTL_FAILURE;
			break;
		}
		memcpy(&tags->tag[tags->count++], &get_at_params.data, sizeof(get_at_params.data));

		while (tags->count < VEC_MAX_ENTRIES &&
		       __allocinfo_get_next(fd, &tags->tag[tags->count]) == 0)
			tags->count++;

		if (__allocinfo_get_content_id(fd, &end_cont_id)) {
			ksft_print_msg("allocinfo_get_content_id failed\n");
			status = IOCTL_FAILURE;
			break;
		}

		if (start_cont_id.id == end_cont_id.id) {
			status = IOCTL_SUCCESS;
		} else {
			ksft_print_msg("allocinfo_get_content_id mismatch, retrying...\n");
			status = IOCTL_INVALID_DATA;
		}
	} while (status == IOCTL_INVALID_DATA && retry_count++ < max_retries);

	close(fd);
	return status;
}

static int run_filter_test(const struct allocinfo_filter *filter)
{
	struct allocinfo_tag_data_vec *tags = malloc(sizeof(*tags));
	struct allocinfo_tag_data_vec *procfs_entries = malloc(sizeof(*procfs_entries));
	int ioctl_status;
	int ret = KSFT_PASS;

	if (!tags || !procfs_entries) {
		ksft_print_msg("Memory allocation failed.\n");
		ret = KSFT_FAIL;
		goto exit;
	}

	if (get_filtered_procfs_entries(procfs_entries, filter)) {
		ksft_print_msg("Error retrieving entries from " ALLOCINFO_PROC "\n");
		ret = KSFT_SKIP;
		goto exit;
	}

	if (procfs_entries->count == 0) {
		ksft_print_msg("No entries found in " ALLOCINFO_PROC ", skipping test\n");
		ret = KSFT_SKIP;
		goto exit;
	}

	ioctl_status = get_filtered_ioctl_entries(tags, filter, 0);
	if (ioctl_status == IOCTL_INVALID_DATA) {
		ksft_print_msg("Trouble retrieving valid IOCTL entries, skipping.\n");
		ret = KSFT_SKIP;
		goto exit;
	}
	if (ioctl_status == IOCTL_FAILURE) {
		ksft_print_msg("Error retrieving IOCTL entries.\n");
		ret = KSFT_FAIL;
		goto exit;
	}

	if (!match_entries(procfs_entries, tags, false, false, true, true, true))
		ret = KSFT_FAIL;

exit:
	free(tags);
	free(procfs_entries);
	return ret;
}

static int test_filename_filter(void)
{
	struct allocinfo_filter filter;
	const char *target_filename = "mm/memory.c";

	memset(&filter, 0, sizeof(filter));
	filter.mask |= ALLOCINFO_FILTER_MASK_FILENAME;
	strncpy(filter.fields.filename, target_filename, ALLOCINFO_STR_SIZE);

	return run_filter_test(&filter);
}

static int test_function_filter(void)
{
	struct allocinfo_filter filter;
	const char *target_function = "dup_mm";

	memset(&filter, 0, sizeof(filter));
	filter.mask |= ALLOCINFO_FILTER_MASK_FUNCTION;
	strncpy(filter.fields.function, target_function, ALLOCINFO_STR_SIZE);

	return run_filter_test(&filter);
}

int main(int argc, char *argv[])
{
	int ret;

	ksft_set_plan(2);

	ret = test_filename_filter();
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("Skipping test_filename_filter\n");
	else
		ksft_test_result(ret == KSFT_PASS, "test_filename_filter\n");

	ret = test_function_filter();
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("Skipping test_function_filter\n");
	else
		ksft_test_result(ret == KSFT_PASS, "test_function_filter\n");

	ksft_finished();
}
