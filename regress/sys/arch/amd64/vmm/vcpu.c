/*	$OpenBSD: vcpu.c,v 1.9 2025/05/22 15:00:32 bluhm Exp $	*/

/*
 * Copyright (c) 2022 Dave Voutila <dv@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <machine/specialreg.h>
#include <machine/vmmvar.h>

#include <dev/vmm/vmm.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KIB		1024
#define MIB		(1UL << 20)
#define GIB		(1024 * MIB)
#define VMM_NODE	"/dev/vmm"

#define LOW_MEM		0
#define UPPER_MEM	1

#define PCKBC_AUX	0x61
#define PCJR_DISKCTRL	0xF0

const char 		*VM_NAME = "regress";

const uint8_t PUSHW_DX[] = { 0x66, 0x52 };		 // pushw %dx
const uint8_t INS[] = { 0x6C };				 // ins es:[di],dx
const uint8_t IN_PCJR[] = { 0xE4, 0xF0 };		 // in 0xF0

/* Originally from vmd(8)'s vm.c */
const struct vcpu_reg_state vcpu_init_flat16 = {
	.vrs_gprs[VCPU_REGS_RFLAGS] = 0x2,
	.vrs_gprs[VCPU_REGS_RIP] = 0xFFF0,
	.vrs_gprs[VCPU_REGS_RDX] = PCKBC_AUX,	/* Port used by INS */
	.vrs_gprs[VCPU_REGS_RSP] =  0x800,	/* Set our stack in low mem. */
	.vrs_crs[VCPU_REGS_CR0] = 0x60000010,
	.vrs_sregs[VCPU_REGS_CS] = { 0xF000, 0xFFFF, 0x0093, 0xFFFF0000},
	.vrs_sregs[VCPU_REGS_DS] = { 0x0, 0xFFFF, 0x0093, 0x0},
	.vrs_sregs[VCPU_REGS_ES] = { 0x0, 0xFFFF, 0x0093, 0x0},
	.vrs_sregs[VCPU_REGS_FS] = { 0x0, 0xFFFF, 0x0093, 0x0},
	.vrs_sregs[VCPU_REGS_GS] = { 0x0, 0xFFFF, 0x0093, 0x0},
	.vrs_sregs[VCPU_REGS_SS] = { 0x0, 0xFFFF, 0x0093, 0x0},
	.vrs_gdtr = { 0x0, 0xFFFF, 0x0082, 0x0},
	.vrs_idtr = { 0x0, 0xFFFF, 0x0082, 0x0},
	.vrs_sregs[VCPU_REGS_LDTR] = { 0x0, 0xFFFF, 0x0082, 0x0},
	.vrs_sregs[VCPU_REGS_TR] = { 0x0, 0xFFFF, 0x008B, 0x0},
	.vrs_drs[VCPU_REGS_DR6] = 0xFFFF0FF0,
	.vrs_drs[VCPU_REGS_DR7] = 0x400,
	.vrs_crs[VCPU_REGS_XCR0] = XFEATURE_X87,
};

struct intr_handler {
	uint16_t	offset;
	uint16_t	segment;
};

const struct intr_handler ivt[256] = {
	[VMM_EX_GP] = { .segment = 0x0, .offset = 0x0B5D },
};

int
main(int argc, char **argv)
{
	struct vm_create_params		 vcp;
	struct vm_exit			*exit = NULL;
	struct vm_info_params		 vip;
	struct vm_info_result		*info = NULL, *ours = NULL;
	struct vm_resetcpu_params	 vresetp;
	struct vm_run_params		 vrunp;
	struct vm_terminate_params	 vtp;
	struct vm_sharemem_params	 vsp;

	struct vm_mem_range		*vmr;
	int				 fd, ret = 1;
	size_t				 i;
	off_t				 off, reset = 0xFFFFFFF0, stack = 0x800;
	void				*p;

	fd = open(VMM_NODE, O_RDWR);
	if (fd == -1)
		err(1, "open %s", VMM_NODE);

	/*
	 * 1. Create our VM with 1 vcpu and 64 MiB of memory.
	 */
	memset(&vcp, 0, sizeof(vcp));
	strlcpy(vcp.vcp_name, VM_NAME, sizeof(vcp.vcp_name));
	vcp.vcp_ncpus = 1;

	/* Split into two ranges, similar to how vmd(8) might do it. */
	vcp.vcp_nmemranges = 2;
	vcp.vcp_memranges[LOW_MEM].vmr_gpa = 0x0;
	vcp.vcp_memranges[LOW_MEM].vmr_size = 640 * KIB;
	vcp.vcp_memranges[UPPER_MEM].vmr_size = (64 * MIB) - (640 * KIB);
	vcp.vcp_memranges[UPPER_MEM].vmr_gpa = (4 * GIB)
	    - vcp.vcp_memranges[UPPER_MEM].vmr_size;

	if (ioctl(fd, VMM_IOC_CREATE, &vcp) == -1)
		err(1, "VMM_IOC_CREATE");
	printf("created vm %d named \"%s\"\n", vcp.vcp_id, vcp.vcp_name);

	/*
	 * 2. Check we can create shared memory mappings.
	 */
	memset(&vsp, 0, sizeof(vsp));
	vsp.vsp_nmemranges = vcp.vcp_nmemranges;
	memcpy(&vsp.vsp_memranges, &vcp.vcp_memranges,
	    sizeof(vsp.vsp_memranges));
	vsp.vsp_vm_id = vcp.vcp_id;

	/* Perform the shared mapping. */
	if (ioctl(fd, VMM_IOC_SHAREMEM, &vsp) == -1)
		err(1, "VMM_IOC_SHAREMEM");
	printf("created shared memory mappings\n");

	for (i = 0; i < vsp.vsp_nmemranges; i++)
		vcp.vcp_memranges[i].vmr_va = vsp.vsp_va[i];

	for (i = 0; i < vcp.vcp_nmemranges; i++) {
		vmr = &vcp.vcp_memranges[i];
		if (vmr->vmr_size % 2 != 0)
			errx(1, "memory ranges must be multiple of 2");

		p = (void *)vmr->vmr_va;

		printf("created mapped region %zu: { gpa: 0x%08lx, size: %lu,"
		    " hva: 0x%lx }\n", i, vmr->vmr_gpa, vmr->vmr_size,
		    vmr->vmr_va);

		/* Fill with int3 instructions. */
		memset(p, 0xcc, vmr->vmr_size);

		if (i == LOW_MEM) {
			/* Write our IVT. */
			memcpy(p, &ivt, sizeof(ivt));

			/*
			 * Set up a #GP handler that does a read from a
			 * non-existent PC Jr. Disk Controller.
			 */
			p = (uint8_t*)((uint8_t*)p + 0xb5d);
			memcpy(p, IN_PCJR, sizeof(IN_PCJR));
		} else {
			/*
			 * Write our code to the reset vector:
			 *   PUSHW %dx        ; inits the stack
			 *   INS dx, es:[di]  ; read from port in dx
			 */
			off = reset - vmr->vmr_gpa;
			p = (uint8_t*)p + off;
			memcpy(p, PUSHW_DX, sizeof(PUSHW_DX));
			p = (uint8_t*)p + sizeof(PUSHW_DX);
			memcpy(p, INS, sizeof(INS));
		}
	}

	/* We should see our reset vector instructions in the new mappings. */
	for (i = 0; i < vsp.vsp_nmemranges; i++) {
		vmr = &vsp.vsp_memranges[i];
		p = (void*)vmr->vmr_va;

		if (i == LOW_MEM) {
			/* Check if our IVT is there. */
			if (memcmp(&ivt, p, sizeof(ivt)) != 0) {
				warnx("invalid ivt");
				goto out;
			}
		} else {
			/* Check our code at the reset vector. */

		}
		printf("checked shared region %zu: { gpa: 0x%08lx, size: %lu,"
		    " hva: 0x%lx }\n", i, vmr->vmr_gpa, vmr->vmr_size,
		    vmr->vmr_va);
	}
	printf("validated shared memory mappings\n");

	/*
	 * 3. Check that our VM exists.
	 */
	memset(&vip, 0, sizeof(vip));
	vip.vip_size = 0;
	info = NULL;

	if (ioctl(fd, VMM_IOC_INFO, &vip) == -1) {
		warn("VMM_IOC_INFO(1)");
		goto out;
	}

	if (vip.vip_size == 0) {
		warn("no vms found");
		goto out;
	}

	info = malloc(vip.vip_size);
	if (info == NULL) {
		warn("malloc");
		goto out;
	}

	/* Second request that retrieves the VMs. */
	vip.vip_info = info;
	if (ioctl(fd, VMM_IOC_INFO, &vip) == -1) {
		warn("VMM_IOC_INFO(2)");
		goto out;
	}

	for (i = 0; i * sizeof(*info) < vip.vip_size; i++) {
		if (info[i].vir_id == vcp.vcp_id) {
			ours = &info[i];
			break;
		}
	}
	if (ours == NULL) {
		warn("failed to find vm %uz", vcp.vcp_id);
		goto out;
	}

	if (ours->vir_id != vcp.vcp_id) {
		warnx("expected vm id %uz, got %uz", vcp.vcp_id, ours->vir_id);
		goto out;
	}
	if (strncmp(ours->vir_name, VM_NAME, strlen(VM_NAME)) != 0) {
		warnx("expected vm name \"%s\", got \"%s\"", VM_NAME,
		    ours->vir_name);
		goto out;
	}
	printf("found vm %d named \"%s\"\n", vcp.vcp_id, ours->vir_name);
	ours = NULL;

	/*
	 * 4. Reset our VCPU and initialize register state.
	 */
	memset(&vresetp, 0, sizeof(vresetp));
	vresetp.vrp_vm_id = vcp.vcp_id;
	vresetp.vrp_vcpu_id = 0;	/* XXX SP */
	memcpy(&vresetp.vrp_init_state, &vcpu_init_flat16,
	    sizeof(vcpu_init_flat16));

	if (ioctl(fd, VMM_IOC_RESETCPU, &vresetp) == -1) {
		warn("VMM_IOC_RESETCPU");
		goto out;
	}
	printf("reset vcpu %d for vm %d\n", vresetp.vrp_vcpu_id,
	    vresetp.vrp_vm_id);

	/*
	 * 5. Run the vcpu, expecting an immediate exit for IO assist.
	 */
	exit = malloc(sizeof(*exit));
	if (exit == NULL) {
		warn("failed to allocate memory for vm_exit");
		goto out;
	}

	memset(&vrunp, 0, sizeof(vrunp));
	vrunp.vrp_exit = exit;
	vrunp.vrp_vcpu_id = 0;		/* XXX SP */
	vrunp.vrp_vm_id = vcp.vcp_id;
	vrunp.vrp_irqready = 1;

	if (ioctl(fd, VMM_IOC_RUN, &vrunp) == -1) {
		warn("VMM_IOC_RUN");
		goto out;
	}

	if (vrunp.vrp_vm_id != vcp.vcp_id) {
		warnx("expected vm id %uz, got %uz", vcp.vcp_id,
		    vrunp.vrp_vm_id);
		goto out;
	}

	switch (vrunp.vrp_exit_reason) {
	case SVM_VMEXIT_IOIO:
	case VMX_EXIT_IO:
		printf("vcpu %d on vm %d exited for io assist @ ip = 0x%llx, "
		    "cs.base = 0x%llx, ss.base = 0x%llx, rsp = 0x%llx\n",
		    vrunp.vrp_vcpu_id, vrunp.vrp_vm_id,
		    vrunp.vrp_exit->vrs.vrs_gprs[VCPU_REGS_RIP],
		    vrunp.vrp_exit->vrs.vrs_sregs[VCPU_REGS_CS].vsi_base,
		    vrunp.vrp_exit->vrs.vrs_sregs[VCPU_REGS_SS].vsi_base,
		    vrunp.vrp_exit->vrs.vrs_gprs[VCPU_REGS_RSP]);
		break;
	default:
		warnx("unexpected vm exit reason: 0%04x",
		    vrunp.vrp_exit_reason);
		goto out;
	}

	exit = vrunp.vrp_exit;
	if (exit->vei.vei_port != PCKBC_AUX) {
		warnx("expected io port to be PCKBC_AUX, got 0x%02x",
		    exit->vei.vei_port);
		goto out;
	}
	if (exit->vei.vei_string != 1) {
		warnx("expected string instruction (INS)");
		goto out;
	} else
		printf("got expected string instruction\n");

	/* Advance RIP? */
	printf("insn_len = %u\n", exit->vei.vei_insn_len);
	exit->vrs.vrs_gprs[VCPU_REGS_RIP] += exit->vei.vei_insn_len;

	/*
	 * Inject a #GP and see if we end up at our isr.
	 */
	vrunp.vrp_inject.vie_vector = VMM_EX_GP;
	vrunp.vrp_inject.vie_errorcode = 0x11223344;
	vrunp.vrp_inject.vie_type = VCPU_INJECT_EX;
	printf("injecting exception 0x%x\n", vrunp.vrp_inject.vie_vector);
	if (ioctl(fd, VMM_IOC_RUN, &vrunp) == -1) {
		warn("VMM_IOC_RUN 2");
		goto out;
	}

	switch (vrunp.vrp_exit_reason) {
	case SVM_VMEXIT_IOIO:
	case VMX_EXIT_IO:
		printf("vcpu %d on vm %d exited for io assist @ ip = 0x%llx, "
		    "cs.base = 0x%llx\n", vrunp.vrp_vcpu_id, vrunp.vrp_vm_id,
		    vrunp.vrp_exit->vrs.vrs_gprs[VCPU_REGS_RIP],
		    vrunp.vrp_exit->vrs.vrs_sregs[VCPU_REGS_CS].vsi_base);
		break;
	default:
		warnx("unexpected vm exit reason: 0%04x",
		    vrunp.vrp_exit_reason);
		goto out;
	}

	if (exit->vei.vei_port != PCJR_DISKCTRL) {
		warnx("expected NMI handler to poke PCJR_DISKCTLR, got 0x%02x",
		    exit->vei.vei_port);
		printf("rip = 0x%llx\n", exit->vrs.vrs_gprs[VCPU_REGS_RIP]);
		goto out;
	}
	printf("exception handler called\n");

	/*
	 * If we made it here, we're close to passing. Any failures during
	 * cleanup will reset ret back to non-zero.
	 */
	ret = 0;

out:
	printf("--- RESET VECTOR @ gpa 0x%llx ---\n", reset);
	for (i=0; i<10; i++) {
		if (i > 0)
			printf(" ");
		printf("%02x", *(uint8_t*)
		    (vsp.vsp_memranges[UPPER_MEM].vmr_va + off + i));
	}
	printf("\n--- STACK @ gpa 0x%llx ---\n", stack);
	for (i=0; i<16; i++) {
		if (i > 0)
			printf(" ");
		printf("%02x", *(uint8_t*)(vsp.vsp_memranges[LOW_MEM].vmr_va
			+ stack - i - 1));
	}
	printf("\n");

	/*
	 * 6. Terminate our VM and clean up.
	 */
	memset(&vtp, 0, sizeof(vtp));
	vtp.vtp_vm_id = vcp.vcp_id;
	if (ioctl(fd, VMM_IOC_TERM, &vtp) == -1) {
		warn("VMM_IOC_TERM");
		ret = 1;
	} else
		printf("terminated vm %d\n", vtp.vtp_vm_id);

	close(fd);
	free(info);
	free(exit);

	return (ret);
}
