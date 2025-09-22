/*	$OpenBSD: machdep.c,v 1.74 2023/01/07 17:29:37 miod Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/mount.h>
#include <sys/msgbuf.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/syscallargs.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <machine/cpufunc.h>
#include <machine/fpu.h>
#include <machine/opal.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include <net/if.h>
#include <uvm/uvm_extern.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/cons.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#endif

int cacheline_size = 128;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

int cold = 1;
int safepri = 0;
int physmem;
paddr_t physmax;

struct vm_map *exec_map;
struct vm_map *phys_map;

char machine[] = MACHINE;

struct user *proc0paddr;

caddr_t ssym, esym;

extern char _start[], _end[];
extern char __bss_start[];

extern uint64_t opal_base;
extern uint64_t opal_entry;
int opal_have_console_flush;

extern char trapcode[], trapcodeend[];
extern char hvtrapcode[], hvtrapcodeend[];
extern char rsttrapcode[], rsttrapcodeend[];
extern char slbtrapcode[], slbtrapcodeend[];
extern char generictrap[];
extern char generichvtrap[];
extern char kern_slbtrap[];
extern char cpu_idle_restore_context[];

extern char initstack[];

struct fdt_reg memreg[VM_PHYSSEG_MAX];
int nmemreg;

#ifdef DDB
struct fdt_reg initrd_reg;
#endif

void memreg_add(const struct fdt_reg *);
void memreg_remove(const struct fdt_reg *);

uint8_t *bootmac = NULL;

void parse_bootargs(const char *);
const char *parse_bootduid(const char *);
const char *parse_bootmac(const char *);

paddr_t fdt_pa;
size_t fdt_size;

int stdout_node;
int stdout_speed;

static int
atoi(const char *s)
{
	int n, neg;

	n = 0;
	neg = 0;

	while (*s == '-') {
		s++;
		neg = !neg;
	}

	while (*s != '\0') {
		if (*s < '0' || *s > '9')
			break;

		n = (10 * n) + (*s - '0');
		s++;
	}

	return (neg ? -n : n);
}

void *
fdt_find_cons(void)
{
	char *alias = "serial0";
	char buf[128];
	char *stdout = NULL;
	char *p;
	void *node;

	/* First check if "stdout-path" is set. */
	node = fdt_find_node("/chosen");
	if (node) {
		if (fdt_node_property(node, "stdout-path", &stdout) > 0) {
			if (strchr(stdout, ':') != NULL) {
				strlcpy(buf, stdout, sizeof(buf));
				if ((p = strchr(buf, ':')) != NULL) {
					*p++ = '\0';
					stdout_speed = atoi(p);
				}
				stdout = buf;
			}
			if (stdout[0] != '/') {
				/* It's an alias. */
				alias = stdout;
				stdout = NULL;
			}
		}
	}

	/* Perform alias lookup if necessary. */
	if (stdout == NULL) {
		node = fdt_find_node("/aliases");
		if (node)
			fdt_node_property(node, alias, &stdout);
	}

	/* Lookup the physical address of the interface. */
	if (stdout) {
		node = fdt_find_node(stdout);
		if (node) {
			stdout_node = OF_finddevice(stdout);
			return (node);
		}
	}

	return (NULL);
}

void
init_powernv(void *fdt, void *tocbase)
{
	struct fdt_reg reg;
	register_t uspace;
	paddr_t trap;
	uint64_t msr;
	void *node;
	char *prop;
	int len;
	int i;

	/* Store pointer to our struct cpu_info. */
	__asm volatile ("mtsprg0 %0" :: "r"(cpu_info_primary));

	/* Clear BSS. */
	memset(__bss_start, 0, _end - __bss_start);

	if (!fdt_init(fdt) || fdt_get_size(fdt) == 0)
		panic("no FDT");

	/* Get OPAL base and entry addresses from FDT. */
	node = fdt_find_node("/ibm,opal");
	if (node) {
		fdt_node_property(node, "opal-base-address", &prop);
		opal_base = bemtoh64((uint64_t *)prop);
		fdt_node_property(node, "opal-entry-address", &prop);
		opal_entry = bemtoh64((uint64_t *)prop);
		fdt_node_property(node, "compatible", &prop);

		opal_reinit_cpus(OPAL_REINIT_CPUS_HILE_BE);

		/*
		 * The following call will fail on Power ISA 2.0x CPUs,
		 * but that is fine since they don't support Radix Tree
		 * translation.  On Power ISA 3.0 CPUs this will make
		 * the full TLB available.
		 */
		opal_reinit_cpus(OPAL_REINIT_CPUS_MMU_HASH);

		/* Older firmware doesn't implement OPAL_CONSOLE_FLUSH. */
		if (opal_check_token(OPAL_CONSOLE_FLUSH) == OPAL_TOKEN_PRESENT)
			opal_have_console_flush = 1;
	}

	/* At this point we can call OPAL runtime services and use printf(9). */
	printf("Hello, World!\n");

	/* Stash these such that we can remap the FDT later. */
	fdt_pa = (paddr_t)fdt;
	fdt_size = fdt_get_size(fdt);

	fdt_find_cons();

	/*
	 * Initialize all traps with the stub that calls the generic
	 * trap handler.
	 */
	for (trap = EXC_RST; trap < EXC_END; trap += 32)
		memcpy((void *)trap, trapcode, trapcodeend - trapcode);

	/* Hypervisor interrupts needs special handling. */
	memcpy((void *)EXC_HDSI, hvtrapcode, hvtrapcodeend - hvtrapcode);
	memcpy((void *)EXC_HISI, hvtrapcode, hvtrapcodeend - hvtrapcode);
	memcpy((void *)EXC_HEA, hvtrapcode, hvtrapcodeend - hvtrapcode);
	memcpy((void *)EXC_HMI, hvtrapcode, hvtrapcodeend - hvtrapcode);
	memcpy((void *)EXC_HFAC, hvtrapcode, hvtrapcodeend - hvtrapcode);
	memcpy((void *)EXC_HVI, hvtrapcode, hvtrapcodeend - hvtrapcode);

	/* System reset trap needs special handling. */
	memcpy((void *)EXC_RST, rsttrapcode, rsttrapcodeend - rsttrapcode);

	/* SLB trap needs special handling as well. */
	memcpy((void *)EXC_DSE, slbtrapcode, slbtrapcodeend - slbtrapcode);

	*((void **)TRAP_ENTRY) = generictrap;
	*((void **)TRAP_HVENTRY) = generichvtrap;
	*((void **)TRAP_SLBENTRY) = kern_slbtrap;
	*((void **)TRAP_RSTENTRY) = cpu_idle_restore_context;

	/* Make the stubs visible to the CPU. */
	__syncicache(EXC_RSVD, EXC_END - EXC_RSVD);

	/* We're now ready to take traps. */
	msr = mfmsr();
	mtmsr(msr | (PSL_ME|PSL_RI));

	cpu_init_features();
	cpu_init();

	/* Add all memory. */
	node = fdt_find_node("/");
	for (node = fdt_child_node(node); node; node = fdt_next_node(node)) {
		len = fdt_node_property(node, "device_type", &prop);
		if (len <= 0)
			continue;
		if (strcmp(prop, "memory") != 0)
			continue;
		for (i = 0; nmemreg < nitems(memreg); i++) {
			if (fdt_get_reg(node, i, &reg))
				break;
			if (reg.size == 0)
				continue;
			memreg_add(&reg);
			physmem += atop(reg.size);
			physmax = MAX(physmax, reg.addr + reg.size);
		}
	}

	/* Remove reserved memory. */
	node = fdt_find_node("/reserved-memory");
	if (node) {
		for (node = fdt_child_node(node); node;
		     node = fdt_next_node(node)) {
			if (fdt_get_reg(node, 0, &reg))
				continue;
			if (reg.size == 0)
				continue;
			memreg_remove(&reg);
		}
	}

	/* Remove interrupt vectors. */
	reg.addr = trunc_page(EXC_RSVD);
	reg.size = round_page(EXC_END);
	memreg_remove(&reg);

	/* Remove kernel. */
	reg.addr = trunc_page((paddr_t)_start);
	reg.size = round_page((paddr_t)_end) - reg.addr;
	memreg_remove(&reg);

	/* Remove FDT. */
	reg.addr = trunc_page((paddr_t)fdt);
	reg.size = round_page((paddr_t)fdt + fdt_get_size(fdt)) - reg.addr;
	memreg_remove(&reg);

#ifdef DDB
	/* Load symbols from initrd. */
	db_machine_init();
	if (initrd_reg.size != 0)
		memreg_remove(&initrd_reg);
	ssym = (caddr_t)initrd_reg.addr;
	esym = ssym + initrd_reg.size;
#endif

	pmap_bootstrap();
	uvm_setpagesize();

	for (i = 0; i < nmemreg; i++) {
		paddr_t start = memreg[i].addr;
		paddr_t end = start + memreg[i].size;

		uvm_page_physload(atop(start), atop(end),
		    atop(start), atop(end), 0);
	}

	/* Enable translation. */
	msr = mfmsr();
	mtmsr(msr | (PSL_DR|PSL_IR));
	isync();

	initmsgbuf((caddr_t)uvm_pageboot_alloc(MSGBUFSIZE), MSGBUFSIZE);

	proc0paddr = (struct user *)initstack;
	proc0.p_addr = proc0paddr;
	curpcb = &proc0.p_addr->u_pcb;
	uspace = (register_t)proc0paddr + USPACE - FRAMELEN;
	proc0.p_md.md_regs = (struct trapframe *)uspace;
}

void
memreg_add(const struct fdt_reg *reg)
{
	int i;

	for (i = 0; i < nmemreg; i++) {
		if (reg->addr == memreg[i].addr + memreg[i].size) {
			memreg[i].size += reg->size;
			return;
		}
		if (reg->addr + reg->size == memreg[i].addr) {
			memreg[i].addr = reg->addr;
			memreg[i].size += reg->size;
			return;
		}
	}

	if (nmemreg >= nitems(memreg))
		return;

	memreg[nmemreg++] = *reg;
}

void
memreg_remove(const struct fdt_reg *reg)
{
	uint64_t start = reg->addr;
	uint64_t end = reg->addr + reg->size;
	int i, j;

	for (i = 0; i < nmemreg; i++) {
		uint64_t memstart = memreg[i].addr;
		uint64_t memend = memreg[i].addr + memreg[i].size;

		if (end <= memstart)
			continue;
		if (start >= memend)
			continue;

		if (start <= memstart)
			memstart = MIN(end, memend);
		if (end >= memend)
			memend = MAX(start, memstart);

		if (start > memstart && end < memend) {
			if (nmemreg < nitems(memreg)) {
				memreg[nmemreg].addr = end;
				memreg[nmemreg].size = memend - end;
				nmemreg++;
			}
			memend = start;
		}
		memreg[i].addr = memstart;
		memreg[i].size = memend - memstart;
	}

	/* Remove empty slots. */
	for (i = nmemreg - 1; i >= 0; i--) {
		if (memreg[i].size == 0) {
			for (j = i; (j + 1) < nmemreg; j++)
				memreg[j] = memreg[j + 1];
			nmemreg--;
		}
	}
}

#define R_PPC64_RELATIVE	22
#define ELF_R_TYPE_RELATIVE	R_PPC64_RELATIVE

/*
 * Disable optimization for this function to prevent clang from
 * generating jump tables that need relocation.
 */
__attribute__((optnone)) void
self_reloc(Elf_Dyn *dynamic, Elf_Addr base)
{
	Elf_Word relasz = 0, relaent = sizeof(Elf_RelA);
	Elf_RelA *rela = NULL;
	Elf_Addr *addr;
	Elf_Dyn *dynp;

	for (dynp = dynamic; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_RELA:
			rela = (Elf_RelA *)(dynp->d_un.d_ptr + base);
			break;
		case DT_RELASZ:
			relasz = dynp->d_un.d_val;
			break;
		case DT_RELAENT:
			relaent = dynp->d_un.d_val;
			break;
		}
	}

	while (relasz > 0) {
		switch (ELF_R_TYPE(rela->r_info)) {
		case ELF_R_TYPE_RELATIVE:
			addr = (Elf_Addr *)(base + rela->r_offset);
			*addr = base + rela->r_addend;
			break;
		}
		rela = (Elf_RelA *)((caddr_t)rela + relaent);
		relasz -= relaent;
	}
}

void *
opal_phys(void *va)
{
	paddr_t pa;

	pmap_extract(pmap_kernel(), (vaddr_t)va, &pa);
	return (void *)pa;
}

void
opal_printf(const char *fmt, ...)
{
	static char buf[256];
	uint64_t len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (len == (uint64_t)-1)
		len = 0;
	else if (len >= sizeof(buf))
		 len = sizeof(buf) - 1;
	va_end(ap);

	opal_console_write(0, opal_phys(&len), opal_phys(buf));
}

int64_t
opal_do_console_flush(int64_t term_number)
{
	uint64_t events;
	int64_t error;

	if (opal_have_console_flush) {
		error = opal_console_flush(term_number);
		if (error == OPAL_BUSY_EVENT) {
			opal_poll_events(NULL);
			error = OPAL_BUSY;
		}
		return error;
	} else {
		opal_poll_events(opal_phys(&events));
		if (events & OPAL_EVENT_CONSOLE_OUTPUT)
			return OPAL_BUSY;
		return OPAL_SUCCESS;
	}
}

void
opal_cnprobe(struct consdev *cd)
{
}

void
opal_cninit(struct consdev *cd)
{
}

int
opal_cngetc(dev_t dev)
{
	uint64_t len;
	char ch;

	for (;;) {
		len = 1;
		opal_console_read(0, opal_phys(&len), opal_phys(&ch));
		if (len)
			return ch;
		opal_poll_events(NULL);
	}
}

void
opal_cnputc(dev_t dev, int c)
{
	uint64_t len = 1;
	char ch = c;
	int64_t error;

	opal_console_write(0, opal_phys(&len), opal_phys(&ch));
	while (1) {
		error = opal_do_console_flush(0);
		if (error != OPAL_BUSY && error != OPAL_PARTIAL)
			break;
		delay(1);
	}
}

void
opal_cnpollc(dev_t dev, int on)
{
}

struct consdev opal_consdev = {
	.cn_probe = opal_cnprobe,
	.cn_init = opal_cninit,
	.cn_getc = opal_cngetc,
	.cn_putc = opal_cnputc,
	.cn_pollc = opal_cnpollc,
};

struct consdev *cn_tab = &opal_consdev;

int
copyin(const void *uaddr, void *kaddr, size_t len)
{
	pmap_t pm = curproc->p_vmspace->vm_map.pmap;
	vaddr_t kva;
	vsize_t klen;
	int error;

	while (len > 0) {
		error = pmap_set_user_slb(pm, (vaddr_t)uaddr, &kva, &klen);
		if (error)
			return error;
		if (klen > len)
			klen = len;
		error = kcopy((const void *)kva, kaddr, klen);
		pmap_unset_user_slb();
		if (error)
			return error;

		uaddr = (const char *)uaddr + klen;
		kaddr = (char *)kaddr + klen;
		len -= klen;
	}

	return 0;
}

int
copyin32(const uint32_t *uaddr, uint32_t *kaddr)
{
	return copyin(uaddr, kaddr, sizeof(uint32_t));
}

int
copyout(const void *kaddr, void *uaddr, size_t len)
{
	pmap_t pm = curproc->p_vmspace->vm_map.pmap;
	vaddr_t kva;
	vsize_t klen;
	int error;

	while (len > 0) {
		error = pmap_set_user_slb(pm, (vaddr_t)uaddr, &kva, &klen);
		if (error)
			return error;
		if (klen > len)
			klen = len;
		error = kcopy(kaddr, (void *)kva, klen);
		pmap_unset_user_slb();
		if (error)
			return error;

		kaddr = (const char *)kaddr + klen;
		uaddr = (char *)uaddr + klen;
		len -= klen;
	}

	return 0;
}

/* in locore.S */
extern int copystr(const void *, void *, size_t, size_t *)
		__attribute__ ((__bounded__(__string__,2,3)));

int
copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
	pmap_t pm = curproc->p_vmspace->vm_map.pmap;
	vaddr_t kva;
	vsize_t klen;
	size_t count, total;
	int error = 0;

	if (len == 0)
		return ENAMETOOLONG;

	total = 0;
	while (len > 0) {
		error = pmap_set_user_slb(pm, (vaddr_t)uaddr, &kva, &klen);
		if (error)
			goto out;
		if (klen > len)
			klen = len;
		error = copystr((const void *)kva, kaddr, klen, &count);
		total += count;
		pmap_unset_user_slb();
		if (error == 0 || error == EFAULT)
			goto out;

		uaddr = (const char *)uaddr + klen;
		kaddr = (char *)kaddr + klen;
		len -= klen;
	}

out:
	if (done)
		*done = total;
	return error;
}

int
copyoutstr(const void *kaddr, void *uaddr, size_t len, size_t *done)
{
	pmap_t pm = curproc->p_vmspace->vm_map.pmap;
	vaddr_t kva;
	vsize_t klen;
	size_t count, total;
	int error = 0;

	if (len == 0)
		return ENAMETOOLONG;

	total = 0;
	while (len > 0) {
		error = pmap_set_user_slb(pm, (vaddr_t)uaddr, &kva, &klen);
		if (error)
			goto out;
		if (klen > len)
			klen = len;
		error = copystr(kaddr, (void *)kva, klen, &count);
		total += count;
		pmap_unset_user_slb();
		if (error == 0 || error == EFAULT)
			goto out;

		kaddr = (const char *)kaddr + klen;
		uaddr = (char *)uaddr + klen;
		len -= klen;
	}

out:
	if (done)
		*done = total;
	return error;
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc) {
		aston(ci->ci_curproc);
		cpu_kick(ci);
	}
}

void
cpu_startup(void)
{
	vaddr_t minaddr, maxaddr, va;
	paddr_t pa, epa;
	void *fdt;
	void *node;
	char *prop;
	int len;

	printf("%s", version);

	printf("real mem  = %lu (%luMB)\n", ptoa(physmem),
	    ptoa(physmem)/1024/1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio.
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024/1024);

	/* Remap the FDT. */
	pa = trunc_page(fdt_pa);
	epa = round_page(fdt_pa + fdt_size);
	va = (vaddr_t)km_alloc(epa - pa, &kv_any, &kp_none, &kd_waitok);
	fdt = (void *)(va + (fdt_pa & PAGE_MASK));
	while (pa < epa) {
		pmap_kenter_pa(va, pa, PROT_READ | PROT_WRITE);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}

	if (!fdt_init(fdt) || fdt_get_size(fdt) == 0)
		panic("can't remap FDT");

	intr_init();

	node = fdt_find_node("/chosen");
	if (node) {
		len = fdt_node_property(node, "bootargs", &prop);
		if (len > 0)
			parse_bootargs(prop);

		len = fdt_node_property(node, "openbsd,boothowto", &prop);
		if (len == sizeof(boothowto))
			boothowto = bemtoh32((uint32_t *)prop);

		len = fdt_node_property(node, "openbsd,bootduid", &prop);
		if (len == sizeof(bootduid))
			memcpy(bootduid, prop, sizeof(bootduid));
	}

	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}
}

void
parse_bootargs(const char *bootargs)
{
	const char *cp = bootargs;

	if (strncmp(cp, "bootduid=", strlen("bootduid=")) == 0)
		cp = parse_bootduid(cp + strlen("bootduid="));

	if (strncmp(cp, "bootmac=", strlen("bootmac=")) == 0)
		cp = parse_bootmac(cp + strlen("bootmac="));

	while (*cp != '-')
		if (*cp++ == '\0')
			return;
	cp++;

	while (*cp != 0) {
		switch(*cp) {
		case 'a':
			boothowto |= RB_ASKNAME;
			break;
		case 'c':
			boothowto |= RB_CONFIG;
			break;
		case 'd':
			boothowto |= RB_KDB;
			break;
		case 's':
			boothowto |= RB_SINGLE;
			break;
		default:
			printf("unknown option `%c'\n", *cp);
			break;
		}
		cp++;
	}
}

const char *
parse_bootduid(const char *bootarg)
{
	const char *cp = bootarg;
	uint64_t duid = 0;
	int digit, count = 0;

	while (count < 16) {
		if (*cp >= '0' && *cp <= '9')
			digit = *cp - '0';
		else if (*cp >= 'a' && *cp <= 'f')
			digit = *cp - 'a' + 10;
		else
			break;
		duid *= 16;
		duid += digit;
		count++;
		cp++;
	}

	if (count > 0) {
		memcpy(&bootduid, &duid, sizeof(bootduid));
		return cp;
	}

	return bootarg;
}

const char *
parse_bootmac(const char *bootarg)
{
	static uint8_t lladdr[6];
	const char *cp = bootarg;
	int digit, count = 0;

	memset(lladdr, 0, sizeof(lladdr));

	while (count < 12) {
		if (*cp >= '0' && *cp <= '9')
			digit = *cp - '0';
		else if (*cp >= 'a' && *cp <= 'f')
			digit = *cp - 'a' + 10;
		else if (*cp == ':') {
			cp++;
			continue;
		} else
			break;
		lladdr[count / 2] |= digit << (4 * !(count % 2));
		count++;
		cp++;
	}

	if (count > 0) {
		bootmac = lladdr;
		return cp;
	}

	return bootarg;
}

#define PSL_USER \
    (PSL_SF | PSL_HV | PSL_EE | PSL_PR | PSL_ME | PSL_IR | PSL_DR | PSL_RI)

void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct trapframe *frame = p->p_md.md_regs;
	struct pcb *pcb = &p->p_addr->u_pcb;

	memset(frame, 0, sizeof(*frame));
	frame->fixreg[1] = stack;
	frame->fixreg[3] = arginfo->ps_nargvstr;
	frame->fixreg[4] = (register_t)arginfo->ps_argvstr;
	frame->fixreg[5] = (register_t)arginfo->ps_envstr;
	frame->fixreg[6] = (register_t)pack->ep_auxinfo;
	frame->fixreg[12] = pack->ep_entry;
	frame->srr0 = pack->ep_entry;
	frame->srr1 = PSL_USER;

	memset(&pcb->pcb_slb, 0, sizeof(pcb->pcb_slb));
	memset(&pcb->pcb_fpstate, 0, sizeof(pcb->pcb_fpstate));
	pcb->pcb_flags = 0;
}

int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct trapframe *tf = p->p_md.md_regs;
	struct sigframe *fp, frame;
	siginfo_t *sip = NULL;
	int i;

	/* Allocate space for the signal handler context. */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(tf->fixreg[1]) && onstack)
		fp = (struct sigframe *)
		    trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size);
	else
		fp = (struct sigframe *)tf->fixreg[1];

	fp = (struct sigframe *)(STACKALIGN(fp - 1) - 288);

	/* Save FPU state to PCB if necessary. */
	if (pcb->pcb_flags & (PCB_FPU|PCB_VEC|PCB_VSX) &&
	    tf->srr1 & (PSL_FPU|PSL_VEC|PSL_VSX)) {
		tf->srr1 &= ~(PSL_FPU|PSL_VEC|PSL_VSX);
		save_vsx(p);
	}

	/* Build stack frame for signal trampoline. */
	memset(&frame, 0, sizeof(frame));
	frame.sf_signum = sig;

	/* Save register context. */
	for (i = 0; i < 32; i++)
		frame.sf_sc.sc_reg[i] = tf->fixreg[i];
	frame.sf_sc.sc_lr = tf->lr;
	frame.sf_sc.sc_cr = tf->cr;
	frame.sf_sc.sc_xer = tf->xer;
	frame.sf_sc.sc_ctr = tf->ctr;
	frame.sf_sc.sc_pc = tf->srr0;
	frame.sf_sc.sc_ps = PSL_USER;
	frame.sf_sc.sc_vrsave = tf->vrsave;

	/* Copy the saved FPU state into the frame if necessary. */
	if (pcb->pcb_flags & (PCB_FPU|PCB_VEC|PCB_VSX)) {
		memcpy(frame.sf_sc.sc_vsx, pcb->pcb_fpstate.fp_vsx,
		    sizeof(pcb->pcb_fpstate.fp_vsx));
		frame.sf_sc.sc_fpscr = pcb->pcb_fpstate.fp_fpscr;
		frame.sf_sc.sc_vscr = pcb->pcb_fpstate.fp_vscr;
	}

	/* Save signal mask. */
	frame.sf_sc.sc_mask = mask;

	if (info) {
		sip = &fp->sf_si;
		frame.sf_si = *ksip;
	}

	frame.sf_sc.sc_cookie = (long)&fp->sf_sc ^ p->p_p->ps_sigcookie;
	if (copyout(&frame, fp, sizeof(frame)))
		return 1;

	/*
	 * Build context to run handler in.
	 */
	tf->fixreg[1] = (register_t)fp;
	tf->fixreg[3] = sig;
	tf->fixreg[4] = (register_t)sip;
	tf->fixreg[5] = (register_t)&fp->sf_sc;
	tf->fixreg[12] = (register_t)catcher;

	tf->srr0 = p->p_p->ps_sigcode;

	return 0;
}

int
sys_sigreturn(struct proc *p, void *v, register_t *retval)
{
	struct sys_sigreturn_args /* {
		syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext ksc, *scp = SCARG(uap, sigcntxp);
	struct trapframe *tf = p->p_md.md_regs;
	struct pcb *pcb = &p->p_addr->u_pcb;
	int error;
	int i;

	if (PROC_PC(p) != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return EPERM;
	}

	if ((error = copyin(scp, &ksc, sizeof ksc)))
		return error;

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return EFAULT;
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof (ksc.sc_cookie));

	/* Make sure the processor mode has not been tampered with. */
	if (ksc.sc_ps != PSL_USER)
		return EINVAL;

	/* Restore register context. */
	for (i = 0; i < 32; i++)
		tf->fixreg[i] = ksc.sc_reg[i];
	tf->lr = ksc.sc_lr;
	tf->cr = ksc.sc_cr;
	tf->xer = ksc.sc_xer;
	tf->ctr = ksc.sc_ctr;
	tf->srr0 = ksc.sc_pc;
	tf->srr1 = ksc.sc_ps;
	tf->vrsave = ksc.sc_vrsave;

	/* Write saved FPU state back to PCB if necessary. */
	if (pcb->pcb_flags & (PCB_FPU|PCB_VEC|PCB_VSX)) {
		memcpy(pcb->pcb_fpstate.fp_vsx, ksc.sc_vsx,
		    sizeof(pcb->pcb_fpstate.fp_vsx));
		pcb->pcb_fpstate.fp_fpscr = ksc.sc_fpscr;
		pcb->pcb_fpstate.fp_vscr = ksc.sc_vscr;
	}

	/* Restore signal mask. */
	p->p_sigmask = ksc.sc_mask & ~sigcantmask;

	return EJUSTRETURN;
}

/*
 * Notify the current process (p) that it has a signal pending,
 * process as soon as possible.
 */
void
signotify(struct proc *p)
{
	aston(p);
	cpu_kick(p->p_cpu);
}

void	cpu_switchto_asm(struct proc *, struct proc *);

void
cpu_switchto(struct proc *old, struct proc *new)
{
	if (old) {
		struct pcb *pcb = &old->p_addr->u_pcb;
		struct trapframe *tf = old->p_md.md_regs;

		if (pcb->pcb_flags & (PCB_FPU|PCB_VEC|PCB_VSX) &&
		    tf->srr1 & (PSL_FPU|PSL_VEC|PSL_VSX)) {
			tf->srr1 &= ~(PSL_FPU|PSL_VEC|PSL_VSX);
			save_vsx(old);
		}

		pmap_clear_user_slb();
	}

	cpu_switchto_asm(old, new);
}

/*
 * machine dependent system variables.
 */

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	int altivec = 1;	/* Altivec is always supported */

	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return ENOTDIR;		/* overloaded */

	switch (name[0]) {
	case CPU_ALTIVEC:
		return (sysctl_rdint(oldp, oldlenp, newp, altivec));
	default:
		return EOPNOTSUPP;
	}
	/* NOTREACHED */
}

void
consinit(void)
{
}

void
opal_powerdown(void)
{
	int64_t error;

	do {
		error = opal_cec_power_down(0);
		if (error == OPAL_BUSY_EVENT)
			opal_poll_events(NULL);
	} while (error == OPAL_BUSY || error == OPAL_BUSY_EVENT);

	if (error != OPAL_SUCCESS)
		return;

	/* Wait for the actual powerdown to happen. */
	for (;;)
		opal_poll_events(NULL);
}

int	waittime = -1;

__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;
		vfs_shutdown(curproc);

		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}
	if_downall();

	uvm_shutdown();
	splhigh();
	cold = 1;

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

	if ((howto & RB_HALT) != 0) {
		if ((howto & RB_POWERDOWN) != 0)
			opal_powerdown();

		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cngetc();
	}

doreset:
	printf("rebooting...\n");
	opal_cec_reboot();

	for (;;)
		continue;
	/* NOTREACHED */
}
