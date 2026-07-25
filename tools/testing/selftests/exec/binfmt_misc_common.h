/* SPDX-License-Identifier: GPL-2.0 */
/* Helpers shared by the binfmt_misc selftests. */
#ifndef __SELFTESTS_EXEC_BINFMT_MISC_COMMON_H
#define __SELFTESTS_EXEC_BINFMT_MISC_COMMON_H

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <link.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BINFMT_DIR	"/proc/sys/fs/binfmt_misc"
#define BINFMT_REG	BINFMT_DIR "/register"

/* comm holds 15 usable chars; a read of /proc/self/comm appends a newline. */
#define TASK_COMM_LEN	16

/* The canonical payload argv: run_payload() passes it, the payloads assert it. */
#define PAYLOAD_ARGV0	"payload-argv0"
#define PAYLOAD_ARG1	"argone"
#define PAYLOAD_ARG2	"argtwo"

/* Marker the loader tests poke into the payload's e_ident padding. */
#define LOADER_MARKER	"LDRTST"

/* Exit status run_payload() reports when the exec was refused as unhandled. */
#define RUN_ENOEXEC	42

static inline int copy_file(const char *src, const char *dst)
{
	char buf[4096];
	int in, out;
	ssize_t n;

	in = open(src, O_RDONLY);
	if (in < 0)
		return -1;
	/* The tests share /tmp, so never write through a name they don't own. */
	unlink(dst);
	out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0755);
	if (out < 0) {
		close(in);
		return -1;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0) {
		if (write(out, buf, n) != n) {
			close(in);
			close(out);
			return -1;
		}
	}
	close(in);
	close(out);
	return n < 0 ? -1 : 0;
}

/* Write @rule to the register file, preserving the write's errno. */
static inline int write_reg(const char *rule)
{
	int fd, saved;
	ssize_t n;

	fd = open(BINFMT_REG, O_WRONLY);
	if (fd < 0)
		return -1;
	n = write(fd, rule, strlen(rule));
	saved = errno;
	close(fd);
	errno = saved;
	return n < 0 ? -1 : 0;
}

static inline void unregister(const char *name)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), BINFMT_DIR "/%s", name);
	fd = open(path, O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "-1", 2) < 0)
			; /* best effort */
		close(fd);
	}
}

/* Mount binfmt_misc unless it already is, and report whether it is usable. */
static inline bool binfmt_misc_available(void)
{
	if (access(BINFMT_REG, F_OK) < 0)
		mount("binfmt_misc", BINFMT_DIR, "binfmt_misc", 0, NULL);
	return access(BINFMT_REG, F_OK) == 0;
}

/* Absolute path of @name in the directory this test was built into. */
static inline int artifact_path(char *out, size_t sz, const char *name)
{
	char exe[PATH_MAX];
	ssize_t n;

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n < 0)
		return -1;
	exe[n] = '\0';
	if ((size_t)snprintf(out, sz, "%s/%s", dirname(exe), name) >= sz)
		return -1;
	return 0;
}

/* Probe kernel support for a registration flag with a throwaway entry. */
static inline int binfmt_flag_supported(char flag)
{
	char rule[64];

	snprintf(rule, sizeof(rule), ":bm_flag_probe:E::bmprobe::/bin/true:%c",
		 flag);
	if (write_reg(rule))
		return -1;
	unregister("bm_flag_probe");
	return 0;
}

/*
 * Run @path with the canonical payload argv and return its exit status, or
 * RUN_ENOEXEC when the exec itself was refused as unhandled.
 */
static inline int run_payload(const char *path)
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		execl(path, PAYLOAD_ARGV0, PAYLOAD_ARG1, PAYLOAD_ARG2,
		      (char *)NULL);
		_exit(errno == ENOEXEC ? RUN_ENOEXEC : 126);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

/* Does the exe link name @path? */
static inline bool exe_is(const char *path)
{
	char exe[PATH_MAX], real[PATH_MAX];
	ssize_t n;

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n <= 0 || !realpath(path, real))
		return false;
	exe[n] = '\0';
	return !strcmp(exe, real);
}

/* Is comm @name truncated to what a comm can hold? */
static inline bool comm_is(const char *name)
{
	char comm[TASK_COMM_LEN + 2], expect[TASK_COMM_LEN];
	ssize_t n;
	int fd;

	fd = open("/proc/self/comm", O_RDONLY);
	if (fd < 0)
		return false;
	n = read(fd, comm, sizeof(comm) - 1);
	close(fd);
	if (n <= 0)
		return false;
	if (comm[n - 1] == '\n')
		n--;
	comm[n] = '\0';
	snprintf(expect, sizeof(expect), "%s", name);
	return !strcmp(comm, expect);
}

/* Opening @path for writing has to fail with ETXTBSY. */
static inline bool write_denied(const char *path)
{
	int fd = open(path, O_WRONLY);

	if (fd >= 0) {
		close(fd);
		return false;
	}
	return errno == ETXTBSY;
}

static inline int patch_file(const char *path, off_t off, const void *data, size_t len)
{
	ssize_t n;
	int fd;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	n = pwrite(fd, data, len, off);
	close(fd);
	return n == (ssize_t)len ? 0 : -1;
}

/* start_code and end_code are the 26th and 27th fields of /proc/pid/stat. */
static inline int stat_codes(pid_t pid, unsigned long *start_code,
			     unsigned long *end_code)
{
	char buf[4096], path[64], *p;
	ssize_t n;
	int fd, i;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	/* Skip "pid (comm)", then start_code is the 24th field after it. */
	p = strrchr(buf, ')');
	if (!p)
		return -1;
	p++;
	for (i = 0; i < 23; i++) {
		p = strchr(p + 1, ' ');
		if (!p)
			return -1;
	}
	if (sscanf(p, " %lu %lu", start_code, end_code) != 2)
		return -1;
	return 0;
}

/* Find the system loader through our own PT_INTERP. */
static inline int find_loader(char *out, size_t sz)
{
	ElfW(Ehdr) eh;
	ElfW(Phdr) ph;
	int fd, i, ret = -1;

	fd = open("/proc/self/exe", O_RDONLY);
	if (fd < 0)
		return -1;
	if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh))
		goto out;
	for (i = 0; i < eh.e_phnum; i++) {
		if (pread(fd, &ph, sizeof(ph),
			  eh.e_phoff + i * eh.e_phentsize) != sizeof(ph))
			goto out;
		if (ph.p_type != PT_INTERP)
			continue;
		if (!ph.p_filesz || ph.p_filesz > sz)
			goto out;
		if (pread(fd, out, ph.p_filesz, ph.p_offset) !=
		    (ssize_t)ph.p_filesz)
			goto out;
		out[ph.p_filesz - 1] = '\0';
		ret = 0;
		break;
	}
out:
	close(fd);
	return ret;
}

#endif /* __SELFTESTS_EXEC_BINFMT_MISC_COMMON_H */
