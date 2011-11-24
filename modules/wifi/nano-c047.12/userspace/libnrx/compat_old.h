#ifndef __compat_old_h__
#define __compat_old_h__

/* $Id: compat_old.h 9954 2008-09-15 09:41:38Z joda $ */

/* This file will go away in the near future */

/* XXX this prototype doesn't belong here */
int nrx_cancel_custom_event_by_id(nrx_context, const char*, int32_t);

static inline int
o_nrx_enable_link_monitoring (nrx_context ctx,
                              int32_t *thr_id,
                              uint32_t interval,
                              uint8_t miss_thres,
                              nrx_callback_t cb,
                              void *cb_ctx)
{
   int ret;
   nrx_callback_handle handle;
   ret = nrx_enable_link_monitoring (ctx,
                                     thr_id,
                                     interval,
                                     miss_thres);
   if(ret != 0)
      return ret;
   
   handle = nrx_register_link_monitoring_callback (ctx, *thr_id, cb, cb_ctx);
   if(handle == 0)
      return ENOMEM;
   return 0;
}

static inline int
o_nrx_disable_link_monitoring (nrx_context ctx, int32_t thr_id)
{
   return nrx_cancel_custom_event_by_id (ctx, "MIBTRIG", thr_id);
}

static inline int
o_nrx_enable_conn_lost_notification (nrx_context ctx,
                                     nrx_conn_lost_type_t type,
                                     nrx_callback_t cb,
                                     void *cb_ctx)
{
   if(nrx_register_conn_lost_notification(ctx, type, cb, cb_ctx) == 0)
      return ENOMEM;
   return 0;
}

static inline int
o_nrx_disable_conn_lost_notification (nrx_context ctx)
{
   return nrx_cancel_custom_event_by_id (ctx, "CONNLOST", 0);
}

 
static inline int
o_nrx_enable_rssi_threshold (nrx_context ctx,
                             int32_t *thr_id,
                             int32_t rssi_thr,
                             uint32_t chk_period,
                             nrx_thr_dir_t dir,
                             nrx_detection_target_t type,
                             nrx_callback_t cb,
                             void *cb_ctx)
{
   int ret;
   nrx_callback_handle handle;

   ret = nrx_enable_rssi_threshold (ctx,
                                    thr_id,
                                    rssi_thr,
                                    chk_period,
                                    dir,
                                    type);
   if(ret != 0)
      return ret;
   
   handle = nrx_register_rssi_threshold_callback (ctx,
                                                  *thr_id,
                                                  cb,
                                                  cb_ctx);
   if(handle == 0)
      return ENOMEM;
   return 0;
}

static inline int
o_nrx_disable_rssi_threshold (nrx_context ctx,
                              int thr_id)
{
   int ret;

   ret = nrx_disable_rssi_threshold(ctx, thr_id);
   if(ret != 0)
      return ret;

   return nrx_cancel_custom_event_by_id(ctx, "MIBTRIG", thr_id);
}


static inline int
o_nrx_enable_per_threshold (nrx_context ctx,
                            int32_t *thr_id,
                            uint32_t chk_period,
                            uint32_t per_thr,
                            nrx_thr_dir_t dir,
                            nrx_callback_t cb,
                            void *cb_ctx)
{
   int ret;
   nrx_callback_handle handle;
   
   ret = nrx_enable_per_threshold (ctx,
                                   thr_id,
                                   chk_period,
                                   per_thr,
                                   dir);
   if(ret != 0)
      return ret;
   
   handle = nrx_register_per_threshold_callback (ctx,
                                                 *thr_id,
                                                 cb,
                                                 cb_ctx);
   if(handle == 0)
      return ENOMEM;
   return 0;
}
 
static inline int
o_nrx_disable_per_threshold (nrx_context ctx,
                             int thr_id)
{
   int ret;

   ret = nrx_disable_per_threshold(ctx, thr_id);
   if(ret != 0)
      return ret;

   return nrx_cancel_custom_event_by_id(ctx, "MIBTRIG", thr_id);
}

static inline int
o_nrx_enable_scan_notification (nrx_context ctx,
                                nrx_sn_pol_t sn_pol,
                                nrx_callback_t cb,
                                void *cb_ctx)
{
   nrx_callback_handle handle;
   handle = nrx_register_scan_notification_handler (ctx, sn_pol, cb, cb_ctx);
   if(handle == 0)
      return ENOMEM;
   return 0;
}
 
static inline int
o_nrx_disable_scan_notification (nrx_context ctx)
{
   return nrx_cancel_custom_event_by_id(ctx, "SCANNOTIFICATION", 0);
}

#endif /* __compat_old_h__ */
