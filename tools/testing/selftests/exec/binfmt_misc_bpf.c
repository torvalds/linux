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
 * Five self-contained cases are exercised:
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
 *   4. loader: the load program sets BPF_BINPRM_LOADER; the payload
 *      (binfmt_loader_payload) runs as the main image with the selected
 *      interpreter substituted for its PT_INTERP and asserts the native
 *      identity from inside.
 *   5. interp_bind: an entry registered disabled with 'D' is given its
 *      interpreters one write at a time, and the load program picks one by
 *      name per exec. Replacing what the path holds afterwards changes
 *      nothing, which is the point of binding a file rather than resolving
 *      a name at exec time. Enabling the entry seals it.
 *
 * The first two route to a test interpreter that prints BPF_INTERP_RAN,
 * proving the program's chosen interpreter actually ran.
 */
#define _GNU_SOURCE
#include <elf.h>
#include <limits.h>
#include <sched.h>
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
#define LOADER_INTERP	"/tmp/binfmt_loader_interp"
#define LOADER_PATH	"/tmp/binfmt_bpf_loader.ldrtest"
#define BIND_FIRST	"/tmp/binfmt_bind_first"
#define BIND_SECOND	"/tmp/binfmt_bind_second"
#define BIND_ARM_PATH	"/tmp/binfmt_bind_arm"
#define BIND_RISCV_PATH	"/tmp/binfmt_bind_riscv"
#define BIND_EXPECT	"BIND_RAN "
#define BIND_MAX	100
#define INTERP_LIMIT	"/proc/sys/user/max_binfmt_misc_interpreters"
/* Exit status of the binding child when it cannot set up a budget of its own. */
#define BIND_NO_BUDGET	200

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

/*
 * Register a 'B' entry for @handler. With @flags "D" the entry is created
 * disabled, which is what leaves it open to being given interpreters.
 */
static int register_entry(const char *name, const char *handler,
			  const char *flags)
{
	char rule[PATH_MAX];

	snprintf(rule, sizeof(rule), ":%s:B::::%s:%s", name, handler,
		 flags ? flags : "");
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

/* Does the kernel BTF know struct binfmt_misc_ops (CONFIG_BINFMT_MISC_BPF)? */
static bool have_binfmt_misc_ops(void)
{
	struct btf *btf = btf__load_vmlinux_btf();
	bool have;

	have = btf && btf__find_by_name_kind(btf, "binfmt_misc_ops",
					     BTF_KIND_STRUCT) >= 0;
	btf__free(btf);
	return have;
}

/* The reason bpf handler cases cannot run here, NULL if they can. */
static const char *bpf_handler_unsupported(void)
{
	if (getuid() != 0)
		return "test must be run as root";
	if (!have_binfmt_misc_ops())
		return "no struct binfmt_misc_ops in the kernel BTF (CONFIG_BINFMT_MISC_BPF)";
	if (!binfmt_misc_available())
		return "no binfmt_misc";
	return NULL;
}

/* An attached handler with its 'B' entry activated. */
struct bpf_case {
	struct bpf_object *obj;
	struct bpf_link *link;
	const char *entry;
};

/*
 * Load @objfile, attach its struct_ops map @handler (which publishes the
 * handler) and register a 'B' entry named @entry that references it, with
 * @flags as the entry's register-string flags.
 */
static int bpf_case_start_flags(struct bpf_case *c, const char *objfile,
				const char *handler, const char *entry,
				const char *flags)
{
	struct bpf_map *map;

	c->obj = NULL;
	c->link = NULL;
	c->entry = entry;

	c->obj = bpf_object__open_file(objfile, NULL);
	if (!c->obj || libbpf_get_error(c->obj)) {
		fprintf(stderr, "open %s failed\n", objfile);
		c->obj = NULL;
		return -1;
	}
	if (bpf_object__load(c->obj)) {
		fprintf(stderr, "load %s failed (check dmesg for the verifier log)\n",
			objfile);
		goto fail;
	}
	map = bpf_object__find_map_by_name(c->obj, handler);
	if (!map) {
		fprintf(stderr, "no struct_ops map '%s' in %s\n", handler, objfile);
		goto fail;
	}
	c->link = bpf_map__attach_struct_ops(map);
	if (!c->link || libbpf_get_error(c->link)) {
		fprintf(stderr, "attach struct_ops '%s' failed\n", handler);
		c->link = NULL;
		goto fail;
	}
	if (register_entry(entry, handler, flags)) {
		fprintf(stderr, "register 'B' entry '%s' failed\n", entry);
		goto fail;
	}
	return 0;

fail:
	bpf_link__destroy(c->link);
	bpf_object__close(c->obj);
	c->obj = NULL;
	c->link = NULL;
	return -1;
}

static int bpf_case_start(struct bpf_case *c, const char *objfile,
			  const char *handler, const char *entry)
{
	return bpf_case_start_flags(c, objfile, handler, entry, NULL);
}

static void bpf_case_stop(struct bpf_case *c)
{
	unregister(c->entry);
	bpf_link__destroy(c->link);
	bpf_object__close(c->obj);
}

/* Activate @handler, run @target and check it produced @expect. */
static int run_case(const char *objfile, const char *handler,
		    const char *entry, const char *target, const char *expect)
{
	struct bpf_case c;
	int ret;

	if (bpf_case_start(&c, objfile, handler, entry))
		return -1;
	ret = check_output(target, expect);
	bpf_case_stop(&c);
	return ret;
}

FIXTURE(bpf_handler) {
	char obj[PATH_MAX];	/* struct_ops object of the case under test */
};

FIXTURE_SETUP(bpf_handler)
{
	char src[PATH_MAX];
	const char *why = bpf_handler_unsupported();

	if (why)
		SKIP(return, "%s", why);

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
	if (!binfmt_flag_supported('T'))
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

/* A per-exec loader substitution: the payload runs as a native exec. */
TEST_F(bpf_handler, loader_substitution)
{
	char src[PATH_MAX], loader[PATH_MAX];
	struct bpf_case c;
	int status;

	if (find_loader(loader, sizeof(loader)))
		SKIP(return, "cannot determine own PT_INTERP");

	ASSERT_EQ(copy_file(loader, LOADER_INTERP), 0);
	ASSERT_EQ(artifact_path(src, sizeof(src), "binfmt_loader_payload"), 0);
	ASSERT_EQ(copy_file(src, LOADER_PATH), 0);
	ASSERT_EQ(patch_file(LOADER_PATH, EI_PAD, LOADER_MARKER,
			     strlen(LOADER_MARKER)), 0);
	ASSERT_EQ(artifact_path(self->obj, sizeof(self->obj),
				"loader.bpf.o"), 0);

	setenv("BINFMT_TEST_BINARY", LOADER_PATH, 1);
	setenv("BINFMT_TEST_INTERP", LOADER_INTERP, 1);

	ASSERT_EQ(bpf_case_start(&c, self->obj, "loader", "test_bpf_loader"), 0);
	status = run_payload(LOADER_PATH);
	bpf_case_stop(&c);
	EXPECT_EQ(status, 0);

	unsetenv("BINFMT_TEST_INTERP");
	unlink(LOADER_PATH);
	unlink(LOADER_INTERP);
}

/* The errno an exec of @path fails with, 0 if it succeeded. */
static int exec_errno(const char *path)
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		execl(path, path, (char *)NULL);
		_exit(errno);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

/* Install a copy of the bound-interpreter test binary at @path. */
static int install_interp(const char *path)
{
	char src[PATH_MAX];

	if (artifact_path(src, sizeof(src), "binfmt_bind_interp"))
		return -1;
	return copy_file(src, path);
}

/* Bind @path to @entry under @name, the '+' command of a disabled entry. */
static int entry_bind(const char *entry, const char *name, const char *path)
{
	char cmd[PATH_MAX];

	snprintf(cmd, sizeof(cmd), "+%s %s\n", name, path);
	return entry_command(entry, cmd);
}

/* Set the interpreter budget of this namespace. */
static int write_interp_limit(const char *val)
{
	ssize_t n;
	int fd;

	fd = open(INTERP_LIMIT, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = write(fd, val, strlen(val));
	close(fd);
	return n < 0 ? -1 : 0;
}

/*
 * The errno a bind is refused with when the writer is a child that has spent
 * the budget of a user namespace of its own, 0 if it succeeded and -1 if the
 * child could not set itself up. The fd is opened here and inherited, so the
 * interpreter is still opened with this process's credentials.
 */
static int bind_out_of_budget(const char *entry, const char *name,
			      const char *path)
{
	char cmd[PATH_MAX], file[PATH_MAX];
	int fd, status, retval;
	pid_t pid;

	snprintf(file, sizeof(file), BINFMT_DIR "/%s", entry);
	snprintf(cmd, sizeof(cmd), "+%s %s\n", name, path);

	fd = open(file, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	pid = fork();
	if (pid == 0) {
		ssize_t n;

		/* A namespace of its own, with nothing left in it to spend. */
		if (unshare(CLONE_NEWUSER) || write_interp_limit("0"))
			_exit(BIND_NO_BUDGET);
		n = write(fd, cmd, strlen(cmd));
		_exit(n < 0 ? errno : 0);
	}
	close(fd);
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	retval = WEXITSTATUS(status);
	return retval == BIND_NO_BUDGET ? -1 : retval;
}

FIXTURE(bound_interp) {
	char obj[PATH_MAX];
	struct bpf_case c;
	bool started;
};

FIXTURE_SETUP(bound_interp)
{
	const char *why = bpf_handler_unsupported();

	if (why)
		SKIP(return, "%s", why);
	if (!binfmt_flag_supported('D')) {
		ASSERT_EQ(errno, EINVAL);
		SKIP(return, "kernel without the 'D' flag");
	}

	ASSERT_EQ(install_interp(BIND_FIRST), 0);
	ASSERT_EQ(install_interp(BIND_SECOND), 0);

	ASSERT_EQ(artifact_path(self->obj, sizeof(self->obj),
				"interp_bind.bpf.o"), 0);

	/*
	 * Registered disabled, so it cannot be matched yet and can still be
	 * given interpreters. Each path is resolved once, by its write(2);
	 * from here on the entry holds the files themselves.
	 */
	ASSERT_EQ(bpf_case_start_flags(&self->c, self->obj, "interp_bind",
				       "test_interp_bind", "D"), 0);
	self->started = true;

	ASSERT_EQ(entry_bind("test_interp_bind", "first", BIND_FIRST), 0);
	ASSERT_EQ(entry_bind("test_interp_bind", "second", BIND_SECOND), 0);
}

FIXTURE_TEARDOWN(bound_interp)
{
	if (self->started)
		bpf_case_stop(&self->c);
	unlink(BIND_FIRST);
	unlink(BIND_SECOND);
	unlink(AARCH64_PATH);
	unlink(BIND_RISCV_PATH);
	unlink(BIND_ARM_PATH);
}

/* Enabling is what makes the configured entry matchable. */
static int activate(const char *entry)
{
	return entry_command(entry, "1\n");
}

/* One entry, one interpreter per guest architecture, picked per exec. */
TEST_F(bound_interp, selects_by_name)
{
	ASSERT_EQ(create_fake_elf(AARCH64_PATH, EM_AARCH64), 0);
	ASSERT_EQ(create_fake_elf(BIND_RISCV_PATH, EM_RISCV), 0);

	/* Disabled, so it does not match and no format claims the binary. */
	EXPECT_EQ(exec_errno(AARCH64_PATH), ENOEXEC);

	ASSERT_EQ(activate("test_interp_bind"), 0);
	EXPECT_EQ(check_output(AARCH64_PATH, BIND_EXPECT BIND_FIRST), 0);
	EXPECT_EQ(check_output(BIND_RISCV_PATH, BIND_EXPECT BIND_SECOND), 0);
}

/* What was bound is what runs, whatever the path holds afterwards. */
TEST_F(bound_interp, path_no_longer_decides)
{
	char other[PATH_MAX];

	ASSERT_EQ(create_fake_elf(AARCH64_PATH, EM_AARCH64), 0);
	ASSERT_EQ(activate("test_interp_bind"), 0);

	/* Bound interpreters are pinned against writes, exactly like 'F'. */
	EXPECT_TRUE(write_denied(BIND_FIRST));

	/* Replace the path with a different binary: a new file, new inode. */
	ASSERT_EQ(artifact_path(other, sizeof(other), "binfmt_bpf_interp"), 0);
	ASSERT_EQ(unlink(BIND_FIRST), 0);
	ASSERT_EQ(copy_file(other, BIND_FIRST), 0);

	EXPECT_EQ(check_output(AARCH64_PATH, BIND_EXPECT BIND_FIRST), 0);
}

/* The entry reports what it bound, under the names it bound them as. */
TEST_F(bound_interp, entry_reports_bindings)
{
	EXPECT_TRUE(entry_shows("test_interp_bind",
				"bpf-interpreter first " BIND_FIRST));
	EXPECT_TRUE(entry_shows("test_interp_bind",
				"bpf-interpreter second " BIND_SECOND));
}

/* Selecting a name the entry did not bind fails the exec. */
TEST_F(bound_interp, unbound_name_fails)
{
	ASSERT_EQ(create_fake_elf(BIND_ARM_PATH, EM_ARM), 0);
	ASSERT_EQ(activate("test_interp_bind"), 0);

	EXPECT_EQ(exec_errno(BIND_ARM_PATH), ENOENT);
}

/* Activating seals it: what can be matched cannot be changed. */
TEST_F(bound_interp, sealed_once_active)
{
	ASSERT_EQ(activate("test_interp_bind"), 0);

	EXPECT_EQ(entry_bind("test_interp_bind", "third", BIND_SECOND), -EBUSY);
	EXPECT_FALSE(entry_shows("test_interp_bind",
				 "bpf-interpreter third " BIND_SECOND));
}

/* The seal is for good: disabling the entry again reopens nothing. */
TEST_F(bound_interp, disable_does_not_unseal)
{
	ASSERT_EQ(activate("test_interp_bind"), 0);
	ASSERT_EQ(entry_command("test_interp_bind", "0\n"), 0);

	EXPECT_EQ(entry_bind("test_interp_bind", "third", BIND_SECOND), -EBUSY);
}

/* An entry registered without 'D' is sealed from the start. */
TEST_F(bound_interp, born_sealed)
{
	/* A second entry for the handler the fixture already published. */
	ASSERT_EQ(register_entry("test_born_sealed", "interp_bind", NULL), 0);

	EXPECT_EQ(entry_bind("test_born_sealed", "first", BIND_FIRST), -EBUSY);
	unregister("test_born_sealed");
}

/* A name is bound once; a second use of it is refused. */
TEST_F(bound_interp, duplicate_name_refused)
{
	EXPECT_EQ(entry_bind("test_interp_bind", "first", BIND_SECOND), -EEXIST);
}

/* A name is a printable word: the entry file reports 'name path' lines. */
TEST_F(bound_interp, name_must_be_printable)
{
	/* A control character would forge a line into the entry file. */
	EXPECT_EQ(entry_bind("test_interp_bind", "a\tb", BIND_FIRST), -EINVAL);
	EXPECT_EQ(entry_bind("test_interp_bind", "a\nb", BIND_FIRST), -EINVAL);

	/* A space cannot even be spelled: the path starts after the first one. */
	EXPECT_EQ(entry_bind("test_interp_bind", "a b", BIND_FIRST), -EINVAL);
}

/* The command ends at the write: bytes past an embedded nul are refused. */
TEST_F(bound_interp, trailing_bytes_refused)
{
	char cmd[PATH_MAX];
	size_t len;
	int fd;

	/* entry_command() cannot spell a nul, so write the buffer raw. */
	snprintf(cmd, sizeof(cmd), "+nul %s", BIND_FIRST);
	len = strlen(cmd) + 1;
	memcpy(cmd + len, "junk", sizeof("junk"));
	len += sizeof("junk");

	fd = open(BINFMT_DIR "/test_interp_bind", O_WRONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(write(fd, cmd, len), -1);
	EXPECT_EQ(errno, EINVAL);
	close(fd);

	EXPECT_FALSE(entry_shows("test_interp_bind",
				 "bpf-interpreter nul " BIND_FIRST));
}

/* An entry binds at most BIND_MAX interpreters. */
TEST_F(bound_interp, capped_bindings)
{
	char name[16];
	int i;

	/* The fixture bound "first" and "second" already. */
	for (i = 2; i < BIND_MAX; i++) {
		snprintf(name, sizeof(name), "n%d", i);
		ASSERT_EQ(entry_bind("test_interp_bind", name, BIND_FIRST), 0);
	}
	EXPECT_EQ(entry_bind("test_interp_bind", "over", BIND_FIRST), -ENOSPC);
}

/* A binding pins a file: it is charged, and refused once the budget is out. */
TEST_F(bound_interp, bindings_are_charged)
{
	int err = bind_out_of_budget("test_interp_bind", "third", BIND_FIRST);

	if (err < 0)
		SKIP(return, "no user namespaces or no " INTERP_LIMIT);

	/* The charge follows the writer, not the entry file it writes to. */
	EXPECT_EQ(err, ENOSPC);

	/* The budget was the only thing in the way. */
	EXPECT_EQ(entry_bind("test_interp_bind", "third", BIND_FIRST), 0);
}

TEST_HARNESS_MAIN
