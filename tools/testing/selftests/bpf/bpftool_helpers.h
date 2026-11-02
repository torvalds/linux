/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#define MAX_BPFTOOL_CMD_LEN	(256)

int detect_bpftool_path(char *buffer, size_t size);
int run_bpftool_command(char *args);
int get_bpftool_command_output(char *args, char *output_buf, size_t output_max_len);
