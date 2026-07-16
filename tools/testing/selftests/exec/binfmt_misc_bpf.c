// SPDX-License-Identifier: GPL-2.0
/*
 * Selftest for binfmt_misc bpf-backed ('B') handlers.
 *
 * A handler is a struct binfmt_misc_ops struct_ops map with a sleepable match
 * and a sleepable load program. Attaching it publishes it by name in the
 * caller's user namespace; a 'B' entry referencing it by name in the
 * interpreter field activates it:
 *
 *     echo ':name:B::::<handler>:' > /proc/sys/fs/binfmt_misc/register
 *
 * Two self-contained cases are exercised:
 *
 *   1. bpf_interp: the match program matches a synthetic aarch64 ELF header
 *      from the prefetched bprm->buf and the load program routes it to a
 *      fixed interpreter of its choosing.
 *   2. nix_origin: the match program reads the binary's program headers to
 *      commit only to a "$ORIGIN/..."-relative PT_INTERP and the load program
 *      resolves it to an interpreter co-located with the binary (the
 *      relocatable-loader case the kernel ELF loader cannot express).
 *
 * Both route to a test interpreter that prints BPF_INTERP_RAN, proving the
 * program's chosen interpreter actually ran.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <bpf/btf.h>
#include <bpf/libbpf.h>

#define INTERP_PATH	"/tmp/binfmt_bpf_interp"
#define AARCH64_PATH	"/tmp/binfmt_bpf_aarch64"
#define RELOC_DIR	"/tmp/binfmt_reloc"
#define BINFMT_REG	"/proc/sys/fs/binfmt_misc/register"
#define EXPECT		"BPF_INTERP_RAN"

static char testdir[512]; /* directory holding this test's built artifacts */

static int copy_file(const char *src, const char *dst)
{
	char buf[4096];
	int in, out;
	ssize_t n;

	in = open(src, O_RDONLY);
	if (in < 0)
		return -1;
	out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
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

/* A minimal 64-bit little-endian aarch64 ELF header, padded to the read size. */
static int create_fake_aarch64(const char *path)
{
	unsigned char hdr[256] = {0};
	int fd;

	hdr[0] = 0x7f; hdr[1] = 'E'; hdr[2] = 'L'; hdr[3] = 'F';
	hdr[4] = 2;			/* ELFCLASS64 */
	hdr[5] = 1;			/* ELFDATA2LSB */
	hdr[6] = 1;			/* EV_CURRENT */
	hdr[16] = 2;			/* e_type = ET_EXEC */
	hdr[18] = 183 & 0xff;		/* e_machine = EM_AARCH64 */
	hdr[19] = (183 >> 8) & 0xff;
	hdr[20] = 1;			/* e_version */

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0)
		return -1;
	if (write(fd, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int register_entry(const char *name, const char *handler)
{
	char rule[128];
	int fd;
	ssize_t n;

	snprintf(rule, sizeof(rule), ":%s:B::::%s:", name, handler);
	fd = open(BINFMT_REG, O_WRONLY);
	if (fd < 0)
		return -1;
	n = write(fd, rule, strlen(rule));
	close(fd);
	return n < 0 ? -1 : 0;
}

static void unregister_entry(const char *name)
{
	char path[128];
	int fd;

	snprintf(path, sizeof(path), "/proc/sys/fs/binfmt_misc/%s", name);
	fd = open(path, O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "-1", 2) < 0)
			; /* best effort */
		close(fd);
	}
}

static int check_output(const char *cmd, const char *expected)
{
	char buf[128];
	FILE *fp;

	fp = popen(cmd, "r");
	if (!fp)
		return -1;
	if (!fgets(buf, sizeof(buf), fp)) {
		pclose(fp);
		return -1;
	}
	pclose(fp);
	return strncmp(buf, expected, strlen(expected)) ? -1 : 0;
}

/*
 * Load @objfile, attach its struct_ops map @handler (which publishes the
 * handler), activate a 'B' entry named @entry that references it, run @target
 * and check it produced @expect.
 */
static int run_case(const char *objfile, const char *handler,
		    const char *entry, const char *target, const char *expect)
{
	struct bpf_object *obj;
	struct bpf_map *map;
	struct bpf_link *link;
	int ret = -1;

	obj = bpf_object__open_file(objfile, NULL);
	if (!obj || libbpf_get_error(obj)) {
		fprintf(stderr, "open %s failed\n", objfile);
		return -1;
	}
	if (bpf_object__load(obj)) {
		fprintf(stderr, "load %s failed (check dmesg for the verifier log)\n",
			objfile);
		goto close;
	}
	map = bpf_object__find_map_by_name(obj, handler);
	if (!map) {
		fprintf(stderr, "no struct_ops map '%s' in %s\n", handler, objfile);
		goto close;
	}
	link = bpf_map__attach_struct_ops(map);
	if (!link || libbpf_get_error(link)) {
		fprintf(stderr, "attach struct_ops '%s' failed\n", handler);
		goto close;
	}
	if (register_entry(entry, handler)) {
		fprintf(stderr, "register 'B' entry '%s' failed\n", entry);
		goto detach;
	}
	ret = check_output(target, expect);
	unregister_entry(entry);
detach:
	bpf_link__destroy(link);
close:
	bpf_object__close(obj);
	return ret;
}

int main(void)
{
	char src[600], obj[600], appdst[600], interpdst[600];
	char exe[512];
	ssize_t n;
	int fail = 0;
	struct stat st;
	struct btf *btf;

	if (getuid() != 0) {
		fprintf(stderr, "Skipping: test must be run as root\n");
		return 4; /* KSFT_SKIP */
	}

	/* The kernel must know struct binfmt_misc_ops (CONFIG_BINFMT_MISC_BPF). */
	btf = btf__load_vmlinux_btf();
	if (!btf || btf__find_by_name_kind(btf, "binfmt_misc_ops",
					   BTF_KIND_STRUCT) < 0) {
		fprintf(stderr,
			"Skipping: no struct binfmt_misc_ops in the kernel BTF (CONFIG_BINFMT_MISC_BPF)\n");
		btf__free(btf);
		return 4; /* KSFT_SKIP */
	}
	btf__free(btf);

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n < 0) {
		perror("readlink");
		return 1;
	}
	exe[n] = '\0';
	snprintf(testdir, sizeof(testdir), "%s", dirname(exe));

	if (stat("/sys/fs/bpf", &st) < 0)
		mkdir("/sys/fs/bpf", 0755);
	mount("bpf", "/sys/fs/bpf", "bpf", 0, NULL);
	if (access(BINFMT_REG, F_OK) < 0)
		mount("binfmt_misc", "/proc/sys/fs/binfmt_misc", "binfmt_misc", 0, NULL);

	/* Shared test interpreter. */
	snprintf(src, sizeof(src), "%s/binfmt_bpf_interp", testdir);
	if (copy_file(src, INTERP_PATH)) {
		fprintf(stderr, "cannot install %s\n", INTERP_PATH);
		return 1;
	}

	/* Case 1: match a synthetic aarch64 header -> fixed interpreter. */
	printf("[*] case 1: match aarch64 header -> program-chosen interpreter\n");
	if (create_fake_aarch64(AARCH64_PATH)) {
		fprintf(stderr, "cannot create %s\n", AARCH64_PATH);
		return 1;
	}
	snprintf(obj, sizeof(obj), "%s/bpf_interp.bpf.o", testdir);
	if (run_case(obj, "bpf_interp", "test_bpf_interp", AARCH64_PATH, EXPECT) == 0)
		printf("[+] case 1 passed\n");
	else {
		printf("[-] case 1 FAILED\n");
		fail = 1;
	}
	unlink(AARCH64_PATH);

	/* Case 2: $ORIGIN-relative PT_INTERP -> co-located interpreter. */
	printf("[*] case 2: $ORIGIN interpreter resolved relative to the binary\n");
	mkdir(RELOC_DIR, 0755);
	snprintf(appdst, sizeof(appdst), "%s/app", RELOC_DIR);
	snprintf(interpdst, sizeof(interpdst), "%s/binfmt_bpf_interp", RELOC_DIR);
	snprintf(src, sizeof(src), "%s/binfmt_bpf_app", testdir);
	if (copy_file(src, appdst) ||
	    copy_file(INTERP_PATH, interpdst)) {
		fprintf(stderr, "cannot set up %s\n", RELOC_DIR);
		fail = 1;
	} else {
		snprintf(obj, sizeof(obj), "%s/nix_origin.bpf.o", testdir);
		if (run_case(obj, "nix_origin", "test_bpf_origin", appdst, EXPECT) == 0)
			printf("[+] case 2 passed\n");
		else {
			printf("[-] case 2 FAILED\n");
			fail = 1;
		}
	}
	unlink(appdst);
	unlink(interpdst);
	rmdir(RELOC_DIR);
	unlink(INTERP_PATH);

	if (!fail)
		printf("[*] all binfmt_misc bpf cases passed\n");
	return fail;
}
