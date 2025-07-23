// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2025 Didi Technology Co., Tao Chen */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "json_writer.h"
#include "main.h"

#define MOUNTS_FILE "/proc/mounts"

static bool has_delegate_options(const char *mnt_ops)
{
	return strstr(mnt_ops, "delegate_cmds") ||
	       strstr(mnt_ops, "delegate_maps") ||
	       strstr(mnt_ops, "delegate_progs") ||
	       strstr(mnt_ops, "delegate_attachs");
}

static char *get_delegate_value(const char *opts, const char *key)
{
	char *token, *rest, *ret = NULL;
	char *opts_copy = strdup(opts);

	if (!opts_copy)
		return NULL;

	for (token = strtok_r(opts_copy, ",", &rest); token;
			token = strtok_r(NULL, ",", &rest)) {
		if (strncmp(token, key, strlen(key)) == 0 &&
		    token[strlen(key)] == '=') {
			ret = token + strlen(key) + 1;
			break;
		}
	}
	free(opts_copy);

	return ret;
}

static void print_items_per_line(const char *input, int items_per_line)
{
	char *str, *rest, *strs;
	int cnt = 0;

	if (!input)
		return;

	strs = strdup(input);
	if (!strs)
		return;

	for (str = strtok_r(strs, ":", &rest); str;
			str = strtok_r(NULL, ":", &rest)) {
		if (cnt % items_per_line == 0)
			printf("\n\t  ");

		printf("%-20s", str);
		cnt++;
	}

	free(strs);
}

#define ITEMS_PER_LINE 4
static void show_token_info_plain(struct mntent *mntent)
{
	char *value;

	printf("token_info  %s", mntent->mnt_dir);

	printf("\n\tallowed_cmds:");
	value = get_delegate_value(mntent->mnt_opts, "delegate_cmds");
	print_items_per_line(value, ITEMS_PER_LINE);

	printf("\n\tallowed_maps:");
	value = get_delegate_value(mntent->mnt_opts, "delegate_maps");
	print_items_per_line(value, ITEMS_PER_LINE);

	printf("\n\tallowed_progs:");
	value = get_delegate_value(mntent->mnt_opts, "delegate_progs");
	print_items_per_line(value, ITEMS_PER_LINE);

	printf("\n\tallowed_attachs:");
	value = get_delegate_value(mntent->mnt_opts, "delegate_attachs");
	print_items_per_line(value, ITEMS_PER_LINE);
	printf("\n");
}

static void split_json_array_str(const char *input)
{
	char *str, *rest, *strs;

	if (!input) {
		jsonw_start_array(json_wtr);
		jsonw_end_array(json_wtr);
		return;
	}

	strs = strdup(input);
	if (!strs)
		return;

	jsonw_start_array(json_wtr);
	for (str = strtok_r(strs, ":", &rest); str;
			str = strtok_r(NULL, ":", &rest)) {
		jsonw_string(json_wtr, str);
	}
	jsonw_end_array(json_wtr);

	free(strs);
}

static void show_token_info_json(struct mntent *mntent)
{
	char *value;

	jsonw_start_object(json_wtr);

	jsonw_string_field(json_wtr, "token_info", mntent->mnt_dir);

	jsonw_name(json_wtr, "allowed_cmds");
	value = get_delegate_value(mntent->mnt_opts, "delegate_cmds");
	split_json_array_str(value);

	jsonw_name(json_wtr, "allowed_maps");
	value = get_delegate_value(mntent->mnt_opts, "delegate_maps");
	split_json_array_str(value);

	jsonw_name(json_wtr, "allowed_progs");
	value = get_delegate_value(mntent->mnt_opts, "delegate_progs");
	split_json_array_str(value);

	jsonw_name(json_wtr, "allowed_attachs");
	value = get_delegate_value(mntent->mnt_opts, "delegate_attachs");
	split_json_array_str(value);

	jsonw_end_object(json_wtr);
}

static int __show_token_info(struct mntent *mntent)
{
	if (json_output)
		show_token_info_json(mntent);
	else
		show_token_info_plain(mntent);

	return 0;
}

static int show_token_info(void)
{
	FILE *fp;
	struct mntent *ent;

	fp = setmntent(MOUNTS_FILE, "r");
	if (!fp) {
		p_err("Failed to open: %s", MOUNTS_FILE);
		return -1;
	}

	if (json_output)
		jsonw_start_array(json_wtr);

	while ((ent = getmntent(fp)) != NULL) {
		if (strncmp(ent->mnt_type, "bpf", 3) == 0) {
			if (has_delegate_options(ent->mnt_opts))
				__show_token_info(ent);
		}
	}

	if (json_output)
		jsonw_end_array(json_wtr);

	endmntent(fp);

	return 0;
}

static int do_show(int argc, char **argv)
{
	if (argc)
		return BAD_ARG();

	return show_token_info();
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %1$s %2$s { show | list }\n"
		"       %1$s %2$s help\n"
		"\n"
		"",
		bin_name, argv[-2]);
	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "help",	do_help },
	{ 0 }
};

int do_token(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
