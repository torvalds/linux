// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include <test_progs.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/keyctl.h>
#include <linux/bpf.h>

#include "bpf/libbpf_internal.h" /* for libbpf_sha256() */
#include "bpf/skel_internal.h"	 /* for loader ctx layout (bpf_loader_ctx etc) */

#include "test_signed_loader.skel.h"
#include "test_signed_loader_map.skel.h"
#include "test_signed_loader_data.skel.h"
#include "test_signed_loader_lsm.skel.h"

#define SIG_MATCH_INSNS 33 /* excl (5) + 4 * sha-dword (7) */

enum {
	BPF_SIG_UNSIGNED = 0,
	BPF_SIG_VERIFIED,
};

enum {
	BPF_SIG_KEYRING_NONE = 0,
	BPF_SIG_KEYRING_BUILTIN,
	BPF_SIG_KEYRING_SECONDARY,
	BPF_SIG_KEYRING_PLATFORM,
	BPF_SIG_KEYRING_USER,
};

static int load_loader(const void *insns, __u32 insns_sz, int map_fd,
		       const void *sig, __u32 sig_sz, __s32 keyring_id)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = insns_sz / sizeof(struct bpf_insn);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.fd_array = ptr_to_u64(&map_fd);
	if (sig) {
		attr.signature = ptr_to_u64(sig);
		attr.signature_size = sig_sz;
		attr.keyring_id = keyring_id;
	}
	memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));
	fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
		     offsetofend(union bpf_attr, keyring_id));
	return fd < 0 ? -errno : fd;
}

static int run_gen_loader(const void *insns, __u32 insns_sz,
			  const void *data, __u32 data_sz,
			  const void *excl, __u32 excl_sz,
			  const void *sig, __u32 sig_sz,
			  bool get_hash, void *ctx, __u32 ctx_sz, bool *loader_ran)
{
	LIBBPF_OPTS(bpf_map_create_opts, mopts,
		    .excl_prog_hash = excl,
		    .excl_prog_hash_size = excl_sz);
	__u8 hbuf[SHA256_DIGEST_LENGTH];
	struct bpf_map_info info;
	__u32 ilen = sizeof(info), key = 0;
	union bpf_attr attr;
	int map_fd, prog_fd, ret;

	*loader_ran = false;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "__loader.map",
				4, data_sz, 1, &mopts);
	if (map_fd < 0)
		return -errno;
	if (bpf_map_update_elem(map_fd, &key, data, 0)) {
		ret = -errno;
		goto out_map;
	}
	if (bpf_map_freeze(map_fd)) {
		ret = -errno;
		goto out_map;
	}
	if (get_hash) {
		memset(&info, 0, sizeof(info));
		info.hash = ptr_to_u64(hbuf);
		info.hash_size = sizeof(hbuf);
		if (bpf_map_get_info_by_fd(map_fd, &info, &ilen)) {
			ret = -errno;
			goto out_map;
		}
	}

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = insns_sz / sizeof(struct bpf_insn);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.fd_array = ptr_to_u64(&map_fd);
	if (sig) {
		attr.signature = ptr_to_u64(sig);
		attr.signature_size = sig_sz;
		attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	}
	memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));
	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	if (prog_fd < 0) {
		ret = -errno;
		goto out_map;
	}

	memset(&attr, 0, sizeof(attr));
	attr.test.prog_fd = prog_fd;
	attr.test.ctx_in = ptr_to_u64(ctx);
	attr.test.ctx_size_in = ctx_sz;
	if (syscall(__NR_bpf, BPF_PROG_RUN, &attr,
		    offsetofend(union bpf_attr, test)) < 0) {
		ret = -errno;
		goto out_prog;
	}
	*loader_ran = true;
	ret = (int)attr.test.retval;
out_prog:
	close(prog_fd);
out_map:
	close(map_fd);
	return ret;
}

static void close_loader_ctx_fds(void *ctx, int nr_maps, int nr_progs)
{
	struct bpf_map_desc *md = (struct bpf_map_desc *)((char *)ctx +
				  sizeof(struct bpf_loader_ctx));
	struct bpf_prog_desc *pd = (struct bpf_prog_desc *)(md + nr_maps);
	int i;

	for (i = 0; i < nr_maps; i++)
		if (md[i].map_fd > 0)
			close(md[i].map_fd);
	for (i = 0; i < nr_progs; i++)
		if (pd[i].prog_fd > 0)
			close(pd[i].prog_fd);
}

static int run_setup(const char *cmd, const char *dir)
{
	int pid, status;

	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		execlp("./verify_sig_setup.sh", "./verify_sig_setup.sh",
		       cmd, dir, NULL);
		exit(1);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	return (WIFEXITED(status) &&
		WEXITSTATUS(status) == 0) ? 0 : -EINVAL;
}

static int sign_buf(const char *dir, const void *buf, __u32 len,
		    void *sig, __u32 *sig_sz)
{
	char data_tmpl[PATH_MAX], key[PATH_MAX];
	char sigpath[PATH_MAX + sizeof(".p7s")];
	int fd, pid, status, ret;
	struct stat st;

	ret = snprintf(data_tmpl, sizeof(data_tmpl), "%s/dataXXXXXX", dir);
	if (ret < 0 || ret >= (int)sizeof(data_tmpl))
		return -ENAMETOOLONG;
	ret = 0;

	fd = mkstemp(data_tmpl);
	if (fd < 0)
		return -errno;
	if (write(fd, buf, len) != (ssize_t)len) {
		close(fd);
		ret = -EIO;
		goto out;
	}
	close(fd);

	pid = fork();
	if (pid < 0) {
		ret = -errno;
		goto out;
	}
	if (pid == 0) {
		snprintf(key, sizeof(key), "%s/signing_key.pem", dir);
		execlp("./sign-file", "./sign-file", "-d", "sha256",
		       key, key, data_tmpl, NULL);
		exit(1);
	}
	if (waitpid(pid, &status, 0) < 0 ||
	    !WIFEXITED(status) || WEXITSTATUS(status)) {
		ret = -EINVAL;
		goto out;
	}

	snprintf(sigpath, sizeof(sigpath), "%s.p7s", data_tmpl);
	if (stat(sigpath, &st) < 0) {
		ret = -errno;
		goto out;
	}
	if (st.st_size > (off_t)*sig_sz) {
		ret = -E2BIG;
		goto out_sig;
	}
	fd = open(sigpath, O_RDONLY);
	if (fd < 0) {
		ret = -errno;
		goto out_sig;
	}
	if (read(fd, sig, st.st_size) != st.st_size) {
		close(fd);
		ret = -EIO;
		goto out_sig;
	}
	close(fd);
	*sig_sz = st.st_size;
out_sig:
	unlink(sigpath);
out:
	unlink(data_tmpl);
	return ret;
}

static void check_sig_match_shape(const struct bpf_insn *in, int n)
{
	int a = -1, cleanup = -1, i, base, t, br[5], nb = 0;

	/* BPF_PSEUDO_MAP_IDX (the struct bpf_map * form) is used only here. */
	for (i = 0; i + 1 < n; i++) {
		if (in[i].code == (BPF_LD | BPF_IMM | BPF_DW) &&
		    in[i].src_reg == BPF_PSEUDO_MAP_IDX) {
			a = i;
			break;
		}
	}
	if (!ASSERT_GE(a, 0, "emit_signature_match present"))
		return;
	if (!ASSERT_LE(a + SIG_MATCH_INSNS, n, "block fits in program"))
		return;

	/* excl check: r2 = *(u32 *)(map + 32); if r2 != 1 goto cleanup */
	ASSERT_EQ(in[a + 2].code, (BPF_LDX | BPF_MEM | BPF_W), "excl load width");
	ASSERT_EQ(in[a + 2].off, SHA256_DIGEST_LENGTH, "excl field offset");
	ASSERT_EQ(in[a + 4].code, (BPF_JMP | BPF_JNE | BPF_K), "excl branch op");
	ASSERT_EQ(in[a + 4].imm, 1, "excl compared to 1");
	br[nb++] = a + 4;

	/* 4 sha-dword checks: r2 = *(u64 *)(map + i*8); if r2 != r3 goto cleanup */
	for (i = 0; i < 4; i++) {
		base = a + 5 + i * 7;
		ASSERT_EQ(in[base + 2].code, (BPF_LDX | BPF_MEM | BPF_DW), "sha load width");
		ASSERT_EQ(in[base + 2].off, i * 8, "sha dword offset");
		ASSERT_EQ(in[base + 3].code, (BPF_LD | BPF_IMM | BPF_DW), "sha imm64 (H_meta)");
		ASSERT_EQ(in[base + 6].code, (BPF_JMP | BPF_JNE | BPF_X), "sha branch op");
		br[nb++] = base + 6;
	}

	/*
	 * Locate the real cleanup label so we can pin the exact jump target,
	 * not just "some backward label". bpf_gen__init() emits the cleanup
	 * block as a prog-fd close loop whose first instruction is the label
	 * every error branch jumps to.
	 */
	for (i = 0; i + 2 < a; i++) {
		if (in[i].code == (BPF_LDX | BPF_MEM | BPF_W) &&
		    in[i].dst_reg == BPF_REG_1 && in[i].src_reg == BPF_REG_10 &&
		    in[i + 1].code == (BPF_JMP | BPF_JSLE | BPF_K) &&
		    in[i + 1].dst_reg == BPF_REG_1 && in[i + 1].imm == 0 &&
		    in[i + 1].off == 1 &&
		    in[i + 2].code == (BPF_JMP | BPF_CALL) &&
		    in[i + 2].imm == BPF_FUNC_sys_close) {
			cleanup = i;
			break;
		}
	}
	if (!ASSERT_GE(cleanup, 0, "cleanup label located"))
		return;
	for (i = 0; i < nb; i++) {
		t = br[i] + 1 + in[br[i]].off;
		ASSERT_EQ(t, cleanup, "sig-match lands on cleanup");
	}
	/*
	 * Same invariant for every other cleanup-bound jump in the program:
	 * emit_check_err() is the only source of "if (r7 < 0) goto cleanup",
	 * so each of those must also resolve exactly to cleanup.
	 */
	for (i = 0, t = 0; i < n; i++) {
		if (in[i].code != (BPF_JMP | BPF_JSLT | BPF_K) ||
		    in[i].dst_reg != BPF_REG_7 || in[i].imm != 0 || in[i].off >= 0)
			continue;
		ASSERT_EQ(i + 1 + in[i].off, cleanup, "err-check lands on cleanup");
		t++;
	}
	ASSERT_GT(t, 0, "found emit_check_err jumps");
}

struct gen_loader_fixture {
	struct test_signed_loader *skel;
	struct gen_loader_opts gopts;
	unsigned char *blob;
	void *ctx;
	__u32 data_sz;
	__u32 ctx_sz;
	int nr_maps;
	int nr_progs;
	__u8 excl[SHA256_DIGEST_LENGTH];
};

static int gen_loader_fixture_init(struct gen_loader_fixture *f)
{
	LIBBPF_OPTS(gen_loader_opts, gopts, .gen_hash = true);
	int nr_maps = 0, nr_progs = 0;
	struct bpf_program *p;
	struct bpf_map *m;

	memset(f, 0, sizeof(*f));
	f->skel = test_signed_loader__open();
	if (!ASSERT_OK_PTR(f->skel, "skel_open"))
		return -1;
	if (!ASSERT_OK(bpf_object__gen_loader(f->skel->obj, &gopts), "gen_loader"))
		return -1;
	if (!ASSERT_OK(bpf_object__load(f->skel->obj), "gen_load"))
		return -1;
	f->gopts = gopts;

	bpf_object__for_each_program(p, f->skel->obj)
		nr_progs++;
	bpf_object__for_each_map(m, f->skel->obj)
		nr_maps++;
	f->nr_maps = nr_maps;
	f->nr_progs = nr_progs;
	f->ctx_sz = sizeof(struct bpf_loader_ctx) +
		    nr_maps * sizeof(struct bpf_map_desc) +
		    nr_progs * sizeof(struct bpf_prog_desc);
	f->ctx = calloc(1, f->ctx_sz);
	if (!ASSERT_OK_PTR(f->ctx, "ctx_alloc"))
		return -1;
	((struct bpf_loader_ctx *)f->ctx)->sz = f->ctx_sz;

	f->data_sz = gopts.data_sz;
	f->blob = malloc(f->data_sz);
	if (!ASSERT_OK_PTR(f->blob, "blob_alloc"))
		return -1;
	memcpy(f->blob, gopts.data, f->data_sz);

	/* excl_prog_hash = SHA256(loader insns) == the loader's prog->digest. */
	libbpf_sha256(gopts.insns, gopts.insns_sz, f->excl);
	return 0;
}

static void gen_loader_fixture_fini(struct gen_loader_fixture *f)
{
	if (f->ctx)
		close_loader_ctx_fds(f->ctx, f->nr_maps, f->nr_progs);
	free(f->blob);
	free(f->ctx);
	test_signed_loader__destroy(f->skel);
}

static void metadata_check_shape(void)
{
	struct gen_loader_fixture f;

	if (gen_loader_fixture_init(&f) == 0)
		check_sig_match_shape((const struct bpf_insn *)f.gopts.insns,
				      f.gopts.insns_sz / sizeof(struct bpf_insn));
	gen_loader_fixture_fini(&f);
}

static void metadata_match(void)
{
	struct gen_loader_fixture f;
	bool ran;
	int r;

	if (gen_loader_fixture_init(&f) == 0) {
		r = run_gen_loader(f.gopts.insns, f.gopts.insns_sz, f.blob,
				   f.data_sz, f.excl, sizeof(f.excl), NULL, 0,
				   true, f.ctx, f.ctx_sz, &ran);
		ASSERT_TRUE(ran, "loader ran");
		ASSERT_EQ(r, 0, "honest loader retval");
	}
	gen_loader_fixture_fini(&f);
}

static void metadata_sha_mismatch(void)
{
	struct gen_loader_fixture f;
	bool ran;
	int r;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * blob[0] lives in the loader's fd_array scratch (first add_data in
		 * bpf_gen__init); a 0-map program never reads it, so flipping it
		 * changes only map->sha. The metadata check is the only thing that
		 * can notice -> isolates emit_signature_match.
		 */
		f.blob[0] ^= 0xff;
		r = run_gen_loader(f.gopts.insns, f.gopts.insns_sz, f.blob,
				   f.data_sz, f.excl, sizeof(f.excl), NULL, 0,
				   true, f.ctx, f.ctx_sz, &ran);
		ASSERT_TRUE(ran, "loader ran");
		ASSERT_EQ(r, -EINVAL, "tampered blob rejected by emit_signature_match");
	}
	gen_loader_fixture_fini(&f);
}

static void metadata_not_exclusive(void)
{
	struct gen_loader_fixture f;
	bool ran;
	int r;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * Correct blob but a non-exclusive metadata map: the verifier does
		 * not reject (excl_prog_sha unset), so the runtime map->excl == 1
		 * check in the loader must.
		 */
		r = run_gen_loader(f.gopts.insns, f.gopts.insns_sz, f.blob,
				   f.data_sz, NULL, 0, NULL, 0, true, f.ctx,
				   f.ctx_sz, &ran);
		ASSERT_TRUE(ran, "loader ran");
		ASSERT_EQ(r, -EINVAL, "non-exclusive metadata map rejected");
	}
	gen_loader_fixture_fini(&f);
}

static void metadata_hash_not_computed(void)
{
	struct gen_loader_fixture f;
	bool ran;
	int r;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * Correct, exclusive, frozen map, but its hash was never computed
		 * (no OBJ_GET_INFO_BY_FD), so map->sha stays zero. The loader must
		 * fail closed rather than treat an unset hash as a match.
		 */
		r = run_gen_loader(f.gopts.insns, f.gopts.insns_sz, f.blob,
				   f.data_sz, f.excl, sizeof(f.excl), NULL, 0,
				   false, f.ctx, f.ctx_sz, &ran);
		ASSERT_TRUE(ran, "loader ran");
		ASSERT_EQ(r, -EINVAL, "uncomputed metadata hash rejected");
	}
	gen_loader_fixture_fini(&f);
}

static void signature_enforced(void)
{
	static const __u8 junk[64] = { 0x30, 0x42, 0x13, 0x37, };
	struct gen_loader_fixture f;
	int fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * A present-but-invalid signature (the cert bytes are not a
		 * PKCS#7 signature) must be rejected at load: the signature
		 * path is honored, not ignored. (The valid path is covered by
		 * the signed lskels.)
		 */
		fd = load_loader(f.gopts.insns, f.gopts.insns_sz, -1, junk,
				 sizeof(junk), KEY_SPEC_SESSION_KEYRING);
		ASSERT_LT(fd, 0, "invalid signature rejected at load");
	}
	gen_loader_fixture_fini(&f);
}

static void signature_too_large(void)
{
	static const __u8 junk[64] = {};
	struct gen_loader_fixture f;
	int fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * signature_size beyond the kernel's bound (KMALLOC_MAX_CACHE_SIZE)
		 * is rejected before the buffer is read.
		 */
		fd = load_loader(f.gopts.insns, f.gopts.insns_sz, -1, junk,
				 64 << 20, KEY_SPEC_SESSION_KEYRING);
		ASSERT_EQ(fd, -EINVAL, "oversized signature rejected");
	}
	gen_loader_fixture_fini(&f);
}

static void signature_bad_keyring(void)
{
	static const __u8 junk[64] = {};
	struct gen_loader_fixture f;
	int fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * A present signature with a keyring_id that resolves to no key is
		 * rejected up front: bpf_prog_verify_signature() fails the keyring
		 * lookup (-EINVAL) before it ever looks at the signature bytes. A
		 * large positive serial takes the user-keyring path and won't exist.
		 */
		fd = load_loader(f.gopts.insns, f.gopts.insns_sz, -1, junk,
				 sizeof(junk), INT_MAX);
		ASSERT_EQ(fd, -EINVAL, "signature with bad keyring_id rejected");
	}
	gen_loader_fixture_fini(&f);
}

/*
 * A signed loader must ignore ctx-supplied map dimensions: the host cannot
 * resize a signed program's maps via the loader ctx. Drive a one-map program
 * through gen_loader, ask (via ctx) for every map to be resized to a bogus
 * value, and confirm the created maps keep their attested size.
 */
#define GATING_BOGUS_MAX 0x4000

static void metadata_ctx_max_entries_ignored(void)
{
	LIBBPF_OPTS(gen_loader_opts, gopts, .gen_hash = true);
	struct test_signed_loader_map *skel;
	__u8 excl[SHA256_DIGEST_LENGTH];
	int nr_maps = 0, nr_progs = 0, i, checked = 0, r;
	struct bpf_program *p;
	struct bpf_map *m;
	struct bpf_map_desc *md;
	unsigned char *blob;
	__u32 ctx_sz, data_sz;
	void *ctx;
	bool ran;

	skel = test_signed_loader_map__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;
	if (!ASSERT_OK(bpf_object__gen_loader(skel->obj, &gopts), "gen_loader"))
		goto destroy;
	if (!ASSERT_OK(bpf_object__load(skel->obj), "gen_load"))
		goto destroy;

	bpf_object__for_each_program(p, skel->obj)
		nr_progs++;
	bpf_object__for_each_map(m, skel->obj)
		nr_maps++;
	ctx_sz = sizeof(struct bpf_loader_ctx) +
		 nr_maps * sizeof(struct bpf_map_desc) +
		 nr_progs * sizeof(struct bpf_prog_desc);
	ctx = calloc(1, ctx_sz);
	if (!ASSERT_OK_PTR(ctx, "ctx_alloc"))
		goto destroy;
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;

	md = (struct bpf_map_desc *)((char *)ctx + sizeof(struct bpf_loader_ctx));
	for (i = 0; i < nr_maps; i++)
		md[i].max_entries = GATING_BOGUS_MAX;

	libbpf_sha256(gopts.insns, gopts.insns_sz, excl);
	data_sz = gopts.data_sz;
	blob = malloc(data_sz);
	if (!ASSERT_OK_PTR(blob, "blob_alloc"))
		goto free_ctx;
	memcpy(blob, gopts.data, data_sz);

	r = run_gen_loader(gopts.insns, gopts.insns_sz, blob, data_sz,
			   excl, sizeof(excl), NULL, 0, true, ctx, ctx_sz, &ran);
	if (!ASSERT_TRUE(ran, "loader ran") ||
	    !ASSERT_EQ(r, 0, "loader retval"))
		goto free_blob;

	for (i = 0; i < nr_maps; i++) {
		struct bpf_map_info info;
		__u32 ilen = sizeof(info);
		int fd = md[i].map_fd;

		if (fd <= 0)
			continue;
		memset(&info, 0, sizeof(info));
		if (ASSERT_OK(bpf_map_get_info_by_fd(fd, &info, &ilen), "map_info")) {
			ASSERT_NEQ(info.max_entries, GATING_BOGUS_MAX,
				   "ctx max_entries ignored for signed loader");
			checked++;
		}
	}
	ASSERT_GT(checked, 0, "inspected a created map");

free_blob:
	free(blob);
free_ctx:
	close_loader_ctx_fds(ctx, nr_maps, nr_progs);
	free(ctx);
destroy:
	test_signed_loader_map__destroy(skel);
}

/*
 * A signed loader must also ignore ctx-supplied initial_value: the host cannot
 * re-seed a signed program's map contents through the loader ctx. Drive a
 * program with one initialized global (a .data map) through gen_loader, point
 * every map's ctx initial_value at an adversarial buffer, and confirm the
 * created map still holds the attested value, never the ctx bytes.
 */
#define DATA_MAGIC 0x5eed1234abad1deaULL

static void metadata_ctx_initial_value_ignored(void)
{
	LIBBPF_OPTS(gen_loader_opts, gopts, .gen_hash = true);
	struct test_signed_loader_data *skel;
	__u8 excl[SHA256_DIGEST_LENGTH], evil[64];
	int nr_maps = 0, nr_progs = 0, i, found = 0, r;
	struct bpf_program *p;
	struct bpf_map *m;
	struct bpf_map_desc *md;
	unsigned char *blob;
	__u32 ctx_sz, data_sz;
	void *ctx;
	bool ran;

	skel = test_signed_loader_data__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;
	if (!ASSERT_OK(bpf_object__gen_loader(skel->obj, &gopts), "gen_loader"))
		goto destroy;
	if (!ASSERT_OK(bpf_object__load(skel->obj), "gen_load"))
		goto destroy;

	bpf_object__for_each_program(p, skel->obj)
		nr_progs++;
	bpf_object__for_each_map(m, skel->obj)
		nr_maps++;
	ctx_sz = sizeof(struct bpf_loader_ctx) +
		 nr_maps * sizeof(struct bpf_map_desc) +
		 nr_progs * sizeof(struct bpf_prog_desc);
	ctx = calloc(1, ctx_sz);
	if (!ASSERT_OK_PTR(ctx, "ctx_alloc"))
		goto destroy;
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;

	memset(evil, 0xAA, sizeof(evil));
	md = (struct bpf_map_desc *)((char *)ctx + sizeof(struct bpf_loader_ctx));
	for (i = 0; i < nr_maps; i++)
		md[i].initial_value = ptr_to_u64(evil);

	libbpf_sha256(gopts.insns, gopts.insns_sz, excl);
	data_sz = gopts.data_sz;
	blob = malloc(data_sz);
	if (!ASSERT_OK_PTR(blob, "blob_alloc"))
		goto free_ctx;
	memcpy(blob, gopts.data, data_sz);

	r = run_gen_loader(gopts.insns, gopts.insns_sz, blob, data_sz,
			   excl, sizeof(excl), NULL, 0, true, ctx, ctx_sz, &ran);
	if (!ASSERT_TRUE(ran, "loader ran") ||
	    !ASSERT_EQ(r, 0, "loader retval"))
		goto free_blob;

	for (i = 0; i < nr_maps; i++) {
		struct bpf_map_info info;
		__u32 ilen = sizeof(info), key = 0;
		__u8 value[64] = {};
		__u64 got;
		int fd = md[i].map_fd;

		if (fd <= 0)
			continue;
		memset(&info, 0, sizeof(info));
		if (!ASSERT_OK(bpf_map_get_info_by_fd(fd, &info, &ilen), "map_info"))
			continue;
		if (info.value_size <= sizeof(value) &&
		    bpf_map_lookup_elem(fd, &key, value) == 0) {
			memcpy(&got, value, sizeof(got));
			/* attested .data survives; ctx bytes (0xAA..) ignored */
			if (got == DATA_MAGIC)
				found = 1;
			ASSERT_NEQ(got, 0xAAAAAAAAAAAAAAAAULL,
				   "ctx initial_value ignored for signed loader");
		}
	}
	ASSERT_EQ(found, 1, "attested .data value preserved");

free_blob:
	free(blob);
free_ctx:
	close_loader_ctx_fds(ctx, nr_maps, nr_progs);
	free(ctx);
destroy:
	test_signed_loader_data__destroy(skel);
}

/*
 * The load-time signature must authenticate the loader instructions: a valid
 * signature loads, and the very same signature over one-byte-tampered insns is
 * rejected. Uses ./verify_sig_setup.sh + ./sign-file at runtime, like
 * verify_pkcs7_sig, and verifies against the session keyring the key was added
 * to. (signature_enforced/_too_large only cover a malformed signature.)
 */
static void signature_authenticates_insns(void)
{
	LIBBPF_OPTS(gen_loader_opts, gopts, .gen_hash = true);
	char dir_tmpl[] = "/tmp/signed_loaderXXXXXX", *dir;
	struct test_signed_loader *skel = NULL;
	__u8 excl[SHA256_DIGEST_LENGTH], sig[8192];
	__u32 sig_sz = sizeof(sig), insns_sz, data_sz, ctx_sz;
	unsigned char *insns = NULL, *tampered = NULL, *blob = NULL;
	int nr_maps = 0, nr_progs = 0, r;
	struct bpf_program *p;
	struct bpf_map *m;
	void *ctx = NULL;
	bool ran;

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		return;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		return;
	}

	skel = test_signed_loader__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;
	if (!ASSERT_OK(bpf_object__gen_loader(skel->obj, &gopts), "gen_loader"))
		goto cleanup;
	if (!ASSERT_OK(bpf_object__load(skel->obj), "gen_load"))
		goto cleanup;

	bpf_object__for_each_program(p, skel->obj)
		nr_progs++;
	bpf_object__for_each_map(m, skel->obj)
		nr_maps++;
	ctx_sz = sizeof(struct bpf_loader_ctx) +
		 nr_maps * sizeof(struct bpf_map_desc) +
		 nr_progs * sizeof(struct bpf_prog_desc);
	insns_sz = gopts.insns_sz;
	data_sz = gopts.data_sz;
	ctx = calloc(1, ctx_sz);
	insns = malloc(insns_sz);
	tampered = malloc(insns_sz);
	blob = malloc(data_sz);
	if (!ASSERT_OK_PTR(ctx, "ctx") ||
	    !ASSERT_OK_PTR(insns, "insns") ||
	    !ASSERT_OK_PTR(tampered, "tampered") ||
	    !ASSERT_OK_PTR(blob, "blob"))
		goto cleanup;
	memcpy(insns, gopts.insns, insns_sz);
	memcpy(blob, gopts.data, data_sz);
	libbpf_sha256(insns, insns_sz, excl);

	if (!ASSERT_OK(sign_buf(dir, insns, insns_sz, sig, &sig_sz), "sign-file"))
		goto cleanup;

	memset(ctx, 0, ctx_sz);
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;
	r = run_gen_loader(insns, insns_sz, blob, data_sz, excl, sizeof(excl),
			   sig, sig_sz, true, ctx, ctx_sz, &ran);
	ASSERT_TRUE(ran, "valid signature: loader loaded and ran");
	ASSERT_EQ(r, 0, "valid signature accepted");
	close_loader_ctx_fds(ctx, nr_maps, nr_progs);

	memcpy(tampered, insns, insns_sz);
	tampered[insns_sz / 2] ^= 0xff;
	memset(ctx, 0, ctx_sz);
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;
	r = run_gen_loader(tampered, insns_sz, blob, data_sz, excl, sizeof(excl),
			   sig, sig_sz, true, ctx, ctx_sz, &ran);
	ASSERT_FALSE(ran, "tampered loader rejected before run");
	ASSERT_EQ(r, -EKEYREJECTED, "signature is bound to the instructions");
cleanup:
	free(insns);
	free(tampered);
	free(blob);
	free(ctx);
	test_signed_loader__destroy(skel);
	run_setup("cleanup", dir);
}

static int make_excl_map(__u32 flags, __u32 value_size)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	__u8 hash[SHA256_DIGEST_LENGTH] = { 1 };	/* any 32-byte value */

	opts.excl_prog_hash = hash;
	opts.excl_prog_hash_size = sizeof(hash);
	opts.map_flags = flags;
	return bpf_map_create(BPF_MAP_TYPE_ARRAY, "md", 4, value_size, 1, &opts);
}

static void hash_requires_frozen(void)
{
	__u8 hbuf[SHA256_DIGEST_LENGTH], val[64] = {};
	struct bpf_map_info info;
	__u32 ilen, key = 0;
	int fd;

	fd = make_excl_map(0, sizeof(val));
	if (!ASSERT_OK_FD(fd, "excl_map"))
		return;
	ASSERT_OK(bpf_map_update_elem(fd, &key, val, 0), "update");

	memset(&info, 0, sizeof(info));
	info.hash = ptr_to_u64(hbuf);
	info.hash_size = sizeof(hbuf);
	ilen = sizeof(info);
	ASSERT_EQ(bpf_map_get_info_by_fd(fd, &info, &ilen), -EPERM,
		  "hash of unfrozen map rejected");
	close(fd);
}

static void no_update_after_freeze(void)
{
	__u8 val[64] = {};
	__u32 key = 0;
	int fd;

	fd = make_excl_map(0, sizeof(val));
	if (!ASSERT_OK_FD(fd, "excl_map"))
		return;
	ASSERT_OK(bpf_map_update_elem(fd, &key, val, 0), "update");
	ASSERT_OK(bpf_map_freeze(fd), "freeze");
	ASSERT_EQ(bpf_map_update_elem(fd, &key, val, 0), -EPERM,
		  "update after freeze rejected");
	close(fd);
}

static void freeze_writable_mmap(void)
{
	void *w;
	int fd;

	fd = make_excl_map(BPF_F_MMAPABLE, 4096);
	if (!ASSERT_OK_FD(fd, "excl_mmapable_map"))
		return;
	w = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ASSERT_OK_PTR(w, "writable_mmap")) {
		ASSERT_EQ(bpf_map_freeze(fd), -EBUSY,
			  "freeze rejected while writable mmap held");
		munmap(w, 4096);
	}
	close(fd);
}

static void no_writable_mmap_frozen(void)
{
	void *w;
	int fd;

	fd = make_excl_map(BPF_F_MMAPABLE, 4096);
	if (!ASSERT_OK_FD(fd, "excl_mmapable_map"))
		return;
	ASSERT_OK(bpf_map_freeze(fd), "freeze");
	w = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	ASSERT_EQ(w, MAP_FAILED, "writable mmap of frozen map rejected");
	if (w != MAP_FAILED)
		munmap(w, 4096);
	close(fd);
}

static void map_hash_matches_libbpf(void)
{
	__u8 kbuf[SHA256_DIGEST_LENGTH], lbuf[SHA256_DIGEST_LENGTH], val[64] = {};
	struct bpf_map_info info;
	__u32 ilen, key = 0;
	int fd, i;

	/*
	 * The signing scheme assumes the kernel's map hash equals what libbpf
	 * computes over the same bytes (gen_loader bakes libbpf_sha256(blob);
	 * the kernel recomputes via array_map_get_hash). Pin that they agree.
	 */
	for (i = 0; i < (int)sizeof(val); i++)
		val[i] = i * 7 + 1;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "h", 4, sizeof(val), 1, NULL);
	if (!ASSERT_OK_FD(fd, "array_map"))
		return;
	ASSERT_OK(bpf_map_update_elem(fd, &key, val, 0), "update");
	ASSERT_OK(bpf_map_freeze(fd), "freeze");
	memset(&info, 0, sizeof(info));
	info.hash = ptr_to_u64(kbuf);
	info.hash_size = sizeof(kbuf);
	ilen = sizeof(info);
	if (ASSERT_OK(bpf_map_get_info_by_fd(fd, &info, &ilen), "get_hash")) {
		libbpf_sha256(val, sizeof(val), lbuf);
		ASSERT_EQ(memcmp(kbuf, lbuf, sizeof(kbuf)), 0,
			  "kernel map hash matches libbpf_sha256");
	}
	close(fd);
}

static void map_hash_multi_element(void)
{
	const __u32 nr = 8, value_size = 64;
	__u8 kbuf[SHA256_DIGEST_LENGTH], lbuf[SHA256_DIGEST_LENGTH];
	struct bpf_map_info info;
	__u32 ilen, i, j;
	__u8 *full;
	int fd;

	/*
	 * array_map_get_hash() hashes elem_size * max_entries (the whole value
	 * area), not just element 0. With an 8-aligned value_size elem_size has
	 * no padding, so pin that a >1-entry array's kernel hash equals
	 * libbpf_sha256() over the full, concatenated element contents.
	 */
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "h", 4, value_size, nr, NULL);
	if (!ASSERT_OK_FD(fd, "array_map"))
		return;
	full = calloc(nr, value_size);
	if (!ASSERT_OK_PTR(full, "buf"))
		goto close_fd;
	for (i = 0; i < nr; i++) {
		__u8 *v = full + i * value_size;

		for (j = 0; j < value_size; j++)
			v[j] = i * 31 + j * 7 + 1;
		ASSERT_OK(bpf_map_update_elem(fd, &i, v, 0), "update");
	}
	ASSERT_OK(bpf_map_freeze(fd), "freeze");
	memset(&info, 0, sizeof(info));
	info.hash = ptr_to_u64(kbuf);
	info.hash_size = sizeof(kbuf);
	ilen = sizeof(info);
	if (ASSERT_OK(bpf_map_get_info_by_fd(fd, &info, &ilen), "get_hash")) {
		libbpf_sha256(full, (size_t)nr * value_size, lbuf);
		ASSERT_EQ(memcmp(kbuf, lbuf, sizeof(kbuf)), 0,
			  "kernel hash covers full multi-element value area");
	}
	free(full);
close_fd:
	close(fd);
}

static void map_hash_bad_size(void)
{
	__u8 kbuf[SHA256_DIGEST_LENGTH], val[64] = {};
	struct bpf_map_info info;
	__u32 ilen, key = 0;
	int fd;

	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "h", 4, sizeof(val), 1, NULL);
	if (!ASSERT_OK_FD(fd, "array_map"))
		return;
	ASSERT_OK(bpf_map_update_elem(fd, &key, val, 0), "update");
	ASSERT_OK(bpf_map_freeze(fd), "freeze");
	memset(&info, 0, sizeof(info));
	info.hash = ptr_to_u64(kbuf);
	info.hash_size = sizeof(kbuf) / 2;
	ilen = sizeof(info);
	ASSERT_EQ(bpf_map_get_info_by_fd(fd, &info, &ilen), -EINVAL,
		  "wrong hash_size rejected");
	close(fd);
}

static void map_hash_unsupported_type(void)
{
	__u8 kbuf[SHA256_DIGEST_LENGTH];
	struct bpf_map_info info;
	__u32 ilen;
	int fd;

	/* Only arrays implement map_get_hash; a hash map must be refused. */
	fd = bpf_map_create(BPF_MAP_TYPE_HASH, "h", 4, 8, 4, NULL);
	if (!ASSERT_OK_FD(fd, "hash_map"))
		return;
	memset(&info, 0, sizeof(info));
	info.hash = ptr_to_u64(kbuf);
	info.hash_size = sizeof(kbuf);
	ilen = sizeof(info);
	ASSERT_EQ(bpf_map_get_info_by_fd(fd, &info, &ilen), -EINVAL,
		  "hash unsupported for non-array map");
	close(fd);
}

static int setup_meta_map(const struct gen_loader_fixture *f)
{
	LIBBPF_OPTS(bpf_map_create_opts, mopts,
		    .excl_prog_hash = f->excl,
		    .excl_prog_hash_size = sizeof(f->excl));
	__u32 key = 0;
	int fd;

	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "__loader.map", 4,
			    f->data_sz, 1, &mopts);
	if (fd < 0)
		return -errno;
	if (bpf_map_update_elem(fd, &key, f->blob, 0) || bpf_map_freeze(fd)) {
		close(fd);
		return -errno;
	}
	return fd;
}

static void lsm_signature_verdict(void)
{
	char dir_tmpl[] = "/tmp/signed_loader_lsmXXXXXX", *dir = NULL;
	struct test_signed_loader_lsm *lsm = NULL;
	int map_fd = -1, prog_fd = -1;
	bool have_fixture = false;
	struct gen_loader_fixture f;
	__u32 sig_sz = 8192;
	__s32 ses_serial;
	__u8 sig[8192];

	lsm = test_signed_loader_lsm__open_and_load();
	if (!ASSERT_OK_PTR(lsm, "lsm_skel_load"))
		return;
	lsm->bss->monitored_tid = sys_gettid();
	if (!ASSERT_OK(test_signed_loader_lsm__attach(lsm), "lsm_attach"))
		goto out;

	have_fixture = true;
	if (gen_loader_fixture_init(&f) != 0)
		goto out;

	map_fd = setup_meta_map(&f);
	if (!ASSERT_OK_FD(map_fd, "meta_map_unsigned"))
		goto out;
	lsm->bss->seen = 0;
	prog_fd = load_loader(f.gopts.insns, f.gopts.insns_sz, map_fd, NULL, 0, 0);
	close(map_fd);
	map_fd = -1;
	if (!ASSERT_OK_FD(prog_fd, "unsigned loader load"))
		goto out;
	close(prog_fd);
	prog_fd = -1;
	if (!ASSERT_NEQ(lsm->bss->seen, 0, "bpf LSM in the active LSM set"))
		goto out;
	ASSERT_EQ(lsm->bss->seen, 1, "unsigned: one observed load");
	ASSERT_EQ(lsm->bss->sig_verdict, BPF_SIG_UNSIGNED, "unsigned verdict");
	ASSERT_EQ(lsm->bss->sig_keyring_type, BPF_SIG_KEYRING_NONE, "unsigned keyring type");
	ASSERT_EQ(lsm->bss->sig_keyring_serial, 0, "unsigned: no keyring serial");

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		goto out;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		dir = NULL;
		goto out;
	}
	if (!ASSERT_OK(sign_buf(dir, f.gopts.insns, f.gopts.insns_sz, sig,
				&sig_sz), "sign-file"))
		goto out;

	map_fd = setup_meta_map(&f);
	if (!ASSERT_OK_FD(map_fd, "meta_map_signed"))
		goto out;
	lsm->bss->seen = 0;
	prog_fd = load_loader(f.gopts.insns, f.gopts.insns_sz, map_fd, sig,
			      sig_sz, KEY_SPEC_SESSION_KEYRING);
	close(map_fd);
	map_fd = -1;
	if (!ASSERT_OK_FD(prog_fd, "signed loader load"))
		goto out;
	close(prog_fd);
	prog_fd = -1;

	ses_serial = syscall(__NR_keyctl, KEYCTL_GET_KEYRING_ID,
			     KEY_SPEC_SESSION_KEYRING, 0);
	ASSERT_EQ(lsm->bss->seen, 1, "signed: one observed load");
	ASSERT_EQ(lsm->bss->sig_verdict, BPF_SIG_VERIFIED, "signed verdict");
	ASSERT_EQ(lsm->bss->sig_keyring_type, BPF_SIG_KEYRING_USER, "signed keyring type");
	ASSERT_GT(ses_serial, 0, "session keyring serial resolved");
	ASSERT_EQ(lsm->bss->sig_keyring_serial, ses_serial,
		  "signed: validated against session keyring");
out:
	if (map_fd >= 0)
		close(map_fd);
	if (prog_fd >= 0)
		close(prog_fd);
	if (have_fixture)
		gen_loader_fixture_fini(&f);
	if (dir)
		run_setup("cleanup", dir);
	test_signed_loader_lsm__destroy(lsm);
}

void test_signed_loader(void)
{
	if (test__start_subtest("metadata_check_shape"))
		metadata_check_shape();
	if (test__start_subtest("metadata_match"))
		metadata_match();
	if (test__start_subtest("metadata_sha_mismatch"))
		metadata_sha_mismatch();
	if (test__start_subtest("metadata_not_exclusive"))
		metadata_not_exclusive();
	if (test__start_subtest("metadata_hash_not_computed"))
		metadata_hash_not_computed();
	if (test__start_subtest("signature_enforced"))
		signature_enforced();
	if (test__start_subtest("signature_too_large"))
		signature_too_large();
	if (test__start_subtest("signature_bad_keyring"))
		signature_bad_keyring();
	if (test__start_subtest("metadata_ctx_max_entries_ignored"))
		metadata_ctx_max_entries_ignored();
	if (test__start_subtest("metadata_ctx_initial_value_ignored"))
		metadata_ctx_initial_value_ignored();
	if (test__start_subtest("signature_authenticates_insns"))
		signature_authenticates_insns();
	if (test__start_subtest("hash_requires_frozen"))
		hash_requires_frozen();
	if (test__start_subtest("no_update_after_freeze"))
		no_update_after_freeze();
	if (test__start_subtest("freeze_writable_mmap"))
		freeze_writable_mmap();
	if (test__start_subtest("no_writable_mmap_frozen"))
		no_writable_mmap_frozen();
	if (test__start_subtest("map_hash_matches_libbpf"))
		map_hash_matches_libbpf();
	if (test__start_subtest("map_hash_multi_element"))
		map_hash_multi_element();
	if (test__start_subtest("map_hash_bad_size"))
		map_hash_bad_size();
	if (test__start_subtest("map_hash_unsupported_type"))
		map_hash_unsupported_type();
	if (test__start_subtest("lsm_signature_verdict"))
		lsm_signature_verdict();
}
