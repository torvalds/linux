/*
 * Copyright (c) 2009 Atheros Communications Inc.
 * All rights reserved.
 * 
 *
 * 
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
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <asm/types.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/version.h>
#include <linux/wireless.h>
#include <linux/if_ether.h>
#include <netpacket/packet.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <pthread.h>
#include <err.h>


#include <a_config.h>
#include <a_osapi.h>
#include <a_types.h>
#include <athdefs.h>
#include <ieee80211.h>
#include <wmi.h>
#include <athdrv_linux.h>
#include "pal_api.h"
#include "pal_util.h"
#include "pal_intf.h"
#include "paldebug.h"
#include "a_hci.h"

#define ID_LEN                         2


static A_STATUS app_hci_event_rx(AMP_DEV *dev, A_UINT8 *datap, int len);
static A_STATUS app_hci_data_rx(AMP_DEV *dev, A_UINT8 *datap, int len);


static AMP_DEV DEV;
struct sockaddr_ll my_addr;
pthread_t   t1;



void *
wlan_data_and_event_recv(void *pdev)
{
    AMP_DEV *dev = (AMP_DEV *)pdev;
    fd_set fds;
    char buf[1600];
    struct ifreq ifr;
    int len, event;
    
    dev->s_rx = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (dev->s_rx < 0) {
        err(1, "socket");
        return NULL;
    }

    strncpy(ifr.ifr_name, dev->ifname, sizeof(dev->ifname));

    if (ioctl(dev->s_rx, SIOCGIFINDEX, &ifr) < 0) {
        err(1, "SIOCGIFINDEX");
        return NULL;
    }

    memset(&my_addr, '\0', sizeof(struct sockaddr_ll));
    my_addr.sll_family = AF_PACKET;
    my_addr.sll_protocol = htons(ETH_P_ALL);
    my_addr.sll_ifindex = ifr.ifr_ifindex;

    if (bind(dev->s_rx, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll)) < 0) {
        err(1, "bind");
        return NULL;
    }

    if (ioctl(dev->s_rx, SIOCGIFHWADDR, &ifr) < 0) {
        PAL_PRINT("Failed to get MAC address of interface!\n");
        return NULL;
    }

    FD_ZERO(&fds);
    FD_SET(dev->s_rx, &fds);

    while(TRUE) {

        if(select(1+dev->s_rx, &fds, NULL, NULL, NULL) < 0) {
            perror("select()");
        }
        if ((len = recvfrom(dev->s_rx, buf, 1600, 0, NULL, NULL)) < 0) {
            PAL_PRINT("error in recvfom\n");
        } else {
            event = *((short *)buf);
            if(event == WMI_HCI_EVENT_EVENTID) {
                app_hci_event_rx(dev, (A_UINT8 *)(buf + sizeof(int)), len - sizeof(int));
            } else if(event == WMI_ACL_DATA_EVENTID) {
                app_hci_data_rx(dev, (A_UINT8 *)(buf + sizeof(int)), len - sizeof(int));
            } else {
                //dump_frame(buf, len);
            }
        }
        /* Invalidate the header, before re-use */
        *((short *)buf) = 0xFFFF;
    }
    
    return NULL;
}


void *
phy_attach(char *intf_name)
{
    AMP_DEV *pDev = &DEV;
    struct ifreq ifr;
    struct iwreq iwr;
    char *hwaddr;
    struct iw_range *range;
    size_t buflen;

    strcpy(pDev->ifname, intf_name);
    memset(&iwr, 0, sizeof(iwr));

    buflen = sizeof(struct iw_range) + 500;
    range = malloc(buflen);
    if (range == NULL)
        return NULL;
    memset(range, 0, buflen);
    iwr.u.data.pointer = (caddr_t) range;
    iwr.u.data.length = buflen;

    pDev->range = range;


    strcpy(pDev->ifname, intf_name);
    pDev->s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (pDev->s < 0) {
        err(1, "socket");
        return NULL;
    }

    strncpy(ifr.ifr_name, pDev->ifname, sizeof(pDev->ifname));
    strncpy(iwr.ifr_name, pDev->ifname, sizeof(pDev->ifname));

    if (ioctl(pDev->s, SIOCGIFINDEX, &ifr) < 0) {
        err(1, "SIOCGIFINDEX");
        return NULL;
    }

    memset(&my_addr, '\0', sizeof(struct sockaddr_ll));
    my_addr.sll_family = AF_PACKET;
    my_addr.sll_protocol = htons(ETH_P_ALL);
    my_addr.sll_ifindex = ifr.ifr_ifindex;

    if (bind(pDev->s, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll)) < 0) {
        err(1, "bind");
        return NULL;
    }

    if (ioctl(pDev->s, SIOCGIFHWADDR, &ifr) < 0) {
        PAL_PRINT("Failed to get MAC address of interface!\n");
        return NULL;
    }
    hwaddr = (char *)ifr.ifr_hwaddr.sa_data;
#if 1
    PAL_PRINT("MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
            hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
#endif


    if (ioctl(pDev->s, SIOCGIWRANGE, &iwr) < 0) {
    } else {
#if 0
        int i;
        PAL_PRINT("Num of channels = %d\n", range->num_channels);
        for(i = 0; i < range->num_channels; i++) {
            PAL_PRINT("%d\n", range->freq[i].m);
            PAL_PRINT("%d\n", range->freq[i].i);
        }
#endif

    }
    return pDev;
}



void *
amp_phy_init(char *intf_name)
{
    AMP_DEV *pDev = &DEV;
    struct ifreq ifr;
    struct iwreq iwr;
    char *hwaddr;
    struct iw_range *range;
    size_t buflen;

    strcpy(pDev->ifname, intf_name);

    /* Create and dispatch recv thread */
    t1 = pthread_create(&t1, NULL, wlan_data_and_event_recv, (void *)pDev);
    memset(&iwr, 0, sizeof(iwr));

    buflen = sizeof(struct iw_range) + 500;
    range = malloc(buflen);
    if (range == NULL)
        return NULL;
    memset(range, 0, buflen);
    iwr.u.data.pointer = (caddr_t) range;
    iwr.u.data.length = buflen;

    pDev->range = range;

#if 0
    if ((ethIf = getenv("NETIF")) == NULL) {
         PAL_PRINT("set NETIF environment variable, after loading the driver\n");
         exit(0);
     } ETH_P_CONTROL
#endif

    strcpy(pDev->ifname, intf_name);
    pDev->s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (pDev->s < 0) {
        err(1, "socket");
        return NULL;
    }

    strncpy(ifr.ifr_name, pDev->ifname, sizeof(pDev->ifname));
    strncpy(iwr.ifr_name, pDev->ifname, sizeof(pDev->ifname));

    if (ioctl(pDev->s, SIOCGIFINDEX, &ifr) < 0) {
        err(1, "SIOCGIFINDEX");
        return NULL;
    }

    memset(&my_addr, '\0', sizeof(struct sockaddr_ll));
    my_addr.sll_family = AF_PACKET;
    my_addr.sll_protocol = htons(ETH_P_ALL);
    my_addr.sll_ifindex = ifr.ifr_ifindex;

    if (bind(pDev->s, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll)) < 0) {
        err(1, "bind");
        return NULL;
    }

    if (ioctl(pDev->s, SIOCGIFHWADDR, &ifr) < 0) {
        PAL_PRINT("Failed to get MAC address of interface!\n");
        return NULL;
    }
    hwaddr = (char *)ifr.ifr_hwaddr.sa_data;
#if 1
    PAL_PRINT("MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
            hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
#endif


    if (ioctl(pDev->s, SIOCGIWRANGE, &iwr) < 0) {
    } else {
#if 0
        int i;
        PAL_PRINT("Num of channels = %d\n", range->num_channels);
        for(i = 0; i < range->num_channels; i++) {
            PAL_PRINT("%d\n", range->freq[i].m);
            PAL_PRINT("%d\n", range->freq[i].i);
        }
#endif

    }
    return pDev;
}




/* HCI events will land here */
static A_STATUS 
app_hci_event_rx(AMP_DEV *dev, A_UINT8 *datap, int len)
{
    WMI_HCI_EVENT *ev;
    
    ev = (WMI_HCI_EVENT *)datap;

    PAL_PRINT("recvd HCI event = \n");
    dump_frame(datap, len);
    pal_decode_event(datap, (A_UINT16)len);
    if(dev->pal_evt_dispatcher) {
        dev->pal_evt_dispatcher(dev, (char *)datap, (short) len);
    }
    return A_OK;
}


/* HCI rx data will land here */
static A_STATUS 
app_hci_data_rx(AMP_DEV *dev, A_UINT8 *datap, int len)
{
    PAL_PRINT("recvd data frame = \n");
    dump_frame(datap, len);
    if(dev->pal_data_dispatcher) {
        dev->pal_data_dispatcher(dev, (char *)datap, (short)len);
    }
    return A_OK;
}

void
app_send_acl_data(AMP_DEV *dev, A_UINT8 *datap, short len)
{
    char buf[1600];
    struct ifreq ifr;
    HCI_ACL_DATA_PKT *acl = (HCI_ACL_DATA_PKT *)(buf + sizeof(int));

    ((int *)buf)[0] = AR6000_XIOCTL_ACL_DATA; 
    ifr.ifr_data = buf;
    strncpy(ifr.ifr_name, dev->ifname, sizeof(dev->ifname));
    /* This copy is avoided by having a headroom in APP data buffer */
    memcpy((A_UINT8 *)acl, datap, len);
    if (ioctl(dev->s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        err(1, ifr.ifr_name);
    }
}

void
app_send_hci_cmd(AMP_DEV *dev, A_UINT8 *datap, short len)
{
    char buf[1024];
    struct ifreq ifr;
    WMI_HCI_CMD *cmd = (WMI_HCI_CMD *)(buf + sizeof(int));

    ((int *)buf)[0] = AR6000_XIOCTL_HCI_CMD; 
    cmd->cmd_buf_sz = len;
    strncpy(ifr.ifr_name, dev->ifname, sizeof(dev->ifname));
    ifr.ifr_data = buf;

    /* This copy is avoided by having a headroom in APP data buffer */
    memcpy(cmd->buf, datap, len);
    if (ioctl(dev->s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        err(1, ifr.ifr_name);
    }

}
