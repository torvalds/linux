// SPDX-License-Identifier: GPL-2.0
/*
 * nix_origin.bpf.c - $ORIGIN-relative PT_INTERP resolution
 *
 * A binfmt_misc_ops handler that makes relocatable (Nix-style) ELF
 * binaries work: if PT_INTERP starts with "$ORIGIN/", the loader is
 * resolved relative to the directory of the binary being executed and
 * selected via bpf_binprm_set_interp(). The match program reads the
 * program headers itself, so anything else never commits to this
 * handler and passes through untouched.
 *
 * Activate with:
 *   bpftool struct_ops register nix_origin.bpf.o /sys/fs/bpf
 *   echo ':nix-origin:B::::nix_origin:' > /proc/sys/fs/binfmt_misc/register
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define PATH_MAX	4096
#define EI_CLASS	4
#define ELFCLASSXX	2	/* ELFCLASS64; flip to 1 for 32-bit */
#define PT_INTERP	3
#define MAX_PHDRS	64

#define ORIGIN		"$ORIGIN"
#define ORIGIN_LEN	(sizeof(ORIGIN) - 1)

#define ENOENT		2
#define ENOEXEC		8
#define ENAMETOOLONG	36

extern int bpf_dynptr_from_file(struct file *file, __u32 flags,
				struct bpf_dynptr *ptr__uninit) __ksym;
extern int bpf_dynptr_file_discard(struct bpf_dynptr *dynptr) __ksym;
extern int bpf_path_d_path(const struct path *path, char *buf,
			   size_t buf__sz) __ksym;
extern int bpf_binprm_set_interp(struct linux_binprm *bprm, const char *path,
				 size_t path__sz) __ksym;

struct scratch {
	char interp[PATH_MAX];	/* PT_INTERP as embedded in the binary */
	char path[PATH_MAX];	/* d_path of the binary, becomes the result */
};

/* Keyed by pid: execs run concurrently and the programs can sleep. */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 512);
	__type(key, __u64);
	__type(value, struct scratch);
} scratch_map SEC(".maps");

static const struct scratch zero_scratch;

/* An ELF64 binary per the prefetched header? */
static bool is_elf64(struct linux_binprm *bprm)
{
	return bprm->buf[0] == 0x7f && bprm->buf[1] == 'E' &&
	       bprm->buf[2] == 'L' && bprm->buf[3] == 'F' &&
	       bprm->buf[EI_CLASS] == ELFCLASSXX;
}

/* Locate PT_INTERP; false if the file has none or looks malformed. */
static bool find_pt_interp(struct bpf_dynptr *dp, struct elf64_phdr *phdr)
{
	struct elf64_hdr ehdr;
	bool found = false;
	int i;

	if (bpf_dynptr_read(&ehdr, sizeof(ehdr), dp, 0, 0))
		return false;
	if (ehdr.e_phentsize != sizeof(struct elf64_phdr))
		return false;

	bpf_for(i, 0, ehdr.e_phnum) {
		if (i >= MAX_PHDRS)
			break;
		if (bpf_dynptr_read(phdr, sizeof(*phdr), dp,
				    ehdr.e_phoff + i * sizeof(*phdr), 0))
			return false;
		if (phdr->p_type == PT_INTERP) {
			found = true;
			break;
		}
	}
	return found;
}

/*
 * An ELF64 binary whose PT_INTERP starts with "$ORIGIN/" is ours. The
 * match can sleep and read the file, so the decision is made here and
 * regular binaries never commit to this handler: later binfmt_misc
 * entries and binfmt_elf see them as if we did not exist.
 */
SEC("struct_ops.s/match")
bool BPF_PROG(nix_origin_match, struct linux_binprm *bprm)
{
	char prefix[ORIGIN_LEN + 1] = {};
	struct elf64_phdr phdr;
	struct bpf_dynptr dp;
	bool ours = false;

	if (!is_elf64(bprm))
		return false;

	/* The dynptr must be discarded on every path once requested. */
	if (bpf_dynptr_from_file(bprm->file, 0, &dp))
		goto out;
	if (find_pt_interp(&dp, &phdr) &&
	    phdr.p_filesz > ORIGIN_LEN + 1 &&
	    !bpf_dynptr_read(prefix, sizeof(prefix), &dp, phdr.p_offset, 0))
		ours = !bpf_strncmp(prefix, sizeof(prefix), ORIGIN "/");
out:
	bpf_dynptr_file_discard(&dp);
	return ours;
}

/*
 * The match is committed and already vetted the "$ORIGIN/" prefix, so
 * everything here reads the file again from scratch: -ENOEXEC only
 * covers a binary that changed under us and stopped being ours.
 */
SEC("struct_ops.s/load")
int BPF_PROG(nix_origin_load, struct linux_binprm *bprm)
{
	__u32 isz, sfx, rsz, slash;
	struct elf64_phdr phdr;
	struct bpf_dynptr dp;
	struct scratch *sc;
	__u64 id;
	int ret = -ENOEXEC, len, i;

	if (bpf_dynptr_from_file(bprm->file, 0, &dp))
		goto out;

	if (!find_pt_interp(&dp, &phdr))
		goto out;

	isz = phdr.p_filesz;
	if (isz <= ORIGIN_LEN + 1 || isz >= sizeof(sc->interp))
		goto out;
	/*
	 * The range check above compiles to a test on a zero-extended copy of
	 * the u64 p_filesz, so the verifier does not carry the bound to the
	 * dynptr_read() length below ("unbounded memory access"). Mask isz to
	 * the buffer size (a power of two) and force the masked value to be
	 * materialized with a barrier so the read uses the bounded register.
	 */
	isz &= sizeof(sc->interp) - 1;
	barrier_var(isz);

	id = bpf_get_current_pid_tgid();
	if (bpf_map_update_elem(&scratch_map, &id, &zero_scratch, BPF_ANY))
		goto out;
	sc = bpf_map_lookup_elem(&scratch_map, &id);
	if (!sc)
		goto out_del;

	if (bpf_dynptr_read(sc->interp, isz, &dp, phdr.p_offset, 0))
		goto out_del;
	if (sc->interp[isz - 1] != '\0')
		goto out_del;

	/* Not "$ORIGIN/..." anymore? Then it is not ours anymore either. */
	if (sc->interp[0] != '$' || sc->interp[1] != 'O' ||
	    sc->interp[2] != 'R' || sc->interp[3] != 'I' ||
	    sc->interp[4] != 'G' || sc->interp[5] != 'I' ||
	    sc->interp[6] != 'N' || sc->interp[7] != '/')
		goto out_del;

	/*
	 * From here on resolution failures fail the exec instead of falling
	 * back to binfmt_elf, which would resolve the literal "$ORIGIN/..."
	 * relative to the caller's cwd.
	 */
	ret = -ENOENT;
	len = bpf_path_d_path(&bprm->file->f_path, sc->path, sizeof(sc->path));
	if (len <= 0 || len > sizeof(sc->path))
		goto out_del;
	/* Unreachable or unlinked ("... (deleted)") binaries can't resolve. */
	if (sc->path[0] != '/')
		goto out_del;

	/* $ORIGIN = dirname of the binary. */
	slash = 0;
	bpf_for(i, 1, len - 1) {
		if (i >= sizeof(sc->path))
			break;
		if (sc->path[i] == '/')
			slash = i;
	}

	/* Splice the suffix (leading '/' and NUL included) onto the dir. */
	sfx = isz - ORIGIN_LEN;
	rsz = slash + sfx;
	if (rsz > sizeof(sc->path)) {
		ret = -ENAMETOOLONG;
		goto out_del;
	}
	bpf_for(i, 0, sfx) {
		__u32 s = ORIGIN_LEN + i, d = slash + i;

		if (s >= sizeof(sc->interp) || d >= sizeof(sc->path))
			break;
		sc->path[d] = sc->interp[s];
	}

	ret = bpf_binprm_set_interp(bprm, sc->path, rsz);
out_del:
	bpf_map_delete_elem(&scratch_map, &id);
out:
	bpf_dynptr_file_discard(&dp);
	return ret;
}

SEC(".struct_ops.link")
struct binfmt_misc_ops nix_origin = {
	.match = (void *)nix_origin_match,
	.load = (void *)nix_origin_load,
	.name = "nix_origin",
};
