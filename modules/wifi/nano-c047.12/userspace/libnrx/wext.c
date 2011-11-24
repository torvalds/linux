/* Copyright (C) 2007 Nanoradio AB */
/* $Id: wext.c 15531 2010-06-03 15:27:08Z joda $ */

/* This file contains stuff that depends on wireless.h. The reason for
 * keeping it in a separate file is that it pulls in some headers that
 * doesn't work well with other headers, for example net/if.h and
 * linux/if.h clash 
 */
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "nrx_priv.h"

/* this is a bit of a bastard, but rather than having two
 * functions, put an extra ifname here */
static int
nrx_iwioctl(nrx_context ctx,
            const char *ifname_opt,
            unsigned int cmd,
            struct iwreq *iwr)
{
   if(ifname_opt != NULL)
      strlcpy(iwr->ifr_name, ifname_opt, sizeof(iwr->ifr_name));
   else
      strlcpy(iwr->ifr_name, ctx->ifname, sizeof(iwr->ifr_name));

   if(ioctl(ctx->sock, cmd, iwr) < 0)
      return errno;
   
   return 0;
}

int
nrx_get_nickname(nrx_context ctx, char *ifname, char *name, size_t len)
{
   struct iwreq iwr;
   NRX_ASSERT(ctx);
   iwr.u.data.pointer = name;
   iwr.u.data.length = len;
   iwr.u.data.flags = 0;

   return nrx_iwioctl(ctx, ifname, SIOCGIWNICKN, &iwr);
}

union my_range {
   struct iw_range range;
   char dummy[1024];
};

static int
get_range(nrx_context ctx, char *ifname, union my_range *range)
{
   struct iwreq iwr;
   memset(&iwr, 0, sizeof(iwr));
   iwr.u.data.pointer = (caddr_t)range;
   iwr.u.data.length = sizeof(*range);
   iwr.u.data.flags = 0;
   
   return nrx_iwioctl(ctx, ifname, SIOCGIWRANGE, &iwr);
}

int
nrx_check_wext(nrx_context ctx, char *ifname)
{
   union my_range range;
   return get_range(ctx, ifname, &range);
}

int
nrx_get_wxconfig(nrx_context ctx)
{
   int ret;
   union my_range range;

   ret = get_range(ctx, NULL, &range);
   if(ret != 0)
      return ret;

   ctx->wx_version = range.range.we_version_compiled;
   return 0;
}

/*!
 * @ingroup SCAN
 * @brief <b>Get the latest scan results</b>
 *
 * The number of results that can be stored in the scan list is
 * limited by the memory on the host. A network is identified by the
 * combination of the BSSID and SSID. This means that there may be
 * several entries with the same SSID but different BSSIDs (such as
 * several APs in the same ESS) and several entries with the same
 * BSSID but different SSIDs (such a stealth AP being shown once 
 * as found by a "broadcast" scan job (without a SSID) and once
 * as found by a scan job that probes for the particular SSID of the
 * stealth AP (with the SSID)).
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param scan_nets Output buffer, allocated by the caller.
 *                  The output format is the same as used by the
 *                  SIOCGIWSCAN Wireless Extension ioctl. This consists of
 *                  a list of elements specifying different aspects of a
 *                  net, for instance BSSID, SSID, beacon RSSI etc. Code
 *                  that parses this structure can be found in Wireless
 *                  Tools iwlib.c function iw_process_scanning_token().
 *                  TODO : Further document result format, and implement
 *                  functions to help parse the data.
 * @param len Pointer to the size of the input buffer (parameter scan_nets) on
 *            input, pointer to the number of bytes copied on a successful call
 *            or to the size needed if the return value was EMSGSIZE.
 *
 *
 * @return
 * - 0 on success.
 * - EMSGSIZE if the input buffer was too small.
 */
int
nrx_get_scan_list(nrx_context ctx,
                  void *scan_nets, /* N.B. var name is used in test tool */
                  size_t *len)
{
   int ret;
   struct iwreq iwr;
   NRX_ASSERT(ctx);
   NRX_ASSERT(scan_nets);
   NRX_ASSERT(len);

   iwr.u.data.pointer = scan_nets;
   iwr.u.data.length = *len;
   iwr.u.data.flags = 0;

   ret = nrx_iwioctl(ctx, NULL, SIOCGIWSCAN, &iwr);
   if(ret == 0)
      *len = iwr.u.data.length;
   else 
      *len = 0;
   return ret;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the TX retry limits</b>
 *
 * Specify the maximum number of transmission retries before discarding a frame.
 * The firmware differentiates between short packets below RTS threshold 
 * and long packets above it. Default is 7 retries for short packets
 * and 5 for long packets.
 *
 * The limits specified with this function will only put an upper bound of the
 * the total number of retransmissions performed according to 
 * nrx_set_tx_retry_limit_by_rate().
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param short_limit Total number of retries done for packets shorter than 
 *                    the RTS threshold. Minimum value is 0 and maximum is 126.
 * @param long_limit Total number of retries done for packets longer than 
 *                    the RTS threshold. Minimum value is 0 and maximum is 126.
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_tx_retry_limits(nrx_context ctx,
                        uint8_t short_limit,
                        uint8_t long_limit)
{
   int ret;
   struct iwreq iwr;
   NRX_ASSERT(ctx);
   NRX_CHECK(short_limit<=126);
   NRX_CHECK(long_limit<=126);

   iwr.u.retry.fixed = 0;
   iwr.u.retry.disabled = 0;

   iwr.u.retry.flags = IW_RETRY_LIMIT | IW_RETRY_MIN;
   iwr.u.retry.value = short_limit;
   ret = nrx_iwioctl(ctx, NULL, SIOCSIWRETRY, &iwr);
   if(ret)
      return ret;

   iwr.u.retry.flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
   iwr.u.retry.value = long_limit;
   return nrx_iwioctl(ctx, NULL, SIOCSIWRETRY, &iwr);

}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the currently used frequency</b>
 *
 * @param [in] ctx NRX context that was created by the call to nrx_init_context().
 * @param [out] frequency Will hold activ frequency in kHz. 
 *
 * @retval 0 on success.
 * @retval EINVAL
 */
int
nrx_get_curr_freq(nrx_context ctx,
                  uint32_t *frequency)
{
   int ret;
   uint32_t m, e;
   struct iwreq iwr;
   NRX_ASSERT(ctx);
   NRX_ASSERT(frequency);

   iwr.u.freq.m = 0;
   iwr.u.freq.e = 0;
   iwr.u.freq.i = 0;
#if WIRELESS_EXT >= 17
   iwr.u.freq.flags = 0;
#endif

   ret = nrx_iwioctl(ctx, NULL, SIOCGIWFREQ, &iwr);
   if(ret)
      return ret;

   m = iwr.u.freq.m;
   e = iwr.u.freq.e;
   
   while(e < 3) {
      m /= 10;
      e++;
   }
   while(e > 3) {
      m *= 10;
      e--;
   }
   *frequency = m;
   return 0;
}
