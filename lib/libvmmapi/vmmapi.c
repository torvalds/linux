/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/_iovec.h>
#include <sys/cpuset.h>

#include <x86/segments.h>
#include <machine/specialreg.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <libutil.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "vmmapi.h"

#define	MB	(1024 * 1024UL)
#define	GB	(1024 * 1024 * 1024UL)

/*
 * Size of the guard region before and after the virtual address space
 * mapping the guest physical memory. This must be a multiple of the
 * superpage size for performance reasons.
 */
#define	VM_MMAP_GUARD_SIZE	(4 * MB)

#define	PROT_RW		(PROT_READ | PROT_WRITE)
#define	PROT_ALL	(PROT_READ | PROT_WRITE | PROT_EXEC)

struct vmctx {
	int	fd;
	uint32_t lowmem_limit;
	int	memflags;
	size_t	lowmem;
	size_t	highmem;
	char	*baseaddr;
	char	*name;
};

#define	CREATE(x)  sysctlbyname("hw.vmm.create", NULL, NULL, (x), strlen((x)))
#define	DESTROY(x) sysctlbyname("hw.vmm.destroy", NULL, NULL, (x), strlen((x)))

static int
vm_device_open(const char *name)
{
	int fd, len;
	char *vmfile;

	len = strlen("/dev/vmm/") + strlen(name) + 1;
	vmfile = malloc(len);
	assert(vmfile != NULL);
	snprintf(vmfile, len, "/dev/vmm/%s", name);

	/* Open the device file */
	fd = open(vmfile, O_RDWR, 0);

	free(vmfile);
	return (fd);
}

int
vm_create(const char *name)
{

	return (CREATE((char *)name));
}

struct vmctx *
vm_open(const char *name)
{
	struct vmctx *vm;

	vm = malloc(sizeof(struct vmctx) + strlen(name) + 1);
	assert(vm != NULL);

	vm->fd = -1;
	vm->memflags = 0;
	vm->lowmem_limit = 3 * GB;
	vm->name = (char *)(vm + 1);
	strcpy(vm->name, name);

	if ((vm->fd = vm_device_open(vm->name)) < 0)
		goto err;

	return (vm);
err:
	vm_destroy(vm);
	return (NULL);
}

void
vm_destroy(struct vmctx *vm)
{
	assert(vm != NULL);

	if (vm->fd >= 0)
		close(vm->fd);
	DESTROY(vm->name);

	free(vm);
}

int
vm_parse_memsize(const char *optarg, size_t *ret_memsize)
{
	char *endptr;
	size_t optval;
	int error;

	optval = strtoul(optarg, &endptr, 0);
	if (*optarg != '\0' && *endptr == '\0') {
		/*
		 * For the sake of backward compatibility if the memory size
		 * specified on the command line is less than a megabyte then
		 * it is interpreted as being in units of MB.
		 */
		if (optval < MB)
			optval *= MB;
		*ret_memsize = optval;
		error = 0;
	} else
		error = expand_number(optarg, ret_memsize);

	return (error);
}

uint32_t
vm_get_lowmem_limit(struct vmctx *ctx)
{

	return (ctx->lowmem_limit);
}

void
vm_set_lowmem_limit(struct vmctx *ctx, uint32_t limit)
{

	ctx->lowmem_limit = limit;
}

void
vm_set_memflags(struct vmctx *ctx, int flags)
{

	ctx->memflags = flags;
}

int
vm_get_memflags(struct vmctx *ctx)
{

	return (ctx->memflags);
}

/*
 * Map segment 'segid' starting at 'off' into guest address range [gpa,gpa+len).
 */
int
vm_mmap_memseg(struct vmctx *ctx, vm_paddr_t gpa, int segid, vm_ooffset_t off,
    size_t len, int prot)
{
	struct vm_memmap memmap;
	int error, flags;

	memmap.gpa = gpa;
	memmap.segid = segid;
	memmap.segoff = off;
	memmap.len = len;
	memmap.prot = prot;
	memmap.flags = 0;

	if (ctx->memflags & VM_MEM_F_WIRED)
		memmap.flags |= VM_MEMMAP_F_WIRED;

	/*
	 * If this mapping already exists then don't create it again. This
	 * is the common case for SYSMEM mappings created by bhyveload(8).
	 */
	error = vm_mmap_getnext(ctx, &gpa, &segid, &off, &len, &prot, &flags);
	if (error == 0 && gpa == memmap.gpa) {
		if (segid != memmap.segid || off != memmap.segoff ||
		    prot != memmap.prot || flags != memmap.flags) {
			errno = EEXIST;
			return (-1);
		} else {
			return (0);
		}
	}

	error = ioctl(ctx->fd, VM_MMAP_MEMSEG, &memmap);
	return (error);
}

int
vm_mmap_getnext(struct vmctx *ctx, vm_paddr_t *gpa, int *segid,
    vm_ooffset_t *segoff, size_t *len, int *prot, int *flags)
{
	struct vm_memmap memmap;
	int error;

	bzero(&memmap, sizeof(struct vm_memmap));
	memmap.gpa = *gpa;
	error = ioctl(ctx->fd, VM_MMAP_GETNEXT, &memmap);
	if (error == 0) {
		*gpa = memmap.gpa;
		*segid = memmap.segid;
		*segoff = memmap.segoff;
		*len = memmap.len;
		*prot = memmap.prot;
		*flags = memmap.flags;
	}
	return (error);
}

/*
 * Return 0 if the segments are identical and non-zero otherwise.
 *
 * This is slightly complicated by the fact that only device memory segments
 * are named.
 */
static int
cmpseg(size_t len, const char *str, size_t len2, const char *str2)
{

	if (len == len2) {
		if ((!str && !str2) || (str && str2 && !strcmp(str, str2)))
			return (0);
	}
	return (-1);
}

static int
vm_alloc_memseg(struct vmctx *ctx, int segid, size_t len, const char *name)
{
	struct vm_memseg memseg;
	size_t n;
	int error;

	/*
	 * If the memory segment has already been created then just return.
	 * This is the usual case for the SYSMEM segment created by userspace
	 * loaders like bhyveload(8).
	 */
	error = vm_get_memseg(ctx, segid, &memseg.len, memseg.name,
	    sizeof(memseg.name));
	if (error)
		return (error);

	if (memseg.len != 0) {
		if (cmpseg(len, name, memseg.len, VM_MEMSEG_NAME(&memseg))) {
			errno = EINVAL;
			return (-1);
		} else {
			return (0);
		}
	}

	bzero(&memseg, sizeof(struct vm_memseg));
	memseg.segid = segid;
	memseg.len = len;
	if (name != NULL) {
		n = strlcpy(memseg.name, name, sizeof(memseg.name));
		if (n >= sizeof(memseg.name)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
	}

	error = ioctl(ctx->fd, VM_ALLOC_MEMSEG, &memseg);
	return (error);
}

int
vm_get_memseg(struct vmctx *ctx, int segid, size_t *lenp, char *namebuf,
    size_t bufsize)
{
	struct vm_memseg memseg;
	size_t n;
	int error;

	memseg.segid = segid;
	error = ioctl(ctx->fd, VM_GET_MEMSEG, &memseg);
	if (error == 0) {
		*lenp = memseg.len;
		n = strlcpy(namebuf, memseg.name, bufsize);
		if (n >= bufsize) {
			errno = ENAMETOOLONG;
			error = -1;
		}
	}
	return (error);
}

static int
setup_memory_segment(struct vmctx *ctx, vm_paddr_t gpa, size_t len, char *base)
{
	char *ptr;
	int error, flags;

	/* Map 'len' bytes starting at 'gpa' in the guest address space */
	error = vm_mmap_memseg(ctx, gpa, VM_SYSMEM, gpa, len, PROT_ALL);
	if (error)
		return (error);

	flags = MAP_SHARED | MAP_FIXED;
	if ((ctx->memflags & VM_MEM_F_INCORE) == 0)
		flags |= MAP_NOCORE;

	/* mmap into the process address space on the host */
	ptr = mmap(base + gpa, len, PROT_RW, flags, ctx->fd, gpa);
	if (ptr == MAP_FAILED)
		return (-1);

	return (0);
}

int
vm_setup_memory(struct vmctx *ctx, size_t memsize, enum vm_mmap_style vms)
{
	size_t objsize, len;
	vm_paddr_t gpa;
	char *baseaddr, *ptr;
	int error;

	assert(vms == VM_MMAP_ALL);

	/*
	 * If 'memsize' cannot fit entirely in the 'lowmem' segment then
	 * create another 'highmem' segment above 4GB for the remainder.
	 */
	if (memsize > ctx->lowmem_limit) {
		ctx->lowmem = ctx->lowmem_limit;
		ctx->highmem = memsize - ctx->lowmem_limit;
		objsize = 4*GB + ctx->highmem;
	} else {
		ctx->lowmem = memsize;
		ctx->highmem = 0;
		objsize = ctx->lowmem;
	}

	error = vm_alloc_memseg(ctx, VM_SYSMEM, objsize, NULL);
	if (error)
		return (error);

	/*
	 * Stake out a contiguous region covering the guest physical memory
	 * and the adjoining guard regions.
	 */
	len = VM_MMAP_GUARD_SIZE + objsize + VM_MMAP_GUARD_SIZE;
	ptr = mmap(NULL, len, PROT_NONE, MAP_GUARD | MAP_ALIGNED_SUPER, -1, 0);
	if (ptr == MAP_FAILED)
		return (-1);

	baseaddr = ptr + VM_MMAP_GUARD_SIZE;
	if (ctx->highmem > 0) {
		gpa = 4*GB;
		len = ctx->highmem;
		error = setup_memory_segment(ctx, gpa, len, baseaddr);
		if (error)
			return (error);
	}

	if (ctx->lowmem > 0) {
		gpa = 0;
		len = ctx->lowmem;
		error = setup_memory_segment(ctx, gpa, len, baseaddr);
		if (error)
			return (error);
	}

	ctx->baseaddr = baseaddr;

	return (0);
}

/*
 * Returns a non-NULL pointer if [gaddr, gaddr+len) is entirely contained in
 * the lowmem or highmem regions.
 *
 * In particular return NULL if [gaddr, gaddr+len) falls in guest MMIO region.
 * The instruction emulation code depends on this behavior.
 */
void *
vm_map_gpa(struct vmctx *ctx, vm_paddr_t gaddr, size_t len)
{

	if (ctx->lowmem > 0) {
		if (gaddr < ctx->lowmem && len <= ctx->lowmem &&
		    gaddr + len <= ctx->lowmem)
			return (ctx->baseaddr + gaddr);
	}

	if (ctx->highmem > 0) {
                if (gaddr >= 4*GB) {
			if (gaddr < 4*GB + ctx->highmem &&
			    len <= ctx->highmem &&
			    gaddr + len <= 4*GB + ctx->highmem)
				return (ctx->baseaddr + gaddr);
		}
	}

	return (NULL);
}

size_t
vm_get_lowmem_size(struct vmctx *ctx)
{

	return (ctx->lowmem);
}

size_t
vm_get_highmem_size(struct vmctx *ctx)
{

	return (ctx->highmem);
}

void *
vm_create_devmem(struct vmctx *ctx, int segid, const char *name, size_t len)
{
	char pathname[MAXPATHLEN];
	size_t len2;
	char *base, *ptr;
	int fd, error, flags;

	fd = -1;
	ptr = MAP_FAILED;
	if (name == NULL || strlen(name) == 0) {
		errno = EINVAL;
		goto done;
	}

	error = vm_alloc_memseg(ctx, segid, len, name);
	if (error)
		goto done;

	strlcpy(pathname, "/dev/vmm.io/", sizeof(pathname));
	strlcat(pathname, ctx->name, sizeof(pathname));
	strlcat(pathname, ".", sizeof(pathname));
	strlcat(pathname, name, sizeof(pathname));

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		goto done;

	/*
	 * Stake out a contiguous region covering the device memory and the
	 * adjoining guard regions.
	 */
	len2 = VM_MMAP_GUARD_SIZE + len + VM_MMAP_GUARD_SIZE;
	base = mmap(NULL, len2, PROT_NONE, MAP_GUARD | MAP_ALIGNED_SUPER, -1,
	    0);
	if (base == MAP_FAILED)
		goto done;

	flags = MAP_SHARED | MAP_FIXED;
	if ((ctx->memflags & VM_MEM_F_INCORE) == 0)
		flags |= MAP_NOCORE;

	/* mmap the devmem region in the host address space */
	ptr = mmap(base + VM_MMAP_GUARD_SIZE, len, PROT_RW, flags, fd, 0);
done:
	if (fd >= 0)
		close(fd);
	return (ptr);
}

int
vm_set_desc(struct vmctx *ctx, int vcpu, int reg,
	    uint64_t base, uint32_t limit, uint32_t access)
{
	int error;
	struct vm_seg_desc vmsegdesc;

	bzero(&vmsegdesc, sizeof(vmsegdesc));
	vmsegdesc.cpuid = vcpu;
	vmsegdesc.regnum = reg;
	vmsegdesc.desc.base = base;
	vmsegdesc.desc.limit = limit;
	vmsegdesc.desc.access = access;

	error = ioctl(ctx->fd, VM_SET_SEGMENT_DESCRIPTOR, &vmsegdesc);
	return (error);
}

int
vm_get_desc(struct vmctx *ctx, int vcpu, int reg,
	    uint64_t *base, uint32_t *limit, uint32_t *access)
{
	int error;
	struct vm_seg_desc vmsegdesc;

	bzero(&vmsegdesc, sizeof(vmsegdesc));
	vmsegdesc.cpuid = vcpu;
	vmsegdesc.regnum = reg;

	error = ioctl(ctx->fd, VM_GET_SEGMENT_DESCRIPTOR, &vmsegdesc);
	if (error == 0) {
		*base = vmsegdesc.desc.base;
		*limit = vmsegdesc.desc.limit;
		*access = vmsegdesc.desc.access;
	}
	return (error);
}

int
vm_get_seg_desc(struct vmctx *ctx, int vcpu, int reg, struct seg_desc *seg_desc)
{
	int error;

	error = vm_get_desc(ctx, vcpu, reg, &seg_desc->base, &seg_desc->limit,
	    &seg_desc->access);
	return (error);
}

int
vm_set_register(struct vmctx *ctx, int vcpu, int reg, uint64_t val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;
	vmreg.regval = val;

	error = ioctl(ctx->fd, VM_SET_REGISTER, &vmreg);
	return (error);
}

int
vm_get_register(struct vmctx *ctx, int vcpu, int reg, uint64_t *ret_val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;

	error = ioctl(ctx->fd, VM_GET_REGISTER, &vmreg);
	*ret_val = vmreg.regval;
	return (error);
}

int
vm_set_register_set(struct vmctx *ctx, int vcpu, unsigned int count,
    const int *regnums, uint64_t *regvals)
{
	int error;
	struct vm_register_set vmregset;

	bzero(&vmregset, sizeof(vmregset));
	vmregset.cpuid = vcpu;
	vmregset.count = count;
	vmregset.regnums = regnums;
	vmregset.regvals = regvals;

	error = ioctl(ctx->fd, VM_SET_REGISTER_SET, &vmregset);
	return (error);
}

int
vm_get_register_set(struct vmctx *ctx, int vcpu, unsigned int count,
    const int *regnums, uint64_t *regvals)
{
	int error;
	struct vm_register_set vmregset;

	bzero(&vmregset, sizeof(vmregset));
	vmregset.cpuid = vcpu;
	vmregset.count = count;
	vmregset.regnums = regnums;
	vmregset.regvals = regvals;

	error = ioctl(ctx->fd, VM_GET_REGISTER_SET, &vmregset);
	return (error);
}

int
vm_run(struct vmctx *ctx, int vcpu, struct vm_exit *vmexit)
{
	int error;
	struct vm_run vmrun;

	bzero(&vmrun, sizeof(vmrun));
	vmrun.cpuid = vcpu;

	error = ioctl(ctx->fd, VM_RUN, &vmrun);
	bcopy(&vmrun.vm_exit, vmexit, sizeof(struct vm_exit));
	return (error);
}

int
vm_suspend(struct vmctx *ctx, enum vm_suspend_how how)
{
	struct vm_suspend vmsuspend;

	bzero(&vmsuspend, sizeof(vmsuspend));
	vmsuspend.how = how;
	return (ioctl(ctx->fd, VM_SUSPEND, &vmsuspend));
}

int
vm_reinit(struct vmctx *ctx)
{

	return (ioctl(ctx->fd, VM_REINIT, 0));
}

int
vm_inject_exception(struct vmctx *ctx, int vcpu, int vector, int errcode_valid,
    uint32_t errcode, int restart_instruction)
{
	struct vm_exception exc;

	exc.cpuid = vcpu;
	exc.vector = vector;
	exc.error_code = errcode;
	exc.error_code_valid = errcode_valid;
	exc.restart_instruction = restart_instruction;

	return (ioctl(ctx->fd, VM_INJECT_EXCEPTION, &exc));
}

int
vm_apicid2vcpu(struct vmctx *ctx, int apicid)
{
	/*
	 * The apic id associated with the 'vcpu' has the same numerical value
	 * as the 'vcpu' itself.
	 */
	return (apicid);
}

int
vm_lapic_irq(struct vmctx *ctx, int vcpu, int vector)
{
	struct vm_lapic_irq vmirq;

	bzero(&vmirq, sizeof(vmirq));
	vmirq.cpuid = vcpu;
	vmirq.vector = vector;

	return (ioctl(ctx->fd, VM_LAPIC_IRQ, &vmirq));
}

int
vm_lapic_local_irq(struct vmctx *ctx, int vcpu, int vector)
{
	struct vm_lapic_irq vmirq;

	bzero(&vmirq, sizeof(vmirq));
	vmirq.cpuid = vcpu;
	vmirq.vector = vector;

	return (ioctl(ctx->fd, VM_LAPIC_LOCAL_IRQ, &vmirq));
}

int
vm_lapic_msi(struct vmctx *ctx, uint64_t addr, uint64_t msg)
{
	struct vm_lapic_msi vmmsi;

	bzero(&vmmsi, sizeof(vmmsi));
	vmmsi.addr = addr;
	vmmsi.msg = msg;

	return (ioctl(ctx->fd, VM_LAPIC_MSI, &vmmsi));
}

int
vm_ioapic_assert_irq(struct vmctx *ctx, int irq)
{
	struct vm_ioapic_irq ioapic_irq;

	bzero(&ioapic_irq, sizeof(struct vm_ioapic_irq));
	ioapic_irq.irq = irq;

	return (ioctl(ctx->fd, VM_IOAPIC_ASSERT_IRQ, &ioapic_irq));
}

int
vm_ioapic_deassert_irq(struct vmctx *ctx, int irq)
{
	struct vm_ioapic_irq ioapic_irq;

	bzero(&ioapic_irq, sizeof(struct vm_ioapic_irq));
	ioapic_irq.irq = irq;

	return (ioctl(ctx->fd, VM_IOAPIC_DEASSERT_IRQ, &ioapic_irq));
}

int
vm_ioapic_pulse_irq(struct vmctx *ctx, int irq)
{
	struct vm_ioapic_irq ioapic_irq;

	bzero(&ioapic_irq, sizeof(struct vm_ioapic_irq));
	ioapic_irq.irq = irq;

	return (ioctl(ctx->fd, VM_IOAPIC_PULSE_IRQ, &ioapic_irq));
}

int
vm_ioapic_pincount(struct vmctx *ctx, int *pincount)
{

	return (ioctl(ctx->fd, VM_IOAPIC_PINCOUNT, pincount));
}

int
vm_isa_assert_irq(struct vmctx *ctx, int atpic_irq, int ioapic_irq)
{
	struct vm_isa_irq isa_irq;

	bzero(&isa_irq, sizeof(struct vm_isa_irq));
	isa_irq.atpic_irq = atpic_irq;
	isa_irq.ioapic_irq = ioapic_irq;

	return (ioctl(ctx->fd, VM_ISA_ASSERT_IRQ, &isa_irq));
}

int
vm_isa_deassert_irq(struct vmctx *ctx, int atpic_irq, int ioapic_irq)
{
	struct vm_isa_irq isa_irq;

	bzero(&isa_irq, sizeof(struct vm_isa_irq));
	isa_irq.atpic_irq = atpic_irq;
	isa_irq.ioapic_irq = ioapic_irq;

	return (ioctl(ctx->fd, VM_ISA_DEASSERT_IRQ, &isa_irq));
}

int
vm_isa_pulse_irq(struct vmctx *ctx, int atpic_irq, int ioapic_irq)
{
	struct vm_isa_irq isa_irq;

	bzero(&isa_irq, sizeof(struct vm_isa_irq));
	isa_irq.atpic_irq = atpic_irq;
	isa_irq.ioapic_irq = ioapic_irq;

	return (ioctl(ctx->fd, VM_ISA_PULSE_IRQ, &isa_irq));
}

int
vm_isa_set_irq_trigger(struct vmctx *ctx, int atpic_irq,
    enum vm_intr_trigger trigger)
{
	struct vm_isa_irq_trigger isa_irq_trigger;

	bzero(&isa_irq_trigger, sizeof(struct vm_isa_irq_trigger));
	isa_irq_trigger.atpic_irq = atpic_irq;
	isa_irq_trigger.trigger = trigger;

	return (ioctl(ctx->fd, VM_ISA_SET_IRQ_TRIGGER, &isa_irq_trigger));
}

int
vm_inject_nmi(struct vmctx *ctx, int vcpu)
{
	struct vm_nmi vmnmi;

	bzero(&vmnmi, sizeof(vmnmi));
	vmnmi.cpuid = vcpu;

	return (ioctl(ctx->fd, VM_INJECT_NMI, &vmnmi));
}

static struct {
	const char	*name;
	int		type;
} capstrmap[] = {
	{ "hlt_exit",		VM_CAP_HALT_EXIT },
	{ "mtrap_exit",		VM_CAP_MTRAP_EXIT },
	{ "pause_exit",		VM_CAP_PAUSE_EXIT },
	{ "unrestricted_guest",	VM_CAP_UNRESTRICTED_GUEST },
	{ "enable_invpcid",	VM_CAP_ENABLE_INVPCID },
	{ 0 }
};

int
vm_capability_name2type(const char *capname)
{
	int i;

	for (i = 0; capstrmap[i].name != NULL && capname != NULL; i++) {
		if (strcmp(capstrmap[i].name, capname) == 0)
			return (capstrmap[i].type);
	}

	return (-1);
}

const char *
vm_capability_type2name(int type)
{
	int i;

	for (i = 0; capstrmap[i].name != NULL; i++) {
		if (capstrmap[i].type == type)
			return (capstrmap[i].name);
	}

	return (NULL);
}

int
vm_get_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
		  int *retval)
{
	int error;
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;

	error = ioctl(ctx->fd, VM_GET_CAPABILITY, &vmcap);
	*retval = vmcap.capval;
	return (error);
}

int
vm_set_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap, int val)
{
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;
	vmcap.capval = val;

	return (ioctl(ctx->fd, VM_SET_CAPABILITY, &vmcap));
}

int
vm_assign_pptdev(struct vmctx *ctx, int bus, int slot, int func)
{
	struct vm_pptdev pptdev;

	bzero(&pptdev, sizeof(pptdev));
	pptdev.bus = bus;
	pptdev.slot = slot;
	pptdev.func = func;

	return (ioctl(ctx->fd, VM_BIND_PPTDEV, &pptdev));
}

int
vm_unassign_pptdev(struct vmctx *ctx, int bus, int slot, int func)
{
	struct vm_pptdev pptdev;

	bzero(&pptdev, sizeof(pptdev));
	pptdev.bus = bus;
	pptdev.slot = slot;
	pptdev.func = func;

	return (ioctl(ctx->fd, VM_UNBIND_PPTDEV, &pptdev));
}

int
vm_map_pptdev_mmio(struct vmctx *ctx, int bus, int slot, int func,
		   vm_paddr_t gpa, size_t len, vm_paddr_t hpa)
{
	struct vm_pptdev_mmio pptmmio;

	bzero(&pptmmio, sizeof(pptmmio));
	pptmmio.bus = bus;
	pptmmio.slot = slot;
	pptmmio.func = func;
	pptmmio.gpa = gpa;
	pptmmio.len = len;
	pptmmio.hpa = hpa;

	return (ioctl(ctx->fd, VM_MAP_PPTDEV_MMIO, &pptmmio));
}

int
vm_setup_pptdev_msi(struct vmctx *ctx, int vcpu, int bus, int slot, int func,
    uint64_t addr, uint64_t msg, int numvec)
{
	struct vm_pptdev_msi pptmsi;

	bzero(&pptmsi, sizeof(pptmsi));
	pptmsi.vcpu = vcpu;
	pptmsi.bus = bus;
	pptmsi.slot = slot;
	pptmsi.func = func;
	pptmsi.msg = msg;
	pptmsi.addr = addr;
	pptmsi.numvec = numvec;

	return (ioctl(ctx->fd, VM_PPTDEV_MSI, &pptmsi));
}

int	
vm_setup_pptdev_msix(struct vmctx *ctx, int vcpu, int bus, int slot, int func,
    int idx, uint64_t addr, uint64_t msg, uint32_t vector_control)
{
	struct vm_pptdev_msix pptmsix;

	bzero(&pptmsix, sizeof(pptmsix));
	pptmsix.vcpu = vcpu;
	pptmsix.bus = bus;
	pptmsix.slot = slot;
	pptmsix.func = func;
	pptmsix.idx = idx;
	pptmsix.msg = msg;
	pptmsix.addr = addr;
	pptmsix.vector_control = vector_control;

	return ioctl(ctx->fd, VM_PPTDEV_MSIX, &pptmsix);
}

uint64_t *
vm_get_stats(struct vmctx *ctx, int vcpu, struct timeval *ret_tv,
	     int *ret_entries)
{
	int error;

	static struct vm_stats vmstats;

	vmstats.cpuid = vcpu;

	error = ioctl(ctx->fd, VM_STATS, &vmstats);
	if (error == 0) {
		if (ret_entries)
			*ret_entries = vmstats.num_entries;
		if (ret_tv)
			*ret_tv = vmstats.tv;
		return (vmstats.statbuf);
	} else
		return (NULL);
}

const char *
vm_get_stat_desc(struct vmctx *ctx, int index)
{
	static struct vm_stat_desc statdesc;

	statdesc.index = index;
	if (ioctl(ctx->fd, VM_STAT_DESC, &statdesc) == 0)
		return (statdesc.desc);
	else
		return (NULL);
}

int
vm_get_x2apic_state(struct vmctx *ctx, int vcpu, enum x2apic_state *state)
{
	int error;
	struct vm_x2apic x2apic;

	bzero(&x2apic, sizeof(x2apic));
	x2apic.cpuid = vcpu;

	error = ioctl(ctx->fd, VM_GET_X2APIC_STATE, &x2apic);
	*state = x2apic.state;
	return (error);
}

int
vm_set_x2apic_state(struct vmctx *ctx, int vcpu, enum x2apic_state state)
{
	int error;
	struct vm_x2apic x2apic;

	bzero(&x2apic, sizeof(x2apic));
	x2apic.cpuid = vcpu;
	x2apic.state = state;

	error = ioctl(ctx->fd, VM_SET_X2APIC_STATE, &x2apic);

	return (error);
}

/*
 * From Intel Vol 3a:
 * Table 9-1. IA-32 Processor States Following Power-up, Reset or INIT
 */
int
vcpu_reset(struct vmctx *vmctx, int vcpu)
{
	int error;
	uint64_t rflags, rip, cr0, cr4, zero, desc_base, rdx;
	uint32_t desc_access, desc_limit;
	uint16_t sel;

	zero = 0;

	rflags = 0x2;
	error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RFLAGS, rflags);
	if (error)
		goto done;

	rip = 0xfff0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RIP, rip)) != 0)
		goto done;

	cr0 = CR0_NE;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR0, cr0)) != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR3, zero)) != 0)
		goto done;
	
	cr4 = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR4, cr4)) != 0)
		goto done;

	/*
	 * CS: present, r/w, accessed, 16-bit, byte granularity, usable
	 */
	desc_base = 0xffff0000;
	desc_limit = 0xffff;
	desc_access = 0x0093;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_CS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	sel = 0xf000;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CS, sel)) != 0)
		goto done;

	/*
	 * SS,DS,ES,FS,GS: present, r/w, accessed, 16-bit, byte granularity
	 */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0x0093;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_SS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_DS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_ES,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_FS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	sel = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_SS, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_DS, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_ES, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_FS, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_GS, sel)) != 0)
		goto done;

	/* General purpose registers */
	rdx = 0xf00;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RAX, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RBX, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RCX, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RDX, rdx)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RSI, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RDI, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RBP, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RSP, zero)) != 0)
		goto done;

	/* GDTR, IDTR */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GDTR,
			    desc_base, desc_limit, desc_access);
	if (error != 0)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_IDTR,
			    desc_base, desc_limit, desc_access);
	if (error != 0)
		goto done;

	/* TR */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0x0000008b;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_TR, 0, 0, desc_access);
	if (error)
		goto done;

	sel = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_TR, sel)) != 0)
		goto done;

	/* LDTR */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0x00000082;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_LDTR, desc_base,
			    desc_limit, desc_access);
	if (error)
		goto done;

	sel = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_LDTR, 0)) != 0)
		goto done;

	/* XXX cr2, debug registers */

	error = 0;
done:
	return (error);
}

int
vm_get_gpa_pmap(struct vmctx *ctx, uint64_t gpa, uint64_t *pte, int *num)
{
	int error, i;
	struct vm_gpa_pte gpapte;

	bzero(&gpapte, sizeof(gpapte));
	gpapte.gpa = gpa;

	error = ioctl(ctx->fd, VM_GET_GPA_PMAP, &gpapte);

	if (error == 0) {
		*num = gpapte.ptenum;
		for (i = 0; i < gpapte.ptenum; i++)
			pte[i] = gpapte.pte[i];
	}

	return (error);
}

int
vm_get_hpet_capabilities(struct vmctx *ctx, uint32_t *capabilities)
{
	int error;
	struct vm_hpet_cap cap;

	bzero(&cap, sizeof(struct vm_hpet_cap));
	error = ioctl(ctx->fd, VM_GET_HPET_CAPABILITIES, &cap);
	if (capabilities != NULL)
		*capabilities = cap.capabilities;
	return (error);
}

int
vm_gla2gpa(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint64_t gla, int prot, uint64_t *gpa, int *fault)
{
	struct vm_gla2gpa gg;
	int error;

	bzero(&gg, sizeof(struct vm_gla2gpa));
	gg.vcpuid = vcpu;
	gg.prot = prot;
	gg.gla = gla;
	gg.paging = *paging;

	error = ioctl(ctx->fd, VM_GLA2GPA, &gg);
	if (error == 0) {
		*fault = gg.fault;
		*gpa = gg.gpa;
	}
	return (error);
}

int
vm_gla2gpa_nofault(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint64_t gla, int prot, uint64_t *gpa, int *fault)
{
	struct vm_gla2gpa gg;
	int error;

	bzero(&gg, sizeof(struct vm_gla2gpa));
	gg.vcpuid = vcpu;
	gg.prot = prot;
	gg.gla = gla;
	gg.paging = *paging;

	error = ioctl(ctx->fd, VM_GLA2GPA_NOFAULT, &gg);
	if (error == 0) {
		*fault = gg.fault;
		*gpa = gg.gpa;
	}
	return (error);
}

#ifndef min
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#endif

int
vm_copy_setup(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint64_t gla, size_t len, int prot, struct iovec *iov, int iovcnt,
    int *fault)
{
	void *va;
	uint64_t gpa;
	int error, i, n, off;

	for (i = 0; i < iovcnt; i++) {
		iov[i].iov_base = 0;
		iov[i].iov_len = 0;
	}

	while (len) {
		assert(iovcnt > 0);
		error = vm_gla2gpa(ctx, vcpu, paging, gla, prot, &gpa, fault);
		if (error || *fault)
			return (error);

		off = gpa & PAGE_MASK;
		n = min(len, PAGE_SIZE - off);

		va = vm_map_gpa(ctx, gpa, n);
		if (va == NULL)
			return (EFAULT);

		iov->iov_base = va;
		iov->iov_len = n;
		iov++;
		iovcnt--;

		gla += n;
		len -= n;
	}
	return (0);
}

void
vm_copy_teardown(struct vmctx *ctx, int vcpu, struct iovec *iov, int iovcnt)
{

	return;
}

void
vm_copyin(struct vmctx *ctx, int vcpu, struct iovec *iov, void *vp, size_t len)
{
	const char *src;
	char *dst;
	size_t n;

	dst = vp;
	while (len) {
		assert(iov->iov_len);
		n = min(len, iov->iov_len);
		src = iov->iov_base;
		bcopy(src, dst, n);

		iov++;
		dst += n;
		len -= n;
	}
}

void
vm_copyout(struct vmctx *ctx, int vcpu, const void *vp, struct iovec *iov,
    size_t len)
{
	const char *src;
	char *dst;
	size_t n;

	src = vp;
	while (len) {
		assert(iov->iov_len);
		n = min(len, iov->iov_len);
		dst = iov->iov_base;
		bcopy(src, dst, n);

		iov++;
		src += n;
		len -= n;
	}
}

static int
vm_get_cpus(struct vmctx *ctx, int which, cpuset_t *cpus)
{
	struct vm_cpuset vm_cpuset;
	int error;

	bzero(&vm_cpuset, sizeof(struct vm_cpuset));
	vm_cpuset.which = which;
	vm_cpuset.cpusetsize = sizeof(cpuset_t);
	vm_cpuset.cpus = cpus;

	error = ioctl(ctx->fd, VM_GET_CPUS, &vm_cpuset);
	return (error);
}

int
vm_active_cpus(struct vmctx *ctx, cpuset_t *cpus)
{

	return (vm_get_cpus(ctx, VM_ACTIVE_CPUS, cpus));
}

int
vm_suspended_cpus(struct vmctx *ctx, cpuset_t *cpus)
{

	return (vm_get_cpus(ctx, VM_SUSPENDED_CPUS, cpus));
}

int
vm_debug_cpus(struct vmctx *ctx, cpuset_t *cpus)
{

	return (vm_get_cpus(ctx, VM_DEBUG_CPUS, cpus));
}

int
vm_activate_cpu(struct vmctx *ctx, int vcpu)
{
	struct vm_activate_cpu ac;
	int error;

	bzero(&ac, sizeof(struct vm_activate_cpu));
	ac.vcpuid = vcpu;
	error = ioctl(ctx->fd, VM_ACTIVATE_CPU, &ac);
	return (error);
}

int
vm_suspend_cpu(struct vmctx *ctx, int vcpu)
{
	struct vm_activate_cpu ac;
	int error;

	bzero(&ac, sizeof(struct vm_activate_cpu));
	ac.vcpuid = vcpu;
	error = ioctl(ctx->fd, VM_SUSPEND_CPU, &ac);
	return (error);
}

int
vm_resume_cpu(struct vmctx *ctx, int vcpu)
{
	struct vm_activate_cpu ac;
	int error;

	bzero(&ac, sizeof(struct vm_activate_cpu));
	ac.vcpuid = vcpu;
	error = ioctl(ctx->fd, VM_RESUME_CPU, &ac);
	return (error);
}

int
vm_get_intinfo(struct vmctx *ctx, int vcpu, uint64_t *info1, uint64_t *info2)
{
	struct vm_intinfo vmii;
	int error;

	bzero(&vmii, sizeof(struct vm_intinfo));
	vmii.vcpuid = vcpu;
	error = ioctl(ctx->fd, VM_GET_INTINFO, &vmii);
	if (error == 0) {
		*info1 = vmii.info1;
		*info2 = vmii.info2;
	}
	return (error);
}

int
vm_set_intinfo(struct vmctx *ctx, int vcpu, uint64_t info1)
{
	struct vm_intinfo vmii;
	int error;

	bzero(&vmii, sizeof(struct vm_intinfo));
	vmii.vcpuid = vcpu;
	vmii.info1 = info1;
	error = ioctl(ctx->fd, VM_SET_INTINFO, &vmii);
	return (error);
}

int
vm_rtc_write(struct vmctx *ctx, int offset, uint8_t value)
{
	struct vm_rtc_data rtcdata;
	int error;

	bzero(&rtcdata, sizeof(struct vm_rtc_data));
	rtcdata.offset = offset;
	rtcdata.value = value;
	error = ioctl(ctx->fd, VM_RTC_WRITE, &rtcdata);
	return (error);
}

int
vm_rtc_read(struct vmctx *ctx, int offset, uint8_t *retval)
{
	struct vm_rtc_data rtcdata;
	int error;

	bzero(&rtcdata, sizeof(struct vm_rtc_data));
	rtcdata.offset = offset;
	error = ioctl(ctx->fd, VM_RTC_READ, &rtcdata);
	if (error == 0)
		*retval = rtcdata.value;
	return (error);
}

int
vm_rtc_settime(struct vmctx *ctx, time_t secs)
{
	struct vm_rtc_time rtctime;
	int error;

	bzero(&rtctime, sizeof(struct vm_rtc_time));
	rtctime.secs = secs;
	error = ioctl(ctx->fd, VM_RTC_SETTIME, &rtctime);
	return (error);
}

int
vm_rtc_gettime(struct vmctx *ctx, time_t *secs)
{
	struct vm_rtc_time rtctime;
	int error;

	bzero(&rtctime, sizeof(struct vm_rtc_time));
	error = ioctl(ctx->fd, VM_RTC_GETTIME, &rtctime);
	if (error == 0)
		*secs = rtctime.secs;
	return (error);
}

int
vm_restart_instruction(void *arg, int vcpu)
{
	struct vmctx *ctx = arg;

	return (ioctl(ctx->fd, VM_RESTART_INSTRUCTION, &vcpu));
}

int
vm_set_topology(struct vmctx *ctx,
    uint16_t sockets, uint16_t cores, uint16_t threads, uint16_t maxcpus)
{
	struct vm_cpu_topology topology;

	bzero(&topology, sizeof (struct vm_cpu_topology));
	topology.sockets = sockets;
	topology.cores = cores;
	topology.threads = threads;
	topology.maxcpus = maxcpus;
	return (ioctl(ctx->fd, VM_SET_TOPOLOGY, &topology));
}

int
vm_get_topology(struct vmctx *ctx,
    uint16_t *sockets, uint16_t *cores, uint16_t *threads, uint16_t *maxcpus)
{
	struct vm_cpu_topology topology;
	int error;

	bzero(&topology, sizeof (struct vm_cpu_topology));
	error = ioctl(ctx->fd, VM_GET_TOPOLOGY, &topology);
	if (error == 0) {
		*sockets = topology.sockets;
		*cores = topology.cores;
		*threads = topology.threads;
		*maxcpus = topology.maxcpus;
	}
	return (error);
}

int
vm_get_device_fd(struct vmctx *ctx)
{

	return (ctx->fd);
}

const cap_ioctl_t *
vm_get_ioctls(size_t *len)
{
	cap_ioctl_t *cmds;
	/* keep in sync with machine/vmm_dev.h */
	static const cap_ioctl_t vm_ioctl_cmds[] = { VM_RUN, VM_SUSPEND, VM_REINIT,
	    VM_ALLOC_MEMSEG, VM_GET_MEMSEG, VM_MMAP_MEMSEG, VM_MMAP_MEMSEG,
	    VM_MMAP_GETNEXT, VM_SET_REGISTER, VM_GET_REGISTER,
	    VM_SET_SEGMENT_DESCRIPTOR, VM_GET_SEGMENT_DESCRIPTOR,
	    VM_SET_REGISTER_SET, VM_GET_REGISTER_SET,
	    VM_INJECT_EXCEPTION, VM_LAPIC_IRQ, VM_LAPIC_LOCAL_IRQ,
	    VM_LAPIC_MSI, VM_IOAPIC_ASSERT_IRQ, VM_IOAPIC_DEASSERT_IRQ,
	    VM_IOAPIC_PULSE_IRQ, VM_IOAPIC_PINCOUNT, VM_ISA_ASSERT_IRQ,
	    VM_ISA_DEASSERT_IRQ, VM_ISA_PULSE_IRQ, VM_ISA_SET_IRQ_TRIGGER,
	    VM_SET_CAPABILITY, VM_GET_CAPABILITY, VM_BIND_PPTDEV,
	    VM_UNBIND_PPTDEV, VM_MAP_PPTDEV_MMIO, VM_PPTDEV_MSI,
	    VM_PPTDEV_MSIX, VM_INJECT_NMI, VM_STATS, VM_STAT_DESC,
	    VM_SET_X2APIC_STATE, VM_GET_X2APIC_STATE,
	    VM_GET_HPET_CAPABILITIES, VM_GET_GPA_PMAP, VM_GLA2GPA,
	    VM_GLA2GPA_NOFAULT,
	    VM_ACTIVATE_CPU, VM_GET_CPUS, VM_SUSPEND_CPU, VM_RESUME_CPU,
	    VM_SET_INTINFO, VM_GET_INTINFO,
	    VM_RTC_WRITE, VM_RTC_READ, VM_RTC_SETTIME, VM_RTC_GETTIME,
	    VM_RESTART_INSTRUCTION, VM_SET_TOPOLOGY, VM_GET_TOPOLOGY };

	if (len == NULL) {
		cmds = malloc(sizeof(vm_ioctl_cmds));
		if (cmds == NULL)
			return (NULL);
		bcopy(vm_ioctl_cmds, cmds, sizeof(vm_ioctl_cmds));
		return (cmds);
	}

	*len = nitems(vm_ioctl_cmds);
	return (NULL);
}
