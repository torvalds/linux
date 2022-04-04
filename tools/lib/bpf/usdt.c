// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libelf.h>
#include <gelf.h>
#include <unistd.h>
#include <linux/ptrace.h>
#include <linux/kernel.h>

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_common.h"
#include "libbpf_internal.h"
#include "hashmap.h"

/* libbpf's USDT support consists of BPF-side state/code and user-space
 * state/code working together in concert. BPF-side parts are defined in
 * usdt.bpf.h header library. User-space state is encapsulated by struct
 * usdt_manager and all the supporting code centered around usdt_manager.
 *
 * usdt.bpf.h defines two BPF maps that usdt_manager expects: USDT spec map
 * and IP-to-spec-ID map, which is auxiliary map necessary for kernels that
 * don't support BPF cookie (see below). These two maps are implicitly
 * embedded into user's end BPF object file when user's code included
 * usdt.bpf.h. This means that libbpf doesn't do anything special to create
 * these USDT support maps. They are created by normal libbpf logic of
 * instantiating BPF maps when opening and loading BPF object.
 *
 * As such, libbpf is basically unaware of the need to do anything
 * USDT-related until the very first call to bpf_program__attach_usdt(), which
 * can be called by user explicitly or happen automatically during skeleton
 * attach (or, equivalently, through generic bpf_program__attach() call). At
 * this point, libbpf will instantiate and initialize struct usdt_manager and
 * store it in bpf_object. USDT manager is per-BPF object construct, as each
 * independent BPF object might or might not have USDT programs, and thus all
 * the expected USDT-related state. There is no coordination between two
 * bpf_object in parts of USDT attachment, they are oblivious of each other's
 * existence and libbpf is just oblivious, dealing with bpf_object-specific
 * USDT state.
 *
 * Quick crash course on USDTs.
 *
 * From user-space application's point of view, USDT is essentially just
 * a slightly special function call that normally has zero overhead, unless it
 * is being traced by some external entity (e.g, BPF-based tool). Here's how
 * a typical application can trigger USDT probe:
 *
 * #include <sys/sdt.h>  // provided by systemtap-sdt-devel package
 * // folly also provide similar functionality in folly/tracing/StaticTracepoint.h
 *
 * STAP_PROBE3(my_usdt_provider, my_usdt_probe_name, 123, x, &y);
 *
 * USDT is identified by it's <provider-name>:<probe-name> pair of names. Each
 * individual USDT has a fixed number of arguments (3 in the above example)
 * and specifies values of each argument as if it was a function call.
 *
 * USDT call is actually not a function call, but is instead replaced by
 * a single NOP instruction (thus zero overhead, effectively). But in addition
 * to that, those USDT macros generate special SHT_NOTE ELF records in
 * .note.stapsdt ELF section. Here's an example USDT definition as emitted by
 * `readelf -n <binary>`:
 *
 *   stapsdt              0x00000089       NT_STAPSDT (SystemTap probe descriptors)
 *   Provider: test
 *   Name: usdt12
 *   Location: 0x0000000000549df3, Base: 0x00000000008effa4, Semaphore: 0x0000000000a4606e
 *   Arguments: -4@-1204(%rbp) -4@%edi -8@-1216(%rbp) -8@%r8 -4@$5 -8@%r9 8@%rdx 8@%r10 -4@$-9 -2@%cx -2@%ax -1@%sil
 *
 * In this case we have USDT test:usdt12 with 12 arguments.
 *
 * Location and base are offsets used to calculate absolute IP address of that
 * NOP instruction that kernel can replace with an interrupt instruction to
 * trigger instrumentation code (BPF program for all that we care about).
 *
 * Semaphore above is and optional feature. It records an address of a 2-byte
 * refcount variable (normally in '.probes' ELF section) used for signaling if
 * there is anything that is attached to USDT. This is useful for user
 * applications if, for example, they need to prepare some arguments that are
 * passed only to USDTs and preparation is expensive. By checking if USDT is
 * "activated", an application can avoid paying those costs unnecessarily.
 * Recent enough kernel has built-in support for automatically managing this
 * refcount, which libbpf expects and relies on. If USDT is defined without
 * associated semaphore, this value will be zero. See selftests for semaphore
 * examples.
 *
 * Arguments is the most interesting part. This USDT specification string is
 * providing information about all the USDT arguments and their locations. The
 * part before @ sign defined byte size of the argument (1, 2, 4, or 8) and
 * whether the argument is signed or unsigned (negative size means signed).
 * The part after @ sign is assembly-like definition of argument location
 * (see [0] for more details). Technically, assembler can provide some pretty
 * advanced definitions, but libbpf is currently supporting three most common
 * cases:
 *   1) immediate constant, see 5th and 9th args above (-4@$5 and -4@-9);
 *   2) register value, e.g., 8@%rdx, which means "unsigned 8-byte integer
 *      whose value is in register %rdx";
 *   3) memory dereference addressed by register, e.g., -4@-1204(%rbp), which
 *      specifies signed 32-bit integer stored at offset -1204 bytes from
 *      memory address stored in %rbp.
 *
 *   [0] https://sourceware.org/systemtap/wiki/UserSpaceProbeImplementation
 *
 * During attachment, libbpf parses all the relevant USDT specifications and
 * prepares `struct usdt_spec` (USDT spec), which is then provided to BPF-side
 * code through spec map. This allows BPF applications to quickly fetch the
 * actual value at runtime using a simple BPF-side code.
 *
 * With basics out of the way, let's go over less immeditately obvious aspects
 * of supporting USDTs.
 *
 * First, there is no special USDT BPF program type. It is actually just
 * a uprobe BPF program (which for kernel, at least currently, is just a kprobe
 * program, so BPF_PROG_TYPE_KPROBE program type). With the only difference
 * that uprobe is usually attached at the function entry, while USDT will
 * normally will be somewhere inside the function. But it should always be
 * pointing to NOP instruction, which makes such uprobes the fastest uprobe
 * kind.
 *
 * Second, it's important to realize that such STAP_PROBEn(provider, name, ...)
 * macro invocations can end up being inlined many-many times, depending on
 * specifics of each individual user application. So single conceptual USDT
 * (identified by provider:name pair of identifiers) is, generally speaking,
 * multiple uprobe locations (USDT call sites) in different places in user
 * application. Further, again due to inlining, each USDT call site might end
 * up having the same argument #N be located in a different place. In one call
 * site it could be a constant, in another will end up in a register, and in
 * yet another could be some other register or even somewhere on the stack.
 *
 * As such, "attaching to USDT" means (in general case) attaching the same
 * uprobe BPF program to multiple target locations in user application, each
 * potentially having a completely different USDT spec associated with it.
 * To wire all this up together libbpf allocates a unique integer spec ID for
 * each unique USDT spec. Spec IDs are allocated as sequential small integers
 * so that they can be used as keys in array BPF map (for performance reasons).
 * Spec ID allocation and accounting is big part of what usdt_manager is
 * about. This state has to be maintained per-BPF object and coordinate
 * between different USDT attachments within the same BPF object.
 *
 * Spec ID is the key in spec BPF map, value is the actual USDT spec layed out
 * as struct usdt_spec. Each invocation of BPF program at runtime needs to
 * know its associated spec ID. It gets it either through BPF cookie, which
 * libbpf sets to spec ID during attach time, or, if kernel is too old to
 * support BPF cookie, through IP-to-spec-ID map that libbpf maintains in such
 * case. The latter means that some modes of operation can't be supported
 * without BPF cookie. Such mode is attaching to shared library "generically",
 * without specifying target process. In such case, it's impossible to
 * calculate absolute IP addresses for IP-to-spec-ID map, and thus such mode
 * is not supported without BPF cookie support.
 *
 * Note that libbpf is using BPF cookie functionality for its own internal
 * needs, so user itself can't rely on BPF cookie feature. To that end, libbpf
 * provides conceptually equivalent USDT cookie support. It's still u64
 * user-provided value that can be associated with USDT attachment. Note that
 * this will be the same value for all USDT call sites within the same single
 * *logical* USDT attachment. This makes sense because to user attaching to
 * USDT is a single BPF program triggered for singular USDT probe. The fact
 * that this is done at multiple actual locations is a mostly hidden
 * implementation details. This USDT cookie value can be fetched with
 * bpf_usdt_cookie(ctx) API provided by usdt.bpf.h
 *
 * Lastly, while single USDT can have tons of USDT call sites, it doesn't
 * necessarily have that many different USDT specs. It very well might be
 * that 1000 USDT call sites only need 5 different USDT specs, because all the
 * arguments are typically contained in a small set of registers or stack
 * locations. As such, it's wasteful to allocate as many USDT spec IDs as
 * there are USDT call sites. So libbpf tries to be frugal and performs
 * on-the-fly deduplication during a single USDT attachment to only allocate
 * the minimal required amount of unique USDT specs (and thus spec IDs). This
 * is trivially achieved by using USDT spec string (Arguments string from USDT
 * note) as a lookup key in a hashmap. USDT spec string uniquely defines
 * everything about how to fetch USDT arguments, so two USDT call sites
 * sharing USDT spec string can safely share the same USDT spec and spec ID.
 * Note, this spec string deduplication is happening only during the same USDT
 * attachment, so each USDT spec shares the same USDT cookie value. This is
 * not generally true for other USDT attachments within the same BPF object,
 * as even if USDT spec string is the same, USDT cookie value can be
 * different. It was deemed excessive to try to deduplicate across independent
 * USDT attachments by taking into account USDT spec string *and* USDT cookie
 * value, which would complicated spec ID accounting significantly for little
 * gain.
 */

struct usdt_target {
	long abs_ip;
	long rel_ip;
	long sema_off;
};

struct usdt_manager {
	struct bpf_map *specs_map;
	struct bpf_map *ip_to_spec_id_map;

	bool has_bpf_cookie;
	bool has_sema_refcnt;
};

struct usdt_manager *usdt_manager_new(struct bpf_object *obj)
{
	static const char *ref_ctr_sysfs_path = "/sys/bus/event_source/devices/uprobe/format/ref_ctr_offset";
	struct usdt_manager *man;
	struct bpf_map *specs_map, *ip_to_spec_id_map;

	specs_map = bpf_object__find_map_by_name(obj, "__bpf_usdt_specs");
	ip_to_spec_id_map = bpf_object__find_map_by_name(obj, "__bpf_usdt_ip_to_spec_id");
	if (!specs_map || !ip_to_spec_id_map) {
		pr_warn("usdt: failed to find USDT support BPF maps, did you forget to include bpf/usdt.bpf.h?\n");
		return ERR_PTR(-ESRCH);
	}

	man = calloc(1, sizeof(*man));
	if (!man)
		return ERR_PTR(-ENOMEM);

	man->specs_map = specs_map;
	man->ip_to_spec_id_map = ip_to_spec_id_map;

	/* Detect if BPF cookie is supported for kprobes.
	 * We don't need IP-to-ID mapping if we can use BPF cookies.
	 * Added in: 7adfc6c9b315 ("bpf: Add bpf_get_attach_cookie() BPF helper to access bpf_cookie value")
	 */
	man->has_bpf_cookie = kernel_supports(obj, FEAT_BPF_COOKIE);

	/* Detect kernel support for automatic refcounting of USDT semaphore.
	 * If this is not supported, USDTs with semaphores will not be supported.
	 * Added in: a6ca88b241d5 ("trace_uprobe: support reference counter in fd-based uprobe")
	 */
	man->has_sema_refcnt = access(ref_ctr_sysfs_path, F_OK) == 0;

	return man;
}

void usdt_manager_free(struct usdt_manager *man)
{
	if (IS_ERR_OR_NULL(man))
		return;

	free(man);
}

static int sanity_check_usdt_elf(Elf *elf, const char *path)
{
	GElf_Ehdr ehdr;
	int endianness;

	if (elf_kind(elf) != ELF_K_ELF) {
		pr_warn("usdt: unrecognized ELF kind %d for '%s'\n", elf_kind(elf), path);
		return -EBADF;
	}

	switch (gelf_getclass(elf)) {
	case ELFCLASS64:
		if (sizeof(void *) != 8) {
			pr_warn("usdt: attaching to 64-bit ELF binary '%s' is not supported\n", path);
			return -EBADF;
		}
		break;
	case ELFCLASS32:
		if (sizeof(void *) != 4) {
			pr_warn("usdt: attaching to 32-bit ELF binary '%s' is not supported\n", path);
			return -EBADF;
		}
		break;
	default:
		pr_warn("usdt: unsupported ELF class for '%s'\n", path);
		return -EBADF;
	}

	if (!gelf_getehdr(elf, &ehdr))
		return -EINVAL;

	if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) {
		pr_warn("usdt: unsupported type of ELF binary '%s' (%d), only ET_EXEC and ET_DYN are supported\n",
			path, ehdr.e_type);
		return -EBADF;
	}

#if __BYTE_ORDER == __LITTLE_ENDIAN
	endianness = ELFDATA2LSB;
#elif __BYTE_ORDER == __BIG_ENDIAN
	endianness = ELFDATA2MSB;
#else
# error "Unrecognized __BYTE_ORDER__"
#endif
	if (endianness != ehdr.e_ident[EI_DATA]) {
		pr_warn("usdt: ELF endianness mismatch for '%s'\n", path);
		return -EBADF;
	}

	return 0;
}

static int collect_usdt_targets(struct usdt_manager *man, Elf *elf, const char *path, pid_t pid,
				const char *usdt_provider, const char *usdt_name, long usdt_cookie,
				struct usdt_target **out_targets, size_t *out_target_cnt)
{
	return -ENOTSUP;
}

struct bpf_link_usdt {
	struct bpf_link link;

	struct usdt_manager *usdt_man;

	size_t uprobe_cnt;
	struct {
		long abs_ip;
		struct bpf_link *link;
	} *uprobes;
};

static int bpf_link_usdt_detach(struct bpf_link *link)
{
	struct bpf_link_usdt *usdt_link = container_of(link, struct bpf_link_usdt, link);
	int i;

	for (i = 0; i < usdt_link->uprobe_cnt; i++) {
		/* detach underlying uprobe link */
		bpf_link__destroy(usdt_link->uprobes[i].link);
	}

	return 0;
}

static void bpf_link_usdt_dealloc(struct bpf_link *link)
{
	struct bpf_link_usdt *usdt_link = container_of(link, struct bpf_link_usdt, link);

	free(usdt_link->uprobes);
	free(usdt_link);
}

struct bpf_link *usdt_manager_attach_usdt(struct usdt_manager *man, const struct bpf_program *prog,
					  pid_t pid, const char *path,
					  const char *usdt_provider, const char *usdt_name,
					  long usdt_cookie)
{
	LIBBPF_OPTS(bpf_uprobe_opts, opts);
	struct bpf_link_usdt *link = NULL;
	struct usdt_target *targets = NULL;
	size_t target_cnt;
	int i, fd, err;
	Elf *elf;

	/* TODO: perform path resolution similar to uprobe's */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		err = -errno;
		pr_warn("usdt: failed to open ELF binary '%s': %d\n", path, err);
		return libbpf_err_ptr(err);
	}

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf) {
		err = -EBADF;
		pr_warn("usdt: failed to parse ELF binary '%s': %s\n", path, elf_errmsg(-1));
		goto err_out;
	}

	err = sanity_check_usdt_elf(elf, path);
	if (err)
		goto err_out;

	/* normalize PID filter */
	if (pid < 0)
		pid = -1;
	else if (pid == 0)
		pid = getpid();

	/* discover USDT in given binary, optionally limiting
	 * activations to a given PID, if pid > 0
	 */
	err = collect_usdt_targets(man, elf, path, pid, usdt_provider, usdt_name,
				   usdt_cookie, &targets, &target_cnt);
	if (err <= 0) {
		err = (err == 0) ? -ENOENT : err;
		goto err_out;
	}

	link = calloc(1, sizeof(*link));
	if (!link) {
		err = -ENOMEM;
		goto err_out;
	}

	link->usdt_man = man;
	link->link.detach = &bpf_link_usdt_detach;
	link->link.dealloc = &bpf_link_usdt_dealloc;

	link->uprobes = calloc(target_cnt, sizeof(*link->uprobes));
	if (!link->uprobes) {
		err = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < target_cnt; i++) {
		struct usdt_target *target = &targets[i];
		struct bpf_link *uprobe_link;

		opts.ref_ctr_offset = target->sema_off;
		uprobe_link = bpf_program__attach_uprobe_opts(prog, pid, path,
							      target->rel_ip, &opts);
		err = libbpf_get_error(uprobe_link);
		if (err) {
			pr_warn("usdt: failed to attach uprobe #%d for '%s:%s' in '%s': %d\n",
				i, usdt_provider, usdt_name, path, err);
			goto err_out;
		}

		link->uprobes[i].link = uprobe_link;
		link->uprobes[i].abs_ip = target->abs_ip;
		link->uprobe_cnt++;
	}

	elf_end(elf);
	close(fd);

	return &link->link;

err_out:
	bpf_link__destroy(&link->link);

	if (elf)
		elf_end(elf);
	close(fd);
	return libbpf_err_ptr(err);
}
