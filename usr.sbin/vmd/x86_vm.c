/*	$OpenBSD: x86_vm.c,v 1.9 2025/09/16 15:10:03 mlarkin Exp $	*/
/*
 * Copyright (c) 2015 Mike Larkin <mlarkin@openbsd.org>
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

#include <sys/stat.h>
#include <sys/types.h>

#include <dev/ic/i8253reg.h>
#include <dev/isa/isareg.h>

#include <machine/pte.h>
#include <machine/specialreg.h>
#include <machine/vmmvar.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <zlib.h>

#include "atomicio.h"
#include "fw_cfg.h"
#include "i8253.h"
#include "i8259.h"
#include "loadfile.h"
#include "mc146818.h"
#include "ns8250.h"
#include "pci.h"
#include "virtio.h"

typedef uint8_t (*io_fn_t)(struct vm_run_params *);

#define MAX_PORTS 65536

io_fn_t	ioports_map[MAX_PORTS];
extern char *__progname;

void	 create_memory_map(struct vm_create_params *);
int	 translate_gva(struct vm_exit*, uint64_t, uint64_t *, int);

static int	loadfile_bios(gzFile, off_t, struct vcpu_reg_state *);
static int	vcpu_exit_eptviolation(struct vm_run_params *);
static void	vcpu_exit_inout(struct vm_run_params *);

extern struct vmd_vm	*current_vm;
extern int		 con_fd;

/*
 * Represents a standard register set for an OS to be booted
 * as a flat 64 bit address space.
 *
 * NOT set here are:
 *  RIP
 *  RSP
 *  GDTR BASE
 *
 * Specific bootloaders should clone this structure and override
 * those fields as needed.
 *
 * Note - CR3 and various bits in CR0 may be overridden by vmm(4) based on
 *        features of the CPU in use.
 */
static const struct vcpu_reg_state vcpu_init_flat64 = {
	.vrs_gprs[VCPU_REGS_RFLAGS] = 0x2,
	.vrs_gprs[VCPU_REGS_RIP] = 0x0,
	.vrs_gprs[VCPU_REGS_RSP] = 0x0,
	.vrs_crs[VCPU_REGS_CR0] = CR0_ET | CR0_PE | CR0_PG,
	.vrs_crs[VCPU_REGS_CR3] = PML4_PAGE,
	.vrs_crs[VCPU_REGS_CR4] = CR4_PAE | CR4_PSE,
	.vrs_crs[VCPU_REGS_PDPTE0] = 0ULL,
	.vrs_crs[VCPU_REGS_PDPTE1] = 0ULL,
	.vrs_crs[VCPU_REGS_PDPTE2] = 0ULL,
	.vrs_crs[VCPU_REGS_PDPTE3] = 0ULL,
	.vrs_sregs[VCPU_REGS_CS] = { 0x8, 0xFFFFFFFF, 0xC09F, 0x0},
	.vrs_sregs[VCPU_REGS_DS] = { 0x10, 0xFFFFFFFF, 0xC093, 0x0},
	.vrs_sregs[VCPU_REGS_ES] = { 0x10, 0xFFFFFFFF, 0xC093, 0x0},
	.vrs_sregs[VCPU_REGS_FS] = { 0x10, 0xFFFFFFFF, 0xC093, 0x0},
	.vrs_sregs[VCPU_REGS_GS] = { 0x10, 0xFFFFFFFF, 0xC093, 0x0},
	.vrs_sregs[VCPU_REGS_SS] = { 0x10, 0xFFFFFFFF, 0xC093, 0x0},
	.vrs_gdtr = { 0x0, 0xFFFF, 0x0, 0x0},
	.vrs_idtr = { 0x0, 0xFFFF, 0x0, 0x0},
	.vrs_sregs[VCPU_REGS_LDTR] = { 0x0, 0xFFFF, 0x0082, 0x0},
	.vrs_sregs[VCPU_REGS_TR] = { 0x0, 0xFFFF, 0x008B, 0x0},
	.vrs_msrs[VCPU_REGS_EFER] = EFER_LME | EFER_LMA,
	.vrs_drs[VCPU_REGS_DR0] = 0x0,
	.vrs_drs[VCPU_REGS_DR1] = 0x0,
	.vrs_drs[VCPU_REGS_DR2] = 0x0,
	.vrs_drs[VCPU_REGS_DR3] = 0x0,
	.vrs_drs[VCPU_REGS_DR6] = 0xFFFF0FF0,
	.vrs_drs[VCPU_REGS_DR7] = 0x400,
	.vrs_msrs[VCPU_REGS_STAR] = 0ULL,
	.vrs_msrs[VCPU_REGS_LSTAR] = 0ULL,
	.vrs_msrs[VCPU_REGS_CSTAR] = 0ULL,
	.vrs_msrs[VCPU_REGS_SFMASK] = 0ULL,
	.vrs_msrs[VCPU_REGS_KGSBASE] = 0ULL,
	.vrs_msrs[VCPU_REGS_MISC_ENABLE] = 0ULL,
	.vrs_crs[VCPU_REGS_XCR0] = XFEATURE_X87
};

/*
 * Represents a standard register set for an BIOS to be booted
 * as a flat 16 bit address space.
 */
static const struct vcpu_reg_state vcpu_init_flat16 = {
	.vrs_gprs[VCPU_REGS_RFLAGS] = 0x2,
	.vrs_gprs[VCPU_REGS_RIP] = 0xFFF0,
	.vrs_gprs[VCPU_REGS_RSP] = 0x0,
	.vrs_crs[VCPU_REGS_CR0] = 0x60000010,
	.vrs_crs[VCPU_REGS_CR3] = 0,
	.vrs_sregs[VCPU_REGS_CS] = { 0xF000, 0xFFFF, 0x809F, 0xF0000},
	.vrs_sregs[VCPU_REGS_DS] = { 0x0, 0xFFFF, 0x8093, 0x0},
	.vrs_sregs[VCPU_REGS_ES] = { 0x0, 0xFFFF, 0x8093, 0x0},
	.vrs_sregs[VCPU_REGS_FS] = { 0x0, 0xFFFF, 0x8093, 0x0},
	.vrs_sregs[VCPU_REGS_GS] = { 0x0, 0xFFFF, 0x8093, 0x0},
	.vrs_sregs[VCPU_REGS_SS] = { 0x0, 0xFFFF, 0x8093, 0x0},
	.vrs_gdtr = { 0x0, 0xFFFF, 0x0, 0x0},
	.vrs_idtr = { 0x0, 0xFFFF, 0x0, 0x0},
	.vrs_sregs[VCPU_REGS_LDTR] = { 0x0, 0xFFFF, 0x0082, 0x0},
	.vrs_sregs[VCPU_REGS_TR] = { 0x0, 0xFFFF, 0x008B, 0x0},
	.vrs_msrs[VCPU_REGS_EFER] = 0ULL,
	.vrs_drs[VCPU_REGS_DR0] = 0x0,
	.vrs_drs[VCPU_REGS_DR1] = 0x0,
	.vrs_drs[VCPU_REGS_DR2] = 0x0,
	.vrs_drs[VCPU_REGS_DR3] = 0x0,
	.vrs_drs[VCPU_REGS_DR6] = 0xFFFF0FF0,
	.vrs_drs[VCPU_REGS_DR7] = 0x400,
	.vrs_msrs[VCPU_REGS_STAR] = 0ULL,
	.vrs_msrs[VCPU_REGS_LSTAR] = 0ULL,
	.vrs_msrs[VCPU_REGS_CSTAR] = 0ULL,
	.vrs_msrs[VCPU_REGS_SFMASK] = 0ULL,
	.vrs_msrs[VCPU_REGS_KGSBASE] = 0ULL,
	.vrs_crs[VCPU_REGS_XCR0] = XFEATURE_X87
};

/*
 * create_memory_map
 *
 * Sets up the guest physical memory ranges that the VM can access.
 *
 * Parameters:
 *  vcp: VM create parameters describing the VM whose memory map
 *       is being created
 *
 * Return values:
 *  nothing
 */
void
create_memory_map(struct vm_create_params *vcp)
{
	size_t len, mem_bytes;
	size_t above_1m = 0, above_4g = 0;

	mem_bytes = vcp->vcp_memranges[0].vmr_size;
	vcp->vcp_nmemranges = 0;
	if (mem_bytes == 0 || mem_bytes > VMM_MAX_VM_MEM_SIZE)
		return;

	/* First memory region: 0 - LOWMEM_KB (DOS low mem) */
	len = LOWMEM_KB * 1024;
	vcp->vcp_memranges[0].vmr_gpa = 0x0;
	vcp->vcp_memranges[0].vmr_size = len;
	vcp->vcp_memranges[0].vmr_type = VM_MEM_RAM;
	mem_bytes -= len;

	/*
	 * Second memory region: LOWMEM_KB - 1MB.
	 *
	 * N.B. - Normally ROMs or parts of video RAM are mapped here.
	 * We have to add this region, because some systems
	 * unconditionally write to 0xb8000 (VGA RAM), and
	 * we need to make sure that vmm(4) permits accesses
	 * to it. So allocate guest memory for it.
	 */
	len = MB(1) - (LOWMEM_KB * 1024);
	vcp->vcp_memranges[1].vmr_gpa = LOWMEM_KB * 1024;
	vcp->vcp_memranges[1].vmr_size = len;
	vcp->vcp_memranges[1].vmr_type = VM_MEM_RESERVED;
	mem_bytes -= len;

	/*
	 * If we have less than 4MB remaining to assign, still create a 2nd
	 * BIOS area.
	 */
	if (mem_bytes <= MB(4)) {
		vcp->vcp_memranges[2].vmr_gpa = PCI_MMIO_BAR_END;
		vcp->vcp_memranges[2].vmr_size = MB(4);
		vcp->vcp_memranges[2].vmr_type = VM_MEM_RESERVED;
		vcp->vcp_nmemranges = 3;
		return;
	}

	/*
	 * Calculate the how to split any remaining memory across the 4GB
	 * boundary while making sure we do not place physical memory into
	 * MMIO ranges.
	 */
	if (mem_bytes > PCI_MMIO_BAR_BASE - MB(1)) {
		above_1m = PCI_MMIO_BAR_BASE - MB(1);
		above_4g = mem_bytes - above_1m;
	} else {
		above_1m = mem_bytes;
		above_4g = 0;
	}

	/* Third memory region: area above 1MB to MMIO region */
	vcp->vcp_memranges[2].vmr_gpa = MB(1);
	vcp->vcp_memranges[2].vmr_size = above_1m;
	vcp->vcp_memranges[2].vmr_type = VM_MEM_RAM;

	/* Fourth region: PCI MMIO range */
	vcp->vcp_memranges[3].vmr_gpa = PCI_MMIO_BAR_BASE;
	vcp->vcp_memranges[3].vmr_size = PCI_MMIO_BAR_END -
	    PCI_MMIO_BAR_BASE + 1;
	vcp->vcp_memranges[3].vmr_type = VM_MEM_MMIO;

	/* Fifth region: 2nd copy of BIOS above MMIO ending at 4GB */
	vcp->vcp_memranges[4].vmr_gpa = PCI_MMIO_BAR_END + 1;
	vcp->vcp_memranges[4].vmr_size = MB(4);
	vcp->vcp_memranges[4].vmr_type = VM_MEM_RESERVED;

	/* Sixth region: any remainder above 4GB */
	if (above_4g > 0) {
		vcp->vcp_memranges[5].vmr_gpa = GB(4);
		vcp->vcp_memranges[5].vmr_size = above_4g;
		vcp->vcp_memranges[5].vmr_type = VM_MEM_RAM;
		vcp->vcp_nmemranges = 6;
	} else
		vcp->vcp_nmemranges = 5;
}

int
load_firmware(struct vmd_vm *vm, struct vcpu_reg_state *vrs)
{
	int		ret;
	gzFile		fp;
	struct stat	sb;

	/*
	 * Set up default "flat 64 bit" register state - RIP, RSP, and
	 * GDT info will be set in bootloader
	 */
	memcpy(vrs, &vcpu_init_flat64, sizeof(*vrs));

	/* Find and open kernel image */
	if ((fp = gzdopen(vm->vm_kernel, "r")) == NULL)
		fatalx("failed to open kernel - exiting");

	/* Load kernel image */
	ret = loadfile_elf(fp, vm, vrs, vm->vm_params.vmc_bootdevice);

	/*
	 * Try BIOS as a fallback (only if it was provided as an image
	 * with vm->vm_kernel and the file is not compressed)
	 */
	if (ret && errno == ENOEXEC && vm->vm_kernel != -1 &&
	    gzdirect(fp) && (ret = fstat(vm->vm_kernel, &sb)) == 0)
		ret = loadfile_bios(fp, sb.st_size, vrs);

	gzclose(fp);

	return (ret);
}


/*
 * loadfile_bios
 *
 * Alternatively to loadfile_elf, this function loads a non-ELF BIOS image
 * directly into memory.
 *
 * Parameters:
 *  fp: file of a kernel file to load
 *  size: uncompressed size of the image
 *  (out) vrs: register state to set on init for this kernel
 *
 * Return values:
 *  0 if successful
 *  various error codes returned from read(2) or loadelf functions
 */
int
loadfile_bios(gzFile fp, off_t size, struct vcpu_reg_state *vrs)
{
	off_t	 off = 0;
	size_t	 lower_sz = size;

	/*
	 * While a 15 byte firmware is most likely useless, given the
	 * reset vector on a PC is 15 bytes below 0xFFFFF, make sure
	 * we will at least align to that boundary.
	 */
	if (size < 15) {
		log_warnx("bios image too small");
		return (-1);
	}

	/* Assumptions elsewhere in memory layout limit to 4 MiB. */
	if (size > (off_t)MB(4)) {
		log_warnx("bios image too large (> 4 MiB)");
		return (-1);
	}

	/* Set up a "flat 16 bit" register state for BIOS. */
	memcpy(vrs, &vcpu_init_flat16, sizeof(*vrs));

	/* Read a full copy into BIOS area ending at 4 GiB. */
	if (gzrewind(fp) == -1)
		return (-1);

	off = GB(4) - size;
	if (mread(fp, off, size) != (size_t)size) {
		errno = EIO;
		return (-1);
	}

	/*
	 * Copy whatever fits of the upper part of the image
	 * into the lower BIOS area ending at 1 MiB.
	 */
	if (gzrewind(fp) == -1)
		return (-1);

	lower_sz = MB(1) - (LOWMEM_KB * 1024);
	lower_sz = MIN((off_t)lower_sz, size);
	if (gzseek(fp, size - lower_sz, SEEK_SET) == -1)
		return (-1);

	off = MB(1) - lower_sz;
	if (mread(fp, off, lower_sz) != lower_sz)
		return (-1);

	log_debug("%s: loaded BIOS image", __func__);

	return (0);
}

/*
 * init_emulated_hw
 *
 * Initializes the userspace hardware emulation
 */
void
init_emulated_hw(struct vmop_create_params *vmc, int child_cdrom,
    int child_disks[][VM_MAX_BASE_PER_DISK], int *child_taps)
{
	struct vm_create_params *vcp = &vmc->vmc_params;
	size_t i;
	uint64_t memlo, memhi;

	/* Calculate memory size for NVRAM registers */
	memlo = memhi = 0;
	for (i = 0; i < vcp->vcp_nmemranges; i++) {
		if (vcp->vcp_memranges[i].vmr_gpa == MB(1) &&
		    vcp->vcp_memranges[i].vmr_size > (15 * MB(1)))
			memlo = vcp->vcp_memranges[i].vmr_size - (15 * MB(1));
		else if (vcp->vcp_memranges[i].vmr_gpa == GB(4))
			memhi = vcp->vcp_memranges[i].vmr_size;
	}

	/* Reset the IO port map */
	memset(&ioports_map, 0, sizeof(io_fn_t) * MAX_PORTS);

	/* Init i8253 PIT */
	i8253_init(vcp->vcp_id);
	ioports_map[TIMER_CTRL] = vcpu_exit_i8253;
	ioports_map[TIMER_BASE + TIMER_CNTR0] = vcpu_exit_i8253;
	ioports_map[TIMER_BASE + TIMER_CNTR1] = vcpu_exit_i8253;
	ioports_map[TIMER_BASE + TIMER_CNTR2] = vcpu_exit_i8253;
	ioports_map[PCKBC_AUX] = vcpu_exit_i8253_misc;

	/* Init mc146818 RTC */
	mc146818_init(vcp->vcp_id, memlo, memhi);
	ioports_map[IO_RTC] = vcpu_exit_mc146818;
	ioports_map[IO_RTC + 1] = vcpu_exit_mc146818;

	/* Init master and slave PICs */
	i8259_init();
	ioports_map[IO_ICU1] = vcpu_exit_i8259;
	ioports_map[IO_ICU1 + 1] = vcpu_exit_i8259;
	ioports_map[IO_ICU2] = vcpu_exit_i8259;
	ioports_map[IO_ICU2 + 1] = vcpu_exit_i8259;
	ioports_map[ELCR0] = vcpu_exit_elcr;
	ioports_map[ELCR1] = vcpu_exit_elcr;

	/* Init ns8250 UART */
	ns8250_init(con_fd, vcp->vcp_id);
	for (i = COM1_DATA; i <= COM1_SCR; i++)
		ioports_map[i] = vcpu_exit_com;

	/* Initialize PCI */
	for (i = VM_PCI_IO_BAR_BASE; i <= VM_PCI_IO_BAR_END; i++)
		ioports_map[i] = vcpu_exit_pci;

	ioports_map[PCI_MODE1_ADDRESS_REG] = vcpu_exit_pci;
	ioports_map[PCI_MODE1_DATA_REG] = vcpu_exit_pci;
	ioports_map[PCI_MODE1_DATA_REG + 1] = vcpu_exit_pci;
	ioports_map[PCI_MODE1_DATA_REG + 2] = vcpu_exit_pci;
	ioports_map[PCI_MODE1_DATA_REG + 3] = vcpu_exit_pci;
	pci_init();

	/* Initialize virtio devices */
	virtio_init(current_vm, child_cdrom, child_disks, child_taps);

	/*
	 * Init QEMU fw_cfg interface. Must be done last for pci hardware
	 * detection.
	 */
	fw_cfg_init(vmc);
	ioports_map[FW_CFG_IO_SELECT] = vcpu_exit_fw_cfg;
	ioports_map[FW_CFG_IO_DATA] = vcpu_exit_fw_cfg;
	ioports_map[FW_CFG_IO_DMA_ADDR_HIGH] = vcpu_exit_fw_cfg_dma;
	ioports_map[FW_CFG_IO_DMA_ADDR_LOW] = vcpu_exit_fw_cfg_dma;
}

void
pause_vm_md(struct vmd_vm *vm)
{
	i8253_stop();
	mc146818_stop();
	ns8250_stop();
	virtio_stop(vm);
}

void
unpause_vm_md(struct vmd_vm *vm)
{
	i8253_start();
	mc146818_start();
	ns8250_start();
	virtio_start(vm);
}

/*
 * vcpu_exit_inout
 *
 * Handle all I/O exits that need to be emulated in vmd. This includes the
 * i8253 PIT, the com1 ns8250 UART, and the MC146818 RTC/NVRAM device.
 *
 * Parameters:
 *  vrp: vcpu run parameters containing guest state for this exit
 */
void
vcpu_exit_inout(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;
	uint8_t intr = 0xFF;

	if (vei->vei.vei_rep || vei->vei.vei_string) {
#ifdef MMIO_DEBUG
		log_info("%s: %s%s%s %d-byte, enc=%d, data=0x%08x, port=0x%04x",
		    __func__,
		    vei->vei.vei_rep == 0 ? "" : "REP ",
		    vei->vei.vei_dir == VEI_DIR_IN ? "IN" : "OUT",
		    vei->vei.vei_string == 0 ? "" : "S",
		    vei->vei.vei_size, vei->vei.vei_encoding,
		    vei->vei.vei_data, vei->vei.vei_port);
		log_info("%s: ECX = 0x%llx, RDX = 0x%llx, RSI = 0x%llx",
		    __func__,
		    vei->vrs.vrs_gprs[VCPU_REGS_RCX],
		    vei->vrs.vrs_gprs[VCPU_REGS_RDX],
		    vei->vrs.vrs_gprs[VCPU_REGS_RSI]);
#endif /* MMIO_DEBUG */
		fatalx("%s: can't emulate REP prefixed IN(S)/OUT(S)",
		    __func__);
	}

	if (ioports_map[vei->vei.vei_port] != NULL)
		intr = ioports_map[vei->vei.vei_port](vrp);
	else if (vei->vei.vei_dir == VEI_DIR_IN)
		set_return_data(vei, 0xFFFFFFFF);

	vei->vrs.vrs_gprs[VCPU_REGS_RIP] += vei->vei.vei_insn_len;

	if (intr != 0xFF)
		vcpu_assert_irq(vrp->vrp_vm_id, vrp->vrp_vcpu_id, intr);
}

/*
 * vcpu_exit
 *
 * Handle a vcpu exit. This function is called when it is determined that
 * vmm(4) requires the assistance of vmd to support a particular guest
 * exit type (eg, accessing an I/O port or device). Guest state is contained
 * in 'vrp', and will be resent to vmm(4) on exit completion.
 *
 * Upon conclusion of handling the exit, the function determines if any
 * interrupts should be injected into the guest, and asserts the proper
 * IRQ line whose interrupt should be vectored.
 *
 * Parameters:
 *  vrp: vcpu run parameters containing guest state for this exit
 *
 * Return values:
 *  0: the exit was handled successfully
 *  1: an error occurred (eg, unknown exit reason passed in 'vrp')
 */
int
vcpu_exit(struct vm_run_params *vrp)
{
	int ret;

	switch (vrp->vrp_exit_reason) {
	case VMX_EXIT_INT_WINDOW:
	case SVM_VMEXIT_VINTR:
	case VMX_EXIT_CPUID:
	case VMX_EXIT_EXTINT:
	case SVM_VMEXIT_INTR:
	case SVM_VMEXIT_MSR:
	case SVM_VMEXIT_CPUID:
		/*
		 * We may be exiting to vmd to handle a pending interrupt but
		 * at the same time the last exit type may have been one of
		 * these. In this case, there's nothing extra to be done
		 * here (and falling through to the default case below results
		 * in more vmd log spam).
		 */
		break;
	case SVM_VMEXIT_NPF:
	case VMX_EXIT_EPT_VIOLATION:
		ret = vcpu_exit_eptviolation(vrp);
		if (ret)
			return (ret);
		break;
	case VMX_EXIT_IO:
	case SVM_VMEXIT_IOIO:
		vcpu_exit_inout(vrp);
		break;
	case VMX_EXIT_HLT:
	case SVM_VMEXIT_HLT:
		vcpu_halt(vrp->vrp_vcpu_id);
		break;
	case VMX_EXIT_TRIPLE_FAULT:
	case SVM_VMEXIT_SHUTDOWN:
		/* reset VM */
		return (EAGAIN);
	default:
		log_debug("%s: unknown exit reason 0x%x",
		    __progname, vrp->vrp_exit_reason);
	}

	return (0);
}

/*
 * vcpu_exit_eptviolation
 *
 * handle an EPT Violation
 *
 * Parameters:
 *  vrp: vcpu run parameters containing guest state for this exit
 *
 * Return values:
 *  0: no action required
 *  EFAULT: a protection fault occured, kill the vm.
 */
static int
vcpu_exit_eptviolation(struct vm_run_params *vrp)
{
	struct vm_exit *ve = vrp->vrp_exit;
	int ret = 0;
#if MMIO_NOTYET
	struct x86_insn insn;
	uint64_t va, pa;
	size_t len = 15;		/* Max instruction length in x86. */
#endif /* MMIO_NOTYET */
	switch (ve->vee.vee_fault_type) {
	case VEE_FAULT_HANDLED:
		break;

#if MMIO_NOTYET
	case VEE_FAULT_MMIO_ASSIST:
		/* Intel VMX might give us the length of the instruction. */
		if (ve->vee.vee_insn_info & VEE_LEN_VALID)
			len = ve->vee.vee_insn_len;

		if (len > 15)
			fatalx("%s: invalid instruction length %lu", __func__,
			    len);

		/* If we weren't given instruction bytes, we need to fetch. */
		if (!(ve->vee.vee_insn_info & VEE_BYTES_VALID)) {
			memset(ve->vee.vee_insn_bytes, 0,
			    sizeof(ve->vee.vee_insn_bytes));
			va = ve->vrs.vrs_gprs[VCPU_REGS_RIP];

			/* XXX Only support instructions that fit on 1 page. */
			if ((va & PAGE_MASK) + len > PAGE_SIZE) {
				log_warnx("%s: instruction might cross page "
				    "boundary", __func__);
				ret = EINVAL;
				break;
			}

			ret = translate_gva(ve, va, &pa, PROT_EXEC);
			if (ret != 0) {
				log_warnx("%s: failed gva translation",
				    __func__);
				break;
			}

			ret = read_mem(pa, ve->vee.vee_insn_bytes, len);
			if (ret != 0) {
				log_warnx("%s: failed to fetch instruction "
				    "bytes from 0x%llx", __func__, pa);
				break;
			}
		}

		ret = insn_decode(ve, &insn);
		if (ret == 0)
			ret = insn_emulate(ve, &insn);
		break;
#endif /* MMIO_NOTYET */

	case VEE_FAULT_PROTECT:
		log_debug("%s: EPT Violation: rip=0x%llx", __progname,
		    ve->vrs.vrs_gprs[VCPU_REGS_RIP]);
		ret = EFAULT;
		break;

	default:
		fatalx("%s: invalid fault_type %d", __progname,
		    ve->vee.vee_fault_type);
		/* UNREACHED */
	}

	return (ret);
}

/*
 * vcpu_exit_pci
 *
 * Handle all I/O to the emulated PCI subsystem.
 *
 * Parameters:
 *  vrp: vcpu run parameters containing guest state for this exit
 *
 * Return value:
 *  Interrupt to inject to the guest VM, or 0xFF if no interrupt should
 *      be injected.
 */
uint8_t
vcpu_exit_pci(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;
	uint8_t intr;

	intr = 0xFF;

	switch (vei->vei.vei_port) {
	case PCI_MODE1_ADDRESS_REG:
		pci_handle_address_reg(vrp);
		break;
	case PCI_MODE1_DATA_REG:
	case PCI_MODE1_DATA_REG + 1:
	case PCI_MODE1_DATA_REG + 2:
	case PCI_MODE1_DATA_REG + 3:
		pci_handle_data_reg(vrp);
		break;
	case VM_PCI_IO_BAR_BASE ... VM_PCI_IO_BAR_END:
		intr = pci_handle_io(vrp);
		break;
	default:
		log_warnx("%s: unknown PCI register 0x%llx",
		    __progname, (uint64_t)vei->vei.vei_port);
		break;
	}

	return (intr);
}

/*
 * find_gpa_range
 *
 * Search for a contiguous guest physical mem range.
 *
 * Parameters:
 *  vcp: VM create parameters that contain the memory map to search in
 *  gpa: the starting guest physical address
 *  len: the length of the memory range
 *
 * Return values:
 *  NULL: on failure if there is no memory range as described by the parameters
 *  Pointer to vm_mem_range that contains the start of the range otherwise.
 */
struct vm_mem_range *
find_gpa_range(struct vm_create_params *vcp, paddr_t gpa, size_t len)
{
	size_t i, n;
	struct vm_mem_range *vmr;

	/* Find the first vm_mem_range that contains gpa */
	for (i = 0; i < vcp->vcp_nmemranges; i++) {
		vmr = &vcp->vcp_memranges[i];
		if (gpa < vmr->vmr_gpa + vmr->vmr_size)
			break;
	}

	/* No range found. */
	if (i == vcp->vcp_nmemranges)
		return (NULL);

	/*
	 * vmr may cover the range [gpa, gpa + len) only partly. Make
	 * sure that the following vm_mem_ranges are contiguous and
	 * cover the rest.
	 */
	n = vmr->vmr_size - (gpa - vmr->vmr_gpa);
	if (len < n)
		len = 0;
	else
		len -= n;
	gpa = vmr->vmr_gpa + vmr->vmr_size;
	for (i = i + 1; len != 0 && i < vcp->vcp_nmemranges; i++) {
		vmr = &vcp->vcp_memranges[i];
		if (gpa != vmr->vmr_gpa)
			return (NULL);
		if (len <= vmr->vmr_size)
			len = 0;
		else
			len -= vmr->vmr_size;

		gpa = vmr->vmr_gpa + vmr->vmr_size;
	}

	if (len != 0)
		return (NULL);

	return (vmr);
}
/*
 * write_mem
 *
 * Copies data from 'buf' into the guest VM's memory at paddr 'dst'.
 *
 * Parameters:
 *  dst: the destination paddr_t in the guest VM
 *  buf: data to copy (or NULL to zero the data)
 *  len: number of bytes to copy
 *
 * Return values:
 *  0: success
 *  EINVAL: if the guest physical memory range [dst, dst + len) does not
 *      exist in the guest.
 */
int
write_mem(paddr_t dst, const void *buf, size_t len)
{
	const char *from = buf;
	char *to;
	size_t n, off;
	struct vm_mem_range *vmr;

	vmr = find_gpa_range(&current_vm->vm_params.vmc_params, dst, len);
	if (vmr == NULL) {
		errno = EINVAL;
		log_warn("%s: failed - invalid memory range dst = 0x%lx, "
		    "len = 0x%zx", __func__, dst, len);
		return (EINVAL);
	}

	off = dst - vmr->vmr_gpa;
	while (len != 0) {
		n = vmr->vmr_size - off;
		if (len < n)
			n = len;

		to = (char *)vmr->vmr_va + off;
		if (buf == NULL)
			memset(to, 0, n);
		else {
			memcpy(to, from, n);
			from += n;
		}
		len -= n;
		off = 0;
		vmr++;
	}

	return (0);
}

/*
 * read_mem
 *
 * Reads memory at guest paddr 'src' into 'buf'.
 *
 * Parameters:
 *  src: the source paddr_t in the guest VM to read from.
 *  buf: destination (local) buffer
 *  len: number of bytes to read
 *
 * Return values:
 *  0: success
 *  EINVAL: if the guest physical memory range [dst, dst + len) does not
 *      exist in the guest.
 */
int
read_mem(paddr_t src, void *buf, size_t len)
{
	char *from, *to = buf;
	size_t n, off;
	struct vm_mem_range *vmr;

	vmr = find_gpa_range(&current_vm->vm_params.vmc_params, src, len);
	if (vmr == NULL) {
		errno = EINVAL;
		log_warn("%s: failed - invalid memory range src = 0x%lx, "
		    "len = 0x%zx", __func__, src, len);
		return (EINVAL);
	}

	off = src - vmr->vmr_gpa;
	while (len != 0) {
		n = vmr->vmr_size - off;
		if (len < n)
			n = len;

		from = (char *)vmr->vmr_va + off;
		memcpy(to, from, n);

		to += n;
		len -= n;
		off = 0;
		vmr++;
	}

	return (0);
}

/*
 * hvaddr_mem
 *
 * Translate a guest physical address to a host virtual address, checking the
 * provided memory range length to confirm it's contiguous within the same
 * guest memory range (vm_mem_range).
 *
 * Parameters:
 *  gpa: guest physical address to translate
 *  len: number of bytes in the intended range
 *
 * Return values:
 *  void* to host virtual memory on success
 *  NULL on error, setting errno to:
 *    EFAULT: gpa falls outside guest memory ranges
 *    EINVAL: requested len extends beyond memory range
 */
void *
hvaddr_mem(paddr_t gpa, size_t len)
{
	struct vm_mem_range *vmr;
	size_t off;

	vmr = find_gpa_range(&current_vm->vm_params.vmc_params, gpa, len);
	if (vmr == NULL) {
		log_warnx("%s: failed - invalid gpa: 0x%lx\n", __func__, gpa);
		errno = EFAULT;
		return (NULL);
	}

	off = gpa - vmr->vmr_gpa;
	if (len > (vmr->vmr_size - off)) {
		log_warnx("%s: failed - invalid memory range: gpa=0x%lx, "
		    "len=%zu", __func__, gpa, len);
		errno = EINVAL;
		return (NULL);
	}

	return ((char *)vmr->vmr_va + off);
}

/*
 * vcpu_assert_irq
 *
 * Injects the specified IRQ on the supplied vcpu/vm
 *
 * Parameters:
 *  vm_id: VM ID to inject to
 *  vcpu_id: VCPU ID to inject to
 *  irq: IRQ to inject
 */
void
vcpu_assert_irq(uint32_t vm_id, uint32_t vcpu_id, int irq)
{
	i8259_assert_irq(irq);

	if (i8259_is_pending()) {
		if (vcpu_intr(vm_id, vcpu_id, 1))
			fatalx("%s: can't assert INTR", __func__);

		vcpu_unhalt(vcpu_id);
		vcpu_signal_run(vcpu_id);
	}
}

/*
 * vcpu_deassert_irq
 *
 * Clears the specified IRQ on the supplied vcpu/vm
 *
 * Parameters:
 *  vm_id: VM ID to clear in
 *  vcpu_id: VCPU ID to clear in
 *  irq: IRQ to clear
 */
void
vcpu_deassert_irq(uint32_t vm_id, uint32_t vcpu_id, int irq)
{
	i8259_deassert_irq(irq);

	if (!i8259_is_pending()) {
		if (vcpu_intr(vm_id, vcpu_id, 0))
			fatalx("%s: can't deassert INTR for vm_id %d, "
			    "vcpu_id %d", __func__, vm_id, vcpu_id);
	}
}

/*
 * set_return_data
 *
 * Utility function for manipulating register data in vm exit info structs. This
 * function ensures that the data is copied to the vei->vei.vei_data field with
 * the proper size for the operation being performed.
 *
 * Parameters:
 *  vei: exit information
 *  data: return data
 */
void
set_return_data(struct vm_exit *vei, uint32_t data)
{
	switch (vei->vei.vei_size) {
	case 1:
		vei->vei.vei_data &= ~0xFF;
		vei->vei.vei_data |= (uint8_t)data;
		break;
	case 2:
		vei->vei.vei_data &= ~0xFFFF;
		vei->vei.vei_data |= (uint16_t)data;
		break;
	case 4:
		vei->vei.vei_data = data;
		break;
	}
}

/*
 * get_input_data
 *
 * Utility function for manipulating register data in vm exit info
 * structs. This function ensures that the data is copied from the
 * vei->vei.vei_data field with the proper size for the operation being
 * performed.
 *
 * Parameters:
 *  vei: exit information
 *  data: location to store the result
 */
void
get_input_data(struct vm_exit *vei, uint32_t *data)
{
	switch (vei->vei.vei_size) {
	case 1:
		*data &= 0xFFFFFF00;
		*data |= (uint8_t)vei->vei.vei_data;
		break;
	case 2:
		*data &= 0xFFFF0000;
		*data |= (uint16_t)vei->vei.vei_data;
		break;
	case 4:
		*data = vei->vei.vei_data;
		break;
	default:
		log_warnx("%s: invalid i/o size %d", __func__,
		    vei->vei.vei_size);
	}

}

/*
 * translate_gva
 *
 * Translates a guest virtual address to a guest physical address by walking
 * the currently active page table (if needed).
 *
 * XXX ensure translate_gva updates the A bit in the PTE
 * XXX ensure translate_gva respects segment base and limits in i386 mode
 * XXX ensure translate_gva respects segment wraparound in i8086 mode
 * XXX ensure translate_gva updates the A bit in the segment selector
 * XXX ensure translate_gva respects CR4.LMSLE if available
 *
 * Parameters:
 *  exit: The VCPU this translation should be performed for (guest MMU settings
 *   are gathered from this VCPU)
 *  va: virtual address to translate
 *  pa: pointer to paddr_t variable that will receive the translated physical
 *   address. 'pa' is unchanged on error.
 *  mode: one of PROT_READ, PROT_WRITE, PROT_EXEC indicating the mode in which
 *   the address should be translated
 *
 * Return values:
 *  0: the address was successfully translated - 'pa' contains the physical
 *     address currently mapped by 'va'.
 *  EFAULT: the PTE for 'VA' is unmapped. A #PF will be injected in this case
 *     and %cr2 set in the vcpu structure.
 *  EINVAL: an error occurred reading paging table structures
 */
int
translate_gva(struct vm_exit* exit, uint64_t va, uint64_t* pa, int mode)
{
	int level, shift, pdidx;
	uint64_t pte, pt_paddr, pte_paddr, mask, low_mask, high_mask;
	uint64_t shift_width, pte_size;
	struct vcpu_reg_state *vrs;

	vrs = &exit->vrs;

	if (!pa)
		return (EINVAL);

	if (!(vrs->vrs_crs[VCPU_REGS_CR0] & CR0_PG)) {
		log_debug("%s: unpaged, va=pa=0x%llx", __func__, va);
		*pa = va;
		return (0);
	}

	pt_paddr = vrs->vrs_crs[VCPU_REGS_CR3];

	log_debug("%s: guest %%cr0=0x%llx, %%cr3=0x%llx", __func__,
	    vrs->vrs_crs[VCPU_REGS_CR0], vrs->vrs_crs[VCPU_REGS_CR3]);

	if (vrs->vrs_crs[VCPU_REGS_CR0] & CR0_PE) {
		if (vrs->vrs_crs[VCPU_REGS_CR4] & CR4_PAE) {
			pte_size = sizeof(uint64_t);
			shift_width = 9;

			if (vrs->vrs_msrs[VCPU_REGS_EFER] & EFER_LMA) {
				/* 4 level paging */
				level = 4;
				mask = L4_MASK;
				shift = L4_SHIFT;
			} else {
				/* 32 bit with PAE paging */
				level = 3;
				mask = L3_MASK;
				shift = L3_SHIFT;
			}
		} else {
			/* 32 bit paging */
			level = 2;
			shift_width = 10;
			mask = 0xFFC00000;
			shift = 22;
			pte_size = sizeof(uint32_t);
		}
	} else
		return (EINVAL);

	/* XXX: Check for R bit in segment selector and set A bit */

	for (;level > 0; level--) {
		pdidx = (va & mask) >> shift;
		pte_paddr = (pt_paddr) + (pdidx * pte_size);

		log_debug("%s: read pte level %d @ GPA 0x%llx", __func__,
		    level, pte_paddr);
		if (read_mem(pte_paddr, &pte, pte_size)) {
			log_warn("%s: failed to read pte", __func__);
			return (EFAULT);
		}

		log_debug("%s: PTE @ 0x%llx = 0x%llx", __func__, pte_paddr,
		    pte);

		/* XXX: Set CR2  */
		if (!(pte & PG_V))
			return (EFAULT);

		/* XXX: Check for SMAP */
		if ((mode == PROT_WRITE) && !(pte & PG_RW))
			return (EPERM);

		if ((exit->cpl > 0) && !(pte & PG_u))
			return (EPERM);

		pte = pte | PG_U;
		if (mode == PROT_WRITE)
			pte = pte | PG_M;
		if (write_mem(pte_paddr, &pte, pte_size)) {
			log_warn("%s: failed to write back flags to pte",
			    __func__);
			return (EIO);
		}

		/* XXX: EINVAL if in 32bit and PG_PS is 1 but CR4.PSE is 0 */
		if (pte & PG_PS)
			break;

		if (level > 1) {
			pt_paddr = pte & PG_FRAME;
			shift -= shift_width;
			mask = mask >> shift_width;
		}
	}

	low_mask = (1 << shift) - 1;
	high_mask = (((uint64_t)1ULL << ((pte_size * 8) - 1)) - 1) ^ low_mask;
	*pa = (pte & high_mask) | (va & low_mask);

	log_debug("%s: final GPA for GVA 0x%llx = 0x%llx\n", __func__, va, *pa);

	return (0);
}

int
intr_pending(struct vmd_vm *vm)
{
	/* XXX select active interrupt controller */
	return i8259_is_pending();
}

int
intr_ack(struct vmd_vm *vm)
{
	/* XXX select active interrupt controller */
	return i8259_ack();
}

void
intr_toggle_el(struct vmd_vm *vm, int irq, int val)
{
	/* XXX select active interrupt controller */
	pic_set_elcr(irq, val);
}
