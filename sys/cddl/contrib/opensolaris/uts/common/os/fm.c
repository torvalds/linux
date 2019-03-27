/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Fault Management Architecture (FMA) Resource and Protocol Support
 *
 * The routines contained herein provide services to support kernel subsystems
 * in publishing fault management telemetry (see PSARC 2002/412 and 2003/089).
 *
 * Name-Value Pair Lists
 *
 * The embodiment of an FMA protocol element (event, fmri or authority) is a
 * name-value pair list (nvlist_t).  FMA-specific nvlist construtor and
 * destructor functions, fm_nvlist_create() and fm_nvlist_destroy(), are used
 * to create an nvpair list using custom allocators.  Callers may choose to
 * allocate either from the kernel memory allocator, or from a preallocated
 * buffer, useful in constrained contexts like high-level interrupt routines.
 *
 * Protocol Event and FMRI Construction
 *
 * Convenience routines are provided to construct nvlist events according to
 * the FMA Event Protocol and Naming Schema specification for ereports and
 * FMRIs for the dev, cpu, hc, mem, legacy hc and de schemes.
 *
 * ENA Manipulation
 *
 * Routines to generate ENA formats 0, 1 and 2 are available as well as
 * routines to increment formats 1 and 2.  Individual fields within the
 * ENA are extractable via fm_ena_time_get(), fm_ena_id_get(),
 * fm_ena_format_get() and fm_ena_gen_get().
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysevent.h>
#include <sys/nvpair.h>
#include <sys/cmn_err.h>
#include <sys/cpuvar.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/compress.h>
#include <sys/cpuvar.h>
#include <sys/kobj.h>
#include <sys/kstat.h>
#include <sys/processor.h>
#include <sys/pcpu.h>
#include <sys/sunddi.h>
#include <sys/systeminfo.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>

/*
 * URL and SUNW-MSG-ID value to display for fm_panic(), defined below.  These
 * values must be kept in sync with the FMA source code in usr/src/cmd/fm.
 */
static const char *fm_url = "http://www.sun.com/msg";
static const char *fm_msgid = "SUNOS-8000-0G";
static char *volatile fm_panicstr = NULL;

#ifdef illumos
errorq_t *ereport_errorq;
#endif
void *ereport_dumpbuf;
size_t ereport_dumplen;

static uint_t ereport_chanlen = ERPT_EVCH_MAX;
static evchan_t *ereport_chan = NULL;
static ulong_t ereport_qlen = 0;
static size_t ereport_size = 0;
static int ereport_cols = 80;

extern void fastreboot_disable_highpil(void);

/*
 * Common fault management kstats to record ereport generation
 * failures
 */

struct erpt_kstat {
	kstat_named_t	erpt_dropped;		/* num erpts dropped on post */
	kstat_named_t	erpt_set_failed;	/* num erpt set failures */
	kstat_named_t	fmri_set_failed;	/* num fmri set failures */
	kstat_named_t	payload_set_failed;	/* num payload set failures */
};

static struct erpt_kstat erpt_kstat_data = {
	{ "erpt-dropped", KSTAT_DATA_UINT64 },
	{ "erpt-set-failed", KSTAT_DATA_UINT64 },
	{ "fmri-set-failed", KSTAT_DATA_UINT64 },
	{ "payload-set-failed", KSTAT_DATA_UINT64 }
};

#ifdef illumos
/*ARGSUSED*/
static void
fm_drain(void *private, void *data, errorq_elem_t *eep)
{
	nvlist_t *nvl = errorq_elem_nvl(ereport_errorq, eep);

	if (!panicstr)
		(void) fm_ereport_post(nvl, EVCH_TRYHARD);
	else
		fm_nvprint(nvl);
}
#endif

void
fm_init(void)
{
	kstat_t *ksp;

#ifdef illumos
	(void) sysevent_evc_bind(FM_ERROR_CHAN,
	    &ereport_chan, EVCH_CREAT | EVCH_HOLD_PEND);

	(void) sysevent_evc_control(ereport_chan,
	    EVCH_SET_CHAN_LEN, &ereport_chanlen);
#endif

	if (ereport_qlen == 0)
		ereport_qlen = ERPT_MAX_ERRS * MAX(max_ncpus, 4);

	if (ereport_size == 0)
		ereport_size = ERPT_DATA_SZ;

#ifdef illumos
	ereport_errorq = errorq_nvcreate("fm_ereport_queue",
	    (errorq_func_t)fm_drain, NULL, ereport_qlen, ereport_size,
	    FM_ERR_PIL, ERRORQ_VITAL);
	if (ereport_errorq == NULL)
		panic("failed to create required ereport error queue");
#endif

	ereport_dumpbuf = kmem_alloc(ereport_size, KM_SLEEP);
	ereport_dumplen = ereport_size;

	/* Initialize ereport allocation and generation kstats */
	ksp = kstat_create("unix", 0, "fm", "misc", KSTAT_TYPE_NAMED,
	    sizeof (struct erpt_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (ksp != NULL) {
		ksp->ks_data = &erpt_kstat_data;
		kstat_install(ksp);
	} else {
		cmn_err(CE_NOTE, "failed to create fm/misc kstat\n");

	}
}

#ifdef illumos
/*
 * Formatting utility function for fm_nvprintr.  We attempt to wrap chunks of
 * output so they aren't split across console lines, and return the end column.
 */
/*PRINTFLIKE4*/
static int
fm_printf(int depth, int c, int cols, const char *format, ...)
{
	va_list ap;
	int width;
	char c1;

	va_start(ap, format);
	width = vsnprintf(&c1, sizeof (c1), format, ap);
	va_end(ap);

	if (c + width >= cols) {
		console_printf("\n\r");
		c = 0;
		if (format[0] != ' ' && depth > 0) {
			console_printf(" ");
			c++;
		}
	}

	va_start(ap, format);
	console_vprintf(format, ap);
	va_end(ap);

	return ((c + width) % cols);
}

/*
 * Recursively print a nvlist in the specified column width and return the
 * column we end up in.  This function is called recursively by fm_nvprint(),
 * below.  We generically format the entire nvpair using hexadecimal
 * integers and strings, and elide any integer arrays.  Arrays are basically
 * used for cache dumps right now, so we suppress them so as not to overwhelm
 * the amount of console output we produce at panic time.  This can be further
 * enhanced as FMA technology grows based upon the needs of consumers.  All
 * FMA telemetry is logged using the dump device transport, so the console
 * output serves only as a fallback in case this procedure is unsuccessful.
 */
static int
fm_nvprintr(nvlist_t *nvl, int d, int c, int cols)
{
	nvpair_t *nvp;

	for (nvp = nvlist_next_nvpair(nvl, NULL);
	    nvp != NULL; nvp = nvlist_next_nvpair(nvl, nvp)) {

		data_type_t type = nvpair_type(nvp);
		const char *name = nvpair_name(nvp);

		boolean_t b;
		uint8_t i8;
		uint16_t i16;
		uint32_t i32;
		uint64_t i64;
		char *str;
		nvlist_t *cnv;

		if (strcmp(name, FM_CLASS) == 0)
			continue; /* already printed by caller */

		c = fm_printf(d, c, cols, " %s=", name);

		switch (type) {
		case DATA_TYPE_BOOLEAN:
			c = fm_printf(d + 1, c, cols, " 1");
			break;

		case DATA_TYPE_BOOLEAN_VALUE:
			(void) nvpair_value_boolean_value(nvp, &b);
			c = fm_printf(d + 1, c, cols, b ? "1" : "0");
			break;

		case DATA_TYPE_BYTE:
			(void) nvpair_value_byte(nvp, &i8);
			c = fm_printf(d + 1, c, cols, "%x", i8);
			break;

		case DATA_TYPE_INT8:
			(void) nvpair_value_int8(nvp, (void *)&i8);
			c = fm_printf(d + 1, c, cols, "%x", i8);
			break;

		case DATA_TYPE_UINT8:
			(void) nvpair_value_uint8(nvp, &i8);
			c = fm_printf(d + 1, c, cols, "%x", i8);
			break;

		case DATA_TYPE_INT16:
			(void) nvpair_value_int16(nvp, (void *)&i16);
			c = fm_printf(d + 1, c, cols, "%x", i16);
			break;

		case DATA_TYPE_UINT16:
			(void) nvpair_value_uint16(nvp, &i16);
			c = fm_printf(d + 1, c, cols, "%x", i16);
			break;

		case DATA_TYPE_INT32:
			(void) nvpair_value_int32(nvp, (void *)&i32);
			c = fm_printf(d + 1, c, cols, "%x", i32);
			break;

		case DATA_TYPE_UINT32:
			(void) nvpair_value_uint32(nvp, &i32);
			c = fm_printf(d + 1, c, cols, "%x", i32);
			break;

		case DATA_TYPE_INT64:
			(void) nvpair_value_int64(nvp, (void *)&i64);
			c = fm_printf(d + 1, c, cols, "%llx",
			    (u_longlong_t)i64);
			break;

		case DATA_TYPE_UINT64:
			(void) nvpair_value_uint64(nvp, &i64);
			c = fm_printf(d + 1, c, cols, "%llx",
			    (u_longlong_t)i64);
			break;

		case DATA_TYPE_HRTIME:
			(void) nvpair_value_hrtime(nvp, (void *)&i64);
			c = fm_printf(d + 1, c, cols, "%llx",
			    (u_longlong_t)i64);
			break;

		case DATA_TYPE_STRING:
			(void) nvpair_value_string(nvp, &str);
			c = fm_printf(d + 1, c, cols, "\"%s\"",
			    str ? str : "<NULL>");
			break;

		case DATA_TYPE_NVLIST:
			c = fm_printf(d + 1, c, cols, "[");
			(void) nvpair_value_nvlist(nvp, &cnv);
			c = fm_nvprintr(cnv, d + 1, c, cols);
			c = fm_printf(d + 1, c, cols, " ]");
			break;

		case DATA_TYPE_NVLIST_ARRAY: {
			nvlist_t **val;
			uint_t i, nelem;

			c = fm_printf(d + 1, c, cols, "[");
			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++) {
				c = fm_nvprintr(val[i], d + 1, c, cols);
			}
			c = fm_printf(d + 1, c, cols, " ]");
			}
			break;

		case DATA_TYPE_BOOLEAN_ARRAY:
		case DATA_TYPE_BYTE_ARRAY:
		case DATA_TYPE_INT8_ARRAY:
		case DATA_TYPE_UINT8_ARRAY:
		case DATA_TYPE_INT16_ARRAY:
		case DATA_TYPE_UINT16_ARRAY:
		case DATA_TYPE_INT32_ARRAY:
		case DATA_TYPE_UINT32_ARRAY:
		case DATA_TYPE_INT64_ARRAY:
		case DATA_TYPE_UINT64_ARRAY:
		case DATA_TYPE_STRING_ARRAY:
			c = fm_printf(d + 1, c, cols, "[...]");
			break;
		case DATA_TYPE_UNKNOWN:
			c = fm_printf(d + 1, c, cols, "<unknown>");
			break;
		}
	}

	return (c);
}

void
fm_nvprint(nvlist_t *nvl)
{
	char *class;
	int c = 0;

	console_printf("\r");

	if (nvlist_lookup_string(nvl, FM_CLASS, &class) == 0)
		c = fm_printf(0, c, ereport_cols, "%s", class);

	if (fm_nvprintr(nvl, 0, c, ereport_cols) != 0)
		console_printf("\n");

	console_printf("\n");
}

/*
 * Wrapper for panic() that first produces an FMA-style message for admins.
 * Normally such messages are generated by fmd(1M)'s syslog-msgs agent: this
 * is the one exception to that rule and the only error that gets messaged.
 * This function is intended for use by subsystems that have detected a fatal
 * error and enqueued appropriate ereports and wish to then force a panic.
 */
/*PRINTFLIKE1*/
void
fm_panic(const char *format, ...)
{
	va_list ap;

	(void) atomic_cas_ptr((void *)&fm_panicstr, NULL, (void *)format);
#if defined(__i386) || defined(__amd64)
	fastreboot_disable_highpil();
#endif /* __i386 || __amd64 */
	va_start(ap, format);
	vpanic(format, ap);
	va_end(ap);
}

/*
 * Simply tell the caller if fm_panicstr is set, ie. an fma event has
 * caused the panic. If so, something other than the default panic
 * diagnosis method will diagnose the cause of the panic.
 */
int
is_fm_panic()
{
	if (fm_panicstr)
		return (1);
	else
		return (0);
}

/*
 * Print any appropriate FMA banner message before the panic message.  This
 * function is called by panicsys() and prints the message for fm_panic().
 * We print the message here so that it comes after the system is quiesced.
 * A one-line summary is recorded in the log only (cmn_err(9F) with "!" prefix).
 * The rest of the message is for the console only and not needed in the log,
 * so it is printed using console_printf().  We break it up into multiple
 * chunks so as to avoid overflowing any small legacy prom_printf() buffers.
 */
void
fm_banner(void)
{
	timespec_t tod;
	hrtime_t now;

	if (!fm_panicstr)
		return; /* panic was not initiated by fm_panic(); do nothing */

	if (panicstr) {
		tod = panic_hrestime;
		now = panic_hrtime;
	} else {
		gethrestime(&tod);
		now = gethrtime_waitfree();
	}

	cmn_err(CE_NOTE, "!SUNW-MSG-ID: %s, "
	    "TYPE: Error, VER: 1, SEVERITY: Major\n", fm_msgid);

	console_printf(
"\n\rSUNW-MSG-ID: %s, TYPE: Error, VER: 1, SEVERITY: Major\n"
"EVENT-TIME: 0x%lx.0x%lx (0x%llx)\n",
	    fm_msgid, tod.tv_sec, tod.tv_nsec, (u_longlong_t)now);

	console_printf(
"PLATFORM: %s, CSN: -, HOSTNAME: %s\n"
"SOURCE: %s, REV: %s %s\n",
	    platform, utsname.nodename, utsname.sysname,
	    utsname.release, utsname.version);

	console_printf(
"DESC: Errors have been detected that require a reboot to ensure system\n"
"integrity.  See %s/%s for more information.\n",
	    fm_url, fm_msgid);

	console_printf(
"AUTO-RESPONSE: Solaris will attempt to save and diagnose the error telemetry\n"
"IMPACT: The system will sync files, save a crash dump if needed, and reboot\n"
"REC-ACTION: Save the error summary below in case telemetry cannot be saved\n");

	console_printf("\n");
}

/*
 * Utility function to write all of the pending ereports to the dump device.
 * This function is called at either normal reboot or panic time, and simply
 * iterates over the in-transit messages in the ereport sysevent channel.
 */
void
fm_ereport_dump(void)
{
	evchanq_t *chq;
	sysevent_t *sep;
	erpt_dump_t ed;

	timespec_t tod;
	hrtime_t now;
	char *buf;
	size_t len;

	if (panicstr) {
		tod = panic_hrestime;
		now = panic_hrtime;
	} else {
		if (ereport_errorq != NULL)
			errorq_drain(ereport_errorq);
		gethrestime(&tod);
		now = gethrtime_waitfree();
	}

	/*
	 * In the panic case, sysevent_evc_walk_init() will return NULL.
	 */
	if ((chq = sysevent_evc_walk_init(ereport_chan, NULL)) == NULL &&
	    !panicstr)
		return; /* event channel isn't initialized yet */

	while ((sep = sysevent_evc_walk_step(chq)) != NULL) {
		if ((buf = sysevent_evc_event_attr(sep, &len)) == NULL)
			break;

		ed.ed_magic = ERPT_MAGIC;
		ed.ed_chksum = checksum32(buf, len);
		ed.ed_size = (uint32_t)len;
		ed.ed_pad = 0;
		ed.ed_hrt_nsec = SE_TIME(sep);
		ed.ed_hrt_base = now;
		ed.ed_tod_base.sec = tod.tv_sec;
		ed.ed_tod_base.nsec = tod.tv_nsec;

		dumpvp_write(&ed, sizeof (ed));
		dumpvp_write(buf, len);
	}

	sysevent_evc_walk_fini(chq);
}
#endif

/*
 * Post an error report (ereport) to the sysevent error channel.  The error
 * channel must be established with a prior call to sysevent_evc_create()
 * before publication may occur.
 */
void
fm_ereport_post(nvlist_t *ereport, int evc_flag)
{
	size_t nvl_size = 0;
	evchan_t *error_chan;
	sysevent_id_t eid;

	(void) nvlist_size(ereport, &nvl_size, NV_ENCODE_NATIVE);
	if (nvl_size > ERPT_DATA_SZ || nvl_size == 0) {
		atomic_inc_64(&erpt_kstat_data.erpt_dropped.value.ui64);
		return;
	}

#ifdef illumos
	if (sysevent_evc_bind(FM_ERROR_CHAN, &error_chan,
	    EVCH_CREAT|EVCH_HOLD_PEND) != 0) {
		atomic_inc_64(&erpt_kstat_data.erpt_dropped.value.ui64);
		return;
	}

	if (sysevent_evc_publish(error_chan, EC_FM, ESC_FM_ERROR,
	    SUNW_VENDOR, FM_PUB, ereport, evc_flag) != 0) {
		atomic_inc_64(&erpt_kstat_data.erpt_dropped.value.ui64);
		(void) sysevent_evc_unbind(error_chan);
		return;
	}
	(void) sysevent_evc_unbind(error_chan);
#else
	(void) ddi_log_sysevent(NULL, SUNW_VENDOR, EC_DEV_STATUS,
	    ESC_DEV_DLE, ereport, &eid, DDI_SLEEP);
#endif
}

/*
 * Wrapppers for FM nvlist allocators
 */
/* ARGSUSED */
static void *
i_fm_alloc(nv_alloc_t *nva, size_t size)
{
	return (kmem_zalloc(size, KM_SLEEP));
}

/* ARGSUSED */
static void
i_fm_free(nv_alloc_t *nva, void *buf, size_t size)
{
	kmem_free(buf, size);
}

const nv_alloc_ops_t fm_mem_alloc_ops = {
	NULL,
	NULL,
	i_fm_alloc,
	i_fm_free,
	NULL
};

/*
 * Create and initialize a new nv_alloc_t for a fixed buffer, buf.  A pointer
 * to the newly allocated nv_alloc_t structure is returned upon success or NULL
 * is returned to indicate that the nv_alloc structure could not be created.
 */
nv_alloc_t *
fm_nva_xcreate(char *buf, size_t bufsz)
{
	nv_alloc_t *nvhdl = kmem_zalloc(sizeof (nv_alloc_t), KM_SLEEP);

	if (bufsz == 0 || nv_alloc_init(nvhdl, nv_fixed_ops, buf, bufsz) != 0) {
		kmem_free(nvhdl, sizeof (nv_alloc_t));
		return (NULL);
	}

	return (nvhdl);
}

/*
 * Destroy a previously allocated nv_alloc structure.  The fixed buffer
 * associated with nva must be freed by the caller.
 */
void
fm_nva_xdestroy(nv_alloc_t *nva)
{
	nv_alloc_fini(nva);
	kmem_free(nva, sizeof (nv_alloc_t));
}

/*
 * Create a new nv list.  A pointer to a new nv list structure is returned
 * upon success or NULL is returned to indicate that the structure could
 * not be created.  The newly created nv list is created and managed by the
 * operations installed in nva.   If nva is NULL, the default FMA nva
 * operations are installed and used.
 *
 * When called from the kernel and nva == NULL, this function must be called
 * from passive kernel context with no locks held that can prevent a
 * sleeping memory allocation from occurring.  Otherwise, this function may
 * be called from other kernel contexts as long a valid nva created via
 * fm_nva_create() is supplied.
 */
nvlist_t *
fm_nvlist_create(nv_alloc_t *nva)
{
	int hdl_alloced = 0;
	nvlist_t *nvl;
	nv_alloc_t *nvhdl;

	if (nva == NULL) {
		nvhdl = kmem_zalloc(sizeof (nv_alloc_t), KM_SLEEP);

		if (nv_alloc_init(nvhdl, &fm_mem_alloc_ops, NULL, 0) != 0) {
			kmem_free(nvhdl, sizeof (nv_alloc_t));
			return (NULL);
		}
		hdl_alloced = 1;
	} else {
		nvhdl = nva;
	}

	if (nvlist_xalloc(&nvl, NV_UNIQUE_NAME, nvhdl) != 0) {
		if (hdl_alloced) {
			nv_alloc_fini(nvhdl);
			kmem_free(nvhdl, sizeof (nv_alloc_t));
		}
		return (NULL);
	}

	return (nvl);
}

/*
 * Destroy a previously allocated nvlist structure.  flag indicates whether
 * or not the associated nva structure should be freed (FM_NVA_FREE) or
 * retained (FM_NVA_RETAIN).  Retaining the nv alloc structure allows
 * it to be re-used for future nvlist creation operations.
 */
void
fm_nvlist_destroy(nvlist_t *nvl, int flag)
{
	nv_alloc_t *nva = nvlist_lookup_nv_alloc(nvl);

	nvlist_free(nvl);

	if (nva != NULL) {
		if (flag == FM_NVA_FREE)
			fm_nva_xdestroy(nva);
	}
}

int
i_fm_payload_set(nvlist_t *payload, const char *name, va_list ap)
{
	int nelem, ret = 0;
	data_type_t type;

	while (ret == 0 && name != NULL) {
		type = va_arg(ap, data_type_t);
		switch (type) {
		case DATA_TYPE_BYTE:
			ret = nvlist_add_byte(payload, name,
			    va_arg(ap, uint_t));
			break;
		case DATA_TYPE_BYTE_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_byte_array(payload, name,
			    va_arg(ap, uchar_t *), nelem);
			break;
		case DATA_TYPE_BOOLEAN_VALUE:
			ret = nvlist_add_boolean_value(payload, name,
			    va_arg(ap, boolean_t));
			break;
		case DATA_TYPE_BOOLEAN_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_boolean_array(payload, name,
			    va_arg(ap, boolean_t *), nelem);
			break;
		case DATA_TYPE_INT8:
			ret = nvlist_add_int8(payload, name,
			    va_arg(ap, int));
			break;
		case DATA_TYPE_INT8_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_int8_array(payload, name,
			    va_arg(ap, int8_t *), nelem);
			break;
		case DATA_TYPE_UINT8:
			ret = nvlist_add_uint8(payload, name,
			    va_arg(ap, uint_t));
			break;
		case DATA_TYPE_UINT8_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_uint8_array(payload, name,
			    va_arg(ap, uint8_t *), nelem);
			break;
		case DATA_TYPE_INT16:
			ret = nvlist_add_int16(payload, name,
			    va_arg(ap, int));
			break;
		case DATA_TYPE_INT16_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_int16_array(payload, name,
			    va_arg(ap, int16_t *), nelem);
			break;
		case DATA_TYPE_UINT16:
			ret = nvlist_add_uint16(payload, name,
			    va_arg(ap, uint_t));
			break;
		case DATA_TYPE_UINT16_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_uint16_array(payload, name,
			    va_arg(ap, uint16_t *), nelem);
			break;
		case DATA_TYPE_INT32:
			ret = nvlist_add_int32(payload, name,
			    va_arg(ap, int32_t));
			break;
		case DATA_TYPE_INT32_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_int32_array(payload, name,
			    va_arg(ap, int32_t *), nelem);
			break;
		case DATA_TYPE_UINT32:
			ret = nvlist_add_uint32(payload, name,
			    va_arg(ap, uint32_t));
			break;
		case DATA_TYPE_UINT32_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_uint32_array(payload, name,
			    va_arg(ap, uint32_t *), nelem);
			break;
		case DATA_TYPE_INT64:
			ret = nvlist_add_int64(payload, name,
			    va_arg(ap, int64_t));
			break;
		case DATA_TYPE_INT64_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_int64_array(payload, name,
			    va_arg(ap, int64_t *), nelem);
			break;
		case DATA_TYPE_UINT64:
			ret = nvlist_add_uint64(payload, name,
			    va_arg(ap, uint64_t));
			break;
		case DATA_TYPE_UINT64_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_uint64_array(payload, name,
			    va_arg(ap, uint64_t *), nelem);
			break;
		case DATA_TYPE_STRING:
			ret = nvlist_add_string(payload, name,
			    va_arg(ap, char *));
			break;
		case DATA_TYPE_STRING_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_string_array(payload, name,
			    va_arg(ap, char **), nelem);
			break;
		case DATA_TYPE_NVLIST:
			ret = nvlist_add_nvlist(payload, name,
			    va_arg(ap, nvlist_t *));
			break;
		case DATA_TYPE_NVLIST_ARRAY:
			nelem = va_arg(ap, int);
			ret = nvlist_add_nvlist_array(payload, name,
			    va_arg(ap, nvlist_t **), nelem);
			break;
		default:
			ret = EINVAL;
		}

		name = va_arg(ap, char *);
	}
	return (ret);
}

void
fm_payload_set(nvlist_t *payload, ...)
{
	int ret;
	const char *name;
	va_list ap;

	va_start(ap, payload);
	name = va_arg(ap, char *);
	ret = i_fm_payload_set(payload, name, ap);
	va_end(ap);

	if (ret)
		atomic_inc_64(&erpt_kstat_data.payload_set_failed.value.ui64);
}

/*
 * Set-up and validate the members of an ereport event according to:
 *
 *	Member name		Type		Value
 *	====================================================
 *	class			string		ereport
 *	version			uint8_t		0
 *	ena			uint64_t	<ena>
 *	detector		nvlist_t	<detector>
 *	ereport-payload		nvlist_t	<var args>
 *
 * We don't actually add a 'version' member to the payload.  Really,
 * the version quoted to us by our caller is that of the category 1
 * "ereport" event class (and we require FM_EREPORT_VERS0) but
 * the payload version of the actual leaf class event under construction
 * may be something else.  Callers should supply a version in the varargs,
 * or (better) we could take two version arguments - one for the
 * ereport category 1 classification (expect FM_EREPORT_VERS0) and one
 * for the leaf class.
 */
void
fm_ereport_set(nvlist_t *ereport, int version, const char *erpt_class,
    uint64_t ena, const nvlist_t *detector, ...)
{
	char ereport_class[FM_MAX_CLASS];
	const char *name;
	va_list ap;
	int ret;

	if (version != FM_EREPORT_VERS0) {
		atomic_inc_64(&erpt_kstat_data.erpt_set_failed.value.ui64);
		return;
	}

	(void) snprintf(ereport_class, FM_MAX_CLASS, "%s.%s",
	    FM_EREPORT_CLASS, erpt_class);
	if (nvlist_add_string(ereport, FM_CLASS, ereport_class) != 0) {
		atomic_inc_64(&erpt_kstat_data.erpt_set_failed.value.ui64);
		return;
	}

	if (nvlist_add_uint64(ereport, FM_EREPORT_ENA, ena)) {
		atomic_inc_64(&erpt_kstat_data.erpt_set_failed.value.ui64);
	}

	if (nvlist_add_nvlist(ereport, FM_EREPORT_DETECTOR,
	    (nvlist_t *)detector) != 0) {
		atomic_inc_64(&erpt_kstat_data.erpt_set_failed.value.ui64);
	}

	va_start(ap, detector);
	name = va_arg(ap, const char *);
	ret = i_fm_payload_set(ereport, name, ap);
	va_end(ap);

	if (ret)
		atomic_inc_64(&erpt_kstat_data.erpt_set_failed.value.ui64);
}

/*
 * Set-up and validate the members of an hc fmri according to;
 *
 *	Member name		Type		Value
 *	===================================================
 *	version			uint8_t		0
 *	auth			nvlist_t	<auth>
 *	hc-name			string		<name>
 *	hc-id			string		<id>
 *
 * Note that auth and hc-id are optional members.
 */

#define	HC_MAXPAIRS	20
#define	HC_MAXNAMELEN	50

static int
fm_fmri_hc_set_common(nvlist_t *fmri, int version, const nvlist_t *auth)
{
	if (version != FM_HC_SCHEME_VERSION) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return (0);
	}

	if (nvlist_add_uint8(fmri, FM_VERSION, version) != 0 ||
	    nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_HC) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return (0);
	}

	if (auth != NULL && nvlist_add_nvlist(fmri, FM_FMRI_AUTHORITY,
	    (nvlist_t *)auth) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return (0);
	}

	return (1);
}

void
fm_fmri_hc_set(nvlist_t *fmri, int version, const nvlist_t *auth,
    nvlist_t *snvl, int npairs, ...)
{
	nv_alloc_t *nva = nvlist_lookup_nv_alloc(fmri);
	nvlist_t *pairs[HC_MAXPAIRS];
	va_list ap;
	int i;

	if (!fm_fmri_hc_set_common(fmri, version, auth))
		return;

	npairs = MIN(npairs, HC_MAXPAIRS);

	va_start(ap, npairs);
	for (i = 0; i < npairs; i++) {
		const char *name = va_arg(ap, const char *);
		uint32_t id = va_arg(ap, uint32_t);
		char idstr[11];

		(void) snprintf(idstr, sizeof (idstr), "%u", id);

		pairs[i] = fm_nvlist_create(nva);
		if (nvlist_add_string(pairs[i], FM_FMRI_HC_NAME, name) != 0 ||
		    nvlist_add_string(pairs[i], FM_FMRI_HC_ID, idstr) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
		}
	}
	va_end(ap);

	if (nvlist_add_nvlist_array(fmri, FM_FMRI_HC_LIST, pairs, npairs) != 0)
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);

	for (i = 0; i < npairs; i++)
		fm_nvlist_destroy(pairs[i], FM_NVA_RETAIN);

	if (snvl != NULL) {
		if (nvlist_add_nvlist(fmri, FM_FMRI_HC_SPECIFIC, snvl) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
		}
	}
}

/*
 * Set-up and validate the members of an dev fmri according to:
 *
 *	Member name		Type		Value
 *	====================================================
 *	version			uint8_t		0
 *	auth			nvlist_t	<auth>
 *	devpath			string		<devpath>
 *	[devid]			string		<devid>
 *	[target-port-l0id]	string		<target-port-lun0-id>
 *
 * Note that auth and devid are optional members.
 */
void
fm_fmri_dev_set(nvlist_t *fmri_dev, int version, const nvlist_t *auth,
    const char *devpath, const char *devid, const char *tpl0)
{
	int err = 0;

	if (version != DEV_SCHEME_VERSION0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	err |= nvlist_add_uint8(fmri_dev, FM_VERSION, version);
	err |= nvlist_add_string(fmri_dev, FM_FMRI_SCHEME, FM_FMRI_SCHEME_DEV);

	if (auth != NULL) {
		err |= nvlist_add_nvlist(fmri_dev, FM_FMRI_AUTHORITY,
		    (nvlist_t *)auth);
	}

	err |= nvlist_add_string(fmri_dev, FM_FMRI_DEV_PATH, devpath);

	if (devid != NULL)
		err |= nvlist_add_string(fmri_dev, FM_FMRI_DEV_ID, devid);

	if (tpl0 != NULL)
		err |= nvlist_add_string(fmri_dev, FM_FMRI_DEV_TGTPTLUN0, tpl0);

	if (err)
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);

}

/*
 * Set-up and validate the members of an cpu fmri according to:
 *
 *	Member name		Type		Value
 *	====================================================
 *	version			uint8_t		0
 *	auth			nvlist_t	<auth>
 *	cpuid			uint32_t	<cpu_id>
 *	cpumask			uint8_t		<cpu_mask>
 *	serial			uint64_t	<serial_id>
 *
 * Note that auth, cpumask, serial are optional members.
 *
 */
void
fm_fmri_cpu_set(nvlist_t *fmri_cpu, int version, const nvlist_t *auth,
    uint32_t cpu_id, uint8_t *cpu_maskp, const char *serial_idp)
{
	uint64_t *failedp = &erpt_kstat_data.fmri_set_failed.value.ui64;

	if (version < CPU_SCHEME_VERSION1) {
		atomic_inc_64(failedp);
		return;
	}

	if (nvlist_add_uint8(fmri_cpu, FM_VERSION, version) != 0) {
		atomic_inc_64(failedp);
		return;
	}

	if (nvlist_add_string(fmri_cpu, FM_FMRI_SCHEME,
	    FM_FMRI_SCHEME_CPU) != 0) {
		atomic_inc_64(failedp);
		return;
	}

	if (auth != NULL && nvlist_add_nvlist(fmri_cpu, FM_FMRI_AUTHORITY,
	    (nvlist_t *)auth) != 0)
		atomic_inc_64(failedp);

	if (nvlist_add_uint32(fmri_cpu, FM_FMRI_CPU_ID, cpu_id) != 0)
		atomic_inc_64(failedp);

	if (cpu_maskp != NULL && nvlist_add_uint8(fmri_cpu, FM_FMRI_CPU_MASK,
	    *cpu_maskp) != 0)
		atomic_inc_64(failedp);

	if (serial_idp == NULL || nvlist_add_string(fmri_cpu,
	    FM_FMRI_CPU_SERIAL_ID, (char *)serial_idp) != 0)
			atomic_inc_64(failedp);
}

/*
 * Set-up and validate the members of a mem according to:
 *
 *	Member name		Type		Value
 *	====================================================
 *	version			uint8_t		0
 *	auth			nvlist_t	<auth>		[optional]
 *	unum			string		<unum>
 *	serial			string		<serial>	[optional*]
 *	offset			uint64_t	<offset>	[optional]
 *
 *	* serial is required if offset is present
 */
void
fm_fmri_mem_set(nvlist_t *fmri, int version, const nvlist_t *auth,
    const char *unum, const char *serial, uint64_t offset)
{
	if (version != MEM_SCHEME_VERSION0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (!serial && (offset != (uint64_t)-1)) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (nvlist_add_uint8(fmri, FM_VERSION, version) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_MEM) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (auth != NULL) {
		if (nvlist_add_nvlist(fmri, FM_FMRI_AUTHORITY,
		    (nvlist_t *)auth) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
		}
	}

	if (nvlist_add_string(fmri, FM_FMRI_MEM_UNUM, unum) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
	}

	if (serial != NULL) {
		if (nvlist_add_string_array(fmri, FM_FMRI_MEM_SERIAL_ID,
		    (char **)&serial, 1) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
		}
		if (offset != (uint64_t)-1 && nvlist_add_uint64(fmri,
		    FM_FMRI_MEM_OFFSET, offset) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
		}
	}
}

void
fm_fmri_zfs_set(nvlist_t *fmri, int version, uint64_t pool_guid,
    uint64_t vdev_guid)
{
	if (version != ZFS_SCHEME_VERSION0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (nvlist_add_uint8(fmri, FM_VERSION, version) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_ZFS) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	if (nvlist_add_uint64(fmri, FM_FMRI_ZFS_POOL, pool_guid) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
	}

	if (vdev_guid != 0) {
		if (nvlist_add_uint64(fmri, FM_FMRI_ZFS_VDEV, vdev_guid) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
		}
	}
}

uint64_t
fm_ena_increment(uint64_t ena)
{
	uint64_t new_ena;

	switch (ENA_FORMAT(ena)) {
	case FM_ENA_FMT1:
		new_ena = ena + (1 << ENA_FMT1_GEN_SHFT);
		break;
	case FM_ENA_FMT2:
		new_ena = ena + (1 << ENA_FMT2_GEN_SHFT);
		break;
	default:
		new_ena = 0;
	}

	return (new_ena);
}

uint64_t
fm_ena_generate_cpu(uint64_t timestamp, processorid_t cpuid, uchar_t format)
{
	uint64_t ena = 0;

	switch (format) {
	case FM_ENA_FMT1:
		if (timestamp) {
			ena = (uint64_t)((format & ENA_FORMAT_MASK) |
			    ((cpuid << ENA_FMT1_CPUID_SHFT) &
			    ENA_FMT1_CPUID_MASK) |
			    ((timestamp << ENA_FMT1_TIME_SHFT) &
			    ENA_FMT1_TIME_MASK));
		} else {
			ena = (uint64_t)((format & ENA_FORMAT_MASK) |
			    ((cpuid << ENA_FMT1_CPUID_SHFT) &
			    ENA_FMT1_CPUID_MASK) |
			    ((gethrtime_waitfree() << ENA_FMT1_TIME_SHFT) &
			    ENA_FMT1_TIME_MASK));
		}
		break;
	case FM_ENA_FMT2:
		ena = (uint64_t)((format & ENA_FORMAT_MASK) |
		    ((timestamp << ENA_FMT2_TIME_SHFT) & ENA_FMT2_TIME_MASK));
		break;
	default:
		break;
	}

	return (ena);
}

uint64_t
fm_ena_generate(uint64_t timestamp, uchar_t format)
{
	return (fm_ena_generate_cpu(timestamp, PCPU_GET(cpuid), format));
}

uint64_t
fm_ena_generation_get(uint64_t ena)
{
	uint64_t gen;

	switch (ENA_FORMAT(ena)) {
	case FM_ENA_FMT1:
		gen = (ena & ENA_FMT1_GEN_MASK) >> ENA_FMT1_GEN_SHFT;
		break;
	case FM_ENA_FMT2:
		gen = (ena & ENA_FMT2_GEN_MASK) >> ENA_FMT2_GEN_SHFT;
		break;
	default:
		gen = 0;
		break;
	}

	return (gen);
}

uchar_t
fm_ena_format_get(uint64_t ena)
{

	return (ENA_FORMAT(ena));
}

uint64_t
fm_ena_id_get(uint64_t ena)
{
	uint64_t id;

	switch (ENA_FORMAT(ena)) {
	case FM_ENA_FMT1:
		id = (ena & ENA_FMT1_ID_MASK) >> ENA_FMT1_ID_SHFT;
		break;
	case FM_ENA_FMT2:
		id = (ena & ENA_FMT2_ID_MASK) >> ENA_FMT2_ID_SHFT;
		break;
	default:
		id = 0;
	}

	return (id);
}

uint64_t
fm_ena_time_get(uint64_t ena)
{
	uint64_t time;

	switch (ENA_FORMAT(ena)) {
	case FM_ENA_FMT1:
		time = (ena & ENA_FMT1_TIME_MASK) >> ENA_FMT1_TIME_SHFT;
		break;
	case FM_ENA_FMT2:
		time = (ena & ENA_FMT2_TIME_MASK) >> ENA_FMT2_TIME_SHFT;
		break;
	default:
		time = 0;
	}

	return (time);
}

#ifdef illumos
/*
 * Convert a getpcstack() trace to symbolic name+offset, and add the resulting
 * string array to a Fault Management ereport as FM_EREPORT_PAYLOAD_NAME_STACK.
 */
void
fm_payload_stack_add(nvlist_t *payload, const pc_t *stack, int depth)
{
	int i;
	char *sym;
	ulong_t off;
	char *stkpp[FM_STK_DEPTH];
	char buf[FM_STK_DEPTH * FM_SYM_SZ];
	char *stkp = buf;

	for (i = 0; i < depth && i != FM_STK_DEPTH; i++, stkp += FM_SYM_SZ) {
		if ((sym = kobj_getsymname(stack[i], &off)) != NULL)
			(void) snprintf(stkp, FM_SYM_SZ, "%s+%lx", sym, off);
		else
			(void) snprintf(stkp, FM_SYM_SZ, "%lx", (long)stack[i]);
		stkpp[i] = stkp;
	}

	fm_payload_set(payload, FM_EREPORT_PAYLOAD_NAME_STACK,
	    DATA_TYPE_STRING_ARRAY, depth, stkpp, NULL);
}
#endif

#ifdef illumos
void
print_msg_hwerr(ctid_t ct_id, proc_t *p)
{
	uprintf("Killed process %d (%s) in contract id %d "
	    "due to hardware error\n", p->p_pid, p->p_user.u_comm, ct_id);
}
#endif

void
fm_fmri_hc_create(nvlist_t *fmri, int version, const nvlist_t *auth,
    nvlist_t *snvl, nvlist_t *bboard, int npairs, ...)
{
	nv_alloc_t *nva = nvlist_lookup_nv_alloc(fmri);
	nvlist_t *pairs[HC_MAXPAIRS];
	nvlist_t **hcl;
	uint_t n;
	int i, j;
	va_list ap;
	char *hcname, *hcid;

	if (!fm_fmri_hc_set_common(fmri, version, auth))
		return;

	/*
	 * copy the bboard nvpairs to the pairs array
	 */
	if (nvlist_lookup_nvlist_array(bboard, FM_FMRI_HC_LIST, &hcl, &n)
	    != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	for (i = 0; i < n; i++) {
		if (nvlist_lookup_string(hcl[i], FM_FMRI_HC_NAME,
		    &hcname) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
			return;
		}
		if (nvlist_lookup_string(hcl[i], FM_FMRI_HC_ID, &hcid) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
			return;
		}

		pairs[i] = fm_nvlist_create(nva);
		if (nvlist_add_string(pairs[i], FM_FMRI_HC_NAME, hcname) != 0 ||
		    nvlist_add_string(pairs[i], FM_FMRI_HC_ID, hcid) != 0) {
			for (j = 0; j <= i; j++) {
				if (pairs[j] != NULL)
					fm_nvlist_destroy(pairs[j],
					    FM_NVA_RETAIN);
			}
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
			return;
		}
	}

	/*
	 * create the pairs from passed in pairs
	 */
	npairs = MIN(npairs, HC_MAXPAIRS);

	va_start(ap, npairs);
	for (i = n; i < npairs + n; i++) {
		const char *name = va_arg(ap, const char *);
		uint32_t id = va_arg(ap, uint32_t);
		char idstr[11];
		(void) snprintf(idstr, sizeof (idstr), "%u", id);
		pairs[i] = fm_nvlist_create(nva);
		if (nvlist_add_string(pairs[i], FM_FMRI_HC_NAME, name) != 0 ||
		    nvlist_add_string(pairs[i], FM_FMRI_HC_ID, idstr) != 0) {
			for (j = 0; j <= i; j++) {
				if (pairs[j] != NULL)
					fm_nvlist_destroy(pairs[j],
					    FM_NVA_RETAIN);
			}
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
			return;
		}
	}
	va_end(ap);

	/*
	 * Create the fmri hc list
	 */
	if (nvlist_add_nvlist_array(fmri, FM_FMRI_HC_LIST, pairs,
	    npairs + n) != 0) {
		atomic_inc_64(&erpt_kstat_data.fmri_set_failed.value.ui64);
		return;
	}

	for (i = 0; i < npairs + n; i++) {
			fm_nvlist_destroy(pairs[i], FM_NVA_RETAIN);
	}

	if (snvl != NULL) {
		if (nvlist_add_nvlist(fmri, FM_FMRI_HC_SPECIFIC, snvl) != 0) {
			atomic_inc_64(
			    &erpt_kstat_data.fmri_set_failed.value.ui64);
			return;
		}
	}
}
