/* sfi.h Simple Firmware Interface */

/*

  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY

  Copyright(c) 2009 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
  The full GNU General Public License is included in this distribution
  in the file called LICENSE.GPL.

  BSD LICENSE

  Copyright(c) 2009 Intel Corporation. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef _LINUX_SFI_H
#define _LINUX_SFI_H

#include <linux/init.h>
#include <linux/types.h>

/* Table signatures reserved by the SFI specification */
#define SFI_SIG_SYST		"SYST"
#define SFI_SIG_FREQ		"FREQ"
#define SFI_SIG_IDLE		"IDLE"
#define SFI_SIG_CPUS		"CPUS"
#define SFI_SIG_MTMR		"MTMR"
#define SFI_SIG_MRTC		"MRTC"
#define SFI_SIG_MMAP		"MMAP"
#define SFI_SIG_APIC		"APIC"
#define SFI_SIG_XSDT		"XSDT"
#define SFI_SIG_WAKE		"WAKE"
#define SFI_SIG_DEVS		"DEVS"
#define SFI_SIG_GPIO		"GPIO"

#define SFI_SIGNATURE_SIZE	4
#define SFI_OEM_ID_SIZE		6
#define SFI_OEM_TABLE_ID_SIZE	8

#define SFI_NAME_LEN		16

#define SFI_SYST_SEARCH_BEGIN		0x000E0000
#define SFI_SYST_SEARCH_END		0x000FFFFF

#define SFI_GET_NUM_ENTRIES(ptable, entry_type) \
	((ptable->header.len - sizeof(struct sfi_table_header)) / \
	(sizeof(entry_type)))
/*
 * Table structures must be byte-packed to match the SFI specification,
 * as they are provided by the BIOS.
 */
struct sfi_table_header {
	char	sig[SFI_SIGNATURE_SIZE];
	u32	len;
	u8	rev;
	u8	csum;
	char	oem_id[SFI_OEM_ID_SIZE];
	char	oem_table_id[SFI_OEM_TABLE_ID_SIZE];
} __packed;

struct sfi_table_simple {
	struct sfi_table_header		header;
	u64				pentry[1];
} __packed;

/* Comply with UEFI spec 2.1 */
struct sfi_mem_entry {
	u32	type;
	u64	phys_start;
	u64	virt_start;
	u64	pages;
	u64	attrib;
} __packed;

struct sfi_cpu_table_entry {
	u32	apic_id;
} __packed;

struct sfi_cstate_table_entry {
	u32	hint;		/* MWAIT hint */
	u32	latency;	/* latency in ms */
} __packed;

struct sfi_apic_table_entry {
	u64	phys_addr;	/* phy base addr for APIC reg */
} __packed;

struct sfi_freq_table_entry {
	u32	freq_mhz;	/* in MHZ */
	u32	latency;	/* transition latency in ms */
	u32	ctrl_val;	/* value to write to PERF_CTL */
} __packed;

struct sfi_wake_table_entry {
	u64	phys_addr;	/* pointer to where the wake vector locates */
} __packed;

struct sfi_timer_table_entry {
	u64	phys_addr;	/* phy base addr for the timer */
	u32	freq_hz;	/* in HZ */
	u32	irq;
} __packed;

struct sfi_rtc_table_entry {
	u64	phys_addr;	/* phy base addr for the RTC */
	u32	irq;
} __packed;

struct sfi_device_table_entry {
	u8	type;		/* bus type, I2C, SPI or ...*/
#define SFI_DEV_TYPE_SPI	0
#define SFI_DEV_TYPE_I2C	1
#define SFI_DEV_TYPE_UART	2
#define SFI_DEV_TYPE_HSI	3
#define SFI_DEV_TYPE_IPC	4
#define SFI_DEV_TYPE_SD		5

	u8	host_num;	/* attached to host 0, 1...*/
	u16	addr;
	u8	irq;
	u32	max_freq;
	char	name[SFI_NAME_LEN];
} __packed;

struct sfi_gpio_table_entry {
	char	controller_name[SFI_NAME_LEN];
	u16	pin_no;
	char	pin_name[SFI_NAME_LEN];
} __packed;

typedef int (*sfi_table_handler) (struct sfi_table_header *table);

#ifdef CONFIG_SFI
extern void __init sfi_init(void);
extern int __init sfi_platform_init(void);
extern void __init sfi_init_late(void);
extern int sfi_table_parse(char *signature, char *oem_id, char *oem_table_id,
				sfi_table_handler handler);

extern int sfi_disabled;
static inline void disable_sfi(void)
{
	sfi_disabled = 1;
}

#else /* !CONFIG_SFI */

static inline void sfi_init(void)
{
}

static inline void sfi_init_late(void)
{
}

#define sfi_disabled	0

static inline int sfi_table_parse(char *signature, char *oem_id,
					char *oem_table_id,
					sfi_table_handler handler)
{
	return -1;
}

#endif /* !CONFIG_SFI */

#endif /*_LINUX_SFI_H*/
