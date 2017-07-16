/*
 * Copyright 2014 Sony Mobile Communications Inc.
 *
 * Licensed under the terms of the GNU GPL License version 2
 *
 * Selftest for runtime system size
 *
 * Prints the amount of RAM that the currently running system is using.
 *
 * This program tries to be as small as possible itself, to
 * avoid perturbing the system memory utilization with its
 * own execution.  It also attempts to have as few dependencies
 * on kernel features as possible.
 *
 * It should be statically linked, with startup libs avoided.
 * It uses no library calls, and only the following 3 syscalls:
 *   sysinfo(), write(), and _exit()
 *
 * For output, it avoids printf (which in some C libraries
 * has large external dependencies) by  implementing it's own
 * number output and print routines, and using __builtin_strlen()
 */

#include <sys/sysinfo.h>
#include <unistd.h>

#define STDOUT_FILENO 1

static int print(const char *s)
{
	return write(STDOUT_FILENO, s, __builtin_strlen(s));
}

static inline char *num_to_str(unsigned long num, char *buf, int len)
{
	unsigned int digit;

	/* put digits in buffer from back to front */
	buf += len - 1;
	*buf = 0;
	do {
		digit = num % 10;
		*(--buf) = digit + '0';
		num /= 10;
	} while (num > 0);

	return buf;
}

static int print_num(unsigned long num)
{
	char num_buf[30];

	return print(num_to_str(num, num_buf, sizeof(num_buf)));
}

static int print_k_value(const char *s, unsigned long num, unsigned long units)
{
	unsigned long long temp;
	int ccode;

	print(s);

	temp = num;
	temp = (temp * units)/1024;
	num = temp;
	ccode = print_num(num);
	print("\n");
	return ccode;
}

/* this program has no main(), as startup libraries are not used */
void _start(void)
{
	int ccode;
	struct sysinfo info;
	unsigned long used;
	static const char *test_name = " get runtime memory use\n";

	print("TAP version 13\n");
	print("# Testing system size.\n");

	ccode = sysinfo(&info);
	if (ccode < 0) {
		print("not ok 1");
		print(test_name);
		print(" ---\n reason: \"could not get sysinfo\"\n ...\n");
		_exit(ccode);
	}
	print("ok 1");
	print(test_name);

	/* ignore cache complexities for now */
	used = info.totalram - info.freeram - info.bufferram;
	print("# System runtime memory report (units in Kilobytes):\n");
	print(" ---\n");
	print_k_value(" Total:  ", info.totalram, info.mem_unit);
	print_k_value(" Free:   ", info.freeram, info.mem_unit);
	print_k_value(" Buffer: ", info.bufferram, info.mem_unit);
	print_k_value(" In use: ", used, info.mem_unit);
	print(" ...\n");
	print("1..1\n");

	_exit(0);
}
