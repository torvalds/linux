/*
 * definition for store system information stsi
 *
 * Copyright IBM Corp. 2001,2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Ulrich Weigand <weigand@de.ibm.com>
 *		 Christian Borntraeger <borntraeger@de.ibm.com>
 */

struct sysinfo_1_1_1 {
	char reserved_0[32];
	char manufacturer[16];
	char type[4];
	char reserved_1[12];
	char model_capacity[16];
	char sequence[16];
	char plant[4];
	char model[16];
	char model_perm_cap[16];
	char model_temp_cap[16];
	char model_cap_rating[4];
	char model_perm_cap_rating[4];
	char model_temp_cap_rating[4];
};

struct sysinfo_1_2_1 {
	char reserved_0[80];
	char sequence[16];
	char plant[4];
	char reserved_1[2];
	unsigned short cpu_address;
};

struct sysinfo_1_2_2 {
	char format;
	char reserved_0[1];
	unsigned short acc_offset;
	char reserved_1[24];
	unsigned int secondary_capability;
	unsigned int capability;
	unsigned short cpus_total;
	unsigned short cpus_configured;
	unsigned short cpus_standby;
	unsigned short cpus_reserved;
	unsigned short adjustment[0];
};

struct sysinfo_1_2_2_extension {
	unsigned int alt_capability;
	unsigned short alt_adjustment[0];
};

struct sysinfo_2_2_1 {
	char reserved_0[80];
	char sequence[16];
	char plant[4];
	unsigned short cpu_id;
	unsigned short cpu_address;
};

struct sysinfo_2_2_2 {
	char reserved_0[32];
	unsigned short lpar_number;
	char reserved_1;
	unsigned char characteristics;
	unsigned short cpus_total;
	unsigned short cpus_configured;
	unsigned short cpus_standby;
	unsigned short cpus_reserved;
	char name[8];
	unsigned int caf;
	char reserved_2[16];
	unsigned short cpus_dedicated;
	unsigned short cpus_shared;
};

#define LPAR_CHAR_DEDICATED	(1 << 7)
#define LPAR_CHAR_SHARED	(1 << 6)
#define LPAR_CHAR_LIMITED	(1 << 5)

struct sysinfo_3_2_2 {
	char reserved_0[31];
	unsigned char count;
	struct {
		char reserved_0[4];
		unsigned short cpus_total;
		unsigned short cpus_configured;
		unsigned short cpus_standby;
		unsigned short cpus_reserved;
		char name[8];
		unsigned int caf;
		char cpi[16];
		char reserved_1[24];

	} vm[8];
};

static inline int stsi(void *sysinfo, int fc, int sel1, int sel2)
{
	register int r0 asm("0") = (fc << 28) | sel1;
	register int r1 asm("1") = sel2;

	asm volatile(
		"   stsi 0(%2)\n"
		"0: jz   2f\n"
		"1: lhi  %0,%3\n"
		"2:\n"
		EX_TABLE(0b, 1b)
		: "+d" (r0) : "d" (r1), "a" (sysinfo), "K" (-ENOSYS)
		: "cc", "memory");
	return r0;
}
