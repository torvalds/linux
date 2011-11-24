//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
// 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	PAL driver for AR6003
// </summary>
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HCI_PAL_H_
#define _HCI_PAL_H_
#define HCI_GET_OP_CODE(p)          (((A_UINT16)((p)[1])) << 8) | ((A_UINT16)((p)[0]))
#define TX_PACKET_RSV_OFFSET        32
/* pal specific config structure */
typedef A_BOOL (*ar6k_pal_recv_pkt_t)(void *pHciPalInfo, void *skb);
typedef struct ar6k_pal_config_s
{
	ar6k_pal_recv_pkt_t fpar6k_pal_recv_pkt;
}ar6k_pal_config_t;

/**********************************
 * HCI PAL private info structure
 *********************************/
typedef struct ar6k_hci_pal_info_s{

        unsigned long ulFlags;
#define HCI_NORMAL_MODE (1)
#define HCI_REGISTERED (1<<1)
        struct hci_dev *hdev;            /* BT Stack HCI dev */
        AR_SOFTC_DEV_T *ar;

}ar6k_hci_pal_info_t;

void register_pal_cb(ar6k_pal_config_t *palConfig_p);
#endif /* _HCI_PAL_H_ */
