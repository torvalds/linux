/*
 * Copyright (c) 2009 Atheros Communications Inc.
 * All rights reserved.

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
 *
 */

#ifndef __PAL_INTF_H__
#define __PAL_INTF_H__

typedef struct  amp_dev_t {
    int s;                  /* Socket handle for interface */
    int s_rx;                  /* Socket handle for interface, for recv */
    char ifname[IFNAMSIZ];  /* Interface name */
    struct iw_range *range; /* Interface info, channel, security... */
    evt_dispatcher  pal_evt_dispatcher;
    data_rx_handler pal_data_dispatcher;
    A_UINT64 evt_mask[2];
}AMP_DEV;

void *
amp_phy_init(char *intf_name);

void
app_send_hci_cmd(AMP_DEV *dev, A_UINT8 *datap, short len);

void
app_send_acl_data(AMP_DEV *dev, A_UINT8 *datap, short len);
#endif  /* __PAL_INTF_H__ */
