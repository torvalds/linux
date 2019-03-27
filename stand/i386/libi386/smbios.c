/*-
 * Copyright (c) 2005-2009 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
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

#include <stand.h>
#include <bootstrap.h>
#include <sys/endian.h>

#ifdef EFI
/* In EFI, we don't need PTOV(). */
#define PTOV(x)		(caddr_t)(x)
#else
#include "btxv86.h"
#endif
#include "smbios.h"

/*
 * Detect SMBIOS and export information about the SMBIOS into the
 * environment.
 *
 * System Management BIOS Reference Specification, v2.6 Final
 * http://www.dmtf.org/standards/published_documents/DSP0134_2.6.0.pdf
 */

/*
 * 2.1.1 SMBIOS Structure Table Entry Point
 *
 * "On non-EFI systems, the SMBIOS Entry Point structure, described below, can
 * be located by application software by searching for the anchor-string on
 * paragraph (16-byte) boundaries within the physical memory address range
 * 000F0000h to 000FFFFFh. This entry point encapsulates an intermediate anchor
 * string that is used by some existing DMI browsers."
 */
#define	SMBIOS_START		0xf0000
#define	SMBIOS_LENGTH		0x10000
#define	SMBIOS_STEP		0x10
#define	SMBIOS_SIG		"_SM_"
#define	SMBIOS_DMI_SIG		"_DMI_"

#define	SMBIOS_GET8(base, off)	(*(uint8_t *)((base) + (off)))
#define	SMBIOS_GET16(base, off)	(*(uint16_t *)((base) + (off)))
#define	SMBIOS_GET32(base, off)	(*(uint32_t *)((base) + (off)))

#define	SMBIOS_GETLEN(base)	SMBIOS_GET8(base, 0x01)
#define	SMBIOS_GETSTR(base)	((base) + SMBIOS_GETLEN(base))

struct smbios_attr {
	int		probed;
	caddr_t 	addr;
	size_t		length;
	size_t		count;
	int		major;
	int		minor;
	int		ver;
	const char*	bios_vendor;
	const char*	maker;
	const char*	product;
	uint32_t	enabled_memory;
	uint32_t	old_enabled_memory;
	uint8_t		enabled_sockets;
	uint8_t		populated_sockets;
};

static struct smbios_attr smbios;

static uint8_t
smbios_checksum(const caddr_t addr, const uint8_t len)
{
	uint8_t		sum;
	int		i;

	for (sum = 0, i = 0; i < len; i++)
		sum += SMBIOS_GET8(addr, i);
	return (sum);
}

static caddr_t
smbios_sigsearch(const caddr_t addr, const uint32_t len)
{
	caddr_t		cp;

	/* Search on 16-byte boundaries. */
	for (cp = addr; cp < addr + len; cp += SMBIOS_STEP)
		if (strncmp(cp, SMBIOS_SIG, 4) == 0 &&
		    smbios_checksum(cp, SMBIOS_GET8(cp, 0x05)) == 0 &&
		    strncmp(cp + 0x10, SMBIOS_DMI_SIG, 5) == 0 &&
		    smbios_checksum(cp + 0x10, 0x0f) == 0)
			return (cp);
	return (NULL);
}

static const char*
smbios_getstring(caddr_t addr, const int offset)
{
	caddr_t		cp;
	int		i, idx;

	idx = SMBIOS_GET8(addr, offset);
	if (idx != 0) {
		cp = SMBIOS_GETSTR(addr);
		for (i = 1; i < idx; i++)
			cp += strlen(cp) + 1;
		return cp;
	}
	return (NULL);
}

static void
smbios_setenv(const char *name, caddr_t addr, const int offset)
{
	const char*	val;

	val = smbios_getstring(addr, offset);
	if (val != NULL)
		setenv(name, val, 1);
}

#ifdef SMBIOS_SERIAL_NUMBERS

#define	UUID_SIZE		16
#define	UUID_TYPE		uint32_t
#define	UUID_STEP		sizeof(UUID_TYPE)
#define	UUID_ALL_BITS		(UUID_SIZE / UUID_STEP)
#define	UUID_GET(base, off)	(*(UUID_TYPE *)((base) + (off)))

static void
smbios_setuuid(const char *name, const caddr_t addr, const int ver)
{
	char		uuid[37];
	int		byteorder, i, ones, zeros;
	UUID_TYPE	n;
	uint32_t	f1;
	uint16_t	f2, f3;

	for (i = 0, ones = 0, zeros = 0; i < UUID_SIZE; i += UUID_STEP) {
		n = UUID_GET(addr, i) + 1;
		if (zeros == 0 && n == 0)
			ones++;
		else if (ones == 0 && n == 1)
			zeros++;
		else
			break;
	}

	if (ones != UUID_ALL_BITS && zeros != UUID_ALL_BITS) {
		/*
		 * 3.3.2.1 System UUID
		 *
		 * "Although RFC 4122 recommends network byte order for all
		 * fields, the PC industry (including the ACPI, UEFI, and
		 * Microsoft specifications) has consistently used
		 * little-endian byte encoding for the first three fields:
		 * time_low, time_mid, time_hi_and_version. The same encoding,
		 * also known as wire format, should also be used for the
		 * SMBIOS representation of the UUID."
		 *
		 * Note: We use network byte order for backward compatibility
		 * unless SMBIOS version is 2.6+ or little-endian is forced.
		 */
#if defined(SMBIOS_LITTLE_ENDIAN_UUID)
		byteorder = LITTLE_ENDIAN;
#elif defined(SMBIOS_NETWORK_ENDIAN_UUID)
		byteorder = BIG_ENDIAN;
#else
		byteorder = ver < 0x0206 ? BIG_ENDIAN : LITTLE_ENDIAN;
#endif
		if (byteorder != LITTLE_ENDIAN) {
			f1 = ntohl(SMBIOS_GET32(addr, 0));
			f2 = ntohs(SMBIOS_GET16(addr, 4));
			f3 = ntohs(SMBIOS_GET16(addr, 6));
		} else {
			f1 = le32toh(SMBIOS_GET32(addr, 0));
			f2 = le16toh(SMBIOS_GET16(addr, 4));
			f3 = le16toh(SMBIOS_GET16(addr, 6));
		}
		sprintf(uuid,
		    "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		    f1, f2, f3, SMBIOS_GET8(addr, 8), SMBIOS_GET8(addr, 9),
		    SMBIOS_GET8(addr, 10), SMBIOS_GET8(addr, 11),
		    SMBIOS_GET8(addr, 12), SMBIOS_GET8(addr, 13),
		    SMBIOS_GET8(addr, 14), SMBIOS_GET8(addr, 15));
		setenv(name, uuid, 1);
	}
}

#undef UUID_SIZE
#undef UUID_TYPE
#undef UUID_STEP
#undef UUID_ALL_BITS
#undef UUID_GET

#endif

static caddr_t
smbios_parse_table(const caddr_t addr)
{
	caddr_t		cp;
	int		proc, size, osize, type;

	type = SMBIOS_GET8(addr, 0);	/* 3.1.2 Structure Header Format */
	switch(type) {
	case 0:		/* 3.3.1 BIOS Information (Type 0) */
		smbios_setenv("smbios.bios.vendor", addr, 0x04);
		smbios_setenv("smbios.bios.version", addr, 0x05);
		smbios_setenv("smbios.bios.reldate", addr, 0x08);
		break;

	case 1:		/* 3.3.2 System Information (Type 1) */
		smbios_setenv("smbios.system.maker", addr, 0x04);
		smbios_setenv("smbios.system.product", addr, 0x05);
		smbios_setenv("smbios.system.version", addr, 0x06);
#ifdef SMBIOS_SERIAL_NUMBERS
		smbios_setenv("smbios.system.serial", addr, 0x07);
		smbios_setuuid("smbios.system.uuid", addr + 0x08, smbios.ver);
#endif
		if (smbios.major > 2 ||
		    (smbios.major == 2 && smbios.minor >= 4)) {
			smbios_setenv("smbios.system.sku", addr, 0x19);
			smbios_setenv("smbios.system.family", addr, 0x1a);
		}
		break;

	case 2:		/* 3.3.3 Base Board (or Module) Information (Type 2) */
		smbios_setenv("smbios.planar.maker", addr, 0x04);
		smbios_setenv("smbios.planar.product", addr, 0x05);
		smbios_setenv("smbios.planar.version", addr, 0x06);
#ifdef SMBIOS_SERIAL_NUMBERS
		smbios_setenv("smbios.planar.serial", addr, 0x07);
		smbios_setenv("smbios.planar.tag", addr, 0x08);
#endif
		smbios_setenv("smbios.planar.location", addr, 0x0a);
		break;

	case 3:		/* 3.3.4 System Enclosure or Chassis (Type 3) */
		smbios_setenv("smbios.chassis.maker", addr, 0x04);
		smbios_setenv("smbios.chassis.version", addr, 0x06);
#ifdef SMBIOS_SERIAL_NUMBERS
		smbios_setenv("smbios.chassis.serial", addr, 0x07);
		smbios_setenv("smbios.chassis.tag", addr, 0x08);
#endif
		break;

	case 4:		/* 3.3.5 Processor Information (Type 4) */
		/*
		 * Offset 18h: Processor Status
		 *
		 * Bit 7	Reserved, must be 0
		 * Bit 6	CPU Socket Populated
		 *		1 - CPU Socket Populated
		 *		0 - CPU Socket Unpopulated
		 * Bit 5:3	Reserved, must be zero
		 * Bit 2:0	CPU Status
		 *		0h - Unknown
		 *		1h - CPU Enabled
		 *		2h - CPU Disabled by User via BIOS Setup
		 *		3h - CPU Disabled by BIOS (POST Error)
		 *		4h - CPU is Idle, waiting to be enabled
		 *		5-6h - Reserved
		 *		7h - Other
		 */
		proc = SMBIOS_GET8(addr, 0x18);
		if ((proc & 0x07) == 1)
			smbios.enabled_sockets++;
		if ((proc & 0x40) != 0)
			smbios.populated_sockets++;
		break;

	case 6:		/* 3.3.7 Memory Module Information (Type 6, Obsolete) */
		/*
		 * Offset 0Ah: Enabled Size
		 *
		 * Bit 7	Bank connection
		 *		1 - Double-bank connection
		 *		0 - Single-bank connection
		 * Bit 6:0	Size (n), where 2**n is the size in MB
		 *		7Dh - Not determinable (Installed Size only)
		 *		7Eh - Module is installed, but no memory
		 *		      has been enabled
		 *		7Fh - Not installed
		 */
		osize = SMBIOS_GET8(addr, 0x0a) & 0x7f;
		if (osize > 0 && osize < 22)
			smbios.old_enabled_memory += 1 << (osize + 10);
		break;

	case 17:	/* 3.3.18 Memory Device (Type 17) */
		/*
		 * Offset 0Ch: Size
		 *
		 * Bit 15	Granularity
		 *		1 - Value is in kilobytes units
		 *		0 - Value is in megabytes units
		 * Bit 14:0	Size
		 */
		size = SMBIOS_GET16(addr, 0x0c);
		if (size != 0 && size != 0xffff)
			smbios.enabled_memory += (size & 0x8000) != 0 ?
			    (size & 0x7fff) : (size << 10);
		break;

	default:	/* skip other types */
		break;
	}

	/* Find structure terminator. */
	cp = SMBIOS_GETSTR(addr);
	while (SMBIOS_GET16(cp, 0) != 0)
		cp++;

	return (cp + 2);
}

static caddr_t
smbios_find_struct(int type)
{
	caddr_t		dmi;
	size_t		i;

	if (smbios.addr == NULL)
		return (NULL);

	for (dmi = smbios.addr, i = 0;
	     dmi < smbios.addr + smbios.length && i < smbios.count; i++) {
		if (SMBIOS_GET8(dmi, 0) == type)
			return dmi;
		/* Find structure terminator. */
		dmi = SMBIOS_GETSTR(dmi);
		while (SMBIOS_GET16(dmi, 0) != 0)
			dmi++;
		dmi += 2;
	}

	return (NULL);
}

static void
smbios_probe(const caddr_t addr)
{
	caddr_t		saddr, info;
	uintptr_t	paddr;

	if (smbios.probed)
		return;
	smbios.probed = 1;

	/* Search signatures and validate checksums. */
	saddr = smbios_sigsearch(addr ? addr : PTOV(SMBIOS_START),
	    SMBIOS_LENGTH);
	if (saddr == NULL)
		return;

	smbios.length = SMBIOS_GET16(saddr, 0x16);	/* Structure Table Length */
	paddr = SMBIOS_GET32(saddr, 0x18);		/* Structure Table Address */
	smbios.count = SMBIOS_GET16(saddr, 0x1c);	/* No of SMBIOS Structures */
	smbios.ver = SMBIOS_GET8(saddr, 0x1e);		/* SMBIOS BCD Revision */

	if (smbios.ver != 0) {
		smbios.major = smbios.ver >> 4;
		smbios.minor = smbios.ver & 0x0f;
		if (smbios.major > 9 || smbios.minor > 9)
			smbios.ver = 0;
	}
	if (smbios.ver == 0) {
		smbios.major = SMBIOS_GET8(saddr, 0x06);/* SMBIOS Major Version */
		smbios.minor = SMBIOS_GET8(saddr, 0x07);/* SMBIOS Minor Version */
	}
	smbios.ver = (smbios.major << 8) | smbios.minor;
	smbios.addr = PTOV(paddr);

	/* Get system information from SMBIOS */
	info = smbios_find_struct(0x00);
	if (info != NULL) {
		smbios.bios_vendor = smbios_getstring(info, 0x04);
	}
	info = smbios_find_struct(0x01);
	if (info != NULL) {
		smbios.maker = smbios_getstring(info, 0x04);
		smbios.product = smbios_getstring(info, 0x05);
	}
}

void
smbios_detect(const caddr_t addr)
{
	char		buf[16];
	caddr_t		dmi;
	size_t		i;

	smbios_probe(addr);
	if (smbios.addr == NULL)
		return;

	for (dmi = smbios.addr, i = 0;
	     dmi < smbios.addr + smbios.length && i < smbios.count; i++)
		dmi = smbios_parse_table(dmi);

	sprintf(buf, "%d.%d", smbios.major, smbios.minor);
	setenv("smbios.version", buf, 1);
	if (smbios.enabled_memory > 0 || smbios.old_enabled_memory > 0) {
		sprintf(buf, "%u", smbios.enabled_memory > 0 ?
		    smbios.enabled_memory : smbios.old_enabled_memory);
		setenv("smbios.memory.enabled", buf, 1);
	}
	if (smbios.enabled_sockets > 0) {
		sprintf(buf, "%u", smbios.enabled_sockets);
		setenv("smbios.socket.enabled", buf, 1);
	}
	if (smbios.populated_sockets > 0) {
		sprintf(buf, "%u", smbios.populated_sockets);
		setenv("smbios.socket.populated", buf, 1);
	}
}

static int
smbios_match_str(const char* s1, const char* s2)
{
	return (s1 == NULL || (s2 != NULL && !strcmp(s1, s2)));
}

int
smbios_match(const char* bios_vendor, const char* maker,
    const char* product)
{
	/* XXXRP currently, only called from non-EFI. */
	smbios_probe(NULL);
	return (smbios_match_str(bios_vendor, smbios.bios_vendor) &&
	    smbios_match_str(maker, smbios.maker) &&
	    smbios_match_str(product, smbios.product));
}
