/*	$OpenBSD: file.c,v 1.20 2024/10/16 18:47:48 miod Exp $ */

/*
 * Copyright (c) 1995-96 Mats O Jansson.  All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "os.h"
#include "common.h"
#include "file.h"
#include "mopdef.h"
#include <stddef.h>

#ifndef NOAOUT
#if defined(__OpenBSD__)
#include <a.out.h>
#endif
#if defined(__bsdi__)
#define NOAOUT
#endif
#if defined(__FreeBSD__)
#include <sys/imgact_aout.h>
#endif
#if !defined(MID_I386)
#define MID_I386 134
#endif
#if !defined(MID_SPARC)
#define MID_SPARC 138
#endif
#if !defined(MID_VAX)
#define MID_VAX 140
#endif
#endif

#ifndef NOELF
#if defined(__OpenBSD__)
#include <elf.h>
#else
#define NOELF
#endif
#endif

#ifndef NOELF
#if !defined(_LP64)
#define NOELF64
#endif
#endif

#ifndef NOAOUT
static int	getCLBYTES(int);
static int	getMID(int, int);
#endif

const char *
FileTypeName(mopd_imagetype type)
{

	switch (type) {
	case IMAGE_TYPE_MOP:
		return ("MOP");

	case IMAGE_TYPE_ELF32:
		return ("Elf32");

	case IMAGE_TYPE_ELF64:
		return ("Elf64");

	case IMAGE_TYPE_AOUT:
		return ("a.out");
	}

	abort();
}

void
mopFilePutLX(u_char *buf, int idx, u_int32_t value, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++) {
		buf[idx+i] = value % 256;
		value = value / 256;
	}
}

void
mopFilePutBX(u_char *buf, int idx, u_int32_t value, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++) {
		buf[idx+cnt-1-i] = value % 256;
		value = value / 256;
	}
}

u_int32_t
mopFileGetLX(u_char *buf, int idx, int cnt)
{
	u_int32_t ret = 0;
	int i;

	for (i = 0; i < cnt; i++) {
		int j = idx + cnt - 1 - i;
		if (j < 0)
			abort();
		ret = ret * 256 + buf[j];
	}

	return(ret);
}

u_int32_t
mopFileGetBX(u_char *buf, int idx, int cnt)
{
	u_int32_t ret = 0;
	int i;

	for (i = 0; i < cnt; i++) {
		int j = idx + i;
		if (j < 0)
			abort();
		ret = ret * 256 + buf[j];
	}

	return(ret);
}

#if !defined(NOELF) && !defined(NOELF64)
u_int64_t
mopFileGetLXX(u_char *buf, int idx, int cnt)
{
	u_int64_t ret = 0;
	int i;

	for (i = 0; i < cnt; i++) {
		int j = idx + cnt - 1 - i;
		if (j < 0)
			abort();
		ret = ret * 256 + buf[j];
	}

	return(ret);
}

u_int64_t
mopFileGetBXX(u_char *buf, int idx, int cnt)
{
	u_int64_t ret = 0;
	int i;

	for (i = 0; i < cnt; i++) {
		int j = idx + i;
		if (j < 0)
			abort();
		ret = ret * 256 + buf[j];
	}

	return(ret);
}
#endif

void
mopFileSwapX(u_char *buf, int idx, int cnt)
{
	int i;
	u_char c;

	for (i = 0; i < (cnt / 2); i++) {
		c = buf[idx+i];
		buf[idx+i] = buf[idx+cnt-1-i];
		buf[idx+cnt-1-i] = c;
	}

}

int
CheckMopFile(int fd)
{
	u_char	header[512];
	short	image_type;

	if (read(fd, header, 512) != 512)
		return(-1);

	(void)lseek(fd, (off_t) 0, SEEK_SET);

	image_type = (u_short)(header[IHD_W_ALIAS+1]*256 + header[IHD_W_ALIAS]);

	switch(image_type) {
	case IHD_C_NATIVE:		/* Native mode image (VAX)   */
	case IHD_C_RSX:			/* RSX image produced by TKB */
	case IHD_C_BPA:			/* BASIC plus analog         */
	case IHD_C_ALIAS:		/* Alias		     */
	case IHD_C_CLI:			/* Image is CLI		     */
	case IHD_C_PMAX:		/* PMAX system image	     */
	case IHD_C_ALPHA:		/* ALPHA system image	     */
		break;
	default:
		return(-1);
	}

	return(0);
}

int
GetMopFileInfo(struct dllist *dl, int info)
{
	u_char		header[512];
	short		image_type;
	u_int32_t	load_addr, xfr_addr, isd, iha, hbcnt, isize;

	if (read(dl->ldfd, header, 512) != 512)
		return(-1);

	image_type = (u_short)(header[IHD_W_ALIAS+1]*256 +
			       header[IHD_W_ALIAS]);

	switch (image_type) {
	case IHD_C_NATIVE:		/* Native mode image (VAX)   */
		isd = (header[IHD_W_SIZE+1]*256 +
		       header[IHD_W_SIZE]);
		iha = (header[IHD_W_ACTIVOFF+1]*256 +
		       header[IHD_W_ACTIVOFF]);
		hbcnt = (header[IHD_B_HDRBLKCNT]);
		isize = (header[isd+ISD_W_PAGCNT+1]*256 +
			 header[isd+ISD_W_PAGCNT]) * 512;
		load_addr = ((header[isd+ISD_V_VPN+1]*256 +
			      header[isd+ISD_V_VPN]) & ISD_M_VPN)
				* 512;
		xfr_addr = (header[iha+IHA_L_TFRADR1+3]*0x1000000 +
			    header[iha+IHA_L_TFRADR1+2]*0x10000 +
			    header[iha+IHA_L_TFRADR1+1]*0x100 +
			    header[iha+IHA_L_TFRADR1]) & 0x7fffffff;
		if (info == INFO_PRINT) {
			printf("Native Image (VAX)\n");
			printf("Header Block Count: %d\n",hbcnt);
			printf("Image Size:         %08x\n",isize);
			printf("Load Address:       %08x\n",load_addr);
			printf("Transfer Address:   %08x\n",xfr_addr);
		}
		break;
	case IHD_C_RSX:			/* RSX image produced by TKB */
		hbcnt = header[L_BBLK+1]*256 + header[L_BBLK];
		isize = (header[L_BLDZ+1]*256 + header[L_BLDZ]) * 64;
		load_addr = header[L_BSA+1]*256 + header[L_BSA];
		xfr_addr  = header[L_BXFR+1]*256 + header[L_BXFR];
		if (info == INFO_PRINT) {
			printf("RSX Image\n");
			printf("Header Block Count: %d\n",hbcnt);
			printf("Image Size:         %08x\n",isize);
			printf("Load Address:       %08x\n",load_addr);
			printf("Transfer Address:   %08x\n",xfr_addr);
		}
		break;
	case IHD_C_BPA:			/* BASIC plus analog         */
		if (info == INFO_PRINT) {
			printf("BASIC-Plus Image, not supported\n");
		}
		return(-1);
		break;
	case IHD_C_ALIAS:		/* Alias		     */
		if (info == INFO_PRINT) {
			printf("Alias, not supported\n");
		}
		return(-1);
		break;
	case IHD_C_CLI:			/* Image is CLI		     */
		if (info == INFO_PRINT) {
			printf("CLI, not supported\n");
		}
		return(-1);
		break;
	case IHD_C_PMAX:		/* PMAX system image	     */
		isd = (header[IHD_W_SIZE+1]*256 +
		       header[IHD_W_SIZE]);
		iha = (header[IHD_W_ACTIVOFF+1]*256 +
		       header[IHD_W_ACTIVOFF]);
		hbcnt = (header[IHD_B_HDRBLKCNT]);
		isize = (header[isd+ISD_W_PAGCNT+1]*256 +
			 header[isd+ISD_W_PAGCNT]) * 512;
		load_addr = (header[isd+ISD_V_VPN+1]*256 +
			     header[isd+ISD_V_VPN]) * 512;
		xfr_addr = (header[iha+IHA_L_TFRADR1+3]*0x1000000 +
			    header[iha+IHA_L_TFRADR1+2]*0x10000 +
			    header[iha+IHA_L_TFRADR1+1]*0x100 +
			    header[iha+IHA_L_TFRADR1]);
		if (info == INFO_PRINT) {
			printf("PMAX Image \n");
			printf("Header Block Count: %d\n",hbcnt);
			printf("Image Size:         %08x\n",isize);
			printf("Load Address:       %08x\n",load_addr);
			printf("Transfer Address:   %08x\n",xfr_addr);
		}
		break;
	case IHD_C_ALPHA:		/* ALPHA system image	     */
		isd = (header[EIHD_L_ISDOFF+3]*0x1000000 +
		       header[EIHD_L_ISDOFF+2]*0x10000 +
		       header[EIHD_L_ISDOFF+1]*0x100 +
		       header[EIHD_L_ISDOFF]);
		hbcnt = (header[EIHD_L_HDRBLKCNT+3]*0x1000000 +
			 header[EIHD_L_HDRBLKCNT+2]*0x10000 +
			 header[EIHD_L_HDRBLKCNT+1]*0x100 +
			 header[EIHD_L_HDRBLKCNT]);
		isize = (header[isd+EISD_L_SECSIZE+3]*0x1000000 +
			 header[isd+EISD_L_SECSIZE+2]*0x10000 +
			 header[isd+EISD_L_SECSIZE+1]*0x100 +
			 header[isd+EISD_L_SECSIZE]);
		load_addr = 0;
		xfr_addr = 0;
		if (info == INFO_PRINT) {
			printf("Alpha Image \n");
			printf("Header Block Count: %d\n",hbcnt);
			printf("Image Size:         %08x\n",isize);
			printf("Load Address:       %08x\n",load_addr);
			printf("Transfer Address:   %08x\n",xfr_addr);
		}
		break;
	default:
		if (info == INFO_PRINT) {
			printf("Unknown Image (%d)\n",image_type);
		}
		return(-1);
	}

	dl->image_type = IMAGE_TYPE_MOP;
	dl->loadaddr = load_addr;
	dl->xferaddr = xfr_addr;

	return(0);
}

#ifndef NOAOUT
static int
getMID(int old_mid, int new_mid)
{
	int	mid;

	mid = old_mid;

	switch (new_mid) {
	case MID_I386:
		mid = MID_I386;
		break;
#ifdef MID_M68K
	case MID_M68K:
		mid = MID_M68K;
		break;
#endif
#ifdef MID_M68K4K
	case MID_M68K4K:
		mid = MID_M68K4K;
		break;
#endif
#ifdef MID_NS32532
	case MID_NS32532:
		mid = MID_NS32532;
		break;
#endif
	case MID_SPARC:
		mid = MID_SPARC;
		break;
#ifdef MID_PMAX
	case MID_PMAX:
		mid = MID_PMAX;
		break;
#endif
#ifdef MID_VAX
	case MID_VAX:
		mid = MID_VAX;
		break;
#endif
#ifdef MID_ALPHA
	case MID_ALPHA:
		mid = MID_ALPHA;
		break;
#endif
#ifdef MID_MIPS
	case MID_MIPS:
		mid = MID_MIPS;
		break;
#endif
#ifdef MID_ARM6
	case MID_ARM6:
		mid = MID_ARM6;
		break;
#endif
	default:
		break;
	}

	return(mid);
}

static int
getCLBYTES(int mid)
{
	int	clbytes;

	switch (mid) {
#ifdef MID_VAX
	case MID_VAX:
		clbytes = 1024;
		break;
#endif
#ifdef MID_I386
	case MID_I386:
#endif
#ifdef MID_M68K4K
	case MID_M68K4K:
#endif
#ifdef MID_NS32532
	case MID_NS32532:
#endif
#ifdef MID_PMAX
	case MID_PMAX:
#endif
#ifdef MID_MIPS
	case MID_MIPS:
#endif
#ifdef MID_ARM6
	case MID_ARM6:
#endif
#if defined(MID_I386) || defined(MID_M68K4K) || defined(MID_NS32532) || \
    defined(MID_PMAX) || defined(MID_MIPS) || defined(MID_ARM6)
		clbytes = 4096;
		break;
#endif
#ifdef MID_M68K
	case MID_M68K:
#endif
#ifdef MID_ALPHA
	case MID_ALPHA:
#endif
#ifdef MID_SPARC
	case MID_SPARC:
#endif
#if defined(MID_M68K) || defined(MID_ALPHA) || defined(MID_SPARC)
		clbytes = 8192;
		break;
#endif
	default:
		clbytes = 0;
	}

	return(clbytes);
}
#endif

int
CheckElfFile(int fd)
{
#ifdef NOELF
	return(-1);
#else
	Elf32_Ehdr ehdr;

	(void)lseek(fd, (off_t) 0, SEEK_SET);

	if (read(fd, (char *)&ehdr, sizeof(ehdr)) != sizeof(ehdr))
		return(-1);

	if (ehdr.e_ident[0] != ELFMAG0 ||
	    ehdr.e_ident[1] != ELFMAG1 ||
	    ehdr.e_ident[2] != ELFMAG2 ||
	    ehdr.e_ident[3] != ELFMAG3)
		return(-1);

	/* Must be Elf32 or Elf64... */
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS32 &&
	    ehdr.e_ident[EI_CLASS] != ELFCLASS64)
		return(-1);

	return(0);
#endif /* NOELF */
}

int
GetElf32FileInfo(struct dllist *dl, int info)
{
#ifdef NOELF
	return(-1);
#else
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr;
	uint32_t e_machine, e_entry;
	uint32_t e_phoff, e_phentsize, e_phnum;
	int ei_data, i;

	(void)lseek(dl->ldfd, (off_t) 0, SEEK_SET);

	if (read(dl->ldfd, (char *)&ehdr, sizeof(ehdr)) != sizeof(ehdr))
		return(-1);

	if (ehdr.e_ident[0] != ELFMAG0 ||
	    ehdr.e_ident[1] != ELFMAG1 ||
	    ehdr.e_ident[2] != ELFMAG2 ||
	    ehdr.e_ident[3] != ELFMAG3)
		return(-1);

	/* Must be Elf32... */
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS32)
		return(-1);

	ei_data = ehdr.e_ident[EI_DATA];

	switch (ei_data) {
	case ELFDATA2LSB:
		e_machine = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_machine),
		    sizeof(ehdr.e_machine));
		e_entry = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_entry),
		    sizeof(ehdr.e_entry));

		e_phoff = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_phoff),
		    sizeof(ehdr.e_phoff));
		e_phentsize = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_phentsize),
		    sizeof(ehdr.e_phentsize));
		e_phnum = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_phnum),
		    sizeof(ehdr.e_phnum));
		break;

	case ELFDATA2MSB:
		e_machine = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_machine),
		    sizeof(ehdr.e_machine));
		e_entry = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_entry),
		    sizeof(ehdr.e_entry));

		e_phoff = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_phoff),
		    sizeof(ehdr.e_phoff));
		e_phentsize = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_phentsize),
		    sizeof(ehdr.e_phentsize));
		e_phnum = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf32_Ehdr, e_phnum),
		    sizeof(ehdr.e_phnum));
		break;

	default:
		return(-1);
	}

	if (e_phnum > SEC_MAX)
		return(-1);
	dl->e_nsec = e_phnum;
	for (i = 0; i < dl->e_nsec; i++) {
		if (lseek(dl->ldfd, (off_t) e_phoff + (i * e_phentsize),
		    SEEK_SET) == (off_t) -1)
			return(-1);
		if (read(dl->ldfd, (char *) &phdr, sizeof(phdr)) !=
		    sizeof(phdr))
			return(-1);

		switch (ei_data) {
		case ELFDATA2LSB:
			dl->e_sections[i].s_foff =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_offset),
			    sizeof(phdr.p_offset));
			dl->e_sections[i].s_vaddr =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_vaddr),
			    sizeof(phdr.p_vaddr));
			dl->e_sections[i].s_fsize =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_filesz),
			    sizeof(phdr.p_filesz));
			dl->e_sections[i].s_msize =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_memsz),
			    sizeof(phdr.p_memsz));
			break;

		case ELFDATA2MSB:
			dl->e_sections[i].s_foff =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_offset),
			    sizeof(phdr.p_offset));
			dl->e_sections[i].s_vaddr =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_vaddr),
			    sizeof(phdr.p_vaddr));
			dl->e_sections[i].s_fsize =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_filesz),
			    sizeof(phdr.p_filesz));
			dl->e_sections[i].s_msize =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf32_Phdr, p_memsz),
			    sizeof(phdr.p_memsz));
			break;

		default:
			return(-1);
		}
	}
	/*
	 * In addition to padding between segments, this also
	 * takes care of memsz > filesz.
	 */
	for (i = 0; i < dl->e_nsec - 1; i++) {
		dl->e_sections[i].s_pad =
		    dl->e_sections[i + 1].s_vaddr -
		    (dl->e_sections[i].s_vaddr + dl->e_sections[i].s_fsize);
	}
	dl->e_sections[dl->e_nsec - 1].s_pad =
	    dl->e_sections[dl->e_nsec - 1].s_msize -
	    dl->e_sections[dl->e_nsec - 1].s_fsize;
	/*
	 * Now compute the logical offsets for each section.
	 */
	dl->e_sections[0].s_loff = 0;
	for (i = 1; i < dl->e_nsec; i++) {
		dl->e_sections[i].s_loff =
		    dl->e_sections[i - 1].s_loff +
		    dl->e_sections[i - 1].s_fsize +
		    dl->e_sections[i - 1].s_pad;
	}

	dl->image_type = IMAGE_TYPE_ELF32;
	dl->loadaddr = 0;
#if 0
	dl->xferaddr = e_entry;		/* will relocate itself if necessary */
#else
	dl->xferaddr = e_entry - dl->e_sections[0].s_vaddr;
#endif

	/* Print info about the image. */
	if (info == INFO_PRINT) {
		printf("Elf32 image (");
		switch (e_machine) {
#ifdef EM_VAX
		case EM_VAX:
			printf("VAX");
			break;
#endif
		default:
			printf("machine %d", e_machine);
			break;
		}
		printf(")\n");
		printf("Transfer Address:   %08x\n", dl->xferaddr);
		printf("Program Sections:   %d\n", dl->e_nsec);
		for (i = 0; i < dl->e_nsec; i++) {
			printf(" S%d File Size:      %08x\n", i,
			    dl->e_sections[i].s_fsize);
			printf(" S%d Pad Size:       %08x\n", i,
			    dl->e_sections[i].s_pad);
		}
	}

	dl->e_machine = e_machine;

	dl->e_curpos = 0;
	dl->e_cursec = 0;

	return(0);
#endif /* NOELF */
}

int
GetElf64FileInfo(struct dllist *dl, int info)
{
#if defined(NOELF) || defined(NOELF64)
	return(-1);
#else
	Elf64_Ehdr ehdr;
	Elf64_Phdr phdr;
	uint32_t e_machine;
	uint32_t e_phentsize, e_phnum;
	uint64_t e_entry, e_phoff;
	int ei_data, i;

	(void)lseek(dl->ldfd, (off_t) 0, SEEK_SET);

	if (read(dl->ldfd, (char *)&ehdr, sizeof(ehdr)) != sizeof(ehdr))
		return(-1);

	if (ehdr.e_ident[0] != ELFMAG0 ||
	    ehdr.e_ident[1] != ELFMAG1 ||
	    ehdr.e_ident[2] != ELFMAG2 ||
	    ehdr.e_ident[3] != ELFMAG3)
		return(-1);

	/* Must be Elf64... */
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS64)
		return(-1);

	ei_data = ehdr.e_ident[EI_DATA];

	switch (ei_data) {
	case ELFDATA2LSB:
		e_machine = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_machine),
		    sizeof(ehdr.e_machine));
		e_entry = mopFileGetLXX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_entry),
		    sizeof(ehdr.e_entry));

		e_phoff = mopFileGetLXX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_phoff),
		    sizeof(ehdr.e_phoff));
		e_phentsize = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_phentsize),
		    sizeof(ehdr.e_phentsize));
		e_phnum = mopFileGetLX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_phnum),
		    sizeof(ehdr.e_phnum));
		break;

	case ELFDATA2MSB:
		e_machine = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_machine),
		    sizeof(ehdr.e_machine));
		e_entry = mopFileGetBXX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_entry),
		    sizeof(ehdr.e_entry));

		e_phoff = mopFileGetBXX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_phoff),
		    sizeof(ehdr.e_phoff));
		e_phentsize = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_phentsize),
		    sizeof(ehdr.e_phentsize));
		e_phnum = mopFileGetBX((u_char *) &ehdr,
		    offsetof(Elf64_Ehdr, e_phnum),
		    sizeof(ehdr.e_phnum));
		break;

	default:
		return(-1);
	}

	if (e_phnum > SEC_MAX)
		return(-1);
	dl->e_nsec = e_phnum;
	for (i = 0; i < dl->e_nsec; i++) {
		if (lseek(dl->ldfd, (off_t) e_phoff + (i * e_phentsize),
		    SEEK_SET) == (off_t) -1)
			return(-1);
		if (read(dl->ldfd, (char *) &phdr, sizeof(phdr)) !=
		    sizeof(phdr))
			return(-1);

		switch (ei_data) {
		case ELFDATA2LSB:
			dl->e_sections[i].s_foff =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_offset),
			    sizeof(phdr.p_offset));
			dl->e_sections[i].s_vaddr =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_vaddr),
			    sizeof(phdr.p_vaddr));
			dl->e_sections[i].s_fsize =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_filesz),
			    sizeof(phdr.p_filesz));
			dl->e_sections[i].s_msize =
			    mopFileGetLX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_memsz),
			    sizeof(phdr.p_memsz));
			break;

		case ELFDATA2MSB:
			dl->e_sections[i].s_foff =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_offset),
			    sizeof(phdr.p_offset));
			dl->e_sections[i].s_vaddr =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_vaddr),
			    sizeof(phdr.p_vaddr));
			dl->e_sections[i].s_fsize =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_filesz),
			    sizeof(phdr.p_filesz));
			dl->e_sections[i].s_msize =
			    mopFileGetBX((u_char *) &phdr,
			    offsetof(Elf64_Phdr, p_memsz),
			    sizeof(phdr.p_memsz));
			break;

		default:
			return(-1);
		}
	}
	/*
	 * In addition to padding between segments, this also
	 * takes care of memsz > filesz.
	 */
	for (i = 0; i < dl->e_nsec - 1; i++) {
		dl->e_sections[i].s_pad =
		    dl->e_sections[i + 1].s_vaddr -
		    (dl->e_sections[i].s_vaddr + dl->e_sections[i].s_fsize);
	}
	dl->e_sections[dl->e_nsec - 1].s_pad =
	    dl->e_sections[dl->e_nsec - 1].s_msize -
	    dl->e_sections[dl->e_nsec - 1].s_fsize;
	/*
	 * Now compute the logical offsets for each section.
	 */
	dl->e_sections[0].s_loff = 0;
	for (i = 1; i < dl->e_nsec; i++) {
		dl->e_sections[i].s_loff =
		    dl->e_sections[i - 1].s_loff +
		    dl->e_sections[i - 1].s_fsize +
		    dl->e_sections[i - 1].s_pad;
	}

	dl->image_type = IMAGE_TYPE_ELF64;
	dl->loadaddr = 0;
#if 0
	dl->xferaddr = e_entry;		/* will relocate itself if necessary */
#else
	dl->xferaddr = e_entry - dl->e_sections[0].s_vaddr;
#endif

	/* Print info about the image. */
	if (info == INFO_PRINT) {
		printf("Elf64 image (");
		switch (e_machine) {
#ifdef EM_ALPHA
		case EM_ALPHA:
#endif
#ifdef EM_ALPHA_EXP
		case EM_ALPHA_EXP:
#endif
#if defined(EM_ALPHA) || defined(EM_ALPHA_EXP)
			printf("ALPHA");
			break;
#endif
		default:
			printf("machine %d", e_machine);
			break;
		}
		printf(")\n");
		printf("Transfer Address:   %08x\n", dl->xferaddr);
		printf("Program Sections:   %d\n", dl->e_nsec);
		for (i = 0; i < dl->e_nsec; i++) {
			printf(" S%d File Size:      %08x\n", i,
			    dl->e_sections[i].s_fsize);
			printf(" S%d Pad Size:       %08x\n", i,
			    dl->e_sections[i].s_pad);
		}
	}

	dl->e_machine = e_machine;

	dl->e_curpos = 0;
	dl->e_cursec = 0;

	return(0);
#endif /* NOELF || NOELF64 */
}

int
CheckAOutFile(int fd)
{
#ifdef NOAOUT
	return(-1);
#else
	struct exec ex, ex_swap;
	int	mid = -1;

	if (read(fd, (char *)&ex, sizeof(ex)) != sizeof(ex))
		return(-1);

	(void)lseek(fd, (off_t) 0, SEEK_SET);

	if (read(fd, (char *)&ex_swap, sizeof(ex_swap)) != sizeof(ex_swap))
		return(-1);

	(void)lseek(fd, (off_t) 0, SEEK_SET);

	mid = getMID(mid, N_GETMID (ex));

	if (mid == -1) {
		mid = getMID(mid, N_GETMID (ex_swap));
	}

	if (mid != -1) {
		return(0);
	} else {
		return(-1);
	}
#endif /* NOAOUT */
}

int
GetAOutFileInfo(struct dllist *dl, int info)
{
#ifdef NOAOUT
	return(-1);
#else
	struct exec ex, ex_swap;
	u_int32_t	mid = -1;
	u_int32_t	magic, clbytes, clofset;

	if (read(dl->ldfd, (char *)&ex, sizeof(ex)) != sizeof(ex))
		return(-1);

	(void)lseek(dl->ldfd, (off_t) 0, SEEK_SET);

	if (read(dl->ldfd, (char *)&ex_swap,
		 sizeof(ex_swap)) != sizeof(ex_swap))
		return(-1);

	mopFileSwapX((u_char *)&ex_swap, 0, 4);

	mid = getMID(mid, N_GETMID (ex));

	if (mid == (uint32_t)-1) {
		mid = getMID(mid, N_GETMID (ex_swap));
		if (mid != (uint32_t)-1) {
			mopFileSwapX((u_char *)&ex, 0, 4);
		}
	}

	if (mid == (uint32_t)-1) {
		return(-1);
	}

	if (N_BADMAG (ex)) {
		return(-1);
	}

	switch (mid) {
	case MID_I386:
#ifdef MID_NS32532
	case MID_NS32532:
#endif
#ifdef MID_PMAX
	case MID_PMAX:
#endif
#ifdef MID_VAX
	case MID_VAX:
#endif
#ifdef MID_ALPHA
	case MID_ALPHA:
#endif
#ifdef MID_ARM6
	case MID_ARM6:
#endif
		ex.a_text  = mopFileGetLX((u_char *)&ex_swap,  4, 4);
		ex.a_data  = mopFileGetLX((u_char *)&ex_swap,  8, 4);
		ex.a_bss   = mopFileGetLX((u_char *)&ex_swap, 12, 4);
		ex.a_syms  = mopFileGetLX((u_char *)&ex_swap, 16, 4);
		ex.a_entry = mopFileGetLX((u_char *)&ex_swap, 20, 4);
		ex.a_trsize= mopFileGetLX((u_char *)&ex_swap, 24, 4);
		ex.a_drsize= mopFileGetLX((u_char *)&ex_swap, 28, 4);
		break;
#ifdef MID_M68K
	case MID_M68K:
#endif
#ifdef MID_M68K4K
	case MID_M68K4K:
#endif
	case MID_SPARC:
#ifdef MID_MIPS
	case MID_MIPS:
#endif
		ex.a_text  = mopFileGetBX((u_char *)&ex_swap,  4, 4);
		ex.a_data  = mopFileGetBX((u_char *)&ex_swap,  8, 4);
		ex.a_bss   = mopFileGetBX((u_char *)&ex_swap, 12, 4);
		ex.a_syms  = mopFileGetBX((u_char *)&ex_swap, 16, 4);
		ex.a_entry = mopFileGetBX((u_char *)&ex_swap, 20, 4);
		ex.a_trsize= mopFileGetBX((u_char *)&ex_swap, 24, 4);
		ex.a_drsize= mopFileGetBX((u_char *)&ex_swap, 28, 4);
		break;
	default:
		break;
	}

	if (info == INFO_PRINT) {
		printf("a.out image (");
		switch (N_GETMID (ex)) {
		case MID_I386:
			printf("i386");
			break;
#ifdef MID_M68K
		case MID_M68K:
			printf("m68k");
			break;
#endif
#ifdef MID_M68K4K
		case MID_M68K4K:
			printf("m68k 4k");
			break;
#endif
#ifdef MID_NS32532
		case MID_NS32532:
			printf("pc532");
			break;
#endif
		case MID_SPARC:
			printf("sparc");
			break;
#ifdef MID_PMAX
		case MID_PMAX:
			printf("pmax");
			break;
#endif
#ifdef MID_VAX
		case MID_VAX:
			printf("vax");
			break;
#endif
#ifdef MID_ALPHA
		case MID_ALPHA:
			printf("alpha");
			break;
#endif
#ifdef MID_MIPS
		case MID_MIPS:
			printf("mips");
			break;
#endif
#ifdef MID_ARM6
		case MID_ARM6:
			printf("arm32");
			break;
#endif
		default:
			break;
		}
		printf(") Magic: ");
		switch (N_GETMAGIC (ex)) {
		case OMAGIC:
			printf("OMAGIC");
			break;
		case NMAGIC:
			printf("NMAGIC");
			break;
		case ZMAGIC:
			printf("ZMAGIC");
			break;
		case QMAGIC:
			printf("QMAGIC");
			break;
		default:
			printf("Unknown %ld", (long) N_GETMAGIC (ex));
		}
		printf("\n");
		printf("Size of text:       %08lx\n", (long)ex.a_text);
		printf("Size of data:       %08lx\n", (long)ex.a_data);
		printf("Size of bss:        %08lx\n", (long)ex.a_bss);
		printf("Size of symbol tab: %08lx\n", (long)ex.a_syms);
		printf("Transfer Address:   %08lx\n", (long)ex.a_entry);
		printf("Size of reloc text: %08lx\n", (long)ex.a_trsize);
		printf("Size of reloc data: %08lx\n", (long)ex.a_drsize);
	}

	magic = N_GETMAGIC (ex);
	clbytes = getCLBYTES(mid);
	clofset = clbytes - 1;

	dl->image_type = IMAGE_TYPE_AOUT;
	dl->loadaddr = 0;
	dl->xferaddr = ex.a_entry;

	dl->a_text = ex.a_text;
	if (magic == ZMAGIC || magic == NMAGIC) {
		dl->a_text_fill = clbytes - (ex.a_text & clofset);
		if (dl->a_text_fill == clbytes)
			dl->a_text_fill = 0;
	} else
		dl->a_text_fill = 0;
	dl->a_data = ex.a_data;
	if (magic == ZMAGIC || magic == NMAGIC) {
		dl->a_data_fill = clbytes - (ex.a_data & clofset);
		if (dl->a_data_fill == clbytes)
			dl->a_data_fill = 0;
	} else
		dl->a_data_fill = 0;
	dl->a_bss = ex.a_bss;
	if (magic == ZMAGIC || magic == NMAGIC) {
		dl->a_bss_fill = clbytes - (ex.a_bss & clofset);
		if (dl->a_bss_fill == clbytes)
			dl->a_bss_fill = 0;
	} else {
		dl->a_bss_fill = clbytes -
		    ((ex.a_text+ex.a_data+ex.a_bss) & clofset);
		if (dl->a_bss_fill == clbytes)
			dl->a_bss_fill = 0;
	}
	dl->a_mid = mid;

	return(0);
#endif /* NOAOUT */
}

int
GetFileInfo(struct dllist *dl, int info)
{
	int error;

	error = CheckElfFile(dl->ldfd);
	if (error == 0) {
		error = GetElf32FileInfo(dl, info);
		if (error != 0)
			error = GetElf64FileInfo(dl, info);
		if (error != 0) {
			return(-1);
		}
		return (0);
	}

	error = CheckAOutFile(dl->ldfd);
	if (error == 0) {
		error = GetAOutFileInfo(dl, info);
		if (error != 0) {
			return(-1);
		}
		return (0);
	}

	error = CheckMopFile(dl->ldfd);
	if (error == 0) {
		error = GetMopFileInfo(dl, info);
		if (error != 0) {
			return(-1);
		}
		return (0);
	}

	/* Unknown file format. */
	return(-1);
}

ssize_t
mopFileRead(struct dllist *dlslot, u_char *buf)
{
	ssize_t len, outlen;
	int	bsz, sec;
	int32_t	pos, notdone, total;
	uint32_t secoff;

	switch (dlslot->image_type) {
	case IMAGE_TYPE_MOP:
		len = read(dlslot->ldfd,buf,dlslot->dl_bsz);
		break;

	case IMAGE_TYPE_ELF32:
	case IMAGE_TYPE_ELF64:
		sec = dlslot->e_cursec;

		/*
		 * We're pretty simplistic here.  We do only file-backed
		 * or only zero-fill.
		 */

		/* Determine offset into section. */
		secoff = dlslot->e_curpos - dlslot->e_sections[sec].s_loff;

		/*
		 * If we're in the file-backed part of the section,
		 * transmit some of the file.
		 */
		if (secoff < dlslot->e_sections[sec].s_fsize) {
			bsz = dlslot->e_sections[sec].s_fsize - secoff;
			if (bsz > dlslot->dl_bsz)
				bsz = dlslot->dl_bsz;
			if (lseek(dlslot->ldfd,
			    dlslot->e_sections[sec].s_foff + secoff,
			    SEEK_SET) == (off_t) -1)
				return (-1);
			len = read(dlslot->ldfd, buf, bsz);
		}
		/*
		 * Otherwise, if we're in the zero-fill part of the
		 * section, transmit some zeros.
		 */
		else if (secoff < (dlslot->e_sections[sec].s_fsize +
				   dlslot->e_sections[sec].s_pad)) {
			bsz = dlslot->e_sections[sec].s_pad -
			    (secoff - dlslot->e_sections[sec].s_fsize);
			if (bsz > dlslot->dl_bsz)
				bsz = dlslot->dl_bsz;
			memset(buf, 0, (len = bsz));
		}
		/*
		 * ...and if we haven't hit either of those cases,
		 * that's the end of the image.
		 */
		else {
			return (0);
		}
		/*
		 * Advance the logical image pointer.
		 */
		dlslot->e_curpos += bsz;
		if (dlslot->e_curpos >= (dlslot->e_sections[sec].s_loff +
					 dlslot->e_sections[sec].s_fsize +
					 dlslot->e_sections[sec].s_pad))
			if (++sec != dlslot->e_nsec)
				dlslot->e_cursec = sec;
		break;

	case IMAGE_TYPE_AOUT:
		bsz = dlslot->dl_bsz;
		pos = dlslot->a_lseek;
		len = 0;

		total = dlslot->a_text;

		if (pos < total) {
			notdone = total - pos;
			if (notdone <= bsz) {
				outlen = read(dlslot->ldfd,&buf[len],notdone);
			} else {
				outlen = read(dlslot->ldfd,&buf[len],bsz);
			}
			len = len + outlen;
			pos = pos + outlen;
			bsz = bsz - outlen;
		}

		total = total + dlslot->a_text_fill;

		if ((bsz > 0) && (pos < total)) {
			notdone = total - pos;
			if (notdone <= bsz) {
				outlen = notdone;
			} else {
				outlen = bsz;
			}
			memset(&buf[len], 0, outlen);
			len = len + outlen;
			pos = pos + outlen;
			bsz = bsz - outlen;
		}

		total = total + dlslot->a_data;

		if ((bsz > 0) && (pos < total)) {
			notdone = total - pos;
			if (notdone <= bsz) {
				outlen = read(dlslot->ldfd,&buf[len],notdone);
			} else {
				outlen = read(dlslot->ldfd,&buf[len],bsz);
			}
			len = len + outlen;
			pos = pos + outlen;
			bsz = bsz - outlen;
		}

		total = total + dlslot->a_data_fill;

		if ((bsz > 0) && (pos < total)) {
			notdone = total - pos;
			if (notdone <= bsz) {
				outlen = notdone;
			} else {
				outlen = bsz;
			}
			memset(&buf[len], 0, outlen);
			len = len + outlen;
			pos = pos + outlen;
			bsz = bsz - outlen;
		}

		total = total + dlslot->a_bss;

		if ((bsz > 0) && (pos < total)) {
			notdone = total - pos;
			if (notdone <= bsz) {
				outlen = notdone;
			} else {
				outlen = bsz;
			}
			memset(&buf[len], 0, outlen);
			len = len + outlen;
			pos = pos + outlen;
			bsz = bsz - outlen;
		}

		total = total + dlslot->a_bss_fill;

		if ((bsz > 0) && (pos < total)) {
			notdone = total - pos;
			if (notdone <= bsz) {
				outlen = notdone;
			} else {
				outlen = bsz;
			}
			memset(&buf[len], 0, outlen);
			len = len + outlen;
			pos = pos + outlen;
			bsz = bsz - outlen;
		}

		dlslot->a_lseek = pos;
		break;

	default:
		abort();
	}

	return(len);
}
