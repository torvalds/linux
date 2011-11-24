//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
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
//------------------------------------------------------------------------------
//==============================================================================
// HCI bridge implementation
//
// Author(s): ="Atheros"
//==============================================================================

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
#include <linux/etherdevice.h>
#include <a_config.h>
#include <athdefs.h>
#include "a_types.h"
#include "a_osapi.h"
#include "htc_api.h"
#include "wmi.h"
#include "a_drv.h"
#include "hif.h"
#include "common_drv.h"
#include "a_debug.h"
#define  ATH_DEBUG_HCI_BRIDGE    ATH_DEBUG_MAKE_MODULE_MASK(6)
#define  ATH_DEBUG_HCI_RECV      ATH_DEBUG_MAKE_MODULE_MASK(7)
#define  ATH_DEBUG_HCI_SEND      ATH_DEBUG_MAKE_MODULE_MASK(8)
#define  ATH_DEBUG_HCI_DUMP      ATH_DEBUG_MAKE_MODULE_MASK(9)
#else
#include "ar6000_drv.h"
#endif  /* EXPORT_HCI_BRIDGE_INTERFACE */

#ifdef ATH_AR6K_ENABLE_GMBOX
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
#include "export_hci_transport.h"
#else
#include "hci_transport_api.h"
#endif
#include "epping_test.h"
#include "gmboxif.h"
#include "ar3kconfig.h"
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
    /* only build on newer kernels which have BT configured */
#if defined(CONFIG_BT_MODULE) || defined(CONFIG_BT)
#define CONFIG_BLUEZ_HCI_BRIDGE  
#endif
#endif

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
unsigned int ar3khcibaud = 0;
unsigned int hciuartscale = 0;
unsigned int hciuartstep = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param(ar3khcibaud, int, 0644);
module_param(hciuartscale, int, 0644);
module_param(hciuartstep, int, 0644);
#else

#define __user
/* for linux 2.4 and lower */
MODULE_PARM(ar3khcibaud, "i");
MODULE_PARM(hciuartscale, "i");
MODULE_PARM(hciuartstep, "i");
#endif
#else
extern unsigned int ar3khcibaud;
extern unsigned int hciuartscale;
extern unsigned int hciuartstep;
#endif /* EXPORT_HCI_BRIDGE_INTERFACE */

typedef struct {
    void                    *pHCIDev;          /* HCI bridge device */
    HCI_TRANSPORT_PROPERTIES HCIProps;         /* HCI bridge props */
    struct hci_dev          *pBtStackHCIDev;   /* BT Stack HCI dev */
    A_BOOL                  HciNormalMode;     /* Actual HCI mode enabled (non-TEST)*/
    A_BOOL                  HciRegistered;     /* HCI device registered with stack */
    HTC_PACKET_QUEUE        HTCPacketStructHead;
    A_UINT8                 *pHTCStructAlloc;
    spinlock_t              BridgeLock;
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    HCI_TRANSPORT_MISC_HANDLES    HCITransHdl; 
#else
    AR_SOFTC_T              *ar;
#endif /* EXPORT_HCI_BRIDGE_INTERFACE */
} AR6K_HCI_BRIDGE_INFO;

#define MAX_ACL_RECV_BUFS           16
#define MAX_EVT_RECV_BUFS           8
#define MAX_HCI_WRITE_QUEUE_DEPTH   32
#define MAX_ACL_RECV_LENGTH         1200
#define MAX_EVT_RECV_LENGTH         257
#define TX_PACKET_RSV_OFFSET        32
#define NUM_HTC_PACKET_STRUCTS     ((MAX_ACL_RECV_BUFS + MAX_EVT_RECV_BUFS + MAX_HCI_WRITE_QUEUE_DEPTH) * 2)

#define HCI_GET_OP_CODE(p)          (((A_UINT16)((p)[1])) << 8) | ((A_UINT16)((p)[0]))

extern unsigned int setupbtdev;
AR3K_CONFIG_INFO      ar3kconfig;

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
AR6K_HCI_BRIDGE_INFO *g_pHcidevInfo;
#endif

static A_STATUS bt_setup_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo);
static void     bt_cleanup_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo);
static A_STATUS bt_register_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo);
static A_BOOL   bt_indicate_recv(AR6K_HCI_BRIDGE_INFO      *pHcidevInfo, 
                                 HCI_TRANSPORT_PACKET_TYPE Type, 
                                 struct sk_buff            *skb);
static struct sk_buff *bt_alloc_buffer(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, int Length);
static void     bt_free_buffer(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, struct sk_buff *skb);   
                               
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
A_STATUS ar6000_setup_hci(void *ar);
void     ar6000_cleanup_hci(void *ar);
A_STATUS hci_test_send(void *ar, struct sk_buff *skb);
#else
A_STATUS ar6000_setup_hci(AR_SOFTC_T *ar);
void     ar6000_cleanup_hci(AR_SOFTC_T *ar);
/* HCI bridge testing */
A_STATUS hci_test_send(AR_SOFTC_T *ar, struct sk_buff *skb);
#endif /* EXPORT_HCI_BRIDGE_INTERFACE */

#define LOCK_BRIDGE(dev)   spin_lock_bh(&(dev)->BridgeLock)
#define UNLOCK_BRIDGE(dev) spin_unlock_bh(&(dev)->BridgeLock)

static inline void FreeBtOsBuf(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, void *osbuf)
{    
    if (pHcidevInfo->HciNormalMode) {
        bt_free_buffer(pHcidevInfo, (struct sk_buff *)osbuf);
    } else {
            /* in test mode, these are just ordinary netbuf allocations */
        A_NETBUF_FREE(osbuf);
    }
}

static void FreeHTCStruct(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, HTC_PACKET *pPacket)
{
    LOCK_BRIDGE(pHcidevInfo);
    HTC_PACKET_ENQUEUE(&pHcidevInfo->HTCPacketStructHead,pPacket);
    UNLOCK_BRIDGE(pHcidevInfo);  
}

static HTC_PACKET * AllocHTCStruct(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{
    HTC_PACKET  *pPacket = NULL;
    LOCK_BRIDGE(pHcidevInfo);
    pPacket = HTC_PACKET_DEQUEUE(&pHcidevInfo->HTCPacketStructHead);
    UNLOCK_BRIDGE(pHcidevInfo);  
    return pPacket;
}

#define BLOCK_ROUND_UP_PWR2(x, align)    (((int) (x) + ((align)-1)) & ~((align)-1))

static void RefillRecvBuffers(AR6K_HCI_BRIDGE_INFO      *pHcidevInfo, 
                              HCI_TRANSPORT_PACKET_TYPE Type, 
                              int                       NumBuffers)
{
    int                 length, i;
    void                *osBuf = NULL;
    HTC_PACKET_QUEUE    queue;
    HTC_PACKET          *pPacket;

    INIT_HTC_PACKET_QUEUE(&queue);
    
    if (Type == HCI_ACL_TYPE) {     
        if (pHcidevInfo->HciNormalMode) {  
            length = HCI_MAX_FRAME_SIZE;
        } else {
            length = MAX_ACL_RECV_LENGTH;    
        }
    } else {
        length = MAX_EVT_RECV_LENGTH;
    }
    
        /* add on transport head and tail room */ 
    length += pHcidevInfo->HCIProps.HeadRoom + pHcidevInfo->HCIProps.TailRoom;
        /* round up to the required I/O padding */      
    length = BLOCK_ROUND_UP_PWR2(length,pHcidevInfo->HCIProps.IOBlockPad);
             
    for (i = 0; i < NumBuffers; i++) {   
           
        if (pHcidevInfo->HciNormalMode) {   
            osBuf = bt_alloc_buffer(pHcidevInfo,length);       
        } else {
            osBuf = A_NETBUF_ALLOC(length);  
        }
          
        if (NULL == osBuf) {
            break;    
        }            
         
        pPacket = AllocHTCStruct(pHcidevInfo);
        if (NULL == pPacket) {
            FreeBtOsBuf(pHcidevInfo,osBuf);
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to alloc HTC struct \n"));
            break;    
        }     
        
        SET_HTC_PACKET_INFO_RX_REFILL(pPacket,osBuf,A_NETBUF_DATA(osBuf),length,Type);
            /* add to queue */
        HTC_PACKET_ENQUEUE(&queue,pPacket);
    }
    
    if (i > 0) {
        HCI_TransportAddReceivePkts(pHcidevInfo->pHCIDev, &queue);    
    }
}

static A_STATUS ar6000_hci_transport_ready(HCI_TRANSPORT_HANDLE     HCIHandle, 
                                           HCI_TRANSPORT_PROPERTIES *pProps, 
                                           void                     *pContext)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    A_STATUS              status;
    AR_SOFTC_DEV_T *arDev = pHcidevInfo->ar->arDev[0];

    
    pHcidevInfo->pHCIDev = HCIHandle;
    
    A_MEMCPY(&pHcidevInfo->HCIProps,pProps,sizeof(*pProps));
    
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE,("HCI ready (hci:0x%lX, headroom:%d, tailroom:%d blockpad:%d) \n", 
            (unsigned long)HCIHandle, 
            pHcidevInfo->HCIProps.HeadRoom, 
            pHcidevInfo->HCIProps.TailRoom,
            pHcidevInfo->HCIProps.IOBlockPad));
    
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    A_ASSERT((pProps->HeadRoom + pProps->TailRoom) <= (struct net_device *)(pHcidevInfo->HCITransHdl.netDevice)->hard_header_len);
#else
    A_ASSERT((pProps->HeadRoom + pProps->TailRoom) <= arDev->arNetDev->hard_header_len);
#endif
                             
        /* provide buffers */
    RefillRecvBuffers(pHcidevInfo, HCI_ACL_TYPE, MAX_ACL_RECV_BUFS);
    RefillRecvBuffers(pHcidevInfo, HCI_EVENT_TYPE, MAX_EVT_RECV_BUFS);
   
    do {
            /* start transport */
        status = HCI_TransportStart(pHcidevInfo->pHCIDev);
         
        if (A_FAILED(status)) {
            break;    
        }
        
        if (!pHcidevInfo->HciNormalMode) {
                /* in test mode, no need to go any further */
            break;    
        }

        // The delay is required when AR6K is driving the BT reset line
        // where time is needed after the BT chip is out of reset (HCI_TransportStart)
        // and before the first HCI command is issued (AR3KConfigure)
        // FIXME
        // The delay should be configurable and be only applied when AR6K driving the BT
        // reset line. This could be done by some module parameter or based on some HW config
        // info. For now apply 100ms delay blindly
        A_MDELAY(100);
        
        A_MEMZERO(&ar3kconfig,sizeof(ar3kconfig));
        ar3kconfig.pHCIDev = pHcidevInfo->pHCIDev;
        ar3kconfig.pHCIProps = &pHcidevInfo->HCIProps;
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        ar3kconfig.pHIFDevice = (HIF_DEVICE *)(pHcidevInfo->HCITransHdl.hifDevice);
#else
        ar3kconfig.pHIFDevice = pHcidevInfo->ar->arHifDevice;
#endif
        ar3kconfig.pBtStackHCIDev = pHcidevInfo->pBtStackHCIDev;
        
        if (ar3khcibaud != 0) {
                /* user wants ar3k baud rate change */
            ar3kconfig.Flags |= AR3K_CONFIG_FLAG_SET_AR3K_BAUD;
            ar3kconfig.Flags |= AR3K_CONFIG_FLAG_AR3K_BAUD_CHANGE_DELAY;
            ar3kconfig.AR3KBaudRate = ar3khcibaud;    
        }
        
        if ((hciuartscale != 0) || (hciuartstep != 0)) {   
                /* user wants to tune HCI bridge UART scale/step values */
            ar3kconfig.AR6KScale = (A_UINT16)hciuartscale;
            ar3kconfig.AR6KStep = (A_UINT16)hciuartstep;           
            ar3kconfig.Flags |= AR3K_CONFIG_FLAG_SET_AR6K_SCALE_STEP;
        }
        
            /* configure the AR3K device */  
		memcpy(ar3kconfig.bdaddr,pHcidevInfo->ar->bdaddr,6);       
        status = AR3KConfigure(&ar3kconfig);
        if (A_FAILED(status)) {
            extern unsigned int setuphci;
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: Fail to configure AR3K. No device? Cleanup HCI\n"));
            pHcidevInfo->ar->exitCallback = NULL;
            ar6000_cleanup_hci(pHcidevInfo->ar);
            setuphci = 0;
            pHcidevInfo->ar->arBTSharing = 0;
            break; 
        }

        /* Make sure both AR6K and AR3K have power management enabled */
        if (ar3kconfig.PwrMgmtEnabled) {
            A_UINT32 address, hci_uart_pwr_mgmt_params;
            /* Fetch the address of the hi_hci_uart_pwr_mgmt_params instance in the host interest area */
            address = TARG_VTOP(pHcidevInfo->ar->arTargetType, 
                                HOST_INTEREST_ITEM_ADDRESS(pHcidevInfo->ar->arTargetType, hi_hci_uart_pwr_mgmt_params));
            status = ar6000_ReadRegDiag(pHcidevInfo->ar->arHifDevice, &address, &hci_uart_pwr_mgmt_params);
            hci_uart_pwr_mgmt_params &= 0xFFFF;
            /* wakeup timeout is [31:16] */
            hci_uart_pwr_mgmt_params |= (ar3kconfig.WakeupTimeout << 16);
            status |= ar6000_WriteRegDiag(pHcidevInfo->ar->arHifDevice, &address, &hci_uart_pwr_mgmt_params);
            if (A_OK != status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to write hci_uart_pwr_mgmt_params!\n"));
            }

            /* Fetch the address of the hi_hci_uart_pwr_mgmt_params_ext instance in the host interest area */
            address = TARG_VTOP(pHcidevInfo->ar->arTargetType, 
                                HOST_INTEREST_ITEM_ADDRESS(pHcidevInfo->ar->arTargetType, hi_hci_uart_pwr_mgmt_params_ext));
            status = ar6000_WriteRegDiag(pHcidevInfo->ar->arHifDevice, &address, &ar3kconfig.IdleTimeout);
            if (A_OK != status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to write hci_uart_pwr_mgmt_params_ext!\n"));
            }
        
            status = HCI_TransportEnablePowerMgmt(pHcidevInfo->pHCIDev, TRUE);
            if (A_FAILED(status)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to enable TLPM for AR6K! \n"));
            }
        }
        
        status = bt_register_hci(pHcidevInfo);
        
    } while (FALSE);

    return status; 
}

static void ar6000_hci_transport_failure(void *pContext, A_STATUS Status)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: transport failure! \n"));
    
    if (pHcidevInfo->HciNormalMode) {
        /* TODO .. */    
    }
}

static void ar6000_hci_transport_removed(void *pContext)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: transport removed. \n"));
    
    A_ASSERT(pHcidevInfo->pHCIDev != NULL);
        
    HCI_TransportDetach(pHcidevInfo->pHCIDev);
    bt_cleanup_hci(pHcidevInfo);
    pHcidevInfo->pHCIDev = NULL;
}

static void ar6000_hci_send_complete(void *pContext, HTC_PACKET *pPacket)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    void                 *osbuf = pPacket->pPktContext;
    A_ASSERT(osbuf != NULL);
    A_ASSERT(pHcidevInfo != NULL);
    
    if (A_FAILED(pPacket->Status)) {
        if ((pPacket->Status != A_ECANCELED) && (pPacket->Status != A_NO_RESOURCE)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: Send Packet Failed: %d \n",pPacket->Status)); 
        }   
    }
            
    FreeHTCStruct(pHcidevInfo,pPacket);    
    FreeBtOsBuf(pHcidevInfo,osbuf);
    
}

static void ar6000_hci_pkt_recv(void *pContext, HTC_PACKET *pPacket)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    struct sk_buff       *skb;
    AR_SOFTC_DEV_T *arDev = pHcidevInfo->ar->arDev[0];
    
    A_ASSERT(pHcidevInfo != NULL);
    skb = (struct sk_buff *)pPacket->pPktContext;
    A_ASSERT(skb != NULL);
          
    do {
        
        if (A_FAILED(pPacket->Status)) {
            break;
        }
  
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_RECV, 
                        ("HCI Bridge, packet received type : %d len:%d \n",
                        HCI_GET_PACKET_TYPE(pPacket),pPacket->ActualLength));
    
            /* set the actual buffer position in the os buffer, HTC recv buffers posted to HCI are set
             * to fill the front of the buffer */
        A_NETBUF_PUT(skb,pPacket->ActualLength + pHcidevInfo->HCIProps.HeadRoom);
        A_NETBUF_PULL(skb,pHcidevInfo->HCIProps.HeadRoom);
        
        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_HCI_DUMP)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,("<<< Recv HCI %s packet len:%d \n",
                        (HCI_GET_PACKET_TYPE(pPacket) == HCI_EVENT_TYPE) ? "EVENT" : "ACL",
                        skb->len));
            AR_DEBUG_PRINTBUF(skb->data, skb->len,"BT HCI RECV Packet Dump");
        }
        
        if (pHcidevInfo->HciNormalMode) {
                /* indicate the packet */         
            if (bt_indicate_recv(pHcidevInfo,HCI_GET_PACKET_TYPE(pPacket),skb)) {
                    /* bt stack accepted the packet */
                skb = NULL;
            }  
            break;
        }
        
            /* for testing, indicate packet to the network stack */ 
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        skb->dev = (struct net_device *)(pHcidevInfo->HCITransHdl.netDevice);        
        if ((((struct net_device *)pHcidevInfo->HCITransHdl.netDevice)->flags & IFF_UP) == IFF_UP) {
            skb->protocol = eth_type_trans(skb, (struct net_device *)(pHcidevInfo->HCITransHdl.netDevice));
#else
        skb->dev = arDev->arNetDev;        
        if ((arDev->arNetDev->flags & IFF_UP) == IFF_UP) {
            skb->protocol = eth_type_trans(skb, arDev->arNetDev);
#endif
            A_NETIF_RX(skb);
            skb = NULL;
        } 
        
    } while (FALSE);
    
    FreeHTCStruct(pHcidevInfo,pPacket);
    
    if (skb != NULL) {
            /* packet was not accepted, free it */
        FreeBtOsBuf(pHcidevInfo,skb);       
    }
    
}

static void  ar6000_hci_pkt_refill(void *pContext, HCI_TRANSPORT_PACKET_TYPE Type, int BuffersAvailable)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    int                  refillCount;

    if (Type == HCI_ACL_TYPE) {
        refillCount =  MAX_ACL_RECV_BUFS - BuffersAvailable;   
    } else {
        refillCount =  MAX_EVT_RECV_BUFS - BuffersAvailable;     
    }
    
    if (refillCount > 0) {
        RefillRecvBuffers(pHcidevInfo,Type,refillCount);
    }
    
}

static HCI_SEND_FULL_ACTION  ar6000_hci_pkt_send_full(void *pContext, HTC_PACKET *pPacket)
{
    AR6K_HCI_BRIDGE_INFO    *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)pContext;
    HCI_SEND_FULL_ACTION    action = HCI_SEND_FULL_KEEP;
    
    if (!pHcidevInfo->HciNormalMode) {
            /* for epping testing, check packet tag, some epping packets are
             * special and cannot be dropped */
        if (HTC_GET_TAG_FROM_PKT(pPacket) == AR6K_DATA_PKT_TAG) {
            action = HCI_SEND_FULL_DROP;     
        }
    }
    
    return action;
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
A_STATUS ar6000_setup_hci(void *ar)
#else
A_STATUS ar6000_setup_hci(AR_SOFTC_T *ar)
#endif
{
    HCI_TRANSPORT_CONFIG_INFO config;
    A_STATUS                  status = A_OK;
    int                       i;
    HTC_PACKET                *pPacket;
    AR6K_HCI_BRIDGE_INFO      *pHcidevInfo;
        
       
    do {
        
        pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)A_MALLOC(sizeof(AR6K_HCI_BRIDGE_INFO));
        
        if (NULL == pHcidevInfo) {
            status = A_NO_MEMORY;
            break;    
        }
        
        A_MEMZERO(pHcidevInfo, sizeof(AR6K_HCI_BRIDGE_INFO));
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        g_pHcidevInfo = pHcidevInfo;
        pHcidevInfo->HCITransHdl = *(HCI_TRANSPORT_MISC_HANDLES *)ar;
#else
        ar->hcidev_info = pHcidevInfo;
        pHcidevInfo->ar = ar;
#endif
        spin_lock_init(&pHcidevInfo->BridgeLock);
        INIT_HTC_PACKET_QUEUE(&pHcidevInfo->HTCPacketStructHead);

        ar->exitCallback = AR3KConfigureExit;
    
        status = bt_setup_hci(pHcidevInfo);
        if (A_FAILED(status)) {
            break;    
        }
        
        if (pHcidevInfo->HciNormalMode) {      
            AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: running in normal mode... \n"));    
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: running in test mode... \n"));     
        }
        
        pHcidevInfo->pHTCStructAlloc = (A_UINT8 *)A_MALLOC((sizeof(HTC_PACKET)) * NUM_HTC_PACKET_STRUCTS);
        
        if (NULL == pHcidevInfo->pHTCStructAlloc) {
            status = A_NO_MEMORY;
            break;    
        }
        
        pPacket = (HTC_PACKET *)pHcidevInfo->pHTCStructAlloc;
        for (i = 0; i < NUM_HTC_PACKET_STRUCTS; i++,pPacket++) {
            FreeHTCStruct(pHcidevInfo,pPacket);                
        }
        
        A_MEMZERO(&config,sizeof(HCI_TRANSPORT_CONFIG_INFO));        
        config.ACLRecvBufferWaterMark = MAX_ACL_RECV_BUFS / 2;
        config.EventRecvBufferWaterMark = MAX_EVT_RECV_BUFS / 2;
        config.MaxSendQueueDepth = MAX_HCI_WRITE_QUEUE_DEPTH;
        config.pContext = pHcidevInfo;    
        config.TransportFailure = ar6000_hci_transport_failure;
        config.TransportReady = ar6000_hci_transport_ready;
        config.TransportRemoved = ar6000_hci_transport_removed;
        config.pHCISendComplete = ar6000_hci_send_complete;
        config.pHCIPktRecv = ar6000_hci_pkt_recv;
        config.pHCIPktRecvRefill = ar6000_hci_pkt_refill;
        config.pHCISendFull = ar6000_hci_pkt_send_full;
       
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        pHcidevInfo->pHCIDev = HCI_TransportAttach(pHcidevInfo->HCITransHdl.htcHandle, &config);
#else
        pHcidevInfo->pHCIDev = HCI_TransportAttach(ar->arHtcTarget, &config);
#endif

        if (NULL == pHcidevInfo->pHCIDev) {
            status = A_ERROR;      
        }
    
    } while (FALSE);
    
    if (A_FAILED(status)) {
        if (pHcidevInfo != NULL) {
            if (NULL == pHcidevInfo->pHCIDev) {
                /* GMBOX may not be present in older chips */
                /* just return success */ 
                status = A_OK;
            }
        }
        ar6000_cleanup_hci(ar);    
    }
    
    return status;
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
void  ar6000_cleanup_hci(void *ar)
#else
void  ar6000_cleanup_hci(AR_SOFTC_T *ar)
#endif
{
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = g_pHcidevInfo;
#else
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)ar->hcidev_info;
#endif
    
    if (pHcidevInfo != NULL) {
        bt_cleanup_hci(pHcidevInfo);   
        
        if (pHcidevInfo->pHCIDev != NULL) {
            HCI_TransportStop(pHcidevInfo->pHCIDev);
            HCI_TransportDetach(pHcidevInfo->pHCIDev);
            pHcidevInfo->pHCIDev = NULL;
        } 
        
        if (pHcidevInfo->pHTCStructAlloc != NULL) {
            A_FREE(pHcidevInfo->pHTCStructAlloc);
            pHcidevInfo->pHTCStructAlloc = NULL;    
        }
        
        A_FREE(pHcidevInfo);
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
        ar->hcidev_info = NULL;
        ar->exitCallback = NULL;
#endif
    }
    
    
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
A_STATUS hci_test_send(void *ar, struct sk_buff *skb)
#else
A_STATUS hci_test_send(AR_SOFTC_T *ar, struct sk_buff *skb)
#endif
{
    int              status = A_OK;
    int              length;
    EPPING_HEADER    *pHeader;
    HTC_PACKET       *pPacket;   
    HTC_TX_TAG       htc_tag = AR6K_DATA_PKT_TAG;
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = g_pHcidevInfo;
#else
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)ar->hcidev_info;
#endif
            
    do {
        
        if (NULL == pHcidevInfo) {
            status = A_ERROR;
            break;    
        }
        
        if (NULL == pHcidevInfo->pHCIDev) {
            status = A_ERROR;
            break;    
        }
        
        if (pHcidevInfo->HciNormalMode) {
                /* this interface cannot run when normal WMI is running */
            status = A_ERROR;
            break;    
        }
        
        pHeader = (EPPING_HEADER *)A_NETBUF_DATA(skb);
        
        if (!IS_EPPING_PACKET(pHeader)) {
            status = A_EINVAL;
            break;
        }
                       
        if (IS_EPING_PACKET_NO_DROP(pHeader)) {
            htc_tag = AR6K_CONTROL_PKT_TAG;   
        }
        
        length = sizeof(EPPING_HEADER) + pHeader->DataLength;
                
        pPacket = AllocHTCStruct(pHcidevInfo);
        if (NULL == pPacket) {        
            status = A_NO_MEMORY;
            break;
        } 
     
        SET_HTC_PACKET_INFO_TX(pPacket,
                               skb,
                               A_NETBUF_DATA(skb),
                               length,
                               HCI_ACL_TYPE,  /* send every thing out as ACL */
                               htc_tag);
             
        HCI_TransportSendPkt(pHcidevInfo->pHCIDev,pPacket,FALSE);                           
        pPacket = NULL;
            
    } while (FALSE);
            
    return status;
}

void ar6000_set_default_ar3kconfig(AR_SOFTC_T *ar, void *ar3kconfig)
{
    AR6K_HCI_BRIDGE_INFO *pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)ar->hcidev_info;
    AR3K_CONFIG_INFO *config = (AR3K_CONFIG_INFO *)ar3kconfig;

    config->pHCIDev = pHcidevInfo->pHCIDev;
    config->pHCIProps = &pHcidevInfo->HCIProps;
    config->pHIFDevice = ar->arHifDevice;
    config->pBtStackHCIDev = pHcidevInfo->pBtStackHCIDev;
    config->Flags |= AR3K_CONFIG_FLAG_SET_AR3K_BAUD;
    config->AR3KBaudRate = 115200;    
}

#ifdef CONFIG_BLUEZ_HCI_BRIDGE   
/*** BT Stack Entrypoints *******/

/*
 * bt_open - open a handle to the device
*/
static int bt_open(struct hci_dev *hdev)
{
 
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_open - enter - x\n"));
    set_bit(HCI_RUNNING, &hdev->flags);
    set_bit(HCI_UP, &hdev->flags);
    set_bit(HCI_INIT, &hdev->flags);         
    return 0;
}

/*
 * bt_close - close handle to the device
*/
static int bt_close(struct hci_dev *hdev)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_close - enter\n"));
    clear_bit(HCI_RUNNING, &hdev->flags);
    return 0;
}

/*
 * bt_send_frame - send data frames
*/
static int bt_send_frame(struct sk_buff *skb)
{
    struct hci_dev             *hdev = (struct hci_dev *)skb->dev;
    HCI_TRANSPORT_PACKET_TYPE  type;
    AR6K_HCI_BRIDGE_INFO       *pHcidevInfo;
    HTC_PACKET                 *pPacket;
    A_STATUS                   status = A_OK;
    struct sk_buff             *txSkb = NULL;
    
    if (!hdev) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("HCI Bridge: bt_send_frame - no device\n"));
        return -ENODEV;
    }
      
    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_send_frame - not open\n"));
        return -EBUSY;
    }
  
    pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)hdev->driver_data;   
    A_ASSERT(pHcidevInfo != NULL);
      
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("+bt_send_frame type: %d \n",bt_cb(skb)->pkt_type));
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
            /* we don't support SCO over the bridge */
            kfree_skb(skb);
            return 0;
        default:
            A_ASSERT(FALSE);
            kfree_skb(skb);
            return 0;
    } 

    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_HCI_DUMP)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,(">>> Send HCI %s packet len: %d\n",
                        (type == HCI_COMMAND_TYPE) ? "COMMAND" : "ACL",
                        skb->len));
        if (type == HCI_COMMAND_TYPE) {
            A_UINT16 opcode = HCI_GET_OP_CODE(skb->data);
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,("    HCI Command: OGF:0x%X OCF:0x%X \r\n", 
                  opcode >> 10, opcode & 0x3FF));
        }
        AR_DEBUG_PRINTBUF(skb->data,skb->len,"BT HCI SEND Packet Dump");
    }
    
    do {
        
        txSkb = bt_skb_alloc(TX_PACKET_RSV_OFFSET + pHcidevInfo->HCIProps.HeadRoom + 
                             pHcidevInfo->HCIProps.TailRoom + skb->len, 
                             GFP_ATOMIC);

        if (txSkb == NULL) {
            status = A_NO_MEMORY;
            break;    
        }
        
        bt_cb(txSkb)->pkt_type = bt_cb(skb)->pkt_type;
        txSkb->dev = (void *)pHcidevInfo->pBtStackHCIDev;
        skb_reserve(txSkb, TX_PACKET_RSV_OFFSET + pHcidevInfo->HCIProps.HeadRoom);
        A_MEMCPY(txSkb->data, skb->data, skb->len);
        skb_put(txSkb,skb->len);
        
        pPacket = AllocHTCStruct(pHcidevInfo);        
        if (NULL == pPacket) {
            status = A_NO_MEMORY;
            break;    
        }       
              
        /* HCI packet length here doesn't include the 1-byte transport header which
         * will be handled by the HCI transport layer. Enough headroom has already
         * been reserved above for the transport header
         */
        SET_HTC_PACKET_INFO_TX(pPacket,
                               txSkb,
                               txSkb->data,
                               txSkb->len,
                               type, 
                               AR6K_CONTROL_PKT_TAG); /* HCI packets cannot be dropped */
        
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("HCI Bridge: bt_send_frame skb:0x%lX \n",(unsigned long)txSkb));
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("HCI Bridge: type:%d, Total Length:%d Bytes \n",
                                      type, txSkb->len));
                                      
        status = HCI_TransportSendPkt(pHcidevInfo->pHCIDev,pPacket,FALSE);   
        pPacket = NULL;
        txSkb = NULL;
        
    } while (FALSE);
   
    if (txSkb != NULL) {
        kfree_skb(txSkb);    
    }
    
    kfree_skb(skb);        
       
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("-bt_send_frame  \n"));
    return 0;
}

/*
 * bt_ioctl - ioctl processing
*/
static int bt_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_ioctl - enter\n"));
    return -ENOIOCTLCMD;
}

/*
 * bt_flush - flush outstandingbpackets
*/
static int bt_flush(struct hci_dev *hdev)
{
    AR6K_HCI_BRIDGE_INFO    *pHcidevInfo; 
    
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_flush - enter\n"));
    
    pHcidevInfo = (AR6K_HCI_BRIDGE_INFO *)hdev->driver_data;   
    
    /* TODO??? */   
    
    return 0;
}


/*
 * bt_destruct - 
*/
static void bt_destruct(struct hci_dev *hdev)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_destruct - enter\n"));
    /* nothing to do here */
}

static A_STATUS bt_setup_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{
    A_STATUS                    status = A_OK;
    struct hci_dev              *pHciDev = NULL;
    HIF_DEVICE_OS_DEVICE_INFO   osDevInfo;
    
    if (!setupbtdev) {
        return A_OK;    
    } 
        
    do {
            
        A_MEMZERO(&osDevInfo,sizeof(osDevInfo));
            /* get the underlying OS device */
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        status = ar6000_get_hif_dev((HIF_DEVICE *)(pHcidevInfo->HCITransHdl.hifDevice), 
                                    &osDevInfo);
#else
        status = HIFConfigureDevice(pHcidevInfo->ar->arHifDevice, 
                                    HIF_DEVICE_GET_OS_DEVICE,
                                    &osDevInfo, 
                                    sizeof(osDevInfo));
#endif
                                    
        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to OS device info from HIF\n"));
            break;
        }
        
            /* allocate a BT HCI struct for this device */
        pHciDev = hci_alloc_dev();
        if (NULL == pHciDev) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge - failed to allocate bt struct \n"));
            status = A_NO_MEMORY;
            break;
        }    
            /* save the device, we'll register this later */
        pHcidevInfo->pBtStackHCIDev = pHciDev;       
        SET_HCIDEV_DEV(pHciDev,osDevInfo.pOSDevice);          
        SET_HCI_BUS_TYPE(pHciDev, HCI_VIRTUAL, HCI_BREDR);
        pHciDev->driver_data = pHcidevInfo;
        pHciDev->open     = bt_open;
        pHciDev->close    = bt_close;
        pHciDev->send     = bt_send_frame;
        pHciDev->ioctl    = bt_ioctl;
        pHciDev->flush    = bt_flush;
        pHciDev->destruct = bt_destruct;
        pHciDev->owner = THIS_MODULE; 
            /* driver is running in normal BT mode */
        pHcidevInfo->HciNormalMode = TRUE;  
        
    } while (FALSE);
    
    if (A_FAILED(status)) {
        bt_cleanup_hci(pHcidevInfo);    
    }
    
    return status;
}

static void bt_cleanup_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{   
    int   err;      
        
    if (pHcidevInfo->HciRegistered) {
        pHcidevInfo->HciRegistered = FALSE;
        clear_bit(HCI_RUNNING, &pHcidevInfo->pBtStackHCIDev->flags);
        clear_bit(HCI_UP, &pHcidevInfo->pBtStackHCIDev->flags);
        clear_bit(HCI_INIT, &pHcidevInfo->pBtStackHCIDev->flags);   
        A_ASSERT(pHcidevInfo->pBtStackHCIDev != NULL);
            /* unregister */
        if ((err = hci_unregister_dev(pHcidevInfo->pBtStackHCIDev)) < 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to unregister with bluetooth %d\n",err));
        }          
    }   
    
    if (pHcidevInfo->pBtStackHCIDev != NULL) {
        kfree(pHcidevInfo->pBtStackHCIDev);
        pHcidevInfo->pBtStackHCIDev = NULL;
    }  
}

static A_STATUS bt_register_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{
    int       err;
    A_STATUS  status = A_OK;
    
    do {          
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: registering HCI... \n"));
        A_ASSERT(pHcidevInfo->pBtStackHCIDev != NULL);
             /* mark that we are registered */
        pHcidevInfo->HciRegistered = TRUE;
        if ((err = hci_register_dev(pHcidevInfo->pBtStackHCIDev)) < 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to register with bluetooth %d\n",err));
            pHcidevInfo->HciRegistered = FALSE;
            status = A_ERROR;
            break;
        }
    
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: HCI registered \n"));
        
    } while (FALSE);
    
    return status;
}

static A_BOOL bt_indicate_recv(AR6K_HCI_BRIDGE_INFO      *pHcidevInfo, 
                               HCI_TRANSPORT_PACKET_TYPE Type, 
                               struct                    sk_buff *skb)
{
    A_UINT8               btType;
    int                   len;
    A_BOOL                success = FALSE;
    BT_HCI_EVENT_HEADER   *pEvent;
    
    do {
             
        if (!test_bit(HCI_RUNNING, &pHcidevInfo->pBtStackHCIDev->flags)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("HCI Bridge: bt_indicate_recv - not running\n"));
            break;
        }
    
        switch (Type) {
            case HCI_ACL_TYPE:
                btType = HCI_ACLDATA_PKT;
                break;
            case HCI_EVENT_TYPE:  
                btType = HCI_EVENT_PKT;  
                break;
            default:
                btType = 0;
                A_ASSERT(FALSE);
                break;
        } 
        
        if (0 == btType) {
            break;    
        }
        
            /* set the final type */
        bt_cb(skb)->pkt_type = btType;
            /* set dev */
        skb->dev = (void *)pHcidevInfo->pBtStackHCIDev;
        len = skb->len;
        
        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_HCI_RECV)) {
            if (bt_cb(skb)->pkt_type == HCI_EVENT_PKT) {                
                pEvent = (BT_HCI_EVENT_HEADER *)skb->data; 
                AR_DEBUG_PRINTF(ATH_DEBUG_HCI_RECV, ("BT HCI EventCode: %d, len:%d \n", 
                        pEvent->EventCode, pEvent->ParamLength));
            } 
        }
        
        /* pass receive packet up the stack */
#ifdef CONFIG_BT
        if (hci_recv_frame(skb) != 0) {
#else
        if (1) {
#endif
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: hci_recv_frame failed \n"));
            break;
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_HCI_RECV, 
                    ("HCI Bridge: Indicated RCV of type:%d, Length:%d \n",btType,len));
        }
            
        success = TRUE;
    
    } while (FALSE); 
    
    return success;
}

static struct sk_buff* bt_alloc_buffer(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, int Length) 
{ 
    struct sk_buff *skb;         
        /* in normal HCI mode we need to alloc from the bt core APIs */
    skb = bt_skb_alloc(Length, GFP_ATOMIC);
    if (NULL == skb) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to alloc bt sk_buff \n"));
    }
    return skb;
}

static void bt_free_buffer(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, struct sk_buff *skb)
{
    kfree_skb(skb);    
}

#else // { CONFIG_BLUEZ_HCI_BRIDGE

    /* stubs when we only want to test the HCI bridging Interface without the HT stack */
static A_STATUS bt_setup_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{
    return A_OK;    
}
static void bt_cleanup_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{   
     
}
static A_STATUS bt_register_hci(AR6K_HCI_BRIDGE_INFO *pHcidevInfo)
{
    A_ASSERT(FALSE);
    return A_ERROR;    
}

static A_BOOL bt_indicate_recv(AR6K_HCI_BRIDGE_INFO      *pHcidevInfo, 
                               HCI_TRANSPORT_PACKET_TYPE Type, 
                               struct                    sk_buff *skb)
{
    A_ASSERT(FALSE);
    return FALSE;    
}

static struct sk_buff* bt_alloc_buffer(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, int Length) 
{
    A_ASSERT(FALSE);
    return NULL;
}
static void bt_free_buffer(AR6K_HCI_BRIDGE_INFO *pHcidevInfo, struct sk_buff *skb)
{
    A_ASSERT(FALSE);
}

#endif // } CONFIG_BLUEZ_HCI_BRIDGE

#else  // { ATH_AR6K_ENABLE_GMBOX

    /* stubs when GMBOX support is not needed */
    
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
A_STATUS ar6000_setup_hci(void *ar)
#else
A_STATUS ar6000_setup_hci(AR_SOFTC_T *ar)
#endif
{
    return A_OK;   
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
void ar6000_cleanup_hci(void *ar)
#else
void ar6000_cleanup_hci(AR_SOFTC_T *ar)
#endif
{
    return;    
}

#ifndef EXPORT_HCI_BRIDGE_INTERFACE
void ar6000_set_default_ar3kconfig(AR_SOFTC_T *ar, void *ar3kconfig)
{
    return;
}
#endif

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
int hci_test_send(void *ar, struct sk_buff *skb)
#else
int hci_test_send(AR_SOFTC_T *ar, struct sk_buff *skb)
#endif
{
    return -EOPNOTSUPP;
}

#endif // } ATH_AR6K_ENABLE_GMBOX


#ifdef EXPORT_HCI_BRIDGE_INTERFACE
static int __init
hcibridge_init_module(void)
{
    A_STATUS status;
    HCI_TRANSPORT_CALLBACKS hciTransCallbacks;

    hciTransCallbacks.setupTransport = ar6000_setup_hci;
    hciTransCallbacks.cleanupTransport = ar6000_cleanup_hci;

    status = ar6000_register_hci_transport(&hciTransCallbacks);
    if(status != A_OK)
        return -ENODEV;

    return 0;
}

static void __exit
hcibridge_cleanup_module(void)
{
}

module_init(hcibridge_init_module);
module_exit(hcibridge_cleanup_module);
MODULE_LICENSE("GPL and additional rights");
#endif
