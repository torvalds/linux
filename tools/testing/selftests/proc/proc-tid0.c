/*
 * Copyright (c) 2021 Alexey Dobriyan <adobriyan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
// Test that /proc/*/task never contains "0".
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static pid_t pid = -1;

static void atexit_hook(void)
{
	if (pid > 0) {
		kill(pid, SIGKILL);
	}
}

static void *f(void *_)
{
	return NULL;
}

static void sigalrm(int _)
{
	exit(0);
}

int main(void)
{
	pid = fork();
	if (pid == 0) {
		/* child */
		while (1) {
			pthread_t pth;
			pthread_create(&pth, NULL, f, NULL);
			pthread_join(pth, NULL);
		}
	} else if (pid > 0) {
		/* parent */
		atexit(atexit_hook);

		char buf[64];
		snprintf(buf, sizeof(buf), "/proc/%u/task", pid);

		signal(SIGALRM, sigalrm);
		alarm(1);

		while (1) {
			DIR *d = opendir(buf);
			struct dirent *de;
			while ((de = readdir(d))) {
				if (strcmp(de->d_name, "0") == 0) {
					exit(1);
				}
			}
			closedir(d);
		}

		return 0;
	} else {
		perror("fork");
		return 1;
	}
}
