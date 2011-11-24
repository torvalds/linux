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


extern FILE *opFile;
void recvPalEvents(char *intf_name);
extern int eventLogFile;

static AMP_DEV mDEV;
struct sockaddr_ll my_addr_debugV;
void palEvents( HCI_EVENT_PKT *hciEvent,int len);
void palData(A_UINT8 *data,A_UINT32 len);

void *wlan_data_and_event_recv_debugV(void *pdev)
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

    memset(&my_addr_debugV, '\0', sizeof(struct sockaddr_ll));
    my_addr_debugV.sll_family = AF_PACKET;
    my_addr_debugV.sll_protocol = htons(ETH_P_ALL);
    my_addr_debugV.sll_ifindex = ifr.ifr_ifindex;

    if (bind(dev->s_rx, (struct sockaddr *)&my_addr_debugV, sizeof(struct sockaddr_ll)) < 0) {
        err(1, "bind");
        return NULL;
    }

    if (ioctl(dev->s_rx, SIOCGIFHWADDR, &ifr) < 0) {
        printf("Failed to get MAC address of interface!\n");
        return NULL;
    }

    FD_ZERO(&fds);
    FD_SET(dev->s_rx, &fds);

    while(TRUE) {

        if(select(1+dev->s_rx, &fds, NULL, NULL, NULL) < 0) {
            perror("select()");
        }
        if ((len = recvfrom(dev->s_rx, buf, 1600, 0, NULL, NULL)) < 0) {
            printf("error in recvfom\n");
        } else {
            event = *((short *)buf);
            if(event == WMI_HCI_EVENT_EVENTID) {
                palEvents((HCI_EVENT_PKT *)(buf + sizeof(int)),len - sizeof(int));
                //app_hci_event_rx(dev, (A_UINT8 *)(buf + sizeof(int)), len - sizeof(int));
            } else if(event == WMI_ACL_DATA_EVENTID) {
                printf("Recevied data\n");
                palData((unsigned char *)(buf + sizeof(int)),len - sizeof(int));
                //app_hci_data_rx(dev, (A_UINT8 *)(buf + sizeof(int)), len - sizeof(int));
            } else {
                //dump_frame(buf, len);
            }
        }
    }
    if(opFile)
    {
        fclose(opFile);
    }
    printf("End of thread\n");
    return NULL;
}

void recvPalEvents (char *intf_name)
{
    AMP_DEV *pDev = &mDEV;
    strcpy(pDev->ifname, intf_name);
    wlan_data_and_event_recv_debugV (pDev);
}

int main (int argc,char *argv[])
{
    eventLogFile = 1;
    opFile = fopen ("palEvents.log","w+");
    if (argv[1] != NULL)
    {
        printf ("Attaching to the interface %s\n", argv[1]);
        recvPalEvents (argv[1]);
    }
    else
    {
        printf ("Usage : palLogEvents <interface name> | eg: pal_app eth1\n");
    }
    return 0;
}

