/******************************************************************************
 * platform.h
 *
 * Hardware platform operations. Intended for use by domain-0 kernel.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2002-2006, K Fraser
 */

#ifndef __XEN_PUBLIC_PLATFORM_H__
#define __XEN_PUBLIC_PLATFORM_H__

#include <xen/interface/xen.h>

#define XENPF_INTERFACE_VERSION 0x03000001

/*
 * Set clock such that it would read <secs,nsecs> after 00:00:00 UTC,
 * 1 January, 1970 if the current system time was <system_time>.
 */
#define XENPF_settime             17
struct xenpf_settime {
	/* IN variables. */
	uint32_t secs;
	uint32_t nsecs;
	uint64_t system_time;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_settime_t);

/*
 * Request memory range (@mfn, @mfn+@nr_mfns-1) to have type @type.
 * On x86, @type is an architecture-defined MTRR memory type.
 * On success, returns the MTRR that was used (@reg) and a handle that can
 * be passed to XENPF_DEL_MEMTYPE to accurately tear down the new setting.
 * (x86-specific).
 */
#define XENPF_add_memtype         31
struct xenpf_add_memtype {
	/* IN variables. */
	xen_pfn_t mfn;
	uint64_t nr_mfns;
	uint32_t type;
	/* OUT variables. */
	uint32_t handle;
	uint32_t reg;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_add_memtype_t);

/*
 * Tear down an existing memory-range type. If @handle is remembered then it
 * should be passed in to accurately tear down the correct setting (in case
 * of overlapping memory regions with differing types). If it is not known
 * then @handle should be set to zero. In all cases @reg must be set.
 * (x86-specific).
 */
#define XENPF_del_memtype         32
struct xenpf_del_memtype {
	/* IN variables. */
	uint32_t handle;
	uint32_t reg;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_del_memtype_t);

/* Read current type of an MTRR (x86-specific). */
#define XENPF_read_memtype        33
struct xenpf_read_memtype {
	/* IN variables. */
	uint32_t reg;
	/* OUT variables. */
	xen_pfn_t mfn;
	uint64_t nr_mfns;
	uint32_t type;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_read_memtype_t);

#define XENPF_microcode_update    35
struct xenpf_microcode_update {
	/* IN variables. */
	GUEST_HANDLE(void) data;          /* Pointer to microcode data */
	uint32_t length;                  /* Length of microcode data. */
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_microcode_update_t);

#define XENPF_platform_quirk      39
#define QUIRK_NOIRQBALANCING      1 /* Do not restrict IO-APIC RTE targets */
#define QUIRK_IOAPIC_BAD_REGSEL   2 /* IO-APIC REGSEL forgets its value    */
#define QUIRK_IOAPIC_GOOD_REGSEL  3 /* IO-APIC REGSEL behaves properly     */
struct xenpf_platform_quirk {
	/* IN variables. */
	uint32_t quirk_id;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_platform_quirk_t);

#define XENPF_firmware_info       50
#define XEN_FW_DISK_INFO          1 /* from int 13 AH=08/41/48 */
#define XEN_FW_DISK_MBR_SIGNATURE 2 /* from MBR offset 0x1b8 */
#define XEN_FW_VBEDDC_INFO        3 /* from int 10 AX=4f15 */
#define XEN_FW_KBD_SHIFT_FLAGS    5 /* Int16, Fn02: Get keyboard shift flags. */
struct xenpf_firmware_info {
	/* IN variables. */
	uint32_t type;
	uint32_t index;
	/* OUT variables. */
	union {
		struct {
			/* Int13, Fn48: Check Extensions Present. */
			uint8_t device;                   /* %dl: bios device number */
			uint8_t version;                  /* %ah: major version      */
			uint16_t interface_support;       /* %cx: support bitmap     */
			/* Int13, Fn08: Legacy Get Device Parameters. */
			uint16_t legacy_max_cylinder;     /* %cl[7:6]:%ch: max cyl # */
			uint8_t legacy_max_head;          /* %dh: max head #         */
			uint8_t legacy_sectors_per_track; /* %cl[5:0]: max sector #  */
			/* Int13, Fn41: Get Device Parameters (as filled into %ds:%esi). */
			/* NB. First uint16_t of buffer must be set to buffer size.      */
			GUEST_HANDLE(void) edd_params;
		} disk_info; /* XEN_FW_DISK_INFO */
		struct {
			uint8_t device;                   /* bios device number  */
			uint32_t mbr_signature;           /* offset 0x1b8 in mbr */
		} disk_mbr_signature; /* XEN_FW_DISK_MBR_SIGNATURE */
		struct {
			/* Int10, AX=4F15: Get EDID info. */
			uint8_t capabilities;
			uint8_t edid_transfer_time;
			/* must refer to 128-byte buffer */
			GUEST_HANDLE(uchar) edid;
		} vbeddc_info; /* XEN_FW_VBEDDC_INFO */

		uint8_t kbd_shift_flags; /* XEN_FW_KBD_SHIFT_FLAGS */
	} u;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_firmware_info_t);

#define XENPF_enter_acpi_sleep    51
struct xenpf_enter_acpi_sleep {
	/* IN variables */
	uint16_t pm1a_cnt_val;      /* PM1a control value. */
	uint16_t pm1b_cnt_val;      /* PM1b control value. */
	uint32_t sleep_state;       /* Which state to enter (Sn). */
	uint32_t flags;             /* Must be zero. */
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_enter_acpi_sleep_t);

#define XENPF_change_freq         52
struct xenpf_change_freq {
	/* IN variables */
	uint32_t flags; /* Must be zero. */
	uint32_t cpu;   /* Physical cpu. */
	uint64_t freq;  /* New frequency (Hz). */
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_change_freq_t);

/*
 * Get idle times (nanoseconds since boot) for physical CPUs specified in the
 * @cpumap_bitmap with range [0..@cpumap_nr_cpus-1]. The @idletime array is
 * indexed by CPU number; only entries with the corresponding @cpumap_bitmap
 * bit set are written to. On return, @cpumap_bitmap is modified so that any
 * non-existent CPUs are cleared. Such CPUs have their @idletime array entry
 * cleared.
 */
#define XENPF_getidletime         53
struct xenpf_getidletime {
	/* IN/OUT variables */
	/* IN: CPUs to interrogate; OUT: subset of IN which are present */
	GUEST_HANDLE(uchar) cpumap_bitmap;
	/* IN variables */
	/* Size of cpumap bitmap. */
	uint32_t cpumap_nr_cpus;
	/* Must be indexable for every cpu in cpumap_bitmap. */
	GUEST_HANDLE(uint64_t) idletime;
	/* OUT variables */
	/* System time when the idletime snapshots were taken. */
	uint64_t now;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_getidletime_t);

#define XENPF_set_processor_pminfo      54

/* ability bits */
#define XEN_PROCESSOR_PM_CX	1
#define XEN_PROCESSOR_PM_PX	2
#define XEN_PROCESSOR_PM_TX	4

/* cmd type */
#define XEN_PM_CX   0
#define XEN_PM_PX   1
#define XEN_PM_TX   2
#define XEN_PM_PDC  3
/* Px sub info type */
#define XEN_PX_PCT   1
#define XEN_PX_PSS   2
#define XEN_PX_PPC   4
#define XEN_PX_PSD   8

struct xen_power_register {
	uint32_t     space_id;
	uint32_t     bit_width;
	uint32_t     bit_offset;
	uint32_t     access_size;
	uint64_t     address;
};

struct xen_processor_csd {
	uint32_t    domain;      /* domain number of one dependent group */
	uint32_t    coord_type;  /* coordination type */
	uint32_t    num;         /* number of processors in same domain */
};
DEFINE_GUEST_HANDLE_STRUCT(xen_processor_csd);

struct xen_processor_cx {
	struct xen_power_register  reg; /* GAS for Cx trigger register */
	uint8_t     type;     /* cstate value, c0: 0, c1: 1, ... */
	uint32_t    latency;  /* worst latency (ms) to enter/exit this cstate */
	uint32_t    power;    /* average power consumption(mW) */
	uint32_t    dpcnt;    /* number of dependency entries */
	GUEST_HANDLE(xen_processor_csd) dp; /* NULL if no dependency */
};
DEFINE_GUEST_HANDLE_STRUCT(xen_processor_cx);

struct xen_processor_flags {
	uint32_t bm_control:1;
	uint32_t bm_check:1;
	uint32_t has_cst:1;
	uint32_t power_setup_done:1;
	uint32_t bm_rld_set:1;
};

struct xen_processor_power {
	uint32_t count;  /* number of C state entries in array below */
	struct xen_processor_flags flags;  /* global flags of this processor */
	GUEST_HANDLE(xen_processor_cx) states; /* supported c states */
};

struct xen_pct_register {
	uint8_t  descriptor;
	uint16_t length;
	uint8_t  space_id;
	uint8_t  bit_width;
	uint8_t  bit_offset;
	uint8_t  reserved;
	uint64_t address;
};

struct xen_processor_px {
	uint64_t core_frequency; /* megahertz */
	uint64_t power;      /* milliWatts */
	uint64_t transition_latency; /* microseconds */
	uint64_t bus_master_latency; /* microseconds */
	uint64_t control;        /* control value */
	uint64_t status;     /* success indicator */
};
DEFINE_GUEST_HANDLE_STRUCT(xen_processor_px);

struct xen_psd_package {
	uint64_t num_entries;
	uint64_t revision;
	uint64_t domain;
	uint64_t coord_type;
	uint64_t num_processors;
};

struct xen_processor_performance {
	uint32_t flags;     /* flag for Px sub info type */
	uint32_t platform_limit;  /* Platform limitation on freq usage */
	struct xen_pct_register control_register;
	struct xen_pct_register status_register;
	uint32_t state_count;     /* total available performance states */
	GUEST_HANDLE(xen_processor_px) states;
	struct xen_psd_package domain_info;
	uint32_t shared_type;     /* coordination type of this processor */
};
DEFINE_GUEST_HANDLE_STRUCT(xen_processor_performance);

struct xenpf_set_processor_pminfo {
	/* IN variables */
	uint32_t id;    /* ACPI CPU ID */
	uint32_t type;  /* {XEN_PM_CX, XEN_PM_PX} */
	union {
		struct xen_processor_power          power;/* Cx: _CST/_CSD */
		struct xen_processor_performance    perf; /* Px: _PPC/_PCT/_PSS/_PSD */
		GUEST_HANDLE(uint32_t)              pdc;
	};
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_set_processor_pminfo);

#define XENPF_get_cpuinfo 55
struct xenpf_pcpuinfo {
	/* IN */
	uint32_t xen_cpuid;
	/* OUT */
	/* The maxium cpu_id that is present */
	uint32_t max_present;
#define XEN_PCPU_FLAGS_ONLINE   1
	/* Correponding xen_cpuid is not present*/
#define XEN_PCPU_FLAGS_INVALID  2
	uint32_t flags;
	uint32_t apic_id;
	uint32_t acpi_id;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_pcpuinfo);

#define XENPF_cpu_online	56
#define XENPF_cpu_offline	57
struct xenpf_cpu_ol {
	uint32_t cpuid;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_cpu_ol);

/*
 * CMD 58 and 59 are reserved for cpu hotadd and memory hotadd,
 * which are already occupied at Xen hypervisor side.
 */
#define XENPF_core_parking     60
struct xenpf_core_parking {
	/* IN variables */
#define XEN_CORE_PARKING_SET   1
#define XEN_CORE_PARKING_GET   2
	uint32_t type;
	/* IN variables:  set cpu nums expected to be idled */
	/* OUT variables: get cpu nums actually be idled */
	uint32_t idle_nums;
};
DEFINE_GUEST_HANDLE_STRUCT(xenpf_core_parking);

struct xen_platform_op {
	uint32_t cmd;
	uint32_t interface_version; /* XENPF_INTERFACE_VERSION */
	union {
		struct xenpf_settime           settime;
		struct xenpf_add_memtype       add_memtype;
		struct xenpf_del_memtype       del_memtype;
		struct xenpf_read_memtype      read_memtype;
		struct xenpf_microcode_update  microcode;
		struct xenpf_platform_quirk    platform_quirk;
		struct xenpf_firmware_info     firmware_info;
		struct xenpf_enter_acpi_sleep  enter_acpi_sleep;
		struct xenpf_change_freq       change_freq;
		struct xenpf_getidletime       getidletime;
		struct xenpf_set_processor_pminfo set_pminfo;
		struct xenpf_pcpuinfo          pcpu_info;
		struct xenpf_cpu_ol            cpu_ol;
		struct xenpf_core_parking      core_parking;
		uint8_t                        pad[128];
	} u;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_platform_op_t);

#endif /* __XEN_PUBLIC_PLATFORM_H__ */
