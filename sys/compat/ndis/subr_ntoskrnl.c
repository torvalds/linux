/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ctype.h>
#include <sys/unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/callout.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/kthread.h>
#include <sys/module.h>
#include <sys/smp.h>
#include <sys/sched.h>
#include <sys/sysctl.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/stdarg.h>
#include <machine/resource.h>

#include <sys/bus.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/uma.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <compat/ndis/pe_var.h>
#include <compat/ndis/cfg_var.h>
#include <compat/ndis/resource_var.h>
#include <compat/ndis/ntoskrnl_var.h>
#include <compat/ndis/hal_var.h>
#include <compat/ndis/ndis_var.h>

#ifdef NTOSKRNL_DEBUG_TIMERS
static int sysctl_show_timers(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_debug, OID_AUTO, ntoskrnl_timers, CTLTYPE_INT | CTLFLAG_RW,
    NULL, 0, sysctl_show_timers, "I",
    "Show ntoskrnl timer stats");
#endif

struct kdpc_queue {
	list_entry		kq_disp;
	struct thread		*kq_td;
	int			kq_cpu;
	int			kq_exit;
	int			kq_running;
	kspin_lock		kq_lock;
	nt_kevent		kq_proc;
	nt_kevent		kq_done;
};

typedef struct kdpc_queue kdpc_queue;

struct wb_ext {
	struct cv		we_cv;
	struct thread		*we_td;
};

typedef struct wb_ext wb_ext;

#define NTOSKRNL_TIMEOUTS	256
#ifdef NTOSKRNL_DEBUG_TIMERS
static uint64_t ntoskrnl_timer_fires;
static uint64_t ntoskrnl_timer_sets;
static uint64_t ntoskrnl_timer_reloads;
static uint64_t ntoskrnl_timer_cancels;
#endif

struct callout_entry {
	struct callout		ce_callout;
	list_entry		ce_list;
};

typedef struct callout_entry callout_entry;

static struct list_entry ntoskrnl_calllist;
static struct mtx ntoskrnl_calllock;
struct kuser_shared_data kuser_shared_data;

static struct list_entry ntoskrnl_intlist;
static kspin_lock ntoskrnl_intlock;

static uint8_t RtlEqualUnicodeString(unicode_string *,
	unicode_string *, uint8_t);
static void RtlCopyString(ansi_string *, const ansi_string *);
static void RtlCopyUnicodeString(unicode_string *,
	unicode_string *);
static irp *IoBuildSynchronousFsdRequest(uint32_t, device_object *,
	 void *, uint32_t, uint64_t *, nt_kevent *, io_status_block *);
static irp *IoBuildAsynchronousFsdRequest(uint32_t,
	device_object *, void *, uint32_t, uint64_t *, io_status_block *);
static irp *IoBuildDeviceIoControlRequest(uint32_t,
	device_object *, void *, uint32_t, void *, uint32_t,
	uint8_t, nt_kevent *, io_status_block *);
static irp *IoAllocateIrp(uint8_t, uint8_t);
static void IoReuseIrp(irp *, uint32_t);
static void IoFreeIrp(irp *);
static void IoInitializeIrp(irp *, uint16_t, uint8_t);
static irp *IoMakeAssociatedIrp(irp *, uint8_t);
static uint32_t KeWaitForMultipleObjects(uint32_t,
	nt_dispatch_header **, uint32_t, uint32_t, uint32_t, uint8_t,
	int64_t *, wait_block *);
static void ntoskrnl_waittest(nt_dispatch_header *, uint32_t);
static void ntoskrnl_satisfy_wait(nt_dispatch_header *, struct thread *);
static void ntoskrnl_satisfy_multiple_waits(wait_block *);
static int ntoskrnl_is_signalled(nt_dispatch_header *, struct thread *);
static void ntoskrnl_insert_timer(ktimer *, int);
static void ntoskrnl_remove_timer(ktimer *);
#ifdef NTOSKRNL_DEBUG_TIMERS
static void ntoskrnl_show_timers(void);
#endif
static void ntoskrnl_timercall(void *);
static void ntoskrnl_dpc_thread(void *);
static void ntoskrnl_destroy_dpc_threads(void);
static void ntoskrnl_destroy_workitem_threads(void);
static void ntoskrnl_workitem_thread(void *);
static void ntoskrnl_workitem(device_object *, void *);
static void ntoskrnl_unicode_to_ascii(uint16_t *, char *, int);
static void ntoskrnl_ascii_to_unicode(char *, uint16_t *, int);
static uint8_t ntoskrnl_insert_dpc(list_entry *, kdpc *);
static void WRITE_REGISTER_USHORT(uint16_t *, uint16_t);
static uint16_t READ_REGISTER_USHORT(uint16_t *);
static void WRITE_REGISTER_ULONG(uint32_t *, uint32_t);
static uint32_t READ_REGISTER_ULONG(uint32_t *);
static void WRITE_REGISTER_UCHAR(uint8_t *, uint8_t);
static uint8_t READ_REGISTER_UCHAR(uint8_t *);
static int64_t _allmul(int64_t, int64_t);
static int64_t _alldiv(int64_t, int64_t);
static int64_t _allrem(int64_t, int64_t);
static int64_t _allshr(int64_t, uint8_t);
static int64_t _allshl(int64_t, uint8_t);
static uint64_t _aullmul(uint64_t, uint64_t);
static uint64_t _aulldiv(uint64_t, uint64_t);
static uint64_t _aullrem(uint64_t, uint64_t);
static uint64_t _aullshr(uint64_t, uint8_t);
static uint64_t _aullshl(uint64_t, uint8_t);
static slist_entry *ntoskrnl_pushsl(slist_header *, slist_entry *);
static void InitializeSListHead(slist_header *);
static slist_entry *ntoskrnl_popsl(slist_header *);
static void ExFreePoolWithTag(void *, uint32_t);
static void ExInitializePagedLookasideList(paged_lookaside_list *,
	lookaside_alloc_func *, lookaside_free_func *,
	uint32_t, size_t, uint32_t, uint16_t);
static void ExDeletePagedLookasideList(paged_lookaside_list *);
static void ExInitializeNPagedLookasideList(npaged_lookaside_list *,
	lookaside_alloc_func *, lookaside_free_func *,
	uint32_t, size_t, uint32_t, uint16_t);
static void ExDeleteNPagedLookasideList(npaged_lookaside_list *);
static slist_entry
	*ExInterlockedPushEntrySList(slist_header *,
	slist_entry *, kspin_lock *);
static slist_entry
	*ExInterlockedPopEntrySList(slist_header *, kspin_lock *);
static uint32_t InterlockedIncrement(volatile uint32_t *);
static uint32_t InterlockedDecrement(volatile uint32_t *);
static void ExInterlockedAddLargeStatistic(uint64_t *, uint32_t);
static void *MmAllocateContiguousMemory(uint32_t, uint64_t);
static void *MmAllocateContiguousMemorySpecifyCache(uint32_t,
	uint64_t, uint64_t, uint64_t, enum nt_caching_type);
static void MmFreeContiguousMemory(void *);
static void MmFreeContiguousMemorySpecifyCache(void *, uint32_t,
	enum nt_caching_type);
static uint32_t MmSizeOfMdl(void *, size_t);
static void *MmMapLockedPages(mdl *, uint8_t);
static void *MmMapLockedPagesSpecifyCache(mdl *,
	uint8_t, uint32_t, void *, uint32_t, uint32_t);
static void MmUnmapLockedPages(void *, mdl *);
static device_t ntoskrnl_finddev(device_t, uint64_t, struct resource **);
static void RtlZeroMemory(void *, size_t);
static void RtlSecureZeroMemory(void *, size_t);
static void RtlFillMemory(void *, size_t, uint8_t);
static void RtlMoveMemory(void *, const void *, size_t);
static ndis_status RtlCharToInteger(const char *, uint32_t, uint32_t *);
static void RtlCopyMemory(void *, const void *, size_t);
static size_t RtlCompareMemory(const void *, const void *, size_t);
static ndis_status RtlUnicodeStringToInteger(unicode_string *,
	uint32_t, uint32_t *);
static int atoi (const char *);
static long atol (const char *);
static int rand(void);
static void srand(unsigned int);
static void KeQuerySystemTime(uint64_t *);
static uint32_t KeTickCount(void);
static uint8_t IoIsWdmVersionAvailable(uint8_t, uint8_t);
static int32_t IoOpenDeviceRegistryKey(struct device_object *, uint32_t,
    uint32_t, void **);
static void ntoskrnl_thrfunc(void *);
static ndis_status PsCreateSystemThread(ndis_handle *,
	uint32_t, void *, ndis_handle, void *, void *, void *);
static ndis_status PsTerminateSystemThread(ndis_status);
static ndis_status IoGetDeviceObjectPointer(unicode_string *,
	uint32_t, void *, device_object *);
static ndis_status IoGetDeviceProperty(device_object *, uint32_t,
	uint32_t, void *, uint32_t *);
static void KeInitializeMutex(kmutant *, uint32_t);
static uint32_t KeReleaseMutex(kmutant *, uint8_t);
static uint32_t KeReadStateMutex(kmutant *);
static ndis_status ObReferenceObjectByHandle(ndis_handle,
	uint32_t, void *, uint8_t, void **, void **);
static void ObfDereferenceObject(void *);
static uint32_t ZwClose(ndis_handle);
static uint32_t WmiQueryTraceInformation(uint32_t, void *, uint32_t,
	uint32_t, void *);
static uint32_t WmiTraceMessage(uint64_t, uint32_t, void *, uint16_t, ...);
static uint32_t IoWMIRegistrationControl(device_object *, uint32_t);
static void *ntoskrnl_memset(void *, int, size_t);
static void *ntoskrnl_memmove(void *, void *, size_t);
static void *ntoskrnl_memchr(void *, unsigned char, size_t);
static char *ntoskrnl_strstr(char *, char *);
static char *ntoskrnl_strncat(char *, char *, size_t);
static int ntoskrnl_toupper(int);
static int ntoskrnl_tolower(int);
static funcptr ntoskrnl_findwrap(funcptr);
static uint32_t DbgPrint(char *, ...);
static void DbgBreakPoint(void);
static void KeBugCheckEx(uint32_t, u_long, u_long, u_long, u_long);
static int32_t KeDelayExecutionThread(uint8_t, uint8_t, int64_t *);
static int32_t KeSetPriorityThread(struct thread *, int32_t);
static void dummy(void);

static struct mtx ntoskrnl_dispatchlock;
static struct mtx ntoskrnl_interlock;
static kspin_lock ntoskrnl_cancellock;
static int ntoskrnl_kth = 0;
static struct nt_objref_head ntoskrnl_reflist;
static uma_zone_t mdl_zone;
static uma_zone_t iw_zone;
static struct kdpc_queue *kq_queues;
static struct kdpc_queue *wq_queues;
static int wq_idx = 0;

int
ntoskrnl_libinit()
{
	image_patch_table	*patch;
	int			error;
	struct proc		*p;
	kdpc_queue		*kq;
	callout_entry		*e;
	int			i;

	mtx_init(&ntoskrnl_dispatchlock,
	    "ntoskrnl dispatch lock", MTX_NDIS_LOCK, MTX_DEF|MTX_RECURSE);
	mtx_init(&ntoskrnl_interlock, MTX_NTOSKRNL_SPIN_LOCK, NULL, MTX_SPIN);
	KeInitializeSpinLock(&ntoskrnl_cancellock);
	KeInitializeSpinLock(&ntoskrnl_intlock);
	TAILQ_INIT(&ntoskrnl_reflist);

	InitializeListHead(&ntoskrnl_calllist);
	InitializeListHead(&ntoskrnl_intlist);
	mtx_init(&ntoskrnl_calllock, MTX_NTOSKRNL_SPIN_LOCK, NULL, MTX_SPIN);

	kq_queues = ExAllocatePoolWithTag(NonPagedPool,
#ifdef NTOSKRNL_MULTIPLE_DPCS
	    sizeof(kdpc_queue) * mp_ncpus, 0);
#else
	    sizeof(kdpc_queue), 0);
#endif

	if (kq_queues == NULL)
		return (ENOMEM);

	wq_queues = ExAllocatePoolWithTag(NonPagedPool,
	    sizeof(kdpc_queue) * WORKITEM_THREADS, 0);

	if (wq_queues == NULL)
		return (ENOMEM);

#ifdef NTOSKRNL_MULTIPLE_DPCS
	bzero((char *)kq_queues, sizeof(kdpc_queue) * mp_ncpus);
#else
	bzero((char *)kq_queues, sizeof(kdpc_queue));
#endif
	bzero((char *)wq_queues, sizeof(kdpc_queue) * WORKITEM_THREADS);

	/*
	 * Launch the DPC threads.
	 */

#ifdef NTOSKRNL_MULTIPLE_DPCS
	for (i = 0; i < mp_ncpus; i++) {
#else
	for (i = 0; i < 1; i++) {
#endif
		kq = kq_queues + i;
		kq->kq_cpu = i;
		error = kproc_create(ntoskrnl_dpc_thread, kq, &p,
		    RFHIGHPID, NDIS_KSTACK_PAGES, "Windows DPC %d", i);
		if (error)
			panic("failed to launch DPC thread");
	}

	/*
	 * Launch the workitem threads.
	 */

	for (i = 0; i < WORKITEM_THREADS; i++) {
		kq = wq_queues + i;
		error = kproc_create(ntoskrnl_workitem_thread, kq, &p,
		    RFHIGHPID, NDIS_KSTACK_PAGES, "Windows Workitem %d", i);
		if (error)
			panic("failed to launch workitem thread");
	}

	patch = ntoskrnl_functbl;
	while (patch->ipt_func != NULL) {
		windrv_wrap((funcptr)patch->ipt_func,
		    (funcptr *)&patch->ipt_wrap,
		    patch->ipt_argcnt, patch->ipt_ftype);
		patch++;
	}

	for (i = 0; i < NTOSKRNL_TIMEOUTS; i++) {
		e = ExAllocatePoolWithTag(NonPagedPool,
		    sizeof(callout_entry), 0);
		if (e == NULL)
			panic("failed to allocate timeouts");
		mtx_lock_spin(&ntoskrnl_calllock);
		InsertHeadList((&ntoskrnl_calllist), (&e->ce_list));
		mtx_unlock_spin(&ntoskrnl_calllock);
	}

	/*
	 * MDLs are supposed to be variable size (they describe
	 * buffers containing some number of pages, but we don't
	 * know ahead of time how many pages that will be). But
	 * always allocating them off the heap is very slow. As
	 * a compromise, we create an MDL UMA zone big enough to
	 * handle any buffer requiring up to 16 pages, and we
	 * use those for any MDLs for buffers of 16 pages or less
	 * in size. For buffers larger than that (which we assume
	 * will be few and far between, we allocate the MDLs off
	 * the heap.
	 */

	mdl_zone = uma_zcreate("Windows MDL", MDL_ZONE_SIZE,
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);

	iw_zone = uma_zcreate("Windows WorkItem", sizeof(io_workitem),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);

	return (0);
}

int
ntoskrnl_libfini()
{
	image_patch_table	*patch;
	callout_entry		*e;
	list_entry		*l;

	patch = ntoskrnl_functbl;
	while (patch->ipt_func != NULL) {
		windrv_unwrap(patch->ipt_wrap);
		patch++;
	}

	/* Stop the workitem queues. */
	ntoskrnl_destroy_workitem_threads();
	/* Stop the DPC queues. */
	ntoskrnl_destroy_dpc_threads();

	ExFreePool(kq_queues);
	ExFreePool(wq_queues);

	uma_zdestroy(mdl_zone);
	uma_zdestroy(iw_zone);

	mtx_lock_spin(&ntoskrnl_calllock);
	while(!IsListEmpty(&ntoskrnl_calllist)) {
		l = RemoveHeadList(&ntoskrnl_calllist);
		e = CONTAINING_RECORD(l, callout_entry, ce_list);
		mtx_unlock_spin(&ntoskrnl_calllock);
		ExFreePool(e);
		mtx_lock_spin(&ntoskrnl_calllock);
	}
	mtx_unlock_spin(&ntoskrnl_calllock);

	mtx_destroy(&ntoskrnl_dispatchlock);
	mtx_destroy(&ntoskrnl_interlock);
	mtx_destroy(&ntoskrnl_calllock);

	return (0);
}

/*
 * We need to be able to reference this externally from the wrapper;
 * GCC only generates a local implementation of memset.
 */
static void *
ntoskrnl_memset(buf, ch, size)
	void			*buf;
	int			ch;
	size_t			size;
{
	return (memset(buf, ch, size));
}

static void *
ntoskrnl_memmove(dst, src, size)
	void			*src;
	void			*dst;
	size_t			size;
{
	bcopy(src, dst, size);
	return (dst);
}

static void *
ntoskrnl_memchr(void *buf, unsigned char ch, size_t len)
{
	if (len != 0) {
		unsigned char *p = buf;

		do {
			if (*p++ == ch)
				return (p - 1);
		} while (--len != 0);
	}
	return (NULL);
}

static char *
ntoskrnl_strstr(s, find)
	char *s, *find;
{
	char c, sc;
	size_t len;

	if ((c = *find++) != 0) {
		len = strlen(find);
		do {
			do {
				if ((sc = *s++) == 0)
					return (NULL);
			} while (sc != c);
		} while (strncmp(s, find, len) != 0);
		s--;
	}
	return ((char *)s);
}

/* Taken from libc */
static char *
ntoskrnl_strncat(dst, src, n)
	char		*dst;
	char		*src;
	size_t		n;
{
	if (n != 0) {
		char *d = dst;
		const char *s = src;

		while (*d != 0)
			d++;
		do {
			if ((*d = *s++) == 0)
				break;
			d++;
		} while (--n != 0);
		*d = 0;
	}
	return (dst);
}

static int
ntoskrnl_toupper(c)
	int			c;
{
	return (toupper(c));
}

static int
ntoskrnl_tolower(c)
	int			c;
{
	return (tolower(c));
}

static uint8_t
RtlEqualUnicodeString(unicode_string *str1, unicode_string *str2,
	uint8_t caseinsensitive)
{
	int			i;

	if (str1->us_len != str2->us_len)
		return (FALSE);

	for (i = 0; i < str1->us_len; i++) {
		if (caseinsensitive == TRUE) {
			if (toupper((char)(str1->us_buf[i] & 0xFF)) !=
			    toupper((char)(str2->us_buf[i] & 0xFF)))
				return (FALSE);
		} else {
			if (str1->us_buf[i] != str2->us_buf[i])
				return (FALSE);
		}
	}

	return (TRUE);
}

static void
RtlCopyString(dst, src)
	ansi_string		*dst;
	const ansi_string	*src;
{
	if (src != NULL && src->as_buf != NULL && dst->as_buf != NULL) {
		dst->as_len = min(src->as_len, dst->as_maxlen);
		memcpy(dst->as_buf, src->as_buf, dst->as_len);
		if (dst->as_len < dst->as_maxlen)
			dst->as_buf[dst->as_len] = 0;
	} else
		dst->as_len = 0;
}

static void
RtlCopyUnicodeString(dest, src)
	unicode_string		*dest;
	unicode_string		*src;
{

	if (dest->us_maxlen >= src->us_len)
		dest->us_len = src->us_len;
	else
		dest->us_len = dest->us_maxlen;
	memcpy(dest->us_buf, src->us_buf, dest->us_len);
}

static void
ntoskrnl_ascii_to_unicode(ascii, unicode, len)
	char			*ascii;
	uint16_t		*unicode;
	int			len;
{
	int			i;
	uint16_t		*ustr;

	ustr = unicode;
	for (i = 0; i < len; i++) {
		*ustr = (uint16_t)ascii[i];
		ustr++;
	}
}

static void
ntoskrnl_unicode_to_ascii(unicode, ascii, len)
	uint16_t		*unicode;
	char			*ascii;
	int			len;
{
	int			i;
	uint8_t			*astr;

	astr = ascii;
	for (i = 0; i < len / 2; i++) {
		*astr = (uint8_t)unicode[i];
		astr++;
	}
}

uint32_t
RtlUnicodeStringToAnsiString(ansi_string *dest, unicode_string *src, uint8_t allocate)
{
	if (dest == NULL || src == NULL)
		return (STATUS_INVALID_PARAMETER);

	dest->as_len = src->us_len / 2;
	if (dest->as_maxlen < dest->as_len)
		dest->as_len = dest->as_maxlen;

	if (allocate == TRUE) {
		dest->as_buf = ExAllocatePoolWithTag(NonPagedPool,
		    (src->us_len / 2) + 1, 0);
		if (dest->as_buf == NULL)
			return (STATUS_INSUFFICIENT_RESOURCES);
		dest->as_len = dest->as_maxlen = src->us_len / 2;
	} else {
		dest->as_len = src->us_len / 2; /* XXX */
		if (dest->as_maxlen < dest->as_len)
			dest->as_len = dest->as_maxlen;
	}

	ntoskrnl_unicode_to_ascii(src->us_buf, dest->as_buf,
	    dest->as_len * 2);

	return (STATUS_SUCCESS);
}

uint32_t
RtlAnsiStringToUnicodeString(unicode_string *dest, ansi_string *src,
	uint8_t allocate)
{
	if (dest == NULL || src == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (allocate == TRUE) {
		dest->us_buf = ExAllocatePoolWithTag(NonPagedPool,
		    src->as_len * 2, 0);
		if (dest->us_buf == NULL)
			return (STATUS_INSUFFICIENT_RESOURCES);
		dest->us_len = dest->us_maxlen = strlen(src->as_buf) * 2;
	} else {
		dest->us_len = src->as_len * 2; /* XXX */
		if (dest->us_maxlen < dest->us_len)
			dest->us_len = dest->us_maxlen;
	}

	ntoskrnl_ascii_to_unicode(src->as_buf, dest->us_buf,
	    dest->us_len / 2);

	return (STATUS_SUCCESS);
}

void *
ExAllocatePoolWithTag(pooltype, len, tag)
	uint32_t		pooltype;
	size_t			len;
	uint32_t		tag;
{
	void			*buf;

	buf = malloc(len, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (buf == NULL)
		return (NULL);

	return (buf);
}

static void
ExFreePoolWithTag(buf, tag)
	void		*buf;
	uint32_t	tag;
{
	ExFreePool(buf);
}

void
ExFreePool(buf)
	void			*buf;
{
	free(buf, M_DEVBUF);
}

uint32_t
IoAllocateDriverObjectExtension(drv, clid, extlen, ext)
	driver_object		*drv;
	void			*clid;
	uint32_t		extlen;
	void			**ext;
{
	custom_extension	*ce;

	ce = ExAllocatePoolWithTag(NonPagedPool, sizeof(custom_extension)
	    + extlen, 0);

	if (ce == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	ce->ce_clid = clid;
	InsertTailList((&drv->dro_driverext->dre_usrext), (&ce->ce_list));

	*ext = (void *)(ce + 1);

	return (STATUS_SUCCESS);
}

void *
IoGetDriverObjectExtension(drv, clid)
	driver_object		*drv;
	void			*clid;
{
	list_entry		*e;
	custom_extension	*ce;

	/*
	 * Sanity check. Our dummy bus drivers don't have
	 * any driver extensions.
	 */

	if (drv->dro_driverext == NULL)
		return (NULL);

	e = drv->dro_driverext->dre_usrext.nle_flink;
	while (e != &drv->dro_driverext->dre_usrext) {
		ce = (custom_extension *)e;
		if (ce->ce_clid == clid)
			return ((void *)(ce + 1));
		e = e->nle_flink;
	}

	return (NULL);
}


uint32_t
IoCreateDevice(driver_object *drv, uint32_t devextlen, unicode_string *devname,
	uint32_t devtype, uint32_t devchars, uint8_t exclusive,
	device_object **newdev)
{
	device_object		*dev;

	dev = ExAllocatePoolWithTag(NonPagedPool, sizeof(device_object), 0);
	if (dev == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	dev->do_type = devtype;
	dev->do_drvobj = drv;
	dev->do_currirp = NULL;
	dev->do_flags = 0;

	if (devextlen) {
		dev->do_devext = ExAllocatePoolWithTag(NonPagedPool,
		    devextlen, 0);

		if (dev->do_devext == NULL) {
			ExFreePool(dev);
			return (STATUS_INSUFFICIENT_RESOURCES);
		}

		bzero(dev->do_devext, devextlen);
	} else
		dev->do_devext = NULL;

	dev->do_size = sizeof(device_object) + devextlen;
	dev->do_refcnt = 1;
	dev->do_attacheddev = NULL;
	dev->do_nextdev = NULL;
	dev->do_devtype = devtype;
	dev->do_stacksize = 1;
	dev->do_alignreq = 1;
	dev->do_characteristics = devchars;
	dev->do_iotimer = NULL;
	KeInitializeEvent(&dev->do_devlock, EVENT_TYPE_SYNC, TRUE);

	/*
	 * Vpd is used for disk/tape devices,
	 * but we don't support those. (Yet.)
	 */
	dev->do_vpb = NULL;

	dev->do_devobj_ext = ExAllocatePoolWithTag(NonPagedPool,
	    sizeof(devobj_extension), 0);

	if (dev->do_devobj_ext == NULL) {
		if (dev->do_devext != NULL)
			ExFreePool(dev->do_devext);
		ExFreePool(dev);
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	dev->do_devobj_ext->dve_type = 0;
	dev->do_devobj_ext->dve_size = sizeof(devobj_extension);
	dev->do_devobj_ext->dve_devobj = dev;

	/*
	 * Attach this device to the driver object's list
	 * of devices. Note: this is not the same as attaching
	 * the device to the device stack. The driver's AddDevice
	 * routine must explicitly call IoAddDeviceToDeviceStack()
	 * to do that.
	 */

	if (drv->dro_devobj == NULL) {
		drv->dro_devobj = dev;
		dev->do_nextdev = NULL;
	} else {
		dev->do_nextdev = drv->dro_devobj;
		drv->dro_devobj = dev;
	}

	*newdev = dev;

	return (STATUS_SUCCESS);
}

void
IoDeleteDevice(dev)
	device_object		*dev;
{
	device_object		*prev;

	if (dev == NULL)
		return;

	if (dev->do_devobj_ext != NULL)
		ExFreePool(dev->do_devobj_ext);

	if (dev->do_devext != NULL)
		ExFreePool(dev->do_devext);

	/* Unlink the device from the driver's device list. */

	prev = dev->do_drvobj->dro_devobj;
	if (prev == dev)
		dev->do_drvobj->dro_devobj = dev->do_nextdev;
	else {
		while (prev->do_nextdev != dev)
			prev = prev->do_nextdev;
		prev->do_nextdev = dev->do_nextdev;
	}

	ExFreePool(dev);
}

device_object *
IoGetAttachedDevice(dev)
	device_object		*dev;
{
	device_object		*d;

	if (dev == NULL)
		return (NULL);

	d = dev;

	while (d->do_attacheddev != NULL)
		d = d->do_attacheddev;

	return (d);
}

static irp *
IoBuildSynchronousFsdRequest(func, dobj, buf, len, off, event, status)
	uint32_t		func;
	device_object		*dobj;
	void			*buf;
	uint32_t		len;
	uint64_t		*off;
	nt_kevent		*event;
	io_status_block		*status;
{
	irp			*ip;

	ip = IoBuildAsynchronousFsdRequest(func, dobj, buf, len, off, status);
	if (ip == NULL)
		return (NULL);
	ip->irp_usrevent = event;

	return (ip);
}

static irp *
IoBuildAsynchronousFsdRequest(func, dobj, buf, len, off, status)
	uint32_t		func;
	device_object		*dobj;
	void			*buf;
	uint32_t		len;
	uint64_t		*off;
	io_status_block		*status;
{
	irp			*ip;
	io_stack_location	*sl;

	ip = IoAllocateIrp(dobj->do_stacksize, TRUE);
	if (ip == NULL)
		return (NULL);

	ip->irp_usriostat = status;
	ip->irp_tail.irp_overlay.irp_thread = NULL;

	sl = IoGetNextIrpStackLocation(ip);
	sl->isl_major = func;
	sl->isl_minor = 0;
	sl->isl_flags = 0;
	sl->isl_ctl = 0;
	sl->isl_devobj = dobj;
	sl->isl_fileobj = NULL;
	sl->isl_completionfunc = NULL;

	ip->irp_userbuf = buf;

	if (dobj->do_flags & DO_BUFFERED_IO) {
		ip->irp_assoc.irp_sysbuf =
		    ExAllocatePoolWithTag(NonPagedPool, len, 0);
		if (ip->irp_assoc.irp_sysbuf == NULL) {
			IoFreeIrp(ip);
			return (NULL);
		}
		bcopy(buf, ip->irp_assoc.irp_sysbuf, len);
	}

	if (dobj->do_flags & DO_DIRECT_IO) {
		ip->irp_mdl = IoAllocateMdl(buf, len, FALSE, FALSE, ip);
		if (ip->irp_mdl == NULL) {
			if (ip->irp_assoc.irp_sysbuf != NULL)
				ExFreePool(ip->irp_assoc.irp_sysbuf);
			IoFreeIrp(ip);
			return (NULL);
		}
		ip->irp_userbuf = NULL;
		ip->irp_assoc.irp_sysbuf = NULL;
	}

	if (func == IRP_MJ_READ) {
		sl->isl_parameters.isl_read.isl_len = len;
		if (off != NULL)
			sl->isl_parameters.isl_read.isl_byteoff = *off;
		else
			sl->isl_parameters.isl_read.isl_byteoff = 0;
	}

	if (func == IRP_MJ_WRITE) {
		sl->isl_parameters.isl_write.isl_len = len;
		if (off != NULL)
			sl->isl_parameters.isl_write.isl_byteoff = *off;
		else
			sl->isl_parameters.isl_write.isl_byteoff = 0;
	}

	return (ip);
}

static irp *
IoBuildDeviceIoControlRequest(uint32_t iocode, device_object *dobj, void *ibuf,
	uint32_t ilen, void *obuf, uint32_t olen, uint8_t isinternal,
	nt_kevent *event, io_status_block *status)
{
	irp			*ip;
	io_stack_location	*sl;
	uint32_t		buflen;

	ip = IoAllocateIrp(dobj->do_stacksize, TRUE);
	if (ip == NULL)
		return (NULL);
	ip->irp_usrevent = event;
	ip->irp_usriostat = status;
	ip->irp_tail.irp_overlay.irp_thread = NULL;

	sl = IoGetNextIrpStackLocation(ip);
	sl->isl_major = isinternal == TRUE ?
	    IRP_MJ_INTERNAL_DEVICE_CONTROL : IRP_MJ_DEVICE_CONTROL;
	sl->isl_minor = 0;
	sl->isl_flags = 0;
	sl->isl_ctl = 0;
	sl->isl_devobj = dobj;
	sl->isl_fileobj = NULL;
	sl->isl_completionfunc = NULL;
	sl->isl_parameters.isl_ioctl.isl_iocode = iocode;
	sl->isl_parameters.isl_ioctl.isl_ibuflen = ilen;
	sl->isl_parameters.isl_ioctl.isl_obuflen = olen;

	switch(IO_METHOD(iocode)) {
	case METHOD_BUFFERED:
		if (ilen > olen)
			buflen = ilen;
		else
			buflen = olen;
		if (buflen) {
			ip->irp_assoc.irp_sysbuf =
			    ExAllocatePoolWithTag(NonPagedPool, buflen, 0);
			if (ip->irp_assoc.irp_sysbuf == NULL) {
				IoFreeIrp(ip);
				return (NULL);
			}
		}
		if (ilen && ibuf != NULL) {
			bcopy(ibuf, ip->irp_assoc.irp_sysbuf, ilen);
			bzero((char *)ip->irp_assoc.irp_sysbuf + ilen,
			    buflen - ilen);
		} else
			bzero(ip->irp_assoc.irp_sysbuf, ilen);
		ip->irp_userbuf = obuf;
		break;
	case METHOD_IN_DIRECT:
	case METHOD_OUT_DIRECT:
		if (ilen && ibuf != NULL) {
			ip->irp_assoc.irp_sysbuf =
			    ExAllocatePoolWithTag(NonPagedPool, ilen, 0);
			if (ip->irp_assoc.irp_sysbuf == NULL) {
				IoFreeIrp(ip);
				return (NULL);
			}
			bcopy(ibuf, ip->irp_assoc.irp_sysbuf, ilen);
		}
		if (olen && obuf != NULL) {
			ip->irp_mdl = IoAllocateMdl(obuf, olen,
			    FALSE, FALSE, ip);
			/*
			 * Normally we would MmProbeAndLockPages()
			 * here, but we don't have to in our
			 * imlementation.
			 */
		}
		break;
	case METHOD_NEITHER:
		ip->irp_userbuf = obuf;
		sl->isl_parameters.isl_ioctl.isl_type3ibuf = ibuf;
		break;
	default:
		break;
	}

	/*
	 * Ideally, we should associate this IRP with the calling
	 * thread here.
	 */

	return (ip);
}

static irp *
IoAllocateIrp(uint8_t stsize, uint8_t chargequota)
{
	irp			*i;

	i = ExAllocatePoolWithTag(NonPagedPool, IoSizeOfIrp(stsize), 0);
	if (i == NULL)
		return (NULL);

	IoInitializeIrp(i, IoSizeOfIrp(stsize), stsize);

	return (i);
}

static irp *
IoMakeAssociatedIrp(irp *ip, uint8_t stsize)
{
	irp			*associrp;

	associrp = IoAllocateIrp(stsize, FALSE);
	if (associrp == NULL)
		return (NULL);

	mtx_lock(&ntoskrnl_dispatchlock);
	associrp->irp_flags |= IRP_ASSOCIATED_IRP;
	associrp->irp_tail.irp_overlay.irp_thread =
	    ip->irp_tail.irp_overlay.irp_thread;
	associrp->irp_assoc.irp_master = ip;
	mtx_unlock(&ntoskrnl_dispatchlock);

	return (associrp);
}

static void
IoFreeIrp(ip)
	irp			*ip;
{
	ExFreePool(ip);
}

static void
IoInitializeIrp(irp *io, uint16_t psize, uint8_t ssize)
{
	bzero((char *)io, IoSizeOfIrp(ssize));
	io->irp_size = psize;
	io->irp_stackcnt = ssize;
	io->irp_currentstackloc = ssize;
	InitializeListHead(&io->irp_thlist);
	io->irp_tail.irp_overlay.irp_csl =
	    (io_stack_location *)(io + 1) + ssize;
}

static void
IoReuseIrp(ip, status)
	irp			*ip;
	uint32_t		status;
{
	uint8_t			allocflags;

	allocflags = ip->irp_allocflags;
	IoInitializeIrp(ip, ip->irp_size, ip->irp_stackcnt);
	ip->irp_iostat.isb_status = status;
	ip->irp_allocflags = allocflags;
}

void
IoAcquireCancelSpinLock(uint8_t *irql)
{
	KeAcquireSpinLock(&ntoskrnl_cancellock, irql);
}

void
IoReleaseCancelSpinLock(uint8_t irql)
{
	KeReleaseSpinLock(&ntoskrnl_cancellock, irql);
}

uint8_t
IoCancelIrp(irp *ip)
{
	cancel_func		cfunc;
	uint8_t			cancelirql;

	IoAcquireCancelSpinLock(&cancelirql);
	cfunc = IoSetCancelRoutine(ip, NULL);
	ip->irp_cancel = TRUE;
	if (cfunc == NULL) {
		IoReleaseCancelSpinLock(cancelirql);
		return (FALSE);
	}
	ip->irp_cancelirql = cancelirql;
	MSCALL2(cfunc, IoGetCurrentIrpStackLocation(ip)->isl_devobj, ip);
	return (uint8_t)IoSetCancelValue(ip, TRUE);
}

uint32_t
IofCallDriver(dobj, ip)
	device_object		*dobj;
	irp			*ip;
{
	driver_object		*drvobj;
	io_stack_location	*sl;
	uint32_t		status;
	driver_dispatch		disp;

	drvobj = dobj->do_drvobj;

	if (ip->irp_currentstackloc <= 0)
		panic("IoCallDriver(): out of stack locations");

	IoSetNextIrpStackLocation(ip);
	sl = IoGetCurrentIrpStackLocation(ip);

	sl->isl_devobj = dobj;

	disp = drvobj->dro_dispatch[sl->isl_major];
	status = MSCALL2(disp, dobj, ip);

	return (status);
}

void
IofCompleteRequest(irp *ip, uint8_t prioboost)
{
	uint32_t		status;
	device_object		*dobj;
	io_stack_location	*sl;
	completion_func		cf;

	KASSERT(ip->irp_iostat.isb_status != STATUS_PENDING,
	    ("incorrect IRP(%p) status (STATUS_PENDING)", ip));

	sl = IoGetCurrentIrpStackLocation(ip);
	IoSkipCurrentIrpStackLocation(ip);

	do {
		if (sl->isl_ctl & SL_PENDING_RETURNED)
			ip->irp_pendingreturned = TRUE;

		if (ip->irp_currentstackloc != (ip->irp_stackcnt + 1))
			dobj = IoGetCurrentIrpStackLocation(ip)->isl_devobj;
		else
			dobj = NULL;

		if (sl->isl_completionfunc != NULL &&
		    ((ip->irp_iostat.isb_status == STATUS_SUCCESS &&
		    sl->isl_ctl & SL_INVOKE_ON_SUCCESS) ||
		    (ip->irp_iostat.isb_status != STATUS_SUCCESS &&
		    sl->isl_ctl & SL_INVOKE_ON_ERROR) ||
		    (ip->irp_cancel == TRUE &&
		    sl->isl_ctl & SL_INVOKE_ON_CANCEL))) {
			cf = sl->isl_completionfunc;
			status = MSCALL3(cf, dobj, ip, sl->isl_completionctx);
			if (status == STATUS_MORE_PROCESSING_REQUIRED)
				return;
		} else {
			if ((ip->irp_currentstackloc <= ip->irp_stackcnt) &&
			    (ip->irp_pendingreturned == TRUE))
				IoMarkIrpPending(ip);
		}

		/* move to the next.  */
		IoSkipCurrentIrpStackLocation(ip);
		sl++;
	} while (ip->irp_currentstackloc <= (ip->irp_stackcnt + 1));

	if (ip->irp_usriostat != NULL)
		*ip->irp_usriostat = ip->irp_iostat;
	if (ip->irp_usrevent != NULL)
		KeSetEvent(ip->irp_usrevent, prioboost, FALSE);

	/* Handle any associated IRPs. */

	if (ip->irp_flags & IRP_ASSOCIATED_IRP) {
		uint32_t		masterirpcnt;
		irp			*masterirp;
		mdl			*m;

		masterirp = ip->irp_assoc.irp_master;
		masterirpcnt =
		    InterlockedDecrement(&masterirp->irp_assoc.irp_irpcnt);

		while ((m = ip->irp_mdl) != NULL) {
			ip->irp_mdl = m->mdl_next;
			IoFreeMdl(m);
		}
		IoFreeIrp(ip);
		if (masterirpcnt == 0)
			IoCompleteRequest(masterirp, IO_NO_INCREMENT);
		return;
	}

	/* With any luck, these conditions will never arise. */

	if (ip->irp_flags & IRP_PAGING_IO) {
		if (ip->irp_mdl != NULL)
			IoFreeMdl(ip->irp_mdl);
		IoFreeIrp(ip);
	}
}

void
ntoskrnl_intr(arg)
	void			*arg;
{
	kinterrupt		*iobj;
	uint8_t			irql;
	uint8_t			claimed;
	list_entry		*l;

	KeAcquireSpinLock(&ntoskrnl_intlock, &irql);
	l = ntoskrnl_intlist.nle_flink;
	while (l != &ntoskrnl_intlist) {
		iobj = CONTAINING_RECORD(l, kinterrupt, ki_list);
		claimed = MSCALL2(iobj->ki_svcfunc, iobj, iobj->ki_svcctx);
		if (claimed == TRUE)
			break;
		l = l->nle_flink;
	}
	KeReleaseSpinLock(&ntoskrnl_intlock, irql);
}

uint8_t
KeAcquireInterruptSpinLock(iobj)
	kinterrupt		*iobj;
{
	uint8_t			irql;
	KeAcquireSpinLock(&ntoskrnl_intlock, &irql);
	return (irql);
}

void
KeReleaseInterruptSpinLock(kinterrupt *iobj, uint8_t irql)
{
	KeReleaseSpinLock(&ntoskrnl_intlock, irql);
}

uint8_t
KeSynchronizeExecution(iobj, syncfunc, syncctx)
	kinterrupt		*iobj;
	void			*syncfunc;
	void			*syncctx;
{
	uint8_t			irql;

	KeAcquireSpinLock(&ntoskrnl_intlock, &irql);
	MSCALL1(syncfunc, syncctx);
	KeReleaseSpinLock(&ntoskrnl_intlock, irql);

	return (TRUE);
}

/*
 * IoConnectInterrupt() is passed only the interrupt vector and
 * irql that a device wants to use, but no device-specific tag
 * of any kind. This conflicts rather badly with FreeBSD's
 * bus_setup_intr(), which needs the device_t for the device
 * requesting interrupt delivery. In order to bypass this
 * inconsistency, we implement a second level of interrupt
 * dispatching on top of bus_setup_intr(). All devices use
 * ntoskrnl_intr() as their ISR, and any device requesting
 * interrupts will be registered with ntoskrnl_intr()'s interrupt
 * dispatch list. When an interrupt arrives, we walk the list
 * and invoke all the registered ISRs. This effectively makes all
 * interrupts shared, but it's the only way to duplicate the
 * semantics of IoConnectInterrupt() and IoDisconnectInterrupt() properly.
 */

uint32_t
IoConnectInterrupt(kinterrupt **iobj, void *svcfunc, void *svcctx,
	kspin_lock *lock, uint32_t vector, uint8_t irql, uint8_t syncirql,
	uint8_t imode, uint8_t shared, uint32_t affinity, uint8_t savefloat)
{
	uint8_t			curirql;

	*iobj = ExAllocatePoolWithTag(NonPagedPool, sizeof(kinterrupt), 0);
	if (*iobj == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	(*iobj)->ki_svcfunc = svcfunc;
	(*iobj)->ki_svcctx = svcctx;

	if (lock == NULL) {
		KeInitializeSpinLock(&(*iobj)->ki_lock_priv);
		(*iobj)->ki_lock = &(*iobj)->ki_lock_priv;
	} else
		(*iobj)->ki_lock = lock;

	KeAcquireSpinLock(&ntoskrnl_intlock, &curirql);
	InsertHeadList((&ntoskrnl_intlist), (&(*iobj)->ki_list));
	KeReleaseSpinLock(&ntoskrnl_intlock, curirql);

	return (STATUS_SUCCESS);
}

void
IoDisconnectInterrupt(iobj)
	kinterrupt		*iobj;
{
	uint8_t			irql;

	if (iobj == NULL)
		return;

	KeAcquireSpinLock(&ntoskrnl_intlock, &irql);
	RemoveEntryList((&iobj->ki_list));
	KeReleaseSpinLock(&ntoskrnl_intlock, irql);

	ExFreePool(iobj);
}

device_object *
IoAttachDeviceToDeviceStack(src, dst)
	device_object		*src;
	device_object		*dst;
{
	device_object		*attached;

	mtx_lock(&ntoskrnl_dispatchlock);
	attached = IoGetAttachedDevice(dst);
	attached->do_attacheddev = src;
	src->do_attacheddev = NULL;
	src->do_stacksize = attached->do_stacksize + 1;
	mtx_unlock(&ntoskrnl_dispatchlock);

	return (attached);
}

void
IoDetachDevice(topdev)
	device_object		*topdev;
{
	device_object		*tail;

	mtx_lock(&ntoskrnl_dispatchlock);

	/* First, break the chain. */
	tail = topdev->do_attacheddev;
	if (tail == NULL) {
		mtx_unlock(&ntoskrnl_dispatchlock);
		return;
	}
	topdev->do_attacheddev = tail->do_attacheddev;
	topdev->do_refcnt--;

	/* Now reduce the stacksize count for the takm_il objects. */

	tail = topdev->do_attacheddev;
	while (tail != NULL) {
		tail->do_stacksize--;
		tail = tail->do_attacheddev;
	}

	mtx_unlock(&ntoskrnl_dispatchlock);
}

/*
 * For the most part, an object is considered signalled if
 * dh_sigstate == TRUE. The exception is for mutant objects
 * (mutexes), where the logic works like this:
 *
 * - If the thread already owns the object and sigstate is
 *   less than or equal to 0, then the object is considered
 *   signalled (recursive acquisition).
 * - If dh_sigstate == 1, the object is also considered
 *   signalled.
 */

static int
ntoskrnl_is_signalled(obj, td)
	nt_dispatch_header	*obj;
	struct thread		*td;
{
	kmutant			*km;

	if (obj->dh_type == DISP_TYPE_MUTANT) {
		km = (kmutant *)obj;
		if ((obj->dh_sigstate <= 0 && km->km_ownerthread == td) ||
		    obj->dh_sigstate == 1)
			return (TRUE);
		return (FALSE);
	}

	if (obj->dh_sigstate > 0)
		return (TRUE);
	return (FALSE);
}

static void
ntoskrnl_satisfy_wait(obj, td)
	nt_dispatch_header	*obj;
	struct thread		*td;
{
	kmutant			*km;

	switch (obj->dh_type) {
	case DISP_TYPE_MUTANT:
		km = (struct kmutant *)obj;
		obj->dh_sigstate--;
		/*
		 * If sigstate reaches 0, the mutex is now
		 * non-signalled (the new thread owns it).
		 */
		if (obj->dh_sigstate == 0) {
			km->km_ownerthread = td;
			if (km->km_abandoned == TRUE)
				km->km_abandoned = FALSE;
		}
		break;
	/* Synchronization objects get reset to unsignalled. */
	case DISP_TYPE_SYNCHRONIZATION_EVENT:
	case DISP_TYPE_SYNCHRONIZATION_TIMER:
		obj->dh_sigstate = 0;
		break;
	case DISP_TYPE_SEMAPHORE:
		obj->dh_sigstate--;
		break;
	default:
		break;
	}
}

static void
ntoskrnl_satisfy_multiple_waits(wb)
	wait_block		*wb;
{
	wait_block		*cur;
	struct thread		*td;

	cur = wb;
	td = wb->wb_kthread;

	do {
		ntoskrnl_satisfy_wait(wb->wb_object, td);
		cur->wb_awakened = TRUE;
		cur = cur->wb_next;
	} while (cur != wb);
}

/* Always called with dispatcher lock held. */
static void
ntoskrnl_waittest(obj, increment)
	nt_dispatch_header	*obj;
	uint32_t		increment;
{
	wait_block		*w, *next;
	list_entry		*e;
	struct thread		*td;
	wb_ext			*we;
	int			satisfied;

	/*
	 * Once an object has been signalled, we walk its list of
	 * wait blocks. If a wait block can be awakened, then satisfy
	 * waits as necessary and wake the thread.
	 *
	 * The rules work like this:
	 *
	 * If a wait block is marked as WAITTYPE_ANY, then
	 * we can satisfy the wait conditions on the current
	 * object and wake the thread right away. Satisfying
	 * the wait also has the effect of breaking us out
	 * of the search loop.
	 *
	 * If the object is marked as WAITTYLE_ALL, then the
	 * wait block will be part of a circularly linked
	 * list of wait blocks belonging to a waiting thread
	 * that's sleeping in KeWaitForMultipleObjects(). In
	 * order to wake the thread, all the objects in the
	 * wait list must be in the signalled state. If they
	 * are, we then satisfy all of them and wake the
	 * thread.
	 *
	 */

	e = obj->dh_waitlisthead.nle_flink;

	while (e != &obj->dh_waitlisthead && obj->dh_sigstate > 0) {
		w = CONTAINING_RECORD(e, wait_block, wb_waitlist);
		we = w->wb_ext;
		td = we->we_td;
		satisfied = FALSE;
		if (w->wb_waittype == WAITTYPE_ANY) {
			/*
			 * Thread can be awakened if
			 * any wait is satisfied.
			 */
			ntoskrnl_satisfy_wait(obj, td);
			satisfied = TRUE;
			w->wb_awakened = TRUE;
		} else {
			/*
			 * Thread can only be woken up
			 * if all waits are satisfied.
			 * If the thread is waiting on multiple
			 * objects, they should all be linked
			 * through the wb_next pointers in the
			 * wait blocks.
			 */
			satisfied = TRUE;
			next = w->wb_next;
			while (next != w) {
				if (ntoskrnl_is_signalled(obj, td) == FALSE) {
					satisfied = FALSE;
					break;
				}
				next = next->wb_next;
			}
			ntoskrnl_satisfy_multiple_waits(w);
		}

		if (satisfied == TRUE)
			cv_broadcastpri(&we->we_cv,
			    (w->wb_oldpri - (increment * 4)) > PRI_MIN_KERN ?
			    w->wb_oldpri - (increment * 4) : PRI_MIN_KERN);

		e = e->nle_flink;
	}
}

/*
 * Return the number of 100 nanosecond intervals since
 * January 1, 1601. (?!?!)
 */
void
ntoskrnl_time(tval)
	uint64_t                *tval;
{
	struct timespec		ts;

	nanotime(&ts);
	*tval = (uint64_t)ts.tv_nsec / 100 + (uint64_t)ts.tv_sec * 10000000 +
	    11644473600 * 10000000; /* 100ns ticks from 1601 to 1970 */
}

static void
KeQuerySystemTime(current_time)
	uint64_t		*current_time;
{
	ntoskrnl_time(current_time);
}

static uint32_t
KeTickCount(void)
{
	struct timeval tv;
	getmicrouptime(&tv);
	return tvtohz(&tv);
}


/*
 * KeWaitForSingleObject() is a tricky beast, because it can be used
 * with several different object types: semaphores, timers, events,
 * mutexes and threads. Semaphores don't appear very often, but the
 * other object types are quite common. KeWaitForSingleObject() is
 * what's normally used to acquire a mutex, and it can be used to
 * wait for a thread termination.
 *
 * The Windows NDIS API is implemented in terms of Windows kernel
 * primitives, and some of the object manipulation is duplicated in
 * NDIS. For example, NDIS has timers and events, which are actually
 * Windows kevents and ktimers. Now, you're supposed to only use the
 * NDIS variants of these objects within the confines of the NDIS API,
 * but there are some naughty developers out there who will use
 * KeWaitForSingleObject() on NDIS timer and event objects, so we
 * have to support that as well. Conseqently, our NDIS timer and event
 * code has to be closely tied into our ntoskrnl timer and event code,
 * just as it is in Windows.
 *
 * KeWaitForSingleObject() may do different things for different kinds
 * of objects:
 *
 * - For events, we check if the event has been signalled. If the
 *   event is already in the signalled state, we just return immediately,
 *   otherwise we wait for it to be set to the signalled state by someone
 *   else calling KeSetEvent(). Events can be either synchronization or
 *   notification events.
 *
 * - For timers, if the timer has already fired and the timer is in
 *   the signalled state, we just return, otherwise we wait on the
 *   timer. Unlike an event, timers get signalled automatically when
 *   they expire rather than someone having to trip them manually.
 *   Timers initialized with KeInitializeTimer() are always notification
 *   events: KeInitializeTimerEx() lets you initialize a timer as
 *   either a notification or synchronization event.
 *
 * - For mutexes, we try to acquire the mutex and if we can't, we wait
 *   on the mutex until it's available and then grab it. When a mutex is
 *   released, it enters the signalled state, which wakes up one of the
 *   threads waiting to acquire it. Mutexes are always synchronization
 *   events.
 *
 * - For threads, the only thing we do is wait until the thread object
 *   enters a signalled state, which occurs when the thread terminates.
 *   Threads are always notification events.
 *
 * A notification event wakes up all threads waiting on an object. A
 * synchronization event wakes up just one. Also, a synchronization event
 * is auto-clearing, which means we automatically set the event back to
 * the non-signalled state once the wakeup is done.
 */

uint32_t
KeWaitForSingleObject(void *arg, uint32_t reason, uint32_t mode,
    uint8_t alertable, int64_t *duetime)
{
	wait_block		w;
	struct thread		*td = curthread;
	struct timeval		tv;
	int			error = 0;
	uint64_t		curtime;
	wb_ext			we;
	nt_dispatch_header	*obj;

	obj = arg;

	if (obj == NULL)
		return (STATUS_INVALID_PARAMETER);

	mtx_lock(&ntoskrnl_dispatchlock);

	cv_init(&we.we_cv, "KeWFS");
	we.we_td = td;

	/*
	 * Check to see if this object is already signalled,
	 * and just return without waiting if it is.
	 */
	if (ntoskrnl_is_signalled(obj, td) == TRUE) {
		/* Sanity check the signal state value. */
		if (obj->dh_sigstate != INT32_MIN) {
			ntoskrnl_satisfy_wait(obj, curthread);
			mtx_unlock(&ntoskrnl_dispatchlock);
			return (STATUS_SUCCESS);
		} else {
			/*
			 * There's a limit to how many times we can
			 * recursively acquire a mutant. If we hit
			 * the limit, something is very wrong.
			 */
			if (obj->dh_type == DISP_TYPE_MUTANT) {
				mtx_unlock(&ntoskrnl_dispatchlock);
				panic("mutant limit exceeded");
			}
		}
	}

	bzero((char *)&w, sizeof(wait_block));
	w.wb_object = obj;
	w.wb_ext = &we;
	w.wb_waittype = WAITTYPE_ANY;
	w.wb_next = &w;
	w.wb_waitkey = 0;
	w.wb_awakened = FALSE;
	w.wb_oldpri = td->td_priority;

	InsertTailList((&obj->dh_waitlisthead), (&w.wb_waitlist));

	/*
	 * The timeout value is specified in 100 nanosecond units
	 * and can be a positive or negative number. If it's positive,
	 * then the duetime is absolute, and we need to convert it
	 * to an absolute offset relative to now in order to use it.
	 * If it's negative, then the duetime is relative and we
	 * just have to convert the units.
	 */

	if (duetime != NULL) {
		if (*duetime < 0) {
			tv.tv_sec = - (*duetime) / 10000000;
			tv.tv_usec = (- (*duetime) / 10) -
			    (tv.tv_sec * 1000000);
		} else {
			ntoskrnl_time(&curtime);
			if (*duetime < curtime)
				tv.tv_sec = tv.tv_usec = 0;
			else {
				tv.tv_sec = ((*duetime) - curtime) / 10000000;
				tv.tv_usec = ((*duetime) - curtime) / 10 -
				    (tv.tv_sec * 1000000);
			}
		}
	}

	if (duetime == NULL)
		cv_wait(&we.we_cv, &ntoskrnl_dispatchlock);
	else
		error = cv_timedwait(&we.we_cv,
		    &ntoskrnl_dispatchlock, tvtohz(&tv));

	RemoveEntryList(&w.wb_waitlist);

	cv_destroy(&we.we_cv);

	/* We timed out. Leave the object alone and return status. */

	if (error == EWOULDBLOCK) {
		mtx_unlock(&ntoskrnl_dispatchlock);
		return (STATUS_TIMEOUT);
	}

	mtx_unlock(&ntoskrnl_dispatchlock);

	return (STATUS_SUCCESS);
/*
	return (KeWaitForMultipleObjects(1, &obj, WAITTYPE_ALL, reason,
	    mode, alertable, duetime, &w));
*/
}

static uint32_t
KeWaitForMultipleObjects(uint32_t cnt, nt_dispatch_header *obj[], uint32_t wtype,
	uint32_t reason, uint32_t mode, uint8_t alertable, int64_t *duetime,
	wait_block *wb_array)
{
	struct thread		*td = curthread;
	wait_block		*whead, *w;
	wait_block		_wb_array[MAX_WAIT_OBJECTS];
	nt_dispatch_header	*cur;
	struct timeval		tv;
	int			i, wcnt = 0, error = 0;
	uint64_t		curtime;
	struct timespec		t1, t2;
	uint32_t		status = STATUS_SUCCESS;
	wb_ext			we;

	if (cnt > MAX_WAIT_OBJECTS)
		return (STATUS_INVALID_PARAMETER);
	if (cnt > THREAD_WAIT_OBJECTS && wb_array == NULL)
		return (STATUS_INVALID_PARAMETER);

	mtx_lock(&ntoskrnl_dispatchlock);

	cv_init(&we.we_cv, "KeWFM");
	we.we_td = td;

	if (wb_array == NULL)
		whead = _wb_array;
	else
		whead = wb_array;

	bzero((char *)whead, sizeof(wait_block) * cnt);

	/* First pass: see if we can satisfy any waits immediately. */

	wcnt = 0;
	w = whead;

	for (i = 0; i < cnt; i++) {
		InsertTailList((&obj[i]->dh_waitlisthead),
		    (&w->wb_waitlist));
		w->wb_ext = &we;
		w->wb_object = obj[i];
		w->wb_waittype = wtype;
		w->wb_waitkey = i;
		w->wb_awakened = FALSE;
		w->wb_oldpri = td->td_priority;
		w->wb_next = w + 1;
		w++;
		wcnt++;
		if (ntoskrnl_is_signalled(obj[i], td)) {
			/*
			 * There's a limit to how many times
			 * we can recursively acquire a mutant.
			 * If we hit the limit, something
			 * is very wrong.
			 */
			if (obj[i]->dh_sigstate == INT32_MIN &&
			    obj[i]->dh_type == DISP_TYPE_MUTANT) {
				mtx_unlock(&ntoskrnl_dispatchlock);
				panic("mutant limit exceeded");
			}

			/*
			 * If this is a WAITTYPE_ANY wait, then
			 * satisfy the waited object and exit
			 * right now.
			 */

			if (wtype == WAITTYPE_ANY) {
				ntoskrnl_satisfy_wait(obj[i], td);
				status = STATUS_WAIT_0 + i;
				goto wait_done;
			} else {
				w--;
				wcnt--;
				w->wb_object = NULL;
				RemoveEntryList(&w->wb_waitlist);
			}
		}
	}

	/*
	 * If this is a WAITTYPE_ALL wait and all objects are
	 * already signalled, satisfy the waits and exit now.
	 */

	if (wtype == WAITTYPE_ALL && wcnt == 0) {
		for (i = 0; i < cnt; i++)
			ntoskrnl_satisfy_wait(obj[i], td);
		status = STATUS_SUCCESS;
		goto wait_done;
	}

	/*
	 * Create a circular waitblock list. The waitcount
	 * must always be non-zero when we get here.
	 */

	(w - 1)->wb_next = whead;

	/* Wait on any objects that aren't yet signalled. */

	/* Calculate timeout, if any. */

	if (duetime != NULL) {
		if (*duetime < 0) {
			tv.tv_sec = - (*duetime) / 10000000;
			tv.tv_usec = (- (*duetime) / 10) -
			    (tv.tv_sec * 1000000);
		} else {
			ntoskrnl_time(&curtime);
			if (*duetime < curtime)
				tv.tv_sec = tv.tv_usec = 0;
			else {
				tv.tv_sec = ((*duetime) - curtime) / 10000000;
				tv.tv_usec = ((*duetime) - curtime) / 10 -
				    (tv.tv_sec * 1000000);
			}
		}
	}

	while (wcnt) {
		nanotime(&t1);

		if (duetime == NULL)
			cv_wait(&we.we_cv, &ntoskrnl_dispatchlock);
		else
			error = cv_timedwait(&we.we_cv,
			    &ntoskrnl_dispatchlock, tvtohz(&tv));

		/* Wait with timeout expired. */

		if (error) {
			status = STATUS_TIMEOUT;
			goto wait_done;
		}

		nanotime(&t2);

		/* See what's been signalled. */

		w = whead;
		do {
			cur = w->wb_object;
			if (ntoskrnl_is_signalled(cur, td) == TRUE ||
			    w->wb_awakened == TRUE) {
				/* Sanity check the signal state value. */
				if (cur->dh_sigstate == INT32_MIN &&
				    cur->dh_type == DISP_TYPE_MUTANT) {
					mtx_unlock(&ntoskrnl_dispatchlock);
					panic("mutant limit exceeded");
				}
				wcnt--;
				if (wtype == WAITTYPE_ANY) {
					status = w->wb_waitkey &
					    STATUS_WAIT_0;
					goto wait_done;
				}
			}
			w = w->wb_next;
		} while (w != whead);

		/*
		 * If all objects have been signalled, or if this
		 * is a WAITTYPE_ANY wait and we were woke up by
		 * someone, we can bail.
		 */

		if (wcnt == 0) {
			status = STATUS_SUCCESS;
			goto wait_done;
		}

		/*
		 * If this is WAITTYPE_ALL wait, and there's still
		 * objects that haven't been signalled, deduct the
		 * time that's elapsed so far from the timeout and
		 * wait again (or continue waiting indefinitely if
		 * there's no timeout).
		 */

		if (duetime != NULL) {
			tv.tv_sec -= (t2.tv_sec - t1.tv_sec);
			tv.tv_usec -= (t2.tv_nsec - t1.tv_nsec) / 1000;
		}
	}


wait_done:

	cv_destroy(&we.we_cv);

	for (i = 0; i < cnt; i++) {
		if (whead[i].wb_object != NULL)
			RemoveEntryList(&whead[i].wb_waitlist);

	}
	mtx_unlock(&ntoskrnl_dispatchlock);

	return (status);
}

static void
WRITE_REGISTER_USHORT(uint16_t *reg, uint16_t val)
{
	bus_space_write_2(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg, val);
}

static uint16_t
READ_REGISTER_USHORT(reg)
	uint16_t		*reg;
{
	return (bus_space_read_2(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg));
}

static void
WRITE_REGISTER_ULONG(reg, val)
	uint32_t		*reg;
	uint32_t		val;
{
	bus_space_write_4(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg, val);
}

static uint32_t
READ_REGISTER_ULONG(reg)
	uint32_t		*reg;
{
	return (bus_space_read_4(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg));
}

static uint8_t
READ_REGISTER_UCHAR(uint8_t *reg)
{
	return (bus_space_read_1(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg));
}

static void
WRITE_REGISTER_UCHAR(uint8_t *reg, uint8_t val)
{
	bus_space_write_1(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg, val);
}

static int64_t
_allmul(a, b)
	int64_t			a;
	int64_t			b;
{
	return (a * b);
}

static int64_t
_alldiv(a, b)
	int64_t			a;
	int64_t			b;
{
	return (a / b);
}

static int64_t
_allrem(a, b)
	int64_t			a;
	int64_t			b;
{
	return (a % b);
}

static uint64_t
_aullmul(a, b)
	uint64_t		a;
	uint64_t		b;
{
	return (a * b);
}

static uint64_t
_aulldiv(a, b)
	uint64_t		a;
	uint64_t		b;
{
	return (a / b);
}

static uint64_t
_aullrem(a, b)
	uint64_t		a;
	uint64_t		b;
{
	return (a % b);
}

static int64_t
_allshl(int64_t a, uint8_t b)
{
	return (a << b);
}

static uint64_t
_aullshl(uint64_t a, uint8_t b)
{
	return (a << b);
}

static int64_t
_allshr(int64_t a, uint8_t b)
{
	return (a >> b);
}

static uint64_t
_aullshr(uint64_t a, uint8_t b)
{
	return (a >> b);
}

static slist_entry *
ntoskrnl_pushsl(head, entry)
	slist_header		*head;
	slist_entry		*entry;
{
	slist_entry		*oldhead;

	oldhead = head->slh_list.slh_next;
	entry->sl_next = head->slh_list.slh_next;
	head->slh_list.slh_next = entry;
	head->slh_list.slh_depth++;
	head->slh_list.slh_seq++;

	return (oldhead);
}

static void
InitializeSListHead(head)
	slist_header		*head;
{
	memset(head, 0, sizeof(*head));
}

static slist_entry *
ntoskrnl_popsl(head)
	slist_header		*head;
{
	slist_entry		*first;

	first = head->slh_list.slh_next;
	if (first != NULL) {
		head->slh_list.slh_next = first->sl_next;
		head->slh_list.slh_depth--;
		head->slh_list.slh_seq++;
	}

	return (first);
}

/*
 * We need this to make lookaside lists work for amd64.
 * We pass a pointer to ExAllocatePoolWithTag() the lookaside
 * list structure. For amd64 to work right, this has to be a
 * pointer to the wrapped version of the routine, not the
 * original. Letting the Windows driver invoke the original
 * function directly will result in a convention calling
 * mismatch and a pretty crash. On x86, this effectively
 * becomes a no-op since ipt_func and ipt_wrap are the same.
 */

static funcptr
ntoskrnl_findwrap(func)
	funcptr			func;
{
	image_patch_table	*patch;

	patch = ntoskrnl_functbl;
	while (patch->ipt_func != NULL) {
		if ((funcptr)patch->ipt_func == func)
			return ((funcptr)patch->ipt_wrap);
		patch++;
	}

	return (NULL);
}

static void
ExInitializePagedLookasideList(paged_lookaside_list *lookaside,
	lookaside_alloc_func *allocfunc, lookaside_free_func *freefunc,
	uint32_t flags, size_t size, uint32_t tag, uint16_t depth)
{
	bzero((char *)lookaside, sizeof(paged_lookaside_list));

	if (size < sizeof(slist_entry))
		lookaside->nll_l.gl_size = sizeof(slist_entry);
	else
		lookaside->nll_l.gl_size = size;
	lookaside->nll_l.gl_tag = tag;
	if (allocfunc == NULL)
		lookaside->nll_l.gl_allocfunc =
		    ntoskrnl_findwrap((funcptr)ExAllocatePoolWithTag);
	else
		lookaside->nll_l.gl_allocfunc = allocfunc;

	if (freefunc == NULL)
		lookaside->nll_l.gl_freefunc =
		    ntoskrnl_findwrap((funcptr)ExFreePool);
	else
		lookaside->nll_l.gl_freefunc = freefunc;

#ifdef __i386__
	KeInitializeSpinLock(&lookaside->nll_obsoletelock);
#endif

	lookaside->nll_l.gl_type = NonPagedPool;
	lookaside->nll_l.gl_depth = depth;
	lookaside->nll_l.gl_maxdepth = LOOKASIDE_DEPTH;
}

static void
ExDeletePagedLookasideList(lookaside)
	paged_lookaside_list   *lookaside;
{
	void			*buf;
	void		(*freefunc)(void *);

	freefunc = lookaside->nll_l.gl_freefunc;
	while((buf = ntoskrnl_popsl(&lookaside->nll_l.gl_listhead)) != NULL)
		MSCALL1(freefunc, buf);
}

static void
ExInitializeNPagedLookasideList(npaged_lookaside_list *lookaside,
	lookaside_alloc_func *allocfunc, lookaside_free_func *freefunc,
	uint32_t flags, size_t size, uint32_t tag, uint16_t depth)
{
	bzero((char *)lookaside, sizeof(npaged_lookaside_list));

	if (size < sizeof(slist_entry))
		lookaside->nll_l.gl_size = sizeof(slist_entry);
	else
		lookaside->nll_l.gl_size = size;
	lookaside->nll_l.gl_tag = tag;
	if (allocfunc == NULL)
		lookaside->nll_l.gl_allocfunc =
		    ntoskrnl_findwrap((funcptr)ExAllocatePoolWithTag);
	else
		lookaside->nll_l.gl_allocfunc = allocfunc;

	if (freefunc == NULL)
		lookaside->nll_l.gl_freefunc =
		    ntoskrnl_findwrap((funcptr)ExFreePool);
	else
		lookaside->nll_l.gl_freefunc = freefunc;

#ifdef __i386__
	KeInitializeSpinLock(&lookaside->nll_obsoletelock);
#endif

	lookaside->nll_l.gl_type = NonPagedPool;
	lookaside->nll_l.gl_depth = depth;
	lookaside->nll_l.gl_maxdepth = LOOKASIDE_DEPTH;
}

static void
ExDeleteNPagedLookasideList(lookaside)
	npaged_lookaside_list   *lookaside;
{
	void			*buf;
	void		(*freefunc)(void *);

	freefunc = lookaside->nll_l.gl_freefunc;
	while((buf = ntoskrnl_popsl(&lookaside->nll_l.gl_listhead)) != NULL)
		MSCALL1(freefunc, buf);
}

slist_entry *
InterlockedPushEntrySList(head, entry)
	slist_header		*head;
	slist_entry		*entry;
{
	slist_entry		*oldhead;

	mtx_lock_spin(&ntoskrnl_interlock);
	oldhead = ntoskrnl_pushsl(head, entry);
	mtx_unlock_spin(&ntoskrnl_interlock);

	return (oldhead);
}

slist_entry *
InterlockedPopEntrySList(head)
	slist_header		*head;
{
	slist_entry		*first;

	mtx_lock_spin(&ntoskrnl_interlock);
	first = ntoskrnl_popsl(head);
	mtx_unlock_spin(&ntoskrnl_interlock);

	return (first);
}

static slist_entry *
ExInterlockedPushEntrySList(head, entry, lock)
	slist_header		*head;
	slist_entry		*entry;
	kspin_lock		*lock;
{
	return (InterlockedPushEntrySList(head, entry));
}

static slist_entry *
ExInterlockedPopEntrySList(head, lock)
	slist_header		*head;
	kspin_lock		*lock;
{
	return (InterlockedPopEntrySList(head));
}

uint16_t
ExQueryDepthSList(head)
	slist_header		*head;
{
	uint16_t		depth;

	mtx_lock_spin(&ntoskrnl_interlock);
	depth = head->slh_list.slh_depth;
	mtx_unlock_spin(&ntoskrnl_interlock);

	return (depth);
}

void
KeInitializeSpinLock(lock)
	kspin_lock		*lock;
{
	*lock = 0;
}

#ifdef __i386__
void
KefAcquireSpinLockAtDpcLevel(lock)
	kspin_lock		*lock;
{
#ifdef NTOSKRNL_DEBUG_SPINLOCKS
	int			i = 0;
#endif

	while (atomic_cmpset_acq_int((volatile u_int *)lock, 0, 1) == 0) {
		/* sit and spin */;
#ifdef NTOSKRNL_DEBUG_SPINLOCKS
		i++;
		if (i > 200000000)
			panic("DEADLOCK!");
#endif
	}
}

void
KefReleaseSpinLockFromDpcLevel(lock)
	kspin_lock		*lock;
{
	atomic_store_rel_int((volatile u_int *)lock, 0);
}

uint8_t
KeAcquireSpinLockRaiseToDpc(kspin_lock *lock)
{
	uint8_t			oldirql;

	if (KeGetCurrentIrql() > DISPATCH_LEVEL)
		panic("IRQL_NOT_LESS_THAN_OR_EQUAL");

	KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	KeAcquireSpinLockAtDpcLevel(lock);

	return (oldirql);
}
#else
void
KeAcquireSpinLockAtDpcLevel(kspin_lock *lock)
{
	while (atomic_cmpset_acq_int((volatile u_int *)lock, 0, 1) == 0)
		/* sit and spin */;
}

void
KeReleaseSpinLockFromDpcLevel(kspin_lock *lock)
{
	atomic_store_rel_int((volatile u_int *)lock, 0);
}
#endif /* __i386__ */

uintptr_t
InterlockedExchange(dst, val)
	volatile uint32_t	*dst;
	uintptr_t		val;
{
	uintptr_t		r;

	mtx_lock_spin(&ntoskrnl_interlock);
	r = *dst;
	*dst = val;
	mtx_unlock_spin(&ntoskrnl_interlock);

	return (r);
}

static uint32_t
InterlockedIncrement(addend)
	volatile uint32_t	*addend;
{
	atomic_add_long((volatile u_long *)addend, 1);
	return (*addend);
}

static uint32_t
InterlockedDecrement(addend)
	volatile uint32_t	*addend;
{
	atomic_subtract_long((volatile u_long *)addend, 1);
	return (*addend);
}

static void
ExInterlockedAddLargeStatistic(addend, inc)
	uint64_t		*addend;
	uint32_t		inc;
{
	mtx_lock_spin(&ntoskrnl_interlock);
	*addend += inc;
	mtx_unlock_spin(&ntoskrnl_interlock);
};

mdl *
IoAllocateMdl(void *vaddr, uint32_t len, uint8_t secondarybuf,
	uint8_t chargequota, irp *iopkt)
{
	mdl			*m;
	int			zone = 0;

	if (MmSizeOfMdl(vaddr, len) > MDL_ZONE_SIZE)
		m = ExAllocatePoolWithTag(NonPagedPool,
		    MmSizeOfMdl(vaddr, len), 0);
	else {
		m = uma_zalloc(mdl_zone, M_NOWAIT | M_ZERO);
		zone++;
	}

	if (m == NULL)
		return (NULL);

	MmInitializeMdl(m, vaddr, len);

	/*
	 * MmInitializMdl() clears the flags field, so we
	 * have to set this here. If the MDL came from the
	 * MDL UMA zone, tag it so we can release it to
	 * the right place later.
	 */
	if (zone)
		m->mdl_flags = MDL_ZONE_ALLOCED;

	if (iopkt != NULL) {
		if (secondarybuf == TRUE) {
			mdl			*last;
			last = iopkt->irp_mdl;
			while (last->mdl_next != NULL)
				last = last->mdl_next;
			last->mdl_next = m;
		} else {
			if (iopkt->irp_mdl != NULL)
				panic("leaking an MDL in IoAllocateMdl()");
			iopkt->irp_mdl = m;
		}
	}

	return (m);
}

void
IoFreeMdl(m)
	mdl			*m;
{
	if (m == NULL)
		return;

	if (m->mdl_flags & MDL_ZONE_ALLOCED)
		uma_zfree(mdl_zone, m);
	else
		ExFreePool(m);
}

static void *
MmAllocateContiguousMemory(size, highest)
	uint32_t		size;
	uint64_t		highest;
{
	void *addr;
	size_t pagelength = roundup(size, PAGE_SIZE);

	addr = ExAllocatePoolWithTag(NonPagedPool, pagelength, 0);

	return (addr);
}

static void *
MmAllocateContiguousMemorySpecifyCache(size, lowest, highest,
    boundary, cachetype)
	uint32_t		size;
	uint64_t		lowest;
	uint64_t		highest;
	uint64_t		boundary;
	enum nt_caching_type	cachetype;
{
	vm_memattr_t		memattr;
	void			*ret;

	switch (cachetype) {
	case MmNonCached:
		memattr = VM_MEMATTR_UNCACHEABLE;
		break;
	case MmWriteCombined:
		memattr = VM_MEMATTR_WRITE_COMBINING;
		break;
	case MmNonCachedUnordered:
		memattr = VM_MEMATTR_UNCACHEABLE;
		break;
	case MmCached:
	case MmHardwareCoherentCached:
	case MmUSWCCached:
	default:
		memattr = VM_MEMATTR_DEFAULT;
		break;
	}

	ret = (void *)kmem_alloc_contig(size, M_ZERO | M_NOWAIT, lowest,
	    highest, PAGE_SIZE, boundary, memattr);
	if (ret != NULL)
		malloc_type_allocated(M_DEVBUF, round_page(size));
	return (ret);
}

static void
MmFreeContiguousMemory(base)
	void			*base;
{
	ExFreePool(base);
}

static void
MmFreeContiguousMemorySpecifyCache(base, size, cachetype)
	void			*base;
	uint32_t		size;
	enum nt_caching_type	cachetype;
{
	contigfree(base, size, M_DEVBUF);
}

static uint32_t
MmSizeOfMdl(vaddr, len)
	void			*vaddr;
	size_t			len;
{
	uint32_t		l;

	l = sizeof(struct mdl) +
	    (sizeof(vm_offset_t *) * SPAN_PAGES(vaddr, len));

	return (l);
}

/*
 * The Microsoft documentation says this routine fills in the
 * page array of an MDL with the _physical_ page addresses that
 * comprise the buffer, but we don't really want to do that here.
 * Instead, we just fill in the page array with the kernel virtual
 * addresses of the buffers.
 */
void
MmBuildMdlForNonPagedPool(m)
	mdl			*m;
{
	vm_offset_t		*mdl_pages;
	int			pagecnt, i;

	pagecnt = SPAN_PAGES(m->mdl_byteoffset, m->mdl_bytecount);

	if (pagecnt > (m->mdl_size - sizeof(mdl)) / sizeof(vm_offset_t *))
		panic("not enough pages in MDL to describe buffer");

	mdl_pages = MmGetMdlPfnArray(m);

	for (i = 0; i < pagecnt; i++)
		*mdl_pages = (vm_offset_t)m->mdl_startva + (i * PAGE_SIZE);

	m->mdl_flags |= MDL_SOURCE_IS_NONPAGED_POOL;
	m->mdl_mappedsystemva = MmGetMdlVirtualAddress(m);
}

static void *
MmMapLockedPages(mdl *buf, uint8_t accessmode)
{
	buf->mdl_flags |= MDL_MAPPED_TO_SYSTEM_VA;
	return (MmGetMdlVirtualAddress(buf));
}

static void *
MmMapLockedPagesSpecifyCache(mdl *buf, uint8_t accessmode, uint32_t cachetype,
	void *vaddr, uint32_t bugcheck, uint32_t prio)
{
	return (MmMapLockedPages(buf, accessmode));
}

static void
MmUnmapLockedPages(vaddr, buf)
	void			*vaddr;
	mdl			*buf;
{
	buf->mdl_flags &= ~MDL_MAPPED_TO_SYSTEM_VA;
}

/*
 * This function has a problem in that it will break if you
 * compile this module without PAE and try to use it on a PAE
 * kernel. Unfortunately, there's no way around this at the
 * moment. It's slightly less broken that using pmap_kextract().
 * You'd think the virtual memory subsystem would help us out
 * here, but it doesn't.
 */

static uint64_t
MmGetPhysicalAddress(void *base)
{
	return (pmap_extract(kernel_map->pmap, (vm_offset_t)base));
}

void *
MmGetSystemRoutineAddress(ustr)
	unicode_string		*ustr;
{
	ansi_string		astr;

	if (RtlUnicodeStringToAnsiString(&astr, ustr, TRUE))
		return (NULL);
	return (ndis_get_routine_address(ntoskrnl_functbl, astr.as_buf));
}

uint8_t
MmIsAddressValid(vaddr)
	void			*vaddr;
{
	if (pmap_extract(kernel_map->pmap, (vm_offset_t)vaddr))
		return (TRUE);

	return (FALSE);
}

void *
MmMapIoSpace(paddr, len, cachetype)
	uint64_t		paddr;
	uint32_t		len;
	uint32_t		cachetype;
{
	devclass_t		nexus_class;
	device_t		*nexus_devs, devp;
	int			nexus_count = 0;
	device_t		matching_dev = NULL;
	struct resource		*res;
	int			i;
	vm_offset_t		v;

	/* There will always be at least one nexus. */

	nexus_class = devclass_find("nexus");
	devclass_get_devices(nexus_class, &nexus_devs, &nexus_count);

	for (i = 0; i < nexus_count; i++) {
		devp = nexus_devs[i];
		matching_dev = ntoskrnl_finddev(devp, paddr, &res);
		if (matching_dev)
			break;
	}

	free(nexus_devs, M_TEMP);

	if (matching_dev == NULL)
		return (NULL);

	v = (vm_offset_t)rman_get_virtual(res);
	if (paddr > rman_get_start(res))
		v += paddr - rman_get_start(res);

	return ((void *)v);
}

void
MmUnmapIoSpace(vaddr, len)
	void			*vaddr;
	size_t			len;
{
}


static device_t
ntoskrnl_finddev(dev, paddr, res)
	device_t		dev;
	uint64_t		paddr;
	struct resource		**res;
{
	device_t		*children = NULL;
	device_t		matching_dev;
	int			childcnt;
	struct resource		*r;
	struct resource_list	*rl;
	struct resource_list_entry	*rle;
	uint32_t		flags;
	int			i;

	/* We only want devices that have been successfully probed. */

	if (device_is_alive(dev) == FALSE)
		return (NULL);

	rl = BUS_GET_RESOURCE_LIST(device_get_parent(dev), dev);
	if (rl != NULL) {
		STAILQ_FOREACH(rle, rl, link) {
			r = rle->res;

			if (r == NULL)
				continue;

			flags = rman_get_flags(r);

			if (rle->type == SYS_RES_MEMORY &&
			    paddr >= rman_get_start(r) &&
			    paddr <= rman_get_end(r)) {
				if (!(flags & RF_ACTIVE))
					bus_activate_resource(dev,
					    SYS_RES_MEMORY, 0, r);
				*res = r;
				return (dev);
			}
		}
	}

	/*
	 * If this device has children, do another
	 * level of recursion to inspect them.
	 */

	device_get_children(dev, &children, &childcnt);

	for (i = 0; i < childcnt; i++) {
		matching_dev = ntoskrnl_finddev(children[i], paddr, res);
		if (matching_dev != NULL) {
			free(children, M_TEMP);
			return (matching_dev);
		}
	}


	/* Won't somebody please think of the children! */

	if (children != NULL)
		free(children, M_TEMP);

	return (NULL);
}

/*
 * Workitems are unlike DPCs, in that they run in a user-mode thread
 * context rather than at DISPATCH_LEVEL in kernel context. In our
 * case we run them in kernel context anyway.
 */
static void
ntoskrnl_workitem_thread(arg)
	void			*arg;
{
	kdpc_queue		*kq;
	list_entry		*l;
	io_workitem		*iw;
	uint8_t			irql;

	kq = arg;

	InitializeListHead(&kq->kq_disp);
	kq->kq_td = curthread;
	kq->kq_exit = 0;
	KeInitializeSpinLock(&kq->kq_lock);
	KeInitializeEvent(&kq->kq_proc, EVENT_TYPE_SYNC, FALSE);

	while (1) {
		KeWaitForSingleObject(&kq->kq_proc, 0, 0, TRUE, NULL);

		KeAcquireSpinLock(&kq->kq_lock, &irql);

		if (kq->kq_exit) {
			kq->kq_exit = 0;
			KeReleaseSpinLock(&kq->kq_lock, irql);
			break;
		}

		while (!IsListEmpty(&kq->kq_disp)) {
			l = RemoveHeadList(&kq->kq_disp);
			iw = CONTAINING_RECORD(l,
			    io_workitem, iw_listentry);
			InitializeListHead((&iw->iw_listentry));
			if (iw->iw_func == NULL)
				continue;
			KeReleaseSpinLock(&kq->kq_lock, irql);
			MSCALL2(iw->iw_func, iw->iw_dobj, iw->iw_ctx);
			KeAcquireSpinLock(&kq->kq_lock, &irql);
		}

		KeReleaseSpinLock(&kq->kq_lock, irql);
	}

	kproc_exit(0);
	return; /* notreached */
}

static ndis_status
RtlCharToInteger(src, base, val)
	const char		*src;
	uint32_t		base;
	uint32_t		*val;
{
	int negative = 0;
	uint32_t res;

	if (!src || !val)
		return (STATUS_ACCESS_VIOLATION);
	while (*src != '\0' && *src <= ' ')
		src++;
	if (*src == '+')
		src++;
	else if (*src == '-') {
		src++;
		negative = 1;
	}
	if (base == 0) {
		base = 10;
		if (*src == '0') {
			src++;
			if (*src == 'b') {
				base = 2;
				src++;
			} else if (*src == 'o') {
				base = 8;
				src++;
			} else if (*src == 'x') {
				base = 16;
				src++;
			}
		}
	} else if (!(base == 2 || base == 8 || base == 10 || base == 16))
		return (STATUS_INVALID_PARAMETER);

	for (res = 0; *src; src++) {
		int v;
		if (isdigit(*src))
			v = *src - '0';
		else if (isxdigit(*src))
			v = tolower(*src) - 'a' + 10;
		else
			v = base;
		if (v >= base)
			return (STATUS_INVALID_PARAMETER);
		res = res * base + v;
	}
	*val = negative ? -res : res;
	return (STATUS_SUCCESS);
}

static void
ntoskrnl_destroy_workitem_threads(void)
{
	kdpc_queue		*kq;
	int			i;

	for (i = 0; i < WORKITEM_THREADS; i++) {
		kq = wq_queues + i;
		kq->kq_exit = 1;
		KeSetEvent(&kq->kq_proc, IO_NO_INCREMENT, FALSE);
		while (kq->kq_exit)
			tsleep(kq->kq_td->td_proc, PWAIT, "waitiw", hz/10);
	}
}

io_workitem *
IoAllocateWorkItem(dobj)
	device_object		*dobj;
{
	io_workitem		*iw;

	iw = uma_zalloc(iw_zone, M_NOWAIT);
	if (iw == NULL)
		return (NULL);

	InitializeListHead(&iw->iw_listentry);
	iw->iw_dobj = dobj;

	mtx_lock(&ntoskrnl_dispatchlock);
	iw->iw_idx = wq_idx;
	WORKIDX_INC(wq_idx);
	mtx_unlock(&ntoskrnl_dispatchlock);

	return (iw);
}

void
IoFreeWorkItem(iw)
	io_workitem		*iw;
{
	uma_zfree(iw_zone, iw);
}

void
IoQueueWorkItem(iw, iw_func, qtype, ctx)
	io_workitem		*iw;
	io_workitem_func	iw_func;
	uint32_t		qtype;
	void			*ctx;
{
	kdpc_queue		*kq;
	list_entry		*l;
	io_workitem		*cur;
	uint8_t			irql;

	kq = wq_queues + iw->iw_idx;

	KeAcquireSpinLock(&kq->kq_lock, &irql);

	/*
	 * Traverse the list and make sure this workitem hasn't
	 * already been inserted. Queuing the same workitem
	 * twice will hose the list but good.
	 */

	l = kq->kq_disp.nle_flink;
	while (l != &kq->kq_disp) {
		cur = CONTAINING_RECORD(l, io_workitem, iw_listentry);
		if (cur == iw) {
			/* Already queued -- do nothing. */
			KeReleaseSpinLock(&kq->kq_lock, irql);
			return;
		}
		l = l->nle_flink;
	}

	iw->iw_func = iw_func;
	iw->iw_ctx = ctx;

	InsertTailList((&kq->kq_disp), (&iw->iw_listentry));
	KeReleaseSpinLock(&kq->kq_lock, irql);

	KeSetEvent(&kq->kq_proc, IO_NO_INCREMENT, FALSE);
}

static void
ntoskrnl_workitem(dobj, arg)
	device_object		*dobj;
	void			*arg;
{
	io_workitem		*iw;
	work_queue_item		*w;
	work_item_func		f;

	iw = arg;
	w = (work_queue_item *)dobj;
	f = (work_item_func)w->wqi_func;
	uma_zfree(iw_zone, iw);
	MSCALL2(f, w, w->wqi_ctx);
}

/*
 * The ExQueueWorkItem() API is deprecated in Windows XP. Microsoft
 * warns that it's unsafe and to use IoQueueWorkItem() instead. The
 * problem with ExQueueWorkItem() is that it can't guard against
 * the condition where a driver submits a job to the work queue and
 * is then unloaded before the job is able to run. IoQueueWorkItem()
 * acquires a reference to the device's device_object via the
 * object manager and retains it until after the job has completed,
 * which prevents the driver from being unloaded before the job
 * runs. (We don't currently support this behavior, though hopefully
 * that will change once the object manager API is fleshed out a bit.)
 *
 * Having said all that, the ExQueueWorkItem() API remains, because
 * there are still other parts of Windows that use it, including
 * NDIS itself: NdisScheduleWorkItem() calls ExQueueWorkItem().
 * We fake up the ExQueueWorkItem() API on top of our implementation
 * of IoQueueWorkItem(). Workitem thread #3 is reserved exclusively
 * for ExQueueWorkItem() jobs, and we pass a pointer to the work
 * queue item (provided by the caller) in to IoAllocateWorkItem()
 * instead of the device_object. We need to save this pointer so
 * we can apply a sanity check: as with the DPC queue and other
 * workitem queues, we can't allow the same work queue item to
 * be queued twice. If it's already pending, we silently return
 */

void
ExQueueWorkItem(w, qtype)
	work_queue_item		*w;
	uint32_t		qtype;
{
	io_workitem		*iw;
	io_workitem_func	iwf;
	kdpc_queue		*kq;
	list_entry		*l;
	io_workitem		*cur;
	uint8_t			irql;


	/*
	 * We need to do a special sanity test to make sure
	 * the ExQueueWorkItem() API isn't used to queue
	 * the same workitem twice. Rather than checking the
	 * io_workitem pointer itself, we test the attached
	 * device object, which is really a pointer to the
	 * legacy work queue item structure.
	 */

	kq = wq_queues + WORKITEM_LEGACY_THREAD;
	KeAcquireSpinLock(&kq->kq_lock, &irql);
	l = kq->kq_disp.nle_flink;
	while (l != &kq->kq_disp) {
		cur = CONTAINING_RECORD(l, io_workitem, iw_listentry);
		if (cur->iw_dobj == (device_object *)w) {
			/* Already queued -- do nothing. */
			KeReleaseSpinLock(&kq->kq_lock, irql);
			return;
		}
		l = l->nle_flink;
	}
	KeReleaseSpinLock(&kq->kq_lock, irql);

	iw = IoAllocateWorkItem((device_object *)w);
	if (iw == NULL)
		return;

	iw->iw_idx = WORKITEM_LEGACY_THREAD;
	iwf = (io_workitem_func)ntoskrnl_findwrap((funcptr)ntoskrnl_workitem);
	IoQueueWorkItem(iw, iwf, qtype, iw);
}

static void
RtlZeroMemory(dst, len)
	void			*dst;
	size_t			len;
{
	bzero(dst, len);
}

static void
RtlSecureZeroMemory(dst, len)
	void			*dst;
	size_t			len;
{
	memset(dst, 0, len);
}

static void
RtlFillMemory(void *dst, size_t len, uint8_t c)
{
	memset(dst, c, len);
}

static void
RtlMoveMemory(dst, src, len)
	void			*dst;
	const void		*src;
	size_t			len;
{
	memmove(dst, src, len);
}

static void
RtlCopyMemory(dst, src, len)
	void			*dst;
	const void		*src;
	size_t			len;
{
	bcopy(src, dst, len);
}

static size_t
RtlCompareMemory(s1, s2, len)
	const void		*s1;
	const void		*s2;
	size_t			len;
{
	size_t			i;
	uint8_t			*m1, *m2;

	m1 = __DECONST(char *, s1);
	m2 = __DECONST(char *, s2);

	for (i = 0; i < len && m1[i] == m2[i]; i++);
	return (i);
}

void
RtlInitAnsiString(dst, src)
	ansi_string		*dst;
	char			*src;
{
	ansi_string		*a;

	a = dst;
	if (a == NULL)
		return;
	if (src == NULL) {
		a->as_len = a->as_maxlen = 0;
		a->as_buf = NULL;
	} else {
		a->as_buf = src;
		a->as_len = a->as_maxlen = strlen(src);
	}
}

void
RtlInitUnicodeString(dst, src)
	unicode_string		*dst;
	uint16_t		*src;
{
	unicode_string		*u;
	int			i;

	u = dst;
	if (u == NULL)
		return;
	if (src == NULL) {
		u->us_len = u->us_maxlen = 0;
		u->us_buf = NULL;
	} else {
		i = 0;
		while(src[i] != 0)
			i++;
		u->us_buf = src;
		u->us_len = u->us_maxlen = i * 2;
	}
}

ndis_status
RtlUnicodeStringToInteger(ustr, base, val)
	unicode_string		*ustr;
	uint32_t		base;
	uint32_t		*val;
{
	uint16_t		*uchr;
	int			len, neg = 0;
	char			abuf[64];
	char			*astr;

	uchr = ustr->us_buf;
	len = ustr->us_len;
	bzero(abuf, sizeof(abuf));

	if ((char)((*uchr) & 0xFF) == '-') {
		neg = 1;
		uchr++;
		len -= 2;
	} else if ((char)((*uchr) & 0xFF) == '+') {
		neg = 0;
		uchr++;
		len -= 2;
	}

	if (base == 0) {
		if ((char)((*uchr) & 0xFF) == 'b') {
			base = 2;
			uchr++;
			len -= 2;
		} else if ((char)((*uchr) & 0xFF) == 'o') {
			base = 8;
			uchr++;
			len -= 2;
		} else if ((char)((*uchr) & 0xFF) == 'x') {
			base = 16;
			uchr++;
			len -= 2;
		} else
			base = 10;
	}

	astr = abuf;
	if (neg) {
		strcpy(astr, "-");
		astr++;
	}

	ntoskrnl_unicode_to_ascii(uchr, astr, len);
	*val = strtoul(abuf, NULL, base);

	return (STATUS_SUCCESS);
}

void
RtlFreeUnicodeString(ustr)
	unicode_string		*ustr;
{
	if (ustr->us_buf == NULL)
		return;
	ExFreePool(ustr->us_buf);
	ustr->us_buf = NULL;
}

void
RtlFreeAnsiString(astr)
	ansi_string		*astr;
{
	if (astr->as_buf == NULL)
		return;
	ExFreePool(astr->as_buf);
	astr->as_buf = NULL;
}

static int
atoi(str)
	const char		*str;
{
	return (int)strtol(str, (char **)NULL, 10);
}

static long
atol(str)
	const char		*str;
{
	return strtol(str, (char **)NULL, 10);
}

static int
rand(void)
{

	return (random());
}

static void
srand(unsigned int seed)
{

	srandom(seed);
}

static uint8_t
IoIsWdmVersionAvailable(uint8_t major, uint8_t minor)
{
	if (major == WDM_MAJOR && minor == WDM_MINOR_WINXP)
		return (TRUE);
	return (FALSE);
}

static int32_t
IoOpenDeviceRegistryKey(struct device_object *devobj, uint32_t type,
    uint32_t mask, void **key)
{
	return (NDIS_STATUS_INVALID_DEVICE_REQUEST);
}

static ndis_status
IoGetDeviceObjectPointer(name, reqaccess, fileobj, devobj)
	unicode_string		*name;
	uint32_t		reqaccess;
	void			*fileobj;
	device_object		*devobj;
{
	return (STATUS_SUCCESS);
}

static ndis_status
IoGetDeviceProperty(devobj, regprop, buflen, prop, reslen)
	device_object		*devobj;
	uint32_t		regprop;
	uint32_t		buflen;
	void			*prop;
	uint32_t		*reslen;
{
	driver_object		*drv;
	uint16_t		**name;

	drv = devobj->do_drvobj;

	switch (regprop) {
	case DEVPROP_DRIVER_KEYNAME:
		name = prop;
		*name = drv->dro_drivername.us_buf;
		*reslen = drv->dro_drivername.us_len;
		break;
	default:
		return (STATUS_INVALID_PARAMETER_2);
		break;
	}

	return (STATUS_SUCCESS);
}

static void
KeInitializeMutex(kmutex, level)
	kmutant			*kmutex;
	uint32_t		level;
{
	InitializeListHead((&kmutex->km_header.dh_waitlisthead));
	kmutex->km_abandoned = FALSE;
	kmutex->km_apcdisable = 1;
	kmutex->km_header.dh_sigstate = 1;
	kmutex->km_header.dh_type = DISP_TYPE_MUTANT;
	kmutex->km_header.dh_size = sizeof(kmutant) / sizeof(uint32_t);
	kmutex->km_ownerthread = NULL;
}

static uint32_t
KeReleaseMutex(kmutant *kmutex, uint8_t kwait)
{
	uint32_t		prevstate;

	mtx_lock(&ntoskrnl_dispatchlock);
	prevstate = kmutex->km_header.dh_sigstate;
	if (kmutex->km_ownerthread != curthread) {
		mtx_unlock(&ntoskrnl_dispatchlock);
		return (STATUS_MUTANT_NOT_OWNED);
	}

	kmutex->km_header.dh_sigstate++;
	kmutex->km_abandoned = FALSE;

	if (kmutex->km_header.dh_sigstate == 1) {
		kmutex->km_ownerthread = NULL;
		ntoskrnl_waittest(&kmutex->km_header, IO_NO_INCREMENT);
	}

	mtx_unlock(&ntoskrnl_dispatchlock);

	return (prevstate);
}

static uint32_t
KeReadStateMutex(kmutex)
	kmutant			*kmutex;
{
	return (kmutex->km_header.dh_sigstate);
}

void
KeInitializeEvent(nt_kevent *kevent, uint32_t type, uint8_t state)
{
	InitializeListHead((&kevent->k_header.dh_waitlisthead));
	kevent->k_header.dh_sigstate = state;
	if (type == EVENT_TYPE_NOTIFY)
		kevent->k_header.dh_type = DISP_TYPE_NOTIFICATION_EVENT;
	else
		kevent->k_header.dh_type = DISP_TYPE_SYNCHRONIZATION_EVENT;
	kevent->k_header.dh_size = sizeof(nt_kevent) / sizeof(uint32_t);
}

uint32_t
KeResetEvent(kevent)
	nt_kevent		*kevent;
{
	uint32_t		prevstate;

	mtx_lock(&ntoskrnl_dispatchlock);
	prevstate = kevent->k_header.dh_sigstate;
	kevent->k_header.dh_sigstate = FALSE;
	mtx_unlock(&ntoskrnl_dispatchlock);

	return (prevstate);
}

uint32_t
KeSetEvent(nt_kevent *kevent, uint32_t increment, uint8_t kwait)
{
	uint32_t		prevstate;
	wait_block		*w;
	nt_dispatch_header	*dh;
	struct thread		*td;
	wb_ext			*we;

	mtx_lock(&ntoskrnl_dispatchlock);
	prevstate = kevent->k_header.dh_sigstate;
	dh = &kevent->k_header;

	if (IsListEmpty(&dh->dh_waitlisthead))
		/*
		 * If there's nobody in the waitlist, just set
		 * the state to signalled.
		 */
		dh->dh_sigstate = 1;
	else {
		/*
		 * Get the first waiter. If this is a synchronization
		 * event, just wake up that one thread (don't bother
		 * setting the state to signalled since we're supposed
		 * to automatically clear synchronization events anyway).
		 *
		 * If it's a notification event, or the first
		 * waiter is doing a WAITTYPE_ALL wait, go through
		 * the full wait satisfaction process.
		 */
		w = CONTAINING_RECORD(dh->dh_waitlisthead.nle_flink,
		    wait_block, wb_waitlist);
		we = w->wb_ext;
		td = we->we_td;
		if (kevent->k_header.dh_type == DISP_TYPE_NOTIFICATION_EVENT ||
		    w->wb_waittype == WAITTYPE_ALL) {
			if (prevstate == 0) {
				dh->dh_sigstate = 1;
				ntoskrnl_waittest(dh, increment);
			}
		} else {
			w->wb_awakened |= TRUE;
			cv_broadcastpri(&we->we_cv,
			    (w->wb_oldpri - (increment * 4)) > PRI_MIN_KERN ?
			    w->wb_oldpri - (increment * 4) : PRI_MIN_KERN);
		}
	}

	mtx_unlock(&ntoskrnl_dispatchlock);

	return (prevstate);
}

void
KeClearEvent(kevent)
	nt_kevent		*kevent;
{
	kevent->k_header.dh_sigstate = FALSE;
}

uint32_t
KeReadStateEvent(kevent)
	nt_kevent		*kevent;
{
	return (kevent->k_header.dh_sigstate);
}

/*
 * The object manager in Windows is responsible for managing
 * references and access to various types of objects, including
 * device_objects, events, threads, timers and so on. However,
 * there's a difference in the way objects are handled in user
 * mode versus kernel mode.
 *
 * In user mode (i.e. Win32 applications), all objects are
 * managed by the object manager. For example, when you create
 * a timer or event object, you actually end up with an 
 * object_header (for the object manager's bookkeeping
 * purposes) and an object body (which contains the actual object
 * structure, e.g. ktimer, kevent, etc...). This allows Windows
 * to manage resource quotas and to enforce access restrictions
 * on basically every kind of system object handled by the kernel.
 *
 * However, in kernel mode, you only end up using the object
 * manager some of the time. For example, in a driver, you create
 * a timer object by simply allocating the memory for a ktimer
 * structure and initializing it with KeInitializeTimer(). Hence,
 * the timer has no object_header and no reference counting or
 * security/resource checks are done on it. The assumption in
 * this case is that if you're running in kernel mode, you know
 * what you're doing, and you're already at an elevated privilege
 * anyway.
 *
 * There are some exceptions to this. The two most important ones
 * for our purposes are device_objects and threads. We need to use
 * the object manager to do reference counting on device_objects,
 * and for threads, you can only get a pointer to a thread's
 * dispatch header by using ObReferenceObjectByHandle() on the
 * handle returned by PsCreateSystemThread().
 */

static ndis_status
ObReferenceObjectByHandle(ndis_handle handle, uint32_t reqaccess, void *otype,
	uint8_t accessmode, void **object, void **handleinfo)
{
	nt_objref		*nr;

	nr = malloc(sizeof(nt_objref), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (nr == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	InitializeListHead((&nr->no_dh.dh_waitlisthead));
	nr->no_obj = handle;
	nr->no_dh.dh_type = DISP_TYPE_THREAD;
	nr->no_dh.dh_sigstate = 0;
	nr->no_dh.dh_size = (uint8_t)(sizeof(struct thread) /
	    sizeof(uint32_t));
	TAILQ_INSERT_TAIL(&ntoskrnl_reflist, nr, link);
	*object = nr;

	return (STATUS_SUCCESS);
}

static void
ObfDereferenceObject(object)
	void			*object;
{
	nt_objref		*nr;

	nr = object;
	TAILQ_REMOVE(&ntoskrnl_reflist, nr, link);
	free(nr, M_DEVBUF);
}

static uint32_t
ZwClose(handle)
	ndis_handle		handle;
{
	return (STATUS_SUCCESS);
}

static uint32_t
WmiQueryTraceInformation(traceclass, traceinfo, infolen, reqlen, buf)
	uint32_t		traceclass;
	void			*traceinfo;
	uint32_t		infolen;
	uint32_t		reqlen;
	void			*buf;
{
	return (STATUS_NOT_FOUND);
}

static uint32_t
WmiTraceMessage(uint64_t loghandle, uint32_t messageflags,
	void *guid, uint16_t messagenum, ...)
{
	return (STATUS_SUCCESS);
}

static uint32_t
IoWMIRegistrationControl(dobj, action)
	device_object		*dobj;
	uint32_t		action;
{
	return (STATUS_SUCCESS);
}

/*
 * This is here just in case the thread returns without calling
 * PsTerminateSystemThread().
 */
static void
ntoskrnl_thrfunc(arg)
	void			*arg;
{
	thread_context		*thrctx;
	uint32_t (*tfunc)(void *);
	void			*tctx;
	uint32_t		rval;

	thrctx = arg;
	tfunc = thrctx->tc_thrfunc;
	tctx = thrctx->tc_thrctx;
	free(thrctx, M_TEMP);

	rval = MSCALL1(tfunc, tctx);

	PsTerminateSystemThread(rval);
	return; /* notreached */
}

static ndis_status
PsCreateSystemThread(handle, reqaccess, objattrs, phandle,
	clientid, thrfunc, thrctx)
	ndis_handle		*handle;
	uint32_t		reqaccess;
	void			*objattrs;
	ndis_handle		phandle;
	void			*clientid;
	void			*thrfunc;
	void			*thrctx;
{
	int			error;
	thread_context		*tc;
	struct proc		*p;

	tc = malloc(sizeof(thread_context), M_TEMP, M_NOWAIT);
	if (tc == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	tc->tc_thrctx = thrctx;
	tc->tc_thrfunc = thrfunc;

	error = kproc_create(ntoskrnl_thrfunc, tc, &p,
	    RFHIGHPID, NDIS_KSTACK_PAGES, "Windows Kthread %d", ntoskrnl_kth);

	if (error) {
		free(tc, M_TEMP);
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	*handle = p;
	ntoskrnl_kth++;

	return (STATUS_SUCCESS);
}

/*
 * In Windows, the exit of a thread is an event that you're allowed
 * to wait on, assuming you've obtained a reference to the thread using
 * ObReferenceObjectByHandle(). Unfortunately, the only way we can
 * simulate this behavior is to register each thread we create in a
 * reference list, and if someone holds a reference to us, we poke
 * them.
 */
static ndis_status
PsTerminateSystemThread(status)
	ndis_status		status;
{
	struct nt_objref	*nr;

	mtx_lock(&ntoskrnl_dispatchlock);
	TAILQ_FOREACH(nr, &ntoskrnl_reflist, link) {
		if (nr->no_obj != curthread->td_proc)
			continue;
		nr->no_dh.dh_sigstate = 1;
		ntoskrnl_waittest(&nr->no_dh, IO_NO_INCREMENT);
		break;
	}
	mtx_unlock(&ntoskrnl_dispatchlock);

	ntoskrnl_kth--;

	kproc_exit(0);
	return (0);	/* notreached */
}

static uint32_t
DbgPrint(char *fmt, ...)
{
	va_list			ap;

	if (bootverbose) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}

	return (STATUS_SUCCESS);
}

static void
DbgBreakPoint(void)
{

	kdb_enter(KDB_WHY_NDIS, "DbgBreakPoint(): breakpoint");
}

static void
KeBugCheckEx(code, param1, param2, param3, param4)
    uint32_t			code;
    u_long			param1;
    u_long			param2;
    u_long			param3;
    u_long			param4;
{
	panic("KeBugCheckEx: STOP 0x%X", code);
}

static void
ntoskrnl_timercall(arg)
	void			*arg;
{
	ktimer			*timer;
	struct timeval		tv;
	kdpc			*dpc;

	mtx_lock(&ntoskrnl_dispatchlock);

	timer = arg;

#ifdef NTOSKRNL_DEBUG_TIMERS
	ntoskrnl_timer_fires++;
#endif
	ntoskrnl_remove_timer(timer);

	/*
	 * This should never happen, but complain
	 * if it does.
	 */

	if (timer->k_header.dh_inserted == FALSE) {
		mtx_unlock(&ntoskrnl_dispatchlock);
		printf("NTOS: timer %p fired even though "
		    "it was canceled\n", timer);
		return;
	}

	/* Mark the timer as no longer being on the timer queue. */

	timer->k_header.dh_inserted = FALSE;

	/* Now signal the object and satisfy any waits on it. */

	timer->k_header.dh_sigstate = 1;
	ntoskrnl_waittest(&timer->k_header, IO_NO_INCREMENT);

	/*
	 * If this is a periodic timer, re-arm it
	 * so it will fire again. We do this before
	 * calling any deferred procedure calls because
	 * it's possible the DPC might cancel the timer,
	 * in which case it would be wrong for us to
	 * re-arm it again afterwards.
	 */

	if (timer->k_period) {
		tv.tv_sec = 0;
		tv.tv_usec = timer->k_period * 1000;
		timer->k_header.dh_inserted = TRUE;
		ntoskrnl_insert_timer(timer, tvtohz(&tv));
#ifdef NTOSKRNL_DEBUG_TIMERS
		ntoskrnl_timer_reloads++;
#endif
	}

	dpc = timer->k_dpc;

	mtx_unlock(&ntoskrnl_dispatchlock);

	/* If there's a DPC associated with the timer, queue it up. */

	if (dpc != NULL)
		KeInsertQueueDpc(dpc, NULL, NULL);
}

#ifdef NTOSKRNL_DEBUG_TIMERS
static int
sysctl_show_timers(SYSCTL_HANDLER_ARGS)
{
	int			ret;

	ret = 0;
	ntoskrnl_show_timers();
	return (sysctl_handle_int(oidp, &ret, 0, req));
}

static void
ntoskrnl_show_timers()
{
	int			i = 0;
	list_entry		*l;

	mtx_lock_spin(&ntoskrnl_calllock);
	l = ntoskrnl_calllist.nle_flink;
	while(l != &ntoskrnl_calllist) {
		i++;
		l = l->nle_flink;
	}
	mtx_unlock_spin(&ntoskrnl_calllock);

	printf("\n");
	printf("%d timers available (out of %d)\n", i, NTOSKRNL_TIMEOUTS);
	printf("timer sets: %qu\n", ntoskrnl_timer_sets);
	printf("timer reloads: %qu\n", ntoskrnl_timer_reloads);
	printf("timer cancels: %qu\n", ntoskrnl_timer_cancels);
	printf("timer fires: %qu\n", ntoskrnl_timer_fires);
	printf("\n");
}
#endif

/*
 * Must be called with dispatcher lock held.
 */

static void
ntoskrnl_insert_timer(timer, ticks)
	ktimer			*timer;
	int			ticks;
{
	callout_entry		*e;
	list_entry		*l;
	struct callout		*c;

	/*
	 * Try and allocate a timer.
	 */
	mtx_lock_spin(&ntoskrnl_calllock);
	if (IsListEmpty(&ntoskrnl_calllist)) {
		mtx_unlock_spin(&ntoskrnl_calllock);
#ifdef NTOSKRNL_DEBUG_TIMERS
		ntoskrnl_show_timers();
#endif
		panic("out of timers!");
	}
	l = RemoveHeadList(&ntoskrnl_calllist);
	mtx_unlock_spin(&ntoskrnl_calllock);

	e = CONTAINING_RECORD(l, callout_entry, ce_list);
	c = &e->ce_callout;

	timer->k_callout = c;

	callout_init(c, 1);
	callout_reset(c, ticks, ntoskrnl_timercall, timer);
}

static void
ntoskrnl_remove_timer(timer)
	ktimer			*timer;
{
	callout_entry		*e;

	e = (callout_entry *)timer->k_callout;
	callout_stop(timer->k_callout);

	mtx_lock_spin(&ntoskrnl_calllock);
	InsertHeadList((&ntoskrnl_calllist), (&e->ce_list));
	mtx_unlock_spin(&ntoskrnl_calllock);
}

void
KeInitializeTimer(timer)
	ktimer			*timer;
{
	if (timer == NULL)
		return;

	KeInitializeTimerEx(timer,  EVENT_TYPE_NOTIFY);
}

void
KeInitializeTimerEx(timer, type)
	ktimer			*timer;
	uint32_t		type;
{
	if (timer == NULL)
		return;

	bzero((char *)timer, sizeof(ktimer));
	InitializeListHead((&timer->k_header.dh_waitlisthead));
	timer->k_header.dh_sigstate = FALSE;
	timer->k_header.dh_inserted = FALSE;
	if (type == EVENT_TYPE_NOTIFY)
		timer->k_header.dh_type = DISP_TYPE_NOTIFICATION_TIMER;
	else
		timer->k_header.dh_type = DISP_TYPE_SYNCHRONIZATION_TIMER;
	timer->k_header.dh_size = sizeof(ktimer) / sizeof(uint32_t);
}

/*
 * DPC subsystem. A Windows Defered Procedure Call has the following
 * properties:
 * - It runs at DISPATCH_LEVEL.
 * - It can have one of 3 importance values that control when it
 *   runs relative to other DPCs in the queue.
 * - On SMP systems, it can be set to run on a specific processor.
 * In order to satisfy the last property, we create a DPC thread for
 * each CPU in the system and bind it to that CPU. Each thread
 * maintains three queues with different importance levels, which
 * will be processed in order from lowest to highest.
 *
 * In Windows, interrupt handlers run as DPCs. (Not to be confused
 * with ISRs, which run in interrupt context and can preempt DPCs.)
 * ISRs are given the highest importance so that they'll take
 * precedence over timers and other things.
 */

static void
ntoskrnl_dpc_thread(arg)
	void			*arg;
{
	kdpc_queue		*kq;
	kdpc			*d;
	list_entry		*l;
	uint8_t			irql;

	kq = arg;

	InitializeListHead(&kq->kq_disp);
	kq->kq_td = curthread;
	kq->kq_exit = 0;
	kq->kq_running = FALSE;
	KeInitializeSpinLock(&kq->kq_lock);
	KeInitializeEvent(&kq->kq_proc, EVENT_TYPE_SYNC, FALSE);
	KeInitializeEvent(&kq->kq_done, EVENT_TYPE_SYNC, FALSE);

	/*
	 * Elevate our priority. DPCs are used to run interrupt
	 * handlers, and they should trigger as soon as possible
	 * once scheduled by an ISR.
	 */

	thread_lock(curthread);
#ifdef NTOSKRNL_MULTIPLE_DPCS
	sched_bind(curthread, kq->kq_cpu);
#endif
	sched_prio(curthread, PRI_MIN_KERN);
	thread_unlock(curthread);

	while (1) {
		KeWaitForSingleObject(&kq->kq_proc, 0, 0, TRUE, NULL);

		KeAcquireSpinLock(&kq->kq_lock, &irql);

		if (kq->kq_exit) {
			kq->kq_exit = 0;
			KeReleaseSpinLock(&kq->kq_lock, irql);
			break;
		}

		kq->kq_running = TRUE;

		while (!IsListEmpty(&kq->kq_disp)) {
			l = RemoveHeadList((&kq->kq_disp));
			d = CONTAINING_RECORD(l, kdpc, k_dpclistentry);
			InitializeListHead((&d->k_dpclistentry));
			KeReleaseSpinLockFromDpcLevel(&kq->kq_lock);
			MSCALL4(d->k_deferedfunc, d, d->k_deferredctx,
			    d->k_sysarg1, d->k_sysarg2);
			KeAcquireSpinLockAtDpcLevel(&kq->kq_lock);
		}

		kq->kq_running = FALSE;

		KeReleaseSpinLock(&kq->kq_lock, irql);

		KeSetEvent(&kq->kq_done, IO_NO_INCREMENT, FALSE);
	}

	kproc_exit(0);
	return; /* notreached */
}

static void
ntoskrnl_destroy_dpc_threads(void)
{
	kdpc_queue		*kq;
	kdpc			dpc;
	int			i;

	kq = kq_queues;
#ifdef NTOSKRNL_MULTIPLE_DPCS
	for (i = 0; i < mp_ncpus; i++) {
#else
	for (i = 0; i < 1; i++) {
#endif
		kq += i;

		kq->kq_exit = 1;
		KeInitializeDpc(&dpc, NULL, NULL);
		KeSetTargetProcessorDpc(&dpc, i);
		KeInsertQueueDpc(&dpc, NULL, NULL);
		while (kq->kq_exit)
			tsleep(kq->kq_td->td_proc, PWAIT, "dpcw", hz/10);
	}
}

static uint8_t
ntoskrnl_insert_dpc(head, dpc)
	list_entry		*head;
	kdpc			*dpc;
{
	list_entry		*l;
	kdpc			*d;

	l = head->nle_flink;
	while (l != head) {
		d = CONTAINING_RECORD(l, kdpc, k_dpclistentry);
		if (d == dpc)
			return (FALSE);
		l = l->nle_flink;
	}

	if (dpc->k_importance == KDPC_IMPORTANCE_LOW)
		InsertTailList((head), (&dpc->k_dpclistentry));
	else
		InsertHeadList((head), (&dpc->k_dpclistentry));

	return (TRUE);
}

void
KeInitializeDpc(dpc, dpcfunc, dpcctx)
	kdpc			*dpc;
	void			*dpcfunc;
	void			*dpcctx;
{

	if (dpc == NULL)
		return;

	dpc->k_deferedfunc = dpcfunc;
	dpc->k_deferredctx = dpcctx;
	dpc->k_num = KDPC_CPU_DEFAULT;
	dpc->k_importance = KDPC_IMPORTANCE_MEDIUM;
	InitializeListHead((&dpc->k_dpclistentry));
}

uint8_t
KeInsertQueueDpc(dpc, sysarg1, sysarg2)
	kdpc			*dpc;
	void			*sysarg1;
	void			*sysarg2;
{
	kdpc_queue		*kq;
	uint8_t			r;
	uint8_t			irql;

	if (dpc == NULL)
		return (FALSE);

	kq = kq_queues;

#ifdef NTOSKRNL_MULTIPLE_DPCS
	KeRaiseIrql(DISPATCH_LEVEL, &irql);

	/*
	 * By default, the DPC is queued to run on the same CPU
	 * that scheduled it.
	 */

	if (dpc->k_num == KDPC_CPU_DEFAULT)
		kq += curthread->td_oncpu;
	else
		kq += dpc->k_num;
	KeAcquireSpinLockAtDpcLevel(&kq->kq_lock);
#else
	KeAcquireSpinLock(&kq->kq_lock, &irql);
#endif

	r = ntoskrnl_insert_dpc(&kq->kq_disp, dpc);
	if (r == TRUE) {
		dpc->k_sysarg1 = sysarg1;
		dpc->k_sysarg2 = sysarg2;
	}
	KeReleaseSpinLock(&kq->kq_lock, irql);

	if (r == FALSE)
		return (r);

	KeSetEvent(&kq->kq_proc, IO_NO_INCREMENT, FALSE);

	return (r);
}

uint8_t
KeRemoveQueueDpc(dpc)
	kdpc			*dpc;
{
	kdpc_queue		*kq;
	uint8_t			irql;

	if (dpc == NULL)
		return (FALSE);

#ifdef NTOSKRNL_MULTIPLE_DPCS
	KeRaiseIrql(DISPATCH_LEVEL, &irql);

	kq = kq_queues + dpc->k_num;

	KeAcquireSpinLockAtDpcLevel(&kq->kq_lock);
#else
	kq = kq_queues;
	KeAcquireSpinLock(&kq->kq_lock, &irql);
#endif

	if (dpc->k_dpclistentry.nle_flink == &dpc->k_dpclistentry) {
		KeReleaseSpinLockFromDpcLevel(&kq->kq_lock);
		KeLowerIrql(irql);
		return (FALSE);
	}

	RemoveEntryList((&dpc->k_dpclistentry));
	InitializeListHead((&dpc->k_dpclistentry));

	KeReleaseSpinLock(&kq->kq_lock, irql);

	return (TRUE);
}

void
KeSetImportanceDpc(dpc, imp)
	kdpc			*dpc;
	uint32_t		imp;
{
	if (imp != KDPC_IMPORTANCE_LOW &&
	    imp != KDPC_IMPORTANCE_MEDIUM &&
	    imp != KDPC_IMPORTANCE_HIGH)
		return;

	dpc->k_importance = (uint8_t)imp;
}

void
KeSetTargetProcessorDpc(kdpc *dpc, uint8_t cpu)
{
	if (cpu > mp_ncpus)
		return;

	dpc->k_num = cpu;
}

void
KeFlushQueuedDpcs(void)
{
	kdpc_queue		*kq;
	int			i;

	/*
	 * Poke each DPC queue and wait
	 * for them to drain.
	 */

#ifdef NTOSKRNL_MULTIPLE_DPCS
	for (i = 0; i < mp_ncpus; i++) {
#else
	for (i = 0; i < 1; i++) {
#endif
		kq = kq_queues + i;
		KeSetEvent(&kq->kq_proc, IO_NO_INCREMENT, FALSE);
		KeWaitForSingleObject(&kq->kq_done, 0, 0, TRUE, NULL);
	}
}

uint32_t
KeGetCurrentProcessorNumber(void)
{
	return ((uint32_t)curthread->td_oncpu);
}

uint8_t
KeSetTimerEx(timer, duetime, period, dpc)
	ktimer			*timer;
	int64_t			duetime;
	uint32_t		period;
	kdpc			*dpc;
{
	struct timeval		tv;
	uint64_t		curtime;
	uint8_t			pending;

	if (timer == NULL)
		return (FALSE);

	mtx_lock(&ntoskrnl_dispatchlock);

	if (timer->k_header.dh_inserted == TRUE) {
		ntoskrnl_remove_timer(timer);
#ifdef NTOSKRNL_DEBUG_TIMERS
		ntoskrnl_timer_cancels++;
#endif
		timer->k_header.dh_inserted = FALSE;
		pending = TRUE;
	} else
		pending = FALSE;

	timer->k_duetime = duetime;
	timer->k_period = period;
	timer->k_header.dh_sigstate = FALSE;
	timer->k_dpc = dpc;

	if (duetime < 0) {
		tv.tv_sec = - (duetime) / 10000000;
		tv.tv_usec = (- (duetime) / 10) -
		    (tv.tv_sec * 1000000);
	} else {
		ntoskrnl_time(&curtime);
		if (duetime < curtime)
			tv.tv_sec = tv.tv_usec = 0;
		else {
			tv.tv_sec = ((duetime) - curtime) / 10000000;
			tv.tv_usec = ((duetime) - curtime) / 10 -
			    (tv.tv_sec * 1000000);
		}
	}

	timer->k_header.dh_inserted = TRUE;
	ntoskrnl_insert_timer(timer, tvtohz(&tv));
#ifdef NTOSKRNL_DEBUG_TIMERS
	ntoskrnl_timer_sets++;
#endif

	mtx_unlock(&ntoskrnl_dispatchlock);

	return (pending);
}

uint8_t
KeSetTimer(timer, duetime, dpc)
	ktimer			*timer;
	int64_t			duetime;
	kdpc			*dpc;
{
	return (KeSetTimerEx(timer, duetime, 0, dpc));
}

/*
 * The Windows DDK documentation seems to say that cancelling
 * a timer that has a DPC will result in the DPC also being
 * cancelled, but this isn't really the case.
 */

uint8_t
KeCancelTimer(timer)
	ktimer			*timer;
{
	uint8_t			pending;

	if (timer == NULL)
		return (FALSE);

	mtx_lock(&ntoskrnl_dispatchlock);

	pending = timer->k_header.dh_inserted;

	if (timer->k_header.dh_inserted == TRUE) {
		timer->k_header.dh_inserted = FALSE;
		ntoskrnl_remove_timer(timer);
#ifdef NTOSKRNL_DEBUG_TIMERS
		ntoskrnl_timer_cancels++;
#endif
	}

	mtx_unlock(&ntoskrnl_dispatchlock);

	return (pending);
}

uint8_t
KeReadStateTimer(timer)
	ktimer			*timer;
{
	return (timer->k_header.dh_sigstate);
}

static int32_t
KeDelayExecutionThread(uint8_t wait_mode, uint8_t alertable, int64_t *interval)
{
	ktimer                  timer;

	if (wait_mode != 0)
		panic("invalid wait_mode %d", wait_mode);

	KeInitializeTimer(&timer);
	KeSetTimer(&timer, *interval, NULL);
	KeWaitForSingleObject(&timer, 0, 0, alertable, NULL);

	return STATUS_SUCCESS;
}

static uint64_t
KeQueryInterruptTime(void)
{
	int ticks;
	struct timeval tv;

	getmicrouptime(&tv);

	ticks = tvtohz(&tv);

	return ticks * howmany(10000000, hz);
}

static struct thread *
KeGetCurrentThread(void)
{

	return curthread;
}

static int32_t
KeSetPriorityThread(td, pri)
	struct thread	*td;
	int32_t		pri;
{
	int32_t old;

	if (td == NULL)
		return LOW_REALTIME_PRIORITY;

	if (td->td_priority <= PRI_MIN_KERN)
		old = HIGH_PRIORITY;
	else if (td->td_priority >= PRI_MAX_KERN)
		old = LOW_PRIORITY;
	else
		old = LOW_REALTIME_PRIORITY;

	thread_lock(td);
	if (pri == HIGH_PRIORITY)
		sched_prio(td, PRI_MIN_KERN);
	if (pri == LOW_REALTIME_PRIORITY)
		sched_prio(td, PRI_MIN_KERN + (PRI_MAX_KERN - PRI_MIN_KERN) / 2);
	if (pri == LOW_PRIORITY)
		sched_prio(td, PRI_MAX_KERN);
	thread_unlock(td);

	return old;
}

static void
dummy()
{
	printf("ntoskrnl dummy called...\n");
}


image_patch_table ntoskrnl_functbl[] = {
	IMPORT_SFUNC(RtlZeroMemory, 2),
	IMPORT_SFUNC(RtlSecureZeroMemory, 2),
	IMPORT_SFUNC(RtlFillMemory, 3),
	IMPORT_SFUNC(RtlMoveMemory, 3),
	IMPORT_SFUNC(RtlCharToInteger, 3),
	IMPORT_SFUNC(RtlCopyMemory, 3),
	IMPORT_SFUNC(RtlCopyString, 2),
	IMPORT_SFUNC(RtlCompareMemory, 3),
	IMPORT_SFUNC(RtlEqualUnicodeString, 3),
	IMPORT_SFUNC(RtlCopyUnicodeString, 2),
	IMPORT_SFUNC(RtlUnicodeStringToAnsiString, 3),
	IMPORT_SFUNC(RtlAnsiStringToUnicodeString, 3),
	IMPORT_SFUNC(RtlInitAnsiString, 2),
	IMPORT_SFUNC_MAP(RtlInitString, RtlInitAnsiString, 2),
	IMPORT_SFUNC(RtlInitUnicodeString, 2),
	IMPORT_SFUNC(RtlFreeAnsiString, 1),
	IMPORT_SFUNC(RtlFreeUnicodeString, 1),
	IMPORT_SFUNC(RtlUnicodeStringToInteger, 3),
	IMPORT_CFUNC(sprintf, 0),
	IMPORT_CFUNC(vsprintf, 0),
	IMPORT_CFUNC_MAP(_snprintf, snprintf, 0),
	IMPORT_CFUNC_MAP(_vsnprintf, vsnprintf, 0),
	IMPORT_CFUNC(DbgPrint, 0),
	IMPORT_SFUNC(DbgBreakPoint, 0),
	IMPORT_SFUNC(KeBugCheckEx, 5),
	IMPORT_CFUNC(strncmp, 0),
	IMPORT_CFUNC(strcmp, 0),
	IMPORT_CFUNC_MAP(stricmp, strcasecmp, 0),
	IMPORT_CFUNC(strncpy, 0),
	IMPORT_CFUNC(strcpy, 0),
	IMPORT_CFUNC(strlen, 0),
	IMPORT_CFUNC_MAP(toupper, ntoskrnl_toupper, 0),
	IMPORT_CFUNC_MAP(tolower, ntoskrnl_tolower, 0),
	IMPORT_CFUNC_MAP(strstr, ntoskrnl_strstr, 0),
	IMPORT_CFUNC_MAP(strncat, ntoskrnl_strncat, 0),
	IMPORT_CFUNC_MAP(strchr, index, 0),
	IMPORT_CFUNC_MAP(strrchr, rindex, 0),
	IMPORT_CFUNC(memcpy, 0),
	IMPORT_CFUNC_MAP(memmove, ntoskrnl_memmove, 0),
	IMPORT_CFUNC_MAP(memset, ntoskrnl_memset, 0),
	IMPORT_CFUNC_MAP(memchr, ntoskrnl_memchr, 0),
	IMPORT_SFUNC(IoAllocateDriverObjectExtension, 4),
	IMPORT_SFUNC(IoGetDriverObjectExtension, 2),
	IMPORT_FFUNC(IofCallDriver, 2),
	IMPORT_FFUNC(IofCompleteRequest, 2),
	IMPORT_SFUNC(IoAcquireCancelSpinLock, 1),
	IMPORT_SFUNC(IoReleaseCancelSpinLock, 1),
	IMPORT_SFUNC(IoCancelIrp, 1),
	IMPORT_SFUNC(IoConnectInterrupt, 11),
	IMPORT_SFUNC(IoDisconnectInterrupt, 1),
	IMPORT_SFUNC(IoCreateDevice, 7),
	IMPORT_SFUNC(IoDeleteDevice, 1),
	IMPORT_SFUNC(IoGetAttachedDevice, 1),
	IMPORT_SFUNC(IoAttachDeviceToDeviceStack, 2),
	IMPORT_SFUNC(IoDetachDevice, 1),
	IMPORT_SFUNC(IoBuildSynchronousFsdRequest, 7),
	IMPORT_SFUNC(IoBuildAsynchronousFsdRequest, 6),
	IMPORT_SFUNC(IoBuildDeviceIoControlRequest, 9),
	IMPORT_SFUNC(IoAllocateIrp, 2),
	IMPORT_SFUNC(IoReuseIrp, 2),
	IMPORT_SFUNC(IoMakeAssociatedIrp, 2),
	IMPORT_SFUNC(IoFreeIrp, 1),
	IMPORT_SFUNC(IoInitializeIrp, 3),
	IMPORT_SFUNC(KeAcquireInterruptSpinLock, 1),
	IMPORT_SFUNC(KeReleaseInterruptSpinLock, 2),
	IMPORT_SFUNC(KeSynchronizeExecution, 3),
	IMPORT_SFUNC(KeWaitForSingleObject, 5),
	IMPORT_SFUNC(KeWaitForMultipleObjects, 8),
	IMPORT_SFUNC(_allmul, 4),
	IMPORT_SFUNC(_alldiv, 4),
	IMPORT_SFUNC(_allrem, 4),
	IMPORT_RFUNC(_allshr, 0),
	IMPORT_RFUNC(_allshl, 0),
	IMPORT_SFUNC(_aullmul, 4),
	IMPORT_SFUNC(_aulldiv, 4),
	IMPORT_SFUNC(_aullrem, 4),
	IMPORT_RFUNC(_aullshr, 0),
	IMPORT_RFUNC(_aullshl, 0),
	IMPORT_CFUNC(atoi, 0),
	IMPORT_CFUNC(atol, 0),
	IMPORT_CFUNC(rand, 0),
	IMPORT_CFUNC(srand, 0),
	IMPORT_SFUNC(WRITE_REGISTER_USHORT, 2),
	IMPORT_SFUNC(READ_REGISTER_USHORT, 1),
	IMPORT_SFUNC(WRITE_REGISTER_ULONG, 2),
	IMPORT_SFUNC(READ_REGISTER_ULONG, 1),
	IMPORT_SFUNC(READ_REGISTER_UCHAR, 1),
	IMPORT_SFUNC(WRITE_REGISTER_UCHAR, 2),
	IMPORT_SFUNC(ExInitializePagedLookasideList, 7),
	IMPORT_SFUNC(ExDeletePagedLookasideList, 1),
	IMPORT_SFUNC(ExInitializeNPagedLookasideList, 7),
	IMPORT_SFUNC(ExDeleteNPagedLookasideList, 1),
	IMPORT_FFUNC(InterlockedPopEntrySList, 1),
	IMPORT_FFUNC(InitializeSListHead, 1),
	IMPORT_FFUNC(InterlockedPushEntrySList, 2),
	IMPORT_SFUNC(ExQueryDepthSList, 1),
	IMPORT_FFUNC_MAP(ExpInterlockedPopEntrySList,
		InterlockedPopEntrySList, 1),
	IMPORT_FFUNC_MAP(ExpInterlockedPushEntrySList,
		InterlockedPushEntrySList, 2),
	IMPORT_FFUNC(ExInterlockedPopEntrySList, 2),
	IMPORT_FFUNC(ExInterlockedPushEntrySList, 3),
	IMPORT_SFUNC(ExAllocatePoolWithTag, 3),
	IMPORT_SFUNC(ExFreePoolWithTag, 2),
	IMPORT_SFUNC(ExFreePool, 1),
#ifdef __i386__
	IMPORT_FFUNC(KefAcquireSpinLockAtDpcLevel, 1),
	IMPORT_FFUNC(KefReleaseSpinLockFromDpcLevel,1),
	IMPORT_FFUNC(KeAcquireSpinLockRaiseToDpc, 1),
#else
	/*
	 * For AMD64, we can get away with just mapping
	 * KeAcquireSpinLockRaiseToDpc() directly to KfAcquireSpinLock()
	 * because the calling conventions end up being the same.
	 * On i386, we have to be careful because KfAcquireSpinLock()
	 * is _fastcall but KeAcquireSpinLockRaiseToDpc() isn't.
	 */
	IMPORT_SFUNC(KeAcquireSpinLockAtDpcLevel, 1),
	IMPORT_SFUNC(KeReleaseSpinLockFromDpcLevel, 1),
	IMPORT_SFUNC_MAP(KeAcquireSpinLockRaiseToDpc, KfAcquireSpinLock, 1),
#endif
	IMPORT_SFUNC_MAP(KeReleaseSpinLock, KfReleaseSpinLock, 1),
	IMPORT_FFUNC(InterlockedIncrement, 1),
	IMPORT_FFUNC(InterlockedDecrement, 1),
	IMPORT_FFUNC(InterlockedExchange, 2),
	IMPORT_FFUNC(ExInterlockedAddLargeStatistic, 2),
	IMPORT_SFUNC(IoAllocateMdl, 5),
	IMPORT_SFUNC(IoFreeMdl, 1),
	IMPORT_SFUNC(MmAllocateContiguousMemory, 2 + 1),
	IMPORT_SFUNC(MmAllocateContiguousMemorySpecifyCache, 5 + 3),
	IMPORT_SFUNC(MmFreeContiguousMemory, 1),
	IMPORT_SFUNC(MmFreeContiguousMemorySpecifyCache, 3),
	IMPORT_SFUNC(MmSizeOfMdl, 1),
	IMPORT_SFUNC(MmMapLockedPages, 2),
	IMPORT_SFUNC(MmMapLockedPagesSpecifyCache, 6),
	IMPORT_SFUNC(MmUnmapLockedPages, 2),
	IMPORT_SFUNC(MmBuildMdlForNonPagedPool, 1),
	IMPORT_SFUNC(MmGetPhysicalAddress, 1),
	IMPORT_SFUNC(MmGetSystemRoutineAddress, 1),
	IMPORT_SFUNC(MmIsAddressValid, 1),
	IMPORT_SFUNC(MmMapIoSpace, 3 + 1),
	IMPORT_SFUNC(MmUnmapIoSpace, 2),
	IMPORT_SFUNC(KeInitializeSpinLock, 1),
	IMPORT_SFUNC(IoIsWdmVersionAvailable, 2),
	IMPORT_SFUNC(IoOpenDeviceRegistryKey, 4),
	IMPORT_SFUNC(IoGetDeviceObjectPointer, 4),
	IMPORT_SFUNC(IoGetDeviceProperty, 5),
	IMPORT_SFUNC(IoAllocateWorkItem, 1),
	IMPORT_SFUNC(IoFreeWorkItem, 1),
	IMPORT_SFUNC(IoQueueWorkItem, 4),
	IMPORT_SFUNC(ExQueueWorkItem, 2),
	IMPORT_SFUNC(ntoskrnl_workitem, 2),
	IMPORT_SFUNC(KeInitializeMutex, 2),
	IMPORT_SFUNC(KeReleaseMutex, 2),
	IMPORT_SFUNC(KeReadStateMutex, 1),
	IMPORT_SFUNC(KeInitializeEvent, 3),
	IMPORT_SFUNC(KeSetEvent, 3),
	IMPORT_SFUNC(KeResetEvent, 1),
	IMPORT_SFUNC(KeClearEvent, 1),
	IMPORT_SFUNC(KeReadStateEvent, 1),
	IMPORT_SFUNC(KeInitializeTimer, 1),
	IMPORT_SFUNC(KeInitializeTimerEx, 2),
	IMPORT_SFUNC(KeSetTimer, 3),
	IMPORT_SFUNC(KeSetTimerEx, 4),
	IMPORT_SFUNC(KeCancelTimer, 1),
	IMPORT_SFUNC(KeReadStateTimer, 1),
	IMPORT_SFUNC(KeInitializeDpc, 3),
	IMPORT_SFUNC(KeInsertQueueDpc, 3),
	IMPORT_SFUNC(KeRemoveQueueDpc, 1),
	IMPORT_SFUNC(KeSetImportanceDpc, 2),
	IMPORT_SFUNC(KeSetTargetProcessorDpc, 2),
	IMPORT_SFUNC(KeFlushQueuedDpcs, 0),
	IMPORT_SFUNC(KeGetCurrentProcessorNumber, 1),
	IMPORT_SFUNC(ObReferenceObjectByHandle, 6),
	IMPORT_FFUNC(ObfDereferenceObject, 1),
	IMPORT_SFUNC(ZwClose, 1),
	IMPORT_SFUNC(PsCreateSystemThread, 7),
	IMPORT_SFUNC(PsTerminateSystemThread, 1),
	IMPORT_SFUNC(IoWMIRegistrationControl, 2),
	IMPORT_SFUNC(WmiQueryTraceInformation, 5),
	IMPORT_CFUNC(WmiTraceMessage, 0),
	IMPORT_SFUNC(KeQuerySystemTime, 1),
	IMPORT_CFUNC(KeTickCount, 0),
	IMPORT_SFUNC(KeDelayExecutionThread, 3),
	IMPORT_SFUNC(KeQueryInterruptTime, 0),
	IMPORT_SFUNC(KeGetCurrentThread, 0),
	IMPORT_SFUNC(KeSetPriorityThread, 2),

	/*
	 * This last entry is a catch-all for any function we haven't
	 * implemented yet. The PE import list patching routine will
	 * use it for any function that doesn't have an explicit match
	 * in this table.
	 */

	{ NULL, (FUNC)dummy, NULL, 0, WINDRV_WRAP_STDCALL },

	/* End of list. */

	{ NULL, NULL, NULL }
};
