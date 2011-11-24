/* $Id: we_ratemon.c,v 1.3 2008-03-01 12:31:40 peek Exp $ */
/*****************************************************************************

Copyright (c) 2007 by Nanoradio AB

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
This module will monitor changes in RX and TX rate. 

*****************************************************************************/

#include "driverenv.h"
#include "mac_api_data.h"

/*********************
 *
 * Common stuff
 * 
 *********************/

typedef struct 
{
      m80211_std_rate_encoding_t   thr_limit;
      we_ind_cb_t                  thr_cb;         /* NULL when disabled */
      bool_t                       thr_sent;
      uint8_t                      thr_dir;
      int                          hist_len;
      int                          hist_curr;          /* Where in rate_hist we're operating */
      int                          hist_count;         /* Needed for fewer samples than hist_len */
      m80211_std_rate_encoding_t   hist[1];
} rate_ind_conf_t;


static int common_threshold_enable(int32_t *thr_id,
                                   rate_ind_conf_t *rate_ind,
                                   uint32_t supv_interval, 
                                   m80211_std_rate_encoding_t thr_level, 
                                   uint8_t dir, 
                                   we_ind_cb_t cb)
{
   DE_ASSERT(rate_ind != NULL);
   MEMSET(rate_ind, 0, sizeof(*rate_ind));

   /* Don't allow both dir simultaneously */
   DE_ASSERT(dir == WE_TRIG_THR_RISING || dir == WE_TRIG_THR_FALLING);
   
   rate_ind->thr_limit = thr_level;
   rate_ind->thr_cb = cb;
   rate_ind->thr_dir = dir;

   rate_ind->hist_len = supv_interval;
   *thr_id = dir;               /* Use dir as id */

   DE_TRACE_INT(TR_MIB, "Threshold set to %d\n", (char) thr_level);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 *  In a true median calculation over an even number of samples, an
 *  average of the two closest samples is taken. This will not be done
 *  here to ensure that the result is a real rate.
 */
static void common_limit_check(rate_ind_conf_t *rate_ind,
                               m80211_std_rate_encoding_t rate)
{
   int i, j, min_diff;
   m80211_std_rate_encoding_t median;

   DE_ASSERT(rate_ind != NULL);

   /* Add to history */
   rate_ind->hist[rate_ind->hist_curr] = rate;
   rate_ind->hist_curr++;
   if (rate_ind->hist_curr >= rate_ind->hist_len)
      rate_ind->hist_curr = 0;
   if (rate_ind->hist_count < rate_ind->hist_len)
      rate_ind->hist_count++;

   /* Calculate median */
   median = 0;
   min_diff = rate_ind->hist_len;
   for (i = 0; i < rate_ind->hist_count; i++) {
      int larger = 0, smaller = 0, diff, abs_diff;
      for (j = 0; j < rate_ind->hist_count; j++) {
         if (rate_ind->hist[j] > rate_ind->hist[i])
            larger++;
         if (rate_ind->hist[j] < rate_ind->hist[i])
            smaller++;
      }
      diff = larger - smaller;
      abs_diff = diff >= 0 ? diff : -diff;
      if (abs_diff < min_diff) {
         min_diff = abs_diff;
         median = rate_ind->hist[i];
      }
      if (min_diff == 0)
         break;
   }

   /* Clear flags */
   if (rate_ind->thr_dir == WE_TRIG_THR_FALLING && median > rate_ind->thr_limit) 
      rate_ind->thr_sent = FALSE;
   if (rate_ind->thr_dir == WE_TRIG_THR_RISING && median < rate_ind->thr_limit) 
      rate_ind->thr_sent = FALSE;

   /* Send indication */
   if (rate_ind->thr_cb != NULL && ! rate_ind->thr_sent)
      if (rate_ind->hist_count >= rate_ind->hist_len) {
         int send_it = 0;
         if (rate_ind->thr_dir == WE_TRIG_THR_FALLING && median <= rate_ind->thr_limit) 
            send_it = 1;
         if (rate_ind->thr_dir == WE_TRIG_THR_RISING && median >= rate_ind->thr_limit) 
            send_it = 1;
         if (send_it) {
            we_vir_trig_data_t ind;
            ind.trig_id = rate_ind->thr_dir; /* use dir as id */
            ind.type    = WE_TRIG_TYPE_IND;
            ind.value   = median;
            ind.reason  = 0;
            rate_ind->thr_sent = TRUE;
            rate_ind->thr_cb((void *)&ind, sizeof(ind));
         }
      }
}


static void common_threshold_disable(rate_ind_conf_t *rate_ind, uint32_t reason)
{
   we_vir_trig_data_t ind;

   DE_ASSERT(rate_ind != NULL);
   
   ind.trig_id = rate_ind->thr_dir; /* use dir as id */
   ind.type    = WE_TRIG_TYPE_CANCEL;
   ind.value   = 0;
   ind.reason  = reason;
   rate_ind->thr_cb((void *)&ind, sizeof(ind));
}


/*********************
 *
 * Rx specific stuff
 *  (only falling)
 *
 *********************/

static rate_ind_conf_t *rate_rx_falling;
static m80211_std_rate_encoding_t last_rate_rx;

void wei_ratemon_notify_data_ind(m80211_std_rate_encoding_t rate)
{
   last_rate_rx = rate;
   if (rate_rx_falling != NULL)
      common_limit_check(rate_rx_falling, rate);
}

int WiFiEngine_GetRxRate(m80211_std_rate_encoding_t *rate)
{
   if (last_rate_rx == 0)
      return WIFI_ENGINE_FAILURE;
   *rate = last_rate_rx;
   return WIFI_ENGINE_SUCCESS;
}

int WiFiEngine_EnableRxRateMon(int32_t                    *thr_id,
                               uint32_t                   supv_interval, 
                               m80211_std_rate_encoding_t thr_level,
                               uint8_t                    dir, /* rising/falling */
                               we_ind_cb_t                cb)
{
   size_t size;
   rate_ind_conf_t **rate_ind;
   DE_TRACE_STATIC(TR_MIB, "ENTRY\n");

   switch (dir) {
      case WE_TRIG_THR_FALLING:
         if (rate_rx_falling != NULL) {
            DE_TRACE_STATIC(TR_MIB, "Allready enabled\n");
            return WIFI_ENGINE_FAILURE_DEFER;
         }
         rate_ind = &rate_rx_falling;
         break;
      default:
         return WIFI_ENGINE_FAILURE;
   }

   size = sizeof(rate_ind_conf_t) + (supv_interval-1) * sizeof(m80211_std_rate_encoding_t);
   *rate_ind = (rate_ind_conf_t *)DriverEnvironment_Nonpaged_Malloc(size);
   if (*rate_ind == NULL)
      return WIFI_ENGINE_FAILURE_RESOURCES;

   return common_threshold_enable(thr_id, 
                                  *rate_ind,
                                  supv_interval, 
                                  thr_level, 
                                  WE_TRIG_THR_FALLING,
                                  cb);
}

int WiFiEngine_DisableRxRateMon(int32_t thr_id)
{
   DE_TRACE_STATIC(TR_MIB, "ENTRY\n");
   switch (thr_id)
   {
      case WE_TRIG_THR_FALLING:
         if (rate_rx_falling == NULL) {
            DE_TRACE_STATIC(TR_MIB, "Not enabled\n");
            return WIFI_ENGINE_FAILURE;
         }
         common_threshold_disable(rate_rx_falling, NRX_REASON_RM_BY_USER);
         DriverEnvironment_Nonpaged_Free(rate_rx_falling);
         rate_rx_falling = NULL;
         break;
      default:
         return WIFI_ENGINE_FAILURE;
   }
   return WIFI_ENGINE_SUCCESS;
}


/*********************
 *
 * Tx specific stuff
 *  (only falling)
 *
 *********************/

static rate_ind_conf_t *rate_tx_falling;
m80211_std_rate_encoding_t last_rate_tx;

void wei_ratemon_notify_data_cfm(m80211_std_rate_encoding_t rate)
{
   last_rate_tx = rate;
   if (rate_tx_falling != NULL)
      common_limit_check(rate_tx_falling, rate);
}

int WiFiEngine_GetTxRate(m80211_std_rate_encoding_t *rate)
{
   if (last_rate_tx == 0)
      return WIFI_ENGINE_FAILURE;
   *rate = last_rate_tx;
   return WIFI_ENGINE_SUCCESS;
}

int WiFiEngine_EnableTxRateMon(int32_t                    *thr_id,
                               uint32_t                   supv_interval, 
                               m80211_std_rate_encoding_t thr_level,
                               uint8_t                    dir, /* rising/falling */
                               we_ind_cb_t                cb)
{
   size_t size;
   rate_ind_conf_t **rate_ind;
   DE_TRACE_STATIC(TR_MIB, "ENTRY\n");

   switch (dir) {
      case WE_TRIG_THR_FALLING:
         if (rate_tx_falling != NULL) {
            DE_TRACE_STATIC(TR_MIB, "Allready enabled\n");
            return WIFI_ENGINE_FAILURE_DEFER;
         }
         rate_ind = &rate_tx_falling;
         break;
      default:
         return WIFI_ENGINE_FAILURE;
   }

   size = sizeof(rate_ind_conf_t) + (supv_interval-1) * sizeof(m80211_std_rate_encoding_t);
   *rate_ind = (rate_ind_conf_t *)DriverEnvironment_Nonpaged_Malloc(size);
   if (*rate_ind == NULL)
      return WIFI_ENGINE_FAILURE_RESOURCES;

   return common_threshold_enable(thr_id, 
                                  *rate_ind,
                                  supv_interval, 
                                  thr_level, 
                                  WE_TRIG_THR_FALLING,
                                  cb);
}

int WiFiEngine_DisableTxRateMon(int32_t thr_id)
{
   DE_TRACE_STATIC(TR_MIB, "ENTRY\n");
   switch (thr_id)
   {
      case WE_TRIG_THR_FALLING:
         if (rate_tx_falling == NULL) {
            DE_TRACE_STATIC(TR_MIB, "Not enabled\n");
            return WIFI_ENGINE_FAILURE;
         }
         common_threshold_disable(rate_tx_falling, NRX_REASON_RM_BY_USER);
         DriverEnvironment_Nonpaged_Free(rate_tx_falling);
         rate_tx_falling = NULL;
         break;
      default:
         return WIFI_ENGINE_FAILURE;
   }
   return WIFI_ENGINE_SUCCESS;
}


/*********************
 *
 * Init / Shutdown
 *
 *********************/

void WiFiEngine_RateMonInit(void)
{
   rate_tx_falling = NULL;
   rate_rx_falling = NULL;
   last_rate_tx = 0;
   last_rate_rx = 0;
}


void WiFiEngine_RateMonShutdown(void)
{
   if (rate_rx_falling != NULL) {
      common_threshold_disable(rate_rx_falling, NRX_REASON_SHUTDOWN);
      DriverEnvironment_Nonpaged_Free(rate_rx_falling);
      rate_rx_falling = NULL;
   }
   if (rate_tx_falling != NULL) {
      common_threshold_disable(rate_tx_falling, NRX_REASON_SHUTDOWN);
      DriverEnvironment_Nonpaged_Free(rate_tx_falling);
      rate_tx_falling = NULL;
   }
}

