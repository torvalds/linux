// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Program that atomically exchanges two paths using
 * the renameat2() system call RENAME_EXCHANGE flag.
 *
 * Copyright 2022 Red Hat Inc.
 * Author: Javier Martinez Canillas <javierm@redhat.com>
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

void print_usage(const char *program)
{
	printf("Usage: %s [oldpath] [newpath]\n", program);
	printf("Atomically exchange oldpath and newpath\n");
}

int main(int argc, char *argv[])
{
	int ret;

	if (argc != 3) {
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	ret = renameat2(AT_FDCWD, argv[1], AT_FDCWD, argv[2], RENAME_EXCHANGE);
	if (ret) {
		perror("rename exchange failed");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
