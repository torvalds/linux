/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * Implementation of common BSD OS abstraction functions
 */

#include "ocs.h"

static MALLOC_DEFINE(M_OCS, "OCS", "OneCore Storage data");

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

timeout_t	__ocs_callout;

uint32_t
ocs_config_read32(ocs_os_handle_t os, uint32_t reg)
{
	return pci_read_config(os->dev, reg, 4);
}

uint16_t
ocs_config_read16(ocs_os_handle_t os, uint32_t reg)
{
	return pci_read_config(os->dev, reg, 2);
}

uint8_t
ocs_config_read8(ocs_os_handle_t os, uint32_t reg)
{
	return pci_read_config(os->dev, reg, 1);
}

void
ocs_config_write8(ocs_os_handle_t os, uint32_t reg, uint8_t val)
{
	return pci_write_config(os->dev, reg, val, 1);
}

void
ocs_config_write16(ocs_os_handle_t os, uint32_t reg, uint16_t val)
{
	return pci_write_config(os->dev, reg, val, 2);
}

void
ocs_config_write32(ocs_os_handle_t os, uint32_t reg, uint32_t val)
{
	return pci_write_config(os->dev, reg, val, 4);
}

/**
 * @ingroup os
 * @brief Read a 32bit PCI register
 *
 * The SLI documentation uses the term "register set" to describe one or more
 * PCI BARs which form a logical address. For example, a 64-bit address uses
 * two BARs, and thus constitute a register set.
 *
 * @param ocs Pointer to the driver's context
 * @param rset Register Set to use
 * @param off Offset from the base address of the Register Set
 *
 * @return register value
 */
uint32_t
ocs_reg_read32(ocs_t *ocs, uint32_t rset, uint32_t off)
{
	ocs_pci_reg_t		*reg = NULL;

	reg = &ocs->reg[rset];

	return bus_space_read_4(reg->btag, reg->bhandle, off);
}

/**
 * @ingroup os
 * @brief Read a 16bit PCI register
 *
 * The SLI documentation uses the term "register set" to describe one or more
 * PCI BARs which form a logical address. For example, a 64-bit address uses
 * two BARs, and thus constitute a register set.
 *
 * @param ocs Pointer to the driver's context
 * @param rset Register Set to use
 * @param off Offset from the base address of the Register Set
 *
 * @return register value
 */
uint16_t
ocs_reg_read16(ocs_t *ocs, uint32_t rset, uint32_t off)
{
	ocs_pci_reg_t		*reg = NULL;

	reg = &ocs->reg[rset];

	return bus_space_read_2(reg->btag, reg->bhandle, off);
}

/**
 * @ingroup os
 * @brief Read a 8bit PCI register
 *
 * The SLI documentation uses the term "register set" to describe one or more
 * PCI BARs which form a logical address. For example, a 64-bit address uses
 * two BARs, and thus constitute a register set.
 *
 * @param ocs Pointer to the driver's context
 * @param rset Register Set to use
 * @param off Offset from the base address of the Register Set
 *
 * @return register value
 */
uint8_t
ocs_reg_read8(ocs_t *ocs, uint32_t rset, uint32_t off)
{
	ocs_pci_reg_t		*reg = NULL;

	reg = &ocs->reg[rset];

	return bus_space_read_1(reg->btag, reg->bhandle, off);
}

/**
 * @ingroup os
 * @brief Write a 32bit PCI register
 *
 * The SLI documentation uses the term "register set" to describe one or more
 * PCI BARs which form a logical address. For example, a 64-bit address uses
 * two BARs, and thus constitute a register set.
 *
 * @param ocs Pointer to the driver's context
 * @param rset Register Set to use
 * @param off Offset from the base address of the Register Set
 * @param val Value to write
 *
 * @return none
 */
void
ocs_reg_write32(ocs_t *ocs, uint32_t rset, uint32_t off, uint32_t val)
{
	ocs_pci_reg_t		*reg = NULL;

	reg = &ocs->reg[rset];

	return bus_space_write_4(reg->btag, reg->bhandle, off, val);
}

/**
 * @ingroup os
 * @brief Write a 16-bit PCI register
 *
 * The SLI documentation uses the term "register set" to describe one or more
 * PCI BARs which form a logical address. For example, a 64-bit address uses
 * two BARs, and thus constitute a register set.
 *
 * @param ocs Pointer to the driver's context
 * @param rset Register Set to use
 * @param off Offset from the base address of the Register Set
 * @param val Value to write
 *
 * @return none
 */
void
ocs_reg_write16(ocs_t *ocs, uint32_t rset, uint32_t off, uint16_t val)
{
	ocs_pci_reg_t		*reg = NULL;

	reg = &ocs->reg[rset];

	return bus_space_write_2(reg->btag, reg->bhandle, off, val);
}

/**
 * @ingroup os
 * @brief Write a 8-bit PCI register
 *
 * The SLI documentation uses the term "register set" to describe one or more
 * PCI BARs which form a logical address. For example, a 64-bit address uses
 * two BARs, and thus constitute a register set.
 *
 * @param ocs Pointer to the driver's context
 * @param rset Register Set to use
 * @param off Offset from the base address of the Register Set
 * @param val Value to write
 *
 * @return none
 */
void
ocs_reg_write8(ocs_t *ocs, uint32_t rset, uint32_t off, uint8_t val)
{
	ocs_pci_reg_t		*reg = NULL;

	reg = &ocs->reg[rset];

	return bus_space_write_1(reg->btag, reg->bhandle, off, val);
}

/**
 * @ingroup os
 * @brief Allocate host memory
 *
 * @param os OS handle
 * @param size number of bytes to allocate
 * @param flags additional options
 *
 * @return pointer to allocated memory, NULL otherwise
 */
void *
ocs_malloc(ocs_os_handle_t os, size_t size, int32_t flags)
{
	if ((flags & OCS_M_NOWAIT) == 0) {
		flags |= M_WAITOK;
	}

#ifndef OCS_DEBUG_MEMORY
	return malloc(size, M_OCS, flags);
#else
	char nameb[80];
	long offset = 0;
	void *addr = malloc(size, M_OCS, flags);

	linker_ddb_search_symbol_name(__builtin_return_address(1), nameb, sizeof(nameb), &offset);
	printf("A: %p %ld @ %s+%#lx\n", addr, size, nameb, offset);

	return addr;
#endif
}

/**
 * @ingroup os
 * @brief Free host memory
 *
 * @param os OS handle
 * @param addr pointer to memory
 * @param size bytes to free
 *
 * @note size ignored in BSD
 */
void
ocs_free(ocs_os_handle_t os, void *addr, size_t size)
{
#ifndef OCS_DEBUG_MEMORY
	free(addr, M_OCS);
#else
	printf("F: %p %ld\n", addr, size);
	free(addr, M_OCS);
#endif
}

/**
 * @brief Callback function provided to bus_dmamap_load
 *
 * Function loads the physical / bus address into the DMA descriptor. The caller
 * can detect a mapping failure if a descriptor's phys element is zero.
 *
 * @param arg Argument provided to bus_dmamap_load is a ocs_dma_t
 * @param seg Array of DMA segment(s), each describing segment's address and length
 * @param nseg Number of elements in array
 * @param error Indicates success (0) or failure of mapping
 */
static void
ocs_dma_load(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	ocs_dma_t	*dma = arg;

	if (error) {
		printf("%s: error=%d\n", __func__, error);
		dma->phys = 0;
	} else {
		dma->phys = seg->ds_addr;
	}
}

/**
 * @ingroup os
 * @brief Free a DMA capable block of memory
 *
 * @param os Device abstraction
 * @param dma DMA descriptor for memory to be freed
 *
 * @return 0 if memory is de-allocated, -1 otherwise
 */
int32_t
ocs_dma_free(ocs_os_handle_t os, ocs_dma_t *dma)
{
	struct ocs_softc	*ocs = os;

	if (!dma) {
		device_printf(ocs->dev, "%s: bad parameter(s) dma=%p\n", __func__, dma);
		return -1;
	}

	if (dma->size == 0) {
		return 0;
	}

	if (dma->map) {
		bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_POSTREAD |
				BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->tag, dma->map);
	}

	if (dma->virt) {
		bus_dmamem_free(dma->tag, dma->virt, dma->map);
		bus_dmamap_destroy(dma->tag, dma->map);
	}
	bus_dma_tag_destroy(dma->tag);

	bzero(dma, sizeof(ocs_dma_t));

	return 0;
}

/**
 * @ingroup os
 * @brief Allocate a DMA capable block of memory
 *
 * @param os Device abstraction
 * @param dma DMA descriptor containing results of memory allocation
 * @param size Size in bytes of desired allocation
 * @param align Alignment in bytes
 *
 * @return 0 on success, ENOMEM otherwise
 */
int32_t
ocs_dma_alloc(ocs_os_handle_t os, ocs_dma_t *dma, size_t size, size_t align)
{
	struct ocs_softc	*ocs = os;

	if (!dma || !size) {
		device_printf(ocs->dev, "%s bad parameter(s) dma=%p size=%zd\n",
				__func__, dma, size);
		return ENOMEM;
	}

	bzero(dma, sizeof(ocs_dma_t));

	/* create a "tag" that describes the desired memory allocation */
	if (bus_dma_tag_create(ocs->dmat, align, 0, BUS_SPACE_MAXADDR,
				BUS_SPACE_MAXADDR, NULL, NULL,
				size, 1, size, 0, NULL, NULL, &dma->tag)) {
		device_printf(ocs->dev, "DMA tag allocation failed\n");
		return ENOMEM;
	}

	dma->size = size;

	/* allocate the memory */
	if (bus_dmamem_alloc(dma->tag, &dma->virt, BUS_DMA_NOWAIT | BUS_DMA_COHERENT,
				&dma->map)) {
		device_printf(ocs->dev, "DMA memory allocation failed s=%zd a=%zd\n", size, align);
		ocs_dma_free(ocs, dma);
		return ENOMEM;
	}

	dma->alloc = dma->virt;

	/* map virtual address to device visible address */
	if (bus_dmamap_load(dma->tag, dma->map, dma->virt, dma->size, ocs_dma_load,
				dma, 0)) {
		device_printf(ocs->dev, "DMA memory load failed\n");
		ocs_dma_free(ocs, dma);
		return ENOMEM;
	}

	/* if the DMA map load callback fails, it sets the physical address to zero */
	if (0 == dma->phys) {
		device_printf(ocs->dev, "ocs_dma_load failed\n");
		ocs_dma_free(ocs, dma);
		return ENOMEM;
	}

	return 0;
}

/**
 * @ingroup os
 * @brief Synchronize the DMA buffer memory
 *
 * Ensures memory coherency between the CPU and device
 *
 * @param dma DMA descriptor of memory to synchronize
 * @param flags Describes direction of synchronization
 *   See BUS_DMA(9) for details
 *   - BUS_DMASYNC_PREWRITE
 *   - BUS_DMASYNC_POSTREAD
 */
void
ocs_dma_sync(ocs_dma_t *dma, uint32_t flags)
{
	bus_dmamap_sync(dma->tag, dma->map, flags);
}

int32_t
ocs_dma_copy_in(ocs_dma_t *dma, void *buffer, uint32_t buffer_length)
{
	if (!dma)
		return -1;
	if (!buffer)
		return -1;
	if (buffer_length == 0)
		return 0;
	if (buffer_length > dma->size)
		buffer_length = dma->size;
	ocs_memcpy(dma->virt, buffer, buffer_length);
	dma->len = buffer_length;
	return buffer_length;
}

int32_t
ocs_dma_copy_out(ocs_dma_t *dma, void *buffer, uint32_t buffer_length)
{
	if (!dma)
		return -1;
	if (!buffer)
		return -1;
	if (buffer_length == 0)
		return 0;
	if (buffer_length > dma->len)
		buffer_length = dma->len;
	ocs_memcpy(buffer, dma->virt, buffer_length);
	return buffer_length;
}

/**
 * @ingroup os
 * @brief Initialize a lock
 *
 * @param lock lock to initialize
 * @param name string identifier for the lock
 */
void
ocs_lock_init(void *os, ocs_lock_t *lock, const char *name, ...)
{
	va_list ap;

	va_start(ap, name);
	ocs_vsnprintf(lock->name, MAX_LOCK_DESC_LEN, name, ap);
	va_end(ap);

	mtx_init(&lock->lock, lock->name, NULL, MTX_DEF);
}

/**
 * @brief Allocate a bit map
 *
 * For BSD, this is a simple character string
 *
 * @param n_bits number of bits in bit map
 *
 * @return pointer to the bit map, NULL on error
 */
ocs_bitmap_t *
ocs_bitmap_alloc(uint32_t n_bits)
{

	return malloc(bitstr_size(n_bits), M_OCS, M_ZERO | M_NOWAIT);
}

/**
 * @brief Free a bit map
 *
 * @param bitmap pointer to previously allocated bit map
 */
void
ocs_bitmap_free(ocs_bitmap_t *bitmap)
{

	free(bitmap, M_OCS);
}

/**
 * @brief find next unset bit and set it
 *
 * @param bitmap bit map to search
 * @param n_bits number of bits in map
 *
 * @return bit position or -1 if map is full
 */
int32_t
ocs_bitmap_find(ocs_bitmap_t *bitmap, uint32_t n_bits)
{
	int32_t		position = -1;

	bit_ffc(bitmap, n_bits, &position);

	if (-1 != position) {
		bit_set(bitmap, position);
	}

	return position;
}

/**
 * @brief search for next (un)set bit
 *
 * @param bitmap bit map to search
 * @param set search for a set or unset bit
 * @param n_bits number of bits in map
 *
 * @return bit position or -1
 */
int32_t
ocs_bitmap_search(ocs_bitmap_t *bitmap, uint8_t set, uint32_t n_bits)
{
	int32_t		position;

	if (!bitmap) {
		return -1;
	}

	if (set) {
		bit_ffs(bitmap, n_bits, &position);
	} else {
		bit_ffc(bitmap, n_bits, &position);
	}

	return position;
}

/**
 * @brief clear the specified bit
 *
 * @param bitmap pointer to bit map
 * @param bit bit number to clear
 */
void
ocs_bitmap_clear(ocs_bitmap_t *bitmap, uint32_t bit)
{
	bit_clear(bitmap, bit);
}

void _ocs_log(ocs_t *ocs, const char *func_name, int line, const char *fmt, ...)
{
	va_list ap;
	char buf[256];
	char *p = buf;

	va_start(ap, fmt);

	/* TODO: Add Current PID info here. */

	p += snprintf(p, sizeof(buf) - (p - buf), "%s: ", DRV_NAME);
	p += snprintf(p, sizeof(buf) - (p - buf), "%s:", func_name);
	p += snprintf(p, sizeof(buf) - (p - buf), "%i:", line);
	p += snprintf(p, sizeof(buf) - (p - buf), "%s:", (ocs != NULL) ? device_get_nameunit(ocs->dev) : "");
	p += vsnprintf(p, sizeof(buf) - (p - buf), fmt, ap);

	va_end(ap);

	printf("%s", buf);
}

/**
 * @brief Common thread call function
 *
 * This is the common function called whenever a thread instantiated by ocs_thread_create() is started.
 * It captures the return value from the actual thread function and stashes it in the thread object, to
 * be later retrieved by ocs_thread_get_retval(), and calls kthread_exit(), the proscribed method to terminate
 * a thread.
 *
 * @param arg a pointer to the thread object
 *
 * @return none
 */

static void
ocs_thread_call_fctn(void *arg)
{
	ocs_thread_t *thread = arg;
	thread->retval = (*thread->fctn)(thread->arg);
	ocs_free(NULL, thread->name, ocs_strlen(thread->name+1));
	kthread_exit();
}

/**
 * @brief Create a kernel thread
 *
 * Creates a kernel thread and optionally starts it.   If the thread is not immediately
 * started, ocs_thread_start() should be called at some later point.
 *
 * @param os OS handle
 * @param thread pointer to thread object
 * @param fctn function for thread to be begin executing
 * @param name text name to identify thread
 * @param arg application specific argument passed to thread function
 * @param start start option, OCS_THREAD_RUN will start the thread immediately,
 *			OCS_THREAD_CREATE will create but not start the thread
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_thread_create(ocs_os_handle_t os, ocs_thread_t *thread, ocs_thread_fctn fctn, const char *name, void *arg, ocs_thread_start_e start)
{
	int32_t rc = 0;

	ocs_memset(thread, 0, sizeof(*thread));

	thread->fctn = fctn;
	thread->name = ocs_strdup(name);
	if (thread->name == NULL) {
		thread->name = "unknown";
	}
	thread->arg = arg;

	ocs_atomic_set(&thread->terminate, 0);

	rc = kthread_add(ocs_thread_call_fctn, thread, NULL, &thread->tcb, (start == OCS_THREAD_CREATE) ? RFSTOPPED : 0,
		OCS_THREAD_DEFAULT_STACK_SIZE_PAGES, "%s", name);

	return rc;
}

/**
 * @brief Start a thread
 *
 * Starts a thread that was created with OCS_THREAD_CREATE rather than OCS_THREAD_RUN
 *
 * @param thread pointer to thread object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t ocs_thread_start(ocs_thread_t *thread)
{
	sched_add(thread->tcb, SRQ_BORING);
	return 0;
}

/**
 * @brief return thread argument
 *
 * Returns a pointer to the thread's application specific argument
 *
 * @param mythread pointer to the thread object
 *
 * @return pointer to application specific argument
 */

void *ocs_thread_get_arg(ocs_thread_t *mythread)
{
	return mythread->arg;
}

/**
 * @brief Request thread stop
 *
 * A stop request is made to the thread.  This is a voluntary call, the thread needs
 * to periodically query its terminate request using ocs_thread_terminate_requested()
 *
 * @param thread pointer to thread object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_thread_terminate(ocs_thread_t *thread)
{
	ocs_atomic_set(&thread->terminate, 1);
	return 0;
}

/**
 * @brief See if a terminate request has been made
 *
 * Check to see if a stop request has been made to the current thread.  This
 * function would be used by a thread to see if it should terminate.
 *
 * @return returns non-zero if a stop has been requested
 */

int32_t ocs_thread_terminate_requested(ocs_thread_t *thread)
{
	return ocs_atomic_read(&thread->terminate);
}

/**
 * @brief Retrieve threads return value
 *
 * After a thread has terminated, it's return value may be retrieved with this function.
 *
 * @param thread pointer to thread object
 *
 * @return return value from thread function
 */

int32_t
ocs_thread_get_retval(ocs_thread_t *thread)
{
	return thread->retval;
}

/**
 * @brief Request that the currently running thread yield
 *
 * The currently running thread yields to the scheduler
 *
 * @param thread pointer to thread (ignored)
 *
 * @return none
 */

void
ocs_thread_yield(ocs_thread_t *thread) {
	pause("thread yield", 1);
}

ocs_thread_t *
ocs_thread_self(void)
{
	ocs_printf(">>> %s not implemented\n", __func__);
	ocs_abort();
}

int32_t
ocs_thread_setcpu(ocs_thread_t *thread, uint32_t cpu)
{
	ocs_printf(">>> %s not implemented\n", __func__);
	return -1;
}

int32_t
ocs_thread_getcpu(void)
{
	return curcpu;
}

int
ocs_sem_init(ocs_sem_t *sem, int val, const char *name, ...)
{
	va_list ap;

	va_start(ap, name);
	ocs_vsnprintf(sem->name, sizeof(sem->name), name, ap);
	va_end(ap);

	sema_init(&sem->sem, val, sem->name);
	return 0;
}

/**
 * @ingroup os
 * @brief  Copy user arguments in to kernel space for an ioctl
 * @par Description
 * This function is called at the beginning of an ioctl function
 * to copy the ioctl argument from user space to kernel space.
 *
 * BSD handles this for us - arg is already in kernel space,
 * so we just return it.
 *
 * @param os OS handle
 * @param arg The argument passed to the ioctl function
 * @param size The size of the structure pointed to by arg
 *
 * @return A pointer to a kernel space copy of the argument on
 *	success; NULL on failure
 */
void *ocs_ioctl_preprocess(ocs_os_handle_t os, void *arg, size_t size)
{
	 return arg;
}

/**
 * @ingroup os
 * @brief  Copy results of an ioctl back to user space
 * @par Description
 * This function is called at the end of ioctl processing to
 * copy the argument back to user space.
 *
 * BSD handles this for us.
 *
 * @param os OS handle
 * @param arg The argument passed to the ioctl function
 * @param kern_ptr A pointer to the kernel space copy of the
 *		   argument
 * @param size The size of the structure pointed to by arg.
 *
 * @return Returns 0.
 */
int32_t ocs_ioctl_postprocess(ocs_os_handle_t os, void *arg, void *kern_ptr, size_t size)
{
	return 0;
}

/**
 * @ingroup os
 * @brief  Free memory allocated by ocs_ioctl_preprocess
 * @par Description
 * This function is called in the event of an error in ioctl
 * processing.  For operating environments where ocs_ioctlpreprocess
 * allocates memory, this call frees the memory without copying
 * results back to user space.
 *
 * For BSD, because no memory was allocated in ocs_ioctl_preprocess,
 * nothing needs to be done here.
 *
 * @param os OS handle
 * @param kern_ptr A pointer to the kernel space copy of the
 *		   argument
 * @param size The size of the structure pointed to by arg.
 *
 * @return Returns nothing.
 */
void ocs_ioctl_free(ocs_os_handle_t os, void *kern_ptr, size_t size)
{
	return;
}

void ocs_intr_disable(ocs_os_handle_t os)
{
}

void ocs_intr_enable(ocs_os_handle_t os)
{
}

void ocs_print_stack(void)
{
#if defined(STACK)
	struct stack st;

	stack_zero(&st);
	stack_save(&st);
	stack_print(&st);
#endif
}

void ocs_abort(void)
{
	panic(">>> abort/panic\n");
}

const char *
ocs_pci_model(uint16_t vendor, uint16_t device)
{
	switch (device) {
	case PCI_PRODUCT_EMULEX_OCE16002:	return "OCE16002";
	case PCI_PRODUCT_EMULEX_OCE1600_VF:	return "OCE1600_VF";
	case PCI_PRODUCT_EMULEX_OCE50102:	return "OCE50102";
	case PCI_PRODUCT_EMULEX_OCE50102_VF:	return "OCE50102_VR";
	default:
		break;
	}

	return "unknown";
}

int32_t
ocs_get_bus_dev_func(ocs_t *ocs, uint8_t* bus, uint8_t* dev, uint8_t* func)
{
	*bus = pci_get_bus(ocs->dev);
	*dev = pci_get_slot(ocs->dev);
	*func= pci_get_function(ocs->dev);
	return 0;
}

/**
 * @brief return CPU information
 *
 * This function populates the ocs_cpuinfo_t buffer with CPU information
 *
 * @param cpuinfo pointer to ocs_cpuinfo_t buffer
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
extern int mp_ncpus;
int32_t
ocs_get_cpuinfo(ocs_cpuinfo_t *cpuinfo)
{
	cpuinfo->num_cpus = mp_ncpus;
	return 0;
}

uint32_t
ocs_get_num_cpus(void)
{
	static ocs_cpuinfo_t cpuinfo;

	if (cpuinfo.num_cpus == 0) {
		ocs_get_cpuinfo(&cpuinfo);
	}
	return cpuinfo.num_cpus;
}


void
__ocs_callout(void *t)
{
	ocs_timer_t *timer = t;

	if (callout_pending(&timer->callout)) {
		/* Callout was reset */
		return;
	}

	if (!callout_active(&timer->callout)) {
		/* Callout was stopped */
		return;
	}

	callout_deactivate(&timer->callout);

	if (timer->func) {
		timer->func(timer->data);
	}
}

int32_t
ocs_setup_timer(ocs_os_handle_t os, ocs_timer_t *timer, void(*func)(void *arg), void *data, uint32_t timeout_ms)
{
	struct	timeval tv;
	int	hz;

	if (timer == NULL) {
		ocs_log_err(NULL, "bad parameter\n");
		return -1;
	}

	if (!mtx_initialized(&timer->lock)) {
		mtx_init(&timer->lock, "ocs_timer", NULL, MTX_DEF);
	}

	callout_init_mtx(&timer->callout, &timer->lock, 0);

	timer->func = func;
	timer->data = data;

	tv.tv_sec  = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;

	hz = tvtohz(&tv);
	if (hz < 0)
		hz = INT32_MAX;
	if (hz == 0)
		hz = 1;

	mtx_lock(&timer->lock);
		callout_reset(&timer->callout, hz, __ocs_callout, timer);
	mtx_unlock(&timer->lock);

	return 0;
}

int32_t
ocs_mod_timer(ocs_timer_t *timer, uint32_t timeout_ms)
{
	struct	timeval tv;
	int	hz;

	if (timer == NULL) {
		ocs_log_err(NULL, "bad parameter\n");
		return -1;
	}

	tv.tv_sec  = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;

	hz = tvtohz(&tv);
	if (hz < 0)
		hz = INT32_MAX;
	if (hz == 0)
		hz = 1;

	mtx_lock(&timer->lock);
		callout_reset(&timer->callout, hz, __ocs_callout, timer);
	mtx_unlock(&timer->lock);

	return 0;
}

int32_t
ocs_timer_pending(ocs_timer_t *timer)
{
	return callout_active(&timer->callout);
}

int32_t
ocs_del_timer(ocs_timer_t *timer)
{

	mtx_lock(&timer->lock);
		callout_stop(&timer->callout);
	mtx_unlock(&timer->lock);

	return 0;
}

char *
ocs_strdup(const char *s)
{
	uint32_t l = strlen(s);
	char *d;

	d = ocs_malloc(NULL, l+1, OCS_M_NOWAIT);
	if (d != NULL) {
		ocs_strcpy(d, s);
	}
	return d;
}

void
_ocs_assert(const char *cond, const char *filename, int linenum)
{
	const char *fn = strrchr(__FILE__, '/');

	ocs_log_err(NULL, "%s(%d) assertion (%s) failed\n", (fn ? fn + 1 : filename), linenum, cond);
	ocs_print_stack();
	ocs_save_ddump_all(OCS_DDUMP_FLAGS_WQES|OCS_DDUMP_FLAGS_CQES|OCS_DDUMP_FLAGS_MQES, -1, TRUE);
}
