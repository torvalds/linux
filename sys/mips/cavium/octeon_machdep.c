/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Wojciech A. Koszek <wkoszek@FreeBSD.org>
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
 *
 * $FreeBSD$
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/boot.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cons.h>
#include <sys/exec.h>
#include <sys/ucontext.h>
#include <sys/proc.h>
#include <sys/kdb.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/user.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <machine/atomic.h>
#include <machine/cache.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/cpufunc.h>
#include <mips/cavium/octeon_pcmap_regs.h>
#include <machine/hwfunc.h>
#include <machine/intr_machdep.h>
#include <machine/locore.h>
#include <machine/md_var.h>
#include <machine/pcpu.h>
#include <machine/pte.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-bootmem.h>
#include <contrib/octeon-sdk/cvmx-ebt3000.h>
#include <contrib/octeon-sdk/cvmx-helper-cfg.h>
#include <contrib/octeon-sdk/cvmx-interrupt.h>
#include <contrib/octeon-sdk/cvmx-version.h>

#include <mips/cavium/octeon_irq.h>

#if defined(__mips_n64) 
#define MAX_APP_DESC_ADDR     0xffffffffafffffff
#else
#define MAX_APP_DESC_ADDR     0xafffffff
#endif

struct octeon_feature_description {
	octeon_feature_t ofd_feature;
	const char *ofd_string;
};

extern int	*end;
extern char cpu_model[];
extern char cpu_board[];
static char octeon_kenv[0x2000];

static const struct octeon_feature_description octeon_feature_descriptions[] = {
	{ OCTEON_FEATURE_SAAD,			"SAAD" },
	{ OCTEON_FEATURE_ZIP,			"ZIP" },
	{ OCTEON_FEATURE_CRYPTO,		"CRYPTO" },
	{ OCTEON_FEATURE_DORM_CRYPTO,		"DORM_CRYPTO" },
	{ OCTEON_FEATURE_PCIE,			"PCIE" },
	{ OCTEON_FEATURE_SRIO,			"SRIO" },
	{ OCTEON_FEATURE_KEY_MEMORY,		"KEY_MEMORY" },
	{ OCTEON_FEATURE_LED_CONTROLLER,	"LED_CONTROLLER" },
	{ OCTEON_FEATURE_TRA,			"TRA" },
	{ OCTEON_FEATURE_MGMT_PORT,		"MGMT_PORT" },
	{ OCTEON_FEATURE_RAID,			"RAID" },
	{ OCTEON_FEATURE_USB,			"USB" },
	{ OCTEON_FEATURE_NO_WPTR,		"NO_WPTR" },
	{ OCTEON_FEATURE_DFA,			"DFA" },
	{ OCTEON_FEATURE_MDIO_CLAUSE_45,	"MDIO_CLAUSE_45" },
	{ OCTEON_FEATURE_NPEI,			"NPEI" },
	{ OCTEON_FEATURE_ILK,			"ILK" },
	{ OCTEON_FEATURE_HFA,			"HFA" },
	{ OCTEON_FEATURE_DFM,			"DFM" },
	{ OCTEON_FEATURE_CIU2,			"CIU2" },
	{ OCTEON_FEATURE_DICI_MODE,		"DICI_MODE" },
	{ OCTEON_FEATURE_BIT_EXTRACTOR,		"BIT_EXTRACTOR" },
	{ OCTEON_FEATURE_NAND,			"NAND" },
	{ OCTEON_FEATURE_MMC,			"MMC" },
	{ OCTEON_FEATURE_PKND,			"PKND" },
	{ OCTEON_FEATURE_CN68XX_WQE,		"CN68XX_WQE" },
	{ 0,					NULL }
};

static uint64_t octeon_get_ticks(void);
static unsigned octeon_get_timecount(struct timecounter *tc);

static void octeon_boot_params_init(register_t ptr);
static void octeon_init_kenv(register_t ptr);

static struct timecounter octeon_timecounter = {
	octeon_get_timecount,	/* get_timecount */
	0,			/* no poll_pps */
	0xffffffffu,		/* octeon_mask */
	0,			/* frequency */
	"Octeon",		/* name */
	900,			/* quality (adjusted in code) */
};

void
platform_cpu_init()
{
	/* Nothing special yet */
}

/*
 * Perform a board-level soft-reset.
 */
void
platform_reset(void)
{
	cvmx_write_csr(CVMX_CIU_SOFT_RST, 1);
}

/*
 * octeon_debug_symbol
 *
 * Does nothing.
 * Used to mark the point for simulator to begin tracing
 */
void
octeon_debug_symbol(void)
{
}

/*
 * octeon_ciu_reset
 *
 * Shutdown all CIU to IP2, IP3 mappings
 */
void
octeon_ciu_reset(void)
{
	uint64_t cvmctl;

	/* Disable all CIU interrupts by default */
	cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2+1), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2+1), 0);

#ifdef SMP
	/* Enable the MBOX interrupts.  */
	cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2+1),
		       (1ull << (OCTEON_IRQ_MBOX0 - 8)) |
		       (1ull << (OCTEON_IRQ_MBOX1 - 8)));
#endif

	/* 
	 * Move the Performance Counter interrupt to OCTEON_PMC_IRQ
	 */
	cvmctl = mips_rd_cvmctl();
	cvmctl &= ~(7 << 7);
	cvmctl |= (OCTEON_PMC_IRQ + 2) << 7;
	mips_wr_cvmctl(cvmctl);
}

static void
octeon_memory_init(void)
{
	vm_paddr_t phys_end;
	int64_t addr;
	unsigned i, j;

	phys_end = round_page(MIPS_KSEG0_TO_PHYS((vm_offset_t)&end));

	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM) {
		/* Simulator we limit to 96 meg */
		phys_avail[0] = phys_end;
		phys_avail[1] = 96 << 20;

		dump_avail[0] = phys_avail[0];
		dump_avail[1] = phys_avail[1];

		realmem = physmem = btoc(phys_avail[1] - phys_avail[0]);
		return;
	}

	/*
	 * Allocate memory from bootmem 1MB at a time and merge
	 * adjacent entries.
	 */
	i = 0;
	while (i < PHYS_AVAIL_ENTRIES) {
		/*
		 * If there is less than 2MB of memory available in 128-byte
		 * blocks, do not steal any more memory.  We need to leave some
		 * memory for the command queues to be allocated out of.
		 */
		if (cvmx_bootmem_available_mem(128) < 2 << 20)
			break;

		addr = cvmx_bootmem_phy_alloc(1 << 20, phys_end,
					      ~(vm_paddr_t)0, PAGE_SIZE, 0);
		if (addr == -1)
			break;

		/*
		 * The SDK needs to be able to easily map any memory that might
		 * come to it e.g. in the form of an mbuf.  Because on !n64 we
		 * can't direct-map some addresses and we don't want to manage
		 * temporary mappings within the SDK, don't feed memory that
		 * can't be direct-mapped to the kernel.
		 */
#if !defined(__mips_n64)
		if (!MIPS_DIRECT_MAPPABLE(addr + (1 << 20) - 1))
			continue;
#endif

		physmem += btoc(1 << 20);

		if (i > 0 && phys_avail[i - 1] == addr) {
			phys_avail[i - 1] += 1 << 20;
			continue;
		}

		phys_avail[i + 0] = addr;
		phys_avail[i + 1] = addr + (1 << 20);

		i += 2;
	}

	for (j = 0; j < i; j++)
		dump_avail[j] = phys_avail[j];

	realmem = physmem;
}

void
platform_start(__register_t a0, __register_t a1, __register_t a2 __unused,
    __register_t a3)
{
	const struct octeon_feature_description *ofd;
	uint64_t platform_counter_freq;
	int rv;

	mips_postboot_fixup();

	/*
	 * Initialize boot parameters so that we can determine things like
	 * which console we shoud use, etc.
	 */
	octeon_boot_params_init(a3);

	/* Initialize pcpu stuff */
	mips_pcpu0_init();
	mips_timer_early_init(cvmx_sysinfo_get()->cpu_clock_hz);

	/* Initialize console.  */
	cninit();

	/*
	 * Display information about the CPU.
	 */
#if !defined(OCTEON_MODEL)
	printf("Using runtime CPU model checks.\n");
#else
	printf("Compiled for CPU model: " __XSTRING(OCTEON_MODEL) "\n");
#endif
	strcpy(cpu_model, octeon_model_get_string(cvmx_get_proc_id()));
	printf("CPU Model: %s\n", cpu_model);
	printf("CPU clock: %uMHz  Core Mask: %#x\n",
	       cvmx_sysinfo_get()->cpu_clock_hz / 1000000,
	       cvmx_sysinfo_get()->core_mask);
	rv = octeon_model_version_check(cvmx_get_proc_id());
	if (rv == -1)
		panic("%s: kernel not compatible with this processor.", __func__);

	/*
	 * Display information about the board.
	 */
#if defined(OCTEON_BOARD_CAPK_0100ND)
	strcpy(cpu_board, "CAPK-0100ND");
	if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_CN3010_EVB_HS5) {
		panic("Compiled for %s, but board type is %s.", cpu_board,
		       cvmx_board_type_to_string(cvmx_sysinfo_get()->board_type));
	}
#else
	strcpy(cpu_board,
	       cvmx_board_type_to_string(cvmx_sysinfo_get()->board_type));
#endif
	printf("Board: %s\n", cpu_board);
	printf("Board Type: %u  Revision: %u/%u\n",
	       cvmx_sysinfo_get()->board_type,
	       cvmx_sysinfo_get()->board_rev_major,
	       cvmx_sysinfo_get()->board_rev_minor);
	printf("Serial number: %s\n", cvmx_sysinfo_get()->board_serial_number);

	/*
	 * Additional on-chip hardware/settings.
	 *
	 * XXX Display PCI host/target?  What else?
	 */
	printf("MAC address base: %6D (%u configured)\n",
	       cvmx_sysinfo_get()->mac_addr_base, ":",
	       cvmx_sysinfo_get()->mac_addr_count);


	octeon_ciu_reset();
	/*
	 * Convert U-Boot 'bootoctlinux' loader command line arguments into
	 * boot flags and kernel environment variables.
	 */
	bootverbose = 1;
	octeon_init_kenv(a3);

	/*
	 * For some reason on the cn38xx simulator ebase register is set to
	 * 0x80001000 at bootup time.  Move it back to the default, but
	 * when we move to having support for multiple executives, we need
	 * to rethink this.
	 */
	mips_wr_ebase(0x80000000);

	octeon_memory_init();
	init_param1();
	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();
	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
	cpu_clock = cvmx_sysinfo_get()->cpu_clock_hz;
	platform_counter_freq = cpu_clock;
	octeon_timecounter.tc_frequency = cpu_clock;
	platform_timecounter = &octeon_timecounter;
	mips_timer_init_params(platform_counter_freq, 0);
	set_cputicker(octeon_get_ticks, cpu_clock, 0);

#ifdef SMP
	/*
	 * Clear any pending IPIs.
	 */
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(0), 0xffffffff);
#endif

	printf("Octeon SDK: %s\n", OCTEON_SDK_VERSION_STRING);
	printf("Available Octeon features:");
	for (ofd = octeon_feature_descriptions; ofd->ofd_string != NULL; ofd++)
		if (octeon_has_feature(ofd->ofd_feature))
			printf(" %s", ofd->ofd_string);
	printf("\n");
}

static uint64_t
octeon_get_ticks(void)
{
	uint64_t cvmcount;

	CVMX_MF_CYCLE(cvmcount);
	return (cvmcount);
}

static unsigned
octeon_get_timecount(struct timecounter *tc)
{
	return ((unsigned)octeon_get_ticks());
}

static int
sysctl_machdep_led_display(SYSCTL_HANDLER_ARGS)
{
	size_t buflen;
	char buf[9];
	int error;

	if (req->newptr == NULL)
		return (EINVAL);

	if (cvmx_sysinfo_get()->led_display_base_addr == 0)
		return (ENODEV);

	/*
	 * Revision 1.x of the EBT3000 only supports 4 characters, but
	 * other devices support 8.
	 */
	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_EBT3000 &&
	    cvmx_sysinfo_get()->board_rev_major == 1)
		buflen = 4;
	else
		buflen = 8;

	if (req->newlen > buflen)
		return (E2BIG);

	error = SYSCTL_IN(req, buf, req->newlen);
	if (error != 0)
		return (error);

	buf[req->newlen] = '\0';
	ebt3000_str_write(buf);

	return (0);
}

SYSCTL_PROC(_machdep, OID_AUTO, led_display, CTLTYPE_STRING | CTLFLAG_WR,
    NULL, 0, sysctl_machdep_led_display, "A",
    "String to display on LED display");

void
cvmx_dvprintf(const char *fmt, va_list ap)
{
	if (!bootverbose)
		return;
	vprintf(fmt, ap);
}

void
cvmx_dprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cvmx_dvprintf(fmt, ap);
	va_end(ap);
}

/**
 * version of printf that works better in exception context.
 *
 * @param format
 *
 * XXX If this function weren't in cvmx-interrupt.c, we'd use the SDK version.
 */
void cvmx_safe_printf(const char *format, ...)
{
    char buffer[256];
    char *ptr = buffer;
    int count;
    va_list args;

    va_start(args, format);
#ifndef __U_BOOT__
    count = vsnprintf(buffer, sizeof(buffer), format, args);
#else
    count = vsprintf(buffer, format, args);
#endif
    va_end(args);

    while (count-- > 0)
    {
        cvmx_uart_lsr_t lsrval;

        /* Spin until there is room */
        do
        {
            lsrval.u64 = cvmx_read_csr(CVMX_MIO_UARTX_LSR(0));
#if !defined(CONFIG_OCTEON_SIM_SPEED)
            if (lsrval.s.temt == 0)
                cvmx_wait(10000);   /* Just to reduce the load on the system */
#endif
        }
        while (lsrval.s.temt == 0);

        if (*ptr == '\n')
            cvmx_write_csr(CVMX_MIO_UARTX_THR(0), '\r');
        cvmx_write_csr(CVMX_MIO_UARTX_THR(0), *ptr++);
    }
}

/* impSTART: This stuff should move back into the Cavium SDK */
/*
 ****************************************************************************************
 *
 * APP/BOOT  DESCRIPTOR  STUFF
 *
 ****************************************************************************************
 */

/* Define the struct that is initialized by the bootloader used by the 
 * startup code.
 *
 * Copyright (c) 2004, 2005, 2006 Cavium Networks.
 *
 * The authors hereby grant permission to use, copy, modify, distribute,
 * and license this software and its documentation for any purpose, provided
 * that existing copyright notices are retained in all copies and that this
 * notice is included verbatim in any distributions. No written agreement,
 * license, or royalty fee is required for any of the authorized uses.
 * Modifications to this software may be copyrighted by their authors
 * and need not follow the licensing terms described here, provided that
 * the new terms are clearly indicated on the first page of each file where
 * they apply.
 */

#define OCTEON_CURRENT_DESC_VERSION     6
#define OCTEON_ARGV_MAX_ARGS            (64)
#define OCTOEN_SERIAL_LEN 20

typedef struct {
	/* Start of block referenced by assembly code - do not change! */
	uint32_t desc_version;
	uint32_t desc_size;

	uint64_t stack_top;
	uint64_t heap_base;
	uint64_t heap_end;
	uint64_t entry_point;   /* Only used by bootloader */
	uint64_t desc_vaddr;
	/* End of This block referenced by assembly code - do not change! */

	uint32_t exception_base_addr;
	uint32_t stack_size;
	uint32_t heap_size;
	uint32_t argc;  /* Argc count for application */
	uint32_t argv[OCTEON_ARGV_MAX_ARGS];
	uint32_t flags;
	uint32_t core_mask;
	uint32_t dram_size;  /**< DRAM size in megabyes */
	uint32_t phy_mem_desc_addr;  /**< physical address of free memory descriptor block*/
	uint32_t debugger_flags_base_addr;  /**< used to pass flags from app to debugger */
	uint32_t eclock_hz;  /**< CPU clock speed, in hz */
	uint32_t dclock_hz;  /**< DRAM clock speed, in hz */
	uint32_t spi_clock_hz;  /**< SPI4 clock in hz */
	uint16_t board_type;
	uint8_t board_rev_major;
	uint8_t board_rev_minor;
	uint16_t chip_type;
	uint8_t chip_rev_major;
	uint8_t chip_rev_minor;
	char board_serial_number[OCTOEN_SERIAL_LEN];
	uint8_t mac_addr_base[6];
	uint8_t mac_addr_count;
	uint64_t cvmx_desc_vaddr;
} octeon_boot_descriptor_t;

static cvmx_bootinfo_t *
octeon_process_app_desc_ver_6(octeon_boot_descriptor_t *app_desc_ptr)
{
	cvmx_bootinfo_t *octeon_bootinfo;

	/* XXX Why is 0x00000000ffffffffULL a bad value?  */
	if (app_desc_ptr->cvmx_desc_vaddr == 0 ||
	    app_desc_ptr->cvmx_desc_vaddr == 0xfffffffful) {
            	cvmx_safe_printf("Bad octeon_bootinfo %#jx\n",
		    (uintmax_t)app_desc_ptr->cvmx_desc_vaddr);
		return (NULL);
	}

    	octeon_bootinfo = cvmx_phys_to_ptr(app_desc_ptr->cvmx_desc_vaddr);
        if (octeon_bootinfo->major_version != 1) {
            	cvmx_safe_printf("Incompatible CVMX descriptor from bootloader: %d.%d %p\n",
		    (int) octeon_bootinfo->major_version,
		    (int) octeon_bootinfo->minor_version, octeon_bootinfo);
		return (NULL);
	}

	cvmx_sysinfo_minimal_initialize(octeon_bootinfo->phy_mem_desc_addr,
					octeon_bootinfo->board_type,
					octeon_bootinfo->board_rev_major,
					octeon_bootinfo->board_rev_minor,
					octeon_bootinfo->eclock_hz);
	memcpy(cvmx_sysinfo_get()->mac_addr_base,
	       octeon_bootinfo->mac_addr_base, 6);
	cvmx_sysinfo_get()->mac_addr_count = octeon_bootinfo->mac_addr_count;
	cvmx_sysinfo_get()->compact_flash_common_base_addr = 
		octeon_bootinfo->compact_flash_common_base_addr;
	cvmx_sysinfo_get()->compact_flash_attribute_base_addr = 
		octeon_bootinfo->compact_flash_attribute_base_addr;
	cvmx_sysinfo_get()->core_mask = octeon_bootinfo->core_mask;
	cvmx_sysinfo_get()->led_display_base_addr =
		octeon_bootinfo->led_display_base_addr;
	memcpy(cvmx_sysinfo_get()->board_serial_number,
	       octeon_bootinfo->board_serial_number,
	       sizeof cvmx_sysinfo_get()->board_serial_number);
	return (octeon_bootinfo);
}

static void
octeon_boot_params_init(register_t ptr)
{
	octeon_boot_descriptor_t *app_desc_ptr;
	cvmx_bootinfo_t *octeon_bootinfo;

	if (ptr == 0 || ptr >= MAX_APP_DESC_ADDR) {
		cvmx_safe_printf("app descriptor passed at invalid address %#jx\n",
		    (uintmax_t)ptr);
		platform_reset();
	}

	app_desc_ptr = (octeon_boot_descriptor_t *)(intptr_t)ptr;
	if (app_desc_ptr->desc_version < 6) {
		cvmx_safe_printf("Your boot code is too old to be supported.\n");
		platform_reset();
	}
	octeon_bootinfo = octeon_process_app_desc_ver_6(app_desc_ptr);
	if (octeon_bootinfo == NULL) {
		cvmx_safe_printf("Could not parse boot descriptor.\n");
		platform_reset();
	}

	if (cvmx_sysinfo_get()->led_display_base_addr != 0) {
		/*
		 * Revision 1.x of the EBT3000 only supports 4 characters, but
		 * other devices support 8.
		 */
		if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_EBT3000 &&
		    cvmx_sysinfo_get()->board_rev_major == 1)
			ebt3000_str_write("FBSD");
		else
			ebt3000_str_write("FreeBSD!");
	}

	if (cvmx_sysinfo_get()->phy_mem_desc_addr == (uint64_t)0) {
		cvmx_safe_printf("Your boot loader did not supply a memory descriptor.\n");
		platform_reset();
	}
	cvmx_bootmem_init(cvmx_sysinfo_get()->phy_mem_desc_addr);

	octeon_feature_init();

	__cvmx_helper_cfg_init();
}
/* impEND: This stuff should move back into the Cavium SDK */

/*
 * The boot loader command line may specify kernel environment variables or
 * applicable boot flags of boot(8).
 */
static void
octeon_init_kenv(register_t ptr)
{
	int i;
	char *n;
	char *v;
	octeon_boot_descriptor_t *app_desc_ptr;

	app_desc_ptr = (octeon_boot_descriptor_t *)(intptr_t)ptr;
	memset(octeon_kenv, 0, sizeof(octeon_kenv));
	init_static_kenv(octeon_kenv, sizeof(octeon_kenv));

	for (i = 0; i < app_desc_ptr->argc; i++) {
		v = cvmx_phys_to_ptr(app_desc_ptr->argv[i]);
		if (v == NULL)
			continue;
		if (*v == '-') {
			boothowto |= boot_parse_arg(v);
			continue;
		}
		n = strsep(&v, "=");
		if (v == NULL)
			kern_setenv(n, "1");
		else
			kern_setenv(n, v);
	}
}
