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

#include <bpf/btf.h>

#include "bpf/libbpf_internal.h" /* for libbpf_sha256() */
#include "bpf/skel_internal.h"	 /* for loader ctx layout (bpf_loader_ctx etc) */

#include "test_signed_loader.skel.h"
#include "test_signed_loader_map.skel.h"
#include "test_signed_loader_data.skel.h"
#include "test_signed_loader_lsm.skel.h"

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
		       const void *sig, __u32 sig_sz, __s32 keyring_id,
		       __u32 fd_array_cnt)
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
	attr.fd_array_cnt = fd_array_cnt;
	memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));
	fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
		     offsetofend(union bpf_attr, keyring_id));
	return fd < 0 ? -errno : fd;
}

static int run_gen_loader(const void *insns, __u32 insns_sz,
			  const void *data, __u32 data_sz,
			  const void *excl, __u32 excl_sz,
			  const void *sig, __u32 sig_sz,
			  void *ctx, __u32 ctx_sz, bool *loader_ran)
{
	LIBBPF_OPTS(bpf_map_create_opts, mopts,
		    .excl_prog_hash = excl,
		    .excl_prog_hash_size = excl_sz);
	__u32 key = 0;
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
		attr.fd_array_cnt = 1;
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

static void metadata_match(void)
{
	struct gen_loader_fixture f;
	bool ran;
	int r;

	if (gen_loader_fixture_init(&f) == 0) {
		r = run_gen_loader(f.gopts.insns, f.gopts.insns_sz, f.blob,
				   f.data_sz, f.excl, sizeof(f.excl), NULL, 0,
				   f.ctx, f.ctx_sz, &ran);
		ASSERT_TRUE(ran, "loader ran");
		ASSERT_EQ(r, 0, "honest loader retval");
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
		 * the signed lskels.) Pin -EBADMSG, the PKCS#7 parse failure:
		 * a looser fd < 0 check could also be satisfied by the sparse
		 * fd_array rejection (-EACCES) that the loader's map reference
		 * would trip even if the signature were silently ignored.
		 */
		fd = load_loader(f.gopts.insns, f.gopts.insns_sz, -1, junk,
				 sizeof(junk), KEY_SPEC_SESSION_KEYRING, 0);
		ASSERT_EQ(fd, -EBADMSG, "invalid signature rejected at load");
		if (fd >= 0)
			close(fd);
	}
	gen_loader_fixture_fini(&f);
}

static void signed_nonexcl_fd_array_rejected(void)
{
	static const __u8 junk[64] = { 0x30, 0x42, 0x13, 0x37, };
	struct gen_loader_fixture f;
	int map_fd, fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * A signed program may only bind exclusive maps through fd_array
		 * (their contents are folded into the signature). Binding a
		 * non-exclusive map is rejected, before the signature is even
		 * examined.
		 */
		map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "nonexcl", 4,
					f.data_sz, 1, NULL);
		if (ASSERT_OK_FD(map_fd, "nonexcl_map")) {
			if (ASSERT_OK(bpf_map_freeze(map_fd), "freeze")) {
				fd = load_loader(f.gopts.insns, f.gopts.insns_sz,
						 map_fd, junk, sizeof(junk),
						 KEY_SPEC_SESSION_KEYRING, 1);
				ASSERT_EQ(fd, -EPERM,
					  "non-exclusive map in signed fd_array rejected");
				if (fd >= 0)
					close(fd);
			}
			close(map_fd);
		}
	}
	gen_loader_fixture_fini(&f);
}

static void signed_unfrozen_fd_array_rejected(void)
{
	static const __u8 junk[64] = { 0x30, 0x42, 0x13, 0x37, };
	LIBBPF_OPTS(bpf_map_create_opts, mopts);
	struct gen_loader_fixture f;
	__u32 key = 0;
	int map_fd, fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * The metadata map must be frozen before a signed load so the
		 * folded bytes cannot change afterwards. Bind an exclusive map
		 * with matching contents but skip the freeze: the load must be
		 * rejected by the frozen check with -EPERM. The exclusivity
		 * check right after it would pass, so the errno uniquely pins
		 * the freeze requirement.
		 */
		mopts.excl_prog_hash = f.excl;
		mopts.excl_prog_hash_size = sizeof(f.excl);
		map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "unfrozen", 4,
					f.data_sz, 1, &mopts);
		if (ASSERT_OK_FD(map_fd, "unfrozen_map")) {
			if (ASSERT_OK(bpf_map_update_elem(map_fd, &key, f.blob, 0),
				      "update")) {
				fd = load_loader(f.gopts.insns, f.gopts.insns_sz,
						 map_fd, junk, sizeof(junk),
						 KEY_SPEC_SESSION_KEYRING, 1);
				ASSERT_EQ(fd, -EPERM,
					  "unfrozen map in signed fd_array rejected");
				if (fd >= 0)
					close(fd);
			}
			close(map_fd);
		}
	}
	gen_loader_fixture_fini(&f);
}

static void signed_nonarray_fd_array_rejected(void)
{
	static const __u8 junk[64] = { 0x30, 0x42, 0x13, 0x37, };
	LIBBPF_OPTS(bpf_map_create_opts, mopts);
	struct gen_loader_fixture f;
	int map_fd, fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * Only a plain BPF_MAP_TYPE_ARRAY may be folded into the
		 * signature. An exclusive map of any other type is rejected
		 * (-EINVAL) rather than folded - this is the type gate that
		 * keeps arena maps (map_direct_value_addr() returns a user
		 * address) and insn-array maps (buffer smaller than value_size)
		 * out of the hashed region, where the old code would have
		 * memcpy()'d from them. A hash map stands in here: it is
		 * exclusive (bound to the loader digest) but not an array.
		 */
		mopts.excl_prog_hash = f.excl;
		mopts.excl_prog_hash_size = sizeof(f.excl);
		map_fd = bpf_map_create(BPF_MAP_TYPE_HASH, "excl_hash", 4, 4, 1,
					&mopts);
		if (ASSERT_OK_FD(map_fd, "excl_hash_map")) {
			fd = load_loader(f.gopts.insns, f.gopts.insns_sz, map_fd,
					 junk, sizeof(junk),
					 KEY_SPEC_SESSION_KEYRING, 1);
			ASSERT_EQ(fd, -EINVAL,
				  "non-array map in signed fd_array rejected");
			if (fd >= 0)
				close(fd);
			close(map_fd);
		}
	}
	gen_loader_fixture_fini(&f);
}

static int setup_meta_map(const struct gen_loader_fixture *f);

static void signed_btf_fd_array_rejected(void)
{
	char dir_tmpl[] = "/tmp/signed_loader_btfXXXXXX", *dir = NULL;
	__u32 sig_sz = 8192;
	int map_fd = -1, prog_fd = -1;
	unsigned char *buf = NULL;
	struct gen_loader_fixture f;
	bool have_fixture = false;
	struct btf *btf = NULL;
	union bpf_attr attr;
	int fds[2];
	__u8 sig[8192];

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		return;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		return;
	}
	have_fixture = true;
	if (gen_loader_fixture_init(&f) != 0)
		goto out;

	/*
	 * fd_array binds maps and BTFs alike, but only exclusive array maps are
	 * folded into the signature. Build an otherwise genuinely signed load -
	 * insns || metadata, exclusive frozen map at fd_array[0] - then smuggle
	 * an extra BTF into fd_array[1]. A signed program may not bind any BTF,
	 * so resolving the fd_array entries rejects the BTF with -EACCES (in
	 * __add_used_btf(), before the signature is even verified).
	 */
	buf = malloc((size_t)f.gopts.insns_sz + f.data_sz);
	if (!ASSERT_OK_PTR(buf, "signbuf"))
		goto out;
	memcpy(buf, f.gopts.insns, f.gopts.insns_sz);
	memcpy(buf + f.gopts.insns_sz, f.blob, f.data_sz);
	if (!ASSERT_OK(sign_buf(dir, buf, f.gopts.insns_sz + f.data_sz, sig,
			       &sig_sz), "sign insns||metadata"))
		goto out;

	map_fd = setup_meta_map(&f);
	if (!ASSERT_OK_FD(map_fd, "meta_map"))
		goto out;
	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf_new_empty"))
		goto out;
	btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf_load"))
		goto out;

	fds[0] = map_fd;
	fds[1] = btf__fd(btf);
	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(f.gopts.insns);
	attr.insn_cnt = f.gopts.insns_sz / sizeof(struct bpf_insn);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.fd_array = ptr_to_u64(fds);
	attr.fd_array_cnt = 2;
	attr.signature = ptr_to_u64(sig);
	attr.signature_size = sig_sz;
	attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));
	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	ASSERT_EQ(prog_fd < 0 ? -errno : prog_fd, -EACCES,
		  "BTF in signed fd_array rejected");
	if (prog_fd >= 0)
		close(prog_fd);
out:
	if (btf)
		btf__free(btf);
	if (map_fd >= 0)
		close(map_fd);
	if (have_fixture)
		gen_loader_fixture_fini(&f);
	if (dir)
		run_setup("cleanup", dir);
	free(buf);
}

static void signature_failure_logs(void)
{
	static const __u8 junk[64] = { 0x30, 0x42, 0x13, 0x37, };
	char log_buf[1024] = {};
	struct gen_loader_fixture f;
	union bpf_attr attr;
	int fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * Signature verification now runs inside bpf_check(), so a
		 * failure is reported through the verifier log. A present-but-
		 * invalid signature is rejected and the log says why.
		 */
		memset(&attr, 0, sizeof(attr));
		attr.prog_type = BPF_PROG_TYPE_SYSCALL;
		attr.insns = ptr_to_u64(f.gopts.insns);
		attr.insn_cnt = f.gopts.insns_sz / sizeof(struct bpf_insn);
		attr.license = ptr_to_u64("Dual BSD/GPL");
		attr.prog_flags = BPF_F_SLEEPABLE;
		attr.signature = ptr_to_u64(junk);
		attr.signature_size = sizeof(junk);
		attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
		attr.log_level = 1;
		attr.log_buf = ptr_to_u64(log_buf);
		attr.log_size = sizeof(log_buf);
		memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));

		fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			     offsetofend(union bpf_attr, keyring_id));
		ASSERT_LT(fd, 0, "invalid signature rejected at load");
		if (fd >= 0)
			close(fd);
		ASSERT_HAS_SUBSTR(log_buf, "signature verification failed",
				  "verifier logs signature failure");
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
				 64 << 20, KEY_SPEC_SESSION_KEYRING, 0);
		ASSERT_EQ(fd, -EINVAL, "oversized signature rejected");
		if (fd >= 0)
			close(fd);
	}
	gen_loader_fixture_fini(&f);
}

static void signature_zero_size(void)
{
	static const __u8 junk[64] = {};
	struct gen_loader_fixture f;
	int fd;

	if (gen_loader_fixture_init(&f) == 0) {
		/*
		 * A present signature with signature_size == 0 is rejected
		 * up front, before the keyring is resolved or the signature
		 * buffer is read.
		 */
		fd = load_loader(f.gopts.insns, f.gopts.insns_sz, -1, junk,
				 0, KEY_SPEC_SESSION_KEYRING, 0);
		ASSERT_EQ(fd, -EINVAL, "zero-size signature rejected");
		if (fd >= 0)
			close(fd);
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
				 sizeof(junk), INT_MAX, 0);
		ASSERT_EQ(fd, -EINVAL, "signature with bad keyring_id rejected");
		if (fd >= 0)
			close(fd);
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
			   excl, sizeof(excl), NULL, 0, ctx, ctx_sz, &ran);
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
			   excl, sizeof(excl), NULL, 0, ctx, ctx_sz, &ran);
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
	unsigned char *signbuf = NULL;
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

	signbuf = malloc((size_t)insns_sz + data_sz);
	if (!ASSERT_OK_PTR(signbuf, "signbuf"))
		goto cleanup;
	memcpy(signbuf, insns, insns_sz);
	memcpy(signbuf + insns_sz, blob, data_sz);
	if (!ASSERT_OK(sign_buf(dir, signbuf, insns_sz + data_sz, sig, &sig_sz),
		       "sign-file"))
		goto cleanup;

	memset(ctx, 0, ctx_sz);
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;
	r = run_gen_loader(insns, insns_sz, blob, data_sz, excl, sizeof(excl),
			   sig, sig_sz, ctx, ctx_sz, &ran);
	ASSERT_TRUE(ran, "valid signature: loader loaded and ran");
	ASSERT_EQ(r, 0, "valid signature accepted");
	close_loader_ctx_fds(ctx, nr_maps, nr_progs);

	memcpy(tampered, insns, insns_sz);
	tampered[insns_sz / 2] ^= 0xff;
	/*
	 * Bind the metadata map to the tampered loader's own digest, so the
	 * verifier's exclusive-map check (excl_prog_sha == prog->digest) passes
	 * and the signature - verified after the maps are resolved - is what
	 * rejects the load. This is the attacker's best case: even after
	 * re-binding the exclusive map to their tampered loader, the signature
	 * over the original insns || metadata still fails. (Leaving the map
	 * bound to the original digest would instead trip the excl check first.)
	 */
	libbpf_sha256(tampered, insns_sz, excl);
	memset(ctx, 0, ctx_sz);
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;
	r = run_gen_loader(tampered, insns_sz, blob, data_sz, excl, sizeof(excl),
			   sig, sig_sz, ctx, ctx_sz, &ran);
	ASSERT_FALSE(ran, "tampered loader rejected before run");
	ASSERT_EQ(r, -EKEYREJECTED, "signature is bound to the instructions");
cleanup:
	free(insns);
	free(tampered);
	free(blob);
	free(signbuf);
	free(ctx);
	test_signed_loader__destroy(skel);
	run_setup("cleanup", dir);
}

static void signature_authenticates_metadata(void)
{
	LIBBPF_OPTS(gen_loader_opts, gopts, .gen_hash = true);
	char dir_tmpl[] = "/tmp/signed_loaderXXXXXX", *dir;
	struct test_signed_loader *skel = NULL;
	__u8 excl[SHA256_DIGEST_LENGTH], sig[8192];
	__u32 sig_sz = sizeof(sig), insns_sz, data_sz, ctx_sz;
	unsigned char *insns = NULL, *blob = NULL;
	unsigned char *signbuf = NULL;
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
	blob = malloc(data_sz);
	if (!ASSERT_OK_PTR(ctx, "ctx") ||
	    !ASSERT_OK_PTR(insns, "insns") ||
	    !ASSERT_OK_PTR(blob, "blob"))
		goto cleanup;
	memcpy(insns, gopts.insns, insns_sz);
	memcpy(blob, gopts.data, data_sz);
	libbpf_sha256(insns, insns_sz, excl);

	signbuf = malloc((size_t)insns_sz + data_sz);
	if (!ASSERT_OK_PTR(signbuf, "signbuf"))
		goto cleanup;
	memcpy(signbuf, insns, insns_sz);
	memcpy(signbuf + insns_sz, blob, data_sz);
	if (!ASSERT_OK(sign_buf(dir, signbuf, insns_sz + data_sz, sig, &sig_sz),
		       "sign-file"))
		goto cleanup;

	memset(ctx, 0, ctx_sz);
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;
	r = run_gen_loader(insns, insns_sz, blob, data_sz, excl, sizeof(excl),
			   sig, sig_sz, ctx, ctx_sz, &ran);
	ASSERT_TRUE(ran, "valid signature: loader loaded and ran");
	ASSERT_EQ(r, 0, "valid signature accepted");
	close_loader_ctx_fds(ctx, nr_maps, nr_progs);

	/*
	 * Tamper the metadata after signing while leaving the instructions
	 * and thus the exclusive hash binding untouched: the map freezes
	 * fine and excl_prog_sha still matches the loader's digest, so the
	 * load reaches signature verification, which folds the live frozen
	 * map bytes into the checked payload and must reject the modified
	 * blob. A kernel folding anything but the map contents themselves
	 * would wrongly accept this load.
	 */
	blob[data_sz / 2] ^= 0xff;
	memset(ctx, 0, ctx_sz);
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;
	r = run_gen_loader(insns, insns_sz, blob, data_sz, excl, sizeof(excl),
			   sig, sig_sz, ctx, ctx_sz, &ran);
	ASSERT_FALSE(ran, "tampered metadata rejected before run");
	ASSERT_EQ(r, -EKEYREJECTED, "signature is bound to the metadata");
cleanup:
	free(insns);
	free(blob);
	free(signbuf);
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
	__u32 sig_sz = 8192, msig_sz = 8192;
	int map_fd = -1, prog_fd = -1;
	bool have_fixture = false;
	struct gen_loader_fixture f;
	unsigned char *buf;
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
	prog_fd = load_loader(f.gopts.insns, f.gopts.insns_sz, map_fd, NULL, 0, 0, 0);
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
			      sig_sz, KEY_SPEC_SESSION_KEYRING, 0);
	close(map_fd);
	map_fd = -1;
	ASSERT_EQ(prog_fd, -EACCES, "unfolded metadata rejected");
	if (prog_fd >= 0)
		close(prog_fd);
	prog_fd = -1;

	ses_serial = syscall(__NR_keyctl, KEYCTL_GET_KEYRING_ID,
			     KEY_SPEC_SESSION_KEYRING, 0);
	ASSERT_EQ(lsm->bss->seen, 1, "signed: one observed load");
	ASSERT_EQ(lsm->bss->sig_verdict, BPF_SIG_VERIFIED,
		  "admission saw a valid signature");
	ASSERT_EQ(lsm->bss->sig_keyring_type, BPF_SIG_KEYRING_USER, "signed keyring type");
	ASSERT_GT(ses_serial, 0, "session keyring serial resolved");
	ASSERT_EQ(lsm->bss->sig_keyring_serial, ses_serial,
		  "signed: validated against session keyring");

	buf = malloc((size_t)f.gopts.insns_sz + f.data_sz);
	if (!ASSERT_OK_PTR(buf, "meta_signbuf"))
		goto out;
	memcpy(buf, f.gopts.insns, f.gopts.insns_sz);
	memcpy(buf + f.gopts.insns_sz, f.blob, f.data_sz);
	if (!ASSERT_OK(sign_buf(dir, buf, f.gopts.insns_sz + f.data_sz,
				sig, &msig_sz), "sign insns||metadata")) {
		free(buf);
		goto out;
	}
	free(buf);

	map_fd = setup_meta_map(&f);
	if (!ASSERT_OK_FD(map_fd, "meta_map_bound"))
		goto out;
	lsm->bss->seen = 0;
	prog_fd = load_loader(f.gopts.insns, f.gopts.insns_sz, map_fd, sig,
			      msig_sz, KEY_SPEC_SESSION_KEYRING, 1);
	close(map_fd);
	map_fd = -1;
	if (!ASSERT_OK_FD(prog_fd, "metadata-bound loader load"))
		goto out;
	close(prog_fd);
	prog_fd = -1;
	ASSERT_EQ(lsm->bss->seen, 1, "metadata: one observed load");
	ASSERT_EQ(lsm->bss->sig_verdict, BPF_SIG_VERIFIED,
		  "metadata-bound verdict");
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

/*
 * Load-time metadata verification: the kernel folds the frozen metadata map
 * into the signature (insns || metadata) and checks it at BPF_PROG_LOAD via
 * fd_array_cnt, rather than the loader checking from within BPF. Sign that
 * concatenation, hand the kernel the map, and confirm the signed loader loads,
 * runs, and installs its target.
 */
static int loadtime_drive(const char *dir, const void *insns, __u32 insns_sz,
			  const void *data, __u32 data_sz, const __u8 *excl,
			  void *ctx, __u32 ctx_sz, int *load_ret, bool *ran)
{
	LIBBPF_OPTS(bpf_map_create_opts, mopts,
		    .excl_prog_hash = excl,
		    .excl_prog_hash_size = SHA256_DIGEST_LENGTH);
	__u32 sig_sz = 8192, key = 0;
	unsigned char *buf = NULL;
	int map_fd, prog_fd, ret = 0;
	union bpf_attr attr;
	__u8 sig[8192];

	*ran = false;
	*load_ret = 0;

	/*
	 * Metadata map, bound to the loader digest and frozen, exactly as
	 * skel_internal.h's bpf_load_and_run() sets it up.
	 */
	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "__loader.map", 4,
				data_sz, 1, &mopts);
	if (map_fd < 0) {
		ret = -errno;
		goto out_load;
	}
	if (bpf_map_update_elem(map_fd, &key, data, 0) || bpf_map_freeze(map_fd)) {
		ret = -errno;
		goto out_load;
	}

	/* Sign insns || metadata, the same bytes the kernel reconstructs. */
	buf = malloc((size_t)insns_sz + data_sz);
	if (!buf) {
		ret = -ENOMEM;
		goto out_load;
	}
	memcpy(buf, insns, insns_sz);
	memcpy(buf + insns_sz, data, data_sz);
	ret = sign_buf(dir, buf, insns_sz + data_sz, sig, &sig_sz);
	if (ret)
		goto out_load;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = insns_sz / sizeof(struct bpf_insn);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.fd_array = ptr_to_u64(&map_fd);
	attr.signature = ptr_to_u64(sig);
	attr.signature_size = sig_sz;
	attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	attr.fd_array_cnt = 1;
	memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));
	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	if (prog_fd < 0) {
		ret = -errno;
		goto out_load;
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
	*ran = true;
	ret = (int)attr.test.retval;
out_prog:
	close(prog_fd);
	goto out_map;
out_load:
	*load_ret = ret;
out_map:
	free(buf);
	if (map_fd >= 0)
		close(map_fd);
	return ret;
}

static void loadtime_verify(struct bpf_object *obj, int expect_maps)
{
	LIBBPF_OPTS(gen_loader_opts, gopts, .gen_hash = true);
	char dir_tmpl[] = "/tmp/signed_loader_ltXXXXXX", *dir = NULL;
	int nr_maps = 0, nr_progs = 0, load_ret = 0, r;
	__u8 excl[SHA256_DIGEST_LENGTH];
	struct bpf_prog_desc *pd;
	struct bpf_map_desc *md;
	unsigned char *blob = NULL;
	struct bpf_program *p;
	struct bpf_map *m;
	__u32 ctx_sz, data_sz;
	void *ctx = NULL;
	bool ran = false;

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		return;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		return;
	}

	if (!ASSERT_OK(bpf_object__gen_loader(obj, &gopts), "gen_loader"))
		goto out;
	if (!ASSERT_OK(bpf_object__load(obj), "gen_load"))
		goto out;

	bpf_object__for_each_program(p, obj)
		nr_progs++;
	bpf_object__for_each_map(m, obj)
		nr_maps++;
	if (!ASSERT_EQ(nr_maps, expect_maps, "fixture map count"))
		goto out;

	ctx_sz = sizeof(struct bpf_loader_ctx) +
		 nr_maps * sizeof(struct bpf_map_desc) +
		 nr_progs * sizeof(struct bpf_prog_desc);
	ctx = calloc(1, ctx_sz);
	if (!ASSERT_OK_PTR(ctx, "ctx_alloc"))
		goto out;
	((struct bpf_loader_ctx *)ctx)->sz = ctx_sz;

	data_sz = gopts.data_sz;
	blob = malloc(data_sz);
	if (!ASSERT_OK_PTR(blob, "blob_alloc"))
		goto out;
	memcpy(blob, gopts.data, data_sz);

	/* excl_prog_hash = SHA256(loader insns) == the loader's prog->digest. */
	libbpf_sha256(gopts.insns, gopts.insns_sz, excl);

	r = loadtime_drive(dir, gopts.insns, gopts.insns_sz, blob, data_sz,
			   excl, ctx, ctx_sz, &load_ret, &ran);
	ASSERT_OK(load_ret, "signed loader loaded (insns || metadata)");
	ASSERT_TRUE(ran, "loader ran");
	ASSERT_EQ(r, 0, "loader installed its target");

	md = (struct bpf_map_desc *)((char *)ctx + sizeof(struct bpf_loader_ctx));
	pd = (struct bpf_prog_desc *)(md + nr_maps);
	ASSERT_GT(pd[0].prog_fd, 0, "target program installed");
	if (nr_maps)
		ASSERT_GT(md[0].map_fd, 0, "target map installed");

	close_loader_ctx_fds(ctx, nr_maps, nr_progs);
out:
	free(blob);
	free(ctx);
	if (dir)
		run_setup("cleanup", dir);
}

static void loadtime_no_map(void)
{
	struct test_signed_loader *skel = test_signed_loader__open();

	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;
	loadtime_verify(skel->obj, 0);
	test_signed_loader__destroy(skel);
}

static void loadtime_with_map(void)
{
	struct test_signed_loader_map *skel = test_signed_loader_map__open();

	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;
	loadtime_verify(skel->obj, 1);
	test_signed_loader_map__destroy(skel);
}

/*
 * A signed program need not bind any map. A plain BPF_PROG_TYPE_SYSCALL
 * program with no fd_array is signed over its instructions alone: the kernel
 * verifies the signature, folds no metadata, and the program loads. Exercise
 * the fd_array == NULL / fd_array_cnt == 0 path, and confirm the signature
 * still authenticates the instructions (a tampered copy is rejected).
 */
static void signed_no_fd_array(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char dir_tmpl[] = "/tmp/signed_loaderXXXXXX", *dir;
	__u32 sig_sz = 8192;
	union bpf_attr attr;
	__u8 sig[8192];
	int prog_fd, err;

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		return;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		return;
	}

	/* No metadata map: the signed payload is the instructions alone. */
	if (!ASSERT_OK(sign_buf(dir, insns, sizeof(insns), sig, &sig_sz),
		       "sign-file"))
		goto cleanup;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = ARRAY_SIZE(insns);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.signature = ptr_to_u64(sig);
	attr.signature_size = sig_sz;
	attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	/* fd_array and fd_array_cnt deliberately left NULL/0. */
	memcpy(attr.prog_name, "signed_nomap", sizeof("signed_nomap"));

	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	if (!ASSERT_GE(prog_fd, 0, "map-less signed program loaded")) {
		if (prog_fd >= 0)
			close(prog_fd);
		goto cleanup;
	}
	close(prog_fd);

	/* The signature covers the instructions, so tampering must be rejected. */
	insns[0].imm = 1;
	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	err = prog_fd < 0 ? -errno : prog_fd;
	ASSERT_EQ(err, -EKEYREJECTED, "tampered map-less program rejected");
	if (prog_fd >= 0)
		close(prog_fd);
cleanup:
	run_setup("cleanup", dir);
}

/*
 * A signed program may reach maps only through fd_array indices, so the kernel
 * folds (and thus attests) them. A direct BPF_PSEUDO_MAP_FD reference - a raw,
 * unfolded fd baked into the signed instructions - is rejected by the verifier.
 */
static void signed_map_by_fd_rejected(void)
{
	struct bpf_insn insns[] = {
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char dir_tmpl[] = "/tmp/signed_loaderXXXXXX", *dir;
	__u32 sig_sz = 8192;
	union bpf_attr attr;
	__u8 sig[8192];
	int map_fd, prog_fd, err;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "sig_mapfd", 4, 4, 1, NULL);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;
	insns[0].imm = map_fd;	/* bake the raw map fd into the ld_imm64 */

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		goto out_map;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		goto out_map;
	}

	/* Sign the instructions, raw map fd and all. */
	if (!ASSERT_OK(sign_buf(dir, insns, sizeof(insns), sig, &sig_sz),
		       "sign-file"))
		goto cleanup;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = ARRAY_SIZE(insns);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.signature = ptr_to_u64(sig);
	attr.signature_size = sig_sz;
	attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	/* No fd_array: the map is reached by a raw fd in the instructions. */
	memcpy(attr.prog_name, "signed_mapfd", sizeof("signed_mapfd"));

	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	err = prog_fd < 0 ? -errno : prog_fd;
	ASSERT_EQ(err, -EINVAL, "signed program referencing a map by fd rejected");
	if (prog_fd >= 0)
		close(prog_fd);
cleanup:
	run_setup("cleanup", dir);
out_map:
	close(map_fd);
}

/*
 * A signed program may reach maps only through the continuous fd_array, so the
 * kernel folds (and thus attests) them. Referencing a map by fd_array *index*
 * while leaving fd_array_cnt at 0 selects the sparse path, which resolves a map
 * the signature never covered; the verifier rejects it up front with -EACCES.
 */
static void signed_sparse_fd_array_rejected(void)
{
	struct bpf_insn insns[] = {
		BPF_LD_IMM64_RAW(BPF_REG_1, BPF_PSEUDO_MAP_IDX, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char dir_tmpl[] = "/tmp/signed_loader_spXXXXXX", *dir;
	__u32 sig_sz = 8192;
	union bpf_attr attr;
	__u8 sig[8192];
	int map_fd, prog_fd, err;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "sig_sparse", 4, 4, 1, NULL);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		goto out_map;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		goto out_map;
	}

	/* Sign the instructions alone; the sparse map is not folded. */
	if (!ASSERT_OK(sign_buf(dir, insns, sizeof(insns), sig, &sig_sz),
		       "sign-file"))
		goto cleanup;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = ARRAY_SIZE(insns);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.fd_array = ptr_to_u64(&map_fd);
	attr.fd_array_cnt = 0; /* sparse: force lazy map resolution */
	attr.signature = ptr_to_u64(sig);
	attr.signature_size = sig_sz;
	attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	memcpy(attr.prog_name, "signed_sparse", sizeof("signed_sparse"));

	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	err = prog_fd < 0 ? -errno : prog_fd;
	ASSERT_EQ(err, -EACCES, "signed program binding a sparse fd_array map rejected");
	if (prog_fd >= 0)
		close(prog_fd);
cleanup:
	run_setup("cleanup", dir);
out_map:
	close(map_fd);
}

static void signed_module_kfunc_rejected(void)
{
	struct bpf_insn insns[] = {
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_KFUNC_CALL, 1, 1),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char dir_tmpl[] = "/tmp/signed_loader_kfnXXXXXX", *dir;
	int prog_fd, err, fds[2];
	struct btf *btf = NULL;
	__u32 sig_sz = 8192;
	union bpf_attr attr;
	__u8 sig[8192];

	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);
	dir = mkdtemp(dir_tmpl);
	if (!ASSERT_OK_PTR(dir, "mkdtemp"))
		return;
	if (!ASSERT_OK(run_setup("setup", dir), "verify_sig_setup")) {
		rmdir(dir);
		return;
	}
	if (!ASSERT_OK(sign_buf(dir, insns, sizeof(insns), sig, &sig_sz),
		       "sign-file"))
		goto cleanup;
	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf_new_empty"))
		goto cleanup;
	btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf_load"))
		goto cleanup;
	fds[0] = -1;
	fds[1] = btf__fd(btf);

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = ARRAY_SIZE(insns);
	attr.license = ptr_to_u64("Dual BSD/GPL");
	attr.prog_flags = BPF_F_SLEEPABLE;
	attr.fd_array = ptr_to_u64(fds);
	attr.fd_array_cnt = 0; /* sparse: force lazy kfunc BTF resolution */
	attr.signature = ptr_to_u64(sig);
	attr.signature_size = sig_sz;
	attr.keyring_id = KEY_SPEC_SESSION_KEYRING;
	memcpy(attr.prog_name, "signed_kfunc", sizeof("signed_kfunc"));

	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr,
			  offsetofend(union bpf_attr, keyring_id));
	err = prog_fd < 0 ? -errno : prog_fd;
	if (prog_fd >= 0)
		close(prog_fd);

	ASSERT_EQ(err, -EACCES, "module kfunc BTF in signed program rejected");
cleanup:
	if (btf)
		btf__free(btf);
	run_setup("cleanup", dir);
}

void test_signed_loader(void)
{
	if (test__start_subtest("loadtime_no_map"))
		loadtime_no_map();
	if (test__start_subtest("loadtime_with_map"))
		loadtime_with_map();
	if (test__start_subtest("metadata_match"))
		metadata_match();
	if (test__start_subtest("signature_enforced"))
		signature_enforced();
	if (test__start_subtest("signed_nonexcl_fd_array_rejected"))
		signed_nonexcl_fd_array_rejected();
	if (test__start_subtest("signed_unfrozen_fd_array_rejected"))
		signed_unfrozen_fd_array_rejected();
	if (test__start_subtest("signed_nonarray_fd_array_rejected"))
		signed_nonarray_fd_array_rejected();
	if (test__start_subtest("signed_btf_fd_array_rejected"))
		signed_btf_fd_array_rejected();
	if (test__start_subtest("signed_module_kfunc_rejected"))
		signed_module_kfunc_rejected();
	if (test__start_subtest("signature_failure_logs"))
		signature_failure_logs();
	if (test__start_subtest("signature_too_large"))
		signature_too_large();
	if (test__start_subtest("signature_zero_size"))
		signature_zero_size();
	if (test__start_subtest("signature_bad_keyring"))
		signature_bad_keyring();
	if (test__start_subtest("metadata_ctx_max_entries_ignored"))
		metadata_ctx_max_entries_ignored();
	if (test__start_subtest("metadata_ctx_initial_value_ignored"))
		metadata_ctx_initial_value_ignored();
	if (test__start_subtest("signature_authenticates_insns"))
		signature_authenticates_insns();
	if (test__start_subtest("signature_authenticates_metadata"))
		signature_authenticates_metadata();
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
	if (test__start_subtest("signed_no_fd_array"))
		signed_no_fd_array();
	if (test__start_subtest("signed_map_by_fd_rejected"))
		signed_map_by_fd_rejected();
	if (test__start_subtest("signed_sparse_fd_array_rejected"))
		signed_sparse_fd_array_rejected();
}
