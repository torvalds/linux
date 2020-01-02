// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/utsname.h>
#include <sys/sendfile.h>
#include <sys/sysmacros.h>
#include <linux/random.h>
#include <linux/version.h>

__attribute__((noreturn)) static void poweroff(void)
{
	fflush(stdout);
	fflush(stderr);
	reboot(RB_AUTOBOOT);
	sleep(30);
	fprintf(stderr, "\x1b[37m\x1b[41m\x1b[1mFailed to power off!!!\x1b[0m\n");
	exit(1);
}

static void panic(const char *what)
{
	fprintf(stderr, "\n\n\x1b[37m\x1b[41m\x1b[1mSOMETHING WENT HORRIBLY WRONG\x1b[0m\n\n    \x1b[31m\x1b[1m%s: %s\x1b[0m\n\n\x1b[37m\x1b[44m\x1b[1mPower off...\x1b[0m\n\n", what, strerror(errno));
	poweroff();
}

#define pretty_message(msg) puts("\x1b[32m\x1b[1m" msg "\x1b[0m")

static void print_banner(void)
{
	struct utsname utsname;
	int len;

	if (uname(&utsname) < 0)
		panic("uname");

	len = strlen("    WireGuard Test Suite on       ") + strlen(utsname.sysname) + strlen(utsname.release) + strlen(utsname.machine);
	printf("\x1b[45m\x1b[33m\x1b[1m%*.s\x1b[0m\n\x1b[45m\x1b[33m\x1b[1m    WireGuard Test Suite on %s %s %s    \x1b[0m\n\x1b[45m\x1b[33m\x1b[1m%*.s\x1b[0m\n\n", len, "", utsname.sysname, utsname.release, utsname.machine, len, "");
}

static void seed_rng(void)
{
	int fd;
	struct {
		int entropy_count;
		int buffer_size;
		unsigned char buffer[256];
	} entropy = {
		.entropy_count = sizeof(entropy.buffer) * 8,
		.buffer_size = sizeof(entropy.buffer),
		.buffer = "Adding real entropy is not actually important for these tests. Don't try this at home, kids!"
	};

	if (mknod("/dev/urandom", S_IFCHR | 0644, makedev(1, 9)))
		panic("mknod(/dev/urandom)");
	fd = open("/dev/urandom", O_WRONLY);
	if (fd < 0)
		panic("open(urandom)");
	for (int i = 0; i < 256; ++i) {
		if (ioctl(fd, RNDADDENTROPY, &entropy) < 0)
			panic("ioctl(urandom)");
	}
	close(fd);
}

static void mount_filesystems(void)
{
	pretty_message("[+] Mounting filesystems...");
	mkdir("/dev", 0755);
	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mkdir("/tmp", 0755);
	mkdir("/run", 0755);
	mkdir("/var", 0755);
	if (mount("none", "/dev", "devtmpfs", 0, NULL))
		panic("devtmpfs mount");
	if (mount("none", "/proc", "proc", 0, NULL))
		panic("procfs mount");
	if (mount("none", "/sys", "sysfs", 0, NULL))
		panic("sysfs mount");
	if (mount("none", "/tmp", "tmpfs", 0, NULL))
		panic("tmpfs mount");
	if (mount("none", "/run", "tmpfs", 0, NULL))
		panic("tmpfs mount");
	if (mount("none", "/sys/kernel/debug", "debugfs", 0, NULL))
		; /* Not a problem if it fails.*/
	if (symlink("/run", "/var/run"))
		panic("run symlink");
	if (symlink("/proc/self/fd", "/dev/fd"))
		panic("fd symlink");
}

static void enable_logging(void)
{
	int fd;
	pretty_message("[+] Enabling logging...");
	fd = open("/proc/sys/kernel/printk", O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "9\n", 2) != 2)
			panic("write(printk)");
		close(fd);
	}
	fd = open("/proc/sys/debug/exception-trace", O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "1\n", 2) != 2)
			panic("write(exception-trace)");
		close(fd);
	}
	fd = open("/proc/sys/kernel/panic_on_warn", O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "1\n", 2) != 2)
			panic("write(panic_on_warn)");
		close(fd);
	}
}

static void kmod_selftests(void)
{
	FILE *file;
	char line[2048], *start, *pass;
	bool success = true;
	pretty_message("[+] Module self-tests:");
	file = fopen("/proc/kmsg", "r");
	if (!file)
		panic("fopen(kmsg)");
	if (fcntl(fileno(file), F_SETFL, O_NONBLOCK) < 0)
		panic("fcntl(kmsg, nonblock)");
	while (fgets(line, sizeof(line), file)) {
		start = strstr(line, "wireguard: ");
		if (!start)
			continue;
		start += 11;
		*strchrnul(start, '\n') = '\0';
		if (strstr(start, "www.wireguard.com"))
			break;
		pass = strstr(start, ": pass");
		if (!pass || pass[6] != '\0') {
			success = false;
			printf(" \x1b[31m*  %s\x1b[0m\n", start);
		} else
			printf(" \x1b[32m*  %s\x1b[0m\n", start);
	}
	fclose(file);
	if (!success) {
		puts("\x1b[31m\x1b[1m[-] Tests failed! \u2639\x1b[0m");
		poweroff();
	}
}

static void launch_tests(void)
{
	char cmdline[4096], *success_dev;
	int status, fd;
	pid_t pid;

	pretty_message("[+] Launching tests...");
	pid = fork();
	if (pid == -1)
		panic("fork");
	else if (pid == 0) {
		execl("/init.sh", "init", NULL);
		panic("exec");
	}
	if (waitpid(pid, &status, 0) < 0)
		panic("waitpid");
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		pretty_message("[+] Tests successful! :-)");
		fd = open("/proc/cmdline", O_RDONLY);
		if (fd < 0)
			panic("open(/proc/cmdline)");
		if (read(fd, cmdline, sizeof(cmdline) - 1) <= 0)
			panic("read(/proc/cmdline)");
		cmdline[sizeof(cmdline) - 1] = '\0';
		for (success_dev = strtok(cmdline, " \n"); success_dev; success_dev = strtok(NULL, " \n")) {
			if (strncmp(success_dev, "wg.success=", 11))
				continue;
			memcpy(success_dev + 11 - 5, "/dev/", 5);
			success_dev += 11 - 5;
			break;
		}
		if (!success_dev || !strlen(success_dev))
			panic("Unable to find success device");

		fd = open(success_dev, O_WRONLY);
		if (fd < 0)
			panic("open(success_dev)");
		if (write(fd, "success\n", 8) != 8)
			panic("write(success_dev)");
		close(fd);
	} else {
		const char *why = "unknown cause";
		int what = -1;

		if (WIFEXITED(status)) {
			why = "exit code";
			what = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			why = "signal";
			what = WTERMSIG(status);
		}
		printf("\x1b[31m\x1b[1m[-] Tests failed with %s %d! \u2639\x1b[0m\n", why, what);
	}
}

static void ensure_console(void)
{
	for (unsigned int i = 0; i < 1000; ++i) {
		int fd = open("/dev/console", O_RDWR);
		if (fd < 0) {
			usleep(50000);
			continue;
		}
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		close(fd);
		if (write(1, "\0\0\0\0\n", 5) == 5)
			return;
	}
	panic("Unable to open console device");
}

static void clear_leaks(void)
{
	int fd;

	fd = open("/sys/kernel/debug/kmemleak", O_WRONLY);
	if (fd < 0)
		return;
	pretty_message("[+] Starting memory leak detection...");
	write(fd, "clear\n", 5);
	close(fd);
}

static void check_leaks(void)
{
	int fd;

	fd = open("/sys/kernel/debug/kmemleak", O_WRONLY);
	if (fd < 0)
		return;
	pretty_message("[+] Scanning for memory leaks...");
	sleep(2); /* Wait for any grace periods. */
	write(fd, "scan\n", 5);
	close(fd);

	fd = open("/sys/kernel/debug/kmemleak", O_RDONLY);
	if (fd < 0)
		return;
	if (sendfile(1, fd, NULL, 0x7ffff000) > 0)
		panic("Memory leaks encountered");
	close(fd);
}

int main(int argc, char *argv[])
{
	seed_rng();
	ensure_console();
	print_banner();
	mount_filesystems();
	kmod_selftests();
	enable_logging();
	clear_leaks();
	launch_tests();
	check_leaks();
	poweroff();
	return 1;
}
