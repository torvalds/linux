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
 * Three self-contained cases are exercised:
 *
 *   1. bpf_interp: the match program matches a synthetic aarch64 ELF header
 *      from the prefetched bprm->buf and the load program routes it to a
 *      fixed interpreter of its choosing.
 *   2. nix_origin: the match program reads the binary's program headers to
 *      commit only to a "$ORIGIN/..."-relative PT_INTERP and the load program
 *      resolves it to an interpreter co-located with the binary (the
 *      relocatable-loader case the kernel ELF loader cannot express).
 *   3. transparent: the load program sets BPF_BINPRM_TRANSPARENT; the
 *      asserting interpreter (binfmt_transparent_interp) verifies the
 *      identity the kernel constructed (exe link, argv, cmdline, comm,
 *      AT_EXECFD, write denial) from inside the process.
 *
 * The first two route to a test interpreter that prints BPF_INTERP_RAN,
 * proving the program's chosen interpreter actually ran.
 */
#define _GNU_SOURCE
#include <elf.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <bpf/btf.h>
#include <bpf/libbpf.h>

#include "binfmt_misc_common.h"
#include "kselftest_harness.h"

#define INTERP_PATH	"/tmp/binfmt_bpf_interp"
#define AARCH64_PATH	"/tmp/binfmt_bpf_aarch64"
#define RELOC_TEMPLATE	"/tmp/binfmt_relocXXXXXX"
#define TRANS_INTERP	"/tmp/binfmt_transparent_interp"
#define TRANS_PATH	"/tmp/binfmt_bpf_riscv"
#define EXPECT		"BPF_INTERP_RAN"
#define TRANS_EXPECT	"TRANSPARENT_OK"

/* A minimal 64-bit little-endian ELF header, padded to the read size. */
static int create_fake_elf(const char *path, unsigned short machine)
{
	unsigned char hdr[256] = {0};
	int fd;

	hdr[0] = 0x7f; hdr[1] = 'E'; hdr[2] = 'L'; hdr[3] = 'F';
	hdr[4] = ELFCLASS64;
	hdr[5] = ELFDATA2LSB;
	hdr[6] = EV_CURRENT;
	hdr[16] = ET_EXEC;
	hdr[18] = machine & 0xff;	/* e_machine, little-endian */
	hdr[19] = machine >> 8;
	hdr[20] = EV_CURRENT;

	unlink(path);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0755);
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
	char rule[PATH_MAX];

	snprintf(rule, sizeof(rule), ":%s:B::::%s:", name, handler);
	return write_reg(rule);
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
	unregister(entry);
detach:
	bpf_link__destroy(link);
close:
	bpf_object__close(obj);
	return ret;
}

FIXTURE(bpf_handler) {
	char obj[PATH_MAX];	/* struct_ops object of the case under test */
};

FIXTURE_SETUP(bpf_handler)
{
	char src[PATH_MAX];
	struct btf *btf;

	if (getuid() != 0)
		SKIP(return, "test must be run as root");

	/* The kernel must know struct binfmt_misc_ops (CONFIG_BINFMT_MISC_BPF). */
	btf = btf__load_vmlinux_btf();
	if (!btf || btf__find_by_name_kind(btf, "binfmt_misc_ops",
					   BTF_KIND_STRUCT) < 0) {
		btf__free(btf);
		SKIP(return,
		     "no struct binfmt_misc_ops in the kernel BTF (CONFIG_BINFMT_MISC_BPF)");
	}
	btf__free(btf);

	if (!binfmt_misc_available())
		SKIP(return, "no binfmt_misc");

	/* Shared test interpreter. */
	ASSERT_EQ(artifact_path(src, sizeof(src), "binfmt_bpf_interp"), 0);
	ASSERT_EQ(copy_file(src, INTERP_PATH), 0);
}

FIXTURE_TEARDOWN(bpf_handler)
{
	unlink(INTERP_PATH);
}

/* The match program matches a synthetic header, the load program routes it. */
TEST_F(bpf_handler, fixed_interpreter)
{
	ASSERT_EQ(create_fake_elf(AARCH64_PATH, EM_AARCH64), 0);
	ASSERT_EQ(artifact_path(self->obj, sizeof(self->obj),
				"bpf_interp.bpf.o"), 0);
	EXPECT_EQ(run_case(self->obj, "bpf_interp", "test_bpf_interp",
			   AARCH64_PATH, EXPECT), 0);
	unlink(AARCH64_PATH);
}

/* A "$ORIGIN/..." PT_INTERP resolved to an interpreter next to the binary. */
TEST_F(bpf_handler, origin_relative_interpreter)
{
	char src[PATH_MAX], app[PATH_MAX], interp[PATH_MAX];
	char dir[] = RELOC_TEMPLATE;

	ASSERT_NE(mkdtemp(dir), NULL);
	snprintf(app, sizeof(app), "%s/app", dir);
	snprintf(interp, sizeof(interp), "%s/binfmt_bpf_interp", dir);
	ASSERT_EQ(artifact_path(src, sizeof(src), "binfmt_bpf_app"), 0);
	ASSERT_EQ(copy_file(src, app), 0);
	ASSERT_EQ(copy_file(INTERP_PATH, interp), 0);

	ASSERT_EQ(artifact_path(self->obj, sizeof(self->obj),
				"nix_origin.bpf.o"), 0);
	EXPECT_EQ(run_case(self->obj, "nix_origin", "test_bpf_origin",
			   app, EXPECT), 0);

	unlink(app);
	unlink(interp);
	rmdir(dir);
}

/* A transparent dispatch: the process presents as the binary, not the interp. */
TEST_F(bpf_handler, transparent_dispatch)
{
	char src[PATH_MAX], cmd[PATH_MAX + 16];

	/* Probe for transparent-mode support via its static counterpart. */
	if (binfmt_flag_supported('T'))
		SKIP(return, "kernel without transparent mode");

	ASSERT_EQ(artifact_path(src, sizeof(src), "binfmt_transparent_interp"), 0);
	ASSERT_EQ(copy_file(src, TRANS_INTERP), 0);
	ASSERT_EQ(create_fake_elf(TRANS_PATH, EM_RISCV), 0);

	setenv("BINFMT_TEST_BINARY", TRANS_PATH, 1);
	snprintf(cmd, sizeof(cmd), "%s argone argtwo", TRANS_PATH);
	ASSERT_EQ(artifact_path(self->obj, sizeof(self->obj),
				"transparent.bpf.o"), 0);
	EXPECT_EQ(run_case(self->obj, "transparent", "test_bpf_transparent",
			   cmd, TRANS_EXPECT), 0);

	unlink(TRANS_PATH);
	unlink(TRANS_INTERP);
}

TEST_HARNESS_MAIN
