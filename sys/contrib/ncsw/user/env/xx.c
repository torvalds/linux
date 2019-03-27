/*-
 * Copyright (c) 2011 Semihalf.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>

#include <machine/cpufunc.h>
#include <machine/intr_machdep.h>
#include <machine/pmap.h>
#include <machine/stdarg.h>

#include <dev/dpaa/bman.h>
#include <dev/dpaa/qman.h>
#include <dev/dpaa/portals.h>

#include <powerpc/mpc85xx/mpc85xx.h>
#include "error_ext.h"
#include "std_ext.h"
#include "list_ext.h"
#include "mm_ext.h"

/* Configuration */

/* Define the number of dTSEC ports active in system */
#define MALLOCSMART_DTSEC_IN_USE	4

/*
 * Calculate malloc's pool size for dTSEC's buffers.
 * We reserve 1MB pool for each dTSEC port.
 */
#define	MALLOCSMART_POOL_SIZE		\
    (MALLOCSMART_DTSEC_IN_USE * 1024 * 1024)

#define MALLOCSMART_SLICE_SIZE		(PAGE_SIZE / 2)		/* 2kB */

/* Defines */
#define MALLOCSMART_SIZE_TO_SLICE(x)	\
    (((x) + MALLOCSMART_SLICE_SIZE - 1) / MALLOCSMART_SLICE_SIZE)
#define MALLOCSMART_SLICES		\
    MALLOCSMART_SIZE_TO_SLICE(MALLOCSMART_POOL_SIZE)

/* Malloc Pool for NetCommSW */
MALLOC_DEFINE(M_NETCOMMSW, "NetCommSW", "NetCommSW software stack");
MALLOC_DEFINE(M_NETCOMMSW_MT, "NetCommSWTrack",
    "NetCommSW software allocation tracker");

/* MallocSmart data structures */
static void *XX_MallocSmartPool;
static int XX_MallocSmartMap[MALLOCSMART_SLICES];

static struct mtx XX_MallocSmartLock;
static struct mtx XX_MallocTrackLock;
MTX_SYSINIT(XX_MallocSmartLockInit, &XX_MallocSmartLock,
    "NetCommSW MallocSmart Lock", MTX_DEF);
MTX_SYSINIT(XX_MallocTrackLockInit, &XX_MallocTrackLock,
    "NetCommSW MallocTrack Lock", MTX_DEF);

/* Interrupt info */
#define XX_INTR_FLAG_PREALLOCATED	(1 << 0)
#define XX_INTR_FLAG_BOUND		(1 << 1)
#define XX_INTR_FLAG_FMAN_FIX		(1 << 2)

struct XX_IntrInfo {
	driver_intr_t	*handler;
	void		*arg;
	int		cpu;
	int		flags;
	void		*cookie;
};

static struct XX_IntrInfo XX_IntrInfo[INTR_VECTORS];
/* Portal type identifiers */
enum XX_PortalIdent{
	BM_PORTAL = 0,
	QM_PORTAL,
};
/* Structure to store portals' properties */
struct XX_PortalInfo {
	vm_paddr_t	portal_ce_pa[2][MAXCPU];
	vm_paddr_t	portal_ci_pa[2][MAXCPU];
	uint32_t	portal_ce_size[2][MAXCPU];
	uint32_t	portal_ci_size[2][MAXCPU];
	vm_offset_t	portal_ce_va[2];
	vm_offset_t	portal_ci_va[2];
	uintptr_t	portal_intr[2][MAXCPU];
};

static struct XX_PortalInfo XX_PInfo;

void
XX_Exit(int status)
{

	panic("NetCommSW: Exit called with status %i", status);
}

void
XX_Print(char *str, ...)
{
	va_list ap;

	va_start(ap, str);
	vprintf(str, ap);
	va_end(ap);
}

void *
XX_Malloc(uint32_t size)
{
	void *p = (malloc(size, M_NETCOMMSW, M_NOWAIT));

	return (p);
}

static int
XX_MallocSmartMapCheck(unsigned int start, unsigned int slices)
{
	unsigned int i;

	mtx_assert(&XX_MallocSmartLock, MA_OWNED);
	for (i = start; i < start + slices; i++)
		if (XX_MallocSmartMap[i])
			return (FALSE);
	return (TRUE);
}

static void
XX_MallocSmartMapSet(unsigned int start, unsigned int slices)
{
	unsigned int i;

	mtx_assert(&XX_MallocSmartLock, MA_OWNED);

	for (i = start; i < start + slices; i++)
		XX_MallocSmartMap[i] = ((i == start) ? slices : -1);
}

static void
XX_MallocSmartMapClear(unsigned int start, unsigned int slices)
{
	unsigned int i;

	mtx_assert(&XX_MallocSmartLock, MA_OWNED);

	for (i = start; i < start + slices; i++)
		XX_MallocSmartMap[i] = 0;
}

int
XX_MallocSmartInit(void)
{
	int error;

	error = E_OK;
	mtx_lock(&XX_MallocSmartLock);

	if (XX_MallocSmartPool)
		goto out;

	/* Allocate MallocSmart pool */
	XX_MallocSmartPool = contigmalloc(MALLOCSMART_POOL_SIZE, M_NETCOMMSW,
	    M_NOWAIT, 0, 0xFFFFFFFFFull, MALLOCSMART_POOL_SIZE, 0);
	if (!XX_MallocSmartPool) {
		error = E_NO_MEMORY;
		goto out;
	}

out:
	mtx_unlock(&XX_MallocSmartLock);
	return (error);
}

void *
XX_MallocSmart(uint32_t size, int memPartitionId, uint32_t alignment)
{
	unsigned int i;
	vm_offset_t addr;

	addr = 0;

	/* Convert alignment and size to number of slices */
	alignment = MALLOCSMART_SIZE_TO_SLICE(alignment);
	size = MALLOCSMART_SIZE_TO_SLICE(size);

	/* Lock resources */
	mtx_lock(&XX_MallocSmartLock);

	/* Allocate region */
	for (i = 0; i + size <= MALLOCSMART_SLICES; i += alignment) {
		if (XX_MallocSmartMapCheck(i, size)) {
			XX_MallocSmartMapSet(i, size);
			addr = (vm_offset_t)XX_MallocSmartPool +
			    (i * MALLOCSMART_SLICE_SIZE);
			break;
		}
	}

	/* Unlock resources */
	mtx_unlock(&XX_MallocSmartLock);

	return ((void *)addr);
}

void
XX_FreeSmart(void *p)
{
	unsigned int start, slices;

	/* Calculate first slice of region */
	start = MALLOCSMART_SIZE_TO_SLICE((vm_offset_t)(p) -
	    (vm_offset_t)XX_MallocSmartPool);

	/* Lock resources */
	mtx_lock(&XX_MallocSmartLock);

	KASSERT(XX_MallocSmartMap[start] > 0,
	    ("XX_FreeSmart: Double or mid-block free!\n"));

	/* Free region */
	slices = XX_MallocSmartMap[start];
	XX_MallocSmartMapClear(start, slices);

	/* Unlock resources */
	mtx_unlock(&XX_MallocSmartLock);
}

void
XX_Free(void *p)
{

	free(p, M_NETCOMMSW);
}

uint32_t
XX_DisableAllIntr(void)
{

	return (intr_disable());
}

void
XX_RestoreAllIntr(uint32_t flags)
{

	intr_restore(flags);
}

static bool
XX_IsPortalIntr(uintptr_t irq)
{
	int cpu, type;
	/* Check interrupt numbers of all available portals */
	for (cpu = 0, type = 0; XX_PInfo.portal_intr[type][cpu] != 0; cpu++) {
		if (irq == XX_PInfo.portal_intr[type][cpu]) {
			/* Found it! */
			return (1);
		}
		if (XX_PInfo.portal_intr[type][cpu + 1] == 0) {
			type++;
			cpu = 0;
		}
	}

	return (0);
}

void
XX_FmanFixIntr(int irq)
{

	XX_IntrInfo[irq].flags |= XX_INTR_FLAG_FMAN_FIX;
}

static bool
XX_FmanNeedsIntrFix(int irq)
{

	if (XX_IntrInfo[irq].flags & XX_INTR_FLAG_FMAN_FIX)
		return (1);

	return (0);
}

static void
XX_Dispatch(void *arg)
{
	struct XX_IntrInfo *info;

	info = arg;

	/* Bind this thread to proper CPU when SMP has been already started. */
	if ((info->flags & XX_INTR_FLAG_BOUND) == 0 && smp_started &&
	    info->cpu >= 0) {
		thread_lock(curthread);
		sched_bind(curthread, info->cpu);
		thread_unlock(curthread);

		info->flags |= XX_INTR_FLAG_BOUND;
	}

	if (info->handler == NULL) {
		printf("%s(): IRQ handler is NULL!\n", __func__);
		return;
	}

	info->handler(info->arg);
}

t_Error
XX_PreallocAndBindIntr(uintptr_t irq, unsigned int cpu)
{
	struct resource *r;
	unsigned int inum;
	t_Error error;

	r = (struct resource *)irq;
	inum = rman_get_start(r);

	error = XX_SetIntr(irq, XX_Dispatch, &XX_IntrInfo[inum]);
	if (error != 0)
		return (error);

	XX_IntrInfo[inum].flags = XX_INTR_FLAG_PREALLOCATED;
	XX_IntrInfo[inum].cpu = cpu;

	return (E_OK);
}

t_Error
XX_DeallocIntr(uintptr_t irq)
{
	struct resource *r;
	unsigned int inum;

	r = (struct resource *)irq;
	inum = rman_get_start(r);

	if ((XX_IntrInfo[inum].flags & XX_INTR_FLAG_PREALLOCATED) == 0)
		return (E_INVALID_STATE);

	XX_IntrInfo[inum].flags = 0;
	return (XX_FreeIntr(irq));
}

t_Error
XX_SetIntr(uintptr_t irq, t_Isr *f_Isr, t_Handle handle)
{
	device_t dev;
	struct resource *r;
	unsigned int flags;
	int err;

	r = (struct resource *)irq;
	dev = rman_get_device(r);
	irq = rman_get_start(r);

	/* Handle preallocated interrupts */
	if (XX_IntrInfo[irq].flags & XX_INTR_FLAG_PREALLOCATED) {
		if (XX_IntrInfo[irq].handler != NULL)
			return (E_BUSY);

		XX_IntrInfo[irq].handler = f_Isr;
		XX_IntrInfo[irq].arg = handle;

		return (E_OK);
	}

	flags = INTR_TYPE_NET | INTR_MPSAFE;

	/* BMAN/QMAN Portal interrupts must be exlusive */
	if (XX_IsPortalIntr(irq))
		flags |= INTR_EXCL;

	err = bus_setup_intr(dev, r, flags, NULL, f_Isr, handle,
		    &XX_IntrInfo[irq].cookie);
	if (err)
		goto finish;

	/*
	 * XXX: Bind FMan IRQ to CPU0. Current interrupt subsystem directs each
	 * interrupt to all CPUs. Race between an interrupt assertion and
	 * masking may occur and interrupt handler may be called multiple times
	 * per one interrupt. FMan doesn't support such a situation. Workaround
	 * is to bind FMan interrupt to one CPU0 only.
	 */
#ifdef SMP
	if (XX_FmanNeedsIntrFix(irq))
		err = powerpc_bind_intr(irq, 0);
#endif
finish:
	return (err);
}

t_Error
XX_FreeIntr(uintptr_t irq)
{
	device_t dev;
	struct resource *r;

	r = (struct resource *)irq;
	dev = rman_get_device(r);
	irq = rman_get_start(r);

	/* Handle preallocated interrupts */
	if (XX_IntrInfo[irq].flags & XX_INTR_FLAG_PREALLOCATED) {
		if (XX_IntrInfo[irq].handler == NULL)
			return (E_INVALID_STATE);

		XX_IntrInfo[irq].handler = NULL;
		XX_IntrInfo[irq].arg = NULL;

		return (E_OK);
	}

	return (bus_teardown_intr(dev, r, XX_IntrInfo[irq].cookie));
}

t_Error
XX_EnableIntr(uintptr_t irq)
{
	struct resource *r;

	r = (struct resource *)irq;
	irq = rman_get_start(r);

	powerpc_intr_unmask(irq);

	return (E_OK);
}

t_Error
XX_DisableIntr(uintptr_t irq)
{
	struct resource *r;

	r = (struct resource *)irq;
	irq = rman_get_start(r);

	powerpc_intr_mask(irq);

	return (E_OK);
}

t_TaskletHandle
XX_InitTasklet (void (*routine)(void *), void *data)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (NULL);
}


void
XX_FreeTasklet (t_TaskletHandle h_Tasklet)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
}

int
XX_ScheduleTask(t_TaskletHandle h_Tasklet, int immediate)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (0);
}

void
XX_FlushScheduledTasks(void)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
}

int
XX_TaskletIsQueued(t_TaskletHandle h_Tasklet)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (0);
}

void
XX_SetTaskletData(t_TaskletHandle h_Tasklet, t_Handle data)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
}

t_Handle
XX_GetTaskletData(t_TaskletHandle h_Tasklet)
{
	/* Not referenced */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (NULL);
}

t_Handle
XX_InitSpinlock(void)
{
	struct mtx *m;

	m = malloc(sizeof(*m), M_NETCOMMSW, M_NOWAIT | M_ZERO);
	if (!m)
		return (0);

	mtx_init(m, "NetCommSW Lock", NULL, MTX_DEF | MTX_DUPOK);

	return (m);
}

void
XX_FreeSpinlock(t_Handle h_Spinlock)
{
	struct mtx *m;

	m = h_Spinlock;

	mtx_destroy(m);
	free(m, M_NETCOMMSW);
}

void
XX_LockSpinlock(t_Handle h_Spinlock)
{
	struct mtx *m;

	m = h_Spinlock;
	mtx_lock(m);
}

void
XX_UnlockSpinlock(t_Handle h_Spinlock)
{
	struct mtx *m;

	m = h_Spinlock;
	mtx_unlock(m);
}

uint32_t
XX_LockIntrSpinlock(t_Handle h_Spinlock)
{

	XX_LockSpinlock(h_Spinlock);
	return (0);
}

void
XX_UnlockIntrSpinlock(t_Handle h_Spinlock, uint32_t intrFlags)
{

	XX_UnlockSpinlock(h_Spinlock);
}

uint32_t
XX_Sleep(uint32_t msecs)
{

	XX_UDelay(1000 * msecs);
	return (0);
}

void
XX_UDelay(uint32_t usecs)
{
	DELAY(usecs);
}

t_Error
XX_IpcRegisterMsgHandler(char addr[XX_IPC_MAX_ADDR_NAME_LENGTH],
    t_IpcMsgHandler *f_MsgHandler, t_Handle  h_Module, uint32_t replyLength)
{

	/*
	 * This function returns fake E_OK status and does nothing
	 * as NetCommSW IPC is not used by FreeBSD drivers.
	 */
	return (E_OK);
}

t_Error
XX_IpcUnregisterMsgHandler(char addr[XX_IPC_MAX_ADDR_NAME_LENGTH])
{
	/*
	 * This function returns fake E_OK status and does nothing
	 * as NetCommSW IPC is not used by FreeBSD drivers.
	 */
	return (E_OK);
}


t_Error
XX_IpcSendMessage(t_Handle h_Session,
    uint8_t *p_Msg, uint32_t msgLength, uint8_t *p_Reply,
    uint32_t *p_ReplyLength, t_IpcMsgCompletion *f_Completion, t_Handle h_Arg)
{

	/* Should not be called */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (E_OK);
}

t_Handle
XX_IpcInitSession(char destAddr[XX_IPC_MAX_ADDR_NAME_LENGTH],
    char srcAddr[XX_IPC_MAX_ADDR_NAME_LENGTH])
{

	/* Should not be called */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (E_OK);
}

t_Error
XX_IpcFreeSession(t_Handle h_Session)
{

	/* Should not be called */
	printf("NetCommSW: Unimplemented function %s() called!\n", __func__);
	return (E_OK);
}

physAddress_t
XX_VirtToPhys(void *addr)
{
	vm_paddr_t paddr;
	int cpu;

	cpu = PCPU_GET(cpuid);

	/* Handle NULL address */
	if (addr == NULL)
		return (-1);

	/* Check CCSR */
	if ((vm_offset_t)addr >= ccsrbar_va &&
	    (vm_offset_t)addr < ccsrbar_va + ccsrbar_size)
		return (((vm_offset_t)addr - ccsrbar_va) + ccsrbar_pa);

	/* Handle BMAN mappings */
	if (((vm_offset_t)addr >= XX_PInfo.portal_ce_va[BM_PORTAL]) &&
	    ((vm_offset_t)addr < XX_PInfo.portal_ce_va[BM_PORTAL] +
	    XX_PInfo.portal_ce_size[BM_PORTAL][cpu]))
		return (XX_PInfo.portal_ce_pa[BM_PORTAL][cpu] +
		    (vm_offset_t)addr - XX_PInfo.portal_ce_va[BM_PORTAL]);

	if (((vm_offset_t)addr >= XX_PInfo.portal_ci_va[BM_PORTAL]) &&
	    ((vm_offset_t)addr < XX_PInfo.portal_ci_va[BM_PORTAL] +
	    XX_PInfo.portal_ci_size[BM_PORTAL][cpu]))
		return (XX_PInfo.portal_ci_pa[BM_PORTAL][cpu] +
		    (vm_offset_t)addr - XX_PInfo.portal_ci_va[BM_PORTAL]);

	/* Handle QMAN mappings */
	if (((vm_offset_t)addr >= XX_PInfo.portal_ce_va[QM_PORTAL]) &&
	    ((vm_offset_t)addr < XX_PInfo.portal_ce_va[QM_PORTAL] +
	    XX_PInfo.portal_ce_size[QM_PORTAL][cpu]))
		return (XX_PInfo.portal_ce_pa[QM_PORTAL][cpu] +
		    (vm_offset_t)addr - XX_PInfo.portal_ce_va[QM_PORTAL]);

	if (((vm_offset_t)addr >= XX_PInfo.portal_ci_va[QM_PORTAL]) &&
	    ((vm_offset_t)addr < XX_PInfo.portal_ci_va[QM_PORTAL] +
	    XX_PInfo.portal_ci_size[QM_PORTAL][cpu]))
		return (XX_PInfo.portal_ci_pa[QM_PORTAL][cpu] +
		    (vm_offset_t)addr - XX_PInfo.portal_ci_va[QM_PORTAL]);

	if (PMAP_HAS_DMAP && (vm_offset_t)addr >= DMAP_BASE_ADDRESS &&
	    (vm_offset_t)addr <= DMAP_MAX_ADDRESS)
		return (DMAP_TO_PHYS((vm_offset_t)addr));
	else
		paddr = pmap_kextract((vm_offset_t)addr);

	if (paddr == 0)
		printf("NetCommSW: "
		    "Unable to translate virtual address %p!\n", addr);
	else
		pmap_track_page(kernel_pmap, (vm_offset_t)addr);

	return (paddr);
}

void *
XX_PhysToVirt(physAddress_t addr)
{
	struct pv_entry *pv;
	vm_page_t page;
	int cpu;

	/* Check CCSR */
	if (addr >= ccsrbar_pa && addr < ccsrbar_pa + ccsrbar_size)
		return ((void *)((vm_offset_t)(addr - ccsrbar_pa) +
		    ccsrbar_va));

	cpu = PCPU_GET(cpuid);

	/* Handle BMAN mappings */
	if ((addr >= XX_PInfo.portal_ce_pa[BM_PORTAL][cpu]) &&
	    (addr < XX_PInfo.portal_ce_pa[BM_PORTAL][cpu] +
	    XX_PInfo.portal_ce_size[BM_PORTAL][cpu]))
		return ((void *)(XX_PInfo.portal_ci_va[BM_PORTAL] +
		    (vm_offset_t)(addr - XX_PInfo.portal_ci_pa[BM_PORTAL][cpu])));

	if ((addr >= XX_PInfo.portal_ci_pa[BM_PORTAL][cpu]) &&
	    (addr < XX_PInfo.portal_ci_pa[BM_PORTAL][cpu] +
	    XX_PInfo.portal_ci_size[BM_PORTAL][cpu]))
		return ((void *)(XX_PInfo.portal_ci_va[BM_PORTAL] +
		    (vm_offset_t)(addr - XX_PInfo.portal_ci_pa[BM_PORTAL][cpu])));

	/* Handle QMAN mappings */
	if ((addr >= XX_PInfo.portal_ce_pa[QM_PORTAL][cpu]) &&
	    (addr < XX_PInfo.portal_ce_pa[QM_PORTAL][cpu] +
	    XX_PInfo.portal_ce_size[QM_PORTAL][cpu]))
		return ((void *)(XX_PInfo.portal_ce_va[QM_PORTAL] +
		    (vm_offset_t)(addr - XX_PInfo.portal_ce_pa[QM_PORTAL][cpu])));

	if ((addr >= XX_PInfo.portal_ci_pa[QM_PORTAL][cpu]) &&
	    (addr < XX_PInfo.portal_ci_pa[QM_PORTAL][cpu] +
	    XX_PInfo.portal_ci_size[QM_PORTAL][cpu]))
		return ((void *)(XX_PInfo.portal_ci_va[QM_PORTAL] +
		    (vm_offset_t)(addr - XX_PInfo.portal_ci_pa[QM_PORTAL][cpu])));

	page = PHYS_TO_VM_PAGE(addr);
	pv = TAILQ_FIRST(&page->md.pv_list);

	if (pv != NULL)
		return ((void *)(pv->pv_va + ((vm_offset_t)addr & PAGE_MASK)));

	if (PMAP_HAS_DMAP)
		return ((void *)(uintptr_t)PHYS_TO_DMAP(addr));

	printf("NetCommSW: "
	    "Unable to translate physical address 0x%09jx!\n", (uintmax_t)addr);

	return (NULL);
}

void
XX_PortalSetInfo(device_t dev)
{
	char *dev_name;
	struct dpaa_portals_softc *sc;
	int i, type, len;

	dev_name = malloc(sizeof(*dev_name), M_TEMP, M_WAITOK |
	    M_ZERO);

	len = strlen("bman-portals");

	strncpy(dev_name, device_get_name(dev), len);

	if (strncmp(dev_name, "bman-portals", len) && strncmp(dev_name,
	    "qman-portals", len))
		goto end;

	if (strncmp(dev_name, "bman-portals", len) == 0)
		type = BM_PORTAL;
	else
		type = QM_PORTAL;

	sc = device_get_softc(dev);

	for (i = 0; sc->sc_dp[i].dp_ce_pa != 0; i++) {
		XX_PInfo.portal_ce_pa[type][i] = sc->sc_dp[i].dp_ce_pa;
		XX_PInfo.portal_ci_pa[type][i] = sc->sc_dp[i].dp_ci_pa;
		XX_PInfo.portal_ce_size[type][i] = sc->sc_dp[i].dp_ce_size;
		XX_PInfo.portal_ci_size[type][i] = sc->sc_dp[i].dp_ci_size;
		XX_PInfo.portal_intr[type][i] = sc->sc_dp[i].dp_intr_num;
	}

	XX_PInfo.portal_ce_va[type] = rman_get_bushandle(sc->sc_rres[0]);
	XX_PInfo.portal_ci_va[type] = rman_get_bushandle(sc->sc_rres[1]);
end:
	free(dev_name, M_TEMP);
}
