/*	$OpenBSD: autoconf.c,v 1.152 2025/07/16 07:15:42 jsg Exp $	*/
/*	$NetBSD: autoconf.c,v 1.51 2001/07/24 19:32:11 eeh Exp $ */

/*
 * Copyright (c) 1996
 *    The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by Harvard University.
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)autoconf.c	8.4 (Berkeley) 10/1/93
 */

#include "mpath.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/msgbuf.h>

#include <net/if.h>

#include <dev/cons.h>
#include <dev/clock_subr.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/boot_flag.h>
#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/mdesc.h>
#include <machine/openfirm.h>
#include <machine/sparc64.h>
#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/trap.h>
#include <sparc64/sparc64/cache.h>
#include <sparc64/dev/vbusvar.h>
#include <sparc64/dev/cbusvar.h>

#include <stand/boot/bootarg.h>

#include <dev/ata/atavar.h>
#include <dev/pci/pcivar.h>
#include <dev/sbus/sbusvar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#if NMPATH > 0
#include <scsi/mpathvar.h>
#endif

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#endif

#include "softraid.h"
#if NSOFTRAID > 0
#include <sys/sensors.h>
#include <dev/softraidvar.h>

/* XXX */
#undef DPRINTF
#undef DNPRINTF
#endif

int printspl = 0;

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */
int	stdinnode;	/* node ID of ROM's console input device */
int	fbnode;		/* node ID of ROM's console output device */
int	optionsnode;	/* node ID of ROM's options */

static	int rootnode;

static	char *str2hex(char *, long *);
static	int mbprint(void *, const char *);
int	mainbus_match(struct device *, void *, void *);
static	void mainbus_attach(struct device *, struct device *, void *);
int	get_ncpus(void);

struct device *booted_device;
struct	bootpath bootpath[16];
int	nbootpath;
int	bootnode;
static	void bootpath_build(void);
static	void bootpath_print(struct bootpath *);
void bootpath_nodes(struct bootpath *, int);

struct openbsd_bootdata obd __attribute__((section(".openbsd.bootdata")));

void nail_bootdev(struct device *, struct bootpath *);

/* Global interrupt mappings for all device types.  Match against the OBP
 * 'device_type' property. 
 */
struct intrmap intrmap[] = {
	{ "block",	PIL_FD },	/* Floppy disk */
	{ "serial",	PIL_SER },	/* zs */
	{ "scsi",	PIL_SCSI },
	{ "scsi-2",	PIL_SCSI },
	{ "network",	PIL_NET },
	{ "display",	PIL_VIDEO },
	{ "audio",	PIL_AUD },
	{ "ide",	PIL_SCSI },
/* The following devices don't have device types: */
	{ "SUNW,CS4231",	PIL_AUD },
	{ NULL,		0 }
};

#ifdef SUN4V
void	sun4v_soft_state_init(void);
void	sun4v_set_soft_state(int, const char *);

#define __align32 __attribute__((__aligned__(32)))
char sun4v_soft_state_booting[] __align32 = "OpenBSD booting";
char sun4v_soft_state_running[] __align32 = "OpenBSD running";

void	sun4v_interrupt_init(void);
void	sun4v_sdio_init(void);
#endif

extern void us_tlb_flush_pte(vaddr_t, uint64_t);
extern void us3_tlb_flush_pte(vaddr_t, uint64_t);
extern void sun4v_tlb_flush_pte(vaddr_t, uint64_t);
extern void us_tlb_flush_ctx(uint64_t);
extern void us3_tlb_flush_ctx(uint64_t);
extern void sun4v_tlb_flush_ctx(uint64_t);

void (*sp_tlb_flush_pte)(vaddr_t, uint64_t) = us_tlb_flush_pte;
void (*sp_tlb_flush_ctx)(uint64_t) = us_tlb_flush_ctx;

#ifdef DEBUG
#define ACDB_BOOTDEV	0x1
#define	ACDB_PROBE	0x2
int autoconf_debug = 0x0;
#define DPRINTF(l, s)   do { if (autoconf_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

/*
 * Convert hex ASCII string to a value.  Returns updated pointer.
 * Depends on ASCII order (this *is* machine-dependent code, you know).
 */
static char *
str2hex(char *str, long *vp)
{
	long v;
	int c;

	if (*str == 'w') {
		for (v = 1;; v++) {
			if (str[v] >= '0' && str[v] <= '9')
				continue;
			if (str[v] >= 'a' && str[v] <= 'f')
				continue;
			if (str[v] >= 'A' && str[v] <= 'F')
				continue;
			if (str[v] == '\0' || str[v] == ',')
				break;
			*vp = 0;
			return (str + v);
		}
		str++;
	}

	for (v = 0;; v = v * 16 + c, str++) {
		c = *(u_char *)str;
		if (c <= '9') {
			if ((c -= '0') < 0)
				break;
		} else if (c <= 'F') {
			if ((c -= 'A' - 10) < 10)
				break;
		} else if (c <= 'f') {
			if ((c -= 'a' - 10) < 10)
				break;
		} else
			break;
	}
	*vp = v;
	return (str);
}

/*
 * Hunt through the device tree for CPUs.  There should be no need to
 * go more than four levels deep; an UltraSPARC-IV on Seregeti shows
 * up as /ssm@0,0/cmp@0,0/cpu@0 and a SPARC64-VI will show up as
 * /cmp@0,0/core@0/cpu@0.
 */
int
get_ncpus(void)
{
	int node, child, stack[4], depth, ncpus;
	char buf[32];

	stack[0] = findroot();
	depth = 0;

	ncpus = 0;
	for (;;) {
		node = stack[depth];

		if (node == 0 || node == -1) {
			if (--depth < 0)
				goto done;
			
			stack[depth] = OF_peer(stack[depth]);
			continue;
		}

		if (OF_getprop(node, "device_type", buf, sizeof(buf)) > 0 &&
		    strcmp(buf, "cpu") == 0)
			ncpus++;

		child = OF_child(node);
		if (child != 0 && child != -1 && depth < 3)
			stack[++depth] = child;
		else
			stack[depth] = OF_peer(stack[depth]);
	}

done:
	ncpusfound = ncpus;
#ifdef MULTIPROCESSOR
	return (ncpus);
#else
	return (1);
#endif
}

/*
 * locore.s code calls bootstrap() just before calling main().
 *
 * What we try to do is as follows:
 *
 * 1) We will try to re-allocate the old message buffer.
 *
 * 2) We will then get the list of the total and available
 *	physical memory and available virtual memory from the
 *	prom.
 *
 * 3) We will pass the list to pmap_bootstrap to manage them.
 *
 * We will try to run out of the prom until we get to cpu_init().
 */
void
bootstrap(int nctx)
{
	extern int end;	/* End of kernel */
	struct trapvec *romtba;
#if defined(SUN4US) || defined(SUN4V)
	char buf[32];
#endif
	int impl = 0;
	int ncpus;

	/* Initialize the PROM console so printf will not panic. */
	(*cn_tab->cn_init)(cn_tab);

	/* 
	 * Initialize ddb first and register OBP callbacks.
	 * We can do this because ddb_init() does not allocate anything,
	 * just initializes some pointers to important things
	 * like the symtab.
	 *
	 * By doing this first and installing the OBP callbacks
	 * we get to do symbolic debugging of pmap_bootstrap().
	 */
#ifdef DDB
	db_machine_init();
	ddb_init();
	/* This can only be installed on an 64-bit system cause otherwise our stack is screwed */
	OF_set_symbol_lookup(OF_sym2val, OF_val2sym);
#endif

#if defined (SUN4US) || defined(SUN4V)
	if (OF_getprop(findroot(), "compatible", buf, sizeof(buf)) > 0) {
		if (strcmp(buf, "sun4us") == 0)
			cputyp = CPU_SUN4US;
		if (strcmp(buf, "sun4v") == 0)
			cputyp = CPU_SUN4V;
	}
#endif

	/* We cannot read %ver on sun4v systems. */
	if (CPU_ISSUN4U || CPU_ISSUN4US)
		impl = (getver() & VER_IMPL) >> VER_IMPL_SHIFT;

	if (impl >= IMPL_CHEETAH) {
		extern vaddr_t dlflush_start;
		vaddr_t *pva;
		u_int32_t insn;

		for (pva = &dlflush_start; *pva; pva++) {
			insn = *(u_int32_t *)(*pva);
			insn &= ~(ASI_DCACHE_TAG << 5);
			insn |= (ASI_DCACHE_INVALIDATE << 5);
			*(u_int32_t *)(*pva) = insn;
			flush((void *)(*pva));
		}

		cacheinfo.c_dcache_flush_page = us3_dcache_flush_page;
		sp_tlb_flush_pte = us3_tlb_flush_pte;
		sp_tlb_flush_ctx = us3_tlb_flush_ctx;
	}

	if ((impl >= IMPL_ZEUS && impl <= IMPL_JUPITER) || CPU_ISSUN4V) {
		extern vaddr_t dlflush_start;
		vaddr_t *pva;

		for (pva = &dlflush_start; *pva; pva++) {
			*(u_int32_t *)(*pva) = 0x01000000; /* nop */
			flush((void *)(*pva));
		}

		cacheinfo.c_dcache_flush_page = no_dcache_flush_page;
	}

#ifdef MULTIPROCESSOR
	if (impl >= IMPL_OLYMPUS_C && impl <= IMPL_JUPITER) {
		struct sun4u_patch {
			u_int32_t addr;
			u_int32_t insn;
		} *p;

		extern struct sun4u_patch sun4u_mtp_patch;
		extern struct sun4u_patch sun4u_mtp_patch_end;

		for (p = &sun4u_mtp_patch; p < &sun4u_mtp_patch_end; p++) {
			*(u_int32_t *)(vaddr_t)p->addr = p->insn;
			flush((void *)(vaddr_t)p->addr);
		}
	}
#endif

#ifdef SUN4V
	if (CPU_ISSUN4V) {
		struct sun4v_patch {
			u_int32_t addr;
			u_int32_t insn;
		} *p;

		extern struct sun4v_patch sun4v_patch;
		extern struct sun4v_patch sun4v_patch_end;

		for (p = &sun4v_patch; p < &sun4v_patch_end; p++) {
			*(u_int32_t *)(vaddr_t)p->addr = p->insn;
			flush((void *)(vaddr_t)p->addr);
		}

#ifdef MULTIPROCESSOR
		extern struct sun4v_patch sun4v_mp_patch;
		extern struct sun4v_patch sun4v_mp_patch_end;

		for (p = &sun4v_mp_patch; p < &sun4v_mp_patch_end; p++) {
			*(u_int32_t *)(vaddr_t)p->addr = p->insn;
			flush((void *)(vaddr_t)p->addr);
		}
#endif

		sp_tlb_flush_pte = sun4v_tlb_flush_pte;
		sp_tlb_flush_ctx = sun4v_tlb_flush_ctx;
	}
#endif

	/*
	 * Copy over the OBP breakpoint trap vector; OpenFirmware 5.x
	 * needs it to be able to return to the ok prompt.
	 */
	romtba = (struct trapvec *)sparc_rdpr(tba);
	bcopy(&romtba[T_MON_BREAKPOINT], &trapbase[T_MON_BREAKPOINT],
	    sizeof(struct trapvec));
	flush((void *)trapbase);

	ncpus = get_ncpus();
	pmap_bootstrap(KERNBASE, (u_long)&end, nctx, ncpus);

	if (obd.version == BOOTDATA_VERSION &&
	    obd.len >= BOOTDATA_LEN_BOOTHOWTO)
		boothowto = obd.boothowto;

#ifdef SUN4V
	if (CPU_ISSUN4V) {
		sun4v_soft_state_init();
		sun4v_set_soft_state(SIS_TRANSITION, sun4v_soft_state_booting);
		sun4v_interrupt_init();
		sun4v_sdio_init();
	}
#endif
}

void
bootpath_nodes(struct bootpath *bp, int nbp)
{
	int chosen;
	int i;
	char buf[128], *cp, c;

	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "bootpath", buf, sizeof(buf));
	cp = buf;

	for (i = 0; i < nbp; i++, bp++) {
		if (*cp == '\0')
			return;
		while (*cp != '\0' && *cp == '/')
			cp++;
		while (*cp && *cp != '/')
			cp++;
		c = *cp;
		*cp = '\0';
		bootnode = bp->node = OF_finddevice(buf);
		*cp = c;
	}
}

/*
 * bootpath_build: build a bootpath. Used when booting a generic
 * kernel to find our root device.  Newer proms give us a bootpath,
 * for older proms we have to create one.  An element in a bootpath
 * has 4 fields: name (device name), val[0], val[1], and val[2]. Note that:
 * Interpretation of val[] is device-dependent. Some examples:
 *
 * if (val[0] == -1) {
 *	val[1] is a unit number    (happens most often with old proms)
 * } else {
 *	[sbus device] val[0] is a sbus slot, and val[1] is an sbus offset
 *	[scsi disk] val[0] is target, val[1] is lun, val[2] is partition
 *	[scsi tape] val[0] is target, val[1] is lun, val[2] is file #
 *	[pci device] val[0] is device, val[1] is function, val[2] might be partition
 * }
 *
 */

static void
bootpath_build(void)
{
	register char *cp, *pp;
	register struct bootpath *bp;
	register long chosen;
	char buf[128];

	bzero((void *)bootpath, sizeof(bootpath));
	bp = bootpath;

	/*
	 * Grab boot path from PROM
	 */
	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "bootpath", buf, sizeof(buf));
	cp = buf;
	while (cp != NULL && *cp == '/') {
		/* Step over '/' */
		++cp;
		/* Extract name */
		pp = bp->name;
		while (*cp != '@' && *cp != '/' && *cp != '\0')
			*pp++ = *cp++;
		*pp = '\0';
		if (*cp == '@') {
			cp = str2hex(++cp, &bp->val[0]);
			if (*cp == ',')
				cp = str2hex(++cp, &bp->val[1]);
			if (*cp == ':') {
				/*
				 * We only store one character here, as we will
				 * only use this field to compute a partition
				 * index for block devices.  However, it might
				 * be an ethernet media specification, so be
				 * sure to skip all letters.
				 */
				bp->val[2] = *++cp - 'a';
				while (*cp != '\0' && *cp != '/')
					cp++;
			}
		} else {
			bp->val[0] = -1; /* no #'s: assume unit 0, no
					    sbus offset/address */
		}
		++bp;
		++nbootpath;
	}
	bp->name[0] = 0;
	
	bootpath_nodes(bootpath, nbootpath);

	/* Setup pointer to boot flags */
	OF_getprop(chosen, "bootargs", buf, sizeof(buf));
	cp = buf;

	/* Find start of boot flags */
	while (*cp) {
		while(*cp == ' ' || *cp == '\t') cp++;
		if (*cp == '-' || *cp == '\0')
			break;
		while(*cp != ' ' && *cp != '\t' && *cp != '\0') cp++;
		
	}
	if (*cp != '-')
		return;

	for (;*++cp;) {
		int fl;

		fl = 0;
		switch(*cp) {
		case 'a':
			fl |= RB_ASKNAME;
			break;
		case 'b':
			fl |= RB_HALT;
			break;
		case 'c':
			fl |= RB_CONFIG;
			break;
		case 'd':
			fl |= RB_KDB;
			break;
		case 's':
			fl |= RB_SINGLE;
			break;
		default:
			break;
		}
		if (!fl) {
			printf("unknown option `%c'\n", *cp);
			continue;
		}
		boothowto |= fl;

		/* specialties */
		if (*cp == 'd') {
#if defined(DDB)
			db_enter();
#else
			printf("kernel has no debugger\n");
#endif
		}
	}
}

/*
 * print out the bootpath
 * the %x isn't 0x%x because the Sun EPROMs do it this way, and
 * consistency with the EPROMs is probably better here.
 */

static void
bootpath_print(struct bootpath *bp)
{
	printf("bootpath: ");
	while (bp->name[0]) {
		if (bp->val[0] == -1)
			printf("/%s%lx", bp->name, bp->val[1]);
		else
			printf("/%s@%lx,%lx", bp->name, bp->val[0], bp->val[1]);
		if (bp->val[2] != 0)
			printf(":%c", (int)bp->val[2] + 'a');
		bp++;
	}
	printf("\n");
}

/*
 * save or read a bootpath pointer from the bootpath store.
 *
 * XXX. required because of SCSI... we don't have control over the "sd"
 * device, so we can't set boot device there.   we patch in with
 * device_register(), and use this to recover the bootpath.
 */
struct bootpath *
bootpath_store(int storep, struct bootpath *bp)
{
	static struct bootpath *save;
	struct bootpath *retval;

	retval = save;
	if (storep)
		save = bp;

	return (retval);
}

/*
 * Determine mass storage and memory configuration for a machine.
 * We get the PROM's root device and make sure we understand it, then
 * attach it as `mainbus0'.
 */
void
cpu_configure(void)
{
#ifdef SUN4V
	int pause = 0;

	if (CPU_ISSUN4V) {
		const char *prop;
		size_t len;
		int idx;

		mdesc_init();
		idx = mdesc_find_node("cpu");
		prop = mdesc_get_prop_data(idx, "hwcap-list", &len);
		if (prop) {
			while (len > 0) {
				if (strcmp(prop, "pause") == 0)
					pause = 1;
				len -= strlen(prop) + 1;
				prop += strlen(prop) + 1;
			}
		}
	}

	if (pause) {
		struct sun4v_patch {
			u_int32_t addr;
			u_int32_t insn;
		} *p;
		paddr_t pa;

		extern struct sun4v_patch sun4v_pause_patch;
		extern struct sun4v_patch sun4v_pause_patch_end;

		/*
		 * Use physical addresses to patch since kernel .text
		 * is already mapped read-only at this point.
		 */
		for (p = &sun4v_pause_patch; p < &sun4v_pause_patch_end; p++) {
			pmap_extract(pmap_kernel(), (vaddr_t)p->addr, &pa);
			stwa(pa, ASI_PHYS_NON_CACHED, p->insn);
			flush((void *)(vaddr_t)p->addr);
		}
	}
#endif

	if (obd.version == BOOTDATA_VERSION &&
	    obd.len >= BOOTDATA_LEN_BOOTHOWTO) {
#if NSOFTRAID > 0
		memcpy(sr_bootuuid.sui_id, obd.sr_uuid,
		    sizeof(sr_bootuuid.sui_id));
		memcpy(sr_bootkey, obd.sr_maskkey, sizeof(sr_bootkey));
#endif
		explicit_bzero(obd.sr_maskkey, sizeof(obd.sr_maskkey));
	}

	/* build the bootpath */
	bootpath_build();

	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}

	/* block clock interrupts and anything below */
	splclock();
	/* Enable device interrupts */
        setpstate(getpstate()|PSTATE_IE);

	if (config_rootfound("mainbus", NULL) == NULL)
		panic("mainbus not configured");

	/* Enable device interrupts */
        setpstate(getpstate()|PSTATE_IE);

	(void)spl0();
	cold = 0;

#ifdef SUN4V
	if (CPU_ISSUN4V)
		sun4v_set_soft_state(SIS_NORMAL, sun4v_soft_state_running);
#endif
}

#ifdef SUN4V

#define HSVC_GROUP_INTERRUPT	0x002
#define HSVC_GROUP_SOFT_STATE	0x003
#define HSVC_GROUP_SDIO		0x108

int sun4v_soft_state_initialized = 0;

void
sun4v_soft_state_init(void)
{
	uint64_t minor;

	if (prom_set_sun4v_api_version(HSVC_GROUP_SOFT_STATE, 1, 0, &minor))
		return;

	prom_sun4v_soft_state_supported();
	sun4v_soft_state_initialized = 1;
}

void
sun4v_set_soft_state(int state, const char *desc)
{
	paddr_t pa;
	int err;

	if (!sun4v_soft_state_initialized)
		return;

	if (!pmap_extract(pmap_kernel(), (vaddr_t)desc, &pa))
		panic("sun4v_set_soft_state: pmap_extract failed");

	err = hv_soft_state_set(state, pa);
	if (err != H_EOK)
		printf("soft_state_set: %d\n", err);
}

void
sun4v_interrupt_init(void)
{
	uint64_t minor;

	if (prom_set_sun4v_api_version(HSVC_GROUP_INTERRUPT, 3, 0, &minor))
		return;

	sun4v_group_interrupt_major = 3;
}

void
sun4v_sdio_init(void)
{
	uint64_t minor;

	if (prom_set_sun4v_api_version(HSVC_GROUP_SDIO, 1, 0, &minor))
		return;

	sun4v_group_sdio_major = 1;
}

#endif

void
diskconf(void)
{
	struct bootpath *bp;
	struct device *bootdv;

	bootpath_print(bootpath);

	bp = nbootpath == 0 ? NULL : &bootpath[nbootpath-1];
	bootdv = (bp == NULL) ? NULL : bp->dev;

#if NMPATH > 0
	if (bootdv != NULL)
		bootdv = mpath_bootdv(bootdv);
#endif

	setroot(bootdv, bp->val[2], RB_USERREQ | RB_HALT);
	dumpconf();
}

char *
clockfreq(long freq)
{
	char *p;
	static char buf[10];

	freq /= 1000;
	snprintf(buf, sizeof buf, "%ld", freq / 1000);
	freq %= 1000;
	if (freq) {
		freq += 1000;	/* now in 1000..1999 */
		p = buf + strlen(buf);
		snprintf(p, buf + sizeof buf - p, "%ld", freq);
		*p = '.';	/* now buf = %d.%3d */
	}
	return (buf);
}

static int
mbprint(void *aux, const char *name)
{
	struct mainbus_attach_args *ma = aux;

	if (name)
		printf("\"%s\" at %s", ma->ma_name, name);
	if (ma->ma_address)
		printf(" addr 0x%08lx", (u_long)ma->ma_address[0]);
	if (ma->ma_pri)
		printf(" ipl %d", ma->ma_pri);
	return (UNCONF);
}

int
findroot(void)
{
	int node;

	if ((node = rootnode) == 0 && (node = OF_peer(0)) == 0)
		panic("no PROM root device");
	rootnode = node;
	return (node);
}

/*
 * Given a `first child' node number, locate the node with the given name.
 * Return the node number, or 0 if not found.
 */
int
findnode(int first, const char *name)
{
	int node;
	char buf[32];

	for (node = first; node; node = OF_peer(node)) {
		if ((OF_getprop(node, "name", buf, sizeof(buf)) > 0) &&
			(strcmp(buf, name) == 0))
			return (node);
	}
	return (0);
}

int
mainbus_match(struct device *parent, void *cf, void *aux)
{
	return (1);
}

/*
 * Attach the mainbus.
 *
 * Our main job is to attach the CPU (the root node we got in cpu_configure())
 * and iterate down the list of `mainbus devices' (children of that node).
 * We also record the `node id' of the default frame buffer, if any.
 */
static void
mainbus_attach(struct device *parent, struct device *dev, void *aux)
{
extern struct sparc_bus_dma_tag mainbus_dma_tag;
extern bus_space_tag_t mainbus_space_tag;

	struct mainbus_attach_args ma;
	char buf[64];
	const char *const *ssp, *sp = NULL;
	int node0, node, rv, len;

	static const char *const openboot_special[] = {
		/* ignore these (end with NULL) */
		/*
		 * These are _root_ devices to ignore. Others must be handled
		 * elsewhere.
		 */
		"virtual-memory",
		"aliases",
		"memory",
		"openprom",
		"options",
		"packages",
		"chosen",
		"counter-timer",
		NULL
	};

	/*
	 * Print the "banner-name" property in dmesg.  It provides a
	 * description of the machine that is generally more
	 * informative than the "name" property.  However, if the
	 * "banner-name" property is missing, fall back on the "name"
	 * property.
	 */
	if (OF_getprop(findroot(), "banner-name", buf, sizeof(buf)) > 0 ||
	    OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0)
		printf(": %s\n", buf);
	else
		printf("\n");

	/*
	 * Base the hw.product and hw.vendor strings on the "name"
	 * property.  They describe the hardware in a much more
	 * consistent way than the "banner-property".
	 */
	if ((len = OF_getprop(findroot(), "name", buf, sizeof(buf))) > 0) {
		hw_prod = malloc(len, M_DEVBUF, M_NOWAIT);
		if (hw_prod)
			strlcpy(hw_prod, buf, len);

		if (strncmp(buf, "SUNW,", 5) == 0)
			hw_vendor = "Sun";
		if (strncmp(buf, "FJSV,", 5) == 0)
			hw_vendor = "Fujitsu";
		if (strncmp(buf, "TAD,", 4) == 0)
			hw_vendor = "Tadpole";
		if (strncmp(buf, "NATE,", 5) == 0)
			hw_vendor = "Naturetech";
		if (strncmp(buf, "ORCL,", 5) == 0)
			hw_vendor = "Oracle";

		/*
		 * The Momentum Leopard-V advertises itself as
		 * SUNW,UltraSPARC-IIi-Engine, but can be
		 * distinguished by looking at the "model" property.
		 */
		if (OF_getprop(findroot(), "model", buf, sizeof(buf)) > 0 &&
		    strncmp(buf, "MOMENTUM,", 9) == 0)
			hw_vendor = "Momentum";
	}

	/* Establish the first component of the boot path */
	bootpath_store(1, bootpath);

	/* We configure the CPUs first. */

	node = findroot();
	for (node0 = OF_child(node); node0; node0 = OF_peer(node0)) {
		if (OF_getprop(node0, "name", buf, sizeof(buf)) <= 0)
			continue;
	}

	for (node = OF_child(node); node; node = OF_peer(node)) {
		if (!checkstatus(node))
			continue;

		/* 
		 * UltraSPARC-IV cpus appear as two "cpu" nodes below
		 * a "cmp" node.
		 */
		if (OF_getprop(node, "name", buf, sizeof(buf)) <= 0)
			continue;
		if (strcmp(buf, "cmp") == 0) {
			bzero(&ma, sizeof(ma));
			ma.ma_node = node;
			ma.ma_name = buf;
			getprop(node, "reg", sizeof(*ma.ma_reg),
			    &ma.ma_nreg, (void **)&ma.ma_reg);
			config_found(dev, &ma, mbprint);
			continue;
		}

		if (OF_getprop(node, "device_type", buf, sizeof(buf)) <= 0)
			continue;
		if (strcmp(buf, "cpu") == 0) {
			bzero(&ma, sizeof(ma));
			ma.ma_node = node;
			OF_getprop(node, "name", buf, sizeof(buf));
			if (strcmp(buf, "cpu") == 0)
				OF_getprop(node, "compatible", buf, sizeof(buf));
			ma.ma_name = buf;
			getprop(node, "reg", sizeof(*ma.ma_reg),
			    &ma.ma_nreg, (void **)&ma.ma_reg);
			config_found(dev, &ma, mbprint);
			continue;
		}
	}

	node = findroot();	/* re-init root node */

	/* Find the "options" node */
	node0 = OF_child(node);
	optionsnode = findnode(node0, "options");
	if (optionsnode == 0)
		panic("no options in OPENPROM");

	for (node0 = OF_child(node); node0; node0 = OF_peer(node0)) {
		if (OF_getprop(node0, "name", buf, sizeof(buf)) <= 0)
			continue;
	}

	/*
	 * Configure the devices, in PROM order.  Skip
	 * PROM entries that are not for devices, or which must be
	 * done before we get here.
	 */
	for (node = OF_child(node); node; node = OF_peer(node)) {
		int portid;

		DPRINTF(ACDB_PROBE, ("Node: %x", node));
		if (OF_getprop(node, "device_type", buf, sizeof(buf)) > 0 &&
		    strcmp(buf, "cpu") == 0)
			continue;
		if (OF_getprop(node, "name", buf, sizeof(buf)) > 0 &&
		    strcmp(buf, "cmp") == 0)
			continue;
		DPRINTF(ACDB_PROBE, (" name %s\n", buf));
		for (ssp = openboot_special; (sp = *ssp) != NULL; ssp++)
			if (strcmp(buf, sp) == 0)
				break;
		if (sp != NULL)
			continue; /* an "early" device already configured */

		if (!checkstatus(node))
			continue;

		bzero(&ma, sizeof ma);
		ma.ma_bustag = mainbus_space_tag;
		ma.ma_dmatag = &mainbus_dma_tag;
		ma.ma_name = buf;
		ma.ma_node = node;
		if (OF_getprop(node, "upa-portid", &portid, sizeof(portid)) !=
		    sizeof(portid)) {
			if (OF_getprop(node, "portid", &portid,
			    sizeof(portid)) != sizeof(portid))
				portid = -1;
		}
		ma.ma_upaid = portid;

		if (getprop(node, "reg", sizeof(*ma.ma_reg), 
			     &ma.ma_nreg, (void **)&ma.ma_reg) != 0)
			continue;
#ifdef DEBUG
		if (autoconf_debug & ACDB_PROBE) {
			if (ma.ma_nreg)
				printf(" reg %08lx.%08lx\n",
					(long)ma.ma_reg->ur_paddr, 
					(long)ma.ma_reg->ur_len);
			else
				printf(" no reg\n");
		}
#endif
		rv = getprop(node, "interrupts", sizeof(*ma.ma_interrupts), 
			&ma.ma_ninterrupts, (void **)&ma.ma_interrupts);
		if (rv != 0 && rv != ENOENT) {
			free(ma.ma_reg, M_DEVBUF, 0);
			continue;
		}
#ifdef DEBUG
		if (autoconf_debug & ACDB_PROBE) {
			if (ma.ma_interrupts)
				printf(" interrupts %08x\n", 
					*ma.ma_interrupts);
			else
				printf(" no interrupts\n");
		}
#endif
		rv = getprop(node, "address", sizeof(*ma.ma_address), 
			&ma.ma_naddress, (void **)&ma.ma_address);
		if (rv != 0 && rv != ENOENT) {
			free(ma.ma_reg, M_DEVBUF, 0);
			free(ma.ma_interrupts, M_DEVBUF, 0);
			continue;
		}
#ifdef DEBUG
		if (autoconf_debug & ACDB_PROBE) {
			if (ma.ma_naddress)
				printf(" address %08x\n", 
					*ma.ma_address);
			else
				printf(" no address\n");
		}
#endif
		config_found(dev, &ma, mbprint);
		free(ma.ma_reg, M_DEVBUF, 0);
		free(ma.ma_interrupts, M_DEVBUF, 0);
		free(ma.ma_address, M_DEVBUF, 0);
	}

	extern int prom_cngetc(dev_t);

	/* Attach PROM console if no other console attached. */
	if (cn_tab->cn_getc == prom_cngetc) {
		bzero(&ma, sizeof ma);
		ma.ma_name = "pcons";
		config_found(dev, &ma, mbprint);
	}

	extern todr_chip_handle_t todr_handle;

	if (todr_handle == NULL) {
		bzero(&ma, sizeof ma);
		ma.ma_name = "prtc";
		config_found(dev, &ma, mbprint);
	}
}

const struct cfattach mainbus_ca = {
	sizeof(struct device), mainbus_match, mainbus_attach
};

int
getprop(int node, char *name, size_t size, int *nitem, void **bufp)
{
	void	*buf;
	long	len;

	*nitem = 0;
	len = getproplen(node, name);
	if (len <= 0)
		return (ENOENT);

	if ((len % size) != 0)
		return (EINVAL);

	buf = *bufp;
	if (buf == NULL) {
		/* No storage provided, so we allocate some */
		buf = malloc(len + 1, M_DEVBUF, M_NOWAIT);
		if (buf == NULL)
			return (ENOMEM);
	}

	OF_getprop(node, name, buf, len);
	*bufp = buf;
	*nitem = len / size;
	return (0);
}


/*
 * Internal form of proplen().  Returns the property length.
 */
long
getproplen(int node, char *name)
{
	return (OF_getproplen(node, name));
}

/*
 * Return a string property.  There is a (small) limit on the length;
 * the string is fetched into a static buffer which is overwritten on
 * subsequent calls.
 */
char *
getpropstring(int node, char *name)
{
	static char stringbuf[32];

	return (getpropstringA(node, name, stringbuf));
}

/* Alternative getpropstring(), where caller provides the buffer */
char *
getpropstringA(int node, char *name, char *buffer)
{
	int blen;

	if (getprop(node, name, 1, &blen, (void **)&buffer) != 0)
		blen = 0;

	buffer[blen] = '\0';	/* usually unnecessary */
	return (buffer);
}

/*
 * Fetch an integer (or pointer) property.
 * The return value is the property, or the default if there was none.
 */
int
getpropint(int node, char *name, int deflt)
{
	int intbuf;

	if (OF_getprop(node, name, &intbuf, sizeof(intbuf)) != sizeof(intbuf))
		return (deflt);

	return (intbuf);
}

int
getpropspeed(int node, char *name)
{
	char buf[128];
	int i, speed = 0;

	if (OF_getprop(node, name, buf, sizeof(buf)) != -1) {
		for (i = 0; i < sizeof(buf); i++) {
			if (buf[i] < '0' || buf[i] > '9')
				break;
			speed *= 10;
			speed += buf[i] - '0';
		}
	}

	if (speed == 0)
		speed = 9600;

	return (speed);
}

/*
 * OPENPROM functions.  These are here mainly to hide the OPENPROM interface
 * from the rest of the kernel.
 */
int
firstchild(int node)
{

	return OF_child(node);
}

int
nextsibling(int node)
{

	return OF_peer(node);
}

int
checkstatus(int node)
{
	char buf[32];

	/* If there is no "status" property, assume everything is fine. */
	if (OF_getprop(node, "status", buf, sizeof(buf)) <= 0)
		return 1;

	/*
	 * If OpenBoot Diagnostics discovers a problem with a device
	 * it will mark it with "fail" or "fail-xxx", where "xxx" is
	 * additional human-readable information about the particular
	 * fault-condition.
	 */
	if (strcmp(buf, "disabled") == 0 || strncmp(buf, "fail", 4) == 0)
		return 0;

	return 1;
}

/* returns 1 if node has given property */
int
node_has_property(int node, const char *prop)
{
	return (OF_getproplen(node, (caddr_t)prop) != -1);
}

/*
 * Try to figure out where the PROM stores the cursor row & column
 * variables.  Returns nonzero on error.
 */
int
romgetcursoraddr(int **rowp, int **colp)
{
	cell_t row = 0, col = 0;

	OF_interpret("stdout @ is my-self addr line# addr column# ",
	    2, &col, &row);

	/*
	 * We are running on a 64-bit big-endian machine, so these things
	 * point to 64-bit big-endian values.  To convert them to pointers
	 * to int, add 4 to the address.
	 */
	if (row == 0 || col == 0)
		return (-1);
	*rowp = (int *)(row + 4);
	*colp = (int *)(col + 4);
	return (0);
}

void
callrom(void)
{

	__asm volatile("wrpr	%%g0, 0, %%tl" : );
	OF_enter();
}

/*
 * find a device matching "name" and unit number
 */
struct device *
getdevunit(const char *name, int unit)
{
	struct device *dev = TAILQ_FIRST(&alldevs);
	char num[10], fullname[16];
	int lunit;

	/* compute length of name and decimal expansion of unit number */
	snprintf(num, sizeof num, "%d", unit);
	lunit = strlen(num);
	if (strlen(name) + lunit >= sizeof(fullname) - 1)
		panic("config_attach: device name too long");

	strlcpy(fullname, name, sizeof fullname);
	strlcat(fullname, num, sizeof fullname);

	while (strcmp(dev->dv_xname, fullname) != 0) {
		if ((dev = TAILQ_NEXT(dev, dv_list)) == NULL)
			return NULL;
	}
	return dev;
}

void
device_register(struct device *dev, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	struct pci_attach_args *pa = aux;
	struct sbus_attach_args *sa = aux;
	struct vbus_attach_args *va = aux;
	struct cbus_attach_args *ca = aux;
	struct bootpath *bp = bootpath_store(0, NULL);
	struct device *busdev = dev->dv_parent;
	const char *devname = dev->dv_cfdata->cf_driver->cd_name;
	const char *busname;
	int node = -1;

	/*
	 * There is no point in continuing if we've exhausted all
	 * bootpath components.
	 */
	if (bp == NULL)
		return;

	DPRINTF(ACDB_BOOTDEV,
	    ("\n%s: device_register: devname %s(%s) component %s\n",
	    dev->dv_xname, devname, dev->dv_xname, bp->name));

	/*
	 * Ignore mainbus0 itself, it certainly is not a boot device.
	 */
	if (busdev == NULL)
		return;

	/*
	 * We don't know the type of 'aux'; it depends on the bus this
	 * device attaches to.  We are only interested in certain bus
	 * types; this is only used to find the boot device.
	 */
	busname = busdev->dv_cfdata->cf_driver->cd_name;
	if (strcmp(busname, "mainbus") == 0 ||
	    strcmp(busname, "ssm") == 0 || strcmp(busname, "upa") == 0)
		node = ma->ma_node;
	else if (strcmp(busname, "sbus") == 0 ||
	    strcmp(busname, "dma") == 0 || strcmp(busname, "ledma") == 0)
		node = sa->sa_node;
	else if (strcmp(busname, "vbus") == 0)
		node = va->va_node;
	else if (strcmp(busname, "cbus") == 0)
		node = ca->ca_node;
	else if (strcmp(busname, "pci") == 0)
		node = PCITAG_NODE(pa->pa_tag);

	if (node == bootnode) {
		if (strcmp(devname, "vdsk") == 0) {
			/*
			 * For virtual disks, don't nail the boot
			 * device just yet.  Instead, we add fake a
			 * SCSI target/lun, such that we match it the
			 * next time around.
			 */
			bp->dev = dev;
			(bp + 1)->val[0] = 0;
			(bp + 1)->val[1] = 0;
			nbootpath++;
			bootpath_store(1, bp + 1);
			return;
		}

		nail_bootdev(dev, bp);
		return;
	}

	if (node == bp->node) {
		bp->dev = dev;
		DPRINTF(ACDB_BOOTDEV, ("\t-- matched component %s to %s\n",
		    bp->name, dev->dv_xname));
		bootpath_store(1, bp + 1);
		return;
	}

	if (strcmp(devname, "scsibus") == 0) {
		/*
		 * Booting from anything but the first (physical) port
		 * isn't supported by OBP.
		 */
		if (strcmp(bp->name, "fp") == 0 && bp->val[0] == 0) {
			DPRINTF(ACDB_BOOTDEV, ("\t-- matched component %s to %s\n",
			    bp->name, dev->dv_xname));
			bootpath_store(1, bp + 1);
			return;
		}
	}

	if (strcmp(busname, "scsibus") == 0) {
		/*
		 * A SCSI disk or cd; retrieve target/lun information
		 * from parent and match with current bootpath component.
		 * Note that we also have look back past the `scsibus'
		 * device to determine whether this target is on the
		 * correct controller in our boot path.
		 */
		struct scsi_attach_args *sa = aux;
		struct scsi_link *sl = sa->sa_sc_link;
		u_int target = bp->val[0];
		u_int lun = bp->val[1];

		if (bp->val[0] & 0xffffffff00000000 && bp->val[0] != -1) {
			/* Hardware RAID or Fibre channel? */
			if (bp->val[0] == sl->port_wwn && lun == sl->lun) {
				nail_bootdev(dev, bp);
			}

			/*
			 * sata devices on some controllers don't get
			 * port_wwn filled in, so look at devid too.
			 */
			if (sl->id && sl->id->d_len == 8 &&
			    sl->id->d_type == DEVID_NAA &&
			    memcmp(sl->id + 1, &bp->val[0], 8) == 0)
				nail_bootdev(dev, bp);
			return;
		}

		/* Check the controller that this scsibus is on. */
		if ((bp-1)->dev != sl->bus->sc_dev.dv_parent)
			return;

		/*
		 * Bounds check: we know the target and lun widths.
		 */
		if (target >= sl->bus->sb_adapter_buswidth ||
		    lun >= sl->bus->sb_luns) {
			printf("SCSI disk bootpath component not accepted: "
			       "target %u; lun %u\n", target, lun);
			return;
		}

		if (target == sl->target && lun == sl->lun) {
			nail_bootdev(dev, bp);
			return;
		}
	}

	if (strcmp("wd", devname) == 0) {
		/* IDE disks. */
		struct ata_atapi_attach *aa = aux;
		u_int channel, drive;

		if (strcmp(bp->name, "ata") == 0 &&
		    bp->val[0] == aa->aa_channel) {
			channel = bp->val[0]; bp++;
			drive = bp->val[0];
		} else {
			channel = bp->val[0] / 2;
			drive = bp->val[0] % 2;
		}

		if (channel == aa->aa_channel &&
		    drive == aa->aa_drv_data->drive) {
			nail_bootdev(dev, bp);
			return;
		}
	}
}

void
nail_bootdev(struct device *dev, struct bootpath *bp)
{

	if (bp->dev != NULL)
		panic("device_register: already got a boot device: %s",
			bp->dev->dv_xname);

	/*
	 * Mark this bootpath component by linking it to the matched
	 * device. We pick up the device pointer in cpu_rootconf().
	 */
	booted_device = bp->dev = dev;
	DPRINTF(ACDB_BOOTDEV, ("\t-- found bootdevice: %s\n",dev->dv_xname));

	/*
	 * Then clear the current bootpath component, so we don't spuriously
	 * match similar instances on other busses, e.g. a disk on
	 * another SCSI bus with the same target.
	 */
	bootpath_store(1, NULL);
}

const struct nam2blk nam2blk[] = {
	{ "rd",		 5 },
	{ "sd",		 7 },
	{ "vnd",	 8 },
	{ "wd",		12 },
	{ "fd",		16 },
	{ "cd",		18 },
	{ NULL,		-1 }
};
