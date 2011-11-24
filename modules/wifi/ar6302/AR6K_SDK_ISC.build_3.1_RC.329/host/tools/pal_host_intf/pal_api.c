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

void *  
pal_init(char *if_name)
{
    return amp_phy_init(if_name);
}

int     
pal_send_hci_cmd(void *dev, char *buf, short sz)
{
    PAL_PRINT("PAL recv HCI cmd ->\n");
    dump_frame((A_UINT8 *)buf, (A_UINT32)sz);
    app_send_hci_cmd((AMP_DEV *)dev, (A_UINT8 *)buf, sz);
    return 0;
}

int     
pal_send_acl_data_pkt(void *dev, char *buf, short sz)
{
    PAL_PRINT("PAL recv ACL data pkt\n");
    dump_frame((A_UINT8 *)buf, (A_UINT32)sz);
    app_send_acl_data((AMP_DEV *)dev, (A_UINT8 *)buf, sz);
    return 0;
}


void    
pal_evt_set_dispatcher(void *dev, evt_dispatcher fn)
{
    AMP_DEV *pdev = (AMP_DEV *)dev;
    pdev->pal_evt_dispatcher = fn;

}


void    
pal_data_set_dispatcher(void *dev, data_rx_handler fn)
{
    AMP_DEV *pdev = (AMP_DEV *)dev;
    pdev->pal_data_dispatcher = fn;
}




