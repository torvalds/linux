/* Copyright (C) 2007 Nanoradio AB */
/* $Id: init.c 15534 2010-06-03 15:49:16Z joda $ */

#include "nrx_priv.h"
#include <net/if.h>


#include <stdlib.h>
#include <stdarg.h> 
#include <unistd.h>

static nrx_debug_callback_t log_cb = NULL;

/** \defgroup LIB Library setup and management
 *
 * \brief The library setup and management functions should be used to
 *        initialize and handle communication with the Nanoradio Linux driver.
 *
 * The example below shows the general structure of using the NRX API when
 * pthreads are available. Note that this is just an example. A real
 * application have to handle concurrency protection etc.
 *
 * \code
 * int rssi_cb(nrx_context ctx, int operation, void *event_data,
 *             size_t event_data_size, void *user_data)
 * {
 *     int context = (int) user_data;
 *     printf("Callback invoked, user data is %d.\n", context);
 * }
 * 
 * void *dispatch(void* data)
 * {
 *     nrx_context ctx = (nrx_context) data;
 *     for(;;) {
 *         if(nrx_wait_event(ctx, 1000) == 0)
 *             nrx_next_event(ctx);
 *     } 
 *     return NULL;
 * }
 * 
 * int main()
 * {
 *     nrx_context ctx;
 *     pthread_t thread;
 *     void *status;
 *     int32_t thr_id;
 *     nrx_callback_handle handle;
 * 
 *     nrx_init_context(&ctx, NULL);
 *     pthread_create(&thread, NULL, dispatch, ctx);
 *
 *     nrx_enable_rssi_threshold(ctx, &thr_id, -50, 1000, NRX_THR_FALLING,
 *                               NRX_DT_DATA);
 *     nrx_register_rssi_threshold_callback(ctx, thr_id, rssi_cb, (void*) 5);
 *     pthread_join(thread, (void**) &status);
 *     nrx_free_context(ctx);
 * }
 * \endcode
 *
 * It is not necessary to use a thread library in order to use NRX API. If a
 * thread library is not available then the structure shown below could
 * be used instead.
 *
 * \code
 * int rssi_cb(nrx_context ctx, int operation, void *event_data,
 *             size_t event_data_size, void *user_data)
 * {
 *     int context = (int) user_data;
 *     printf("Callback invoked, user data is %d.\n", context);
 * }
 * 
 * int main()
 * {
 *     nrx_context ctx;
 *     int32_t thr_id;
 *     nrx_callback_handle handle;
 *
 *     nrx_init_context(&ctx, NULL);
 *
 *     nrx_enable_rssi_threshold(ctx, &thr_id, -50, 1000, NRX_THR_FALLING,
 *                               NRX_DT_DATA);
 *     nrx_register_rssi_threshold_callback(ctx, thr_id, rssi_cb, (void*) 5);
 *
 *     for(;;) {
 *         if(nrx_wait_event(ctx, 1000) == 0)
 *             nrx_next_event(ctx);
 *     }
 * 
 *     nrx_free_context(ctx);
 * }
 * \endcode
 */

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Set function to receive debug info</b>
 *
 * A callback can be registered by this function. It will be called 
 * by nrx_log_printf() each time debug macros are used, e.g. LOG() 
 * and ERROR().
 *
 * @param cb Callback function where debug information is to be sent. 
 *           When set to NULL all debugging is skipped.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
void nrx_set_log_cb(nrx_debug_callback_t cb)
{
   log_cb = cb;
}


/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Handle debug info</b>
 *
 * This function should only be used by macros such as LOG() and ERROR().
 * Input should have printf() formating. This is converted to a string, which 
 * is sent to a callback function.
 *
 * @param prio Priority of debug info. Low values are more important (e.g. fatal errors)
 *             than higher values (e.g. debug traces).
 * @param file File where e.g. error happend.
 * @param line Line where e.g. error happend.
 * @param fmt String formated as printf().
 *
 * @return Zero on success or an error code.
 */
int nrx_log_printf(int prio, const char *file, int line, const char *fmt, ...)
{
   char *msg;
   va_list ap;
   int n, size;

   if (log_cb == NULL)
      return 0;

   size = 100;                  /* should be enough mem */
   msg = (char*)malloc(size);
   if (msg == NULL)
      return -1;
   
   va_start(ap, fmt);
   n = vsnprintf (msg, size, fmt, ap);
   if (n >= size) {             /* failed: allocate more mem and try again */
      free(msg);
      size = n + 1;
      msg = (char*)malloc(size);
      if (msg == NULL) 
         goto exit;
      n = vsnprintf (msg, size, fmt, ap);
   }

   log_cb(prio, file, line, msg);
   free(msg);

  exit:
   va_end(ap);
   return n;
}


int
nrx_ioctl(nrx_context ctx, unsigned int cmd, void *data)
{
   struct ifreq ifr;
   
   ifr.ifr_data = (caddr_t)data;
   strlcpy(ifr.ifr_name, ctx->ifname, sizeof(ifr.ifr_name));

   if(ioctl(ctx->sock, cmd, &ifr) < 0)
      return errno;
   
   return 0;
}                 

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Perform a low-level ioctl</b>
 *
 * This is a low-level API function and usage directly from application should
 * be avoided if possible.
 *
 * @param ctx A valid nrx_context.
 * @param cmd The ioctl to perform.
 * @param param Parameters for the ioctl.
 *
 * @return Zero on success or an error code.
 */
int
nrx_nrxioctl(nrx_context ctx, unsigned int cmd, struct nrx_ioc *param)
{
   param->magic = NRXIOCMAGIC;
   param->cmd = cmd;

   return nrx_ioctl(ctx, SIOCNRXIOCTL, param);
}

/*!
 * @ingroup LIB
 * @brief <b>Initializes an nrxlib context</b>
 *
 * The context should be freed after use.
 *
 * @param ctx A pointer to the context to initialise.
 * @param ifname The interface name for accesses. 
 *               If NULL an attempt to discover the correct interface
 *               will be made.
 *
 * @retval zero on success
 * @retval ENOMEM if memory could not be allocated
 * @retval ENODEV if ifname is NULL, but no interface could be found
 * @retval "other errno" in case of socket open failure
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_init_context(nrx_context *ctx, const char *ifname)
{
   int sock;
   NRX_ASSERT(ctx);

   sock = socket(AF_INET, SOCK_DGRAM, 0);
   if(sock < 0)
      return errno;

   *ctx = malloc(sizeof(**ctx));
   if(*ctx == NULL) {
      return ENOMEM;
   }
   memset(*ctx, 0, sizeof(**ctx));

   (*ctx)->sock = sock;
   (*ctx)->wx_version = 0;
   _nrx_netlink_init(*ctx);

   if(ifname == NULL)
      nrx_find_ifname(*ctx, (*ctx)->ifname, sizeof((*ctx)->ifname));
   else
      strlcpy((*ctx)->ifname, ifname, sizeof((*ctx)->ifname));

   if((*ctx)->ifname[0] == '\0') {
      nrx_free_context(*ctx);
      return ENODEV;
   }
   
   nrx_get_wxconfig(*ctx);
   
   return 0;
}

/*!
 * @ingroup LIB
 * @brief <b>Free resources allocated to an nrxlib context</b>
 *
 * The context may not be used after this call.
 *
 * @param ctx The context to free.
 *
 * @return Returns nothing.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
void
nrx_free_context(nrx_context ctx)
{
   NRX_ASSERT(ctx);
   if(ctx->sock >= 0) {
      shutdown(ctx->sock, SHUT_RDWR); /* necessary for poll to exit */
      close(ctx->sock);
      ctx->sock = -1;
      _nrx_netlink_free(ctx);
   }
   free(ctx);
}

/*!
 * @ingroup LIB
 * @brief <b>Check version of Wireless Extensions</b>
 *
 * Checks which version of Wireless Extensions is used by the kernel.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param version The version to compare against.
 *
 * @retval Zero if the kernel version is exactly the same as the compared version.
 * @retval Negative if kernel version is less than the compared version.
 * @retval Positive if kernel version is greater than the compared version.
 */
int
nrx_check_wx_version(nrx_context ctx, unsigned int version)
{
   return ctx->wx_version - version;
}
