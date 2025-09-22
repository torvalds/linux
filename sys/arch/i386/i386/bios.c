/*	$OpenBSD: bios.c,v 1.130 2024/08/18 15:50:49 deraadt Exp $	*/

/*
 * Copyright (c) 1997-2001 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #define BIOS_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/reboot.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>
#include <sys/sysctl.h>

#include <dev/cons.h>
#include <stand/boot/bootarg.h>

#include <machine/conf.h>
#include <machine/gdt.h>
#include <machine/biosvar.h>
#include <machine/mpbiosvar.h>
#include <machine/smbiosvar.h>

#include <dev/isa/isareg.h>
#include <i386/isa/isa_machdep.h>

#include <dev/pci/pcivar.h>

#include "apm.h"
#include "acpi.h"
#include "mpbios.h"
#include "pcibios.h"
#include "pci.h"

#include "com.h"
#if NCOM > 0
#include <sys/termios.h>
#include <dev/ic/comvar.h>
#endif

#include "softraid.h"
#if NSOFTRAID > 0
#include <dev/softraidvar.h>
#endif

struct bios_softc {
	struct	device sc_dev;
	vaddr_t bios32_service_va;
};

int biosprobe(struct device *, void *, void *);
void biosattach(struct device *, struct device *, void *);
int bios_print(void *, const char *);
char *fixstring(char *);

const struct cfattach bios_ca = {
	sizeof(struct bios_softc), biosprobe, biosattach
};

struct cfdriver bios_cd = {
	NULL, "bios", DV_DULL
};

extern dev_t bootdev;

#if NAPM > 0 || defined(DEBUG)
bios_apminfo_t *apm;
#endif
#if NPCI > 0
bios_pciinfo_t *bios_pciinfo;
#endif
bios_diskinfo_t *bios_diskinfo;
bios_memmap_t  *bios_memmap;
struct bios_softc *bios_softc;
uint32_t	bios_cksumlen;
struct bios32_entry bios32_entry;
struct smbios_entry smbios_entry;
#ifdef MULTIPROCESSOR
void		*bios_smpinfo;
#endif
bios_bootmac_t	*bios_bootmac;
#ifdef DDB
extern int	db_console;
#endif
bios_ucode_t	*bios_ucode;

void		smbios_info(char*);

bios_diskinfo_t *bios_getdiskinfo(dev_t);

const char *smbios_uninfo[] = {
	"System",
	"Not ",
	"To be",
	"SYS-"
};


char smbios_bios_date[64];
char smbios_bios_version[64];
char smbios_board_vendor[64];
char smbios_board_prod[64];
char smbios_board_serial[64];

int
biosprobe(struct device *parent, void *match, void *aux)
{
	struct bios_attach_args *bia = aux;

#ifdef BIOS_DEBUG
	printf("%s%d: boot API ver %x, %x; args %p[%d]\n",
	    bia->ba_name, bios_cd.cd_ndevs,
	    bootapiver, BOOTARG_APIVER, bootargp, bootargc);
#endif
	/* only one */
	if (bios_cd.cd_ndevs || strcmp(bia->ba_name, bios_cd.cd_name))
		return 0;

	if (!(bootapiver & BAPIV_VECTOR) || bootargp == NULL)
		return 0;

	return 1;
}

void
biosattach(struct device *parent, struct device *self, void *aux)
{
	struct bios_softc *sc = (struct bios_softc *)self;
#if (NPCI > 0 && NPCIBIOS > 0) || NAPM > 0
	struct bios_attach_args *bia = aux;
#endif
	struct smbios_struct_bios *sb;
	struct smbtable bios;
	volatile uint8_t *va;
	char scratch[64], *str;
	int flags, smbiosrev = 0, ncpu = 0, isa_hole_exec = 0;
#if NACPI > 0 || NAPM > 0
	int usingacpi = 0;
#endif

	bios_softc = sc;
	/* remember flags */
	flags = sc->sc_dev.dv_cfdata->cf_flags;

	va = ISA_HOLE_VADDR(0xffff0);
	printf(": date %c%c/%c%c/%c%c",
	    va[5], va[6], va[8], va[9], va[11], va[12]);

	/*
	 * Determining whether BIOS32 extensions are available is
	 * done by searching for the BIOS32 service directory.
	 * This 16-byte structure can be found somewhere in the
	 * range 0E0000h - 0FFFFFh and must be 16-byte aligned.
	 *
	 *  _______________________________________________________
	 * | Offset | Bytes | Description                          |
	 * |-------------------------------------------------------|
	 * |    0   |   4   | ASCII signature string of "_32_".    |
	 * |    4   |   4   | 32-bit entry point.                  |
	 * |    8   |   1   | Revision Level. Typically 00h.       |
	 * |    9   |   1   | Header length in 16-byte units. So   |
	 * |        |       | would have the value of 01h.         |
	 * |    A   |   1   | Checksum. The sum of all bytes in    |
	 * |        |       | this header must be zero.            |
	 * |    B   |   5   | Reserved. Set to zero.               |
	 *  -------------------------------------------------------
	 *
	 * To find the service directory, we first search for the
	 * signature. If we find a match, we must also verify the
	 * checksum. This service directory may then be used to
	 * determine whether a PCI BIOS is present.
	 *
	 * For more information see the PCI BIOS Specification,
	 * Revision 2.1 (August 26, 1994).
	 */

	if (!(flags & BIOSF_BIOS32)) {
		for (va = ISA_HOLE_VADDR(BIOS32_START);
		    va < (uint8_t *)ISA_HOLE_VADDR(BIOS32_END); va += 16) {
			bios32_header_t h = (bios32_header_t)va;
			uint8_t cksum;
			int i;

			if (h->signature != BIOS32_SIGNATURE)
				continue;

			/* verify checksum */
			for (cksum = 0, i = h->length * 16; i--; cksum += va[i])
				;
			if (cksum != 0)
				continue;

			if (h->entry <= BIOS32_START || h->entry >= BIOS32_END)
				continue;

			bios32_entry.segment = GSEL(GCODE_SEL, SEL_KPL);
			bios32_entry.offset = (uint32_t)ISA_HOLE_VADDR(h->entry);
			printf(", BIOS32 rev. %d @ 0x%x", h->rev, h->entry);
			break;
		}
	}

	/* see if we have SMBIOS extensions */
	if (!(flags & BIOSF_SMBIOS)) {
		for (va = ISA_HOLE_VADDR(SMBIOS_START);
		    va < (uint8_t *)ISA_HOLE_VADDR(SMBIOS_END); va+= 16) {
			struct smbhdr *sh = (struct smbhdr *)va;
			char *sminfop;
			uint8_t chksum;
			vaddr_t eva;
			paddr_t pa, end;
			int i;

			if (sh->sig != SMBIOS_SIGNATURE)
				continue;
			i = sh->len;
			for (chksum = 0; i--; chksum += va[i])
				;
			if (chksum != 0)
				continue;
			va += 0x10;
			if (!(va[0] == '_' && va[1] == 'D' && va[2] == 'M' &&
			    va[3] == 'I' && va[4] == '_'))
				continue;
			for (chksum = 0, i = 0xf; i--; chksum += va[i])
				;
			if (chksum != 0)
				continue;

			pa = trunc_page(sh->addr);
			end = round_page(sh->addr + sh->size);
			eva = (vaddr_t)km_alloc(end - pa, &kv_any,
			    &kp_none, &kd_nowait);
			if (eva == 0)
				break;

			smbios_entry.addr = (uint8_t *)(eva +
			    (sh->addr & PGOFSET));
			smbios_entry.len = sh->size;
			smbios_entry.mjr = sh->majrev;
			smbios_entry.min = sh->minrev;
			smbios_entry.count = sh->count;

			for (; pa < end; pa+= NBPG, eva+= NBPG)
				pmap_kenter_pa(eva, pa, PROT_READ);

			printf(", SMBIOS rev. %d.%d @ 0x%x (%hd entries)",
			    sh->majrev, sh->minrev, sh->addr, sh->count);
			/*
			 * Unbelievably the SMBIOS version number
			 * sequence is like 2.3 ... 2.33 ... 2.4 ... 2.5
			 */
			smbiosrev = sh->majrev * 100 + sh->minrev;
			if (sh->minrev < 10)
				smbiosrev = sh->majrev * 100 + sh->minrev * 10;

			bios.cookie = 0;
			if (smbios_find_table(SMBIOS_TYPE_BIOS, &bios)) {
				sb = bios.tblhdr;
				printf("\n%s:", sc->sc_dev.dv_xname);

				if ((smbios_get_string(&bios, sb->vendor,
				    scratch, sizeof(scratch))) != NULL)
					printf(" vendor %s",
					    fixstring(scratch));
				if ((smbios_get_string(&bios, sb->version,
				    scratch, sizeof(scratch))) != NULL) {
					sminfop = fixstring(scratch);
					if (sminfop != NULL) {
						strlcpy(smbios_bios_version,
						    sminfop,
						    sizeof(smbios_bios_version));
						printf(" version \"%s\"",
						    sminfop);
					}
				}
				if ((smbios_get_string(&bios, sb->release,
				    scratch, sizeof(scratch))) != NULL) {
					sminfop = fixstring(scratch);
					if (sminfop != NULL) {
						strlcpy(smbios_bios_date,
						    sminfop,
						    sizeof(smbios_bios_date));
						printf(" date %s", sminfop);
					}
				}
			}
			smbios_info(sc->sc_dev.dv_xname);

			/* count cpus so that we can disable apm when cpu > 1 */
			bzero(&bios, sizeof(bios));
			while (smbios_find_table(SMBIOS_TYPE_PROCESSOR,&bios)) {
				struct smbios_cpu *cpu = bios.tblhdr;

				if (cpu->cpu_status & SMBIOS_CPUST_POPULATED) {
					/* SMBIOS 2.5 added multicore support */
					if (smbiosrev >= 250 &&
					    cpu->cpu_core_enabled)
						ncpu += cpu->cpu_core_enabled;
					else {
						ncpu++;
						if (cpu->cpu_id_edx & CPUID_HTT)
							ncpu++;
					}
				}
			}
			break;
		}
	}

	printf("\n");

	/* No SMBIOS extensions, go looking for Soekris comBIOS */
	if (!(flags & BIOSF_SMBIOS) && smbiosrev == 0) {
		const char *signature = "Soekris Engineering";

		for (va = ISA_HOLE_VADDR(SMBIOS_START);
		    va <= (uint8_t *)ISA_HOLE_VADDR(SMBIOS_END -
		    (strlen(signature) - 1)); va++)
			if (!memcmp((uint8_t *)va, signature,
			    strlen(signature))) {
				hw_vendor = malloc(strlen(signature) + 1,
				    M_DEVBUF, M_NOWAIT);
				if (hw_vendor)
					strlcpy(hw_vendor, signature,
					    strlen(signature) + 1);
				va += strlen(signature);
				break;
			}

		for (; hw_vendor &&
		    va <= (uint8_t *)ISA_HOLE_VADDR(SMBIOS_END - 6); va++)
			/*
			 * Search for "net(4(5xx|801)|[56]501)" which matches
			 * the strings found in the comBIOS images
			 */
			if (!memcmp((uint8_t *)va, "net45xx", 7) ||
			    !memcmp((uint8_t *)va, "net4801", 7) ||
			    !memcmp((uint8_t *)va, "net5501", 7) ||
			    !memcmp((uint8_t *)va, "net6501", 7)) {
				hw_prod = malloc(8, M_DEVBUF, M_NOWAIT);
				if (hw_prod) {
					memcpy(hw_prod, (uint8_t *)va, 7);
					hw_prod[7] = '\0';
				}
				break;
			}
	}

#if NACPI > 0
#if NPCI > 0
	if (smbiosrev >= 210 && pci_mode_detect() != 0)
#endif
	{
		struct bios_attach_args ba;

		memset(&ba, 0, sizeof(ba));
		ba.ba_name = "acpi";
		ba.ba_func = 0x00;		/* XXX ? */
		ba.ba_iot = I386_BUS_SPACE_IO;
		ba.ba_memt = I386_BUS_SPACE_MEM;
		if (config_found(self, &ba, bios_print)) {
			flags |= BIOSF_PCIBIOS;
			usingacpi = 1;
		}
	}
#endif

#if NAPM > 0
	if (usingacpi == 0 && apm && ncpu < 2 && smbiosrev < 240) {
		struct bios_attach_args ba;

#if defined(DEBUG) || defined(APMDEBUG)
		printf("apminfo: %x, code %x[%x]/%x[%x], data %x[%x], ept %x\n",
		    apm->apm_detail,
		    apm->apm_code32_base, apm->apm_code_len,
		    apm->apm_code16_base, apm->apm_code16_len,
		    apm->apm_data_base, apm->apm_data_len, apm->apm_entry);
#endif
		ba.ba_name = "apm";
		ba.ba_func = 0x15;
		ba.ba_memt = bia->ba_memt;
		ba.ba_iot = bia->ba_iot;
		ba.ba_apmp = apm;
		config_found(self, &ba, bios_print);
		isa_hole_exec = 1;
	}
#endif


#if NMPBIOS > 0
	if (mpbios_probe(self)) {
		struct bios_attach_args ba;

		memset(&ba, 0, sizeof(ba));
		ba.ba_name = "mpbios";
		ba.ba_iot = I386_BUS_SPACE_IO;
		ba.ba_memt = I386_BUS_SPACE_MEM;

		config_found(self, &ba, bios_print);
	}
#endif

#if NPCI > 0 && NPCIBIOS > 0
	if (!(flags & BIOSF_PCIBIOS)) {
		struct bios_attach_args ba;

		ba.ba_name = "pcibios";
		ba.ba_func = 0x1A;
		ba.ba_memt = bia->ba_memt;
		ba.ba_iot = bia->ba_iot;
		config_found(self, &ba, bios_print);
	}
#endif

	/*
	 * now that we gave 'em a chance to attach,
	 * scan and map all the proms we can find
	 */
	if (!(flags & BIOSF_PROMSCAN)) {
		volatile uint8_t *eva;

		for (str = NULL, va = ISA_HOLE_VADDR(0xc0000),
		    eva = ISA_HOLE_VADDR(0xf0000);
		    va < eva; va += 512) {
			extern struct extent *iomem_ex;
			bios_romheader_t romh = (bios_romheader_t)va;
			uint32_t off, len;
			uint8_t cksum;
			int i;

			if (romh->signature != 0xaa55)
				continue;

			/*
			 * for this and the next check we probably want
			 * to reserve the page in the extent anyway
			 */
			if (!romh->len || romh->len == 0xff)
				continue;

			len = romh->len * 512;
			if (va + len > eva)
				continue;

			for (cksum = 0, i = len; i--; cksum += va[i])
				;
			off = 0xc0000 + (va - (uint8_t *)
			    ISA_HOLE_VADDR(0xc0000));

			if (!str)
				printf("%s: ROM list:",
				    str = sc->sc_dev.dv_xname);
			printf(" 0x%05x/0x%x%s", off, len,
			    cksum? "!" : "");

			if ((i = extent_alloc_region(iomem_ex,
			    (paddr_t)off, len, EX_NOWAIT)))
				printf(":%d", i);

			isa_hole_exec = 1;
			va += len - 512;
		}
		if (str)
			printf("\n");
	}

	/*
	 * If we had no BIOS / proms that need exec permission in the ISA
	 * hole, remove X permissions.
	 */
	if (!isa_hole_exec)
		pmap_write_protect(pmap_kernel(), (vaddr_t)atdevbase,
		    (vaddr_t)atdevbase + IOM_SIZE, PROT_READ | PROT_WRITE);
}

void
bios_getopt(void)
{
	bootarg_t *q;
	bios_ddb_t *bios_ddb;
	bios_bootduid_t *bios_bootduid;
	bios_bootsr_t *bios_bootsr;

#ifdef BIOS_DEBUG
	printf("bootargv:");
#endif

	for(q = bootargp; q->ba_type != BOOTARG_END; q = q->ba_next) {
		q->ba_next = (bootarg_t *)((caddr_t)q + q->ba_size);
		switch (q->ba_type) {
		case BOOTARG_MEMMAP:
			bios_memmap = (bios_memmap_t *)q->ba_arg;
#ifdef BIOS_DEBUG
			printf(" memmap %p", bios_memmap);
#endif
			break;
		case BOOTARG_DISKINFO:
			bios_diskinfo = (bios_diskinfo_t *)q->ba_arg;
#ifdef BIOS_DEBUG
			printf(" diskinfo %p", bios_diskinfo);
#endif
			break;
#if NAPM > 0 || defined(DEBUG)
		case BOOTARG_APMINFO:
#ifdef BIOS_DEBUG
			printf(" apminfo %p", q->ba_arg);
#endif
			apm = (bios_apminfo_t *)q->ba_arg;
			break;
#endif
		case BOOTARG_CKSUMLEN:
			bios_cksumlen = *(uint32_t *)q->ba_arg;
#ifdef BIOS_DEBUG
			printf(" cksumlen %d", bios_cksumlen);
#endif
			break;
#if NPCI > 0
		case BOOTARG_PCIINFO:
			bios_pciinfo = (bios_pciinfo_t *)q->ba_arg;
#ifdef BIOS_DEBUG
			printf(" pciinfo %p", bios_pciinfo);
#endif
			break;
#endif
		case BOOTARG_CONSDEV:
			if (q->ba_size >= sizeof(bios_consdev_t) +
			    offsetof(bootarg_t, ba_arg)) {
				bios_consdev_t *cdp =
				    (bios_consdev_t*)q->ba_arg;
#if NCOM > 0
				static const int ports[] =
				    { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
				int unit = minor(cdp->consdev);
				int consaddr = cdp->consaddr;
				if (consaddr == -1 && unit >=0 &&
				    unit < (sizeof(ports)/sizeof(ports[0])))
					consaddr = ports[unit];
				if (major(cdp->consdev) == 8 &&
				    consaddr != -1) {
					comconsunit = unit;
					comconsaddr = consaddr;
					comconsrate = cdp->conspeed;
					comconsiot = I386_BUS_SPACE_IO;

					/* Probe the serial port this time. */
					cninit();
				}
#endif
#ifdef BIOS_DEBUG
				printf(" console 0x%x:%d",
				    cdp->consdev, cdp->conspeed);
#endif
			}
			break;
#ifdef MULTIPROCESSOR
		case BOOTARG_SMPINFO:
			bios_smpinfo = q->ba_arg;
			printf(" smpinfo %p", bios_smpinfo);
			break;
#endif

		case BOOTARG_BOOTMAC:
			bios_bootmac = (bios_bootmac_t *)q->ba_arg;
			break;

		case BOOTARG_DDB:
			bios_ddb = (bios_ddb_t *)q->ba_arg;
#ifdef DDB
			db_console = bios_ddb->db_console;
#endif
			break;

		case BOOTARG_BOOTDUID:
			bios_bootduid = (bios_bootduid_t *)q->ba_arg;
			bcopy(bios_bootduid, bootduid, sizeof(bootduid));
			break;

		case BOOTARG_BOOTSR:
			bios_bootsr = (bios_bootsr_t *)q->ba_arg;
#if NSOFTRAID > 0
			bcopy(&bios_bootsr->uuid, &sr_bootuuid,
			    sizeof(sr_bootuuid));
			bcopy(&bios_bootsr->maskkey, &sr_bootkey,
			    sizeof(sr_bootkey));
#endif
			explicit_bzero(bios_bootsr, sizeof(bios_bootsr_t));
			break;

		case BOOTARG_UCODE:
			bios_ucode = (bios_ucode_t *)q->ba_arg;
			break;

		default:
#ifdef BIOS_DEBUG
			printf(" unsupported arg (%d) %p", q->ba_type,
			    q->ba_arg);
#endif
			break;
		}
	}
	printf("\n");

}

int
bios_print(void *aux, const char *pnp)
{
	struct bios_attach_args *ba = aux;

	if (pnp)
		printf("%s at %s function 0x%x",
		    ba->ba_name, pnp, ba->ba_func);
	return (UNCONF);
}

int
bios32_service(uint32_t service, bios32_entry_t e, bios32_entry_info_t ei)
{
	u_long pa, endpa;
	vaddr_t va, sva;
	uint32_t base, count, off, ent;

	if (bios32_entry.offset == 0)
		return 0;

	base = 0;
	__asm volatile("lcall *(%4)"
	    : "+a" (service), "+b" (base), "=c" (count), "=d" (off)
	    : "D" (&bios32_entry)
	    : "%esi", "cc", "memory");

	if (service & 0xff)
		return 0;	/* not found */

	ent = base + off;
	if (ent <= BIOS32_START || ent >= BIOS32_END)
		return 0;

	endpa = round_page(BIOS32_END);

	sva = va = (vaddr_t)km_alloc(endpa, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return (0);

	/* Store bios32 service kva for cleanup later */
	bios_softc->bios32_service_va = sva;

	setgdt(GBIOS32_SEL, (caddr_t)va, BIOS32_END, SDT_MEMERA, SEL_KPL, 1, 0);

	for (pa = trunc_page(BIOS32_START),
	    va += trunc_page(BIOS32_START);
	    pa < endpa; pa += NBPG, va += NBPG) {
		pmap_enter(pmap_kernel(), va, pa,
		    PROT_READ | PROT_WRITE | PROT_EXEC,
		    PROT_READ | PROT_WRITE | PROT_EXEC | PMAP_WIRED);

		if (pa >= trunc_page(base)) {
			pmap_enter(pmap_kernel(), sva, pa,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    PROT_READ | PROT_WRITE | PROT_EXEC | PMAP_WIRED);
			sva += NBPG;
		}
	}

	e->segment = GSEL(GBIOS32_SEL, SEL_KPL);
	e->offset = (vaddr_t)ent;

	ei->bei_base = base;
	ei->bei_size = count;
	ei->bei_entry = ent;

	return 1;
}

void
bios32_cleanup(void)
{
	u_long pa, size;
	vaddr_t va;

	size = round_page(BIOS32_END);

	for (va = bios_softc->bios32_service_va;
	    va < bios_softc->bios32_service_va + size;
	    va += NBPG) {
		if (pmap_extract(pmap_kernel(), va, &pa))
			pmap_remove(pmap_kernel(), va, va + PAGE_SIZE);
	}

	km_free((void *)bios_softc->bios32_service_va, size,
	    &kv_any, &kp_none);
}

int
biosopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct bios_softc *sc = bios_cd.cd_devs[0];

	if (minor(dev))
		return (ENXIO);

	(void)sc;

	return 0;
}

int
biosclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct bios_softc *sc = bios_cd.cd_devs[0];

	if (minor(dev))
		return (ENXIO);

	(void)sc;

	return 0;
}

int
biosioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct bios_softc *sc = bios_cd.cd_devs[0];

	if (minor(dev))
		return (ENXIO);

	switch (cmd) {
	default:
		return ENXIO;
	}

	(void)sc;

	return 0;
}

int
bios_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	bios_diskinfo_t *pdi;
	int biosdev;

	/* all sysctl names at this level except diskinfo are terminal */
	if (namelen != 1 && name[0] != BIOS_DISKINFO)
		return (ENOTDIR);		/* overloaded */

	if (!(bootapiver & BAPIV_VECTOR))
		return EOPNOTSUPP;

	switch (name[0]) {
	case BIOS_DEV:
		if ((pdi = bios_getdiskinfo(bootdev)) == NULL)
			return ENXIO;
		biosdev = pdi->bios_number;
		return sysctl_rdint(oldp, oldlenp, newp, biosdev);
	case BIOS_DISKINFO:
		if (namelen != 2)
			return ENOTDIR;
		if ((pdi = bios_getdiskinfo(name[1])) == NULL)
			return ENXIO;
		return sysctl_rdstruct(oldp, oldlenp, newp, pdi, sizeof(*pdi));
	case BIOS_CKSUMLEN:
		return sysctl_rdint(oldp, oldlenp, newp, bios_cksumlen);
	default:
		return EOPNOTSUPP;
	}
	/* NOTREACHED */
}

bios_diskinfo_t *
bios_getdiskinfo(dev_t dev)
{
	bios_diskinfo_t *pdi;

	if (bios_diskinfo == NULL)
		return NULL;

	for (pdi = bios_diskinfo; pdi->bios_number != -1; pdi++) {
		if ((dev & B_MAGICMASK) == B_DEVMAGIC) { /* search by bootdev */
			if (pdi->bsd_dev == dev)
				break;
		} else {
			if (pdi->bios_number == dev)
				break;
		}
	}

	if (pdi->bios_number == -1)
		return NULL;
	else
		return pdi;
}

/*
 * smbios_find_table() takes a caller supplied smbios struct type and
 * a pointer to a handle (struct smbtable) returning one if the structure
 * is successfully located and zero otherwise. Callers should take care
 * to initialize the cookie field of the smbtable structure to zero before
 * the first invocation of this function.
 * Multiple tables of the same type can be located by repeatedly calling
 * smbios_find_table with the same arguments.
 */
int
smbios_find_table(uint8_t type, struct smbtable *st)
{
	uint8_t *va, *end;
	struct smbtblhdr *hdr;
	int ret = 0, tcount = 1;

	va = smbios_entry.addr;
	end = va + smbios_entry.len;

	/*
	 * The cookie field of the smtable structure is used to locate
	 * multiple instances of a table of an arbitrary type. Following the
	 * successful location of a table, the type is encoded as bits 0:7 of
	 * the cookie value, the offset in terms of the number of structures
	 * preceding that referenced by the handle is encoded in bits 15:31.
	 */
	if ((st->cookie & 0xfff) == type && st->cookie >> 16) {
		if ((uint8_t *)st->hdr >= va && (uint8_t *)st->hdr < end) {
			hdr = st->hdr;
			if (hdr->type == type) {
				va = (uint8_t *)hdr + hdr->size;
				for (; va + 1 < end; va++)
					if (*va == 0 && *(va + 1) == 0)
						break;
				va += 2;
				tcount = st->cookie >> 16;
			}
		}
	}
	for (; va + sizeof(struct smbtblhdr) < end &&
	    tcount <= smbios_entry.count; tcount++) {
		hdr = (struct smbtblhdr *)va;
		if (hdr->type == type) {
			ret = 1;
			st->hdr = hdr;
			st->tblhdr = va + sizeof(struct smbtblhdr);
			st->cookie = (tcount + 1) << 16 | type;
			break;
		}
		if (hdr->type == SMBIOS_TYPE_EOT)
			break;
		va += hdr->size;
		for (; va + 1 < end; va++)
			if (*va == 0 && *(va + 1) == 0)
				break;
		va += 2;
	}
	return ret;
}

char *
smbios_get_string(struct smbtable *st, uint8_t indx, char *dest, size_t len)
{
	uint8_t *va, *end;
	char *ret = NULL;
	int i;

	va = (uint8_t *)st->hdr + st->hdr->size;
	end = smbios_entry.addr + smbios_entry.len;
	for (i = 1; va < end && i < indx && *va; i++)
		while (*va++)
			;
	if (i == indx) {
		if (va + len < end) {
			ret = dest;
			memcpy(ret, va, len);
			ret[len - 1] = '\0';
		}
	}

	return ret;
}

char *
fixstring(char *s)
{
	char *p, *e;
	int i;

	for (i = 0; i < nitems(smbios_uninfo); i++)
		if ((strncasecmp(s, smbios_uninfo[i],
		    strlen(smbios_uninfo[i]))) == 0)
			return NULL;
	/*
	 * Remove leading and trailing whitespace
	 */
	for (p = s; *p == ' '; p++)
		;
	/*
	 * Special case entire string is whitespace
	 */
	if (p == s + strlen(s))
		return NULL;
	for (e = s + strlen(s) - 1; e > s && *e == ' '; e--)
		;
	if (p > s || e < s + strlen(s) - 1) {
		memmove(s, p, e - p + 1);
		s[e - p + 1] = '\0';
	}

	return s;
}

void
smbios_info(char *str)
{
	char *sminfop, sminfo[64];
	struct smbtable stbl, btbl;
	struct smbios_sys *sys;
	struct smbios_board *board;
	int i, infolen, uuidf, havebb;
	char *p;

	if (smbios_entry.mjr < 2)
		return;
	/*
	 * According to the spec the system table among others is required,
	 * if it is not we do not bother with this smbios implementation.
	 */
	stbl.cookie = btbl.cookie = 0;
	if (!smbios_find_table(SMBIOS_TYPE_SYSTEM, &stbl))
		return;
	havebb = smbios_find_table(SMBIOS_TYPE_BASEBOARD, &btbl);

	sys = (struct smbios_sys *)stbl.tblhdr;
	if (havebb) {
		board = (struct smbios_board *)btbl.tblhdr;

		sminfop = NULL;
		if ((p = smbios_get_string(&btbl, board->vendor,
		    sminfo, sizeof(sminfo))) != NULL)
			sminfop = fixstring(p);
		if (sminfop)
			strlcpy(smbios_board_vendor, sminfop,
			    sizeof(smbios_board_vendor));

		sminfop = NULL;
		if ((p = smbios_get_string(&btbl, board->product,
		    sminfo, sizeof(sminfo))) != NULL)
			sminfop = fixstring(p);
		if (sminfop)
			strlcpy(smbios_board_prod, sminfop,
			    sizeof(smbios_board_prod));

		sminfop = NULL;
		if ((p = smbios_get_string(&btbl, board->serial,
		    sminfo, sizeof(sminfo))) != NULL)
			sminfop = fixstring(p);
		if (sminfop)
			strlcpy(smbios_board_serial, sminfop,
			    sizeof(smbios_board_serial));
	}

	/*
	 * Some smbios implementations have no system vendor or
	 * product strings, some have very uninformative data which is
	 * harder to work around and we must rely upon various
	 * heuristics to detect this. In both cases we attempt to fall
	 * back on the base board information in the perhaps naive
	 * belief that motherboard vendors will supply this
	 * information.
	 */
	sminfop = NULL;
	if ((p = smbios_get_string(&stbl, sys->vendor, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop == NULL) {
		if (havebb) {
			if ((p = smbios_get_string(&btbl, board->vendor,
			    sminfo, sizeof(sminfo))) != NULL)
				sminfop = fixstring(p);
		}
	}
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_vendor = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_vendor)
			strlcpy(hw_vendor, sminfop, infolen);
		sminfop = NULL;
	}
	if ((p = smbios_get_string(&stbl, sys->product, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop == NULL) {
		if (havebb) {
			if ((p = smbios_get_string(&btbl, board->product,
			    sminfo, sizeof(sminfo))) != NULL)
				sminfop = fixstring(p);
		}
	}
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_prod = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_prod)
			strlcpy(hw_prod, sminfop, infolen);
		sminfop = NULL;
	}
	if (hw_vendor != NULL && hw_prod != NULL)
		printf("\n%s: %s %s", str, hw_vendor, hw_prod);
	if ((p = smbios_get_string(&stbl, sys->version, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_ver = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_ver)
			strlcpy(hw_ver, sminfop, infolen);
		sminfop = NULL;
	}
	if ((p = smbios_get_string(&stbl, sys->serial, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		for (i = 0; i < infolen - 1; i++)
			enqueue_randomness(sminfop[i]);
		hw_serial = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_serial)
			strlcpy(hw_serial, sminfop, infolen);
	}
	if (smbios_entry.mjr > 2 || (smbios_entry.mjr == 2 &&
	    smbios_entry.min >= 1)) {
		/*
		 * If the uuid value is all 0xff the uuid is present but not
		 * set, if its all 0 then the uuid isn't present at all.
		 */
		uuidf = SMBIOS_UUID_NPRESENT|SMBIOS_UUID_NSET;
		for (i = 0; i < sizeof(sys->uuid); i++) {
			if (sys->uuid[i] != 0xff)
				uuidf &= ~SMBIOS_UUID_NSET;
			if (sys->uuid[i] != 0)
				uuidf &= ~SMBIOS_UUID_NPRESENT;
		}

		if (uuidf & SMBIOS_UUID_NPRESENT)
			hw_uuid = NULL;
		else if (uuidf & SMBIOS_UUID_NSET)
			hw_uuid = "Not Set";
		else {
			for (i = 0; i < sizeof(sys->uuid); i++)
				enqueue_randomness(sys->uuid[i]);
			hw_uuid = malloc(SMBIOS_UUID_REPLEN, M_DEVBUF,
			    M_NOWAIT);
			if (hw_uuid) {
				snprintf(hw_uuid, SMBIOS_UUID_REPLEN,
				    SMBIOS_UUID_REP,
				    sys->uuid[0], sys->uuid[1], sys->uuid[2],
				    sys->uuid[3], sys->uuid[4], sys->uuid[5],
				    sys->uuid[6], sys->uuid[7], sys->uuid[8],
				    sys->uuid[9], sys->uuid[10], sys->uuid[11],
				    sys->uuid[12], sys->uuid[13], sys->uuid[14],
				    sys->uuid[15]);
			}
		}
	}
}
