/* Clocksource change test
 *		by: john stultz (johnstul@us.ibm.com)
 *		(C) Copyright IBM 2012
 *		Licensed under the GPLv2
 *
 *  NOTE: This is a meta-test which quickly changes the clocksource and
 *  then uses other tests to detect problems. Thus this test requires
 *  that the inconsistency-check and nanosleep tests be present in the
 *  same directory it is run from.
 *
 *  To build:
 *	$ gcc clocksource-switch.c -o clocksource-switch -lrt
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include "../kselftest.h"


int get_clocksources(char list[][30])
{
	int fd, i;
	size_t size;
	char buf[512];
	char *head, *tmp;

	fd = open("/sys/devices/system/clocksource/clocksource0/available_clocksource", O_RDONLY);

	size = read(fd, buf, 512);

	close(fd);

	for (i = 0; i < 10; i++)
		list[i][0] = '\0';

	head = buf;
	i = 0;
	while (head - buf < size) {
		/* Find the next space */
		for (tmp = head; *tmp != ' '; tmp++) {
			if (*tmp == '\n')
				break;
			if (*tmp == '\0')
				break;
		}
		*tmp = '\0';
		strcpy(list[i], head);
		head = tmp + 1;
		i++;
	}

	return i-1;
}

int get_cur_clocksource(char *buf, size_t size)
{
	int fd;

	fd = open("/sys/devices/system/clocksource/clocksource0/current_clocksource", O_RDONLY);

	size = read(fd, buf, size);

	return 0;
}

int change_clocksource(char *clocksource)
{
	int fd;
	ssize_t size;

	fd = open("/sys/devices/system/clocksource/clocksource0/current_clocksource", O_WRONLY);

	if (fd < 0)
		return -1;

	size = write(fd, clocksource, strlen(clocksource));

	if (size < 0)
		return -1;

	close(fd);
	return 0;
}


int run_tests(int secs)
{
	int ret;
	char buf[255];

	sprintf(buf, "./inconsistency-check -t %i", secs);
	ret = system(buf);
	if (WIFEXITED(ret) && WEXITSTATUS(ret))
		return WEXITSTATUS(ret);
	ret = system("./nanosleep");
	return WIFEXITED(ret) ? WEXITSTATUS(ret) : 0;
}


char clocksource_list[10][30];

int main(int argv, char **argc)
{
	char orig_clk[512];
	int count, i, status;
	pid_t pid;

	get_cur_clocksource(orig_clk, 512);

	count = get_clocksources(clocksource_list);

	if (change_clocksource(clocksource_list[0])) {
		printf("Error: You probably need to run this as root\n");
		return -1;
	}

	/* Check everything is sane before we start switching asynchronously */
	for (i = 0; i < count; i++) {
		printf("Validating clocksource %s\n", clocksource_list[i]);
		if (change_clocksource(clocksource_list[i])) {
			status = -1;
			goto out;
		}
		if (run_tests(5)) {
			status = -1;
			goto out;
		}
	}


	printf("Running Asynchronous Switching Tests...\n");
	pid = fork();
	if (!pid)
		return run_tests(60);

	while (pid != waitpid(pid, &status, WNOHANG))
		for (i = 0; i < count; i++)
			if (change_clocksource(clocksource_list[i])) {
				status = -1;
				goto out;
			}
out:
	change_clocksource(orig_clk);

	if (status)
		return ksft_exit_fail();
	return ksft_exit_pass();
}
