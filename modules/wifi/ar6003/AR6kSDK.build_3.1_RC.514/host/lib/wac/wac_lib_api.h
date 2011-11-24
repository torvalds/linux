//------------------------------------------------------------------------------
// Copyright (c) 2010 Atheros Corporation.  All rights reserved.
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
// Author(s): ="Atheros"
//==============================================================================

#ifndef __WAC_LIB_API_H__
#define __WAC_LIB_API_H__

#include "wac_defs.h"

/*
 * Function to enable or disable the WAC feature. This is the entry point to WAC when the 
 * user pushed a button on the remote
 * Input arguments:
 *      s - open file descriptor of socket
 *      enable - flag to enable/disable WAC. 1 = enable; 0 = disable
 *               for disablethe remaining arguments are don't cares
 *      period - time in milliseconds between consecutive scans when WAC is enabled
 *      scan_thres - number of scan retries before the STA gave up on looking for WAC AP
 *      rssi_thres - RSSI threshold the STA will check in beacon or probe response frames 
 *                   to qualify a WAC AP. This is absolute value of the signal strength in dBm
 * Return value:
 *      0  = success;	-1 = failure
 *
 * Examples:
 * To enable WAC:  wac_enable(s, 1, 2000, 3, 25)
 *      - WAC enabled with 2-second interval between scans for up to 3 tries. RSSI threshold is -25dBm
 * To disable WAC: wac_enable(s, 0, 0, 0, 0)
 *      - When the flag is disable, the rest of arguments are don't cares
 */
int wac_enable(int s, int enable, unsigned int period, unsigned int scan_thres, int rssi_thres);

/*
 * Function for I/O Control Request for Samsung IE as specified version 1.2 protocol
 * Input arguments:
 *      s - open file descriptor for socket
 *      req - request type takes the possible values:
 *              WAC_SET
 *              WAC_GET
 *      cmd - command takes the possible values:
 *              WAC_ADD
 *              WAC_DEL
 *              WAC_GET_STATUS
 *      frm - frame type takes the possible values:
 *              PRBREQ (for STA)
 *              PRBRSP (for AP)
 *              BEACON (for AP)
 *      ie - pointer to char string that contains the IE to set or get
 *      ret_val - indicates whether the control request is successful
 *              0  = success; -1 = failure
 *      status - status code returned by STA that takes possible values:
 *              WAC_FAILED_NO_WAC_AP
 *              WAC_FAILED_LOW_RSSI
 *              WAC_FAILED_INVALID_PARAM
 *              WAC_FAILED_REJECTED
 *              WAC_SUCCESS
 *              WAC_PROCEED_FIRST_PHASE
 *              WAC_PROCEED_SECOND_PHASE
 *              WAC_DISABLED
 * Examples:
 * To insert an IE into the probe request frame:
 *      wac_control_request(WAC_SET, WAC_ADD, PRBREQ, 
 *                          "0x0012fb0100010101083132333435363730" val, status)
 * To query the WAC status from STA:
 *      wac_control_request(WAC_GET, WAC_GET_STATUS, NULL, NULL, val, status)
 */
void wac_control_request( int s,
                          WAC_REQUEST_TYPE req, 
                          WAC_COMMAND cmd, 
                          WAC_FRAME_TYPE frm, 
                          char *ie, 
                          int *ret_val, 
                          WAC_STATUS *status );

#endif
