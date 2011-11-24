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


#ifndef __PAL_API_H__
#define __PAL_API_H__


#ifdef __cplusplus
extern "C" {
#endif

typedef int (* evt_dispatcher)(void *dev, char *buf, short sz);
typedef int (* data_rx_handler)(void *dev, char *buf, short sz);

/*
 * pal_init:
 *      Initializes PAL internal data structures and 
 *      interacts  with driver. Stack passes the device
 *      name for PAL to identify the underlying device. 
 * Parameters:
 *  if_name: an ascii character string with dev name. 
 *           Ex : "eth1" or "eth2".
 * Return:
 *  void * : -non-NULL indicates PAL_device handle; to be
 *            used for future interaction by stack.
 *           -NULL indicates failure. A failure will be 
 *            logged.
 */
void *  
pal_init(char *if_name);

/*
 * pal_send_hci_cmd:
 *      Process an HCI cmd. Function returns immediately.
 *      Execution of cmd may happen in a deferred context.
 * Parameters:
 *  dev : device handle of PAL
 *  buf : char *, containing the HCI cmd.
 *  sz  : sz of the cmd buf.
 *
 *  Returns: 0 if the cmd passes basic checks.
 */
int     
pal_send_hci_cmd(void *dev, char *buf, short sz);

/*
 * pal_send_acl_data_pkt:
 *      Process and send an ACL data packet. The format 
 *      of the buffer must adhere to the ACL data 
 *      frame format.
 * Parameters:
 *  dev : device handle of PAL
 *  buf : char *, containing the HCI cmd.
 *  sz  : sz of the cmd buf.
 *
 *  Returns 0: if the frame has been accepted.
 */
int     
pal_send_acl_data_pkt(void *dev, char *buf, short sz);

/*
 * pal_evt_set_dispatcher:
 *      Stack needs to register this event dispatcher
 *      to receive events from PAL. The buffer from PAL
 *      is held in PAL context; which will be freed
 *      upon return from this event dispatcher. Return
 *      value from callback is ignored.
 * Parameters:
 *  dev : device handle of PAL
 *  fn : Call back function, of type evt_dispatcher.
 *
 * Device handle is returned in callback for stack to 
 * identify the pal_device, along with event buffer and 
 * its size. The event buffer will be an HCI event.
 */
void    
pal_evt_set_dispatcher(void *dev, evt_dispatcher fn);


/*
 * pal_data_set_dispatcher:
 *      Stack needs to register receive data handler
 *      to receive ACL data from PAL. The buffer from 
 *      PAL is held in PAL context; which will be freed
 *      upon return from this dispatcher. Return
 *      value from callback is ignored.
 *
 * Parameters:
 *  dev : device handle of PAL
 *  fn : Call back function, of type data_rx_handler.
 *
 * Device handle is returned in callback for stack to 
 * identify the pal_device, along with databuffer and 
 * its size. The databuffer will be an ACL data pkt.
 */
void    
pal_data_set_dispatcher(void *dev, data_rx_handler fn);


/*
 * pal_log_cfg:
 *      Logging support in PAL. PAL will log in two 
 *      places, in a file and on screen. User may choose
 *      based on their need, in the bit fields of input
 *      parameter. It will dump HCI cmds/events, data frames
 *      and lot of other info. Stack must call it after 
 *      pal_init()
 *
 * Parameters:
 *  dev: device handle of PAL
 *  log_cfg: logging configration(bit field)
 *              0x0 = no-logging(default)
 *              0x1 = file log(in "pal.log")
 *              0x2 = screen log
 */
void
pal_log_cfg(void * dev, A_UINT32 log_cfg);


#ifdef __cplusplus
}
#endif

#endif  /* __PAL_API_H__ */


