// SPDX-License-Identifier: GPL-2.0-only

/* kselftest for allocinfo ioctl
 * allocinfo ioctl retrieves allocinfo data through ioctl
 * Copyright (C) 2026 Google, Inc.
 */

#include <errno.h>
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

static int test_size_filter(void)
{
	int fd;
	struct allocinfo_tag_data_vec *tags = malloc(sizeof(*tags));
	struct allocinfo_tag_data_vec *procfs_entries = malloc(sizeof(*procfs_entries));
	struct allocinfo_filter filter;
	int ret = KSFT_PASS;
	__u64 target_size, i, pos;
	struct allocinfo_tag_data *found_tag = NULL;
	const char *target_function = "do_init_module";
	struct allocinfo_content_id start_cont_id, end_cont_id;
	int retry = 0;
	const int max_retries = 10;

	if (!tags || !procfs_entries) {
		ksft_print_msg("Memory allocation failed.\n");
		ret = KSFT_FAIL;
		goto freemem;
	}

	fd = open(ALLOCINFO_PROC, O_RDONLY);
	if (fd < 0) {
		ksft_print_msg("Failed to open " ALLOCINFO_PROC ": %s\n", strerror(errno));
		ret = KSFT_SKIP;
		goto freemem;
	}

	do {
		found_tag = NULL;
		pos = 0;

		if (__allocinfo_get_content_id(fd, &start_cont_id)) {
			ksft_print_msg("allocinfo_get_content_id failed\n");
			ret = KSFT_FAIL;
			goto exit;
		}

		memset(&filter, 0, sizeof(filter));
		filter.mask |= ALLOCINFO_FILTER_MASK_FUNCTION;
		strncpy(filter.fields.function, target_function, ALLOCINFO_STR_SIZE);

		if (get_filtered_procfs_entries(procfs_entries, &filter)) {
			ksft_print_msg("Error retrieving entries from " ALLOCINFO_PROC "\n");
			ret = KSFT_SKIP;
			goto exit;
		}

		if (procfs_entries->count == 0) {
			ksft_print_msg("Function %s not found in procfs\n", target_function);
			ret = KSFT_SKIP;
			goto exit;
		}

		target_size = procfs_entries->tag[0].counter.bytes;

		memset(&filter, 0, sizeof(filter));
		filter.mask |= ALLOCINFO_FILTER_MASK_MIN_SIZE | ALLOCINFO_FILTER_MASK_MAX_SIZE;
		filter.min_size = target_size;
		filter.max_size = target_size;

		while (1) {
			struct allocinfo_get_at get_at_params;

			memset(&get_at_params, 0, sizeof(get_at_params));
			memcpy(&get_at_params.filter, &filter, sizeof(filter));
			get_at_params.pos = pos;

			if (__allocinfo_get_at(fd, &get_at_params))
				break;

			tags->count = 0;
			memcpy(&tags->tag[tags->count++], &get_at_params.data,
			       sizeof(get_at_params.data));

			while (tags->count < VEC_MAX_ENTRIES &&
			       __allocinfo_get_next(fd, &tags->tag[tags->count]) == 0)
				tags->count++;

			for (i = 0; i < tags->count; i++) {
				if (strcmp(tags->tag[i].tag.function, target_function) == 0) {
					found_tag = &tags->tag[i];
					break;
				}
			}

			if (found_tag || tags->count < VEC_MAX_ENTRIES)
				break;

			pos += tags->count;
		}

		if (__allocinfo_get_content_id(fd, &end_cont_id)) {
			ksft_print_msg("allocinfo_get_content_id failed\n");
			ret = KSFT_FAIL;
			goto exit;
		}

		if (start_cont_id.id == end_cont_id.id)
			break;

		ksft_print_msg("Module load detected during size verification, retrying...\n");
	} while (retry++ < max_retries);

	if (start_cont_id.id == end_cont_id.id && !found_tag) {
		ksft_print_msg("Entry with function %s not found in IOCTL results\n",
			       target_function);
		ret = KSFT_FAIL;
	} else if (start_cont_id.id != end_cont_id.id) {
		ksft_print_msg("Failed to match content_ids for procfs and IOCTL, skipping...\n");
		ret = KSFT_SKIP;
	} else if (found_tag && found_tag->counter.bytes != target_size) {
		ksft_print_msg("IOCTL entry size %llu does not match target size %llu\n",
			       found_tag->counter.bytes, target_size);
		ret = KSFT_FAIL;
	}

exit:
	close(fd);
freemem:
	free(tags);
	free(procfs_entries);
	return ret;
}

static int test_lineno_filter(void)
{
	struct allocinfo_tag_data_vec *tags = malloc(sizeof(*tags));
	struct allocinfo_tag_data_vec *procfs_entries = malloc(sizeof(*procfs_entries));
	struct allocinfo_filter filter;
	enum ioctl_ret ioctl_status;
	int ret = KSFT_PASS;
	__u64 target_lineno, i;
	struct allocinfo_tag_data *target_tag;
	bool found = false;

	if (!tags || !procfs_entries) {
		ksft_print_msg("Memory allocation failed.\n");
		ret = KSFT_FAIL;
		goto exit;
	}

	memset(&filter, 0, sizeof(filter));

	if (get_filtered_procfs_entries(procfs_entries, &filter)) {
		ksft_print_msg("Error retrieving entries from " ALLOCINFO_PROC "\n");
		ret = KSFT_SKIP;
		goto exit;
	}
	if (procfs_entries->count == 0) {
		ksft_print_msg("Could not retrieve procfs entries\n");
		ret = KSFT_SKIP;
		goto exit;
	}
	/*
	 * We depend on the procfs results to determine the line number for the filter before
	 * making the ioctl query. Hence, we cannot reuse run_filter_test here.
	 */
	target_tag = &procfs_entries->tag[0];
	target_lineno = target_tag->tag.lineno;

	filter.mask |= ALLOCINFO_FILTER_MASK_LINENO;
	filter.fields.lineno = target_lineno;

	ioctl_status = get_filtered_ioctl_entries(tags, &filter, 0);
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

	for (i = 0; i < tags->count; i++) {
		if (tags->tag[i].tag.lineno != target_lineno) {
			ksft_print_msg("IOCTL entry %llu has incorrect lineno %llu.\n",
				       i, tags->tag[i].tag.lineno);
			ret = KSFT_FAIL;
			goto exit;
		}

		if (strncmp(tags->tag[i].tag.function, target_tag->tag.function,
			    ALLOCINFO_STR_SIZE) == 0 &&
		    strncmp(tags->tag[i].tag.filename, target_tag->tag.filename,
			    ALLOCINFO_STR_SIZE) == 0)
			found = true;
	}

	if (!found) {
		ksft_print_msg("Original procfs entry not found in IOCTL lineno filter results.\n");
		ret = KSFT_FAIL;
	}

exit:
	free(tags);
	free(procfs_entries);
	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	ksft_set_plan(4);

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

	ret = test_size_filter();
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("Skipping test_size_filter\n");
	else
		ksft_test_result(ret == KSFT_PASS, "test_size_filter\n");

	ret = test_lineno_filter();
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("Skipping test_lineno_filter\n");
	else
		ksft_test_result(ret == KSFT_PASS, "test_lineno_filter\n");

	ksft_finished();
}
