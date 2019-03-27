/*-
 * HighPoint RAID Driver for FreeBSD
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2005-2011 HighPoint Technologies, Inc. All Rights Reserved.
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
 *
 * $FreeBSD$
 */

#include <dev/hpt27xx/hpt27xx_config.h>

#include <dev/hpt27xx/os_bsd.h>

BUS_ADDRESS get_dmapool_phy_addr(void *osext, void * dmapool_virt_addr);

/* hardware access */
HPT_U8   os_inb  (void *port) { return inb((unsigned)(HPT_UPTR)port); }
HPT_U16  os_inw  (void *port) { return inw((unsigned)(HPT_UPTR)port); }
HPT_U32  os_inl  (void *port) { return inl((unsigned)(HPT_UPTR)port); }

void os_outb (void *port, HPT_U8 value) { outb((unsigned)(HPT_UPTR)port, (value)); }
void os_outw (void *port, HPT_U16 value) { outw((unsigned)(HPT_UPTR)port, (value)); }
void os_outl (void *port, HPT_U32 value) { outl((unsigned)(HPT_UPTR)port, (value)); }

void os_insw (void *port, HPT_U16 *buffer, HPT_U32 count)
{ insw((unsigned)(HPT_UPTR)port, (void *)buffer, count); }

void os_outsw(void *port, HPT_U16 *buffer, HPT_U32 count)
{ outsw((unsigned)(HPT_UPTR)port, (void *)buffer, count); }

HPT_U32 __dummy_reg = 0;

/* PCI configuration space */
HPT_U8  os_pci_readb (void *osext, HPT_U8 offset)
{
    return  pci_read_config(((PHBA)osext)->pcidev, offset, 1);
}

HPT_U16 os_pci_readw (void *osext, HPT_U8 offset)
{
    return  pci_read_config(((PHBA)osext)->pcidev, offset, 2);
}

HPT_U32 os_pci_readl (void *osext, HPT_U8 offset)
{
    return  pci_read_config(((PHBA)osext)->pcidev, offset, 4);
}

void os_pci_writeb (void *osext, HPT_U8 offset, HPT_U8 value)
{
    pci_write_config(((PHBA)osext)->pcidev, offset, value, 1);
}

void os_pci_writew (void *osext, HPT_U8 offset, HPT_U16 value)
{
    pci_write_config(((PHBA)osext)->pcidev, offset, value, 2);
}

void os_pci_writel (void *osext, HPT_U8 offset, HPT_U32 value)
{
    pci_write_config(((PHBA)osext)->pcidev, offset, value, 4);
}

BUS_ADDRESS get_dmapool_phy_addr(void *osext, void * dmapool_virt_addr)
{
	return (BUS_ADDRESS)vtophys(dmapool_virt_addr);
}

/* PCI space access */
HPT_U8 pcicfg_read_byte (HPT_U8 bus, HPT_U8 dev, HPT_U8 func, HPT_U8 reg)
{
	return (HPT_U8)pci_cfgregread(bus, dev, func, reg, 1);
}
HPT_U32 pcicfg_read_dword(HPT_U8 bus, HPT_U8 dev, HPT_U8 func, HPT_U8 reg)
{
	return (HPT_U32)pci_cfgregread(bus, dev, func, reg, 4);
}
void pcicfg_write_byte (HPT_U8 bus, HPT_U8 dev, HPT_U8 func, HPT_U8 reg, HPT_U8 v)
{
	pci_cfgregwrite(bus, dev, func, reg, v, 1);
}
void pcicfg_write_dword(HPT_U8 bus, HPT_U8 dev, HPT_U8 func, HPT_U8 reg, HPT_U32 v)
{
	pci_cfgregwrite(bus, dev, func, reg, v, 4);
}/* PCI space access */

void *os_map_pci_bar(
    void *osext, 
    int index,   
    HPT_U32 offset,
    HPT_U32 length
)
{
	PHBA hba = (PHBA)osext;
	HPT_U32 base;

	hba->pcibar[index].rid = 0x10 + index * 4;
	base = pci_read_config(hba->pcidev, hba->pcibar[index].rid, 4);

	if (base & 1) {
		hba->pcibar[index].type = SYS_RES_IOPORT;
		hba->pcibar[index].res = bus_alloc_resource_any(hba->pcidev,
			hba->pcibar[index].type, &hba->pcibar[index].rid, RF_ACTIVE);
		hba->pcibar[index].base = (void *)(unsigned long)(base & ~0x1);
	} else {
		hba->pcibar[index].type = SYS_RES_MEMORY;
		hba->pcibar[index].res = bus_alloc_resource_any(hba->pcidev,
			hba->pcibar[index].type, &hba->pcibar[index].rid, RF_ACTIVE);
		hba->pcibar[index].base = (char *)rman_get_virtual(hba->pcibar[index].res) + offset;
	}

	return hba->pcibar[index].base;
}

void os_unmap_pci_bar(void *osext, void *base)
{
	PHBA hba = (PHBA)osext;
	int index;
	
	for (index=0; index<6; index++) {
		if (hba->pcibar[index].base==base) {
			bus_release_resource(hba->pcidev, hba->pcibar[index].type,
				hba->pcibar[index].rid, hba->pcibar[index].res);
			hba->pcibar[index].base = 0;
			return;
		}
	}
}

void freelist_reserve(struct freelist *list, void *osext, HPT_UINT size, HPT_UINT count)
{
    PVBUS_EXT vbus_ext = osext;

    if (vbus_ext->ext_type!=EXT_TYPE_VBUS)
        vbus_ext = ((PHBA)osext)->vbus_ext;

    list->next = vbus_ext->freelist_head;
    vbus_ext->freelist_head = list;
    list->dma = 0;
    list->size = size;
    list->head = 0;
#if DBG
    list->reserved_count =
#endif
    list->count = count;
}

void *freelist_get(struct freelist *list)
{
    void * result;
    if (list->count) {
        HPT_ASSERT(list->head);
        result = list->head;
        list->head = *(void **)result;
        list->count--;
        return result;
    }
    return 0;
}

void freelist_put(struct freelist * list, void *p)
{
    HPT_ASSERT(list->dma==0);
    list->count++;
    *(void **)p = list->head;
    list->head = p;
}

void freelist_reserve_dma(struct freelist *list, void *osext, HPT_UINT size, HPT_UINT alignment, HPT_UINT count)
{
    PVBUS_EXT vbus_ext = osext;

    if (vbus_ext->ext_type!=EXT_TYPE_VBUS)
        vbus_ext = ((PHBA)osext)->vbus_ext;

    list->next = vbus_ext->freelist_dma_head;
    vbus_ext->freelist_dma_head = list;
    list->dma = 1;
    list->alignment = alignment;
    list->size = size;
    list->head = 0;
#if DBG
    list->reserved_count =
#endif
    list->count = count;
}

void *freelist_get_dma(struct freelist *list, BUS_ADDRESS *busaddr)
{
    void *result;
    HPT_ASSERT(list->dma);
    result = freelist_get(list);
    if (result)
        *busaddr = *(BUS_ADDRESS *)((void **)result+1);
    return result;
}

void freelist_put_dma(struct freelist *list, void *p, BUS_ADDRESS busaddr)
{
    HPT_ASSERT(list->dma);
    list->count++;
    *(void **)p = list->head;
    *(BUS_ADDRESS *)((void **)p+1) = busaddr;
    list->head = p;
}

HPT_U32 os_get_stamp(void)
{
    HPT_U32 stamp;
    do { stamp = random(); } while (stamp==0);
    return stamp;
}

void os_stallexec(HPT_U32 microseconds)
{
    DELAY(microseconds);
}

static void os_timer_for_ldm(void *arg)
{
	PVBUS_EXT vbus_ext = (PVBUS_EXT)arg;
	ldm_on_timer((PVBUS)vbus_ext->vbus);
}

void  os_request_timer(void * osext, HPT_U32 interval)
{
	PVBUS_EXT vbus_ext = osext;

	HPT_ASSERT(vbus_ext->ext_type==EXT_TYPE_VBUS);

#if (__FreeBSD_version >= 1000510)
	callout_reset_sbt(&vbus_ext->timer, SBT_1US * interval, 0,
	    os_timer_for_ldm, vbus_ext, 0);
#else 
	untimeout(os_timer_for_ldm, vbus_ext, vbus_ext->timer);
	vbus_ext->timer = timeout(os_timer_for_ldm, vbus_ext, interval * hz / 1000000);
#endif
}

HPT_TIME os_query_time(void)
{
	return ticks * (1000000 / hz);
}

void os_schedule_task(void *osext, OSM_TASK *task)
{
	PVBUS_EXT vbus_ext = osext;
	
	HPT_ASSERT(task->next==0);
	
	if (vbus_ext->tasks==0)
		vbus_ext->tasks = task;
	else {
		OSM_TASK *t = vbus_ext->tasks;
		while (t->next) t = t->next;
		t->next = task;
	}

	if (vbus_ext->worker.ta_context)
		TASK_ENQUEUE(&vbus_ext->worker);
}

int os_revalidate_device(void *osext, int id)
{

    return 0;
}

int os_query_remove_device(void *osext, int id)
{
	return 0;
}

HPT_U8 os_get_vbus_seq(void *osext)
{
    return ((PVBUS_EXT)osext)->sim->path_id;
}

int  os_printk(char *fmt, ...)
{
    va_list args;
    static char buf[512];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return printf("%s: %s\n", driver_name, buf);
}

#if DBG
void os_check_stack(const char *location, int size){}

void __os_dbgbreak(const char *file, int line)
{
    printf("*** break at %s:%d ***", file, line);
    while (1);
}

int hpt_dbg_level = 1;
#endif
