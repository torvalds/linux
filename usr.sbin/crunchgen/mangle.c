/* $OpenBSD: mangle.c,v 1.2 2009/12/08 21:10:24 drahn Exp $ */
/*
 * Copyright (c) 2009 Dale Rahn.
 * All rights reserved.
 *
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "mangle.h"

/*
 * supports randomizing up to 32 characters, if an identifier is longer than 
 * 32 characters, we only modify the first 32 characters of it.
 *
 * However not all of the 32 characters will really be randomized as
 * 2^32 (uint32_t  gives a maximum string of cmpPQeaa[a]*
 * Dont thing this will really matter....
 *
 * HOWEVER this does not mean we want to change the MAX_KEY_STR_LEN to 6
 * because then all strings len >= 6 would use from the same index pool.
 */
#define MAX_KEY_STR_LEN 32
uint32_t key[MAX_KEY_STR_LEN];

char *filename = "mangledata";

int key_fd = -1;
void
init_mangle_state()
{
	int failed = 0;
	ssize_t len;
	int i, ret;

	/* open file if it exists, else init to zeros */
	struct stat sb;

	ret = stat(filename, &sb);

	if (ret != -1 && sb.st_size == sizeof(key)) {
		key_fd = open(filename, O_RDWR);
	}

	if (ret == -1 || key_fd == -1)
		failed = 1;

	if (failed == 0) {
		len = read(key_fd, key, sizeof(key));
		if (len != sizeof(key)) {
			failed = 1;
		}
	}

	if (failed == 1) {
		if (key_fd != -1) {
			close(key_fd);
			key_fd = -1;
		}
		for (i = 0; i < MAX_KEY_STR_LEN; i++) {
			key[i] = 0;
		}
	}
}

void
fini_mangle_state()
{
	int len;
	if (key_fd == -1) {
		/* open file for create/truncate */
		key_fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, 0666);
	} else {
		/* seek to the beginning */
		lseek(key_fd, 0, SEEK_SET);
	}
	if (key_fd != -1) {
		/* write file */
		len = write(key_fd, key, sizeof(key));
		if (len != sizeof(key)) {
			printf("writing file failed\n");
		}
		close(key_fd);
		key_fd = -1;
	}
}

void
dump_mangle_state()
{
	int i;

	for (i = 0; i < MAX_KEY_STR_LEN; i++) {
		printf("key %d %d\n", i, key[i]);
	}
}

char validchars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define NUM_KEYS (sizeof(validchars)-1)

void
mangle_str(char *str)
{
	int i;
	int len;
	int keyval;

	len = strlen(str);
	if (len == 0)
		return; /* nothing to mangle */

	if (len > MAX_KEY_STR_LEN)
		len = MAX_KEY_STR_LEN;

	keyval = key[len-1]++;
	
	for (i = 0; i < len; i++) {
		int idx = keyval % NUM_KEYS;
		keyval = keyval / NUM_KEYS;
		str[i] = validchars[idx];
		
	}
}

