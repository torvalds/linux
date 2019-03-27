/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Dell EMC
 * Copyright (c) 2007 Sandvine Incorporated
 * Copyright (c) 1998 John D. Polstra
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>
#include <sys/procfs.h>
#include <sys/ptrace.h>
#include <sys/queue.h>
#include <sys/linker_set.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <machine/elf.h>
#include <vm/vm_param.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libutil.h>

#include "extern.h"

/*
 * Code for generating ELF core dumps.
 */

typedef void (*segment_callback)(vm_map_entry_t, void *);

/* Closure for cb_put_phdr(). */
struct phdr_closure {
	Elf_Phdr *phdr;		/* Program header to fill in */
	Elf_Off offset;		/* Offset of segment in core file */
};

/* Closure for cb_size_segment(). */
struct sseg_closure {
	int count;		/* Count of writable segments. */
	size_t size;		/* Total size of all writable segments. */
};

#ifdef ELFCORE_COMPAT_32
typedef struct fpreg32 elfcore_fpregset_t;
typedef struct reg32   elfcore_gregset_t;
typedef struct prpsinfo32 elfcore_prpsinfo_t;
typedef struct prstatus32 elfcore_prstatus_t;
typedef struct ptrace_lwpinfo32 elfcore_lwpinfo_t;
static void elf_convert_gregset(elfcore_gregset_t *rd, struct reg *rs);
static void elf_convert_fpregset(elfcore_fpregset_t *rd, struct fpreg *rs);
static void elf_convert_lwpinfo(struct ptrace_lwpinfo32 *pld,
    struct ptrace_lwpinfo *pls);
#else
typedef fpregset_t elfcore_fpregset_t;
typedef gregset_t  elfcore_gregset_t;
typedef prpsinfo_t elfcore_prpsinfo_t;
typedef prstatus_t elfcore_prstatus_t;
typedef struct ptrace_lwpinfo elfcore_lwpinfo_t;
#define elf_convert_gregset(d,s)	*d = *s
#define elf_convert_fpregset(d,s)	*d = *s
#define	elf_convert_lwpinfo(d,s)	*d = *s
#endif

typedef void* (*notefunc_t)(void *, size_t *);

static void cb_put_phdr(vm_map_entry_t, void *);
static void cb_size_segment(vm_map_entry_t, void *);
static void each_dumpable_segment(vm_map_entry_t, segment_callback,
    void *closure);
static void elf_detach(void);	/* atexit() handler. */
static void *elf_note_fpregset(void *, size_t *);
static void *elf_note_prpsinfo(void *, size_t *);
static void *elf_note_prstatus(void *, size_t *);
static void *elf_note_thrmisc(void *, size_t *);
static void *elf_note_ptlwpinfo(void *, size_t *);
#if defined(__arm__)
static void *elf_note_arm_vfp(void *, size_t *);
#endif
#if defined(__i386__) || defined(__amd64__)
static void *elf_note_x86_xstate(void *, size_t *);
#endif
#if defined(__powerpc__)
static void *elf_note_powerpc_vmx(void *, size_t *);
static void *elf_note_powerpc_vsx(void *, size_t *);
#endif
static void *elf_note_procstat_auxv(void *, size_t *);
static void *elf_note_procstat_files(void *, size_t *);
static void *elf_note_procstat_groups(void *, size_t *);
static void *elf_note_procstat_osrel(void *, size_t *);
static void *elf_note_procstat_proc(void *, size_t *);
static void *elf_note_procstat_psstrings(void *, size_t *);
static void *elf_note_procstat_rlimit(void *, size_t *);
static void *elf_note_procstat_umask(void *, size_t *);
static void *elf_note_procstat_vmmap(void *, size_t *);
static void elf_puthdr(int, pid_t, vm_map_entry_t, void *, size_t, size_t,
    size_t, int);
static void elf_putnote(int, notefunc_t, void *, struct sbuf *);
static void elf_putnotes(pid_t, struct sbuf *, size_t *);
static void freemap(vm_map_entry_t);
static vm_map_entry_t readmap(pid_t);
static void *procstat_sysctl(void *, int, size_t, size_t *sizep);

static pid_t g_pid;		/* Pid being dumped, global for elf_detach */
static int g_status;		/* proc status after ptrace attach */

static int
elf_ident(int efd, pid_t pid __unused, char *binfile __unused)
{
	Elf_Ehdr hdr;
	int cnt;
	uint16_t machine;

	cnt = read(efd, &hdr, sizeof(hdr));
	if (cnt != sizeof(hdr))
		return (0);
	if (!IS_ELF(hdr))
		return (0);
	switch (hdr.e_ident[EI_DATA]) {
	case ELFDATA2LSB:
		machine = le16toh(hdr.e_machine);
		break;
	case ELFDATA2MSB:
		machine = be16toh(hdr.e_machine);
		break;
	default:
		return (0);
	}
	if (!ELF_MACHINE_OK(machine))
		return (0);

	/* Looks good. */
	return (1);
}

static void
elf_detach(void)
{
	int sig;

	if (g_pid != 0) {
		/*
		 * Forward any pending signals. SIGSTOP is generated by ptrace
		 * itself, so ignore it.
		 */
		sig = WIFSTOPPED(g_status) ? WSTOPSIG(g_status) : 0;
		if (sig == SIGSTOP)
			sig = 0;
		ptrace(PT_DETACH, g_pid, (caddr_t)1, sig);
	}
}

/*
 * Write an ELF coredump for the given pid to the given fd.
 */
static void
elf_coredump(int efd, int fd, pid_t pid)
{
	vm_map_entry_t map;
	struct sseg_closure seginfo;
	struct sbuf *sb;
	void *hdr;
	size_t hdrsize, notesz, segoff;
	ssize_t n, old_len;
	Elf_Phdr *php;
	int i;

	/* Attach to process to dump. */
	g_pid = pid;
	if (atexit(elf_detach) != 0)
		err(1, "atexit");
	errno = 0;
	ptrace(PT_ATTACH, pid, NULL, 0);
	if (errno)
		err(1, "PT_ATTACH");
	if (waitpid(pid, &g_status, 0) == -1)
		err(1, "waitpid");

	/* Get the program's memory map. */
	map = readmap(pid);

	/* Size the program segments. */
	seginfo.count = 0;
	seginfo.size = 0;
	each_dumpable_segment(map, cb_size_segment, &seginfo);

	/*
	 * Build the header and the notes using sbuf and write to the file.
	 */
	sb = sbuf_new_auto();
	hdrsize = sizeof(Elf_Ehdr) + sizeof(Elf_Phdr) * (1 + seginfo.count);
	if (seginfo.count + 1 >= PN_XNUM)
		hdrsize += sizeof(Elf_Shdr);
	/* Start header + notes section. */
	sbuf_start_section(sb, NULL);
	/* Make empty header subsection. */
	sbuf_start_section(sb, &old_len);
	sbuf_putc(sb, 0);
	sbuf_end_section(sb, old_len, hdrsize, 0);
	/* Put notes. */
	elf_putnotes(pid, sb, &notesz);
	/* Align up to a page boundary for the program segments. */
	sbuf_end_section(sb, -1, PAGE_SIZE, 0);
	if (sbuf_finish(sb) != 0)
		err(1, "sbuf_finish");
	hdr = sbuf_data(sb);
	segoff = sbuf_len(sb);
	/* Fill in the header. */
	elf_puthdr(efd, pid, map, hdr, hdrsize, notesz, segoff, seginfo.count);

	n = write(fd, hdr, segoff);
	if (n == -1)
		err(1, "write");
	if (n < segoff)
              errx(1, "short write");

	/* Write the contents of all of the writable segments. */
	php = (Elf_Phdr *)((char *)hdr + sizeof(Elf_Ehdr)) + 1;
	for (i = 0;  i < seginfo.count;  i++) {
		struct ptrace_io_desc iorequest;
		uintmax_t nleft = php->p_filesz;

		iorequest.piod_op = PIOD_READ_D;
		iorequest.piod_offs = (caddr_t)(uintptr_t)php->p_vaddr;
		while (nleft > 0) {
			char buf[8*1024];
			size_t nwant;
			ssize_t ngot;

			if (nleft > sizeof(buf))
				nwant = sizeof buf;
			else
				nwant = nleft;
			iorequest.piod_addr = buf;
			iorequest.piod_len = nwant;
			ptrace(PT_IO, pid, (caddr_t)&iorequest, 0);
			ngot = iorequest.piod_len;
			if ((size_t)ngot < nwant)
				errx(1, "short read wanted %zu, got %zd",
				    nwant, ngot);
			ngot = write(fd, buf, nwant);
			if (ngot == -1)
				err(1, "write of segment %d failed", i);
			if ((size_t)ngot != nwant)
				errx(1, "short write");
			nleft -= nwant;
			iorequest.piod_offs += ngot;
		}
		php++;
	}
	sbuf_delete(sb);
	freemap(map);
}

/*
 * A callback for each_dumpable_segment() to write out the segment's
 * program header entry.
 */
static void
cb_put_phdr(vm_map_entry_t entry, void *closure)
{
	struct phdr_closure *phc = (struct phdr_closure *)closure;
	Elf_Phdr *phdr = phc->phdr;

	phc->offset = round_page(phc->offset);

	phdr->p_type = PT_LOAD;
	phdr->p_offset = phc->offset;
	phdr->p_vaddr = entry->start;
	phdr->p_paddr = 0;
	phdr->p_filesz = phdr->p_memsz = entry->end - entry->start;
	phdr->p_align = PAGE_SIZE;
	phdr->p_flags = 0;
	if (entry->protection & VM_PROT_READ)
		phdr->p_flags |= PF_R;
	if (entry->protection & VM_PROT_WRITE)
		phdr->p_flags |= PF_W;
	if (entry->protection & VM_PROT_EXECUTE)
		phdr->p_flags |= PF_X;

	phc->offset += phdr->p_filesz;
	phc->phdr++;
}

/*
 * A callback for each_dumpable_segment() to gather information about
 * the number of segments and their total size.
 */
static void
cb_size_segment(vm_map_entry_t entry, void *closure)
{
	struct sseg_closure *ssc = (struct sseg_closure *)closure;

	ssc->count++;
	ssc->size += entry->end - entry->start;
}

/*
 * For each segment in the given memory map, call the given function
 * with a pointer to the map entry and some arbitrary caller-supplied
 * data.
 */
static void
each_dumpable_segment(vm_map_entry_t map, segment_callback func, void *closure)
{
	vm_map_entry_t entry;

	for (entry = map;  entry != NULL;  entry = entry->next)
		(*func)(entry, closure);
}

static void
elf_putnotes(pid_t pid, struct sbuf *sb, size_t *sizep)
{
	lwpid_t *tids;
	size_t threads, old_len;
	ssize_t size;
	int i;

	errno = 0;
	threads = ptrace(PT_GETNUMLWPS, pid, NULL, 0);
	if (errno)
		err(1, "PT_GETNUMLWPS");
	tids = malloc(threads * sizeof(*tids));
	if (tids == NULL)
		errx(1, "out of memory");
	errno = 0;
	ptrace(PT_GETLWPLIST, pid, (void *)tids, threads);
	if (errno)
		err(1, "PT_GETLWPLIST");

	sbuf_start_section(sb, &old_len);
	elf_putnote(NT_PRPSINFO, elf_note_prpsinfo, &pid, sb);

	for (i = 0; i < threads; ++i) {
		elf_putnote(NT_PRSTATUS, elf_note_prstatus, tids + i, sb);
		elf_putnote(NT_FPREGSET, elf_note_fpregset, tids + i, sb);
		elf_putnote(NT_THRMISC, elf_note_thrmisc, tids + i, sb);
		elf_putnote(NT_PTLWPINFO, elf_note_ptlwpinfo, tids + i, sb);
#if defined(__arm__)
		elf_putnote(NT_ARM_VFP, elf_note_arm_vfp, tids + i, sb);
#endif
#if defined(__i386__) || defined(__amd64__)
		elf_putnote(NT_X86_XSTATE, elf_note_x86_xstate, tids + i, sb);
#endif
#if defined(__powerpc__)
		elf_putnote(NT_PPC_VMX, elf_note_powerpc_vmx, tids + i, sb);
		elf_putnote(NT_PPC_VSX, elf_note_powerpc_vsx, tids + i, sb);
#endif
	}

#ifndef ELFCORE_COMPAT_32
	elf_putnote(NT_PROCSTAT_PROC, elf_note_procstat_proc, &pid, sb);
	elf_putnote(NT_PROCSTAT_FILES, elf_note_procstat_files, &pid, sb);
	elf_putnote(NT_PROCSTAT_VMMAP, elf_note_procstat_vmmap, &pid, sb);
	elf_putnote(NT_PROCSTAT_GROUPS, elf_note_procstat_groups, &pid, sb);
	elf_putnote(NT_PROCSTAT_UMASK, elf_note_procstat_umask, &pid, sb);
	elf_putnote(NT_PROCSTAT_RLIMIT, elf_note_procstat_rlimit, &pid, sb);
	elf_putnote(NT_PROCSTAT_OSREL, elf_note_procstat_osrel, &pid, sb);
	elf_putnote(NT_PROCSTAT_PSSTRINGS, elf_note_procstat_psstrings, &pid,
	    sb);
	elf_putnote(NT_PROCSTAT_AUXV, elf_note_procstat_auxv, &pid, sb);
#endif

	size = sbuf_end_section(sb, old_len, 1, 0);
	if (size == -1)
		err(1, "sbuf_end_section");
	free(tids);
	*sizep = size;
}

/*
 * Emit one note section to sbuf.
 */
static void
elf_putnote(int type, notefunc_t notefunc, void *arg, struct sbuf *sb)
{
	Elf_Note note;
	size_t descsz;
	ssize_t old_len;
	void *desc;

	desc = notefunc(arg, &descsz);
	note.n_namesz = 8; /* strlen("FreeBSD") + 1 */
	note.n_descsz = descsz;
	note.n_type = type;

	sbuf_bcat(sb, &note, sizeof(note));
	sbuf_start_section(sb, &old_len);
	sbuf_bcat(sb, "FreeBSD", note.n_namesz);
	sbuf_end_section(sb, old_len, sizeof(Elf32_Size), 0);
	if (descsz == 0)
		return;
	sbuf_start_section(sb, &old_len);
	sbuf_bcat(sb, desc, descsz);
	sbuf_end_section(sb, old_len, sizeof(Elf32_Size), 0);
	free(desc);
}

/*
 * Generate the ELF coredump header.
 */
static void
elf_puthdr(int efd, pid_t pid, vm_map_entry_t map, void *hdr, size_t hdrsize,
    size_t notesz, size_t segoff, int numsegs)
{
	Elf_Ehdr *ehdr, binhdr;
	Elf_Phdr *phdr;
	Elf_Shdr *shdr;
	struct phdr_closure phc;
	ssize_t cnt;

	cnt = read(efd, &binhdr, sizeof(binhdr));
	if (cnt < 0)
		err(1, "Failed to re-read ELF header");
	else if (cnt != sizeof(binhdr))
		errx(1, "Failed to re-read ELF header");

	ehdr = (Elf_Ehdr *)hdr;

	ehdr->e_ident[EI_MAG0] = ELFMAG0;
	ehdr->e_ident[EI_MAG1] = ELFMAG1;
	ehdr->e_ident[EI_MAG2] = ELFMAG2;
	ehdr->e_ident[EI_MAG3] = ELFMAG3;
	ehdr->e_ident[EI_CLASS] = ELF_CLASS;
	ehdr->e_ident[EI_DATA] = ELF_DATA;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;
	ehdr->e_ident[EI_ABIVERSION] = 0;
	ehdr->e_ident[EI_PAD] = 0;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = binhdr.e_machine;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = 0;
	ehdr->e_phoff = sizeof(Elf_Ehdr);
	ehdr->e_flags = binhdr.e_flags;
	ehdr->e_ehsize = sizeof(Elf_Ehdr);
	ehdr->e_phentsize = sizeof(Elf_Phdr);
	ehdr->e_shentsize = sizeof(Elf_Shdr);
	ehdr->e_shstrndx = SHN_UNDEF;
	if (numsegs + 1 < PN_XNUM) {
		ehdr->e_phnum = numsegs + 1;
		ehdr->e_shnum = 0;
	} else {
		ehdr->e_phnum = PN_XNUM;
		ehdr->e_shnum = 1;

		ehdr->e_shoff = ehdr->e_phoff +
		    (numsegs + 1) * ehdr->e_phentsize;

		shdr = (Elf_Shdr *)((char *)hdr + ehdr->e_shoff);
		memset(shdr, 0, sizeof(*shdr));
		/*
		 * A special first section is used to hold large segment and
		 * section counts.  This was proposed by Sun Microsystems in
		 * Solaris and has been adopted by Linux; the standard ELF
		 * tools are already familiar with the technique.
		 *
		 * See table 7-7 of the Solaris "Linker and Libraries Guide"
		 * (or 12-7 depending on the version of the document) for more
		 * details.
		 */
		shdr->sh_type = SHT_NULL;
		shdr->sh_size = ehdr->e_shnum;
		shdr->sh_link = ehdr->e_shstrndx;
		shdr->sh_info = numsegs + 1;
	}

	/*
	 * Fill in the program header entries.
	 */
	phdr = (Elf_Phdr *)((char *)hdr + ehdr->e_phoff);

	/* The note segement. */
	phdr->p_type = PT_NOTE;
	phdr->p_offset = hdrsize;
	phdr->p_vaddr = 0;
	phdr->p_paddr = 0;
	phdr->p_filesz = notesz;
	phdr->p_memsz = 0;
	phdr->p_flags = PF_R;
	phdr->p_align = sizeof(Elf32_Size);
	phdr++;

	/* All the writable segments from the program. */
	phc.phdr = phdr;
	phc.offset = segoff;
	each_dumpable_segment(map, cb_put_phdr, &phc);
}

/*
 * Free the memory map.
 */
static void
freemap(vm_map_entry_t map)
{

	while (map != NULL) {
		vm_map_entry_t next = map->next;
		free(map);
		map = next;
	}
}

/*
 * Read the process's memory map using kinfo_getvmmap(), and return a list of
 * VM map entries.  Only the non-device read/writable segments are
 * returned.  The map entries in the list aren't fully filled in; only
 * the items we need are present.
 */
static vm_map_entry_t
readmap(pid_t pid)
{
	vm_map_entry_t ent, *linkp, map;
	struct kinfo_vmentry *vmentl, *kve;
	int i, nitems;

	vmentl = kinfo_getvmmap(pid, &nitems);
	if (vmentl == NULL)
		err(1, "cannot retrieve mappings for %u process", pid);

	map = NULL;
	linkp = &map;
	for (i = 0; i < nitems; i++) {
		kve = &vmentl[i];

		/*
		 * Ignore 'malformed' segments or ones representing memory
		 * mapping with MAP_NOCORE on.
		 * If the 'full' support is disabled, just dump the most
		 * meaningful data segments.
		 */
		if ((kve->kve_protection & KVME_PROT_READ) == 0 ||
		    (kve->kve_flags & KVME_FLAG_NOCOREDUMP) != 0 ||
		    kve->kve_type == KVME_TYPE_DEAD ||
		    kve->kve_type == KVME_TYPE_UNKNOWN ||
		    ((pflags & PFLAGS_FULL) == 0 &&
		    kve->kve_type != KVME_TYPE_DEFAULT &&
		    kve->kve_type != KVME_TYPE_VNODE &&
		    kve->kve_type != KVME_TYPE_SWAP &&
		    kve->kve_type != KVME_TYPE_PHYS))
			continue;

		ent = calloc(1, sizeof(*ent));
		if (ent == NULL)
			errx(1, "out of memory");
		ent->start = (vm_offset_t)kve->kve_start;
		ent->end = (vm_offset_t)kve->kve_end;
		ent->protection = VM_PROT_READ | VM_PROT_WRITE;
		if ((kve->kve_protection & KVME_PROT_EXEC) != 0)
			ent->protection |= VM_PROT_EXECUTE;

		*linkp = ent;
		linkp = &ent->next;
	}
	free(vmentl);
	return (map);
}

/*
 * Miscellaneous note out functions.
 */

static void *
elf_note_prpsinfo(void *arg, size_t *sizep)
{
	char *cp, *end;
	pid_t pid;
	elfcore_prpsinfo_t *psinfo;
	struct kinfo_proc kip;
	size_t len;
	int name[4];

	pid = *(pid_t *)arg;
	psinfo = calloc(1, sizeof(*psinfo));
	if (psinfo == NULL)
		errx(1, "out of memory");
	psinfo->pr_version = PRPSINFO_VERSION;
	psinfo->pr_psinfosz = sizeof(*psinfo);

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_PID;
	name[3] = pid;
	len = sizeof(kip);
	if (sysctl(name, 4, &kip, &len, NULL, 0) == -1)
		err(1, "kern.proc.pid.%u", pid);
	if (kip.ki_pid != pid)
		err(1, "kern.proc.pid.%u", pid);
	strlcpy(psinfo->pr_fname, kip.ki_comm, sizeof(psinfo->pr_fname));
	name[2] = KERN_PROC_ARGS;
	len = sizeof(psinfo->pr_psargs) - 1;
	if (sysctl(name, 4, psinfo->pr_psargs, &len, NULL, 0) == 0 && len > 0) {
		cp = psinfo->pr_psargs;
		end = cp + len - 1;
		for (;;) {
			cp = memchr(cp, '\0', end - cp);
			if (cp == NULL)
				break;
			*cp = ' ';
		}
	} else
		strlcpy(psinfo->pr_psargs, kip.ki_comm,
		    sizeof(psinfo->pr_psargs));
	psinfo->pr_pid = pid;

	*sizep = sizeof(*psinfo);
	return (psinfo);
}

static void *
elf_note_prstatus(void *arg, size_t *sizep)
{
	lwpid_t tid;
	elfcore_prstatus_t *status;
	struct reg greg;

	tid = *(lwpid_t *)arg;
	status = calloc(1, sizeof(*status));
	if (status == NULL)
		errx(1, "out of memory");
	status->pr_version = PRSTATUS_VERSION;
	status->pr_statussz = sizeof(*status);
	status->pr_gregsetsz = sizeof(elfcore_gregset_t);
	status->pr_fpregsetsz = sizeof(elfcore_fpregset_t);
	status->pr_osreldate = __FreeBSD_version;
	status->pr_pid = tid;
	ptrace(PT_GETREGS, tid, (void *)&greg, 0);
	elf_convert_gregset(&status->pr_reg, &greg);

	*sizep = sizeof(*status);
	return (status);
}

static void *
elf_note_fpregset(void *arg, size_t *sizep)
{
	lwpid_t tid;
	elfcore_fpregset_t *fpregset;
	fpregset_t fpreg;

	tid = *(lwpid_t *)arg;
	fpregset = calloc(1, sizeof(*fpregset));
	if (fpregset == NULL)
		errx(1, "out of memory");
	ptrace(PT_GETFPREGS, tid, (void *)&fpreg, 0);
	elf_convert_fpregset(fpregset, &fpreg);

	*sizep = sizeof(*fpregset);
	return (fpregset);
}

static void *
elf_note_thrmisc(void *arg, size_t *sizep)
{
	lwpid_t tid;
	struct ptrace_lwpinfo lwpinfo;
	thrmisc_t *thrmisc;

	tid = *(lwpid_t *)arg;
	thrmisc = calloc(1, sizeof(*thrmisc));
	if (thrmisc == NULL)
		errx(1, "out of memory");
	ptrace(PT_LWPINFO, tid, (void *)&lwpinfo,
	    sizeof(lwpinfo));
	memset(&thrmisc->_pad, 0, sizeof(thrmisc->_pad));
	strcpy(thrmisc->pr_tname, lwpinfo.pl_tdname);

	*sizep = sizeof(*thrmisc);
	return (thrmisc);
}

static void *
elf_note_ptlwpinfo(void *arg, size_t *sizep)
{
	lwpid_t tid;
	elfcore_lwpinfo_t *elf_info;
	struct ptrace_lwpinfo lwpinfo;
	void *p;

	tid = *(lwpid_t *)arg;
	p = calloc(1, sizeof(int) + sizeof(elfcore_lwpinfo_t));
	if (p == NULL)
		errx(1, "out of memory");
	*(int *)p = sizeof(elfcore_lwpinfo_t);
	elf_info = (void *)((int *)p + 1);
	ptrace(PT_LWPINFO, tid, (void *)&lwpinfo, sizeof(lwpinfo));
	elf_convert_lwpinfo(elf_info, &lwpinfo);

	*sizep = sizeof(int) + sizeof(struct ptrace_lwpinfo);
	return (p);
}

#if defined(__arm__)
static void *
elf_note_arm_vfp(void *arg, size_t *sizep)
{
	lwpid_t tid;
	struct vfpreg *vfp;
	static bool has_vfp = true;
	struct vfpreg info;

	tid = *(lwpid_t *)arg;
	if (has_vfp) {
		if (ptrace(PT_GETVFPREGS, tid, (void *)&info, 0) != 0)
			has_vfp = false;
	}
	if (!has_vfp) {
		*sizep = 0;
		return (NULL);
	}
	vfp = calloc(1, sizeof(*vfp));
	memcpy(vfp, &info, sizeof(*vfp));
	*sizep = sizeof(*vfp);
	return (vfp);
}
#endif

#if defined(__i386__) || defined(__amd64__)
static void *
elf_note_x86_xstate(void *arg, size_t *sizep)
{
	lwpid_t tid;
	char *xstate;
	static bool xsave_checked = false;
	static struct ptrace_xstate_info info;

	tid = *(lwpid_t *)arg;
	if (!xsave_checked) {
		if (ptrace(PT_GETXSTATE_INFO, tid, (void *)&info,
		    sizeof(info)) != 0)
			info.xsave_len = 0;
		xsave_checked = true;
	}
	if (info.xsave_len == 0) {
		*sizep = 0;
		return (NULL);
	}
	xstate = calloc(1, info.xsave_len);
	ptrace(PT_GETXSTATE, tid, xstate, 0);
	*(uint64_t *)(xstate + X86_XSTATE_XCR0_OFFSET) = info.xsave_mask;
	*sizep = info.xsave_len;
	return (xstate);
}
#endif

#if defined(__powerpc__)
static void *
elf_note_powerpc_vmx(void *arg, size_t *sizep)
{
	lwpid_t tid;
	struct vmxreg *vmx;
	static bool has_vmx = true;
	struct vmxreg info;

	tid = *(lwpid_t *)arg;
	if (has_vmx) {
		if (ptrace(PT_GETVRREGS, tid, (void *)&info,
		    sizeof(info)) != 0)
			has_vmx = false;
	}
	if (!has_vmx) {
		*sizep = 0;
		return (NULL);
	}
	vmx = calloc(1, sizeof(*vmx));
	memcpy(vmx, &info, sizeof(*vmx));
	*sizep = sizeof(*vmx);
	return (vmx);
}

static void *
elf_note_powerpc_vsx(void *arg, size_t *sizep)
{
	lwpid_t tid;
	char *vshr_data;
	static bool has_vsx = true;
	uint64_t vshr[32];

	tid = *(lwpid_t *)arg;
	if (has_vsx) {
		if (ptrace(PT_GETVSRREGS, tid, (void *)vshr,
		    sizeof(vshr)) != 0)
			has_vsx = false;
	}
	if (!has_vsx) {
		*sizep = 0;
		return (NULL);
	}
	vshr_data = calloc(1, sizeof(vshr));
	memcpy(vshr_data, vshr, sizeof(vshr));
	*sizep = sizeof(vshr);
	return (vshr_data);
}
#endif

static void *
procstat_sysctl(void *arg, int what, size_t structsz, size_t *sizep)
{
	size_t len;
	pid_t pid;
	int name[4], structsize;
	void *buf, *p;

	pid = *(pid_t *)arg;
	structsize = structsz;
	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = what;
	name[3] = pid;
	len = 0;
	if (sysctl(name, 4, NULL, &len, NULL, 0) == -1)
		err(1, "kern.proc.%d.%u", what, pid);
	buf = calloc(1, sizeof(structsize) + len * 4 / 3);
	if (buf == NULL)
		errx(1, "out of memory");
	bcopy(&structsize, buf, sizeof(structsize));
	p = (char *)buf + sizeof(structsize);
	if (sysctl(name, 4, p, &len, NULL, 0) == -1)
		err(1, "kern.proc.%d.%u", what, pid);

	*sizep = sizeof(structsize) + len;
	return (buf);
}

static void *
elf_note_procstat_proc(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_PID | KERN_PROC_INC_THREAD,
	    sizeof(struct kinfo_proc), sizep));
}

static void *
elf_note_procstat_files(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_FILEDESC,
	    sizeof(struct kinfo_file), sizep));
}

static void *
elf_note_procstat_vmmap(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_VMMAP,
	    sizeof(struct kinfo_vmentry), sizep));
}

static void *
elf_note_procstat_groups(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_GROUPS, sizeof(gid_t), sizep));
}

static void *
elf_note_procstat_umask(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_UMASK, sizeof(u_short), sizep));
}

static void *
elf_note_procstat_osrel(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_OSREL, sizeof(int), sizep));
}

static void *
elf_note_procstat_psstrings(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_PS_STRINGS,
	    sizeof(vm_offset_t), sizep));
}

static void *
elf_note_procstat_auxv(void *arg, size_t *sizep)
{

	return (procstat_sysctl(arg, KERN_PROC_AUXV,
	    sizeof(Elf_Auxinfo), sizep));
}

static void *
elf_note_procstat_rlimit(void *arg, size_t *sizep)
{
	pid_t pid;
	size_t len;
	int i, name[5], structsize;
	void *buf, *p;

	pid = *(pid_t *)arg;
	structsize = sizeof(struct rlimit) * RLIM_NLIMITS;
	buf = calloc(1, sizeof(structsize) + structsize);
	if (buf == NULL)
		errx(1, "out of memory");
	bcopy(&structsize, buf, sizeof(structsize));
	p = (char *)buf + sizeof(structsize);
	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_RLIMIT;
	name[3] = pid;
	len = sizeof(struct rlimit);
	for (i = 0; i < RLIM_NLIMITS; i++) {
		name[4] = i;
		if (sysctl(name, 5, p, &len, NULL, 0) == -1)
			err(1, "kern.proc.rlimit.%u", pid);
		if (len != sizeof(struct rlimit))
			errx(1, "kern.proc.rlimit.%u: short read", pid);
		p += len;
	}

	*sizep = sizeof(structsize) + structsize;
	return (buf);
}

struct dumpers __elfN(dump) = { elf_ident, elf_coredump };
TEXT_SET(dumpset, __elfN(dump));
