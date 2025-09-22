/*	$OpenBSD: procmap.c,v 1.74 2024/10/20 11:21:24 claudio Exp $ */
/*	$NetBSD: pmap.c,v 1.1 2002/09/01 20:32:44 atatat Exp $ */

/*
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Brown.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define _KERNEL
#include <sys/tree.h>
#undef _KERNEL

#include <sys/types.h>
#include <sys/time.h>
#include <sys/exec.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/sysctl.h>

/* XXX until uvm gets cleaned up */
typedef int boolean_t;

#include <uvm/uvm.h>
#include <uvm/uvm_device.h>
#include <uvm/uvm_amap.h>
#include <uvm/uvm_vnode.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#undef doff_t
#undef IN_ACCESS
#undef i_size
#undef i_devvp
#include <isofs/cd9660/iso.h>
#include <isofs/cd9660/cd9660_node.h>

#include <kvm.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

/*
 * stolen (and munged) from #include <uvm/uvm_object.h>
 */
#define UVM_OBJ_IS_VNODE(uobj)	((uobj)->pgops == uvm_vnodeops)
#define UVM_OBJ_IS_AOBJ(uobj)	((uobj)->pgops == aobj_pager)
#define UVM_OBJ_IS_DEVICE(uobj)	((uobj)->pgops == uvm_deviceops)

#define PRINT_VMSPACE		0x00000001
#define PRINT_VM_MAP		0x00000002
#define PRINT_VM_MAP_HEADER	0x00000004
#define PRINT_VM_MAP_ENTRY	0x00000008
#define DUMP_NAMEI_CACHE	0x00000010

struct cache_entry {
	LIST_ENTRY(cache_entry) ce_next;
	struct vnode *ce_vp, *ce_pvp;
	u_long ce_cid, ce_pcid;
	unsigned int ce_nlen;
	char ce_name[256];
};

LIST_HEAD(cache_head, cache_entry) lcache;
TAILQ_HEAD(namecache_head, namecache) nclruhead;
int namecache_loaded;
void *uvm_vnodeops, *uvm_deviceops, *aobj_pager;
u_long kernel_map_addr, nclruhead_addr;
int debug, verbose;
int print_all, print_map, print_maps, print_solaris, print_ddb, print_amap;
int rwx = PROT_READ | PROT_WRITE | PROT_EXEC;
rlim_t maxssiz;

struct sum {
	unsigned long s_am_nslots;
	unsigned long s_am_nusedslots;
};

struct kbit {
	/*
	 * size of data chunk
	 */
	size_t k_size;

	/*
	 * something for printf() and something for kvm_read()
	 */
	union {
		void *k_addr_p;
		u_long k_addr_ul;
	} k_addr;

	/*
	 * where we actually put the "stuff"
	 */
	union {
		char data[1];
		struct vmspace vmspace;
		struct vm_map vm_map;
		struct vm_map_entry vm_map_entry;
		struct uvm_vnode uvm_vnode;
		struct vnode vnode;
		struct uvm_object uvm_object;
		struct mount mount;
		struct inode inode;
		struct iso_node iso_node;
		struct uvm_device uvm_device;
		struct vm_amap vm_amap;
	} k_data;
};

/* the size of the object in the kernel */
#define S(x)	((x)->k_size)
/* the address of the object in kernel, two forms */
#define A(x)	((x)->k_addr.k_addr_ul)
#define P(x)	((x)->k_addr.k_addr_p)
/* the data from the kernel */
#define D(x,d)	(&((x)->k_data.d))

/* suck the data from the kernel */
#define _KDEREF(kd, addr, dst, sz) do { \
	ssize_t len; \
	len = kvm_read((kd), (addr), (dst), (sz)); \
	if (len != (sz)) \
		errx(1, "%s == %ld vs. %lu @ %lx", \
		    kvm_geterr(kd), (long)len, (unsigned long)(sz), (addr)); \
} while (0/*CONSTCOND*/)

/* suck the data using the structure */
#define KDEREF(kd, item) _KDEREF((kd), A(item), D(item, data), S(item))

struct nlist nl[] = {
	{ "_maxsmap" },
#define NL_MAXSSIZ		0
	{ "_uvm_vnodeops" },
#define NL_UVM_VNODEOPS		1
	{ "_uvm_deviceops" },
#define NL_UVM_DEVICEOPS	2
	{ "_aobj_pager" },
#define NL_AOBJ_PAGER		3
	{ "_kernel_map" },
#define NL_KERNEL_MAP		4
	{ "_nclruhead" },
#define NL_NCLRUHEAD		5
	{ NULL }
};

void load_symbols(kvm_t *);
void process_map(kvm_t *, pid_t, struct kinfo_proc *, struct sum *);
struct vm_map_entry *load_vm_map_entries(kvm_t *, struct vm_map_entry *,
    struct vm_map_entry *);
void unload_vm_map_entries(struct vm_map_entry *);
size_t dump_vm_map_entry(kvm_t *, struct kbit *, struct vm_map_entry *,
    struct sum *);
char *findname(kvm_t *, struct kbit *, struct vm_map_entry *, struct kbit *,
    struct kbit *, struct kbit *);
int search_cache(kvm_t *, struct kbit *, char **, char *, size_t);
void load_name_cache(kvm_t *);
void cache_enter(struct namecache *);
static void __dead usage(void);
static pid_t strtopid(const char *);
void print_sum(struct sum *, struct sum *);

/*
 * uvm_map address tree implementation.
 */
static int no_impl(const void *, const void *);
static int
no_impl(const void *p, const void *q)
{
	errx(1, "uvm_map address comparison not implemented");
	return 0;
}

RBT_PROTOTYPE(uvm_map_addr, vm_map_entry, daddrs.addr_entry, no_impl);
RBT_GENERATE(uvm_map_addr, vm_map_entry, daddrs.addr_entry, no_impl);

int
main(int argc, char *argv[])
{
	const char *errstr;
	char errbuf[_POSIX2_LINE_MAX], *kmem = NULL, *kernel = NULL;
	struct kinfo_proc *kproc;
	struct sum total_sum;
	int many, ch, rc;
	kvm_t *kd;
	pid_t pid = -1;
	gid_t gid;

	while ((ch = getopt(argc, argv, "AaD:dlmM:N:p:Prsvx")) != -1) {
		switch (ch) {
		case 'A':
			print_amap = 1;
			break;
		case 'a':
			print_all = 1;
			break;
		case 'd':
			print_ddb = 1;
			break;
		case 'D':
			debug = strtonum(optarg, 0, 0x1f, &errstr);
			if (errstr)
				errx(1, "invalid debug mask");
			break;
		case 'l':
			print_maps = 1;
			break;
		case 'm':
			print_map = 1;
			break;
		case 'M':
			kmem = optarg;
			break;
		case 'N':
			kernel = optarg;
			break;
		case 'p':
			pid = strtopid(optarg);
			break;
		case 'P':
			pid = getpid();
			break;
		case 's':
			print_solaris = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'r':
		case 'x':
			errx(1, "-%c option not implemented, sorry", ch);
			/*NOTREACHED*/
		default:
			usage();
		}
	}

	/*
	 * Discard setgid privileges if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	gid = getgid();
	if (kernel != NULL || kmem != NULL)
		if (setresgid(gid, gid, gid) == -1)
			err(1, "setresgid");

	argc -= optind;
	argv += optind;

	/* more than one "process" to dump? */
	many = (argc > 1 - (pid == -1 ? 0 : 1)) ? 1 : 0;

	/* apply default */
	if (print_all + print_map + print_maps + print_solaris +
	    print_ddb == 0)
		print_all = 1;

	/* start by opening libkvm */
	kd = kvm_openfiles(kernel, kmem, NULL, O_RDONLY, errbuf);

	if (kernel == NULL && kmem == NULL)
		if (setresgid(gid, gid, gid) == -1)
			err(1, "setresgid");

	if (kd == NULL)
		errx(1, "%s", errbuf);

	/* get "bootstrap" addresses from kernel */
	load_symbols(kd);

	memset(&total_sum, 0, sizeof(total_sum));

	do {
		struct sum sum;

		memset(&sum, 0, sizeof(sum));

		if (pid == -1) {
			if (argc == 0)
				pid = getppid();
			else {
				pid = strtopid(argv[0]);
				argv++;
				argc--;
			}
		}

		/* find the process id */
		if (pid == 0)
			kproc = NULL;
		else {
			kproc = kvm_getprocs(kd, KERN_PROC_PID, pid,
			    sizeof(struct kinfo_proc), &rc);
			if (kproc == NULL || rc == 0) {
				warnc(ESRCH, "%d", pid);
				pid = -1;
				continue;
			}
		}

		/* dump it */
		if (many) {
			if (kproc)
				printf("process %d:\n", pid);
			else
				printf("kernel:\n");
		}

		process_map(kd, pid, kproc, &sum);
		if (print_amap)
			print_sum(&sum, &total_sum);
		pid = -1;
	} while (argc > 0);

	if (print_amap)
		print_sum(&total_sum, NULL);

	/* done.  go away. */
	rc = kvm_close(kd);
	if (rc == -1)
		err(1, "kvm_close");

	return (0);
}

void
print_sum(struct sum *sum, struct sum *total_sum)
{
	const char *t = total_sum == NULL ? "total " : "";
	printf("%samap mapped slots: %lu\n", t, sum->s_am_nslots);
	printf("%samap used slots: %lu\n", t, sum->s_am_nusedslots);

	if (total_sum) {
		total_sum->s_am_nslots += sum->s_am_nslots;
		total_sum->s_am_nusedslots += sum->s_am_nusedslots;
	}
}

void
process_map(kvm_t *kd, pid_t pid, struct kinfo_proc *proc, struct sum *sum)
{
	struct kbit kbit[3], *vmspace, *vm_map;
	struct vm_map_entry *vm_map_entry;
	size_t total = 0;
	char *thing;
	uid_t uid;
	int vmmap_flags;

	if ((uid = getuid())) {
		if (pid == 0) {
			warnx("kernel map is restricted");
			return;
		}
		if (uid != proc->p_uid) {
			warnx("other users' process maps are restricted");
			return;
		}
	}

	vmspace = &kbit[0];
	vm_map = &kbit[1];

	A(vmspace) = 0;
	A(vm_map) = 0;

	if (pid > 0) {
		A(vmspace) = (u_long)proc->p_vmspace;
		S(vmspace) = sizeof(struct vmspace);
		KDEREF(kd, vmspace);
		thing = "proc->p_vmspace.vm_map";
	} else {
		A(vmspace) = 0;
		S(vmspace) = 0;
		thing = "kernel_map";
	}

	if (pid > 0 && (debug & PRINT_VMSPACE)) {
		printf("proc->p_vmspace %p = {", P(vmspace));
		printf(" vm_refcnt = %d,", D(vmspace, vmspace)->vm_refcnt);
		printf(" vm_shm = %p,\n", D(vmspace, vmspace)->vm_shm);
		printf("    vm_rssize = %d,", D(vmspace, vmspace)->vm_rssize);
#if 0
		printf(" vm_swrss = %d,", D(vmspace, vmspace)->vm_swrss);
#endif
		printf(" vm_tsize = %d,", D(vmspace, vmspace)->vm_tsize);
		printf(" vm_dsize = %d,\n", D(vmspace, vmspace)->vm_dsize);
		printf("    vm_ssize = %d,", D(vmspace, vmspace)->vm_ssize);
		printf(" vm_taddr = %p,", D(vmspace, vmspace)->vm_taddr);
		printf(" vm_daddr = %p,\n", D(vmspace, vmspace)->vm_daddr);
		printf("    vm_maxsaddr = %p,",
		    D(vmspace, vmspace)->vm_maxsaddr);
		printf(" vm_minsaddr = %p }\n",
		    D(vmspace, vmspace)->vm_minsaddr);
	}

	S(vm_map) = sizeof(struct vm_map);
	if (pid > 0) {
		A(vm_map) = A(vmspace);
		memcpy(D(vm_map, vm_map), &D(vmspace, vmspace)->vm_map,
		    S(vm_map));
	} else {
		A(vm_map) = kernel_map_addr;
		KDEREF(kd, vm_map);
	}
	if (debug & PRINT_VM_MAP) {
		printf("%s %p = {", thing, P(vm_map));

		printf(" pmap = %p,\n", D(vm_map, vm_map)->pmap);
		printf("    lock = <struct lock>\n");
		printf("    size = %lx,", D(vm_map, vm_map)->size);
		printf(" ref_count = %d,", D(vm_map, vm_map)->ref_count);
		printf(" ref_lock = <struct simplelock>,\n");
		printf("    min_offset-max_offset = 0x%lx-0x%lx\n",
		    D(vm_map, vm_map)->min_offset,
		    D(vm_map, vm_map)->max_offset);
		printf("    b_start-b_end = 0x%lx-0x%lx\n",
		    D(vm_map, vm_map)->b_start,
		    D(vm_map, vm_map)->b_end);
		printf("    s_start-s_end = 0x%lx-0x%lx\n",
		    D(vm_map, vm_map)->s_start,
		    D(vm_map, vm_map)->s_end);
		vmmap_flags = D(vm_map, vm_map)->flags;
		printf("    flags = %x <%s%s%s%s%s%s >,\n",
		    vmmap_flags,
		    vmmap_flags & VM_MAP_PAGEABLE ? " PAGEABLE" : "",
		    vmmap_flags & VM_MAP_INTRSAFE ? " INTRSAFE" : "",
		    vmmap_flags & VM_MAP_WIREFUTURE ? " WIREFUTURE" : "",
#ifdef VM_MAP_BUSY
		    vmmap_flags & VM_MAP_BUSY ? " BUSY" :
#endif
		    "",
#ifdef VM_MAP_WANTLOCK
		    vmmap_flags & VM_MAP_WANTLOCK ? " WANTLOCK" :
#endif
		    "",
#if VM_MAP_TOPDOWN > 0
		    vmmap_flags & VM_MAP_TOPDOWN ? " TOPDOWN" :
#endif
		    "");
		printf("    timestamp = %u }\n", D(vm_map, vm_map)->timestamp);
	}
	if (print_ddb) {
		printf("MAP %p: [0x%lx->0x%lx]\n", P(vm_map),
		    D(vm_map, vm_map)->min_offset,
		    D(vm_map, vm_map)->max_offset);
		printf("\tsz=%ld, ref=%d, version=%d, flags=0x%x\n",
		    D(vm_map, vm_map)->size,
		    D(vm_map, vm_map)->ref_count,
		    D(vm_map, vm_map)->timestamp,
		    D(vm_map, vm_map)->flags);
		printf("\tpmap=%p(resident=<unknown>)\n",
		    D(vm_map, vm_map)->pmap);
	}

	/* headers */
#ifdef DISABLED_HEADERS
	if (print_map)
		printf("%-*s   %-*s rwxS RWX CPY NCP I W A\n",
		    (int)sizeof(long) * 2 + 2, "Start",
		    (int)sizeof(long) * 2 + 2, "End");
	if (print_maps)
		printf("%-*s   %-*s   rwxSp %-*s Dev   Inode      File\n",
		    (int)sizeof(long) * 2 + 0, "Start",
		    (int)sizeof(long) * 2 + 0, "End",
		    (int)sizeof(long) * 2 + 0, "Offset");
	if (print_solaris)
		printf("%-*s %*s Protection        File\n",
		    (int)sizeof(long) * 2 + 0, "Start",
		    (int)sizeof(int) * 2 - 1,  "Size ");
#endif
	if (print_all)
		printf("%-*s %-*s %*s %-*s rwxSIpc  RWX  I/W/A Dev  %*s - File\n",
		    (int)sizeof(long) * 2, "Start",
		    (int)sizeof(long) * 2, "End",
		    (int)sizeof(int)  * 2, "Size ",
		    (int)sizeof(long) * 2, "Offset",
		    (int)sizeof(int)  * 2, "Inode");

	/* these are the "sub entries" */
	vm_map_entry = load_vm_map_entries(kd,
	    RBT_ROOT(uvm_map_addr, &D(vm_map, vm_map)->addr), NULL);
	if (vm_map_entry != NULL) {
		/* RBTs point at rb_entries inside nodes */
		D(vm_map, vm_map)->addr.rbh_root.rbt_root =
		    &vm_map_entry->daddrs.addr_entry;
	} else
		RBT_INIT(uvm_map_addr, &D(vm_map, vm_map)->addr);

	RBT_FOREACH(vm_map_entry, uvm_map_addr, &D(vm_map, vm_map)->addr)
		total += dump_vm_map_entry(kd, vmspace, vm_map_entry, sum);
	unload_vm_map_entries(RBT_ROOT(uvm_map_addr, &D(vm_map, vm_map)->addr));

	if (print_solaris)
		printf("%-*s %8luK\n",
		    (int)sizeof(void *) * 2 - 2, " total",
		    (unsigned long)total);
	if (print_all)
		printf("%-*s %9luk\n",
		    (int)sizeof(void *) * 4 - 1, " total",
		    (unsigned long)total);
}

void
load_symbols(kvm_t *kd)
{
	int rc, i;

	rc = kvm_nlist(kd, &nl[0]);
	if (rc == -1)
		errx(1, "%s == %d", kvm_geterr(kd), rc);
	for (i = 0; i < sizeof(nl)/sizeof(nl[0]); i++)
		if (nl[i].n_value == 0 && nl[i].n_name)
			printf("%s not found\n", nl[i].n_name);

	uvm_vnodeops =	(void*)nl[NL_UVM_VNODEOPS].n_value;
	uvm_deviceops =	(void*)nl[NL_UVM_DEVICEOPS].n_value;
	aobj_pager =	(void*)nl[NL_AOBJ_PAGER].n_value;

	nclruhead_addr = nl[NL_NCLRUHEAD].n_value;

	_KDEREF(kd, nl[NL_MAXSSIZ].n_value, &maxssiz,
	    sizeof(maxssiz));
	_KDEREF(kd, nl[NL_KERNEL_MAP].n_value, &kernel_map_addr,
	    sizeof(kernel_map_addr));
}

/*
 * Recreate the addr tree of vm_map in local memory.
 */
struct vm_map_entry *
load_vm_map_entries(kvm_t *kd, struct vm_map_entry *kptr,
    struct vm_map_entry *parent)
{
	static struct kbit map_ent;
	struct vm_map_entry *result, *ld;

	if (kptr == NULL)
		return NULL;

	A(&map_ent) = (u_long)kptr;
	S(&map_ent) = sizeof(struct vm_map_entry);
	KDEREF(kd, &map_ent);

	result = malloc(sizeof(*result));
	if (result == NULL)
		err(1, "malloc");
	memcpy(result, D(&map_ent, vm_map_entry), sizeof(struct vm_map_entry));

	/*
	 * Recurse to download rest of the tree.
	 */

	/* RBTs point at rb_entries inside nodes */
	ld = load_vm_map_entries(kd, RBT_LEFT(uvm_map_addr, result), result);
	result->daddrs.addr_entry.rbt_left = &ld->daddrs.addr_entry;
	ld = load_vm_map_entries(kd, RBT_RIGHT(uvm_map_addr, result), result);
	result->daddrs.addr_entry.rbt_right = &ld->daddrs.addr_entry;
	result->daddrs.addr_entry.rbt_parent = &parent->daddrs.addr_entry;

	return result;
}

/*
 * Release the addr tree of vm_map.
 */
void
unload_vm_map_entries(struct vm_map_entry *ent)
{
	if (ent == NULL)
		return;

	unload_vm_map_entries(RBT_LEFT(uvm_map_addr, ent));
	unload_vm_map_entries(RBT_RIGHT(uvm_map_addr, ent));
	free(ent);
}

size_t
dump_vm_map_entry(kvm_t *kd, struct kbit *vmspace,
    struct vm_map_entry *vme, struct sum *sum)
{
	struct kbit kbit[5], *uvm_obj, *vp, *vfs, *amap, *uvn;
	ino_t inode = 0;
	dev_t dev = 0;
	size_t sz = 0;
	char *name;
	static u_long prevend;

	uvm_obj = &kbit[0];
	vp = &kbit[1];
	vfs = &kbit[2];
	amap = &kbit[3];
	uvn = &kbit[4];

	A(uvm_obj) = 0;
	A(vp) = 0;
	A(vfs) = 0;
	A(uvn) = 0;

	if (debug & PRINT_VM_MAP_ENTRY) {
		printf("%s = {", "vm_map_entry");
		printf(" start = %lx,", vme->start);
		printf(" end = %lx,", vme->end);
		printf(" fspace = %lx,\n", vme->fspace);
		printf("    object.uvm_obj/sub_map = %p,\n",
		    vme->object.uvm_obj);
		printf("    offset = %lx,", (unsigned long)vme->offset);
		printf(" etype = %x <%s%s%s%s%s >,", vme->etype,
		    vme->etype & UVM_ET_OBJ ? " OBJ" : "",
		    vme->etype & UVM_ET_SUBMAP ? " SUBMAP" : "",
		    vme->etype & UVM_ET_COPYONWRITE ? " COW" : "",
		    vme->etype & UVM_ET_NEEDSCOPY ? " NEEDSCOPY" : "",
		    vme->etype & UVM_ET_HOLE ? " HOLE" : "");
		printf(" protection = %x,\n", vme->protection);
		printf("    max_protection = %x,", vme->max_protection);
		printf(" inheritance = %d,", vme->inheritance);
		printf(" wired_count = %d,\n", vme->wired_count);
		printf("    aref = <struct vm_aref>,");
		printf(" advice = %d,", vme->advice);
		printf(" flags = %x <%s%s > }\n", vme->flags,
		    vme->flags & UVM_MAP_STATIC ? " STATIC" : "",
		    vme->flags & UVM_MAP_KMEM ? " KMEM" : "");
	}

	A(vp) = 0;
	A(uvm_obj) = 0;

	if (vme->object.uvm_obj != NULL) {
		P(uvm_obj) = vme->object.uvm_obj;
		S(uvm_obj) = sizeof(struct uvm_object);
		KDEREF(kd, uvm_obj);
		if (UVM_ET_ISOBJ(vme) &&
		    UVM_OBJ_IS_VNODE(D(uvm_obj, uvm_object))) {
			P(uvn) = P(uvm_obj);
			S(uvn) = sizeof(struct uvm_vnode);
			KDEREF(kd, uvn);

			P(vp) = D(uvn, uvm_vnode)->u_vnode;
			S(vp) = sizeof(struct vnode);
			KDEREF(kd, vp);
		}
	}

	if (vme->aref.ar_amap != NULL) {
		P(amap) = vme->aref.ar_amap;
		S(amap) = sizeof(struct vm_amap);
		KDEREF(kd, amap);
	}

	A(vfs) = 0;

	if (P(vp) != NULL && D(vp, vnode)->v_mount != NULL) {
		P(vfs) = D(vp, vnode)->v_mount;
		S(vfs) = sizeof(struct mount);
		KDEREF(kd, vfs);
		D(vp, vnode)->v_mount = D(vfs, mount);
	}

	/*
	 * dig out the device number and inode number from certain
	 * file system types.
	 */
#define V_DATA_IS(vp, type, d, i) do { \
	struct kbit data; \
	P(&data) = D(vp, vnode)->v_data; \
	S(&data) = sizeof(*D(&data, type)); \
	KDEREF(kd, &data); \
	dev = D(&data, type)->d; \
	inode = D(&data, type)->i; \
} while (0/*CONSTCOND*/)

	if (A(vp) &&
	    D(vp, vnode)->v_type == VREG &&
	    D(vp, vnode)->v_data != NULL) {
		switch (D(vp, vnode)->v_tag) {
		case VT_UFS:
		case VT_EXT2FS:
			V_DATA_IS(vp, inode, i_dev, i_number);
			break;
		case VT_ISOFS:
			V_DATA_IS(vp, iso_node, i_dev, i_number);
			break;
		case VT_NON:
		case VT_NFS:
		case VT_MFS:
		case VT_MSDOSFS:
		default:
			break;
		}
	}

	name = findname(kd, vmspace, vme, vp, vfs, uvm_obj);

	if (print_map) {
		printf("0x%-*lx 0x%-*lx %c%c%c%c%c %c%c%c %s %s %d %d %d",
		    (int)sizeof(long) * 2 + 0, vme->start,
		    (int)sizeof(long) * 2 + 0, vme->end,
		    (vme->protection & PROT_READ) ? 'r' : '-',
		    (vme->protection & PROT_WRITE) ? 'w' : '-',
		    (vme->protection & PROT_EXEC) ? 'x' : '-',
		    (vme->etype & UVM_ET_STACK) ? 'S' : '-',
		    (vme->etype & UVM_ET_IMMUTABLE) ? 'I' : '-',
		    (vme->max_protection & PROT_READ) ? 'r' : '-',
		    (vme->max_protection & PROT_WRITE) ? 'w' : '-',
		    (vme->max_protection & PROT_EXEC) ? 'x' : '-',
		    (vme->etype & UVM_ET_COPYONWRITE) ? "COW" : "NCOW",
		    (vme->etype & UVM_ET_NEEDSCOPY) ? "NC" : "NNC",
		    vme->inheritance, vme->wired_count,
		    vme->advice);
		if (verbose) {
			if (inode)
				printf(" %u,%u %llu",
				    major(dev), minor(dev),
				    (unsigned long long)inode);
			if (name[0])
				printf(" %s", name);
		}
		printf("\n");
	}

	if (print_maps)
		printf("0x%-*lx 0x%-*lx %c%c%c%c%c%c %0*lx %02x:%02x %llu     %s\n",
		    (int)sizeof(void *) * 2, vme->start,
		    (int)sizeof(void *) * 2, vme->end,
		    (vme->protection & PROT_READ) ? 'r' : '-',
		    (vme->protection & PROT_WRITE) ? 'w' : '-',
		    (vme->protection & PROT_EXEC) ? 'x' : '-',
		    (vme->etype & UVM_ET_STACK) ? 'S' : '-',
		    (vme->etype & UVM_ET_IMMUTABLE) ? 'I' : '-',
		    (vme->etype & UVM_ET_COPYONWRITE) ? 'p' : 's',
		    (int)sizeof(void *) * 2,
		    (unsigned long)vme->offset,
		    major(dev), minor(dev), (unsigned long long)inode,
		    inode ? name : "");

	if (print_ddb) {
		printf(" - <lost address>: 0x%lx->0x%lx: "
		    "obj=%p/0x%lx, amap=%p/%d\n",
		    vme->start, vme->end,
		    vme->object.uvm_obj, (unsigned long)vme->offset,
		    vme->aref.ar_amap, vme->aref.ar_pageoff);
		printf("\tsubmap=%c, cow=%c, nc=%c, stack=%c, "
		    "immutable=%c, prot(max)=%d/%d, inh=%d, "
		    "wc=%d, adv=%d\n",
		    (vme->etype & UVM_ET_SUBMAP) ? 'T' : 'F',
		    (vme->etype & UVM_ET_COPYONWRITE) ? 'T' : 'F',
		    (vme->etype & UVM_ET_NEEDSCOPY) ? 'T' : 'F',
		    (vme->etype & UVM_ET_STACK) ? 'T' : 'F',
		    (vme->etype & UVM_ET_IMMUTABLE) ? 'T' : 'F',
		    vme->protection, vme->max_protection,
		    vme->inheritance, vme->wired_count, vme->advice);
		if (inode && verbose)
			printf("\t(dev=%u,%u ino=%llu [%s] [%p])\n",
			    major(dev), minor(dev), (unsigned long long)inode,
			    inode ? name : "", P(vp));
		else if (name[0] == ' ' && verbose)
			printf("\t(%s)\n", &name[2]);
	}

	if (print_solaris) {
		char prot[30];

		prot[0] = '\0';
		prot[1] = '\0';
		if (vme->protection & PROT_READ)
			strlcat(prot, "/read", sizeof(prot));
		if (vme->protection & PROT_WRITE)
			strlcat(prot, "/write", sizeof(prot));
		if (vme->protection & PROT_EXEC)
			strlcat(prot, "/exec", sizeof(prot));

		sz = (size_t)((vme->end - vme->start) / 1024);
		printf("%0*lX %6luK %-15s   %s\n",
		    (int)sizeof(void *) * 2, (unsigned long)vme->start,
		    (unsigned long)sz, &prot[1], name);
	}

	if (print_all) {
		if (verbose) {
			if  (prevend < vme->start)
				printf("%0*lx-%0*lx %7luk *\n",
				    (int)sizeof(void *) * 2, prevend,
				    (int)sizeof(void *) * 2, vme->start - 1,
				    (vme->start - prevend) / 1024);
			prevend = vme->end;
		}

		sz = (size_t)((vme->end - vme->start) / 1024);
		printf("%0*lx-%0*lx %7luk %0*lx %c%c%c%c%c%c%c (%c%c%c) %d/%d/%d %02u:%02u %7llu - %s",
		    (int)sizeof(void *) * 2, vme->start, (int)sizeof(void *) * 2,
		    vme->end - (vme->start != vme->end ? 1 : 0), (unsigned long)sz,
		    (int)sizeof(void *) * 2, (unsigned long)vme->offset,
		    (vme->protection & PROT_READ) ? 'r' : '-',
		    (vme->protection & PROT_WRITE) ? 'w' : '-',
		    (vme->protection & PROT_EXEC) ? 'x' : '-',
		    (vme->etype & UVM_ET_STACK) ? 'S' : '-',
		    (vme->etype & UVM_ET_IMMUTABLE) ? 'I' : '-',
		    (vme->etype & UVM_ET_COPYONWRITE) ? 'p' : 's',
		    (vme->etype & UVM_ET_NEEDSCOPY) ? '+' : '-',
		    (vme->max_protection & PROT_READ) ? 'r' : '-',
		    (vme->max_protection & PROT_WRITE) ? 'w' : '-',
		    (vme->max_protection & PROT_EXEC) ? 'x' : '-',
		    vme->inheritance, vme->wired_count, vme->advice,
		    major(dev), minor(dev), (unsigned long long)inode, name);
		if (A(vp))
			printf(" [%p]", P(vp));
		printf("\n");
	}

	if (print_amap && vme->aref.ar_amap) {
		printf(" amap - ref: %d fl: 0x%x nsl: %d nuse: %d\n",
		    D(amap, vm_amap)->am_ref,
		    D(amap, vm_amap)->am_flags,
		    D(amap, vm_amap)->am_nslot,
		    D(amap, vm_amap)->am_nused);
		if (sum) {
			sum->s_am_nslots += D(amap, vm_amap)->am_nslot;
			sum->s_am_nusedslots += D(amap, vm_amap)->am_nused;
		}
	}

	/* no access allowed, don't count space */
	if ((vme->protection & rwx) == 0)
		sz = 0;

	return (sz);
}

char *
findname(kvm_t *kd, struct kbit *vmspace,
    struct vm_map_entry *vme, struct kbit *vp,
    struct kbit *vfs, struct kbit *uvm_obj)
{
	static char buf[1024], *name;
	size_t l;

	if (UVM_ET_ISOBJ(vme)) {
		if (A(vfs)) {
			l = strlen(D(vfs, mount)->mnt_stat.f_mntonname);
			switch (search_cache(kd, vp, &name, buf, sizeof(buf))) {
			case 0: /* found something */
				if (name - (1 + 11 + l) < buf)
					break;
				name--;
				*name = '/';
				/*FALLTHROUGH*/
			case 2: /* found nothing */
				name -= 11;
				memcpy(name, " -unknown- ", (size_t)11);
				name -= l;
				memcpy(name,
				    D(vfs, mount)->mnt_stat.f_mntonname, l);
				break;
			case 1: /* all is well */
				if (name - (1 + l) < buf)
					break;
				name--;
				*name = '/';
				if (l != 1) {
					name -= l;
					memcpy(name,
					    D(vfs, mount)->mnt_stat.f_mntonname, l);
				}
				break;
			}
		} else if (UVM_OBJ_IS_DEVICE(D(uvm_obj, uvm_object))) {
			struct kbit kdev;
			dev_t dev;

			P(&kdev) = P(uvm_obj);
			S(&kdev) = sizeof(struct uvm_device);
			KDEREF(kd, &kdev);
			dev = D(&kdev, uvm_device)->u_device;
			name = devname(dev, S_IFCHR);
			if (name != NULL)
				snprintf(buf, sizeof(buf), "/dev/%s", name);
			else
				snprintf(buf, sizeof(buf), "  [ device %u,%u ]",
				    major(dev), minor(dev));
			name = buf;
		} else if (UVM_OBJ_IS_AOBJ(D(uvm_obj, uvm_object)))
			name = "  [ uvm_aobj ]";
		else if (UVM_OBJ_IS_VNODE(D(uvm_obj, uvm_object)))
			name = "  [ ?VNODE? ]";
		else {
			snprintf(buf, sizeof(buf), "  [ unknown (%p) ]",
			    D(uvm_obj, uvm_object)->pgops);
			name = buf;
		}
	} else if (D(vmspace, vmspace)->vm_maxsaddr <= (caddr_t)vme->start &&
	    (D(vmspace, vmspace)->vm_maxsaddr + (size_t)maxssiz) >=
	    (caddr_t)vme->end) {
		name = "  [ stack ]";
	} else if (UVM_ET_ISHOLE(vme))
		name = "  [ hole ]";
	else
		name = "  [ anon ]";

	return (name);
}

int
search_cache(kvm_t *kd, struct kbit *vp, char **name, char *buf, size_t blen)
{
	struct cache_entry *ce;
	struct kbit svp;
	char *o, *e;
	u_long cid;

	if (!namecache_loaded)
		load_name_cache(kd);

	P(&svp) = P(vp);
	S(&svp) = sizeof(struct vnode);
	cid = D(vp, vnode)->v_id;

	e = &buf[blen - 1];
	o = e;
	do {
		LIST_FOREACH(ce, &lcache, ce_next)
			if (ce->ce_vp == P(&svp) && ce->ce_cid == cid)
				break;
		if (ce && ce->ce_vp == P(&svp) && ce->ce_cid == cid) {
			if (o != e) {
				if (o <= buf)
					break;
				*(--o) = '/';
			}
			if (o - ce->ce_nlen <= buf)
				break;
			o -= ce->ce_nlen;
			memcpy(o, ce->ce_name, ce->ce_nlen);
			P(&svp) = ce->ce_pvp;
			cid = ce->ce_pcid;
		} else
			break;
	} while (1/*CONSTCOND*/);
	*e = '\0';
	*name = o;

	if (e == o)
		return (2);

	KDEREF(kd, &svp);
	return (D(&svp, vnode)->v_flag & VROOT);
}

void
load_name_cache(kvm_t *kd)
{
	struct namecache n, *tmp;
	struct namecache_head nchead;

	LIST_INIT(&lcache);
	_KDEREF(kd, nclruhead_addr, &nchead, sizeof(nchead));
	tmp = TAILQ_FIRST(&nchead);
	while (tmp != NULL) {
		_KDEREF(kd, (u_long)tmp, &n, sizeof(n));

		if (n.nc_nlen > 0) {
			if (n.nc_nlen > 2 ||
			    n.nc_name[0] != '.' ||
			    (n.nc_nlen != 1 && n.nc_name[1] != '.'))
				cache_enter(&n);
		}
		tmp = TAILQ_NEXT(&n, nc_lru);
	}

	namecache_loaded = 1;
}

void
cache_enter(struct namecache *ncp)
{
	struct cache_entry *ce;

	if (debug & DUMP_NAMEI_CACHE)
		printf("ncp->nc_vp %10p, ncp->nc_dvp %10p, ncp->nc_nlen "
		    "%3d [%.*s] (nc_dvpid=%lu, nc_vpid=%lu)\n",
		    ncp->nc_vp, ncp->nc_dvp,
		    ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name,
		    ncp->nc_dvpid, ncp->nc_vpid);

	ce = malloc(sizeof(struct cache_entry));
	if (ce == NULL)
		err(1, "cache_enter");

	ce->ce_vp = ncp->nc_vp;
	ce->ce_pvp = ncp->nc_dvp;
	ce->ce_cid = ncp->nc_vpid;
	ce->ce_pcid = ncp->nc_dvpid;
	ce->ce_nlen = (unsigned)ncp->nc_nlen;
	strlcpy(ce->ce_name, ncp->nc_name, sizeof(ce->ce_name));

	LIST_INSERT_HEAD(&lcache, ce, ce_next);
}

static void __dead
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-AadlmPsv] [-D number] "
	    "[-M core] [-N system] [-p pid] [pid ...]\n",
	    __progname);
	exit(1);
}

static pid_t
strtopid(const char *str)
{
	pid_t pid;

	errno = 0;
	pid = (pid_t)strtonum(str, 0, INT_MAX, NULL);
	if (errno != 0)
		usage();
	return (pid);
}
