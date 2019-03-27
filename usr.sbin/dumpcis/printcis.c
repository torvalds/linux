/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Andrew McRae.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* 
 * Code cleanup, bug-fix and extension
 * by Tatsumi Hosokawa <hosokawa@mt.cs.keio.ac.jp>                   
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "cis.h"
#include "readcis.h"

static void   dump_config_map(struct tuple *tp);
static void   dump_cis_config(struct tuple *tp);
static void   dump_other_cond(u_char *p, int len);
static void   dump_device_desc(u_char *p, int len, const char *type);
static void   dump_info_v1(u_char *p, int len);
static void   dump_longlink_mfc(u_char *p, int len);
static void   dump_bar(u_char *p, int len);
static void   dump_device_geo(u_char *p, int len);
static void   dump_func_id(u_char *p);
static void   dump_serial_ext(u_char *p, int len);
static void   dump_disk_ext(u_char *p, int len);
static void   dump_network_ext(u_char *p, int len);
static void   dump_info_v2(u_char *p, int len);
static void   dump_org(u_char *p, int len);

void
dumpcis(struct tuple_list *tlist)
{
	struct tuple *tp;
	struct tuple_list *tl;
	int     count = 0, sz, ad, i;
	u_char *p;
	int func = 0;

	for (tl = tlist; tl; tl = tl->next)
		for (tp = tl->tuples; tp; tp = tp->next) {
			printf("Tuple #%d, code = 0x%x (%s), length = %d\n",
			    ++count, tp->code, tuple_name(tp->code), tp->length);
			p = tp->data;
			sz = tp->length;
			ad = 0;
			while (sz > 0) {
				printf("    %03x: ", ad);
				for (i = 0; i < ((sz < 16) ? sz : 16); i++)
					printf(" %02x", p[i]);
				printf("\n");
				sz -= 16;
				p += 16;
				ad += 16;
			}
			switch (tp->code) {
			default:
				break;
			case CIS_MEM_COMMON:	/* 0x01 */
				dump_device_desc(tp->data, tp->length, "Common");
				break;
			case CIS_CONF_MAP_CB:	/* 0x04 */
				dump_config_map(tp);
				break;
			case CIS_CONFIG_CB:	/* 0x05 */
				dump_cis_config(tp);
				break;
			case CIS_LONGLINK_MFC:	/* 0x06 */
				dump_longlink_mfc(tp->data, tp->length);
				break;
			case CIS_BAR:		/* 0x07 */
				dump_bar(tp->data, tp->length);
				break;
			case CIS_CHECKSUM:	/* 0x10 */
				printf("\tChecksum from offset %d, length %d, value is 0x%x\n",
				       tpl16(tp->data),
				       tpl16(tp->data + 2),
				       tp->data[4]);
				break;
			case CIS_LONGLINK_A:	/* 0x11 */
				printf("\tLong link to attribute memory, address 0x%x\n",
				       tpl32(tp->data));
				break;
			case CIS_LONGLINK_C:	/* 0x12 */
				printf("\tLong link to common memory, address 0x%x\n",
				       tpl32(tp->data));
				break;	
			case CIS_INFO_V1:	/* 0x15 */
				dump_info_v1(tp->data, tp->length);
				break;
			case CIS_ALTSTR:	/* 0x16 */
				break;
			case CIS_MEM_ATTR:	/* 0x17 */
				dump_device_desc(tp->data, tp->length, "Attribute");
				break;
			case CIS_JEDEC_C:	/* 0x18 */
			case CIS_JEDEC_A:	/* 0x19 */
				break;
			case CIS_CONF_MAP:	/* 0x1A */
				dump_config_map(tp);
				break;
			case CIS_CONFIG:	/* 0x1B */
				dump_cis_config(tp);
				break;
			case CIS_DEVICE_OC:	/* 0x1C */
			case CIS_DEVICE_OA:	/* 0x1D */
				dump_other_cond(tp->data, tp->length);
				break;
			case CIS_DEVICEGEO:	/* 0x1E */
			case CIS_DEVICEGEO_A:	/* 0x1F */
				dump_device_geo(tp->data, tp->length);
				break;
			case CIS_MANUF_ID:	/* 0x20 */
				printf("\tPCMCIA ID = 0x%x, OEM ID = 0x%x\n",
				       tpl16(tp->data),
				       tpl16(tp->data + 2));
				break;
			case CIS_FUNC_ID:	/* 0x21 */
				func = tp->data[0];
				dump_func_id(tp->data);
				break;
			case CIS_FUNC_EXT:	/* 0x22 */
				switch (func) {
				case 2:
					dump_serial_ext(tp->data, tp->length);
					break;
				case 4:
					dump_disk_ext(tp->data, tp->length);
					break;
				case 6:
					dump_network_ext(tp->data, tp->length);
					break;
				}
				break;
			case CIS_VERS_2:	/* 0x40 */
				dump_info_v2(tp->data, tp->length);
				break;
			case CIS_ORG:		/* 0x46 */
				dump_org(tp->data, tp->length);
				break;
			}
		}
}

/*
 *	CIS_CONF_MAP   : Dump configuration map tuple.
 *	CIS_CONF_MAP_CB: Dump configuration map for CardBus
 */
static void
dump_config_map(struct tuple *tp)
{
	u_char *p = tp->data, x;
	unsigned int rlen, mlen = 0, i;

	rlen = (p[0] & 3) + 1;
	if (tp->code == CIS_CONF_MAP)
		mlen = ((p[0] >> 2) & 3) + 1;
	if (tp->length < rlen + mlen + 2) {
		printf("\tWrong length for configuration map tuple\n");
		return;
	}
	printf("\tReg len = %d, config register addr = 0x%x, last config = 0x%x\n",
	       rlen, parse_num(rlen | 0x10, p + 2, &p, 0), p[1]);
	if (mlen) {
		printf("\tRegisters: ");
		for (i = 0; i < mlen; i++, p++) {
			for (x = 0x1; x; x <<= 1)
				printf("%c", x & *p ? 'X' : '-');
			putchar(' ');
		}
	}
	i = tp->length - (rlen + mlen + 2);
	if (i) {
		if (!mlen)
			putchar('\t');
		printf("%d bytes in subtuples", i);
	}
	if (mlen || i)
		putchar('\n');
}

/*
 *	Dump power descriptor.
 *	call from dump_cis_config()
 */
static int
print_pwr_desc(u_char *p)
{
	int     len = 1, i;
	u_char mask;
	const char  **expp;
	static const char *pname[] =
	{"Nominal operating supply voltage",
	 "Minimum operating supply voltage",
	 "Maximum operating supply voltage",
	 "Continuous supply current",
	 "Max current average over 1 second",
	 "Max current average over 10 ms",
	 "Power down supply current",
	 "Reserved"
	};
	static const char *vexp[] =
	{"10uV", "100uV", "1mV", "10mV", "100mV", "1V", "10V", "100V"};
	static const char *cexp[] =
	{"10nA", "1uA", "10uA", "100uA", "1mA", "10mA", "100mA", "1A"};
	static const char *mant[] =
	{"1", "1.2", "1.3", "1.5", "2", "2.5", "3", "3.5", "4", "4.5",
	"5", "5.5", "6", "7", "8", "9"};

	mask = *p++;
	expp = vexp;
	for (i = 0; i < 8; i++)
		if (mask & (1 << i)) {
			len++;
			if (i >= 3)
				expp = cexp;
			printf("\t\t%s: ", pname[i]);
			printf("%s x %s",
			    mant[(*p >> 3) & 0xF],
			    expp[*p & 7]);
			while (*p & 0x80) {
				len++;
				p++;
				printf(", ext = 0x%x", *p);
			}
			printf("\n");
			p++;
		}
	return (len);
}

/*
 *	print_ext_speed - Print extended speed.
 *	call from dump_cis_config(), dump_device_desc()
 */
static void
print_ext_speed(u_char x, int scale)
{
	static const char *mant[] =
	{"Reserved", "1.0", "1.2", "1.3", "1.5", "2.0", "2.5", "3.0",
	"3.5", "4.0", "4.5", "5.0", "5.5", "6.0", "7.0", "8.0"};
	static const char *exp[] =
	{"1 ns", "10 ns", "100 ns", "1 us", "10 us", "100 us",
	"1 ms", "10 ms"};
	static const char *scale_name[] =
	{"None", "10", "100", "1,000", "10,000", "100,000",
	"1,000,000", "10,000,000"};

	printf("Speed = %s x %s", mant[(x >> 3) & 0xF], exp[x & 7]);
	if (scale)
		printf(", scaled by %s", scale_name[scale & 7]);
}

/*
 *	Print variable length value.
 *	call from print_io_map(), print_mem_map()
 */
static int
print_num(int sz, const char *fmt, u_char *p, int ofs)
{
	switch (sz) {
	case 0:
	case 0x10:
		return 0;
	case 1:
	case 0x11:
		printf(fmt, *p + ofs);
		return 1;
	case 2:
	case 0x12:
		printf(fmt, tpl16(p) + ofs);
		return 2;
	case 0x13:
		printf(fmt, tpl24(p) + ofs);
		return 3;
	case 3:
	case 0x14:
		printf(fmt, tpl32(p) + ofs);
		return 4;
	}
	errx(1, "print_num(0x%x): Illegal arguments", sz);
/*NOTREACHED*/
}

/*
 *	Print I/O mapping sub-tuple.
 *	call from dump_cis_config()
 */
static u_char *
print_io_map(u_char *p, u_char *q)
{
	int i, j;
	u_char c;

	if (q <= p)
		goto err;
	if (CIS_IO_ADDR(*p))	/* I/O address line */
		printf("\tCard decodes %d address lines",
			CIS_IO_ADDR(*p));
	else
		printf("\tCard provides address decode");

	/* 8/16 bit I/O */
	switch (*p & (CIS_IO_8BIT | CIS_IO_16BIT)) {
	case CIS_IO_8BIT:
		printf(", 8 Bit I/O only");
		break;
	case CIS_IO_16BIT:
		printf(", limited 8/16 Bit I/O");
		break;
	case (CIS_IO_8BIT | CIS_IO_16BIT):
		printf(", full 8/16 Bit I/O");
		break;
	}
	putchar('\n');

	/* I/O block sub-tuple exist */
	if (*p++ & CIS_IO_RANGE) {
		if (q <= p)
			goto err;
		c = *p++;
		/* calculate byte length */
		j = CIS_IO_ADSZ(c) + CIS_IO_BLKSZ(c);
		if (CIS_IO_ADSZ(c) == 3)
			j++;
		if (CIS_IO_BLKSZ(c) == 3)
			j++;
		/* number of I/O block sub-tuples */
		for (i = 0; i <= CIS_IO_BLKS(c); i++) {
			if (q - p < j)
				goto err;
			printf("\t\tI/O address # %d: ", i + 1);
			/* start block address */
			p += print_num(CIS_IO_ADSZ(c),
				       "block start = 0x%x", p, 0);
			/* block size */
			p += print_num(CIS_IO_BLKSZ(c),
				       " block length = 0x%x", p, 1);
			putchar('\n');
		}
	}
	return p;

 err:	/* warning */
	printf("\tWrong length for I/O mapping sub-tuple\n");
	return p;
}

/*
 *	Print IRQ sub-tuple.
 *	call from dump_cis_config()
 */
static u_char *
print_irq_map(u_char *p, u_char *q)
{
	int i, j;
	u_char c;

	if (q <= p)
		goto err;
	printf("\t\tIRQ modes:");
	c = ' ';
	if (*p & CIS_IRQ_LEVEL) { /* Level triggered interrupts */
		printf(" Level");
		c = ',';
	}
	if (*p & CIS_IRQ_PULSE) { /* Pulse triggered requests */
		printf("%c Pulse", c);
		c = ',';
	}
	if (*p & CIS_IRQ_SHARING) /* Interrupt sharing */
		printf("%c Shared", c);
	putchar('\n');

	/* IRQ mask values exist */
	if (*p & CIS_IRQ_MASK) {
		if (q - p < 3)
			goto err;
		i = tpl16(p + 1); /* IRQ mask */
		printf("\t\tIRQs: ");
		if (*p & 1)
			printf(" NMI");
		if (*p & 0x2)
			printf(" IOCK");
		if (*p & 0x4)
			printf(" BERR");
		if (*p & 0x8)
			printf(" VEND");
		for (j = 0; j < 16; j++)
			if (i & (1 << j))
				printf(" %d", j);
		putchar('\n');
		p += 3;
	} else {
		printf("\t\tIRQ level = %d\n", CIS_IRQ_IRQN(*p));
		p++;
	}
	return p;

 err:	/* warning */
	printf("\tWrong length for IRQ sub-tuple\n");
	return p;
}

/*
 *	Print memory map sub-tuple.
 *	call from dump_cis_config()
 */
static u_char *
print_mem_map(u_char feat, u_char *p, u_char *q)
{
	int i, j;
	u_char c;

	switch (CIS_FEAT_MEMORY(feat)) {

	case CIS_FEAT_MEM_NONE:	/* No memory block */
		break;
	case CIS_FEAT_MEM_LEN:	/* Specify memory length */
		if (q - p < 2)
			goto err;
		printf("\tMemory space length = 0x%x\n", tpl16(p));
		p += 2;
		break;
	case CIS_FEAT_MEM_ADDR:	/* Memory address and length */
		if (q - p < 4)
			goto err;
		printf("\tMemory space address = 0x%x, length = 0x%x\n",
		       tpl16(p + 2), tpl16(p));
		p += 4;
		break;
	case CIS_FEAT_MEM_WIN:	/* Memory descriptors. */
		if (q <= p)
			goto err;
		c = *p++;
		/* calculate byte length */
		j = CIS_MEM_LENSZ(c) + CIS_MEM_ADDRSZ(c);
		if (c & CIS_MEM_HOST)
			j += CIS_MEM_ADDRSZ(c);
		/* number of memory block */
		for (i = 0; i < CIS_MEM_WINS(c); i++) {
			if (q - p < j)
				goto err;
			printf("\tMemory descriptor %d\n\t\t", i + 1);
			/* memory length */
			p += print_num(CIS_MEM_LENSZ(c) | 0x10,
				       " blk length = 0x%x00", p, 0);
			/* card address */
			p += print_num(CIS_MEM_ADDRSZ(c) | 0x10,
				       " card addr = 0x%x00", p, 0);
			if (c & CIS_MEM_HOST) /* Host address value exist */
				p += print_num(CIS_MEM_ADDRSZ(c) | 0x10,
					       " host addr = 0x%x00", p, 0);
			putchar('\n');
		}
		break;
	}
	return p;

 err:	/* warning */
	printf("\tWrong length for memory mapping sub-tuple\n");
	return p;
}

/*
 *	CIS_CONFIG   : Dump a config entry.
 *	CIS_CONFIG_CB: Dump a configuration entry for CardBus
 */
static void
dump_cis_config(struct tuple *tp)
{
	u_char *p, *q, feat;
	int     i, j;
	char    c;

	p = tp->data;
	q = p + tp->length;
	printf("\tConfig index = 0x%x%s\n", *p & 0x3F,
	       *p & 0x40 ? "(default)" : "");

	/* Interface byte exists */
	if (tp->code == CIS_CONFIG && (*p & 0x80)) {
		p++;
		printf("\tInterface byte = 0x%x ", *p);
		switch (*p & 0xF) { /* Interface type */
		default:
			printf("(reserved)");
			break;
		case 0:
			printf("(memory)");
			break;
		case 1:
			printf("(I/O)");
			break;
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			printf("(custom)");
			break;
		}
		c = ' ';
		if (*p & 0x10) { /* Battery voltage detect */
			printf(" BVD1/2 active");
			c = ',';
		}
		if (*p & 0x20) { /* Write protect active */
			printf("%c card WP active", c);	/* Write protect */
			c = ',';
		}
		if (*p & 0x40) { /* RdyBsy active bit */
			printf("%c +RDY/-BSY active", c);
			c = ',';
		}
		if (*p & 0x80)	/* Wait signal required */
			printf("%c wait signal supported", c);
		printf("\n");
	}

	/* features byte */
	p++;
	feat = *p++;

	/* Power structure sub-tuple */
	switch (CIS_FEAT_POWER(feat)) {	/* Power sub-tuple(s) exists */
	case 0:
		break;
	case 1:
		printf("\tVcc pwr:\n");
		p += print_pwr_desc(p);
		break;
	case 2:
		printf("\tVcc pwr:\n");
		p += print_pwr_desc(p);
		printf("\tVpp pwr:\n");
		p += print_pwr_desc(p);
		break;
	case 3:
		printf("\tVcc pwr:\n");
		p += print_pwr_desc(p);
		printf("\tVpp1 pwr:\n");
		p += print_pwr_desc(p);
		printf("\tVpp2 pwr:\n");
		p += print_pwr_desc(p);
		break;
	}

	/* Timing sub-tuple */
	if (tp->code == CIS_CONFIG &&
	    (feat & CIS_FEAT_TIMING)) {	/* Timing sub-tuple exists */
		i = *p++;
		j = CIS_WAIT_SCALE(i);
		if (j != 3) {
			printf("\tWait scale ");
			print_ext_speed(*p++, j);
			printf("\n");
		}
		j = CIS_READY_SCALE(i);
		if (j != 7) {
			printf("\tRDY/BSY scale ");
			print_ext_speed(*p++, j);
			printf("\n");
		}
		j = CIS_RESERVED_SCALE(i);
		if (j != 7) {
			printf("\tExternal scale ");
			print_ext_speed(*p++, j);
			printf("\n");
		}
	}

	/* I/O mapping sub-tuple */
	if (feat & CIS_FEAT_I_O) { /* I/O space sub-tuple exists */
		if (tp->code == CIS_CONFIG)
			p = print_io_map(p, q);
		else {		/* CIS_CONFIG_CB */
			printf("\tI/O base:");
			for (i = 0; i < 8; i++)
				if (*p & (1 << i))
					printf(" %d", i);
			putchar('\n');
			p++;
		}
	}

	/* IRQ descriptor sub-tuple */
	if (feat & CIS_FEAT_IRQ) /* IRQ sub-tuple exists */
		p = print_irq_map(p, q);

	/* Memory map sub-tuple */
	if (CIS_FEAT_MEMORY(feat)) { /* Memory space sub-tuple(s) exists */
		if (tp->code == CIS_CONFIG)
			p = print_mem_map(feat, p, q);
		else {		/* CIS_CONFIG_CB */
			printf("\tMemory base:");
			for (i = 0; i < 8; i++)
				if (*p & (1 << i))
					printf(" %d", i);
			putchar('\n');
			p++;
		}
	}

	/* Misc sub-tuple */
	if (feat & CIS_FEAT_MISC) { /* Miscellaneous sub-tuple exists */
		if (tp->code == CIS_CONFIG) {
			printf("\tMax twin cards = %d\n", *p & 7);
			printf("\tMisc attr:%s%s%s",
			       (*p & 8) ? " (Audio-BVD2)" : "",
			       (*p & 0x10) ? " (Read-only)" : "",
			       (*p & 0x20) ? " (Power down supported)" : "");
			if (*p++ & 0x80) {
				printf(" (Ext byte = 0x%x)", *p);
				p++;
			}
			putchar('\n');
		}
		else {		/* CIS_CONFIG_CB */
			printf("\tMisc attr:");
			printf("%s%s%s%s%s%s%s",
			       (*p & 1) ? " (Master)" : "",
			       (*p & 2) ? " (Invalidate)" : "",
			       (*p & 4) ? " (VGA palette)" : "",
			       (*p & 8) ? " (Parity)" : "",
			       (*p & 0x10) ? " (Wait)" : "",
			       (*p & 0x20) ? " (Serr)" : "",
			       (*p & 0x40) ? " (Fast back)" : "");
			if (*p++ & 0x80) {
				printf("%s%s",
				       (*p & 1) ? " (Binary audio)" : "",
				       (*p & 2) ? " (pwm audio)" : "");
				p++;
			}
			putchar('\n');
		}
	}
}

/*
 *	CIS_DEVICE_OC, CIS_DEVICE_OA:
 *		Dump other conditions for common/attribute memory
 */
static void
dump_other_cond(u_char *p, int len)
{
	if (p[0] && len > 0) {
		printf("\t");
		if (p[0] & 1)
			printf("(MWAIT)");
		if (p[0] & 2)
			printf(" (3V card)");
		if (p[0] & 0x80)
			printf(" (Extension bytes follow)");
		printf("\n");
	}
}

/*
 *	CIS_MEM_COMMON, CIS_MEM_ATTR:
 *		Common / Attribute memory descripter
 */
static void
dump_device_desc(u_char *p, int len, const char *type)
{
	static const char *un_name[] =
	{"512b", "2Kb", "8Kb", "32Kb", "128Kb", "512Kb", "2Mb", "reserved"};
	static const char *speed[] =
	{"No speed", "250nS", "200nS", "150nS",
	"100nS", "Reserved", "Reserved"};
	static const char *dev[] =
	{"No device", "Mask ROM", "OTPROM", "UV EPROM",
	 "EEPROM", "FLASH EEPROM", "SRAM", "DRAM",
	 "Reserved", "Reserved", "Reserved", "Reserved",
	 "Reserved", "Function specific", "Extended",
	"Reserved"};
	int     count = 0;

	while (*p != 0xFF && len > 0) {
		u_char x;

		x = *p++;
		len -= 2;
		if (count++ == 0)
			printf("\t%s memory device information:\n", type);
		printf("\t\tDevice number %d, type %s, WPS = %s\n",
		    count, dev[x >> 4], (x & 0x8) ? "ON" : "OFF");
		if ((x & 7) == 7) {
			len--;
			if (*p) {
				printf("\t\t");
				print_ext_speed(*p, 0);
				while (*p & 0x80) {
					p++;
					len--;
				}
			}
			p++;
		} else
			printf("\t\tSpeed = %s", speed[x & 7]);
		printf(", Memory block size = %s, %d units\n",
		    un_name[*p & 7], (*p >> 3) + 1);
		p++;
	}
}

/*
 *	CIS_INFO_V1: Print version-1 info
 */
static void
dump_info_v1(u_char *p, int len)
{
	if (len < 2) {
		printf("\tWrong length for version-1 info tuple\n");
		return;
	}
	printf("\tVersion = %d.%d", p[0], p[1]);
	p += 2;
	len -= 2;
	if (len > 1 && *p != 0xff) {
		printf(", Manuf = [%s]", p);
		while (*p++ && --len > 0);
	}
	if (len > 1 && *p != 0xff) {
		printf(", card vers = [%s]", p);
		while (*p++ && --len > 0);
	} else {
		printf("\n\tWrong length for version-1 info tuple\n");
		return;
	}
	putchar('\n');
	if (len > 1 && *p != 0xff) {
		printf("\tAddit. info = [%.*s]", len, p);
		while (*p++ && --len > 0);
		if (len > 1 && *p != 0xff)
			printf(",[%.*s]", len, p);
		putchar('\n');
	}
}

/*
 *	CIS_FUNC_ID: Functional ID
 */
static void
dump_func_id(u_char *p)
{
	static const char *id[] = {
		"Multifunction card",
		"Memory card",
		"Serial port/modem",
		"Parallel port",
		"Fixed disk card",
		"Video adapter",
		"Network/LAN adapter",
		"AIMS",
		"SCSI card",
		"Security"
	};

	printf("\t%s%s%s\n",
	       (*p <= 9) ? id[*p] : "Unknown function",
	       (p[1] & 1) ? " - POST initialize" : "",
	       (p[1] & 2) ? " - Card has ROM" : "");
}

/*
 *	CIS_FUNC_EXT: Dump functional extension tuple.
 *		(Serial port/modem)
 */
static void
dump_serial_ext(u_char *p, int len)
{
	static const char *type[] = {
		"", "Modem", "Data", "Fax", "Voice", "Data modem",
		"Fax/modem", "Voice", " (Data)", " (Fax)", " (Voice)"
	};

	if (len < 1)
		return;
	switch (p[0]) {
	case 0:			/* Serial */
	case 8:			/* Data */
	case 9:			/* Fax */
	case 10:		/* Voice */
		printf("\tSerial interface extension:%s\n", type[*p]);
		if (len < 4)
			goto err;
		switch (p[1] & 0x1F) {
		default:
			printf("\t\tUnknown device");
			break;
		case 0:
			printf("\t\t8250 UART");
			break;
		case 1:
			printf("\t\t16450 UART");
			break;
		case 2:
			printf("\t\t16550 UART");
			break;
		}
		printf(", Parity - %s%s%s%s\n",
		       (p[2] & 1) ? "Space," : "",
		       (p[2] & 2) ? "Mark," : "",
		       (p[2] & 4) ? "Odd," : "",
		       (p[2] & 8) ? "Even" : "");
		printf("\t\tData bit - %s%s%s%s Stop bit - %s%s%s\n",
		       (p[3] & 1) ? "5bit," : "",
		       (p[3] & 2) ? "6bit," : "",
		       (p[3] & 4) ? "7bit," : "",
		       (p[3] & 8) ? "8bit," : "",
		       (p[3] & 0x10) ? "1bit," : "",
		       (p[3] & 0x20) ? "1.5bit," : "",
		       (p[3] & 0x40) ? "2bit" : "");
		break;
	case 1:			/* Serial */
	case 5:			/* Data */
	case 6:			/* Fax */
	case 7:			/* Voice */
		printf("\t%s interface capabilities:\n", type[*p]);
		if (len < 9)
			goto err;
		break;
	case 2:			/* Data */
		printf("\tData modem services available:\n");
		break;
	case 0x13:		/* Fax1 */
	case 0x23:		/* Fax2 */
	case 0x33:		/* Fax3 */
		printf("\tFax%d/modem services available:\n", *p >> 4);
		break;
	case 0x84:		/* Voice */
		printf("\tVoice services available:\n");
		break;
	err:	/* warning */
		printf("\tWrong length for serial extension tuple\n");
		return;
	}
}

/*
 *	CIS_FUNC_EXT: Dump functional extension tuple.
 *		(Fixed disk card)
 */
static void
dump_disk_ext(u_char *p, int len)
{
	if (len < 1)
		return;
	switch (p[0]) {
	case 1:			/* IDE interface */
		if (len < 2)
			goto err;
		printf("\tDisk interface: %s\n",
		       (p[1] & 1) ? "IDE" : "Undefined");
		break;
	case 2:			/* Master */
	case 3:			/* Slave */
		if (len < 3)
			goto err;
		printf("\tDisk features: %s, %s%s\n",
		       (p[1] & 0x04) ? "Silicon" : "Rotating",
		       (p[1] & 0x08) ? "Unique, " : "",
		       (p[1] & 0x10) ? "Dual" : "Single");
		if (p[2] & 0x7f)
			printf("\t\t%s%s%s%s%s%s%s\n",
			       (p[2] & 0x01) ? "Sleep, " : "",
			       (p[2] & 0x02) ? "Standby, " : "",
			       (p[2] & 0x04) ? "Idle, " : "",
			       (p[2] & 0x08) ? "Low power, " : "",
			       (p[2] & 0x10) ? "Reg inhibit, " : "",
			       (p[2] & 0x20) ? "Index, " : "",
			       (p[2] & 0x40) ? "Iois16" : "");
		break;
	err:	/* warning */
		printf("\tWrong length for fixed disk extension tuple\n");
		return;
	}
}

static void
print_speed(u_int i)
{
	if (i < 1000)
		printf("%u bits/sec", i);
	else if (i < 1000000)
		printf("%u kb/sec", i / 1000);
	else
		printf("%u Mb/sec", i / 1000000);
}

/*
 *	CIS_FUNC_EXT: Dump functional extension tuple.
 *		(Network/LAN adapter)
 */
static void
dump_network_ext(u_char *p, int len)
{
	static const char *tech[] = {
		"Undefined", "ARCnet", "Ethernet", "Token ring",
		"Localtalk", "FDDI/CDDI", "ATM", "Wireless"
	};
	static const char *media[] = {
		"Undefined", "UTP", "STP", "Thin coax",
		"THICK coax", "Fiber", "900 MHz", "2.4 GHz",
		"5.4 GHz", "Diffuse Infrared", "Point to point Infrared"
	};
	u_int i = 0;

	if (len < 1)
		return;
	switch (p[0]) {
	case 1:			/* Network technology */
		if (len < 2)
			goto err;
		printf("\tNetwork technology: %s\n", tech[p[1] & 7]);
		break;
	case 2:			/* Network speed */
		if (len < 5)
			goto err;
		printf("\tNetwork speed: ");
		print_speed(tpl32(p + 1));
		putchar('\n');
		break;
	case 3:			/* Network media */
		if (len < 2)
			goto err;
		if (p[1] <= 10)
			i = p[1];
		printf("\tNetwork media: %s\n", media[i]);
		break;
	case 4:			/* Node ID */
		if (len <= 2 || len < p[1] + 2)
			goto err;
		printf("\tNetwork node ID:");
		for (i = 0; i < p[1]; i++)
			printf(" %02x", p[i + 2]);
		putchar('\n');
		break;
	case 5:			/* Connector type */
		if (len < 2)
			goto err;
		printf("\tNetwork connector: %s connector standard\n",
		       (p[1] == 0) ? "open" : "closed");
		break;
	err:	/* warning */
		printf("\tWrong length for network extension tuple\n");
		return;
	}
}

/*
 *	CIS_LONGLINK_MFC: Long link to next chain for Multi function card
 */
static void
dump_longlink_mfc(u_char *p, int len)
{
	u_int i, n = *p++;

	--len;
	for (i = 0; i < n; i++) {
		if (len < 5) {
			printf("\tWrong length for long link MFC tuple\n");
			return;
		}
		printf("\tFunction %d: %s memory, address 0x%x\n",
		       i, (*p ? "common" : "attribute"), tpl32(p + 1));
		p += 5;
		len -= 5;
	}
}

/*
 *	CIS_DEVICEGEO, CIS_DEVICEGEO_A:
 *		Geometry info for common/attribute memory
 */
static void
dump_device_geo(u_char *p, int len)
{
	while (len >= 6) {
		printf("\twidth = %d, erase = 0x%x, read = 0x%x, write = 0x%x\n"
		       "\t\tpartition = 0x%x, interleave = 0x%x\n",
		       p[0], 1 << (p[1] - 1),
		       1 << (p[2] - 1), 1 << (p[3] - 1),
		       1 << (p[4] - 1), 1 << (p[5] - 1));
		len -= 6;
	}
}

/*
 *	CIS_INFO_V2: Print version-2 info
 */
static void
dump_info_v2(u_char *p, int len)
{
	if (len < 9) {
		printf("\tWrong length for version-2 info tuple\n");
		return;
	}
	printf("\tVersion = 0x%x, compliance = 0x%x, dindex = 0x%x\n",
	       p[0], p[1], tpl16(p + 2));
	printf("\tVspec8 = 0x%x, vspec9 = 0x%x, nhdr = %d\n",
	       p[6], p[7], p[8]);
	p += 9;
	len -= 9;
	if (len <= 1 || *p == 0xff)
		return;
	printf("\tVendor = [%.*s]", len, p);
	while (*p++ && --len > 0);
	if (len > 1 && *p != 0xff)
		printf(", info = [%.*s]", len, p);
	putchar('\n');
}

/*
 *	CIS_ORG: Organization
 */
static void
dump_org(u_char *p, int len)
{
	if (len < 1) {
		printf("\tWrong length for organization tuple\n");
		return;
	}
	switch (*p) {
	case 0:
		printf("\tFilesystem");
		break;
	case 1:
		printf("\tApp specific");
		break;
	case 2:
		printf("\tCode");
		break;
	default:
		if (*p < 0x80)
			printf("\tReserved");
		else
			printf("\tVendor specific");
		break;
	}
	printf(" [%.*s]\n", len - 1, p + 1);
}

static void
print_size(u_int i)
{
	if (i < 1024)
		printf("%ubits", i);
	else if (i < 1024*1024)
		printf("%ukb", i / 1024);
	else
		printf("%uMb", i / (1024*1024));
}

/*
 *	CIS_BAR: Base address register for CardBus
 */
static void
dump_bar(u_char *p, int len)
{
	if (len < 6) {
		printf("\tWrong length for BAR tuple\n");
		return;
	}
	printf("\tBAR %d: size = ", *p & 7);
	print_size(tpl32(p + 2));
	printf(", %s%s%s%s\n",
	       (*p & 0x10) ? "I/O" : "Memory",
	       (*p & 0x20) ? ", Prefetch" : "",
	       (*p & 0x40) ? ", Cacheable" : "",
	       (*p & 0x80) ? ", <1Mb" : "");
}
