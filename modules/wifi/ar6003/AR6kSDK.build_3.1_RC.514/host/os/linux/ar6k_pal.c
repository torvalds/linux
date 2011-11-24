//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

/* PAL Transport driver for AR6003 */

#include "ar6000_drv.h"
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <ar6k_pal.h>

extern unsigned int setupbtdev;
extern unsigned int loghci;

#define bt_check_bit(val, bit) (val & bit)
#define bt_set_bit(val, bit) (val |= bit)
#define bt_clear_bit(val, bit) (val &= ~bit)

/* export ATH_AR6K_DEBUG_HCI_PAL=yes in host/localmake.linux.inc
 * to enable debug information */
#ifdef HCIPAL_DEBUG
#define PRIN_LOG(format, args...) printk(KERN_ALERT "%s:%d - %s Msg:" format "\n",__FUNCTION__, __LINE__, __FILE__, ## args)
#else
#define PRIN_LOG(format, args...)
#endif


/*** BT Stack Entrypoints *******/
/***************************************
 * bt_open - open a handle to the device
 ***************************************/
static int bt_open(struct hci_dev *hdev)
{
	PRIN_LOG("HCI PAL: bt_open - enter - x\n");
	set_bit(HCI_RUNNING, &hdev->flags);
	set_bit(HCI_UP, &hdev->flags);
	set_bit(HCI_INIT, &hdev->flags);
	return 0;
}

/***************************************
 * bt_close - close handle to the device
 ***************************************/
static int bt_close(struct hci_dev *hdev)
{
	PRIN_LOG("HCI PAL: bt_close - enter\n");
	clear_bit(HCI_RUNNING, &hdev->flags);
	return 0;
}

/*****************************
 * bt_ioctl - ioctl processing
 *****************************/
static int bt_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
	PRIN_LOG("HCI PAL: bt_ioctl - enter\n");
	return -ENOIOCTLCMD;
}

/**************************************
 * bt_flush - flush outstanding packets
 **************************************/
static int bt_flush(struct hci_dev *hdev)
{
	PRIN_LOG("HCI PAL: bt_flush - enter\n");
	return 0;
}

/***************
 * bt_destruct
 ***************/
static void bt_destruct(struct hci_dev *hdev)
{
	PRIN_LOG("HCI PAL: bt_destruct - enter\n");
	/* nothing to do here */
}

/****************************************************
 * Invoked from bluetooth stack via hdev->send()
 * to send the packet out via ar6k to PAL firmware.
 *
 * For HCI command packet wmi_send_hci_cmd() is invoked.
 * wmi_send_hci_cmd adds WMI_CMD_HDR and sends the packet
 * to PAL firmware.
 *
 * For HCI ACL data packet wmi_data_hdr_add is invoked
 * to add WMI_DATA_HDR to the packet.  ar6000_acl_data_tx
 * is then invoked to send the packet to PAL firmware.
 ******************************************************/
static int btpal_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	HCI_TRANSPORT_PACKET_TYPE type;
	ar6k_hci_pal_info_t *pHciPalInfo;
	A_STATUS status = A_OK;
	struct sk_buff *txSkb = NULL;
	AR_SOFTC_DEV_T *ar;

	if (!hdev) {
		PRIN_LOG("HCI PAL: btpal_send_frame - no device\n");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		PRIN_LOG("HCI PAL: btpal_send_frame - not open\n");
		return -EBUSY;
	}

	pHciPalInfo = (ar6k_hci_pal_info_t *)hdev->driver_data;
	A_ASSERT(pHciPalInfo != NULL);
	ar = pHciPalInfo->ar;

	PRIN_LOG("+btpal_send_frame type: %d \n",bt_cb(skb)->pkt_type);
	type = HCI_COMMAND_TYPE;

	switch (bt_cb(skb)->pkt_type) {
		case HCI_COMMAND_PKT:
			type = HCI_COMMAND_TYPE;
			hdev->stat.cmd_tx++;
			break;

		case HCI_ACLDATA_PKT:
			type = HCI_ACL_TYPE;
			hdev->stat.acl_tx++;
			break;

		case HCI_SCODATA_PKT:
			/* we don't support SCO over the pal */
			kfree_skb(skb);
			return 0;
		default:
			A_ASSERT(FALSE);
			kfree_skb(skb);
			return 0;
	}

    	if(loghci) {
		A_PRINTF(">>> Send HCI %s packet len: %d\n",
				(type == HCI_COMMAND_TYPE) ? "COMMAND" : "ACL",
				skb->len);
		if (type == HCI_COMMAND_TYPE) {
			A_PRINTF("HCI Command: OGF:0x%X OCF:0x%X \r\n",
				(HCI_GET_OP_CODE(skb->data)) >> 10, 
				(HCI_GET_OP_CODE(skb->data)) & 0x3FF);
		}
		DebugDumpBytes(skb->data,skb->len,"BT HCI SEND Packet Dump");
	}

	do {
		if(type == HCI_COMMAND_TYPE)
		{
			PRIN_LOG("HCI command");

			if (ar->arSoftc->arWmiReady == FALSE)
			{
				PRIN_LOG("WMI not ready ");
				break;
			}

			if (wmi_send_hci_cmd(ar->arWmi, skb->data, skb->len) != A_OK)
			{
				PRIN_LOG("send hci cmd error");
				break;
			}
		}
		else if(type == HCI_ACL_TYPE)
		{
			void *osbuf;

			PRIN_LOG("ACL data");
			if (ar->arSoftc->arWmiReady == FALSE)
			{
				PRIN_LOG("WMI not ready");
				break;
			}

			/* need to add WMI header so allocate a skb with more space */
			txSkb = bt_skb_alloc(TX_PACKET_RSV_OFFSET + WMI_MAX_TX_META_SZ +
					sizeof(WMI_DATA_HDR) + skb->len,
					GFP_ATOMIC);

			if (txSkb == NULL) {
				status = A_NO_MEMORY;
				PRIN_LOG("No memory");
				break;
			}

			bt_cb(txSkb)->pkt_type = bt_cb(skb)->pkt_type;
			txSkb->dev = (void *)pHciPalInfo->hdev;
			skb_reserve(txSkb, TX_PACKET_RSV_OFFSET + WMI_MAX_TX_META_SZ + sizeof(WMI_DATA_HDR));
			A_MEMCPY(txSkb->data, skb->data, skb->len);
			skb_put(txSkb,skb->len);
			/* Add WMI packet type */
			osbuf = (void *)txSkb;

			PRIN_LOG("\nAdd WMI header");
			if (wmi_data_hdr_add(ar->arWmi, osbuf, DATA_MSGTYPE, 0, WMI_DATA_HDR_DATA_TYPE_ACL,0,NULL) != A_OK) {
				PRIN_LOG("XIOCTL_ACL_DATA - wmi_data_hdr_add failed\n");
			} else {
				/* Send data buffer over HTC */
				PRIN_LOG("acl data tx");
				ar6000_acl_data_tx(osbuf, ar);
			}
			txSkb = NULL;
		}
	} while (FALSE);

	if (txSkb != NULL) {
		PRIN_LOG("Free skb");
		kfree_skb(txSkb);
	}
	kfree_skb(skb);
	return 0;
}


/***********************************************
 * Unregister HCI device and free HCI device info
 ***********************************************/
static void bt_cleanup_hci_pal(ar6k_hci_pal_info_t *pHciPalInfo)
{
	int err;

	if (bt_check_bit(pHciPalInfo->ulFlags, HCI_REGISTERED)) {
		bt_clear_bit(pHciPalInfo->ulFlags, HCI_REGISTERED);
		clear_bit(HCI_RUNNING, &pHciPalInfo->hdev->flags);
		clear_bit(HCI_UP, &pHciPalInfo->hdev->flags);
		clear_bit(HCI_INIT, &pHciPalInfo->hdev->flags);
		A_ASSERT(pHciPalInfo->hdev != NULL);
#ifdef CONFIG_BT
		/* unregister */
		PRIN_LOG("Unregister PAL device");
		if ((err = hci_unregister_dev(pHciPalInfo->hdev)) < 0) {
			PRIN_LOG("HCI PAL: failed to unregister with bluetooth %d\n",err);
		}
#else
		(void)&err;
#endif
	}

	if (pHciPalInfo->hdev != NULL) {
		kfree(pHciPalInfo->hdev);
		pHciPalInfo->hdev = NULL;
	}
}

/*********************************************************
 * Allocate HCI device and store in PAL private info structure.
 *********************************************************/
static A_STATUS bt_setup_hci_pal(ar6k_hci_pal_info_t *pHciPalInfo)
{
#ifdef CONFIG_BT
	A_STATUS status = A_OK;
	struct hci_dev *pHciDev = NULL;

#if 0 //This condition is not required for PAL
	if (!setupbtdev) {
		return A_OK;
	}
#endif
	do {
		/* allocate a BT HCI struct for this device */
		pHciDev = hci_alloc_dev();
		if (NULL == pHciDev) {
			PRIN_LOG("HCI PAL driver - failed to allocate BT HCI struct \n");
			status = A_NO_MEMORY;
			break;
		}

		/* save the device, we'll register this later */
		pHciPalInfo->hdev = pHciDev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
        SET_HCI_BUS_TYPE(pHciDev, HCI_VIRTUAL, HCI_80211);
#else
        SET_HCI_BUS_TYPE(pHciDev, HCI_VIRTUAL, HCI_AMP);
#endif
		pHciDev->driver_data = pHciPalInfo;
		pHciDev->open     = bt_open;
		pHciDev->close    = bt_close;
		pHciDev->send     = btpal_send_frame;
		pHciDev->ioctl    = bt_ioctl;
		pHciDev->flush    = bt_flush;
		pHciDev->destruct = bt_destruct;
		pHciDev->owner = THIS_MODULE;
		/* driver is running in normal BT mode */
		PRIN_LOG("Normal mode enabled");
		bt_set_bit(pHciPalInfo->ulFlags, HCI_NORMAL_MODE);

	} while (FALSE);

	if (A_FAILED(status)) {
		bt_cleanup_hci_pal(pHciPalInfo);
	}
	return status;
#else
	(void)&bt_open;
	(void)&bt_close;
	(void)&btpal_send_frame;
	(void)&bt_ioctl;
	(void)&bt_flush;
	(void)&bt_destruct;
	bt_cleanup_hci_pal(pHciPalInfo);
	return A_ENOTSUP;
#endif
}

/**********************************************
 * Cleanup HCI device and free HCI PAL private info
 *********************************************/
void ar6k_cleanup_hci_pal(void *ar_p)
{
	AR_SOFTC_DEV_T *ar = (AR_SOFTC_DEV_T *)ar_p;
	ar6k_hci_pal_info_t *pHciPalInfo = (ar6k_hci_pal_info_t *)ar->hcipal_info;

	if (pHciPalInfo != NULL) {
		bt_cleanup_hci_pal(pHciPalInfo);
		A_FREE(pHciPalInfo);
		ar->hcipal_info = NULL;
	}
}

/****************************
 *  Register HCI device
 ****************************/
static A_BOOL ar6k_pal_transport_ready(void *pHciPal)
{
#ifdef CONFIG_BT
	ar6k_hci_pal_info_t *pHciPalInfo = (ar6k_hci_pal_info_t *)pHciPal;

	PRIN_LOG("HCI device transport ready");
	if(pHciPalInfo == NULL)
		return FALSE;

	if (hci_register_dev(pHciPalInfo->hdev) < 0) {
		PRIN_LOG("Can't register HCI device");
		hci_free_dev(pHciPalInfo->hdev);
		return FALSE;
	}
	PRIN_LOG("HCI device registered");
	pHciPalInfo->ulFlags |= HCI_REGISTERED;
	return TRUE;
#else
	return FALSE;
#endif
}

/**************************************************
 * Called from ar6k driver when command or ACL data
 * packet is received. Pass the packet to bluetooth
 * stack via hci_recv_frame.
 **************************************************/
A_BOOL ar6k_pal_recv_pkt(void *pHciPal, void *osbuf)
{
	struct sk_buff *skb = (struct sk_buff *)osbuf;
	ar6k_hci_pal_info_t *pHciPalInfo;
	A_BOOL success = FALSE;
	A_UINT8 btType = 0;
	pHciPalInfo = (ar6k_hci_pal_info_t *)pHciPal;

	do {

		/* if normal mode is not enabled pass on to the stack
		 * by returning failure */
		if(!(pHciPalInfo->ulFlags & HCI_NORMAL_MODE))
		{
			PRIN_LOG("Normal mode not enabled");
			break;
		}

		if (!test_bit(HCI_RUNNING, &pHciPalInfo->hdev->flags)) {
			PRIN_LOG("HCI PAL: HCI - not running\n");
			break;
		}

		if(*((short *)A_NETBUF_DATA(skb)) == WMI_ACL_DATA_EVENTID)
			btType = HCI_ACLDATA_PKT;
		else
			btType = HCI_EVENT_PKT;
		/* pull 4 bytes which contains WMI packet type */
		A_NETBUF_PULL(skb, sizeof(int));
		bt_cb(skb)->pkt_type = btType;
		skb->dev = (void *)pHciPalInfo->hdev;

    		if(loghci) {
			AR_DEBUG_PRINTF(ATH_DEBUG_ANY,("<<< Recv HCI %s packet len:%d \n",
						(btType == HCI_EVENT_TYPE) ? "EVENT" : "ACL",
						skb->len));
			DebugDumpBytes(skb->data, skb->len,"BT HCI RECV Packet Dump");
		}
		/* pass the received event packet up the stack */
#ifdef CONFIG_BT
		if (hci_recv_frame(skb) != 0) {
#else
		if (1) {
#endif
			PRIN_LOG("HCI PAL: hci_recv_frame failed \n");
			break;
		} else {
			PRIN_LOG("HCI PAL: Indicated RCV of type:%d, Length:%d \n",HCI_EVENT_PKT, skb->len);
		}
		PRIN_LOG("hci recv success");
		success = TRUE;
	}while(FALSE);
	return success;
}

/**********************************************************
 * HCI PAL init function called from ar6k when it is loaded..
 * Allocates PAL private info, stores the same in ar6k private info.
 * Registers a HCI device.
 * Registers packet receive callback function with ar6k
 **********************************************************/
A_STATUS ar6k_setup_hci_pal(void *ar_p)
{
	A_STATUS status = A_OK;
	ar6k_hci_pal_info_t *pHciPalInfo;
	ar6k_pal_config_t ar6k_pal_config;
	AR_SOFTC_DEV_T *ar = (AR_SOFTC_DEV_T *)ar_p;
	
	do {

		pHciPalInfo = (ar6k_hci_pal_info_t *)A_MALLOC(sizeof(ar6k_hci_pal_info_t));

		if (NULL == pHciPalInfo) {
			status = A_NO_MEMORY;
			break;
		}

		A_MEMZERO(pHciPalInfo, sizeof(ar6k_hci_pal_info_t));
		ar->hcipal_info = pHciPalInfo;
		pHciPalInfo->ar = ar;

		status = bt_setup_hci_pal(pHciPalInfo);
		if (A_FAILED(status)) {
			break;
		}

		if(bt_check_bit(pHciPalInfo->ulFlags, HCI_NORMAL_MODE))
			PRIN_LOG("HCI PAL: running in normal mode... \n");
		else
			PRIN_LOG("HCI PAL: running in test mode... \n");

		ar6k_pal_config.fpar6k_pal_recv_pkt = ar6k_pal_recv_pkt;
		register_pal_cb(&ar6k_pal_config);
		ar6k_pal_transport_ready(ar->hcipal_info);
	} while (FALSE);

	if (A_FAILED(status)) {
		ar6k_cleanup_hci_pal(ar);
	}
	return status;
}
