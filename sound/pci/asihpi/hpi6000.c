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

 Hardware Programming Interface (HPI) for AudioScience ASI6200 series adapters.
 These PCI bus adapters are based on the TI C6711 DSP.

 Exported functions:
 void HPI_6000(struct hpi_message *phm, struct hpi_response *phr)

 #defines
 HIDE_PCI_ASSERTS to show the PCI asserts
 PROFILE_DSP2 get profile data from DSP2 if present (instead of DSP 1)

(C) Copyright AudioScience Inc. 1998-2003
*******************************************************************************/
#define SOURCEFILE_NAME "hpi6000.c"

#include "hpi_internal.h"
#include "hpimsginit.h"
#include "hpidebug.h"
#include "hpi6000.h"
#include "hpidspcd.h"
#include "hpicmn.h"

#define HPI_HIF_BASE (0x00000200)	/* start of C67xx internal RAM */
#define HPI_HIF_ADDR(member) \
	(HPI_HIF_BASE + offsetof(struct hpi_hif_6000, member))
#define HPI_HIF_ERROR_MASK      0x4000

/* HPI6000 specific error codes */
#define HPI6000_ERROR_BASE 900	/* not actually used anywhere */

/* operational/messaging errors */
#define HPI6000_ERROR_MSG_RESP_IDLE_TIMEOUT             901

#define HPI6000_ERROR_MSG_RESP_GET_RESP_ACK             903
#define HPI6000_ERROR_MSG_GET_ADR                       904
#define HPI6000_ERROR_RESP_GET_ADR                      905
#define HPI6000_ERROR_MSG_RESP_BLOCKWRITE32             906
#define HPI6000_ERROR_MSG_RESP_BLOCKREAD32              907

#define HPI6000_ERROR_CONTROL_CACHE_PARAMS              909

#define HPI6000_ERROR_SEND_DATA_IDLE_TIMEOUT            911
#define HPI6000_ERROR_SEND_DATA_ACK                     912
#define HPI6000_ERROR_SEND_DATA_ADR                     913
#define HPI6000_ERROR_SEND_DATA_TIMEOUT                 914
#define HPI6000_ERROR_SEND_DATA_CMD                     915
#define HPI6000_ERROR_SEND_DATA_WRITE                   916
#define HPI6000_ERROR_SEND_DATA_IDLECMD                 917

#define HPI6000_ERROR_GET_DATA_IDLE_TIMEOUT             921
#define HPI6000_ERROR_GET_DATA_ACK                      922
#define HPI6000_ERROR_GET_DATA_CMD                      923
#define HPI6000_ERROR_GET_DATA_READ                     924
#define HPI6000_ERROR_GET_DATA_IDLECMD                  925

#define HPI6000_ERROR_CONTROL_CACHE_ADDRLEN             951
#define HPI6000_ERROR_CONTROL_CACHE_READ                952
#define HPI6000_ERROR_CONTROL_CACHE_FLUSH               953

#define HPI6000_ERROR_MSG_RESP_GETRESPCMD               961
#define HPI6000_ERROR_MSG_RESP_IDLECMD                  962

/* Initialisation/bootload errors */
#define HPI6000_ERROR_UNHANDLED_SUBSYS_ID               930

/* can't access PCI2040 */
#define HPI6000_ERROR_INIT_PCI2040                      931
/* can't access DSP HPI i/f */
#define HPI6000_ERROR_INIT_DSPHPI                       932
/* can't access internal DSP memory */
#define HPI6000_ERROR_INIT_DSPINTMEM                    933
/* can't access SDRAM - test#1 */
#define HPI6000_ERROR_INIT_SDRAM1                       934
/* can't access SDRAM - test#2 */
#define HPI6000_ERROR_INIT_SDRAM2                       935

#define HPI6000_ERROR_INIT_VERIFY                       938

#define HPI6000_ERROR_INIT_NOACK                        939

#define HPI6000_ERROR_INIT_PLDTEST1                     941
#define HPI6000_ERROR_INIT_PLDTEST2                     942

/* local defines */

#define HIDE_PCI_ASSERTS
#define PROFILE_DSP2

/* for PCI2040 i/f chip */
/* HPI CSR registers */
/* word offsets from CSR base */
/* use when io addresses defined as u32 * */

#define INTERRUPT_EVENT_SET     0
#define INTERRUPT_EVENT_CLEAR   1
#define INTERRUPT_MASK_SET      2
#define INTERRUPT_MASK_CLEAR    3
#define HPI_ERROR_REPORT        4
#define HPI_RESET               5
#define HPI_DATA_WIDTH          6

#define MAX_DSPS 2
/* HPI registers, spaced 8K bytes = 2K words apart */
#define DSP_SPACING             0x800

#define CONTROL                 0x0000
#define ADDRESS                 0x0200
#define DATA_AUTOINC            0x0400
#define DATA                    0x0600

#define TIMEOUT 500000

struct dsp_obj {
	__iomem u32 *prHPI_control;
	__iomem u32 *prHPI_address;
	__iomem u32 *prHPI_data;
	__iomem u32 *prHPI_data_auto_inc;
	char c_dsp_rev;		/*A, B */
	u32 control_cache_address_on_dsp;
	u32 control_cache_length_on_dsp;
	struct hpi_adapter_obj *pa_parent_adapter;
};

struct hpi_hw_obj {
	__iomem u32 *dw2040_HPICSR;
	__iomem u32 *dw2040_HPIDSP;

	u16 num_dsp;
	struct dsp_obj ado[MAX_DSPS];

	u32 message_buffer_address_on_dsp;
	u32 response_buffer_address_on_dsp;
	u32 pCI2040HPI_error_count;

	struct hpi_control_cache_single control_cache[HPI_NMIXER_CONTROLS];
	struct hpi_control_cache *p_cache;
};

static u16 hpi6000_dsp_block_write32(struct hpi_adapter_obj *pao,
	u16 dsp_index, u32 hpi_address, u32 *source, u32 count);
static u16 hpi6000_dsp_block_read32(struct hpi_adapter_obj *pao,
	u16 dsp_index, u32 hpi_address, u32 *dest, u32 count);

static short hpi6000_adapter_boot_load_dsp(struct hpi_adapter_obj *pao,
	u32 *pos_error_code);
static short hpi6000_check_PCI2040_error_flag(struct hpi_adapter_obj *pao,
	u16 read_or_write);
#define H6READ 1
#define H6WRITE 0

static short hpi6000_update_control_cache(struct hpi_adapter_obj *pao,
	struct hpi_message *phm);
static short hpi6000_message_response_sequence(struct hpi_adapter_obj *pao,
	u16 dsp_index, struct hpi_message *phm, struct hpi_response *phr);

static void hw_message(struct hpi_adapter_obj *pao, struct hpi_message *phm,
	struct hpi_response *phr);

static short hpi6000_wait_dsp_ack(struct hpi_adapter_obj *pao, u16 dsp_index,
	u32 ack_value);

static short hpi6000_send_host_command(struct hpi_adapter_obj *pao,
	u16 dsp_index, u32 host_cmd);

static void hpi6000_send_dsp_interrupt(struct dsp_obj *pdo);

static short hpi6000_send_data(struct hpi_adapter_obj *pao, u16 dsp_index,
	struct hpi_message *phm, struct hpi_response *phr);

static short hpi6000_get_data(struct hpi_adapter_obj *pao, u16 dsp_index,
	struct hpi_message *phm, struct hpi_response *phr);

static void hpi_write_word(struct dsp_obj *pdo, u32 address, u32 data);

static u32 hpi_read_word(struct dsp_obj *pdo, u32 address);

static void hpi_write_block(struct dsp_obj *pdo, u32 address, u32 *pdata,
	u32 length);

static void hpi_read_block(struct dsp_obj *pdo, u32 address, u32 *pdata,
	u32 length);

static void subsys_create_adapter(struct hpi_message *phm,
	struct hpi_response *phr);

static void adapter_delete(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static void adapter_get_asserts(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr);

static short create_adapter_obj(struct hpi_adapter_obj *pao,
	u32 *pos_error_code);

static void delete_adapter_obj(struct hpi_adapter_obj *pao);

/* local globals */

static u16 gw_pci_read_asserts;	/* used to count PCI2040 errors */
static u16 gw_pci_write_asserts;	/* used to count PCI2040 errors */

static void subsys_message(struct hpi_message *phm, struct hpi_response *phr)
{
	switch (phm->function) {
	case HPI_SUBSYS_CREATE_ADAPTER:
		subsys_create_adapter(phm, phr);
		break;
	default:
		phr->error = HPI_ERROR_INVALID_FUNC;
		break;
	}
}

static void control_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	switch (phm->function) {
	case HPI_CONTROL_GET_STATE:
		if (pao->has_control_cache) {
			u16 err;
			err = hpi6000_update_control_cache(pao, phm);

			if (err) {
				if (err >= HPI_ERROR_BACKEND_BASE) {
					phr->error =
						HPI_ERROR_CONTROL_CACHING;
					phr->specific_error = err;
				} else {
					phr->error = err;
				}
				break;
			}

			if (hpi_check_control_cache(((struct hpi_hw_obj *)
						pao->priv)->p_cache, phm,
					phr))
				break;
		}
		hw_message(pao, phm, phr);
		break;
	case HPI_CONTROL_SET_STATE:
		hw_message(pao, phm, phr);
		hpi_cmn_control_cache_sync_to_msg(((struct hpi_hw_obj *)pao->
				priv)->p_cache, phm, phr);
		break;

	case HPI_CONTROL_GET_INFO:
	default:
		hw_message(pao, phm, phr);
		break;
	}
}

static void adapter_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	switch (phm->function) {
	case HPI_ADAPTER_GET_ASSERT:
		adapter_get_asserts(pao, phm, phr);
		break;

	case HPI_ADAPTER_DELETE:
		adapter_delete(pao, phm, phr);
		break;

	default:
		hw_message(pao, phm, phr);
		break;
	}
}

static void outstream_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	switch (phm->function) {
	case HPI_OSTREAM_HOSTBUFFER_ALLOC:
	case HPI_OSTREAM_HOSTBUFFER_FREE:
		/* Don't let these messages go to the HW function because
		 * they're called without locking the spinlock.
		 * For the HPI6000 adapters the HW would return
		 * HPI_ERROR_INVALID_FUNC anyway.
		 */
		phr->error = HPI_ERROR_INVALID_FUNC;
		break;
	default:
		hw_message(pao, phm, phr);
		return;
	}
}

static void instream_message(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{

	switch (phm->function) {
	case HPI_ISTREAM_HOSTBUFFER_ALLOC:
	case HPI_ISTREAM_HOSTBUFFER_FREE:
		/* Don't let these messages go to the HW function because
		 * they're called without locking the spinlock.
		 * For the HPI6000 adapters the HW would return
		 * HPI_ERROR_INVALID_FUNC anyway.
		 */
		phr->error = HPI_ERROR_INVALID_FUNC;
		break;
	default:
		hw_message(pao, phm, phr);
		return;
	}
}

/************************************************************************/
/** HPI_6000()
 * Entry point from HPIMAN
 * All calls to the HPI start here
 */
void HPI_6000(struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_adapter_obj *pao = NULL;

	if (phm->object != HPI_OBJ_SUBSYSTEM) {
		pao = hpi_find_adapter(phm->adapter_index);
		if (!pao) {
			hpi_init_response(phr, phm->object, phm->function,
				HPI_ERROR_BAD_ADAPTER_NUMBER);
			HPI_DEBUG_LOG(DEBUG, "invalid adapter index: %d \n",
				phm->adapter_index);
			return;
		}

		/* Don't even try to communicate with crashed DSP */
		if (pao->dsp_crashed >= 10) {
			hpi_init_response(phr, phm->object, phm->function,
				HPI_ERROR_DSP_HARDWARE);
			HPI_DEBUG_LOG(DEBUG, "adapter %d dsp crashed\n",
				phm->adapter_index);
			return;
		}
	}
	/* Init default response including the size field */
	if (phm->function != HPI_SUBSYS_CREATE_ADAPTER)
		hpi_init_response(phr, phm->object, phm->function,
			HPI_ERROR_PROCESSING_MESSAGE);

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

/************************************************************************/
/* SUBSYSTEM */

/* create an adapter object and initialise it based on resource information
 * passed in in the message
 * NOTE - you cannot use this function AND the FindAdapters function at the
 * same time, the application must use only one of them to get the adapters
 */
static void subsys_create_adapter(struct hpi_message *phm,
	struct hpi_response *phr)
{
	/* create temp adapter obj, because we don't know what index yet */
	struct hpi_adapter_obj ao;
	struct hpi_adapter_obj *pao;
	u32 os_error_code;
	u16 err = 0;
	u32 dsp_index = 0;

	HPI_DEBUG_LOG(VERBOSE, "subsys_create_adapter\n");

	memset(&ao, 0, sizeof(ao));

	ao.priv = kzalloc(sizeof(struct hpi_hw_obj), GFP_KERNEL);
	if (!ao.priv) {
		HPI_DEBUG_LOG(ERROR, "can't get mem for adapter object\n");
		phr->error = HPI_ERROR_MEMORY_ALLOC;
		return;
	}

	/* create the adapter object based on the resource information */
	ao.pci = *phm->u.s.resource.r.pci;

	err = create_adapter_obj(&ao, &os_error_code);
	if (err) {
		delete_adapter_obj(&ao);
		if (err >= HPI_ERROR_BACKEND_BASE) {
			phr->error = HPI_ERROR_DSP_BOOTLOAD;
			phr->specific_error = err;
		} else {
			phr->error = err;
		}

		phr->u.s.data = os_error_code;
		return;
	}
	/* need to update paParentAdapter */
	pao = hpi_find_adapter(ao.index);
	if (!pao) {
		/* We just added this adapter, why can't we find it!? */
		HPI_DEBUG_LOG(ERROR, "lost adapter after boot\n");
		phr->error = HPI_ERROR_BAD_ADAPTER;
		return;
	}

	for (dsp_index = 0; dsp_index < MAX_DSPS; dsp_index++) {
		struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;
		phw->ado[dsp_index].pa_parent_adapter = pao;
	}

	phr->u.s.adapter_type = ao.adapter_type;
	phr->u.s.adapter_index = ao.index;
	phr->error = 0;
}

static void adapter_delete(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
	delete_adapter_obj(pao);
	hpi_delete_adapter(pao);
	phr->error = 0;
}

/* this routine is called from SubSysFindAdapter and SubSysCreateAdapter */
static short create_adapter_obj(struct hpi_adapter_obj *pao,
	u32 *pos_error_code)
{
	short boot_error = 0;
	u32 dsp_index = 0;
	u32 control_cache_size = 0;
	u32 control_cache_count = 0;
	struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;

	/* The PCI2040 has the following address map */
	/* BAR0 - 4K = HPI control and status registers on PCI2040 (HPI CSR) */
	/* BAR1 - 32K = HPI registers on DSP */
	phw->dw2040_HPICSR = pao->pci.ap_mem_base[0];
	phw->dw2040_HPIDSP = pao->pci.ap_mem_base[1];
	HPI_DEBUG_LOG(VERBOSE, "csr %p, dsp %p\n", phw->dw2040_HPICSR,
		phw->dw2040_HPIDSP);

	/* set addresses for the possible DSP HPI interfaces */
	for (dsp_index = 0; dsp_index < MAX_DSPS; dsp_index++) {
		phw->ado[dsp_index].prHPI_control =
			phw->dw2040_HPIDSP + (CONTROL +
			DSP_SPACING * dsp_index);

		phw->ado[dsp_index].prHPI_address =
			phw->dw2040_HPIDSP + (ADDRESS +
			DSP_SPACING * dsp_index);
		phw->ado[dsp_index].prHPI_data =
			phw->dw2040_HPIDSP + (DATA + DSP_SPACING * dsp_index);

		phw->ado[dsp_index].prHPI_data_auto_inc =
			phw->dw2040_HPIDSP + (DATA_AUTOINC +
			DSP_SPACING * dsp_index);

		HPI_DEBUG_LOG(VERBOSE, "ctl %p, adr %p, dat %p, dat++ %p\n",
			phw->ado[dsp_index].prHPI_control,
			phw->ado[dsp_index].prHPI_address,
			phw->ado[dsp_index].prHPI_data,
			phw->ado[dsp_index].prHPI_data_auto_inc);

		phw->ado[dsp_index].pa_parent_adapter = pao;
	}

	phw->pCI2040HPI_error_count = 0;
	pao->has_control_cache = 0;

	/* Set the default number of DSPs on this card */
	/* This is (conditionally) adjusted after bootloading */
	/* of the first DSP in the bootload section. */
	phw->num_dsp = 1;

	boot_error = hpi6000_adapter_boot_load_dsp(pao, pos_error_code);
	if (boot_error)
		return boot_error;

	HPI_DEBUG_LOG(INFO, "bootload DSP OK\n");

	phw->message_buffer_address_on_dsp = 0L;
	phw->response_buffer_address_on_dsp = 0L;

	/* get info about the adapter by asking the adapter */
	/* send a HPI_ADAPTER_GET_INFO message */
	{
		struct hpi_message hm;
		struct hpi_response hr0;	/* response from DSP 0 */
		struct hpi_response hr1;	/* response from DSP 1 */
		u16 error = 0;

		HPI_DEBUG_LOG(VERBOSE, "send ADAPTER_GET_INFO\n");
		memset(&hm, 0, sizeof(hm));
		hm.type = HPI_TYPE_MESSAGE;
		hm.size = sizeof(struct hpi_message);
		hm.object = HPI_OBJ_ADAPTER;
		hm.function = HPI_ADAPTER_GET_INFO;
		hm.adapter_index = 0;
		memset(&hr0, 0, sizeof(hr0));
		memset(&hr1, 0, sizeof(hr1));
		hr0.size = sizeof(hr0);
		hr1.size = sizeof(hr1);

		error = hpi6000_message_response_sequence(pao, 0, &hm, &hr0);
		if (hr0.error) {
			HPI_DEBUG_LOG(DEBUG, "message error %d\n", hr0.error);
			return hr0.error;
		}
		if (phw->num_dsp == 2) {
			error = hpi6000_message_response_sequence(pao, 1, &hm,
				&hr1);
			if (error)
				return error;
		}
		pao->adapter_type = hr0.u.ax.info.adapter_type;
		pao->index = hr0.u.ax.info.adapter_index;
	}

	memset(&phw->control_cache[0], 0,
		sizeof(struct hpi_control_cache_single) *
		HPI_NMIXER_CONTROLS);
	/* Read the control cache length to figure out if it is turned on */
	control_cache_size =
		hpi_read_word(&phw->ado[0],
		HPI_HIF_ADDR(control_cache_size_in_bytes));
	if (control_cache_size) {
		control_cache_count =
			hpi_read_word(&phw->ado[0],
			HPI_HIF_ADDR(control_cache_count));

		phw->p_cache =
			hpi_alloc_control_cache(control_cache_count,
			control_cache_size, (unsigned char *)
			&phw->control_cache[0]
			);
		if (phw->p_cache)
			pao->has_control_cache = 1;
	}

	HPI_DEBUG_LOG(DEBUG, "get adapter info ASI%04X index %d\n",
		pao->adapter_type, pao->index);
	pao->open = 0;	/* upon creation the adapter is closed */

	if (phw->p_cache)
		phw->p_cache->adap_idx = pao->index;

	return hpi_add_adapter(pao);
}

static void delete_adapter_obj(struct hpi_adapter_obj *pao)
{
	struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;

	if (pao->has_control_cache)
		hpi_free_control_cache(phw->p_cache);

	/* reset DSPs on adapter */
	iowrite32(0x0003000F, phw->dw2040_HPICSR + HPI_RESET);

	kfree(phw);
}

/************************************************************************/
/* ADAPTER */

static void adapter_get_asserts(struct hpi_adapter_obj *pao,
	struct hpi_message *phm, struct hpi_response *phr)
{
#ifndef HIDE_PCI_ASSERTS
	/* if we have PCI2040 asserts then collect them */
	if ((gw_pci_read_asserts > 0) || (gw_pci_write_asserts > 0)) {
		phr->u.ax.assert.p1 =
			gw_pci_read_asserts * 100 + gw_pci_write_asserts;
		phr->u.ax.assert.p2 = 0;
		phr->u.ax.assert.count = 1;	/* assert count */
		phr->u.ax.assert.dsp_index = -1;	/* "dsp index" */
		strcpy(phr->u.ax.assert.sz_message, "PCI2040 error");
		phr->u.ax.assert.dsp_msg_addr = 0;
		gw_pci_read_asserts = 0;
		gw_pci_write_asserts = 0;
		phr->error = 0;
	} else
#endif
		hw_message(pao, phm, phr);	/*get DSP asserts */

	return;
}

/************************************************************************/
/* LOW-LEVEL */

static short hpi6000_adapter_boot_load_dsp(struct hpi_adapter_obj *pao,
	u32 *pos_error_code)
{
	struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;
	short error;
	u32 timeout;
	u32 read = 0;
	u32 i = 0;
	u32 data = 0;
	u32 j = 0;
	u32 test_addr = 0x80000000;
	u32 test_data = 0x00000001;
	u32 dw2040_reset = 0;
	u32 dsp_index = 0;
	u32 endian = 0;
	u32 adapter_info = 0;
	u32 delay = 0;

	struct dsp_code dsp_code;
	u16 boot_load_family = 0;

	/* NOTE don't use wAdapterType in this routine. It is not setup yet */

	switch (pao->pci.pci_dev->subsystem_device) {
	case 0x5100:
	case 0x5110:	/* ASI5100 revB or higher with C6711D */
	case 0x5200:	/* ASI5200 PCIe version of ASI5100 */
	case 0x6100:
	case 0x6200:
		boot_load_family = HPI_ADAPTER_FAMILY_ASI(0x6200);
		break;
	default:
		return HPI6000_ERROR_UNHANDLED_SUBSYS_ID;
	}

	/* reset all DSPs, indicate two DSPs are present
	 * set RST3-=1 to disconnect HAD8 to set DSP in little endian mode
	 */
	endian = 0;
	dw2040_reset = 0x0003000F;
	iowrite32(dw2040_reset, phw->dw2040_HPICSR + HPI_RESET);

	/* read back register to make sure PCI2040 chip is functioning
	 * note that bits 4..15 are read-only and so should always return zero,
	 * even though we wrote 1 to them
	 */
	hpios_delay_micro_seconds(1000);
	delay = ioread32(phw->dw2040_HPICSR + HPI_RESET);

	if (delay != dw2040_reset) {
		HPI_DEBUG_LOG(ERROR, "INIT_PCI2040 %x %x\n", dw2040_reset,
			delay);
		return HPI6000_ERROR_INIT_PCI2040;
	}

	/* Indicate that DSP#0,1 is a C6X */
	iowrite32(0x00000003, phw->dw2040_HPICSR + HPI_DATA_WIDTH);
	/* set Bit30 and 29 - which will prevent Target aborts from being
	 * issued upon HPI or GP error
	 */
	iowrite32(0x60000000, phw->dw2040_HPICSR + INTERRUPT_MASK_SET);

	/* isolate DSP HAD8 line from PCI2040 so that
	 * Little endian can be set by pullup
	 */
	dw2040_reset = dw2040_reset & (~(endian << 3));
	iowrite32(dw2040_reset, phw->dw2040_HPICSR + HPI_RESET);

	phw->ado[0].c_dsp_rev = 'B';	/* revB */
	phw->ado[1].c_dsp_rev = 'B';	/* revB */

	/*Take both DSPs out of reset, setting HAD8 to the correct Endian */
	dw2040_reset = dw2040_reset & (~0x00000001);	/* start DSP 0 */
	iowrite32(dw2040_reset, phw->dw2040_HPICSR + HPI_RESET);
	dw2040_reset = dw2040_reset & (~0x00000002);	/* start DSP 1 */
	iowrite32(dw2040_reset, phw->dw2040_HPICSR + HPI_RESET);

	/* set HAD8 back to PCI2040, now that DSP set to little endian mode */
	dw2040_reset = dw2040_reset & (~0x00000008);
	iowrite32(dw2040_reset, phw->dw2040_HPICSR + HPI_RESET);
	/*delay to allow DSP to get going */
	hpios_delay_micro_seconds(100);

	/* loop through all DSPs, downloading DSP code */
	for (dsp_index = 0; dsp_index < phw->num_dsp; dsp_index++) {
		struct dsp_obj *pdo = &phw->ado[dsp_index];

		/* configure DSP so that we download code into the SRAM */
		/* set control reg for little endian, HWOB=1 */
		iowrite32(0x00010001, pdo->prHPI_control);

		/* test access to the HPI address register (HPIA) */
		test_data = 0x00000001;
		for (j = 0; j < 32; j++) {
			iowrite32(test_data, pdo->prHPI_address);
			data = ioread32(pdo->prHPI_address);
			if (data != test_data) {
				HPI_DEBUG_LOG(ERROR, "INIT_DSPHPI %x %x %x\n",
					test_data, data, dsp_index);
				return HPI6000_ERROR_INIT_DSPHPI;
			}
			test_data = test_data << 1;
		}

/* if C6713 the setup PLL to generate 225MHz from 25MHz.
* Since the PLLDIV1 read is sometimes wrong, even on a C6713,
* we're going to do this unconditionally
*/
/* PLLDIV1 should have a value of 8000 after reset */
/*
	if (HpiReadWord(pdo,0x01B7C118) == 0x8000)
*/
		{
			/* C6713 datasheet says we cannot program PLL from HPI,
			 * and indeed if we try to set the PLL multiply from the
			 * HPI, the PLL does not seem to lock,
			 * so we enable the PLL and use the default of x 7
			 */
			/* bypass PLL */
			hpi_write_word(pdo, 0x01B7C100, 0x0000);
			hpios_delay_micro_seconds(100);

			/*  ** use default of PLL  x7 ** */
			/* EMIF = 225/3=75MHz */
			hpi_write_word(pdo, 0x01B7C120, 0x8002);
			hpios_delay_micro_seconds(100);

			/* peri = 225/2 */
			hpi_write_word(pdo, 0x01B7C11C, 0x8001);
			hpios_delay_micro_seconds(100);

			/* cpu  = 225/1 */
			hpi_write_word(pdo, 0x01B7C118, 0x8000);

			/* ~2ms delay */
			hpios_delay_micro_seconds(2000);

			/* PLL not bypassed */
			hpi_write_word(pdo, 0x01B7C100, 0x0001);
			/* ~2ms delay */
			hpios_delay_micro_seconds(2000);
		}

		/* test r/w to internal DSP memory
		 * C6711 has L2 cache mapped to 0x0 when reset
		 *
		 *  revB - because of bug 3.0.1 last HPI read
		 * (before HPI address issued) must be non-autoinc
		 */
		/* test each bit in the 32bit word */
		for (i = 0; i < 100; i++) {
			test_addr = 0x00000000;
			test_data = 0x00000001;
			for (j = 0; j < 32; j++) {
				hpi_write_word(pdo, test_addr + i, test_data);
				data = hpi_read_word(pdo, test_addr + i);
				if (data != test_data) {
					HPI_DEBUG_LOG(ERROR,
						"DSP mem %x %x %x %x\n",
						test_addr + i, test_data,
						data, dsp_index);

					return HPI6000_ERROR_INIT_DSPINTMEM;
				}
				test_data = test_data << 1;
			}
		}

		/* memory map of ASI6200
		   00000000-0000FFFF    16Kx32 internal program
		   01800000-019FFFFF    Internal peripheral
		   80000000-807FFFFF    CE0 2Mx32 SDRAM running @ 100MHz
		   90000000-9000FFFF    CE1 Async peripherals:

		   EMIF config
		   ------------
		   Global EMIF control
		   0 -
		   1 -
		   2 -
		   3 CLK2EN = 1   CLKOUT2 enabled
		   4 CLK1EN = 0   CLKOUT1 disabled
		   5 EKEN = 1 <--!! C6713 specific, enables ECLKOUT
		   6 -
		   7 NOHOLD = 1   external HOLD disabled
		   8 HOLDA = 0    HOLDA output is low
		   9 HOLD = 0             HOLD input is low
		   10 ARDY = 1    ARDY input is high
		   11 BUSREQ = 0   BUSREQ output is low
		   12,13 Reserved = 1
		 */
		hpi_write_word(pdo, 0x01800000, 0x34A8);

		/* EMIF CE0 setup - 2Mx32 Sync DRAM
		   31..28       Wr setup
		   27..22       Wr strobe
		   21..20       Wr hold
		   19..16       Rd setup
		   15..14       -
		   13..8        Rd strobe
		   7..4         MTYPE   0011            Sync DRAM 32bits
		   3            Wr hold MSB
		   2..0         Rd hold
		 */
		hpi_write_word(pdo, 0x01800008, 0x00000030);

		/* EMIF SDRAM Extension
		   31-21        0
		   20           WR2RD = 0
		   19-18        WR2DEAC = 1
		   17           WR2WR = 0
		   16-15        R2WDQM = 2
		   14-12        RD2WR = 4
		   11-10        RD2DEAC = 1
		   9            RD2RD = 1
		   8-7          THZP = 10b
		   6-5          TWR  = 2-1 = 01b (tWR = 10ns)
		   4            TRRD = 0b = 2 ECLK (tRRD = 14ns)
		   3-1          TRAS = 5-1 = 100b (Tras=42ns = 5 ECLK)
		   1            CAS latency = 3 ECLK
		   (for Micron 2M32-7 operating at 100Mhz)
		 */

		/* need to use this else DSP code crashes */
		hpi_write_word(pdo, 0x01800020, 0x001BDF29);

		/* EMIF SDRAM control - set up for a 2Mx32 SDRAM (512x32x4 bank)
		   31           -               -
		   30           SDBSZ   1               4 bank
		   29..28       SDRSZ   00              11 row address pins
		   27..26       SDCSZ   01              8 column address pins
		   25           RFEN    1               refersh enabled
		   24           INIT    1               init SDRAM
		   23..20       TRCD    0001
		   19..16       TRP             0001
		   15..12       TRC             0110
		   11..0        -               -
		 */
		/*      need to use this else DSP code crashes */
		hpi_write_word(pdo, 0x01800018, 0x47117000);

		/* EMIF SDRAM Refresh Timing */
		hpi_write_word(pdo, 0x0180001C, 0x00000410);

		/*MIF CE1 setup - Async peripherals
		   @100MHz bus speed, each cycle is 10ns,
		   31..28       Wr setup  = 1
		   27..22       Wr strobe = 3                   30ns
		   21..20       Wr hold = 1
		   19..16       Rd setup =1
		   15..14       Ta = 2
		   13..8        Rd strobe = 3                   30ns
		   7..4         MTYPE   0010            Async 32bits
		   3            Wr hold MSB =0
		   2..0         Rd hold = 1
		 */
		{
			u32 cE1 =
				(1L << 28) | (3L << 22) | (1L << 20) | (1L <<
				16) | (2L << 14) | (3L << 8) | (2L << 4) | 1L;
			hpi_write_word(pdo, 0x01800004, cE1);
		}

		/* delay a little to allow SDRAM and DSP to "get going" */
		hpios_delay_micro_seconds(1000);

		/* test access to SDRAM */
		{
			test_addr = 0x80000000;
			test_data = 0x00000001;
			/* test each bit in the 32bit word */
			for (j = 0; j < 32; j++) {
				hpi_write_word(pdo, test_addr, test_data);
				data = hpi_read_word(pdo, test_addr);
				if (data != test_data) {
					HPI_DEBUG_LOG(ERROR,
						"DSP dram %x %x %x %x\n",
						test_addr, test_data, data,
						dsp_index);

					return HPI6000_ERROR_INIT_SDRAM1;
				}
				test_data = test_data << 1;
			}
			/* test every Nth address in the DRAM */
#define DRAM_SIZE_WORDS 0x200000	/*2_mx32 */
#define DRAM_INC 1024
			test_addr = 0x80000000;
			test_data = 0x0;
			for (i = 0; i < DRAM_SIZE_WORDS; i = i + DRAM_INC) {
				hpi_write_word(pdo, test_addr + i, test_data);
				test_data++;
			}
			test_addr = 0x80000000;
			test_data = 0x0;
			for (i = 0; i < DRAM_SIZE_WORDS; i = i + DRAM_INC) {
				data = hpi_read_word(pdo, test_addr + i);
				if (data != test_data) {
					HPI_DEBUG_LOG(ERROR,
						"DSP dram %x %x %x %x\n",
						test_addr + i, test_data,
						data, dsp_index);
					return HPI6000_ERROR_INIT_SDRAM2;
				}
				test_data++;
			}

		}

		/* write the DSP code down into the DSPs memory */
		/*HpiDspCode_Open(nBootLoadFamily,&DspCode,pdwOsErrorCode); */
		dsp_code.ps_dev = pao->pci.pci_dev;

		error = hpi_dsp_code_open(boot_load_family, &dsp_code,
			pos_error_code);

		if (error)
			return error;

		while (1) {
			u32 length;
			u32 address;
			u32 type;
			u32 *pcode;

			error = hpi_dsp_code_read_word(&dsp_code, &length);
			if (error)
				break;
			if (length == 0xFFFFFFFF)
				break;	/* end of code */

			error = hpi_dsp_code_read_word(&dsp_code, &address);
			if (error)
				break;
			error = hpi_dsp_code_read_word(&dsp_code, &type);
			if (error)
				break;
			error = hpi_dsp_code_read_block(length, &dsp_code,
				&pcode);
			if (error)
				break;
			error = hpi6000_dsp_block_write32(pao, (u16)dsp_index,
				address, pcode, length);
			if (error)
				break;
		}

		if (error) {
			hpi_dsp_code_close(&dsp_code);
			return error;
		}
		/* verify that code was written correctly */
		/* this time through, assume no errors in DSP code file/array */
		hpi_dsp_code_rewind(&dsp_code);
		while (1) {
			u32 length;
			u32 address;
			u32 type;
			u32 *pcode;

			hpi_dsp_code_read_word(&dsp_code, &length);
			if (length == 0xFFFFFFFF)
				break;	/* end of code */

			hpi_dsp_code_read_word(&dsp_code, &address);
			hpi_dsp_code_read_word(&dsp_code, &type);
			hpi_dsp_code_read_block(length, &dsp_code, &pcode);

			for (i = 0; i < length; i++) {
				data = hpi_read_word(pdo, address);
				if (data != *pcode) {
					error = HPI6000_ERROR_INIT_VERIFY;
					HPI_DEBUG_LOG(ERROR,
						"DSP verify %x %x %x %x\n",
						address, *pcode, data,
						dsp_index);
					break;
				}
				pcode++;
				address += 4;
			}
			if (error)
				break;
		}
		hpi_dsp_code_close(&dsp_code);
		if (error)
			return error;

		/* zero out the hostmailbox */
		{
			u32 address = HPI_HIF_ADDR(host_cmd);
			for (i = 0; i < 4; i++) {
				hpi_write_word(pdo, address, 0);
				address += 4;
			}
		}
		/* write the DSP number into the hostmailbox */
		/* structure before starting the DSP */
		hpi_write_word(pdo, HPI_HIF_ADDR(dsp_number), dsp_index);

		/* write the DSP adapter Info into the */
		/* hostmailbox before starting the DSP */
		if (dsp_index > 0)
			hpi_write_word(pdo, HPI_HIF_ADDR(adapter_info),
				adapter_info);

		/* step 3. Start code by sending interrupt */
		iowrite32(0x00030003, pdo->prHPI_control);
		hpios_delay_micro_seconds(10000);

		/* wait for a non-zero value in hostcmd -
		 * indicating initialization is complete
		 *
		 * Init could take a while if DSP checks SDRAM memory
		 * Was 200000. Increased to 2000000 for ASI8801 so we
		 * don't get 938 errors.
		 */
		timeout = 2000000;
		while (timeout) {
			do {
				read = hpi_read_word(pdo,
					HPI_HIF_ADDR(host_cmd));
			} while (--timeout
				&& hpi6000_check_PCI2040_error_flag(pao,
					H6READ));

			if (read)
				break;
			/* The following is a workaround for bug #94:
			 * Bluescreen on install and subsequent boots on a
			 * DELL PowerEdge 600SC PC with 1.8GHz P4 and
			 * ServerWorks chipset. Without this delay the system
			 * locks up with a bluescreen (NOT GPF or pagefault).
			 */
			else
				hpios_delay_micro_seconds(10000);
		}
		if (timeout == 0)
			return HPI6000_ERROR_INIT_NOACK;

		/* read the DSP adapter Info from the */
		/* hostmailbox structure after starting the DSP */
		if (dsp_index == 0) {
			/*u32 dwTestData=0; */
			u32 mask = 0;

			adapter_info =
				hpi_read_word(pdo,
				HPI_HIF_ADDR(adapter_info));
			if (HPI_ADAPTER_FAMILY_ASI
				(HPI_HIF_ADAPTER_INFO_EXTRACT_ADAPTER
					(adapter_info)) ==
				HPI_ADAPTER_FAMILY_ASI(0x6200))
				/* all 6200 cards have this many DSPs */
				phw->num_dsp = 2;

			/* test that the PLD is programmed */
			/* and we can read/write 24bits */
#define PLD_BASE_ADDRESS 0x90000000L	/*for ASI6100/6200/8800 */

			switch (boot_load_family) {
			case HPI_ADAPTER_FAMILY_ASI(0x6200):
				/* ASI6100/6200 has 24bit path to FPGA */
				mask = 0xFFFFFF00L;
				/* ASI5100 uses AX6 code, */
				/* but has no PLD r/w register to test */
				if (HPI_ADAPTER_FAMILY_ASI(pao->pci.pci_dev->
						subsystem_device) ==
					HPI_ADAPTER_FAMILY_ASI(0x5100))
					mask = 0x00000000L;
				/* ASI5200 uses AX6 code, */
				/* but has no PLD r/w register to test */
				if (HPI_ADAPTER_FAMILY_ASI(pao->pci.pci_dev->
						subsystem_device) ==
					HPI_ADAPTER_FAMILY_ASI(0x5200))
					mask = 0x00000000L;
				break;
			case HPI_ADAPTER_FAMILY_ASI(0x8800):
				/* ASI8800 has 16bit path to FPGA */
				mask = 0xFFFF0000L;
				break;
			}
			test_data = 0xAAAAAA00L & mask;
			/* write to 24 bit Debug register (D31-D8) */
			hpi_write_word(pdo, PLD_BASE_ADDRESS + 4L, test_data);
			read = hpi_read_word(pdo,
				PLD_BASE_ADDRESS + 4L) & mask;
			if (read != test_data) {
				HPI_DEBUG_LOG(ERROR, "PLD %x %x\n", test_data,
					read);
				return HPI6000_ERROR_INIT_PLDTEST1;
			}
			test_data = 0x55555500L & mask;
			hpi_write_word(pdo, PLD_BASE_ADDRESS + 4L, test_data);
			read = hpi_read_word(pdo,
				PLD_BASE_ADDRESS + 4L) & mask;
			if (read != test_data) {
				HPI_DEBUG_LOG(ERROR, "PLD %x %x\n", test_data,
					read);
				return HPI6000_ERROR_INIT_PLDTEST2;
			}
		}
	}	/* for numDSP */
	return 0;
}

#define PCI_TIMEOUT 100

static int hpi_set_address(struct dsp_obj *pdo, u32 address)
{
	u32 timeout = PCI_TIMEOUT;

	do {
		iowrite32(address, pdo->prHPI_address);
	} while (hpi6000_check_PCI2040_error_flag(pdo->pa_parent_adapter,
			H6WRITE)
		&& --timeout);

	if (timeout)
		return 0;

	return 1;
}

/* write one word to the HPI port */
static void hpi_write_word(struct dsp_obj *pdo, u32 address, u32 data)
{
	if (hpi_set_address(pdo, address))
		return;
	iowrite32(data, pdo->prHPI_data);
}

/* read one word from the HPI port */
static u32 hpi_read_word(struct dsp_obj *pdo, u32 address)
{
	u32 data = 0;

	if (hpi_set_address(pdo, address))
		return 0;	/*? No way to return error */

	/* take care of errata in revB DSP (2.0.1) */
	data = ioread32(pdo->prHPI_data);
	return data;
}

/* write a block of 32bit words to the DSP HPI port using auto-inc mode */
static void hpi_write_block(struct dsp_obj *pdo, u32 address, u32 *pdata,
	u32 length)
{
	u16 length16 = length - 1;

	if (length == 0)
		return;

	if (hpi_set_address(pdo, address))
		return;

	iowrite32_rep(pdo->prHPI_data_auto_inc, pdata, length16);

	/* take care of errata in revB DSP (2.0.1) */
	/* must end with non auto-inc */
	iowrite32(*(pdata + length - 1), pdo->prHPI_data);
}

/** read a block of 32bit words from the DSP HPI port using auto-inc mode
 */
static void hpi_read_block(struct dsp_obj *pdo, u32 address, u32 *pdata,
	u32 length)
{
	u16 length16 = length - 1;

	if (length == 0)
		return;

	if (hpi_set_address(pdo, address))
		return;

	ioread32_rep(pdo->prHPI_data_auto_inc, pdata, length16);

	/* take care of errata in revB DSP (2.0.1) */
	/* must end with non auto-inc */
	*(pdata + length - 1) = ioread32(pdo->prHPI_data);
}

static u16 hpi6000_dsp_block_write32(struct hpi_adapter_obj *pao,
	u16 dsp_index, u32 hpi_address, u32 *source, u32 count)
{
	struct dsp_obj *pdo =
		&(*(struct hpi_hw_obj *)pao->priv).ado[dsp_index];
	u32 time_out = PCI_TIMEOUT;
	int c6711_burst_size = 128;
	u32 local_hpi_address = hpi_address;
	int local_count = count;
	int xfer_size;
	u32 *pdata = source;

	while (local_count) {
		if (local_count > c6711_burst_size)
			xfer_size = c6711_burst_size;
		else
			xfer_size = local_count;

		time_out = PCI_TIMEOUT;
		do {
			hpi_write_block(pdo, local_hpi_address, pdata,
				xfer_size);
		} while (hpi6000_check_PCI2040_error_flag(pao, H6WRITE)
			&& --time_out);

		if (!time_out)
			break;
		pdata += xfer_size;
		local_hpi_address += sizeof(u32) * xfer_size;
		local_count -= xfer_size;
	}

	if (time_out)
		return 0;
	else
		return 1;
}

static u16 hpi6000_dsp_block_read32(struct hpi_adapter_obj *pao,
	u16 dsp_index, u32 hpi_address, u32 *dest, u32 count)
{
	struct dsp_obj *pdo =
		&(*(struct hpi_hw_obj *)pao->priv).ado[dsp_index];
	u32 time_out = PCI_TIMEOUT;
	int c6711_burst_size = 16;
	u32 local_hpi_address = hpi_address;
	int local_count = count;
	int xfer_size;
	u32 *pdata = dest;
	u32 loop_count = 0;

	while (local_count) {
		if (local_count > c6711_burst_size)
			xfer_size = c6711_burst_size;
		else
			xfer_size = local_count;

		time_out = PCI_TIMEOUT;
		do {
			hpi_read_block(pdo, local_hpi_address, pdata,
				xfer_size);
		} while (hpi6000_check_PCI2040_error_flag(pao, H6READ)
			&& --time_out);
		if (!time_out)
			break;

		pdata += xfer_size;
		local_hpi_address += sizeof(u32) * xfer_size;
		local_count -= xfer_size;
		loop_count++;
	}

	if (time_out)
		return 0;
	else
		return 1;
}

static short hpi6000_message_response_sequence(struct hpi_adapter_obj *pao,
	u16 dsp_index, struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;
	struct dsp_obj *pdo = &phw->ado[dsp_index];
	u32 timeout;
	u16 ack;
	u32 address;
	u32 length;
	u32 *p_data;
	u16 error = 0;

	ack = hpi6000_wait_dsp_ack(pao, dsp_index, HPI_HIF_IDLE);
	if (ack & HPI_HIF_ERROR_MASK) {
		pao->dsp_crashed++;
		return HPI6000_ERROR_MSG_RESP_IDLE_TIMEOUT;
	}
	pao->dsp_crashed = 0;

	/* get the message address and size */
	if (phw->message_buffer_address_on_dsp == 0) {
		timeout = TIMEOUT;
		do {
			address =
				hpi_read_word(pdo,
				HPI_HIF_ADDR(message_buffer_address));
			phw->message_buffer_address_on_dsp = address;
		} while (hpi6000_check_PCI2040_error_flag(pao, H6READ)
			&& --timeout);
		if (!timeout)
			return HPI6000_ERROR_MSG_GET_ADR;
	} else
		address = phw->message_buffer_address_on_dsp;

	length = phm->size;

	/* send the message */
	p_data = (u32 *)phm;
	if (hpi6000_dsp_block_write32(pao, dsp_index, address, p_data,
			(u16)length / 4))
		return HPI6000_ERROR_MSG_RESP_BLOCKWRITE32;

	if (hpi6000_send_host_command(pao, dsp_index, HPI_HIF_GET_RESP))
		return HPI6000_ERROR_MSG_RESP_GETRESPCMD;
	hpi6000_send_dsp_interrupt(pdo);

	ack = hpi6000_wait_dsp_ack(pao, dsp_index, HPI_HIF_GET_RESP);
	if (ack & HPI_HIF_ERROR_MASK)
		return HPI6000_ERROR_MSG_RESP_GET_RESP_ACK;

	/* get the response address */
	if (phw->response_buffer_address_on_dsp == 0) {
		timeout = TIMEOUT;
		do {
			address =
				hpi_read_word(pdo,
				HPI_HIF_ADDR(response_buffer_address));
		} while (hpi6000_check_PCI2040_error_flag(pao, H6READ)
			&& --timeout);
		phw->response_buffer_address_on_dsp = address;

		if (!timeout)
			return HPI6000_ERROR_RESP_GET_ADR;
	} else
		address = phw->response_buffer_address_on_dsp;

	/* read the length of the response back from the DSP */
	timeout = TIMEOUT;
	do {
		length = hpi_read_word(pdo, HPI_HIF_ADDR(length));
	} while (hpi6000_check_PCI2040_error_flag(pao, H6READ) && --timeout);
	if (!timeout)
		length = sizeof(struct hpi_response);

	/* get the response */
	p_data = (u32 *)phr;
	if (hpi6000_dsp_block_read32(pao, dsp_index, address, p_data,
			(u16)length / 4))
		return HPI6000_ERROR_MSG_RESP_BLOCKREAD32;

	/* set i/f back to idle */
	if (hpi6000_send_host_command(pao, dsp_index, HPI_HIF_IDLE))
		return HPI6000_ERROR_MSG_RESP_IDLECMD;
	hpi6000_send_dsp_interrupt(pdo);

	error = hpi_validate_response(phm, phr);
	return error;
}

/* have to set up the below defines to match stuff in the MAP file */

#define MSG_ADDRESS (HPI_HIF_BASE+0x18)
#define MSG_LENGTH 11
#define RESP_ADDRESS (HPI_HIF_BASE+0x44)
#define RESP_LENGTH 16
#define QUEUE_START  (HPI_HIF_BASE+0x88)
#define QUEUE_SIZE 0x8000

static short hpi6000_send_data_check_adr(u32 address, u32 length_in_dwords)
{
/*#define CHECKING       // comment this line in to enable checking */
#ifdef CHECKING
	if (address < (u32)MSG_ADDRESS)
		return 0;
	if (address > (u32)(QUEUE_START + QUEUE_SIZE))
		return 0;
	if ((address + (length_in_dwords << 2)) >
		(u32)(QUEUE_START + QUEUE_SIZE))
		return 0;
#else
	(void)address;
	(void)length_in_dwords;
	return 1;
#endif
}

static short hpi6000_send_data(struct hpi_adapter_obj *pao, u16 dsp_index,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct dsp_obj *pdo =
		&(*(struct hpi_hw_obj *)pao->priv).ado[dsp_index];
	u32 data_sent = 0;
	u16 ack;
	u32 length, address;
	u32 *p_data = (u32 *)phm->u.d.u.data.pb_data;
	u16 time_out = 8;

	(void)phr;

	/* round dwDataSize down to nearest 4 bytes */
	while ((data_sent < (phm->u.d.u.data.data_size & ~3L))
		&& --time_out) {
		ack = hpi6000_wait_dsp_ack(pao, dsp_index, HPI_HIF_IDLE);
		if (ack & HPI_HIF_ERROR_MASK)
			return HPI6000_ERROR_SEND_DATA_IDLE_TIMEOUT;

		if (hpi6000_send_host_command(pao, dsp_index,
				HPI_HIF_SEND_DATA))
			return HPI6000_ERROR_SEND_DATA_CMD;

		hpi6000_send_dsp_interrupt(pdo);

		ack = hpi6000_wait_dsp_ack(pao, dsp_index, HPI_HIF_SEND_DATA);

		if (ack & HPI_HIF_ERROR_MASK)
			return HPI6000_ERROR_SEND_DATA_ACK;

		do {
			/* get the address and size */
			address = hpi_read_word(pdo, HPI_HIF_ADDR(address));
			/* DSP returns number of DWORDS */
			length = hpi_read_word(pdo, HPI_HIF_ADDR(length));
		} while (hpi6000_check_PCI2040_error_flag(pao, H6READ));

		if (!hpi6000_send_data_check_adr(address, length))
			return HPI6000_ERROR_SEND_DATA_ADR;

		/* send the data. break data into 512 DWORD blocks (2K bytes)
		 * and send using block write. 2Kbytes is the max as this is the
		 * memory window given to the HPI data register by the PCI2040
		 */

		{
			u32 len = length;
			u32 blk_len = 512;
			while (len) {
				if (len < blk_len)
					blk_len = len;
				if (hpi6000_dsp_block_write32(pao, dsp_index,
						address, p_data, blk_len))
					return HPI6000_ERROR_SEND_DATA_WRITE;
				address += blk_len * 4;
				p_data += blk_len;
				len -= blk_len;
			}
		}

		if (hpi6000_send_host_command(pao, dsp_index, HPI_HIF_IDLE))
			return HPI6000_ERROR_SEND_DATA_IDLECMD;

		hpi6000_send_dsp_interrupt(pdo);

		data_sent += length * 4;
	}
	if (!time_out)
		return HPI6000_ERROR_SEND_DATA_TIMEOUT;
	return 0;
}

static short hpi6000_get_data(struct hpi_adapter_obj *pao, u16 dsp_index,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct dsp_obj *pdo =
		&(*(struct hpi_hw_obj *)pao->priv).ado[dsp_index];
	u32 data_got = 0;
	u16 ack;
	u32 length, address;
	u32 *p_data = (u32 *)phm->u.d.u.data.pb_data;

	(void)phr;	/* this parameter not used! */

	/* round dwDataSize down to nearest 4 bytes */
	while (data_got < (phm->u.d.u.data.data_size & ~3L)) {
		ack = hpi6000_wait_dsp_ack(pao, dsp_index, HPI_HIF_IDLE);
		if (ack & HPI_HIF_ERROR_MASK)
			return HPI6000_ERROR_GET_DATA_IDLE_TIMEOUT;

		if (hpi6000_send_host_command(pao, dsp_index,
				HPI_HIF_GET_DATA))
			return HPI6000_ERROR_GET_DATA_CMD;
		hpi6000_send_dsp_interrupt(pdo);

		ack = hpi6000_wait_dsp_ack(pao, dsp_index, HPI_HIF_GET_DATA);

		if (ack & HPI_HIF_ERROR_MASK)
			return HPI6000_ERROR_GET_DATA_ACK;

		/* get the address and size */
		do {
			address = hpi_read_word(pdo, HPI_HIF_ADDR(address));
			length = hpi_read_word(pdo, HPI_HIF_ADDR(length));
		} while (hpi6000_check_PCI2040_error_flag(pao, H6READ));

		/* read the data */
		{
			u32 len = length;
			u32 blk_len = 512;
			while (len) {
				if (len < blk_len)
					blk_len = len;
				if (hpi6000_dsp_block_read32(pao, dsp_index,
						address, p_data, blk_len))
					return HPI6000_ERROR_GET_DATA_READ;
				address += blk_len * 4;
				p_data += blk_len;
				len -= blk_len;
			}
		}

		if (hpi6000_send_host_command(pao, dsp_index, HPI_HIF_IDLE))
			return HPI6000_ERROR_GET_DATA_IDLECMD;
		hpi6000_send_dsp_interrupt(pdo);

		data_got += length * 4;
	}
	return 0;
}

static void hpi6000_send_dsp_interrupt(struct dsp_obj *pdo)
{
	iowrite32(0x00030003, pdo->prHPI_control);	/* DSPINT */
}

static short hpi6000_send_host_command(struct hpi_adapter_obj *pao,
	u16 dsp_index, u32 host_cmd)
{
	struct dsp_obj *pdo =
		&(*(struct hpi_hw_obj *)pao->priv).ado[dsp_index];
	u32 timeout = TIMEOUT;

	/* set command */
	do {
		hpi_write_word(pdo, HPI_HIF_ADDR(host_cmd), host_cmd);
		/* flush the FIFO */
		hpi_set_address(pdo, HPI_HIF_ADDR(host_cmd));
	} while (hpi6000_check_PCI2040_error_flag(pao, H6WRITE) && --timeout);

	/* reset the interrupt bit */
	iowrite32(0x00040004, pdo->prHPI_control);

	if (timeout)
		return 0;
	else
		return 1;
}

/* if the PCI2040 has recorded an HPI timeout, reset the error and return 1 */
static short hpi6000_check_PCI2040_error_flag(struct hpi_adapter_obj *pao,
	u16 read_or_write)
{
	u32 hPI_error;

	struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;

	/* read the error bits from the PCI2040 */
	hPI_error = ioread32(phw->dw2040_HPICSR + HPI_ERROR_REPORT);
	if (hPI_error) {
		/* reset the error flag */
		iowrite32(0L, phw->dw2040_HPICSR + HPI_ERROR_REPORT);
		phw->pCI2040HPI_error_count++;
		if (read_or_write == 1)
			gw_pci_read_asserts++;	   /************* inc global */
		else
			gw_pci_write_asserts++;
		return 1;
	} else
		return 0;
}

static short hpi6000_wait_dsp_ack(struct hpi_adapter_obj *pao, u16 dsp_index,
	u32 ack_value)
{
	struct dsp_obj *pdo =
		&(*(struct hpi_hw_obj *)pao->priv).ado[dsp_index];
	u32 ack = 0L;
	u32 timeout;
	u32 hPIC = 0L;

	/* wait for host interrupt to signal ack is ready */
	timeout = TIMEOUT;
	while (--timeout) {
		hPIC = ioread32(pdo->prHPI_control);
		if (hPIC & 0x04)	/* 0x04 = HINT from DSP */
			break;
	}
	if (timeout == 0)
		return HPI_HIF_ERROR_MASK;

	/* wait for dwAckValue */
	timeout = TIMEOUT;
	while (--timeout) {
		/* read the ack mailbox */
		ack = hpi_read_word(pdo, HPI_HIF_ADDR(dsp_ack));
		if (ack == ack_value)
			break;
		if ((ack & HPI_HIF_ERROR_MASK)
			&& !hpi6000_check_PCI2040_error_flag(pao, H6READ))
			break;
		/*for (i=0;i<1000;i++) */
		/*      dwPause=i+1; */
	}
	if (ack & HPI_HIF_ERROR_MASK)
		/* indicates bad read from DSP -
		   typically 0xffffff is read for some reason */
		ack = HPI_HIF_ERROR_MASK;

	if (timeout == 0)
		ack = HPI_HIF_ERROR_MASK;
	return (short)ack;
}

static short hpi6000_update_control_cache(struct hpi_adapter_obj *pao,
	struct hpi_message *phm)
{
	const u16 dsp_index = 0;
	struct hpi_hw_obj *phw = (struct hpi_hw_obj *)pao->priv;
	struct dsp_obj *pdo = &phw->ado[dsp_index];
	u32 timeout;
	u32 cache_dirty_flag;
	u16 err;

	hpios_dsplock_lock(pao);

	timeout = TIMEOUT;
	do {
		cache_dirty_flag =
			hpi_read_word((struct dsp_obj *)pdo,
			HPI_HIF_ADDR(control_cache_is_dirty));
	} while (hpi6000_check_PCI2040_error_flag(pao, H6READ) && --timeout);
	if (!timeout) {
		err = HPI6000_ERROR_CONTROL_CACHE_PARAMS;
		goto unlock;
	}

	if (cache_dirty_flag) {
		/* read the cached controls */
		u32 address;
		u32 length;

		timeout = TIMEOUT;
		if (pdo->control_cache_address_on_dsp == 0) {
			do {
				address =
					hpi_read_word((struct dsp_obj *)pdo,
					HPI_HIF_ADDR(control_cache_address));

				length = hpi_read_word((struct dsp_obj *)pdo,
					HPI_HIF_ADDR
					(control_cache_size_in_bytes));
			} while (hpi6000_check_PCI2040_error_flag(pao, H6READ)
				&& --timeout);
			if (!timeout) {
				err = HPI6000_ERROR_CONTROL_CACHE_ADDRLEN;
				goto unlock;
			}
			pdo->control_cache_address_on_dsp = address;
			pdo->control_cache_length_on_dsp = length;
		} else {
			address = pdo->control_cache_address_on_dsp;
			length = pdo->control_cache_length_on_dsp;
		}

		if (hpi6000_dsp_block_read32(pao, dsp_index, address,
				(u32 *)&phw->control_cache[0],
				length / sizeof(u32))) {
			err = HPI6000_ERROR_CONTROL_CACHE_READ;
			goto unlock;
		}
		do {
			hpi_write_word((struct dsp_obj *)pdo,
				HPI_HIF_ADDR(control_cache_is_dirty), 0);
			/* flush the FIFO */
			hpi_set_address(pdo, HPI_HIF_ADDR(host_cmd));
		} while (hpi6000_check_PCI2040_error_flag(pao, H6WRITE)
			&& --timeout);
		if (!timeout) {
			err = HPI6000_ERROR_CONTROL_CACHE_FLUSH;
			goto unlock;
		}

	}
	err = 0;

unlock:
	hpios_dsplock_unlock(pao);
	return err;
}

/** Get dsp index for multi DSP adapters only */
static u16 get_dsp_index(struct hpi_adapter_obj *pao, struct hpi_message *phm)
{
	u16 ret = 0;
	switch (phm->object) {
	case HPI_OBJ_ISTREAM:
		if (phm->obj_index < 2)
			ret = 1;
		break;
	case HPI_OBJ_PROFILE:
		ret = phm->obj_index;
		break;
	default:
		break;
	}
	return ret;
}

/** Complete transaction with DSP

Send message, get response, send or get stream data if any.
*/
static void hw_message(struct hpi_adapter_obj *pao, struct hpi_message *phm,
	struct hpi_response *phr)
{
	u16 error = 0;
	u16 dsp_index = 0;
	u16 num_dsp = ((struct hpi_hw_obj *)pao->priv)->num_dsp;

	if (num_dsp < 2)
		dsp_index = 0;
	else {
		dsp_index = get_dsp_index(pao, phm);

		/* is this  checked on the DSP anyway? */
		if ((phm->function == HPI_ISTREAM_GROUP_ADD)
			|| (phm->function == HPI_OSTREAM_GROUP_ADD)) {
			struct hpi_message hm;
			u16 add_index;
			hm.obj_index = phm->u.d.u.stream.stream_index;
			hm.object = phm->u.d.u.stream.object_type;
			add_index = get_dsp_index(pao, &hm);
			if (add_index != dsp_index) {
				phr->error = HPI_ERROR_NO_INTERDSP_GROUPS;
				return;
			}
		}
	}

	hpios_dsplock_lock(pao);
	error = hpi6000_message_response_sequence(pao, dsp_index, phm, phr);

	if (error)	/* something failed in the HPI/DSP interface */
		goto err;

	if (phr->error)	/* something failed in the DSP */
		goto out;

	switch (phm->function) {
	case HPI_OSTREAM_WRITE:
	case HPI_ISTREAM_ANC_WRITE:
		error = hpi6000_send_data(pao, dsp_index, phm, phr);
		break;
	case HPI_ISTREAM_READ:
	case HPI_OSTREAM_ANC_READ:
		error = hpi6000_get_data(pao, dsp_index, phm, phr);
		break;
	case HPI_ADAPTER_GET_ASSERT:
		phr->u.ax.assert.dsp_index = 0;	/* dsp 0 default */
		if (num_dsp == 2) {
			if (!phr->u.ax.assert.count) {
				/* no assert from dsp 0, check dsp 1 */
				error = hpi6000_message_response_sequence(pao,
					1, phm, phr);
				phr->u.ax.assert.dsp_index = 1;
			}
		}
	}

err:
	if (error) {
		if (error >= HPI_ERROR_BACKEND_BASE) {
			phr->error = HPI_ERROR_DSP_COMMUNICATION;
			phr->specific_error = error;
		} else {
			phr->error = error;
		}

		/* just the header of the response is valid */
		phr->size = sizeof(struct hpi_response_header);
	}
out:
	hpios_dsplock_unlock(pao);
	return;
}
