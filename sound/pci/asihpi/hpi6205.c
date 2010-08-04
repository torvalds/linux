/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2010  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 Hardware Programming Interface (HPI) for AudioScience
 ASI50xx, AS51xx, ASI6xxx, ASI87xx ASI89xx series adapters.
 These PCI and PCIe bus adapters are based on a
 TMS320C6205 PCI bus mastering DSP,
 and (except ASI50xx) TI TMS320C6xxx floating point DSP

 Exported function:
 void HPI_6205(struct hpi_message *phm, struct hpi_response *phr)

(C) Copyright AudioScience Inc. 1998-2010
*******************************************************************************/
#define SOURCEFILE_NAME "hpi6205.c"

#include "hpi_internal.h"
#include "hpimsginit.h"
#include "hpidebug.h"
#include "hpi6205.h"
#include "hpidspcd.h"
#include "hpicmn.h"

/*****************************************************************************/
/* HPI6205 specific error codes */
#define HPI6205_ERROR_BASE                      1000
/*#define HPI6205_ERROR_MEM_ALLOC 1001 */
#define HPI6205_ERROR_6205_NO_IRQ               1002
#define HPI6205_ERROR_6205_INIT_FAILED          1003
/*#define HPI6205_ERROR_MISSING_DSPCODE 1004 */
#define HPI6205_ERROR_UNKNOWN_PCI_DEVICE        1005
#define HPI6205_ERROR_6205_REG                  1006
#define HPI6205_ERROR_6205_DSPPAGE              1007
#define HPI6205_ERROR_BAD_DSPINDEX              1008
#define HPI6205_ERROR_C6713_HPIC                1009
#define HPI6205_ERROR_C6713_HPIA                1010
#define HPI6205_ERROR_C6713_PLL                 1011
#define HPI6205_ERROR_DSP_INTMEM                1012
#define HPI6205_ERROR_DSP_EXTMEM                1013
#define HPI6205_ERROR_DSP_PLD                   1014
#define HPI6205_ERROR_MSG_RESP_IDLE_TIMEOUT     1015
#define HPI6205_ERROR_MSG_RESP_TIMEOUT          1016
#define HPI6205_ERROR_6205_EEPROM               1017
#define HPI6205_ERROR_DSP_EMIF                  1018

#define hpi6205_error(dsp_index, err) (err)
/*****************************************************************************/
/* for C6205 PCI i/f */
/* Host Status Register (HSR) bitfields */
#define C6205_HSR_INTSRC        0x01
#define C6205_HSR_INTAVAL       0x02
#define C6205_HSR_INTAM         0x04
#define C6205_HSR_CFGERR        0x08
#define C6205_HSR_EEREAD        0x10
/* Host-to-DSP Control Register (HDCR) bitfields */
#define C6205_HDCR_WARMRESET    0x01
#define C6205_HDCR_DSPINT       0x02
#define C6205_HDCR_PCIBOOT      0x04
/* DSP Page Register (DSPP) bitfields, */
/* defines 4 Mbyte page that BAR0 points to */
#define C6205_DSPP_MAP1         0x400

/* BAR0 maps to prefetchable 4 Mbyte memory block set by DSPP.
 * BAR1 maps to non-prefetchable 8 Mbyte memory block
 * of DSP memory mapped registers (starting at 0x01800000).
 * 0x01800000 is hardcoded in the PCI i/f, so that only the offset from this
 * needs to be added to the BAR1 base address set in the PCI config reg
 */
#define C6205_BAR1_PCI_IO_OFFSET (0x027FFF0L)
#define C6205_BAR1_HSR  (C6205_BAR1_PCI_IO_OFFSET)
#define C6205_BAR1_HDCR (C6205_BAR1_PCI_IO_OFFSET+4)
#define C6205_BAR1_DSPP (C6205_BAR1_PCI_IO_OFFSET+8)

/* used to control LED (revA) and reset C6713 (revB) */
#define C6205_BAR0_TIMER1_CTL (0x01980000L)

/* For first 6713 in CE1 space, using DA17,16,2 */
#define HPICL_ADDR      0x01400000L
#define HPICH_ADDR      0x01400004L
#define HPIAL_ADDR      0x01410000L
#define HPIAH_ADDR      0x01410004L
#define HPIDIL_ADDR     0x01420000L
#define HPIDIH_ADDR     0x01420004L
#define HPIDL_ADDR      0x01430000L
#define HPIDH_ADDR      0x01430004L

#define C6713_EMIF_GCTL         0x01800000
#define C6713_EMIF_CE1          0x01800004
#define C6713_EMIF_CE0          0x01800008
#define C6713_EMIF_CE2          0x01800010
#define C6713_EMIF_CE3          0x01800014
#define C6713_EMIF_SDRAMCTL     0x01800018
#define C6713_EMIF_SDRAMTIMING  0x0180001C
#define C6713_EMIF_SDRAMEXT     0x01800020

struct hpi_hw_obj {
	/* PCI registers */
	__iomem u32 *prHSR;
	__iomem u32 *prHDCR;
	__iomem u32 *prDSPP;

	u32 dsp_page;

	struct consistent_dma_area h_locked_mem;
	struct bus_master_interface *p_interface_buffer;

	u16 flag_outstream_just_reset[HPI_MAX_STREAMS];
	/* a non-NULL handle means there is an HPI allocated buffer */
	struct consistent_dma_area instream_host_buffers[HPI_MAX_STREAMS];
	struct consistent_dma_area outstream_host_buffers[HPI_MAX_STREAMS];
	/* non-zero size means a buffer exists, may be external */
	u32 instream_host_buffer_size[HPI_MAX_STREAMS];
	u32 outstream_host_buffer_size[HPI_MAX_STREAMS];

	struct consistent_dma_area h_control_cache;
	struct consistent_dma_area h_async_event_buffer;
/*      struct hpi_control_cache_single *pControlCache; */
	struct hpi_async_event *p_async_event_buffer;
	struct hpi_control_cache *p_cache;
};

/*****************************************************************************/
/* local prototypes */

#define check_before_bbm_copy(status, p_bbm_data, l_first_write, l_second_write)

static int wait_dsp_ack(struct hpi_hw_obj *phw, int state, int timeout_us);

static void send_dsp_command(struct hpi_hw_obj *phw, int cmd);

static u16 adapter_boot_load_dsp(struct hpi_adapter_obj *pao,
	u32 *pos_error_code);

static u16 message_response_sequence(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void hw_message(struct hpi_adapter_obj *pao, struct hpi_message *phm,
	struct hpi_response *phr);

#define HPI6205_TIMEOUT 1000000

static void subsys_create_adapter(struct hpi_message *phm,
	struct hpi_response *phr);
static void subsys_delete_adapter(struct hpi_message *phm,
	struct hpi_response *phr);

static u16 create_adapter_obj(struct hpi_adapter_obj *pao,
	u32 *pos_error_code);

static void delete_adapter_obj(struct hpi_adapter_obj *pao);

static void outstream_host_buffer_allocate(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void outstream_host_buffer_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void outstream_host_buffer_free(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);
static void outstream_write(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void outstream_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void outstream_start(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void outstream_open(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void outstream_reset(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void instream_host_buffer_allocate(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void instream_host_buffer_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void instream_host_buffer_free(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void instream_read(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void instream_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void instream_start(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static u32 boot_loader_read_mem32(struct hpi_adapter_obj *pao, int dsp_index,
	u32 address);

static u16 boot_loader_write_mem32(struct hpi_adapter_obj *pao, int dsp_index,
	u32 address, u32 data);

static u16 boot_loader_config_emif(struct hpi_adapter_obj *pao,
	int dsp_index);

static u16 boot_loader_test_memory(struct hpi_adapter_obj *pao, int dsp_index,
	u32 address, u32 length);

static u16 boot_loader_test_internal_memory(struct hpi_adapter_obj *pao,
	int dsp_index);

static u16 boot_loader_test_external_memory(struct hpi_adapter_obj *pao,
	int dsp_index);

static u16 boot_loader_test_pld(struct hpi_adapter_obj *pao, int dsp_index);

/*****************************************************************************/

static void subsys_message(struct hpi_message *phm, struct hpi_response *phr)
{

	switch (phm->function) {
	case HPI_SUBSYS_OPEN:
	case HPI_SUBSYS_CLOSE:
	case HPI_SUBSYS_GET_INFO:
	case HPI_SUBSYS_DRIVER_UNLOAD:
	case HPI_SUBSYS_DRIVER_LOAD:
	case HPI_SUBSYS_FIND_ADAPTERS:
		/* messages that should not get here */
		phr->error = HPI_ERROR_UNIMPLEMENTED;
		break;
	case HPI_SUBSYS_CREATE_ADAPTER:
		subsys_create_adapter(phm, phr);
		break;
	case HPI_SUBSYS_DELETE_ADAPTER:
		subsys_delete_adapter(phm, phr);
		break;
	default:
		phr->error = HPI_ERROR_INVALID_FUNC;
		break;
	}
}

static void control_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{

	struct hpi_hw_obj *phw = pao->priv;

	switch (phm->function) {
	case HPI_CONTROL_GET_STATE:
		if (pao->has_control_cache) {
			rmb();	/* make sure we see updates DM_aed from DSP */
			if (hpi_check_control_cache(phw->p_cache, phm, phr))
				break;
		}
		hw_message(pao, phm, phr);
		break;
	case HPI_CONTROL_GET_INFO:
		hw_message(pao, phm, phr);
		break;
	case HPI_CONTROL_SET_STATE:
		hw_message(pao, phm, phr);
		if (pao->has_control_cache)
			hpi_sync_control_cache(phw->p_cache, phm, phr);
		break;
	default:
		phr->error = HPI_ERROR_INVALID_FUNC;
		break;
	}
}

static void adapter_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	switch (phm->function) {
	default:
		hw_message(pao, phm, phr);
		break;
	}
}

static void outstream_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{

	if (phm->obj_index >= HPI_MAX_STREAMS) {
		phr->error = HPI_ERROR_INVALID_STREAM;
		HPI_DEBUG_LOG(WARNING,
			"message referencing invalid stream %d "
			"on adapter index %d\n", phm->obj_index,
			phm->adapter_index);
		return;
	}

	switch (phm->function) {
	case HPI_OSTREAM_WRITE:
		outstream_write(pao, phm, phr);
		break;
	case HPI_OSTREAM_GET_INFO:
		outstream_get_info(pao, phm, phr);
		break;
	case HPI_OSTREAM_HOSTBUFFER_ALLOC:
		outstream_host_buffer_allocate(pao, phm, phr);
		break;
	case HPI_OSTREAM_HOSTBUFFER_GET_INFO:
		outstream_host_buffer_get_info(pao, phm, phr);
		break;
	case HPI_OSTREAM_HOSTBUFFER_FREE:
		outstream_host_buffer_free(pao, phm, phr);
		break;
	case HPI_OSTREAM_START:
		outstream_start(pao, phm, phr);
		break;
	case HPI_OSTREAM_OPEN:
		outstream_open(pao, phm, phr);
		break;
	case HPI_OSTREAM_RESET:
		outstream_reset(pao, phm, phr);
		break;
	default:
		hw_message(pao, phm, phr);
		break;
	}
}

static void instream_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{

	if (phm->obj_index >= HPI_MAX_STREAMS) {
		phr->error = HPI_ERROR_INVALID_STREAM;
		HPI_DEBUG_LOG(WARNING,
			"message referencing invalid stream %d "
			"on adapter index %d\n", phm->obj_index,
			phm->adapter_index);
		return;
	}

	switch (phm->function) {
	case HPI_ISTREAM_READ:
		instream_read(pao, phm, phr);
		break;
	case HPI_ISTREAM_GET_INFO:
		instream_get_info(pao, phm, phr);
		break;
	case HPI_ISTREAM_HOSTBUFFER_ALLOC:
		instream_host_buffer_allocate(pao, phm, phr);
		break;
	case HPI_ISTREAM_HOSTBUFFER_GET_INFO:
		instream_host_buffer_get_info(pao, phm, phr);
		break;
	case HPI_ISTREAM_HOSTBUFFER_FREE:
		instream_host_buffer_free(pao, phm, phr);
		break;
	case HPI_ISTREAM_START:
		instream_start(pao, phm, phr);
		break;
	default:
		hw_message(pao, phm, phr);
		break;
	}
}

/*****************************************************************************/
/** Entry point to this HPI backend
 * All calls to the HPI start here
 */
void HPI_6205(struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_adapter_obj *pao = NULL;

	/* subsytem messages are processed by every HPI.
	 * All other messages are ignored unless the adapter index matches
	 * an adapter in the HPI
	 */
	HPI_DEBUG_LOG(DEBUG, "HPI obj=%d, func=%d\n", phm->object,
		phm->function);

	/* if Dsp has crashed then do not communicate with it any more */
	if (phm->object != HPI_OBJ_SUBSYSTEM) {
		pao = hpi_find_adapter(phm->adapter_index);
		if (!pao) {
			HPI_DEBUG_LOG(DEBUG,
				" %d,%d refused, for another HPI?\n",
				phm->object, phm->function);
			return;
		}

		if ((pao->dsp_crashed >= 10)
			&& (phm->function != HPI_ADAPTER_DEBUG_READ)) {
			/* allow last resort debug read even after crash */
			hpi_init_response(phr, phm->object, phm->function,
				HPI_ERROR_DSP_HARDWARE);
			HPI_DEBUG_LOG(WARNING, " %d,%d dsp crashed.\n",
				phm->object, phm->function);
			return;
		}
	}

	/* Init default response  */
	if (phm->function != HPI_SUBSYS_CREATE_ADAPTER)
		hpi_init_response(phr, phm->object, phm->function,
			HPI_ERROR_PROCESSING_MESSAGE);

	HPI_DEBUG_LOG(VERBOSE, "start of switch\n");
	switch (phm->type) {
	case HPI_TYPE_MESSAGE:
		switch (phm->object) {
		case HPI_OBJ_SUBSYSTEM:
			subsys_message(phm, phr);
			break;

		case HPI_OBJ_ADAPTER:
			phr->size =
				sizeof(struct hpi_response_header) +
				sizeof(struct hpi_adapter_res);
			adapter_message(pao, phm, phr);
			break;

		case HPI_OBJ_CONTROLEX:
		case HPI_OBJ_CONTROL:
			control_message(pao, phm, phr);
			break;

		case HPI_OBJ_OSTREAM:
			outstream_message(pao, phm, phr);
			break;

		case HPI_OBJ_ISTREAM:
			instream_message(pao, phm, phr);
			break;

		default:
			hw_message(pao, phm, phr);
			break;
		}
		break;

	default:
		phr->error = HPI_ERROR_INVALID_TYPE;
		break;
	}
}

/*****************************************************************************/
/* SUBSYSTEM */

/** Create an adapter object and initialise it based on resource information
 * passed in in the message
 * *** NOTE - you cannot use this function AND the FindAdapters function at the
 * same time, the application must use only one of them to get the adapters ***
 */
static void subsys_create_adapter(struct hpi_message *phm,
	struct hpi_response *phr)
{
	/* create temp adapter obj, because we don't know what index yet */
	struct hpi_adapter_obj ao;
	u32 os_error_code;
	u16 err;

	HPI_DEBUG_LOG(DEBUG, " subsys_create_adapter\n");

	memset(&ao, 0, sizeof(ao));

	/* this HPI only creates adapters for TI/PCI devices */
	if (phm->u.s.resource.bus_type != HPI_BUS_PCI)
		return;
	if (phm->u.s.resource.r.pci->vendor_id != HPI_PCI_VENDOR_ID_TI)
		return;
	if (phm->u.s.resource.r.pci->device_id != HPI_PCI_DEV_ID_DSP6205)
		return;

	ao.priv = kzalloc(sizeof(struct hpi_hw_obj), GFP_KERNEL);
	if (!ao.priv) {
		HPI_DEBUG_LOG(ERROR, "cant get mem for adapter object\n");
		phr->error = HPI_ERROR_MEMORY_ALLOC;
		return;
	}

	ao.pci = *phm->u.s.resource.r.pci;
	err = create_adapter_obj(&ao, &os_error_code);
	if (!err)
		err = hpi_add_adapter(&ao);
	if (err) {
		phr->u.s.data = os_error_code;
		delete_adapter_obj(&ao);
		phr->error = err;
		return;
	}

	phr->u.s.aw_adapter_list[ao.index] = ao.adapter_type;
	phr->u.s.adapter_index = ao.index;
	phr->u.s.num_adapters++;
	phr->error = 0;
}

/** delete an adapter - required by WDM driver */
static void subsys_delete_adapter(struct hpi_message *phm,
	struct hpi_response *phr)
{
	struct hpi_adapter_obj *pao;
	struct hpi_hw_obj *phw;

	pao = hpi_find_adapter(phm->adapter_index);
	if (!pao) {
		phr->error = HPI_ERROR_INVALID_OBJ_INDEX;
		return;
	}
	phw = (struct hpi_hw_obj *)pao->priv;
	/* reset adapter h/w */
	/* Reset C6713 #1 */
	boot_loader_write_mem32(pao, 0, C6205_BAR0_TIMER1_CTL, 0);
	/* reset C6205 */
	iowrite32(C6205_HDCR_WARMRESET, phw->prHDCR);

	delete_adapter_obj(pao);
	phr->error = 0;
}

/** Create adapter object
  allocate buffers, bootload DSPs, initialise control cache
*/
static u16 create_adapter_obj(struct hpi_adapter_obj *pao,
	u32 *pos_error_code)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface;
	u32 phys_addr;
#ifndef HPI6205_NO_HSR_POLL
	u32 time_out = HPI6205_TIMEOUT;
	u32 temp1;
#endif
	int i;
	u16 err;

	/* init error reporting */
	pao->dsp_crashed = 0;

	for (i = 0; i < HPI_MAX_STREAMS; i++)
		phw->flag_outstream_just_reset[i] = 1;

	/* The C6205 memory area 1 is 8Mbyte window into DSP registers */
	phw->prHSR =
		pao->pci.ap_mem_base[1] +
		C6205_BAR1_HSR / sizeof(*pao->pci.ap_mem_base[1]);
	phw->prHDCR =
		pao->pci.ap_mem_base[1] +
		C6205_BAR1_HDCR / sizeof(*pao->pci.ap_mem_base[1]);
	phw->prDSPP =
		pao->pci.ap_mem_base[1] +
		C6205_BAR1_DSPP / sizeof(*pao->pci.ap_mem_base[1]);

	pao->has_control_cache = 0;

	if (hpios_locked_mem_alloc(&phw->h_locked_mem,
			sizeof(struct bus_master_interface),
			pao->pci.p_os_data))
		phw->p_interface_buffer = NULL;
	else if (hpios_locked_mem_get_virt_addr(&phw->h_locked_mem,
			(void *)&phw->p_interface_buffer))
		phw->p_interface_buffer = NULL;

	HPI_DEBUG_LOG(DEBUG, "interface buffer address %p\n",
		phw->p_interface_buffer);

	if (phw->p_interface_buffer) {
		memset((void *)phw->p_interface_buffer, 0,
			sizeof(struct bus_master_interface));
		phw->p_interface_buffer->dsp_ack = H620_HIF_UNKNOWN;
	}

	err = adapter_boot_load_dsp(pao, pos_error_code);
	if (err)
		/* no need to clean up as SubSysCreateAdapter */
		/* calls DeleteAdapter on error. */
		return err;

	HPI_DEBUG_LOG(INFO, "load DSP code OK\n");

	/* allow boot load even if mem alloc wont work */
	if (!phw->p_interface_buffer)
		return hpi6205_error(0, HPI_ERROR_MEMORY_ALLOC);

	interface = phw->p_interface_buffer;

#ifndef HPI6205_NO_HSR_POLL
	/* wait for first interrupt indicating the DSP init is done */
	time_out = HPI6205_TIMEOUT * 10;
	temp1 = 0;
	while (((temp1 & C6205_HSR_INTSRC) == 0) && --time_out)
		temp1 = ioread32(phw->prHSR);

	if (temp1 & C6205_HSR_INTSRC)
		HPI_DEBUG_LOG(INFO,
			"interrupt confirming DSP code running OK\n");
	else {
		HPI_DEBUG_LOG(ERROR,
			"timed out waiting for interrupt "
			"confirming DSP code running\n");
		return hpi6205_error(0, HPI6205_ERROR_6205_NO_IRQ);
	}

	/* reset the interrupt */
	iowrite32(C6205_HSR_INTSRC, phw->prHSR);
#endif

	/* make sure the DSP has started ok */
	if (!wait_dsp_ack(phw, H620_HIF_RESET, HPI6205_TIMEOUT * 10)) {
		HPI_DEBUG_LOG(ERROR, "timed out waiting reset state \n");
		return hpi6205_error(0, HPI6205_ERROR_6205_INIT_FAILED);
	}
	/* Note that *pao, *phw are zeroed after allocation,
	 * so pointers and flags are NULL by default.
	 * Allocate bus mastering control cache buffer and tell the DSP about it
	 */
	if (interface->control_cache.number_of_controls) {
		void *p_control_cache_virtual;

		err = hpios_locked_mem_alloc(&phw->h_control_cache,
			interface->control_cache.size_in_bytes,
			pao->pci.p_os_data);
		if (!err)
			err = hpios_locked_mem_get_virt_addr(&phw->
				h_control_cache, &p_control_cache_virtual);
		if (!err) {
			memset(p_control_cache_virtual, 0,
				interface->control_cache.size_in_bytes);

			phw->p_cache =
				hpi_alloc_control_cache(interface->
				control_cache.number_of_controls,
				interface->control_cache.size_in_bytes,
				(struct hpi_control_cache_info *)
				p_control_cache_virtual);
		}
		if (!err) {
			err = hpios_locked_mem_get_phys_addr(&phw->
				h_control_cache, &phys_addr);
			interface->control_cache.physical_address32 =
				phys_addr;
		}

		if (!err)
			pao->has_control_cache = 1;
		else {
			if (hpios_locked_mem_valid(&phw->h_control_cache))
				hpios_locked_mem_free(&phw->h_control_cache);
			pao->has_control_cache = 0;
		}
	}
	/* allocate bus mastering async buffer and tell the DSP about it */
	if (interface->async_buffer.b.size) {
		err = hpios_locked_mem_alloc(&phw->h_async_event_buffer,
			interface->async_buffer.b.size *
			sizeof(struct hpi_async_event), pao->pci.p_os_data);
		if (!err)
			err = hpios_locked_mem_get_virt_addr
				(&phw->h_async_event_buffer, (void *)
				&phw->p_async_event_buffer);
		if (!err)
			memset((void *)phw->p_async_event_buffer, 0,
				interface->async_buffer.b.size *
				sizeof(struct hpi_async_event));
		if (!err) {
			err = hpios_locked_mem_get_phys_addr
				(&phw->h_async_event_buffer, &phys_addr);
			interface->async_buffer.physical_address32 =
				phys_addr;
		}
		if (err) {
			if (hpios_locked_mem_valid(&phw->
					h_async_event_buffer)) {
				hpios_locked_mem_free
					(&phw->h_async_event_buffer);
				phw->p_async_event_buffer = NULL;
			}
		}
	}
	send_dsp_command(phw, H620_HIF_IDLE);

	{
		struct hpi_message hM;
		struct hpi_response hR;
		u32 max_streams;

		HPI_DEBUG_LOG(VERBOSE, "init ADAPTER_GET_INFO\n");
		memset(&hM, 0, sizeof(hM));
		hM.type = HPI_TYPE_MESSAGE;
		hM.size = sizeof(hM);
		hM.object = HPI_OBJ_ADAPTER;
		hM.function = HPI_ADAPTER_GET_INFO;
		hM.adapter_index = 0;
		memset(&hR, 0, sizeof(hR));
		hR.size = sizeof(hR);

		err = message_response_sequence(pao, &hM, &hR);
		if (err) {
			HPI_DEBUG_LOG(ERROR, "message transport error %d\n",
				err);
			return err;
		}
		if (hR.error)
			return hR.error;

		pao->adapter_type = hR.u.a.adapter_type;
		pao->index = hR.u.a.adapter_index;

		max_streams = hR.u.a.num_outstreams + hR.u.a.num_instreams;

		hpios_locked_mem_prepare((max_streams * 6) / 10, max_streams,
			65536, pao->pci.p_os_data);

		HPI_DEBUG_LOG(VERBOSE,
			"got adapter info type %x index %d serial %d\n",
			hR.u.a.adapter_type, hR.u.a.adapter_index,
			hR.u.a.serial_number);
	}

	pao->open = 0;	/* upon creation the adapter is closed */

	HPI_DEBUG_LOG(INFO, "bootload DSP OK\n");
	return 0;
}

/** Free memory areas allocated by adapter
 * this routine is called from SubSysDeleteAdapter,
  * and SubSysCreateAdapter if duplicate index
*/
static void delete_adapter_obj(struct hpi_adapter_obj *pao)
{
	struct hpi_hw_obj *phw;
	int i;

	phw = pao->priv;

	if (hpios_locked_mem_valid(&phw->h_async_event_buffer)) {
		hpios_locked_mem_free(&phw->h_async_event_buffer);
		phw->p_async_event_buffer = NULL;
	}

	if (hpios_locked_mem_valid(&phw->h_control_cache)) {
		hpios_locked_mem_free(&phw->h_control_cache);
		hpi_free_control_cache(phw->p_cache);
	}

	if (hpios_locked_mem_valid(&phw->h_locked_mem)) {
		hpios_locked_mem_free(&phw->h_locked_mem);
		phw->p_interface_buffer = NULL;
	}

	for (i = 0; i < HPI_MAX_STREAMS; i++)
		if (hpios_locked_mem_valid(&phw->instream_host_buffers[i])) {
			hpios_locked_mem_free(&phw->instream_host_buffers[i]);
			/*?phw->InStreamHostBuffers[i] = NULL; */
			phw->instream_host_buffer_size[i] = 0;
		}

	for (i = 0; i < HPI_MAX_STREAMS; i++)
		if (hpios_locked_mem_valid(&phw->outstream_host_buffers[i])) {
			hpios_locked_mem_free(&phw->outstream_host_buffers
				[i]);
			phw->outstream_host_buffer_size[i] = 0;
		}

	hpios_locked_mem_unprepare(pao->pci.p_os_data);

	hpi_delete_adapter(pao);
	kfree(phw);
}

/*****************************************************************************/
/* OutStream Host buffer functions */

/** Allocate or attach buffer for busmastering
*/
static void outstream_host_buffer_allocate(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	u16 err = 0;
	u32 command = phm->u.d.u.buffer.command;
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;

	hpi_init_response(phr, phm->object, phm->function, 0);

	if (command == HPI_BUFFER_CMD_EXTERNAL
		|| command == HPI_BUFFER_CMD_INTERNAL_ALLOC) {
		/* ALLOC phase, allocate a buffer with power of 2 size,
		   get its bus address for PCI bus mastering
		 */
		phm->u.d.u.buffer.buffer_size =
			roundup_pow_of_two(phm->u.d.u.buffer.buffer_size);
		/* return old size and allocated size,
		   so caller can detect change */
		phr->u.d.u.stream_info.data_available =
			phw->outstream_host_buffer_size[phm->obj_index];
		phr->u.d.u.stream_info.buffer_size =
			phm->u.d.u.buffer.buffer_size;

		if (phw->outstream_host_buffer_size[phm->obj_index] ==
			phm->u.d.u.buffer.buffer_size) {
			/* Same size, no action required */
			return;
		}

		if (hpios_locked_mem_valid(&phw->outstream_host_buffers[phm->
					obj_index]))
			hpios_locked_mem_free(&phw->outstream_host_buffers
				[phm->obj_index]);

		err = hpios_locked_mem_alloc(&phw->outstream_host_buffers
			[phm->obj_index], phm->u.d.u.buffer.buffer_size,
			pao->pci.p_os_data);

		if (err) {
			phr->error = HPI_ERROR_INVALID_DATASIZE;
			phw->outstream_host_buffer_size[phm->obj_index] = 0;
			return;
		}

		err = hpios_locked_mem_get_phys_addr
			(&phw->outstream_host_buffers[phm->obj_index],
			&phm->u.d.u.buffer.pci_address);
		/* get the phys addr into msg for single call alloc caller
		 * needs to do this for split alloc (or use the same message)
		 * return the phy address for split alloc in the respose too
		 */
		phr->u.d.u.stream_info.auxiliary_data_available =
			phm->u.d.u.buffer.pci_address;

		if (err) {
			hpios_locked_mem_free(&phw->outstream_host_buffers
				[phm->obj_index]);
			phw->outstream_host_buffer_size[phm->obj_index] = 0;
			phr->error = HPI_ERROR_MEMORY_ALLOC;
			return;
		}
	}

	if (command == HPI_BUFFER_CMD_EXTERNAL
		|| command == HPI_BUFFER_CMD_INTERNAL_GRANTADAPTER) {
		/* GRANT phase.  Set up the BBM status, tell the DSP about
		   the buffer so it can start using BBM.
		 */
		struct hpi_hostbuffer_status *status;

		if (phm->u.d.u.buffer.buffer_size & (phm->u.d.u.buffer.
				buffer_size - 1)) {
			HPI_DEBUG_LOG(ERROR,
				"buffer size must be 2^N not %d\n",
				phm->u.d.u.buffer.buffer_size);
			phr->error = HPI_ERROR_INVALID_DATASIZE;
			return;
		}
		phw->outstream_host_buffer_size[phm->obj_index] =
			phm->u.d.u.buffer.buffer_size;
		status = &interface->outstream_host_buffer_status[phm->
			obj_index];
		status->samples_processed = 0;
		status->stream_state = HPI_STATE_STOPPED;
		status->dSP_index = 0;
		status->host_index = status->dSP_index;
		status->size_in_bytes = phm->u.d.u.buffer.buffer_size;

		hw_message(pao, phm, phr);

		if (phr->error
			&& hpios_locked_mem_valid(&phw->
				outstream_host_buffers[phm->obj_index])) {
			hpios_locked_mem_free(&phw->outstream_host_buffers
				[phm->obj_index]);
			phw->outstream_host_buffer_size[phm->obj_index] = 0;
		}
	}
}

static void outstream_host_buffer_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	struct hpi_hostbuffer_status *status;
	u8 *p_bbm_data;

	if (hpios_locked_mem_valid(&phw->outstream_host_buffers[phm->
				obj_index])) {
		if (hpios_locked_mem_get_virt_addr(&phw->
				outstream_host_buffers[phm->obj_index],
				(void *)&p_bbm_data)) {
			phr->error = HPI_ERROR_INVALID_OPERATION;
			return;
		}
		status = &interface->outstream_host_buffer_status[phm->
			obj_index];
		hpi_init_response(phr, HPI_OBJ_OSTREAM,
			HPI_OSTREAM_HOSTBUFFER_GET_INFO, 0);
		phr->u.d.u.hostbuffer_info.p_buffer = p_bbm_data;
		phr->u.d.u.hostbuffer_info.p_status = status;
	} else {
		hpi_init_response(phr, HPI_OBJ_OSTREAM,
			HPI_OSTREAM_HOSTBUFFER_GET_INFO,
			HPI_ERROR_INVALID_OPERATION);
	}
}

static void outstream_host_buffer_free(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	u32 command = phm->u.d.u.buffer.command;

	if (phw->outstream_host_buffer_size[phm->obj_index]) {
		if (command == HPI_BUFFER_CMD_EXTERNAL
			|| command == HPI_BUFFER_CMD_INTERNAL_REVOKEADAPTER) {
			phw->outstream_host_buffer_size[phm->obj_index] = 0;
			hw_message(pao, phm, phr);
			/* Tell adapter to stop using the host buffer. */
		}
		if (command == HPI_BUFFER_CMD_EXTERNAL
			|| command == HPI_BUFFER_CMD_INTERNAL_FREE)
			hpios_locked_mem_free(&phw->outstream_host_buffers
				[phm->obj_index]);
	}
	/* Should HPI_ERROR_INVALID_OPERATION be returned
	   if no host buffer is allocated? */
	else
		hpi_init_response(phr, HPI_OBJ_OSTREAM,
			HPI_OSTREAM_HOSTBUFFER_FREE, 0);

}

static u32 outstream_get_space_available(struct hpi_hostbuffer_status
	*status)
{
	return status->size_in_bytes - (status->host_index -
		status->dSP_index);
}

static void outstream_write(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	struct hpi_hostbuffer_status *status;
	u32 space_available;

	if (!phw->outstream_host_buffer_size[phm->obj_index]) {
		/* there  is no BBM buffer, write via message */
		hw_message(pao, phm, phr);
		return;
	}

	hpi_init_response(phr, phm->object, phm->function, 0);
	status = &interface->outstream_host_buffer_status[phm->obj_index];

	if (phw->flag_outstream_just_reset[phm->obj_index]) {
		/* First OutStremWrite() call following reset will write data to the
		   adapter's buffers, reducing delay before stream can start. The DSP
		   takes care of setting the stream data format using format information
		   embedded in phm.
		 */
		int partial_write = 0;
		unsigned int original_size = 0;

		phw->flag_outstream_just_reset[phm->obj_index] = 0;

		/* Send the first buffer to the DSP the old way. */
		/* Limit size of first transfer - */
		/* expect that this will not usually be triggered. */
		if (phm->u.d.u.data.data_size > HPI6205_SIZEOF_DATA) {
			partial_write = 1;
			original_size = phm->u.d.u.data.data_size;
			phm->u.d.u.data.data_size = HPI6205_SIZEOF_DATA;
		}
		/* write it */
		phm->function = HPI_OSTREAM_WRITE;
		hw_message(pao, phm, phr);
		/* update status information that the DSP would typically
		 * update (and will update next time the DSP
		 * buffer update task reads data from the host BBM buffer)
		 */
		status->auxiliary_data_available = phm->u.d.u.data.data_size;
		status->host_index += phm->u.d.u.data.data_size;
		status->dSP_index += phm->u.d.u.data.data_size;

		/* if we did a full write, we can return from here. */
		if (!partial_write)
			return;

		/* tweak buffer parameters and let the rest of the */
		/* buffer land in internal BBM buffer */
		phm->u.d.u.data.data_size =
			original_size - HPI6205_SIZEOF_DATA;
		phm->u.d.u.data.pb_data += HPI6205_SIZEOF_DATA;
	}

	space_available = outstream_get_space_available(status);
	if (space_available < phm->u.d.u.data.data_size) {
		phr->error = HPI_ERROR_INVALID_DATASIZE;
		return;
	}

	/* HostBuffers is used to indicate host buffer is internally allocated.
	   otherwise, assumed external, data written externally */
	if (phm->u.d.u.data.pb_data
		&& hpios_locked_mem_valid(&phw->outstream_host_buffers[phm->
				obj_index])) {
		u8 *p_bbm_data;
		u32 l_first_write;
		u8 *p_app_data = (u8 *)phm->u.d.u.data.pb_data;

		if (hpios_locked_mem_get_virt_addr(&phw->
				outstream_host_buffers[phm->obj_index],
				(void *)&p_bbm_data)) {
			phr->error = HPI_ERROR_INVALID_OPERATION;
			return;
		}

		/* either all data,
		   or enough to fit from current to end of BBM buffer */
		l_first_write =
			min(phm->u.d.u.data.data_size,
			status->size_in_bytes -
			(status->host_index & (status->size_in_bytes - 1)));

		memcpy(p_bbm_data +
			(status->host_index & (status->size_in_bytes - 1)),
			p_app_data, l_first_write);
		/* remaining data if any */
		memcpy(p_bbm_data, p_app_data + l_first_write,
			phm->u.d.u.data.data_size - l_first_write);
	}
	status->host_index += phm->u.d.u.data.data_size;
}

static void outstream_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	struct hpi_hostbuffer_status *status;

	if (!phw->outstream_host_buffer_size[phm->obj_index]) {
		hw_message(pao, phm, phr);
		return;
	}

	hpi_init_response(phr, phm->object, phm->function, 0);

	status = &interface->outstream_host_buffer_status[phm->obj_index];

	phr->u.d.u.stream_info.state = (u16)status->stream_state;
	phr->u.d.u.stream_info.samples_transferred =
		status->samples_processed;
	phr->u.d.u.stream_info.buffer_size = status->size_in_bytes;
	phr->u.d.u.stream_info.data_available =
		status->size_in_bytes - outstream_get_space_available(status);
	phr->u.d.u.stream_info.auxiliary_data_available =
		status->auxiliary_data_available;
}

static void outstream_start(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	hw_message(pao, phm, phr);
}

static void outstream_reset(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	phw->flag_outstream_just_reset[phm->obj_index] = 1;
	hw_message(pao, phm, phr);
}

static void outstream_open(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	outstream_reset(pao, phm, phr);
}

/*****************************************************************************/
/* InStream Host buffer functions */

static void instream_host_buffer_allocate(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	u16 err = 0;
	u32 command = phm->u.d.u.buffer.command;
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;

	hpi_init_response(phr, phm->object, phm->function, 0);

	if (command == HPI_BUFFER_CMD_EXTERNAL
		|| command == HPI_BUFFER_CMD_INTERNAL_ALLOC) {

		phm->u.d.u.buffer.buffer_size =
			roundup_pow_of_two(phm->u.d.u.buffer.buffer_size);
		phr->u.d.u.stream_info.data_available =
			phw->instream_host_buffer_size[phm->obj_index];
		phr->u.d.u.stream_info.buffer_size =
			phm->u.d.u.buffer.buffer_size;

		if (phw->instream_host_buffer_size[phm->obj_index] ==
			phm->u.d.u.buffer.buffer_size) {
			/* Same size, no action required */
			return;
		}

		if (hpios_locked_mem_valid(&phw->instream_host_buffers[phm->
					obj_index]))
			hpios_locked_mem_free(&phw->instream_host_buffers
				[phm->obj_index]);

		err = hpios_locked_mem_alloc(&phw->instream_host_buffers[phm->
				obj_index], phm->u.d.u.buffer.buffer_size,
			pao->pci.p_os_data);

		if (err) {
			phr->error = HPI_ERROR_INVALID_DATASIZE;
			phw->instream_host_buffer_size[phm->obj_index] = 0;
			return;
		}

		err = hpios_locked_mem_get_phys_addr
			(&phw->instream_host_buffers[phm->obj_index],
			&phm->u.d.u.buffer.pci_address);
		/* get the phys addr into msg for single call alloc. Caller
		   needs to do this for split alloc so return the phy address */
		phr->u.d.u.stream_info.auxiliary_data_available =
			phm->u.d.u.buffer.pci_address;
		if (err) {
			hpios_locked_mem_free(&phw->instream_host_buffers
				[phm->obj_index]);
			phw->instream_host_buffer_size[phm->obj_index] = 0;
			phr->error = HPI_ERROR_MEMORY_ALLOC;
			return;
		}
	}

	if (command == HPI_BUFFER_CMD_EXTERNAL
		|| command == HPI_BUFFER_CMD_INTERNAL_GRANTADAPTER) {
		struct hpi_hostbuffer_status *status;

		if (phm->u.d.u.buffer.buffer_size & (phm->u.d.u.buffer.
				buffer_size - 1)) {
			HPI_DEBUG_LOG(ERROR,
				"buffer size must be 2^N not %d\n",
				phm->u.d.u.buffer.buffer_size);
			phr->error = HPI_ERROR_INVALID_DATASIZE;
			return;
		}

		phw->instream_host_buffer_size[phm->obj_index] =
			phm->u.d.u.buffer.buffer_size;
		status = &interface->instream_host_buffer_status[phm->
			obj_index];
		status->samples_processed = 0;
		status->stream_state = HPI_STATE_STOPPED;
		status->dSP_index = 0;
		status->host_index = status->dSP_index;
		status->size_in_bytes = phm->u.d.u.buffer.buffer_size;

		hw_message(pao, phm, phr);
		if (phr->error
			&& hpios_locked_mem_valid(&phw->
				instream_host_buffers[phm->obj_index])) {
			hpios_locked_mem_free(&phw->instream_host_buffers
				[phm->obj_index]);
			phw->instream_host_buffer_size[phm->obj_index] = 0;
		}
	}
}

static void instream_host_buffer_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	struct hpi_hostbuffer_status *status;
	u8 *p_bbm_data;

	if (hpios_locked_mem_valid(&phw->instream_host_buffers[phm->
				obj_index])) {
		if (hpios_locked_mem_get_virt_addr(&phw->
				instream_host_buffers[phm->obj_index],
				(void *)&p_bbm_data)) {
			phr->error = HPI_ERROR_INVALID_OPERATION;
			return;
		}
		status = &interface->instream_host_buffer_status[phm->
			obj_index];
		hpi_init_response(phr, HPI_OBJ_ISTREAM,
			HPI_ISTREAM_HOSTBUFFER_GET_INFO, 0);
		phr->u.d.u.hostbuffer_info.p_buffer = p_bbm_data;
		phr->u.d.u.hostbuffer_info.p_status = status;
	} else {
		hpi_init_response(phr, HPI_OBJ_ISTREAM,
			HPI_ISTREAM_HOSTBUFFER_GET_INFO,
			HPI_ERROR_INVALID_OPERATION);
	}
}

static void instream_host_buffer_free(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	u32 command = phm->u.d.u.buffer.command;

	if (phw->instream_host_buffer_size[phm->obj_index]) {
		if (command == HPI_BUFFER_CMD_EXTERNAL
			|| command == HPI_BUFFER_CMD_INTERNAL_REVOKEADAPTER) {
			phw->instream_host_buffer_size[phm->obj_index] = 0;
			hw_message(pao, phm, phr);
		}

		if (command == HPI_BUFFER_CMD_EXTERNAL
			|| command == HPI_BUFFER_CMD_INTERNAL_FREE)
			hpios_locked_mem_free(&phw->instream_host_buffers
				[phm->obj_index]);

	} else {
		/* Should HPI_ERROR_INVALID_OPERATION be returned
		   if no host buffer is allocated? */
		hpi_init_response(phr, HPI_OBJ_ISTREAM,
			HPI_ISTREAM_HOSTBUFFER_FREE, 0);

	}

}

static void instream_start(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	hw_message(pao, phm, phr);
}

static u32 instream_get_bytes_available(struct hpi_hostbuffer_status *status)
{
	return status->dSP_index - status->host_index;
}

static void instream_read(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	struct hpi_hostbuffer_status *status;
	u32 data_available;
	u8 *p_bbm_data;
	u32 l_first_read;
	u8 *p_app_data = (u8 *)phm->u.d.u.data.pb_data;

	if (!phw->instream_host_buffer_size[phm->obj_index]) {
		hw_message(pao, phm, phr);
		return;
	}
	hpi_init_response(phr, phm->object, phm->function, 0);

	status = &interface->instream_host_buffer_status[phm->obj_index];
	data_available = instream_get_bytes_available(status);
	if (data_available < phm->u.d.u.data.data_size) {
		phr->error = HPI_ERROR_INVALID_DATASIZE;
		return;
	}

	if (hpios_locked_mem_valid(&phw->instream_host_buffers[phm->
				obj_index])) {
		if (hpios_locked_mem_get_virt_addr(&phw->
				instream_host_buffers[phm->obj_index],
				(void *)&p_bbm_data)) {
			phr->error = HPI_ERROR_INVALID_OPERATION;
			return;
		}

		/* either all data,
		   or enough to fit from current to end of BBM buffer */
		l_first_read =
			min(phm->u.d.u.data.data_size,
			status->size_in_bytes -
			(status->host_index & (status->size_in_bytes - 1)));

		memcpy(p_app_data,
			p_bbm_data +
			(status->host_index & (status->size_in_bytes - 1)),
			l_first_read);
		/* remaining data if any */
		memcpy(p_app_data + l_first_read, p_bbm_data,
			phm->u.d.u.data.data_size - l_first_read);
	}
	status->host_index += phm->u.d.u.data.data_size;
}

static void instream_get_info(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	struct hpi_hostbuffer_status *status;
	if (!phw->instream_host_buffer_size[phm->obj_index]) {
		hw_message(pao, phm, phr);
		return;
	}

	status = &interface->instream_host_buffer_status[phm->obj_index];

	hpi_init_response(phr, phm->object, phm->function, 0);

	phr->u.d.u.stream_info.state = (u16)status->stream_state;
	phr->u.d.u.stream_info.samples_transferred =
		status->samples_processed;
	phr->u.d.u.stream_info.buffer_size = status->size_in_bytes;
	phr->u.d.u.stream_info.data_available =
		instream_get_bytes_available(status);
	phr->u.d.u.stream_info.auxiliary_data_available =
		status->auxiliary_data_available;
}

/*****************************************************************************/
/* LOW-LEVEL */
#define HPI6205_MAX_FILES_TO_LOAD 2

static u16 adapter_boot_load_dsp(struct hpi_adapter_obj *pao,
	u32 *pos_error_code)
{
	struct hpi_hw_obj *phw = pao->priv;
	struct dsp_code dsp_code;
	u16 boot_code_id[HPI6205_MAX_FILES_TO_LOAD];
	u16 firmware_id = pao->pci.subsys_device_id;
	u32 temp;
	int dsp = 0, i = 0;
	u16 err = 0;

	boot_code_id[0] = HPI_ADAPTER_ASI(0x6205);

	/* special cases where firmware_id != subsys ID */
	switch (firmware_id) {
	case HPI_ADAPTER_FAMILY_ASI(0x5000):
		boot_code_id[0] = firmware_id;
		firmware_id = 0;
		break;
	case HPI_ADAPTER_FAMILY_ASI(0x5300):
	case HPI_ADAPTER_FAMILY_ASI(0x5400):
	case HPI_ADAPTER_FAMILY_ASI(0x6300):
		firmware_id = HPI_ADAPTER_FAMILY_ASI(0x6400);
		break;
	case HPI_ADAPTER_FAMILY_ASI(0x5600):
	case HPI_ADAPTER_FAMILY_ASI(0x6500):
		firmware_id = HPI_ADAPTER_FAMILY_ASI(0x6600);
		break;
	case HPI_ADAPTER_FAMILY_ASI(0x8800):
		firmware_id = HPI_ADAPTER_FAMILY_ASI(0x8900);
		break;
	}
	boot_code_id[1] = firmware_id;

	/* reset DSP by writing a 1 to the WARMRESET bit */
	temp = C6205_HDCR_WARMRESET;
	iowrite32(temp, phw->prHDCR);
	hpios_delay_micro_seconds(1000);

	/* check that PCI i/f was configured by EEPROM */
	temp = ioread32(phw->prHSR);
	if ((temp & (C6205_HSR_CFGERR | C6205_HSR_EEREAD)) !=
		C6205_HSR_EEREAD)
		return hpi6205_error(0, HPI6205_ERROR_6205_EEPROM);
	temp |= 0x04;
	/* disable PINTA interrupt */
	iowrite32(temp, phw->prHSR);

	/* check control register reports PCI boot mode */
	temp = ioread32(phw->prHDCR);
	if (!(temp & C6205_HDCR_PCIBOOT))
		return hpi6205_error(0, HPI6205_ERROR_6205_REG);

	/* try writing a couple of numbers to the DSP page register */
	/* and reading them back. */
	temp = 1;
	iowrite32(temp, phw->prDSPP);
	if ((temp | C6205_DSPP_MAP1) != ioread32(phw->prDSPP))
		return hpi6205_error(0, HPI6205_ERROR_6205_DSPPAGE);
	temp = 2;
	iowrite32(temp, phw->prDSPP);
	if ((temp | C6205_DSPP_MAP1) != ioread32(phw->prDSPP))
		return hpi6205_error(0, HPI6205_ERROR_6205_DSPPAGE);
	temp = 3;
	iowrite32(temp, phw->prDSPP);
	if ((temp | C6205_DSPP_MAP1) != ioread32(phw->prDSPP))
		return hpi6205_error(0, HPI6205_ERROR_6205_DSPPAGE);
	/* reset DSP page to the correct number */
	temp = 0;
	iowrite32(temp, phw->prDSPP);
	if ((temp | C6205_DSPP_MAP1) != ioread32(phw->prDSPP))
		return hpi6205_error(0, HPI6205_ERROR_6205_DSPPAGE);
	phw->dsp_page = 0;

	/* release 6713 from reset before 6205 is bootloaded.
	   This ensures that the EMIF is inactive,
	   and the 6713 HPI gets the correct bootmode etc
	 */
	if (boot_code_id[1] != 0) {
		/* DSP 1 is a C6713 */
		/* CLKX0 <- '1' release the C6205 bootmode pulldowns */
		boot_loader_write_mem32(pao, 0, (0x018C0024L), 0x00002202);
		hpios_delay_micro_seconds(100);
		/* Reset the 6713 #1 - revB */
		boot_loader_write_mem32(pao, 0, C6205_BAR0_TIMER1_CTL, 0);

		/* dummy read every 4 words for 6205 advisory 1.4.4 */
		boot_loader_read_mem32(pao, 0, 0);

		hpios_delay_micro_seconds(100);
		/* Release C6713 from reset - revB */
		boot_loader_write_mem32(pao, 0, C6205_BAR0_TIMER1_CTL, 4);
		hpios_delay_micro_seconds(100);
	}

	for (dsp = 0; dsp < HPI6205_MAX_FILES_TO_LOAD; dsp++) {
		/* is there a DSP to load? */
		if (boot_code_id[dsp] == 0)
			continue;

		err = boot_loader_config_emif(pao, dsp);
		if (err)
			return err;

		err = boot_loader_test_internal_memory(pao, dsp);
		if (err)
			return err;

		err = boot_loader_test_external_memory(pao, dsp);
		if (err)
			return err;

		err = boot_loader_test_pld(pao, dsp);
		if (err)
			return err;

		/* write the DSP code down into the DSPs memory */
		dsp_code.ps_dev = pao->pci.p_os_data;
		err = hpi_dsp_code_open(boot_code_id[dsp], &dsp_code,
			pos_error_code);
		if (err)
			return err;

		while (1) {
			u32 length;
			u32 address;
			u32 type;
			u32 *pcode;

			err = hpi_dsp_code_read_word(&dsp_code, &length);
			if (err)
				break;
			if (length == 0xFFFFFFFF)
				break;	/* end of code */

			err = hpi_dsp_code_read_word(&dsp_code, &address);
			if (err)
				break;
			err = hpi_dsp_code_read_word(&dsp_code, &type);
			if (err)
				break;
			err = hpi_dsp_code_read_block(length, &dsp_code,
				&pcode);
			if (err)
				break;
			for (i = 0; i < (int)length; i++) {
				err = boot_loader_write_mem32(pao, dsp,
					address, *pcode);
				if (err)
					break;
				/* dummy read every 4 words */
				/* for 6205 advisory 1.4.4 */
				if (i % 4 == 0)
					boot_loader_read_mem32(pao, dsp,
						address);
				pcode++;
				address += 4;
			}

		}
		if (err) {
			hpi_dsp_code_close(&dsp_code);
			return err;
		}

		/* verify code */
		hpi_dsp_code_rewind(&dsp_code);
		while (1) {
			u32 length = 0;
			u32 address = 0;
			u32 type = 0;
			u32 *pcode = NULL;
			u32 data = 0;

			hpi_dsp_code_read_word(&dsp_code, &length);
			if (length == 0xFFFFFFFF)
				break;	/* end of code */

			hpi_dsp_code_read_word(&dsp_code, &address);
			hpi_dsp_code_read_word(&dsp_code, &type);
			hpi_dsp_code_read_block(length, &dsp_code, &pcode);

			for (i = 0; i < (int)length; i++) {
				data = boot_loader_read_mem32(pao, dsp,
					address);
				if (data != *pcode) {
					err = 0;
					break;
				}
				pcode++;
				address += 4;
			}
			if (err)
				break;
		}
		hpi_dsp_code_close(&dsp_code);
		if (err)
			return err;
	}

	/* After bootloading all DSPs, start DSP0 running
	 * The DSP0 code will handle starting and synchronizing with its slaves
	 */
	if (phw->p_interface_buffer) {
		/* we need to tell the card the physical PCI address */
		u32 physicalPC_iaddress;
		struct bus_master_interface *interface =
			phw->p_interface_buffer;
		u32 host_mailbox_address_on_dsp;
		u32 physicalPC_iaddress_verify = 0;
		int time_out = 10;
		/* set ack so we know when DSP is ready to go */
		/* (dwDspAck will be changed to HIF_RESET) */
		interface->dsp_ack = H620_HIF_UNKNOWN;
		wmb();	/* ensure ack is written before dsp writes back */

		err = hpios_locked_mem_get_phys_addr(&phw->h_locked_mem,
			&physicalPC_iaddress);

		/* locate the host mailbox on the DSP. */
		host_mailbox_address_on_dsp = 0x80000000;
		while ((physicalPC_iaddress != physicalPC_iaddress_verify)
			&& time_out--) {
			err = boot_loader_write_mem32(pao, 0,
				host_mailbox_address_on_dsp,
				physicalPC_iaddress);
			physicalPC_iaddress_verify =
				boot_loader_read_mem32(pao, 0,
				host_mailbox_address_on_dsp);
		}
	}
	HPI_DEBUG_LOG(DEBUG, "starting DS_ps running\n");
	/* enable interrupts */
	temp = ioread32(phw->prHSR);
	temp &= ~(u32)C6205_HSR_INTAM;
	iowrite32(temp, phw->prHSR);

	/* start code running... */
	temp = ioread32(phw->prHDCR);
	temp |= (u32)C6205_HDCR_DSPINT;
	iowrite32(temp, phw->prHDCR);

	/* give the DSP 10ms to start up */
	hpios_delay_micro_seconds(10000);
	return err;

}

/*****************************************************************************/
/* Bootloader utility functions */

static u32 boot_loader_read_mem32(struct hpi_adapter_obj *pao, int dsp_index,
	u32 address)
{
	struct hpi_hw_obj *phw = pao->priv;
	u32 data = 0;
	__iomem u32 *p_data;

	if (dsp_index == 0) {
		/* DSP 0 is always C6205 */
		if ((address >= 0x01800000) & (address < 0x02000000)) {
			/* BAR1 register access */
			p_data = pao->pci.ap_mem_base[1] +
				(address & 0x007fffff) /
				sizeof(*pao->pci.ap_mem_base[1]);
			/* HPI_DEBUG_LOG(WARNING,
			   "BAR1 access %08x\n", dwAddress); */
		} else {
			u32 dw4M_page = address >> 22L;
			if (dw4M_page != phw->dsp_page) {
				phw->dsp_page = dw4M_page;
				/* *INDENT OFF* */
				iowrite32(phw->dsp_page, phw->prDSPP);
				/* *INDENT-ON* */
			}
			address &= 0x3fffff;	/* address within 4M page */
			/* BAR0 memory access */
			p_data = pao->pci.ap_mem_base[0] +
				address / sizeof(u32);
		}
		data = ioread32(p_data);
	} else if (dsp_index == 1) {
		/* DSP 1 is a C6713 */
		u32 lsb;
		boot_loader_write_mem32(pao, 0, HPIAL_ADDR, address);
		boot_loader_write_mem32(pao, 0, HPIAH_ADDR, address >> 16);
		lsb = boot_loader_read_mem32(pao, 0, HPIDL_ADDR);
		data = boot_loader_read_mem32(pao, 0, HPIDH_ADDR);
		data = (data << 16) | (lsb & 0xFFFF);
	}
	return data;
}

static u16 boot_loader_write_mem32(struct hpi_adapter_obj *pao, int dsp_index,
	u32 address, u32 data)
{
	struct hpi_hw_obj *phw = pao->priv;
	u16 err = 0;
	__iomem u32 *p_data;
	/*      u32 dwVerifyData=0; */

	if (dsp_index == 0) {
		/* DSP 0 is always C6205 */
		if ((address >= 0x01800000) & (address < 0x02000000)) {
			/* BAR1 - DSP  register access using */
			/* Non-prefetchable PCI access */
			p_data = pao->pci.ap_mem_base[1] +
				(address & 0x007fffff) /
				sizeof(*pao->pci.ap_mem_base[1]);
		} else {
			/* BAR0 access - all of DSP memory using */
			/* pre-fetchable PCI access */
			u32 dw4M_page = address >> 22L;
			if (dw4M_page != phw->dsp_page) {
				phw->dsp_page = dw4M_page;
				/* *INDENT-OFF* */
				iowrite32(phw->dsp_page, phw->prDSPP);
				/* *INDENT-ON* */
			}
			address &= 0x3fffff;	/* address within 4M page */
			p_data = pao->pci.ap_mem_base[0] +
				address / sizeof(u32);
		}
		iowrite32(data, p_data);
	} else if (dsp_index == 1) {
		/* DSP 1 is a C6713 */
		boot_loader_write_mem32(pao, 0, HPIAL_ADDR, address);
		boot_loader_write_mem32(pao, 0, HPIAH_ADDR, address >> 16);

		/* dummy read every 4 words for 6205 advisory 1.4.4 */
		boot_loader_read_mem32(pao, 0, 0);

		boot_loader_write_mem32(pao, 0, HPIDL_ADDR, data);
		boot_loader_write_mem32(pao, 0, HPIDH_ADDR, data >> 16);

		/* dummy read every 4 words for 6205 advisory 1.4.4 */
		boot_loader_read_mem32(pao, 0, 0);
	} else
		err = hpi6205_error(dsp_index, HPI6205_ERROR_BAD_DSPINDEX);
	return err;
}

static u16 boot_loader_config_emif(struct hpi_adapter_obj *pao, int dsp_index)
{
	u16 err = 0;

	if (dsp_index == 0) {
		u32 setting;

		/* DSP 0 is always C6205 */

		/* Set the EMIF */
		/* memory map of C6205 */
		/* 00000000-0000FFFF    16Kx32 internal program */
		/* 00400000-00BFFFFF    CE0     2Mx32 SDRAM running @ 100MHz */

		/* EMIF config */
		/*------------ */
		/* Global EMIF control */
		boot_loader_write_mem32(pao, dsp_index, 0x01800000, 0x3779);
#define WS_OFS 28
#define WST_OFS 22
#define WH_OFS 20
#define RS_OFS 16
#define RST_OFS 8
#define MTYPE_OFS 4
#define RH_OFS 0

		/* EMIF CE0 setup - 2Mx32 Sync DRAM on ASI5000 cards only */
		setting = 0x00000030;
		boot_loader_write_mem32(pao, dsp_index, 0x01800008, setting);
		if (setting != boot_loader_read_mem32(pao, dsp_index,
				0x01800008))
			return hpi6205_error(dsp_index,
				HPI6205_ERROR_DSP_EMIF);

		/* EMIF CE1 setup - 32 bit async. This is 6713 #1 HPI, */
		/* which occupies D15..0. 6713 starts at 27MHz, so need */
		/* plenty of wait states. See dsn8701.rtf, and 6713 errata. */
		/* WST should be 71, but 63  is max possible */
		setting =
			(1L << WS_OFS) | (63L << WST_OFS) | (1L << WH_OFS) |
			(1L << RS_OFS) | (63L << RST_OFS) | (1L << RH_OFS) |
			(2L << MTYPE_OFS);
		boot_loader_write_mem32(pao, dsp_index, 0x01800004, setting);
		if (setting != boot_loader_read_mem32(pao, dsp_index,
				0x01800004))
			return hpi6205_error(dsp_index,
				HPI6205_ERROR_DSP_EMIF);

		/* EMIF CE2 setup - 32 bit async. This is 6713 #2 HPI, */
		/* which occupies D15..0. 6713 starts at 27MHz, so need */
		/* plenty of wait states */
		setting =
			(1L << WS_OFS) | (28L << WST_OFS) | (1L << WH_OFS) |
			(1L << RS_OFS) | (63L << RST_OFS) | (1L << RH_OFS) |
			(2L << MTYPE_OFS);
		boot_loader_write_mem32(pao, dsp_index, 0x01800010, setting);
		if (setting != boot_loader_read_mem32(pao, dsp_index,
				0x01800010))
			return hpi6205_error(dsp_index,
				HPI6205_ERROR_DSP_EMIF);

		/* EMIF CE3 setup - 32 bit async. */
		/* This is the PLD on the ASI5000 cards only */
		setting =
			(1L << WS_OFS) | (10L << WST_OFS) | (1L << WH_OFS) |
			(1L << RS_OFS) | (10L << RST_OFS) | (1L << RH_OFS) |
			(2L << MTYPE_OFS);
		boot_loader_write_mem32(pao, dsp_index, 0x01800014, setting);
		if (setting != boot_loader_read_mem32(pao, dsp_index,
				0x01800014))
			return hpi6205_error(dsp_index,
				HPI6205_ERROR_DSP_EMIF);

		/* set EMIF SDRAM control for 2Mx32 SDRAM (512x32x4 bank) */
		/*  need to use this else DSP code crashes? */
		boot_loader_write_mem32(pao, dsp_index, 0x01800018,
			0x07117000);

		/* EMIF SDRAM Refresh Timing */
		/* EMIF SDRAM timing  (orig = 0x410, emulator = 0x61a) */
		boot_loader_write_mem32(pao, dsp_index, 0x0180001C,
			0x00000410);

	} else if (dsp_index == 1) {
		/* test access to the C6713s HPI registers */
		u32 write_data = 0, read_data = 0, i = 0;

		/* Set up HPIC for little endian, by setiing HPIC:HWOB=1 */
		write_data = 1;
		boot_loader_write_mem32(pao, 0, HPICL_ADDR, write_data);
		boot_loader_write_mem32(pao, 0, HPICH_ADDR, write_data);
		/* C67 HPI is on lower 16bits of 32bit EMIF */
		read_data =
			0xFFF7 & boot_loader_read_mem32(pao, 0, HPICL_ADDR);
		if (write_data != read_data) {
			err = hpi6205_error(dsp_index,
				HPI6205_ERROR_C6713_HPIC);
			HPI_DEBUG_LOG(ERROR, "HPICL %x %x\n", write_data,
				read_data);

			return err;
		}
		/* HPIA - walking ones test */
		write_data = 1;
		for (i = 0; i < 32; i++) {
			boot_loader_write_mem32(pao, 0, HPIAL_ADDR,
				write_data);
			boot_loader_write_mem32(pao, 0, HPIAH_ADDR,
				(write_data >> 16));
			read_data =
				0xFFFF & boot_loader_read_mem32(pao, 0,
				HPIAL_ADDR);
			read_data =
				read_data | ((0xFFFF &
					boot_loader_read_mem32(pao, 0,
						HPIAH_ADDR))
				<< 16);
			if (read_data != write_data) {
				err = hpi6205_error(dsp_index,
					HPI6205_ERROR_C6713_HPIA);
				HPI_DEBUG_LOG(ERROR, "HPIA %x %x\n",
					write_data, read_data);
				return err;
			}
			write_data = write_data << 1;
		}

		/* setup C67x PLL
		 *  ** C6713 datasheet says we cannot program PLL from HPI,
		 * and indeed if we try to set the PLL multiply from the HPI,
		 * the PLL does not seem to lock, so we enable the PLL and
		 * use the default multiply of x 7, which for a 27MHz clock
		 * gives a DSP speed of 189MHz
		 */
		/* bypass PLL */
		boot_loader_write_mem32(pao, dsp_index, 0x01B7C100, 0x0000);
		hpios_delay_micro_seconds(1000);
		/* EMIF = 189/3=63MHz */
		boot_loader_write_mem32(pao, dsp_index, 0x01B7C120, 0x8002);
		/* peri = 189/2 */
		boot_loader_write_mem32(pao, dsp_index, 0x01B7C11C, 0x8001);
		/* cpu  = 189/1 */
		boot_loader_write_mem32(pao, dsp_index, 0x01B7C118, 0x8000);
		hpios_delay_micro_seconds(1000);
		/* ** SGT test to take GPO3 high when we start the PLL */
		/* and low when the delay is completed */
		/* FSX0 <- '1' (GPO3) */
		boot_loader_write_mem32(pao, 0, (0x018C0024L), 0x00002A0A);
		/* PLL not bypassed */
		boot_loader_write_mem32(pao, dsp_index, 0x01B7C100, 0x0001);
		hpios_delay_micro_seconds(1000);
		/* FSX0 <- '0' (GPO3) */
		boot_loader_write_mem32(pao, 0, (0x018C0024L), 0x00002A02);

		/* 6205 EMIF CE1 resetup - 32 bit async. */
		/* Now 6713 #1 is running at 189MHz can reduce waitstates */
		boot_loader_write_mem32(pao, 0, 0x01800004,	/* CE1 */
			(1L << WS_OFS) | (8L << WST_OFS) | (1L << WH_OFS) |
			(1L << RS_OFS) | (12L << RST_OFS) | (1L << RH_OFS) |
			(2L << MTYPE_OFS));

		hpios_delay_micro_seconds(1000);

		/* check that we can read one of the PLL registers */
		/* PLL should not be bypassed! */
		if ((boot_loader_read_mem32(pao, dsp_index, 0x01B7C100) & 0xF)
			!= 0x0001) {
			err = hpi6205_error(dsp_index,
				HPI6205_ERROR_C6713_PLL);
			return err;
		}
		/* setup C67x EMIF  (note this is the only use of
		   BAR1 via BootLoader_WriteMem32) */
		boot_loader_write_mem32(pao, dsp_index, C6713_EMIF_GCTL,
			0x000034A8);
		boot_loader_write_mem32(pao, dsp_index, C6713_EMIF_CE0,
			0x00000030);
		boot_loader_write_mem32(pao, dsp_index, C6713_EMIF_SDRAMEXT,
			0x001BDF29);
		boot_loader_write_mem32(pao, dsp_index, C6713_EMIF_SDRAMCTL,
			0x47117000);
		boot_loader_write_mem32(pao, dsp_index,
			C6713_EMIF_SDRAMTIMING, 0x00000410);

		hpios_delay_micro_seconds(1000);
	} else if (dsp_index == 2) {
		/* DSP 2 is a C6713 */

	} else
		err = hpi6205_error(dsp_index, HPI6205_ERROR_BAD_DSPINDEX);
	return err;
}

static u16 boot_loader_test_memory(struct hpi_adapter_obj *pao, int dsp_index,
	u32 start_address, u32 length)
{
	u32 i = 0, j = 0;
	u32 test_addr = 0;
	u32 test_data = 0, data = 0;

	length = 1000;

	/* for 1st word, test each bit in the 32bit word, */
	/* dwLength specifies number of 32bit words to test */
	/*for(i=0; i<dwLength; i++) */
	i = 0;
	{
		test_addr = start_address + i * 4;
		test_data = 0x00000001;
		for (j = 0; j < 32; j++) {
			boot_loader_write_mem32(pao, dsp_index, test_addr,
				test_data);
			data = boot_loader_read_mem32(pao, dsp_index,
				test_addr);
			if (data != test_data) {
				HPI_DEBUG_LOG(VERBOSE,
					"memtest error details  "
					"%08x %08x %08x %i\n", test_addr,
					test_data, data, dsp_index);
				return 1;	/* error */
			}
			test_data = test_data << 1;
		}	/* for(j) */
	}	/* for(i) */

	/* for the next 100 locations test each location, leaving it as zero */
	/* write a zero to the next word in memory before we read */
	/* the previous write to make sure every memory location is unique */
	for (i = 0; i < 100; i++) {
		test_addr = start_address + i * 4;
		test_data = 0xA5A55A5A;
		boot_loader_write_mem32(pao, dsp_index, test_addr, test_data);
		boot_loader_write_mem32(pao, dsp_index, test_addr + 4, 0);
		data = boot_loader_read_mem32(pao, dsp_index, test_addr);
		if (data != test_data) {
			HPI_DEBUG_LOG(VERBOSE,
				"memtest error details  "
				"%08x %08x %08x %i\n", test_addr, test_data,
				data, dsp_index);
			return 1;	/* error */
		}
		/* leave location as zero */
		boot_loader_write_mem32(pao, dsp_index, test_addr, 0x0);
	}

	/* zero out entire memory block */
	for (i = 0; i < length; i++) {
		test_addr = start_address + i * 4;
		boot_loader_write_mem32(pao, dsp_index, test_addr, 0x0);
	}
	return 0;
}

static u16 boot_loader_test_internal_memory(struct hpi_adapter_obj *pao,
	int dsp_index)
{
	int err = 0;
	if (dsp_index == 0) {
		/* DSP 0 is a C6205 */
		/* 64K prog mem */
		err = boot_loader_test_memory(pao, dsp_index, 0x00000000,
			0x10000);
		if (!err)
			/* 64K data mem */
			err = boot_loader_test_memory(pao, dsp_index,
				0x80000000, 0x10000);
	} else if ((dsp_index == 1) || (dsp_index == 2)) {
		/* DSP 1&2 are a C6713 */
		/* 192K internal mem */
		err = boot_loader_test_memory(pao, dsp_index, 0x00000000,
			0x30000);
		if (!err)
			/* 64K internal mem / L2 cache */
			err = boot_loader_test_memory(pao, dsp_index,
				0x00030000, 0x10000);
	} else
		return hpi6205_error(dsp_index, HPI6205_ERROR_BAD_DSPINDEX);

	if (err)
		return hpi6205_error(dsp_index, HPI6205_ERROR_DSP_INTMEM);
	else
		return 0;
}

static u16 boot_loader_test_external_memory(struct hpi_adapter_obj *pao,
	int dsp_index)
{
	u32 dRAM_start_address = 0;
	u32 dRAM_size = 0;

	if (dsp_index == 0) {
		/* only test for SDRAM if an ASI5000 card */
		if (pao->pci.subsys_device_id == 0x5000) {
			/* DSP 0 is always C6205 */
			dRAM_start_address = 0x00400000;
			dRAM_size = 0x200000;
			/*dwDRAMinc=1024; */
		} else
			return 0;
	} else if ((dsp_index == 1) || (dsp_index == 2)) {
		/* DSP 1 is a C6713 */
		dRAM_start_address = 0x80000000;
		dRAM_size = 0x200000;
		/*dwDRAMinc=1024; */
	} else
		return hpi6205_error(dsp_index, HPI6205_ERROR_BAD_DSPINDEX);

	if (boot_loader_test_memory(pao, dsp_index, dRAM_start_address,
			dRAM_size))
		return hpi6205_error(dsp_index, HPI6205_ERROR_DSP_EXTMEM);
	return 0;
}

static u16 boot_loader_test_pld(struct hpi_adapter_obj *pao, int dsp_index)
{
	u32 data = 0;
	if (dsp_index == 0) {
		/* only test for DSP0 PLD on ASI5000 card */
		if (pao->pci.subsys_device_id == 0x5000) {
			/* PLD is located at CE3=0x03000000 */
			data = boot_loader_read_mem32(pao, dsp_index,
				0x03000008);
			if ((data & 0xF) != 0x5)
				return hpi6205_error(dsp_index,
					HPI6205_ERROR_DSP_PLD);
			data = boot_loader_read_mem32(pao, dsp_index,
				0x0300000C);
			if ((data & 0xF) != 0xA)
				return hpi6205_error(dsp_index,
					HPI6205_ERROR_DSP_PLD);
		}
	} else if (dsp_index == 1) {
		/* DSP 1 is a C6713 */
		if (pao->pci.subsys_device_id == 0x8700) {
			/* PLD is located at CE1=0x90000000 */
			data = boot_loader_read_mem32(pao, dsp_index,
				0x90000010);
			if ((data & 0xFF) != 0xAA)
				return hpi6205_error(dsp_index,
					HPI6205_ERROR_DSP_PLD);
			/* 8713 - LED on */
			boot_loader_write_mem32(pao, dsp_index, 0x90000000,
				0x02);
		}
	}
	return 0;
}

/** Transfer data to or from DSP
 nOperation = H620_H620_HIF_SEND_DATA or H620_HIF_GET_DATA
*/
static short hpi6205_transfer_data(struct hpi_adapter_obj *pao, u8 *p_data,
	u32 data_size, int operation)
{
	struct hpi_hw_obj *phw = pao->priv;
	u32 data_transferred = 0;
	u16 err = 0;
#ifndef HPI6205_NO_HSR_POLL
	u32 time_out;
#endif
	u32 temp2;
	struct bus_master_interface *interface = phw->p_interface_buffer;

	if (!p_data)
		return HPI_ERROR_INVALID_DATA_TRANSFER;

	data_size &= ~3L;	/* round data_size down to nearest 4 bytes */

	/* make sure state is IDLE */
	if (!wait_dsp_ack(phw, H620_HIF_IDLE, HPI6205_TIMEOUT))
		return HPI_ERROR_DSP_HARDWARE;

	while (data_transferred < data_size) {
		u32 this_copy = data_size - data_transferred;

		if (this_copy > HPI6205_SIZEOF_DATA)
			this_copy = HPI6205_SIZEOF_DATA;

		if (operation == H620_HIF_SEND_DATA)
			memcpy((void *)&interface->u.b_data[0],
				&p_data[data_transferred], this_copy);

		interface->transfer_size_in_bytes = this_copy;

#ifdef HPI6205_NO_HSR_POLL
		/* DSP must change this back to nOperation */
		interface->dsp_ack = H620_HIF_IDLE;
#endif

		send_dsp_command(phw, operation);

#ifdef HPI6205_NO_HSR_POLL
		temp2 = wait_dsp_ack(phw, operation, HPI6205_TIMEOUT);
		HPI_DEBUG_LOG(DEBUG, "spun %d times for data xfer of %d\n",
			HPI6205_TIMEOUT - temp2, this_copy);

		if (!temp2) {
			/* timed out */
			HPI_DEBUG_LOG(ERROR,
				"timed out waiting for " "state %d got %d\n",
				operation, interface->dsp_ack);

			break;
		}
#else
		/* spin waiting on the result */
		time_out = HPI6205_TIMEOUT;
		temp2 = 0;
		while ((temp2 == 0) && time_out--) {
			/* give 16k bus mastering transfer time to happen */
			/*(16k / 132Mbytes/s = 122usec) */
			hpios_delay_micro_seconds(20);
			temp2 = ioread32(phw->prHSR);
			temp2 &= C6205_HSR_INTSRC;
		}
		HPI_DEBUG_LOG(DEBUG, "spun %d times for data xfer of %d\n",
			HPI6205_TIMEOUT - time_out, this_copy);
		if (temp2 == C6205_HSR_INTSRC) {
			HPI_DEBUG_LOG(VERBOSE,
				"interrupt from HIF <data> OK\n");
			/*
			   if(interface->dwDspAck != nOperation) {
			   HPI_DEBUG_LOG(DEBUG("interface->dwDspAck=%d,
			   expected %d \n",
			   interface->dwDspAck,nOperation);
			   }
			 */
		}
/* need to handle this differently... */
		else {
			HPI_DEBUG_LOG(ERROR,
				"interrupt from HIF <data> BAD\n");
			err = HPI_ERROR_DSP_HARDWARE;
		}

		/* reset the interrupt from the DSP */
		iowrite32(C6205_HSR_INTSRC, phw->prHSR);
#endif
		if (operation == H620_HIF_GET_DATA)
			memcpy(&p_data[data_transferred],
				(void *)&interface->u.b_data[0], this_copy);

		data_transferred += this_copy;
	}
	if (interface->dsp_ack != operation)
		HPI_DEBUG_LOG(DEBUG, "interface->dsp_ack=%d, expected %d\n",
			interface->dsp_ack, operation);
	/*                      err=HPI_ERROR_DSP_HARDWARE; */

	send_dsp_command(phw, H620_HIF_IDLE);

	return err;
}

/* wait for up to timeout_us microseconds for the DSP
   to signal state by DMA into dwDspAck
*/
static int wait_dsp_ack(struct hpi_hw_obj *phw, int state, int timeout_us)
{
	struct bus_master_interface *interface = phw->p_interface_buffer;
	int t = timeout_us / 4;

	rmb();	/* ensure interface->dsp_ack is up to date */
	while ((interface->dsp_ack != state) && --t) {
		hpios_delay_micro_seconds(4);
		rmb();	/* DSP changes dsp_ack by DMA */
	}

	/*HPI_DEBUG_LOG(VERBOSE, "Spun %d for %d\n", timeout_us/4-t, state); */
	return t * 4;
}

/* set the busmaster interface to cmd, then interrupt the DSP */
static void send_dsp_command(struct hpi_hw_obj *phw, int cmd)
{
	struct bus_master_interface *interface = phw->p_interface_buffer;

	u32 r;

	interface->host_cmd = cmd;
	wmb();	/* DSP gets state by DMA, make sure it is written to memory */
	/* before we interrupt the DSP */
	r = ioread32(phw->prHDCR);
	r |= (u32)C6205_HDCR_DSPINT;
	iowrite32(r, phw->prHDCR);
	r &= ~(u32)C6205_HDCR_DSPINT;
	iowrite32(r, phw->prHDCR);
}

static unsigned int message_count;

static u16 message_response_sequence(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
#ifndef HPI6205_NO_HSR_POLL
	u32 temp2;
#endif
	u32 time_out, time_out2;
	struct hpi_hw_obj *phw = pao->priv;
	struct bus_master_interface *interface = phw->p_interface_buffer;
	u16 err = 0;

	message_count++;
	/* Assume buffer of type struct bus_master_interface
	   is allocated "noncacheable" */

	if (!wait_dsp_ack(phw, H620_HIF_IDLE, HPI6205_TIMEOUT)) {
		HPI_DEBUG_LOG(DEBUG, "timeout waiting for idle\n");
		return hpi6205_error(0, HPI6205_ERROR_MSG_RESP_IDLE_TIMEOUT);
	}
	interface->u.message_buffer = *phm;
	/* signal we want a response */
	send_dsp_command(phw, H620_HIF_GET_RESP);

	time_out2 = wait_dsp_ack(phw, H620_HIF_GET_RESP, HPI6205_TIMEOUT);

	if (time_out2 == 0) {
		HPI_DEBUG_LOG(ERROR,
			"(%u) timed out waiting for " "GET_RESP state [%x]\n",
			message_count, interface->dsp_ack);
	} else {
		HPI_DEBUG_LOG(VERBOSE,
			"(%u) transition to GET_RESP after %u\n",
			message_count, HPI6205_TIMEOUT - time_out2);
	}
	/* spin waiting on HIF interrupt flag (end of msg process) */
	time_out = HPI6205_TIMEOUT;

#ifndef HPI6205_NO_HSR_POLL
	temp2 = 0;
	while ((temp2 == 0) && --time_out) {
		temp2 = ioread32(phw->prHSR);
		temp2 &= C6205_HSR_INTSRC;
		hpios_delay_micro_seconds(1);
	}
	if (temp2 == C6205_HSR_INTSRC) {
		rmb();	/* ensure we see latest value for dsp_ack */
		if ((interface->dsp_ack != H620_HIF_GET_RESP)) {
			HPI_DEBUG_LOG(DEBUG,
				"(%u)interface->dsp_ack(0x%x) != "
				"H620_HIF_GET_RESP, t=%u\n", message_count,
				interface->dsp_ack,
				HPI6205_TIMEOUT - time_out);
		} else {
			HPI_DEBUG_LOG(VERBOSE,
				"(%u)int with GET_RESP after %u\n",
				message_count, HPI6205_TIMEOUT - time_out);
		}

	} else {
		/* can we do anything else in response to the error ? */
		HPI_DEBUG_LOG(ERROR,
			"interrupt from HIF module BAD (function %x)\n",
			phm->function);
	}

	/* reset the interrupt from the DSP */
	iowrite32(C6205_HSR_INTSRC, phw->prHSR);
#endif

	/* read the result */
	if (time_out != 0)
		*phr = interface->u.response_buffer;

	/* set interface back to idle */
	send_dsp_command(phw, H620_HIF_IDLE);

	if ((time_out == 0) || (time_out2 == 0)) {
		HPI_DEBUG_LOG(DEBUG, "something timed out!\n");
		return hpi6205_error(0, HPI6205_ERROR_MSG_RESP_TIMEOUT);
	}
	/* special case for adapter close - */
	/* wait for the DSP to indicate it is idle */
	if (phm->function == HPI_ADAPTER_CLOSE) {
		if (!wait_dsp_ack(phw, H620_HIF_IDLE, HPI6205_TIMEOUT)) {
			HPI_DEBUG_LOG(DEBUG,
				"timeout waiting for idle "
				"(on adapter_close)\n");
			return hpi6205_error(0,
				HPI6205_ERROR_MSG_RESP_IDLE_TIMEOUT);
		}
	}
	err = hpi_validate_response(phm, phr);
	return err;
}

static void hw_message(struct hpi_adapter_obj *pao, struct hpi_message *phm,
	struct hpi_response *phr)
{

	u16 err = 0;

	hpios_dsplock_lock(pao);

	err = message_response_sequence(pao, phm, phr);

	/* maybe an error response */
	if (err) {
		/* something failed in the HPI/DSP interface */
		phr->error = err;
		pao->dsp_crashed++;

		/* just the header of the response is valid */
		phr->size = sizeof(struct hpi_response_header);
		goto err;
	} else
		pao->dsp_crashed = 0;

	if (phr->error != 0)	/* something failed in the DSP */
		goto err;

	switch (phm->function) {
	case HPI_OSTREAM_WRITE:
	case HPI_ISTREAM_ANC_WRITE:
		err = hpi6205_transfer_data(pao, phm->u.d.u.data.pb_data,
			phm->u.d.u.data.data_size, H620_HIF_SEND_DATA);
		break;

	case HPI_ISTREAM_READ:
	case HPI_OSTREAM_ANC_READ:
		err = hpi6205_transfer_data(pao, phm->u.d.u.data.pb_data,
			phm->u.d.u.data.data_size, H620_HIF_GET_DATA);
		break;

	case HPI_CONTROL_SET_STATE:
		if (phm->object == HPI_OBJ_CONTROLEX
			&& phm->u.cx.attribute == HPI_COBRANET_SET_DATA)
			err = hpi6205_transfer_data(pao,
				phm->u.cx.u.cobranet_bigdata.pb_data,
				phm->u.cx.u.cobranet_bigdata.byte_count,
				H620_HIF_SEND_DATA);
		break;

	case HPI_CONTROL_GET_STATE:
		if (phm->object == HPI_OBJ_CONTROLEX
			&& phm->u.cx.attribute == HPI_COBRANET_GET_DATA)
			err = hpi6205_transfer_data(pao,
				phm->u.cx.u.cobranet_bigdata.pb_data,
				phr->u.cx.u.cobranet_data.byte_count,
				H620_HIF_GET_DATA);
		break;
	}
	phr->error = err;

err:
	hpios_dsplock_unlock(pao);

	return;
}
