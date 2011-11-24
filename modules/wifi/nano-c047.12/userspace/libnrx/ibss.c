/* Copyright (C) 2007 Nanoradio AB */
/* $Id: ibss.c 15614 2010-06-10 15:52:52Z toos $ */

#include "nrx_priv.h"

/*!
 * @ingroup MISC
 * @brief <b>Set IBSS beacon period</b>
 *
 * The beacon period is the time between beacon frames and is used to inform
 * stations receiving the beacon when to expect the next beacon. In an IBSS
 * the station that starts the BSS will specify the beacon period and
 * establish the basic beaconing process for the IBSS. When selecting the
 * beacon-period in an IBSS you should consider power management and collision
 * avoidance. Setting the beacon period will only take effect when starting a
 * new IBSS.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param period The beacon period in TU (1 TU = 1024 microseconds). This
 *        cannot be 0; Maximum value is 2^16-1.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_ibss_beacon_period(nrx_context ctx, uint16_t period)
{
   struct nrx_ioc_uint16_t param;
   NRX_ASSERT(ctx);
   NRX_CHECK(period > 0);

   param.value = period;
   return nrx_nrxioctl(ctx, NRXIOWBEACONPERIOD, &param.ioc);
}

/*!
 * @internal
 */
int
nrx_get_ibss_beacon_period(nrx_context ctx, uint16_t *period)
{
   struct nrx_ioc_uint16_t param;
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(period);

   ret = nrx_nrxioctl(ctx, NRXIORBEACONPERIOD, &param.ioc);
   if(ret == 0)
      *period = param.value;
   return ret;
}

/*!
 * @internal
 */
int
nrx_set_ibss_dtim_period(nrx_context ctx, uint8_t period)
{
   struct nrx_ioc_uint8_t param;
   NRX_ASSERT(ctx);

   param.value = period;
   return nrx_nrxioctl(ctx, NRXIOWDTIMPERIOD, &param.ioc);
}

/*!
 * @internal
 */
int
nrx_get_ibss_dtim_period(nrx_context ctx, uint8_t *period)
{
   struct nrx_ioc_uint8_t param;
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(period);

   ret = nrx_nrxioctl(ctx, NRXIORDTIMPERIOD, &param.ioc);
   if(ret == 0)
      *period = param.value;
   return ret;
}

/*!
 * @ingroup PS
 * @brief <b>Set IBSS ATIM period</b>
 *
 * The ATIM parameter defines the window size for sending ATIM frames.
 * This parameter is only used for IBSS power save. 
 * Setting the ATIM window will only take effect when starting a
 * new IBSS.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param window The ATIM window length in TU (1 TU = 1024 microseconds).
 * The maximum value is 2^16.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_ibss_atim_window(nrx_context ctx, uint16_t window)
{
   struct nrx_ioc_uint16_t param;
   NRX_ASSERT(ctx);

   param.value = window;
   return nrx_nrxioctl(ctx, NRXIOWATIMWINDOW, &param.ioc);
}

/*!
 * @internal
 */
int
nrx_get_ibss_atim_window(nrx_context ctx, uint16_t *window)
{
   struct nrx_ioc_uint16_t param;
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(window);

   ret = nrx_nrxioctl(ctx, NRXIORATIMWINDOW, &param.ioc);
   if(ret == 0)
      *window = param.value;
   return ret;
}

