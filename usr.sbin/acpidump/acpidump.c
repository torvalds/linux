/*	$OpenBSD: acpidump.c,v 1.25 2022/09/11 10:40:35 kettenis Exp $	*/
/*
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <paths.h>


#define vm_page_size sysconf(_SC_PAGESIZE)
#define PRINTFLAG(xx)							\
	do {								\
		if (facp->flags & ACPI_FACP_FLAG_## xx) {		\
			fprintf(fhdr, "%c%s", sep, #xx); sep = ',';	\
		}							\
	} while (0)


typedef unsigned long	vm_offset_t;

struct ACPIrsdp {
	u_char		signature[8];
	u_char		sum;
	u_char		oem[6];
	u_char		rev;
	u_int32_t	addr;
#define SIZEOF_RSDP_REV_0	20
	u_int32_t	len;
	u_int64_t	xaddr;
	u_char		xsum;
	u_char		xres[3];
} __packed;

struct ACPIsdt {
	u_char		signature[4];
	u_int32_t	len;
	u_char		rev;
	u_char		check;
	u_char		oemid[6];
	u_char		oemtblid[8];
	u_int32_t	oemrev;
	u_char		creator[4];
	u_int32_t	crerev;
#define SIZEOF_SDT_HDR	36	/* struct size except body */
	u_int32_t	body[1];/* This member should be casted */
} __packed;

struct ACPIgas {
	u_int8_t	address_space_id;
#define ACPI_GAS_MEMORY		0
#define ACPI_GAS_IO		1
#define ACPI_GAS_PCI		2
#define ACPI_GAS_EMBEDDED	3
#define ACPI_GAS_SMBUS		4
#define ACPI_GAS_FIXED		0x7f
	u_int8_t	register_bit_width;
	u_int8_t	register_bit_offset;
	u_int8_t	res;
	u_int64_t	address;
} __packed;

struct FACPbody {
	u_int32_t	facs_ptr;
	u_int32_t	dsdt_ptr;
	u_int8_t	int_model;
#define ACPI_FACP_INTMODEL_PIC	0	/* Standard PC-AT PIC */
#define ACPI_FACP_INTMODEL_APIC	1	/* Multiple APIC */
	u_char		reserved1;
	u_int16_t	sci_int;
	u_int32_t	smi_cmd;
	u_int8_t	acpi_enable;
	u_int8_t	acpi_disable;
	u_int8_t	s4biosreq;
	u_int8_t	reserved2;
	u_int32_t	pm1a_evt_blk;
	u_int32_t	pm1b_evt_blk;
	u_int32_t	pm1a_cnt_blk;
	u_int32_t	pm1b_cnt_blk;
	u_int32_t	pm2_cnt_blk;
	u_int32_t	pm_tmr_blk;
	u_int32_t	gpe0_blk;
	u_int32_t	gpe1_blk;
	u_int8_t	pm1_evt_len;
	u_int8_t	pm1_cnt_len;
	u_int8_t	pm2_cnt_len;
	u_int8_t	pm_tmr_len;
	u_int8_t	gpe0_len;
	u_int8_t	gpe1_len;
	u_int8_t	gpe1_base;
	u_int8_t	reserved3;
	u_int16_t	p_lvl2_lat;
	u_int16_t	p_lvl3_lat;
	u_int16_t	flush_size;
	u_int16_t	flush_stride;
	u_int8_t	duty_off;
	u_int8_t	duty_width;
	u_int8_t	day_alrm;
	u_int8_t	mon_alrm;
	u_int8_t	century;
	u_int16_t	iapc_boot_arch;
	u_char		reserved4[1];
	u_int32_t	flags;
#define ACPI_FACP_FLAG_WBINVD	1	/* WBINVD is correctly supported */
#define ACPI_FACP_FLAG_WBINVD_FLUSH 2	/* WBINVD flushes caches */
#define ACPI_FACP_FLAG_PROC_C1	4	/* C1 power state supported */
#define ACPI_FACP_FLAG_P_LVL2_UP 8	/* C2 power state works on SMP */
#define ACPI_FACP_FLAG_PWR_BUTTON 16	/* Power button uses control method */
#define ACPI_FACP_FLAG_SLP_BUTTON 32	/* Sleep button uses control method */
#define ACPI_FACP_FLAG_FIX_RTC	64	/* RTC wakeup not supported */
#define ACPI_FACP_FLAG_RTC_S4	128	/* RTC can wakeup from S4 state */
#define ACPI_FACP_FLAG_TMR_VAL_EXT 256	/* TMR_VAL is 32bit */
#define ACPI_FACP_FLAG_DCK_CAP	512	/* Can support docking */
	struct ACPIgas	reset_reg;
	u_int8_t	reset_value;
	u_int8_t	reserved5[3];
	u_int64_t	x_firmware_ctrl;
	u_int64_t	x_dsdt;
	struct ACPIgas	x_pm1a_evt_blk;
	struct ACPIgas	x_pm1b_evt_blk;
	struct ACPIgas	x_pm1a_cnt_blk;
	struct ACPIgas	x_pm1b_cnt_blk;
	struct ACPIgas	x_pm2_cnt_blk;
	struct ACPIgas	x_pm_tmr_blk;
	struct ACPIgas	x_gpe0_blk;
	struct ACPIgas	x_gpe1_blk;
} __packed;

struct acpi_user_mapping {
	LIST_ENTRY(acpi_user_mapping)	link;
	vm_offset_t			pa;
	caddr_t				va;
	size_t				size;
};

LIST_HEAD(acpi_user_mapping_list, acpi_user_mapping) maplist;

int		acpi_mem_fd = -1;
char		*aml_dumpfile;
int		aml_dumpdir;
FILE		*fhdr;
int		quiet;

int	acpi_checksum(void *_p, size_t _length);
struct acpi_user_mapping *acpi_user_find_mapping(vm_offset_t _pa, size_t _size);
void	*acpi_map_physical(vm_offset_t _pa, size_t _size);
void	acpi_user_init(void);
struct ACPIrsdp *acpi_check_rsd_ptr(vm_offset_t _pa);
struct ACPIrsdp *acpi_find_rsd_ptr(void);
void	acpi_print_string(char *_s, size_t _length);
void	acpi_print_rsd_ptr(struct ACPIrsdp *_rp);
struct ACPIsdt *acpi_map_sdt(vm_offset_t _pa);
void	aml_dump(struct ACPIsdt *_hdr);
void	acpi_print_sdt(struct ACPIsdt *_sdp);
void	acpi_print_rsdt(struct ACPIsdt *_rsdp);
void	acpi_print_xsdt(struct ACPIsdt *_rsdp);
void	acpi_print_facp(struct FACPbody *_facp);
void	acpi_print_dsdt(struct ACPIsdt *_dsdp);
void	acpi_handle_dsdt(struct ACPIsdt *_dsdp);
void	acpi_handle_facp(struct FACPbody *_facp);
void	acpi_handle_rsdt(struct ACPIsdt *_rsdp);
void	acpi_handle_xsdt(struct ACPIsdt *_rsdp);
void	asl_dump_from_devmem(void);
void	usage(void);
u_long	efi_acpi_addr(void);


struct ACPIsdt	dsdt_header = {
	"DSDT", 0, 1, 0, "OEMID", "OEMTBLID", 0x12345678, "CRTR", 0x12345678
};

int
acpi_checksum(void *p, size_t length)
{
	u_int8_t	*bp;
	u_int8_t	sum;

	bp = p;
	sum = 0;
	while (length--)
		sum += *bp++;

	return (sum);
}

struct acpi_user_mapping *
acpi_user_find_mapping(vm_offset_t pa, size_t size)
{
	struct acpi_user_mapping	*map;
	int	page_mask = getpagesize() - 1;

	/* First search for an existing mapping */
	for (map = LIST_FIRST(&maplist); map; map = LIST_NEXT(map, link)) {
		if (map->pa <= pa && map->size >= pa + size - map->pa)
			return (map);
	}

	/* Then create a new one */
#undef round_page
#undef trunc_page
#define	round_page(x)	(((x) + page_mask) & ~page_mask)
#define	trunc_page(x)	((x) & ~page_mask)
	size = round_page(pa + size) - trunc_page(pa);
	pa = trunc_page(pa);
#undef round_page
#undef trunc_page
	map = malloc(sizeof(struct acpi_user_mapping));
	if (!map)
		errx(1, "out of memory");
	map->pa = pa;
	map->va = mmap(0, size, PROT_READ, MAP_SHARED, acpi_mem_fd, pa);
	map->size = size;
	if (map->va == MAP_FAILED)
		err(1, "can't map address");
	LIST_INSERT_HEAD(&maplist, map, link);

	return (map);
}

void *
acpi_map_physical(vm_offset_t pa, size_t size)
{
	struct acpi_user_mapping	*map;

	map = acpi_user_find_mapping(pa, size);
	return (map->va + (pa - map->pa));
}

void
acpi_user_init(void)
{
	if (acpi_mem_fd == -1) {
		acpi_mem_fd = open("/dev/mem", O_RDONLY);
		if (acpi_mem_fd == -1)
			err(1, "opening /dev/mem");
		LIST_INIT(&maplist);
	}
}

struct ACPIrsdp *
acpi_check_rsd_ptr(vm_offset_t pa)
{
	struct ACPIrsdp rp;
		
	lseek(acpi_mem_fd, pa, SEEK_SET);
	read(acpi_mem_fd, &rp, SIZEOF_RSDP_REV_0);
	if (memcmp(rp.signature, "RSD PTR ", 8) != 0)
		return NULL;

	if (rp.rev >= 2) {
		read(acpi_mem_fd, &(rp.len),
		    sizeof(struct ACPIrsdp) - SIZEOF_RSDP_REV_0);
		if (acpi_checksum(&rp, sizeof(struct ACPIrsdp)) == 0)
			return acpi_map_physical(pa, sizeof(struct ACPIrsdp));
	}

	if (acpi_checksum(&rp, SIZEOF_RSDP_REV_0) == 0)
		return (acpi_map_physical(pa, SIZEOF_RSDP_REV_0));

	return NULL;
}

struct ACPIrsdp *
acpi_find_rsd_ptr(void)
{
	struct ACPIrsdp *rp;
	u_long		addr;

	if ((addr = efi_acpi_addr()) != 0) {
		if ((rp = acpi_check_rsd_ptr(addr)))
			return rp;
	}

#if defined(__amd64__) || defined (__i386__)
	for (addr = 0; addr < 1024 * 1024; addr += 16) {
		if ((rp = acpi_check_rsd_ptr(addr)))
			return rp;
	}
#endif

	return NULL;
}

void
acpi_print_string(char *s, size_t length)
{
	int		c;

	/* Trim trailing spaces and NULLs */
	while (length > 0 && (s[length - 1] == ' ' || s[length - 1] == '\0'))
		length--;

	while (length--) {
		c = *s++;
		fputc(c, fhdr);
	}
}

void
acpi_print_rsd_ptr(struct ACPIrsdp *rp)
{
	fprintf(fhdr, "\n");
	fprintf(fhdr, "RSD PTR: Checksum=%d, OEMID=", rp->sum);
	acpi_print_string(rp->oem, 6);
	fprintf(fhdr, ", Revision=%d", rp->rev);
	fprintf(fhdr, ", RsdtAddress=0x%08x\n", rp->addr);
	if (rp->rev >= 2) {
		fprintf(fhdr, "\tLength=%d", rp->len);
		fprintf(fhdr, ", XsdtAddress=0x%016llx", rp->xaddr);
		fprintf(fhdr, ", Extended Checksum=%d\n", rp->xsum);
	}
	fprintf(fhdr, "\n");
}

struct ACPIsdt *
acpi_map_sdt(vm_offset_t pa)
{
	struct ACPIsdt	*sp;

	sp = acpi_map_physical(pa, sizeof(struct ACPIsdt));
	sp = acpi_map_physical(pa, sp->len);
	return (sp);
}

void
aml_dump(struct ACPIsdt *hdr)
{
	static int	hdr_index;
	char		name[PATH_MAX];
	int		fd;
	mode_t		mode;

	snprintf(name, sizeof(name), "%s%c%c%c%c%c.%d",
	    aml_dumpfile, aml_dumpdir ? '/' : '.',
	    hdr->signature[0], hdr->signature[1],
	    hdr->signature[2], hdr->signature[3],
	    hdr_index++);

	mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd == -1)
		err(1, "aml_dump");

	write(fd, hdr, SIZEOF_SDT_HDR);
	write(fd, hdr->body, hdr->len - SIZEOF_SDT_HDR);
	close(fd);
}

void
acpi_print_sdt(struct ACPIsdt *sdp)
{
	fprintf(fhdr, "\n");
	acpi_print_string(sdp->signature, 4);
	fprintf(fhdr, ": Length=%d, Revision=%d, Checksum=%d,\n",
	       sdp->len, sdp->rev, sdp->check);
	fprintf(fhdr, "\tOEMID=");
	acpi_print_string(sdp->oemid, 6);
	fprintf(fhdr, ", OEM Table ID=");
	acpi_print_string(sdp->oemtblid, 8);
	fprintf(fhdr, ", OEM Revision=0x%x,\n", sdp->oemrev);
	fprintf(fhdr, "\tCreator ID=");
	acpi_print_string(sdp->creator, 4);
	fprintf(fhdr, ", Creator Revision=0x%x\n", sdp->crerev);
	fprintf(fhdr, "\n");
	if (!memcmp(sdp->signature, "DSDT", 4))
		memcpy(&dsdt_header, sdp, sizeof(dsdt_header));
}

void
acpi_print_rsdt(struct ACPIsdt *rsdp)
{
	int		i, entries;

	acpi_print_sdt(rsdp);
	entries = (rsdp->len - SIZEOF_SDT_HDR) / sizeof(u_int32_t);
	fprintf(fhdr, "\n");
	fprintf(fhdr, "\tEntries={ ");
	for (i = 0; i < entries; i++) {
		if (i > 0)
			fprintf(fhdr, ", ");
		fprintf(fhdr, "0x%08x", rsdp->body[i]);
	}
	fprintf(fhdr, " }\n");
	fprintf(fhdr, "\n");
}

void
acpi_print_xsdt(struct ACPIsdt *rsdp)
{
	int		i, entries;
	u_int64_t	*body = (u_int64_t *) rsdp->body;

	acpi_print_sdt(rsdp);
	entries = (rsdp->len - SIZEOF_SDT_HDR) / sizeof(u_int64_t);
	fprintf(fhdr, "\n");
	fprintf(fhdr, "\tEntries={ ");
	for (i = 0; i < entries; i++) {
		if (i > 0)
			fprintf(fhdr, ", ");
		fprintf(fhdr, "0x%016llx", body[i]);
	}
	fprintf(fhdr, " }\n");
	fprintf(fhdr, "\n");
}

void
acpi_print_facp(struct FACPbody *facp)
{
	char		sep;

	fprintf(fhdr, "\n");
	fprintf(fhdr, "\tDSDT=0x%x\n", facp->dsdt_ptr);
	fprintf(fhdr, "\tINT_MODEL=%s\n", facp->int_model ? "APIC" : "PIC");
	fprintf(fhdr, "\tSCI_INT=%d\n", facp->sci_int);
	fprintf(fhdr, "\tSMI_CMD=0x%x, ", facp->smi_cmd);
	fprintf(fhdr, "ACPI_ENABLE=0x%x, ", facp->acpi_enable);
	fprintf(fhdr, "ACPI_DISABLE=0x%x, ", facp->acpi_disable);
	fprintf(fhdr, "S4BIOS_REQ=0x%x\n", facp->s4biosreq);
	if (facp->pm1a_evt_blk)
		fprintf(fhdr, "\tPM1a_EVT_BLK=0x%x-0x%x\n",
		    facp->pm1a_evt_blk,
		    facp->pm1a_evt_blk + facp->pm1_evt_len - 1);
	if (facp->pm1b_evt_blk)
		fprintf(fhdr, "\tPM1b_EVT_BLK=0x%x-0x%x\n",
		    facp->pm1b_evt_blk,
		    facp->pm1b_evt_blk + facp->pm1_evt_len - 1);
	if (facp->pm1a_cnt_blk)
		fprintf(fhdr, "\tPM1a_CNT_BLK=0x%x-0x%x\n",
		    facp->pm1a_cnt_blk,
		    facp->pm1a_cnt_blk + facp->pm1_cnt_len - 1);
	if (facp->pm1b_cnt_blk)
		fprintf(fhdr, "\tPM1b_CNT_BLK=0x%x-0x%x\n",
		    facp->pm1b_cnt_blk,
		    facp->pm1b_cnt_blk + facp->pm1_cnt_len - 1);
	if (facp->pm2_cnt_blk)
		fprintf(fhdr, "\tPM2_CNT_BLK=0x%x-0x%x\n",
		    facp->pm2_cnt_blk,
		    facp->pm2_cnt_blk + facp->pm2_cnt_len - 1);
	if (facp->pm_tmr_blk)
		fprintf(fhdr, "\tPM2_TMR_BLK=0x%x-0x%x\n",
		    facp->pm_tmr_blk,
		    facp->pm_tmr_blk + facp->pm_tmr_len - 1);
	if (facp->gpe0_blk)
		fprintf(fhdr, "\tPM2_GPE0_BLK=0x%x-0x%x\n",
		    facp->gpe0_blk,
		    facp->gpe0_blk + facp->gpe0_len - 1);
	if (facp->gpe1_blk)
		fprintf(fhdr, "\tPM2_GPE1_BLK=0x%x-0x%x, GPE1_BASE=%d\n",
		    facp->gpe1_blk,
		    facp->gpe1_blk + facp->gpe1_len - 1,
		    facp->gpe1_base);
	fprintf(fhdr, "\tP_LVL2_LAT=%dms, P_LVL3_LAT=%dms\n",
	    facp->p_lvl2_lat, facp->p_lvl3_lat);
	fprintf(fhdr, "\tFLUSH_SIZE=%d, FLUSH_STRIDE=%d\n",
	    facp->flush_size, facp->flush_stride);
	fprintf(fhdr, "\tDUTY_OFFSET=%d, DUTY_WIDTH=%d\n",
	    facp->duty_off, facp->duty_width);
	fprintf(fhdr, "\tDAY_ALRM=%d, MON_ALRM=%d, CENTURY=%d\n",
	    facp->day_alrm, facp->mon_alrm, facp->century);
	fprintf(fhdr, "\tFlags=");
	sep = '{';

	PRINTFLAG(WBINVD);
	PRINTFLAG(WBINVD_FLUSH);
	PRINTFLAG(PROC_C1);
	PRINTFLAG(P_LVL2_UP);
	PRINTFLAG(PWR_BUTTON);
	PRINTFLAG(SLP_BUTTON);
	PRINTFLAG(FIX_RTC);
	PRINTFLAG(RTC_S4);
	PRINTFLAG(TMR_VAL_EXT);
	PRINTFLAG(DCK_CAP);

	fprintf(fhdr, "}\n");
	fprintf(fhdr, "\n");
}

void
acpi_print_dsdt(struct ACPIsdt *dsdp)
{
	acpi_print_sdt(dsdp);
}

void
acpi_handle_dsdt(struct ACPIsdt *dsdp)
{
	u_int8_t	*dp;
	u_int8_t	*end;

	acpi_print_dsdt(dsdp);

	dp = (u_int8_t *)dsdp->body;
	end = (u_int8_t *)dsdp + dsdp->len;
}

void
acpi_handle_facp(struct FACPbody *facp)
{
	struct ACPIsdt	*dsdp;

	acpi_print_facp(facp);
	if (facp->dsdt_ptr == 0)
		dsdp = (struct ACPIsdt *) acpi_map_sdt(facp->x_dsdt);
	else
		dsdp = (struct ACPIsdt *) acpi_map_sdt(facp->dsdt_ptr);
	if (acpi_checksum(dsdp, dsdp->len))
		errx(1, "DSDT is corrupt");
	acpi_handle_dsdt(dsdp);
	aml_dump(dsdp);
}

void
acpi_handle_rsdt(struct ACPIsdt *rsdp)
{
	int		i;
	int		entries;
	struct ACPIsdt	*sdp;

	aml_dump(rsdp);
	entries = (rsdp->len - SIZEOF_SDT_HDR) / sizeof(u_int32_t);
	acpi_print_rsdt(rsdp);
	for (i = 0; i < entries; i++) {
		sdp = (struct ACPIsdt *) acpi_map_sdt(rsdp->body[i]);
		if (acpi_checksum(sdp, sdp->len))
			errx(1, "RSDT entry %d is corrupt", i);
		aml_dump(sdp);
		if (!memcmp(sdp->signature, "FACP", 4)) {
			acpi_handle_facp((struct FACPbody *) sdp->body);
		} else {
			acpi_print_sdt(sdp);
		}
	}
}

void
acpi_handle_xsdt(struct ACPIsdt *rsdp)
{
	int		i;
	int		entries;
	struct ACPIsdt	*sdp;
	u_int64_t	*body = (u_int64_t *) rsdp->body;

	aml_dump(rsdp);
	entries = (rsdp->len - SIZEOF_SDT_HDR) / sizeof(u_int64_t);
	acpi_print_xsdt(rsdp);
	for (i = 0; i < entries; i++) {
		sdp = (struct ACPIsdt *) acpi_map_sdt(body[i]);
		if (acpi_checksum(sdp, sdp->len))
			errx(1, "XSDT entry %d is corrupt", i);
		aml_dump(sdp);
		if (!memcmp(sdp->signature, "FACP", 4)) {
			acpi_handle_facp((struct FACPbody *) sdp->body);
		} else {
			acpi_print_sdt(sdp);
		}
	}
}

void
asl_dump_from_devmem(void)
{
	struct ACPIrsdp	*rp;
	struct ACPIsdt	*rsdp;
	char		name[PATH_MAX];

	snprintf(name, sizeof(name), "%s%cheaders", aml_dumpfile,
	    aml_dumpdir ? '/' : '.');

	acpi_user_init();

	/* Can only unveil if being dumped to a dir */
	if (aml_dumpdir) {
		if (unveil(aml_dumpfile, "wc") == -1)
			err(1, "unveil %s", aml_dumpfile);
	} else if (aml_dumpfile[0] == '/') {	/* admittedly pretty shitty */
		if (unveil("/", "wc") == -1)
			err(1, "unveil /");
	} else {
		if (unveil(".", "wc") == -1)
			err(1, "unveil .");
	}

	if (unveil(_PATH_MEM, "r") == -1)
		err(1, "unveil %s", _PATH_MEM);
	if (unveil(_PATH_KMEM, "r") == -1)
		err(1, "unveil %s", _PATH_KMEM);
	if (unveil(_PATH_KVMDB, "r") == -1)
		err(1, "unveil %s", _PATH_KVMDB);
	if (unveil(_PATH_KSYMS, "r") == -1)
		err(1, "unveil %s", _PATH_KSYMS);
	if (unveil(_PATH_UNIX, "r") == -1)
		err(1, "unveil %s", _PATH_UNIX);
	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	rp = acpi_find_rsd_ptr();
	if (!rp) {
		if (!quiet)
			warnx("Can't find ACPI information");
		exit(1);
	}

	fhdr = fopen(name, "w");
	if (fhdr == NULL)
		err(1, "asl_dump_from_devmem");

	acpi_print_rsd_ptr(rp);

	if (rp->rev == 2 && rp->xaddr) {
		rsdp = (struct ACPIsdt *) acpi_map_sdt(rp->xaddr);
		if (memcmp(rsdp->signature, "XSDT", 4) ||
		    acpi_checksum(rsdp, rsdp->len))
			errx(1, "XSDT is corrupted");

		acpi_handle_xsdt(rsdp);
	} else if (rp->addr) {
		rsdp = (struct ACPIsdt *) acpi_map_sdt(rp->addr);
		if (memcmp(rsdp->signature, "RSDT", 4) ||
		    acpi_checksum(rsdp, rsdp->len))
			errx(1, "RSDT is corrupted");

		acpi_handle_rsdt(rsdp);
	} else
		errx(1, "XSDT or RSDT not found");

	fclose(fhdr);
}

void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s -o prefix\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct stat	st;
	int		c;

	while ((c = getopt(argc, argv, "o:q")) != -1) {
		switch (c) {
		case 'o':
			aml_dumpfile = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (aml_dumpfile == NULL)
		usage();

	if (stat(aml_dumpfile, &st) == 0 && S_ISDIR(st.st_mode))
		aml_dumpdir = 1;

	asl_dump_from_devmem();

	return (0);
}

#ifdef __aarch64__

u_long
efi_acpi_addr(void)
{
	kvm_t		*kd;
	struct nlist	 nl[2];
	uint64_t	 table;

	memset(&nl, 0, sizeof(nl));
	kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, NULL);
	if (kd == NULL)
		goto on_error;
	nl[0].n_name = "_efi_acpi_table";
	if (kvm_nlist(kd, nl) != 0)
		goto on_error;
	if (kvm_read(kd, nl[0].n_value, &table, sizeof(table)) == -1)
		goto on_error;

	kvm_close(kd);
	return table;

on_error:
	if (kd != NULL)
		kvm_close(kd);
	return (0);
}

#else

#include <machine/biosvar.h>

u_long
efi_acpi_addr(void)
{
	kvm_t		*kd;
	struct nlist	 nl[2];
	bios_efiinfo_t	 efiinfo;
	u_long	 ptr;

	memset(&nl, 0, sizeof(nl));
	kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, NULL);
	if (kd == NULL)
		goto on_error;
	nl[0].n_name = "_bios_efiinfo";
	if (kvm_nlist(kd, nl) != 0)
		goto on_error;
	if (kvm_read(kd, nl[0].n_value, &ptr, sizeof(ptr)) == -1)
		goto on_error;
	if (kvm_read(kd, ptr, &efiinfo, sizeof(efiinfo)) == -1)
		goto on_error;

	kvm_close(kd);
	return (efiinfo.config_acpi);

on_error:
	if (kd != NULL)
		kvm_close(kd);
	return (0);
}

#endif
