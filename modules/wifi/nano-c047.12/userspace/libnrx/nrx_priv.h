/* Copyright (C) Nanoradio AB */
/* $Id: nrx_priv.h 17821 2011-02-07 07:22:25Z peva $ */

#ifndef __nrx_priv_h__
#define __nrx_priv_h__

#ifdef HAVE_CONFIG_H
#include <string.h>
#include "config.h"
#endif

#include "nrx_lib.h"
#include "nanoioctl.h"
#ifndef IFNAMSIZ
#include <net/if.h>
#endif

#ifdef ANDROID
#include "../android.h"
#else
#include <sys/queue.h>
#endif

#ifdef _WIN32
#define NRX_DEBUG()
#define ERROR()
#define WARNING()
#define LOG()
#else
#define NRX_DEBUG(prio, args...) nrx_log_printf(prio, __FILE__, __LINE__, args)
#define ERROR(args...)   NRX_DEBUG(NRX_PRIO_ERR, args) /* Use same formating as printf() */
#define WARNING(args...) NRX_DEBUG(NRX_PRIO_WRN, args) /* Use same formating as printf() */
#define LOG(args...)     NRX_DEBUG(NRX_PRIO_LOG, args) /* Use same formating as printf() */
#endif

#define NRX_ASSERT(x)  do { if (!(x)) { ERROR("Assertion `%s' failed.", #x); exit(1); } } while(0)
#define NRX_CHECK(x)   do { if (!(x)) { ERROR("Check `%s' failed.", #x); return EINVAL; } } while(0)

/* linux seems to use an older version if sys/queue.h that doesn't
 * defined these */
#ifndef TAILQ_EMPTY
#define TAILQ_EMPTY(head)               ((head)->tqh_first == NULL)
#endif
#ifndef TAILQ_FIRST
#define TAILQ_FIRST(head)               ((head)->tqh_first)
#endif
#ifndef TAILQ_NEXT
#define TAILQ_NEXT(elm, field)          ((elm)->field.tqe_next)
#endif
#ifndef TAILQ_FOREACH
#define TAILQ_FOREACH(var, head, field)         \
	for ((var) = TAILQ_FIRST((head));       \
             (var) != NULL;                     \
             (var) = TAILQ_NEXT((var), field))
#endif

#ifdef ARRAY_SIZE
#define NRX_ARRAY_SIZE(X) ARRAY_SIZE(X)
#else
#define NRX_ARRAY_SIZE(X) (sizeof(X) / sizeof((X)[0]))
#endif

struct nrx_netlink_event {
   uint16_t type;
   uint32_t pid;
   uint32_t seq;
   nrx_callback_t handler;
   void *user_data;
   TAILQ_ENTRY(nrx_netlink_event) next;
};

struct nrx_we_event {
   uint16_t cmd;
   nrx_callback_t handler;
   void *user_data;
   TAILQ_ENTRY(nrx_we_event) next;
};

struct nrx_context_data {
   int sock;
   int netlink_sock;
   char ifname[IFNAMSIZ];
   int wx_version;

   TAILQ_HEAD(, nrx_netlink_event) netlink_handlers;
   TAILQ_HEAD(, nrx_we_event) we_handlers;
};

struct nrx_we_custom_data {
   char *var[8];
   char *val[8];
   size_t nvar;
};
   
struct nrx_event_helper_data {
   nrx_callback_t cb;
   void *helper_data;
   void *user_data;
};

struct nrx_debug_event {
      int level;
      char *message;
};

/* BEGIN GENERATED PROTOS */

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current transmitted fragment count</b>
  * 
  * This counter is incremented for each acknowledged
  * transmitted fragment. A frame that is not fragmented will 
  * be considered as a single "fragment" and increases the 
  * counter with 1.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_tx_fragment_count (nrx_context ctx,
                           uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current transmitted multicast frame count</b>
  * 
  * This counter is incremented for each successfully
  * transmitted multicast frame. Note that multicast in this case refers to
  * the 802.11 destination address, not to the ultimate destination
  * address.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_tx_mcast_frame_count (nrx_context ctx,
                              uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current failed transmitted frame count</b>
  * 
  * This counter is incremented when the transmit retry count
  * exceeds the retry limit.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_tx_failed_count (nrx_context ctx,
                         uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current transmit retry count</b>
  * 
  * This counter is incremented for each frame successfully
  * transmitted after one or more retransmissions.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_tx_retry_count (nrx_context ctx,
                        uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current multiple transmit retry count</b>
  * 
  * This counter is incremented for each frame successfully
  * transmitted after more than one retransmission.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_tx_multiple_retry_count (nrx_context ctx,
                                 uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current received duplicate frame count</b>
  * 
  * This counter is incremented for received frame duplicates.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rx_frame_duplicates_count (nrx_context ctx,
                                   uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current successful RTS/CTS count</b>
  * 
  * This counter is incremented when a CTS is received in
  * response to an RTS.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rts_success_count (nrx_context ctx,
                           uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current failed RTS/CTS count</b>
  * 
  * This counter is incremented when a CTS is not received in
  * response to an RTS.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rts_failure_count (nrx_context ctx,
                           uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current ACK failure count</b>
  * 
  * This counter is incremented when an ACK is not received when
  * expected.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_ack_failure_count (nrx_context ctx,
                           uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current received fragment count</b>
  * 
  * This counter is incremented for each successfully received
  * fragment. A frame that is not fragmented will be considered 
  * as a single "fragment" and increases the counter with 1.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rx_fragment_count (nrx_context ctx,
                           uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current received multicast frame count</b>
  * 
  * This counter is incremented for each successfully received
  * multicast frame (even though the frame is not intended for this STA).
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0). Note that multicast in this case refers to
  * the 802.11 destination address, not to the ultimate destination
  * address.
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rx_mcast_frame_count (nrx_context ctx,
                              uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current received FCS error count</b>
  * 
  * This counter is incremented when an FCS error is detected in
  * a received fragment. 
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rx_fcs_error_count (nrx_context ctx,
                            uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current transmitted frame count</b>
  * 
  * This counter is incremented for each successfully
  * transmitted frame.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_tx_frame_count (nrx_context ctx,
                        uint32_t *count);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current received undecryptable frame count</b>
  * 
  * This counter is incremented when a received frame could not
  * be decrypted.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param [in]  ctx   NRX context that was created by the call to
  *                    nrx_init_context().
  *
  * @param [out] count Pointer to the output buffer that will hold the
  *                    requested counter value.
  *
  *
  * @retval 0      on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rx_undecryptable_count (nrx_context ctx,
                                uint32_t *count);

/*! 
  * @internal
  * @brief Convert a channel number to a frequency
  *
  * @param [in]  channel    the channel number
  * @param [out] frequency  returned frequency in kHz
  *
  * @retval 0 on success
  * @retval EINVAL if channel could not be converted
  */
int
nrx_convert_channel_to_frequency (nrx_context ctx,
                                  nrx_channel_t channel,
                                  uint32_t *frequency);

/*! 
  * @internal
  * @brief Convert a frequency to a channel number
  *
  * @param [in] frequency  returned frequency in kHz
  * @param [out]  channel    the channel number
  *
  * @retval 0 on success
  * @retval EINVAL if channel could not be converted
  */
int
nrx_convert_frequency_to_channel (nrx_context ctx,
                                  uint32_t frequency,
                                  nrx_channel_t *channel);

/*!
  * @internal
  * @brief <b>Registers a handler for Wireless Extensions events</b>
  *
  * @param ctx The context.
  * @param cmd The WE event code to match. Zero matches all events.
  * @param handler The callback function to call when an event is received.
  * @param user_data Pointer to additional data passed to the callback
  *
  * @retval Zero on memory allocation failure.
  * @retval Non-zero integer representing the event handler.
  */
nrx_callback_handle
nrx_register_we_event_handler (nrx_context ctx,
                               uint16_t cmd,
                               nrx_callback_t handler,
                               void *user_data);

/*!
  * @internal
  * @brief <b>Cancels a registered handler for Wireless Extensions events</b>
  *
  * @param ctx The context.
  * @param handle A handle previously obtained from
  * nrx_register_we_event_handler.
  *
  * @retval Zero on success.
  * @retval EINVAL if a callback matching handle was not found.
  */
int
nrx_cancel_we_event_handler (nrx_context ctx,
                             nrx_callback_handle handle);

/*!
  * @internal
  * @brief <b>Registers a handler for Netlink messages</b>
  *
  * @param ctx       The context.
  * @param type      Matches the type field of the netlink message. 
  *                  Zero matches any type.
  * @param pid       Matches the pid field of the netlink message.
  *                  Zero matches any pid.
  * @param seq       Matches the seq field of the netlink message.
  *                  Zero matches any seq.
  * @param handler   The callback function to call when a matching 
  *                  message is received.
  * @param user_data Pointer to additional data passed to the callback
  *
  * @return An integer representing the message handler.
  */
nrx_callback_handle
nrx_register_netlink_handler (nrx_context ctx,
                              uint16_t type,
                              uint32_t pid,
                              uint32_t seq,
                              nrx_callback_t handler,
                              void *user_data);

/*!
  * @internal
  * @brief <b>Cancels a registered handler for Netlink messages</b>
  *
  * @param ctx The context.
  * @param handle A handle previously obtained from
  * nrx_register_netlink_handler.
  *
  * @return Nothing.
  */
void
nrx_cancel_netlink_handler (nrx_context ctx,
                            nrx_callback_handle handler);

void
_nrx_netlink_init (nrx_context ctx);

void
_nrx_netlink_free (nrx_context ctx);

/*!
  * @internal
  * @brief <b>Create a socket suitable for netlink access</b>
  *
  * @retval non-negative a socket
  * @retval negative a negative errno number
  *
  * @note This functions does not follow the convention of taking a
  * context, and returning zero or errno number.
  */
int
_nrx_create_netlink_socket (void);

/*!  
  * @internal
  * @brief Convert hex string in str into buffer in buf. 
  */
ssize_t
nrx_string_to_binary (const char *str,
                      void *buf,
                      size_t size);

nrx_callback_handle
nrx_register_custom_event (nrx_context ctx,
                           const char *type,
                           uint32_t id,
                           nrx_callback_t handler,
                           void *user_data);

int
nrx_cancel_custom_event (nrx_context ctx,
                         nrx_callback_handle handle);

int
nrx_cancel_custom_event_by_id (nrx_context ctx,
                               const char *type,
                               int32_t id);

nrx_callback_handle
nrx_register_mib_trigger_event_handler (nrx_context ctx,
                                        uint32_t id,
                                        nrx_callback_t handler,
                                        void *user_data);

int
nrx_register_mib_trigger (nrx_context ctx,
                          int32_t *trig_id,
                          char *mib_id,
                          int32_t gating_trig_id,
                          uint32_t supv_interval,
                          int32_t level,
                          uint8_t dir,
                          uint16_t event_count,
                          uint16_t trigmode);

int
nrx_cancel_mib_trigger_event_handler (nrx_context ctx,
                                      nrx_callback_handle handle);

int
nrx_del_mib_trigger (nrx_context ctx,
                     uint32_t trig_id);

/*!
  * @internal
  */
int
nrx_get_ibss_beacon_period (nrx_context ctx,
                            uint16_t *period);

/*!
  * @internal
  */
int
nrx_set_ibss_dtim_period (nrx_context ctx,
                          uint8_t period);

/*!
  * @internal
  */
int
nrx_get_ibss_dtim_period (nrx_context ctx,
                          uint8_t *period);

/*!
  * @internal
  */
int
nrx_get_ibss_atim_window (nrx_context ctx,
                          uint16_t *window);

int
nrx_find_ifname (nrx_context ctx,
                 char *ifname,
                 size_t len);

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
void
nrx_set_log_cb (nrx_debug_callback_t cb);

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
int
nrx_log_printf (int prio,
                const char *file,
                int line,
                const char *fmt,
                ...);

int
nrx_ioctl (nrx_context ctx,
           unsigned int cmd,
           void *data);

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
nrx_nrxioctl (nrx_context ctx,
              unsigned int cmd,
              struct nrx_ioc *param);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Retrieve the value of a MIB</b>
  *
  * This is a low-level API function and usage directly from an application should
  * be avoided if possible.
  *
  * @param ctx A valid nrx_context.
  * @param mib_id The MIB id to retrieve.
  * @param buf A buffer where the MIB value will be stored.
  * @param len The size of buf.
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_FOR_TESTING -->
  */
int
nrx_get_mib_val (nrx_context ctx,
                 const char *mib_id,
                 void *buf,
                 size_t *len);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Set the value of a MIB</b>
  *
  * This is a low-level API function and usage directly from an application should
  * be avoided if possible.
  *
  * @param ctx A valid nrx_context.
  * @param mib_id The MIB id to set.
  * @param buf A buffer where the MIB value is stored.
  * @param len The size of buf.
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_FOR_TESTING -->
  */
int
nrx_set_mib_val (nrx_context ctx,
                 const char *mib_id,
                 const void *buf,
                 size_t len);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Retrieve the current join timeout value</b>
  *
  * @param ctx A valid nrx_context.
  * @param value A pointer where the join timeout value given in beacon
  *        intervals will be stored.
  *
  * @return Zero on success or an error code.
  */
int
nrx_get_join_timeout (nrx_context ctx,
                      uint32_t *value);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Disable heartbeat</b>
  *
  * The heartbeat functionality is disabled an no more heartbeat signals will be
  * sent from the target.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_disable_heartbeat (nrx_context ctx);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Enable heartbeat</b>
  *
  * The target will send heartbeat signals to the host. This allows
  * the driver to notice if the target stops working. If the heartbeat stops,
  * the driver will make a dump of the complete contents of the core RAM.
  * This function is useful during development, and allows Nanoradio R&D to get
  * a view of exactly what happened in the chip just before the system failure.
  * The core dump can be used for troubleshooting and analysis.
  *
  * Any core files will be put in /proc/drivers/\<interface name\>/core/ 
  * and will be named 0, 1 etc.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @param hb_period Heartbeat period in ms. Current implementation
  * will round up to a multiple of 1000 (i.e. resolution in seconds). Minimum 
  * value is 1 ms (i.e. 1 second) and maximum is 35 minutes (2100000 ms). 0 will 
  * reset to default value (60 sec).
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_enable_heartbeat (nrx_context ctx,
                      uint32_t hb_period);

/*!
  * @internal
  * @brief Set max number of coredumps retained
  *
  * This limits the number of target coredumps retained by the driver.
  *
  * @param [in] ctx NRX context that was created by the call to nrx_init_context().
  * @param [in] count Number of coredumps to retain.
  *
  * @retval Zero on success.
  * @retval Errno on failure.
  *
  * <!-- NRX_API_FOR_TESTING -->
  */
int
nrx_set_maxcorecount (nrx_context ctx,
                      uint32_t count);

/*!
  * @internal
  * @ingroup MISC
  * @brief Get max number of coredumps retained
  *
  * This gets the current coredumps limit.
  *
  * @param [in] ctx NRX context that was created by the call to nrx_init_context().
  * @param [out] count Number of coredumps to retain.
  *
  * @retval Zero on success.
  * @retval Errno on failure.
  *
  * <!-- NRX_API_FOR_TESTING -->
  */
int
nrx_get_maxcorecount (nrx_context ctx,
                      uint32_t *count);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Retrieve the value of a string MIB</b>
  *
  * This is a low-level API function and usage directly from application should
  * be avoided if possible.
  *
  * @param ctx A valid nrx_context.
  * @param id The MIB id to retrieve.
  * @param res A buffer where the MIB value will be stored; Zero terminated
  * @param len The size of res.
  *
  * @return Zero on success or an error code.
  */
int
nrx_get_mib_string (nrx_context ctx,
                    const char *id,
                    char *res,
                    size_t len);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Configure ARP filtering in FW</b>
  *
  * To avoid that ARP requests wake up host during power save, it is
  * possible to let fw handle these requests to different degrees.
  *
  * @param mode What type of filter to use.
  * @param ip My ip address.
  * @return
  * - 0 on success
  * - error code on failure
  */
int
nrx_conf_arp_filter (nrx_context ctx,
                     nrx_arp_policy_t mode,
                     in_addr_t ip);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Antenna diversity</b>
  *
  * Settings to enable or disable antenna diversity.
  *
  * PRELIMINARY : Antenna diversity is not supported in the current
  *               implementation. This function currently does nothing.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param antenna_mode Specifies the antennas to use. Possible modes are:
  *                  -  0: use default setting (currently antenna #1).
  *                  -  1: use antenna #1.
  *                  -  2: use antenna #2.
  *                  -  3: use antenna diversity, i.e. both antenna #1 and #2.
  * @param rssi_thr RSSI threshold used by the antenna selection 
  *                algorithm. This is only used in mode 3. Minimum value is -120 dBm
  *                and maximum is 0 dBm.
  *
  * @retval Zero on success.
  * @retval EINVAL on invalid arguments.
  */
int
nrx_antenna_diversity (nrx_context ctx,
                       uint32_t antenna_mode,
                       int32_t rssi_thr);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Set the driver activity timeout value</b>
  *
  * This configures the time of inactivity to elapse before the driver 
  * will consider itself inactive. 
  * The inact_check_interval parameter defines how exact the timeout
  * will be. If the inact_check_interval is very short then the timeout
  * will be very accurate but the host CPU load will be high since the
  * timer will trigger frequently. Longer values for
  * inact_check_interval will decrease CPU load but reduce timeout
  * accuracy. Driver inactivity will never be signaled before the
  * timeout has passed, but it may be signaled after. The worst case
  * lag is timeout + inact_check_interval.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param timeout Activity timeout value in milliseconds. The driver is considered
  *        inactive if no rx/tx activity has occurred within the timeout period.
  *        A 0 value disabled the inactivity indications. Maximum value is 2^30-1.
  * @param inact_check_interval The activity timeout check interval. Range 1 to 2^30-1.
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_activity_timeout (nrx_context ctx,
                          uint32_t timeout,
                          uint32_t inact_check_interval);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Crash firmware</b>
  *
  * Issuing this command will cause the firmware to crash. This is used
  * for debugging purposes.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_fw_suicide (nrx_context ctx);

/*!
  * @internal
  * @ingroup PS
  * @brief <b>Get power save mode</b>
  *
  * Detects whether or not power save is enabled.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param ps_enabled Where the result is stored. This is 1 when power
  *        save is enabled and 0 when it's disabled.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_get_ps_mode (nrx_context ctx,
                 nrx_bool *ps_enabled);

/*!
  * @internal
  * @ingroup PS
  * @brief <b>Set the device listen interval</b>
  *
  * The device listen interval can be changed dynamically when connected.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @param listen_every_beacon Choose 1 to listen on every single beacon 
  *        sent by the AP or 0 to use the beacon interval agreed with the AP (default). 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_dev_listen_interval (nrx_context ctx,
                             nrx_bool listen_every_beacon);

/*! 
  * @internal
  * @brief Formats a buffer on stdout.
  *
  * @param data pointer to buffer to format
  * @param len size of data
  * @param prefix string prepended to each output line
  */
void
nrx_printbuf (const void *data,
              size_t len,
              const char *prefix);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set the maximum allowed transmission power levels</b>
  *
  * The maximum transmission power levels are set for OFDM (802.11g)
  * and/or BPSK/QPSK/CCK (802.11b) rates.
  * The levels are specified by setting attenuation levels
  * from the default maximum. The maximum transmission power overrides power
  * limits announced in 802.11d, MIN(max_tx_power, 802.11d) will be used for
  * transmission. Attenuation levels that exceed the hardware maximum
  * power limit results in an effective maximum transmission power of 0 dB.
  *
  * The power levels for OFDM and QPSK are independent. The output power
  * may therefore change as transmission rates are changed.
  *
  * The maximum transmission power levels can be changed dynamically when
  * associated.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param qpsk_attenuation_db Tx attenuation in dB from the maximum allowed
  *        value. Valid values are 0-19 dB. This will limit QPSK transmission
  *        power to the hardware maximum (21dBm) minus this attenuation value.
  * @param ofdm_attenuation_db Tx attenuation in dB from the maximum allowed
  *        value. Valid values are 0-19 dB. This will limit OFDM transmission
  *        power to the hardware maximum (18dBm) minus this attenuation value.
  * @return 
  * - 0 on success.
  * - EINVAL on invalid attenuation values.
  */
int
nrx_set_max_tx_power (nrx_context ctx,
                      uint8_t qpsk_attenuation_db,
                      uint8_t ofdm_attenuation_db);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the currently used channel</b>
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param channel Where the currently used channel is stored. 
  *
  * @return 
  * - 0 on success.
  * - EINVAL when not associated.
  */
int
nrx_get_curr_channel (nrx_context ctx,
                      nrx_channel_t *channel);

/*!
  * @internal
  * @brief <b>Check if a rate is supported by the system</b>
  * @param rate The rate to be investigated
  * @retval 1 on success
  * @retval 0 if rate is not supported
  */
int
_nrx_is_supp_rate (nrx_rate_t rate);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set the adaptive TX rate mode</b>
  * 
  * The adaptive TX rate mode defines if and how the transmission rate
  * will change to adapt to local radio conditions. The initial rate
  * is the preferred rate. Tx power will be restricted by the value set by
  * nrx_set_max_tx_power().
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param modes Bitmask that defines the adaptive TX rate modes to be used.
  *              The mode defines the transmission rate adaptation strategy. 
  *              Several modes can be enabled by OR-ing them together.
  * @param initial_rate The initial rate for the rate adaptation strategy.
  *              A change to the initial_rate parameter will only be effected
  *              upon reassociation.
  * @param penalty_rates The rates that should be used by the tx rate adaption
  *                      algorithm. The initial rate must be included in this list.
  *                      If this parameter is null then all the rates may 
  *                      be used. Only the penalty rates that matches
  *                      the supported rates for the current association will
  *                      be used. Should no match exist, this list will be 
  *                      ignored. For older hardware, the highest supported 
  *                      rate for the current association will be used although not  
  *                      included in this list (affects baseband NRX701A, NRX701B, 
  *                      and radio NRX702, NRX510A).
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_adaptive_tx_rate_mode (nrx_context ctx,
                               nrx_adaptive_tx_rate_mode_t modes,
                               nrx_rate_t initial_rate,
                               const nrx_rate_list_t *penalty_rates);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set the TX retry limits per rate</b>
  *
  * Specify the number of transmission retries before discarding a frame.
  * The retry limit is specified per rate. First the number of retries of
  * the current original rate will be performed. Then the rate is stepped
  * down and the corresponding entry in the subsequent list will be used.
  * The total number of retries will be limited by the retry limit set by
  * set_tx_retry_limits().
  *
  * Example:
  * original_rate_retries is set to 3 for 54M, 12M and 5M.
  * subsequent_rate_retries is set to 1 for 54M, 12M and 5M.
  * The rate sequence before discarding a frame will be 54M, 54M, 54M, 54M, 12M,
  * 5.5M.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param original_rate_retries The number of retries performed at a certain 
  *                              original rate. Each position in this list is 
  *                              matched with the corresponding position in the 
  *                              "rates" parameter. Hence, both lists must have the 
  *                              same length. Maximum value for each rate is
  *                              255. NULL will avoid setting this value.
  * @param subsequent_rate_retries After original rate retries, the rate is stepped 
  *                                down and the list of subsequent retries is used. 
  *                                Each position in this list is matched with the 
  *                                corresponding position in the "rates" parameter. 
  *                                Hence, both lists must have the same length.
  *                                Maximum value for each rate is 255. NULL 
  *                                will avoid setting this value.
  * @param rates The rates for which rate limits will be set. The retry limit
  *              will only be updated for the rates included in the list. 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_tx_retry_limit_by_rate (nrx_context ctx,
                                const nrx_retry_list_t *original_rate_retries,
                                const nrx_retry_list_t *subsequent_rate_retries,
                                const nrx_rate_list_t *rates);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Lock the TX rate that the device may use</b>
  *
  * Locking a rate forces the device to only use this particular rate
  * when transmitting. The transmission rate adaptation feature will
  * also be forced to use this rate only. Note that a succesful locking
  * of a rate requires a valid rate value supported by the AP. Locking of
  * unsupported rates results in undefined behaviour.
  * 
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param rate The rate that will be used by the device.
  * @return 
  * - 0 on success.
  * - EOPNOTSUPP if the rate is not present in the AP supported rates list.
  * - EINVAL on invalid arguments.
  */
int
nrx_lock_rate (nrx_context ctx,
               nrx_rate_t rate);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Unlock the TX rate that the device may use</b>
  *
  * Unlocks a locked TX rate. The device transmits using the rates that both
  * the device and the AP supports as it sees fit. The transmission will
  * start at the initial rate.
  *
  * If rate adaption was enabled when nrx_lock_rate() was called it will still
  * be enabled when nrx_unlock_rate() is called.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_unlock_rate (nrx_context ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set the operational rates that the device may use</b>
  *
  * Specify the set of basic and extended rates that the device should
  * support. The default is to support all 802.11b and 802.11g rates.
  * This overrides the registry setting.
  *
  * The rates set will affect association in two ways. 
  *
  * The rates specified in this call will be added to the basic rates
  * advertised by the AP and used as operational rates in the
  * association request with the AP.
  * 
  * Rates specified as basic (high bit set), will guard against
  * association with an AP (BSS) or STA (IBSS) which does not support
  * that rate.
  *
  * Changing the rates during an active association does not change
  * the rates used for that association, the change takes effect when
  * the next association is started.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param rates The list of rates that will be used by the device.
  *
  * @retval 0 on success.
  * @retval EINVAL on invalid arguments.
  */
int
nrx_set_op_rates (nrx_context ctx,
                  const nrx_rate_list_t *rates);

/*!
  * @internal
  * @ingroup MISC
  * @brief <b>Reassociate with the current AP</b>
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_reassociate (nrx_context ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set allowed channels</b>
  *
  * The channel_list parameter specifies the channels that should be allowed to
  * use. The channels used vary in different regions of the world.
  * In most of Europe thirteen channels are allowed. In Japan also channel 14
  * may be used.
  * Note that nrx_set_region_code() can also be used to change the channels
  * used.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param ch The list of allowed channels.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_channel_list (nrx_context ctx,
                      const nrx_ch_list_t *ch);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Reset traffic filter discard counters</b>
  *
  * Reset the statistic counters for filtered frames. 
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_reset_traffic_filter_counters (nrx_context ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get a traffic filter discard counter</b>
  *
  * The device keeps a counter for the number of frames discarded
  * by the traffic filter.
  *
  * The maximum value is 2^32-1, then it will wrap around
  * (i.e. restart from 0).
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param cntr Pointer to the output buffer that will hold the requested
  *             counter value (in host byteorder).
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_get_traffic_filter_counters (nrx_context ctx,
                                 uint32_t *cntr);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable link monitoring</b>
  * 
  * The device monitors received beacon frames when link monitoring is
  * enabled.  When the specified percentage of beacon frames within a
  * monitoring interval has been missed (the link has failed), a
  * trigger is generated. It is possible to register a callback on this
  * trigger.
  *
  * As an example, setting interval to 5 (beacons) and
  * miss_thres to 80 (%), will notify the caller when 4 out of 5
  * beacon frames have been missed. When the device is in power
  * save mode the interval will be scaled by the DTIM period
  * (nrx_conf_ps() parameter listen_interval) - the interval
  * defines the number of beacons that was expected to be received.
  *
  * Link monitoring is only enabled when the device is associated
  * and it only monitors beacons for the associated access point.
  * 
  * At handover to another AP, an automatic adjustment of internal timers
  * to the new AP will be done such that the number of missed beacons 
  * always will be constant.
  *
  * There exists a default link monitoring in the firmware that may terminate a poor link, 
  * which will conflict with this command, see parameters for more details. The internal 
  * termination of the link can be enabled/disabled. When disabled the user himself has 
  * the responsibility of terminating a poor link as it will no longer be done automatically by 
  * drivers/firmware. To disable the internal default link monitoring, write
  * \code
  * echo 0 > /proc/driver/ifname/config/link-monitoring
  * \endcode
  * where ifname is the interface name (e.g. eth1). To enable, write
  * \code
  * echo 1 > /proc/driver/ifname/config/link-monitoring
  * \endcode
  * 
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id output buffer. This identifies the threshold
  *               so that several thresholds can be defined. 
  *               The id value is filled in by the function call.
  * @param interval The link monitoring interval in number of beacons.  Should several
  *                 link monitoring triggers with different intervals be registered the
  *                 shortest interval will be choosen (necessary due to firmware 
  *                 limitations).
  *                 Minimum value is 1 beacon. Maximum value is 2^32-1, but will most
  *                 likely result in undefined behavior. Only values up to 10 will work
  *                 under all circumstances.  Consider the following firmware limits
  *                 that affect this parameter
  *                 - When enabled, the internal firmware link monitoring has a fix limit of 20 consecutive 
  *                   missed beacons thereafter the link will be considered terminated. 
  *                   As link monitoring is done only when associated, it will be halted at this 
  *                   point. To be guaranteed that link monitoring reaches 100% before it is halted, 
  *                   this parameter must be set to half the firmware limit or less, i.e. max 10. 
  *                   Alternatively, the internal termination of the link can be disabled with the
  *                   implications mentioned above. 
  *                 - Firmware has a max-timeout limitation of roughly 35 minutes. Although unlikely,
  *                   the 802.11 protocol allows an AP to have an interval between beacons of
  *                   maximally 65 seconds (2^16-1 ms). Hence, maximum 32 beacons can be 
  *                   guaranteed not to overflow the internal timer. Should an overflow occur, the
  *                   behavior is undefined.
  *                   
  * @param miss_thres The miss threshold in percent. Range 1-99.
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - EBUSY when previous link monitoring have not been disabled first. 
  */
int
nrx_enable_link_monitoring (nrx_context ctx,
                            int32_t *thr_id,
                            uint32_t interval,
                            uint8_t miss_thres);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable link monitoring</b>
  *
  * The link monitoring feature is disabled and no further notifications
  * of link failure will be made.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id identifying the threshold trigger that should
  *               be disabled. 
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments or non-existing thr_id.
  */
int
nrx_disable_link_monitoring (nrx_context ctx,
                             int32_t thr_id);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register link monitoring callback</b>
  * 
  * This will register a callback for the link monitoring triggers, see
  * nrx_enable_link_monitoring for further details.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id. This identifies the threshold
  *              so that several thresholds can be defined. 
  *              The id value is obtained from nrx_enable_link_monitoring.
  * @param cb The callback function that is to be invoked by threshold notifications.
  *           The callback is invoked with operation NRX_CB_TRIGGER on a 
  *           successful notification whereupon event_data will be a pointer 
  *           to a nrx_event_mibtrigger structure which contains further 
  *           information. When the threshold is cancelled cb is called 
  *           with operation NRX_CB_CANCEL and event_data set to NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * 
  * @return A handle to a callback (an unsigned integer type). The only
  * use for this is to pass it to nrx_cancel_link_monitoring_callback
  * to cancel the callback.
  * @retval Zero on memory allocation failure
  * @retval Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_link_monitoring_callback (nrx_context ctx,
                                       int32_t thr_id,
                                       nrx_callback_t cb,
                                       void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel link monitoring callback</b>
  * 
  * This will cancel a callback for the link monitoring triggers.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle Callback handle obtained from nrx_register_link_monitoring_callback.
  * The handle will no longer be valid after this call.
  *
  * @return
  *  - 0 on success
  *  - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_link_monitoring_callback (nrx_context ctx,
                                     nrx_callback_handle handle);

/*!
  * @internal
  * @brief <b>Disable all link supervision features in the NIC</b>
  *
  * This will stop firmware from doing supervison of the link. 
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @return
  *  - 0 on success,
  *  - an error code on failure.
  */
int
nrx_disable_link_supervision (nrx_context ctx);

/*!
  * @internal
  * @brief <b>Configuration of roundtrip criteria for link supervision</b>
  *
  * This will test the link by attempting to send a roundtrip
  * message. The feature will only be used for a few AP configurations
  * where the link status can not be detected unless a roundtrip
  * message is sent.
  *
  * The feature will monitor traffic and when enough packets are
  * transmitted without any reply, the link will be determined
  * faulty. When an AP is connected, the monitoring is initially
  * passive and it is assumed that other traffic (e.g. DHCP) will
  * generate enough statistics to determine the link status. After the
  * passive period, the feature will inject its own packets to improve
  * the statistical confidence.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @param roundtrip_fail_limit Should no reply have been received
  *        after this number of transmitted messages, the link is 
  *        determined faulty. 0 will disable the roundtrip feature.
  * 
  * @param silent_intervals Number of intervals to wait before this
  *        feature inject its own packets to the recently connected
  *        AP. Each interval is at least 100 ms and could be
  *        considerably longer in power save mode. Should this be set
  *        to 0, there will be no silent period upon which packets are
  *        transmitted immediately after an AP is connected. Should
  *        this be 0xffffffff, the feature will work in passive mode
  *        only and not inject any own packets.
  *
  * @return
  *  - 0 on success,
  *  - an error code on failure.
  */
int
nrx_conf_link_supervision_roundtrip (nrx_context ctx,
                                     uint32_t roundtrip_fail_limit,
                                     uint32_t silent_intervals);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable connection failure notification</b>
  *
  * Notify the caller when a connection or a connection attempt has
  * failed. The caller can elect to be notified on distinct association
  * failures : authentication failures, association failures and
  * disassociations. The callback will be invoked every time (until disabled)
  * the condition is met.
  *
  * This function can be called several times, e.g. with different notification policies for 
  * different callbacks.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param type Bitmask that defines the notification reasons to use.
  * @param cb Callback that is invoked to notify the caller that
  *           the a connection or connection attempt has failed.
  *           The callback is invoked with operation NRX_CB_TRIGGER on
  *           a successful notification. It will be passed a pointer to
  *           struct nrx_conn_lost_data, which identifies the reason
  *           and type of the disconnect event, for event_data. 
  *           nrx_conn_lost_data also contains the BSSID of the 
  *           disconnecting entity. If the disconnect decision was made
  *           by the driver or firmware then the BSSID field will contain
  *           the MAC address of the device.
  *           Memory for event_data will be freed immediately
  *           after the callback has returned.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * @return 
  * - Zero on memory allocation failure
  * - Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_conn_lost_notification (nrx_context ctx,
                                     nrx_conn_lost_type_t type,
                                     nrx_callback_t cb,
                                     void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable connection failure notification</b>
  *
  * The connection failure notification feature is disabled. No further
  * connection failure notifications will be made.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  *        nrx_register_conn_lost_notification.
  *        The handle will no longer be valid after this call.
  * 
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_conn_lost_notification (nrx_context ctx,
                                   nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable connection incompatible notification</b>
  *
  * Notify the caller when a connection is detected to be
  * incompatible. The connection will still remain valid, no
  * disconnection will be done, but reconfiguration is neccessary for
  * traffic to pass on the connection. The callback will be invoked
  * every time (until disabled) the condition is met.
  *
  * This function can be called several times. Calling this function
  * will not unregister any other callbacks.
  *
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param cb Callback that is invoked to notify the caller that
  *           the connection is invalid. The callback is invoked with
  *           operation NRX_CB_TRIGGER on a successful notification. It
  *           will be passed a NULL pointer to struct
  *           nrx_conn_lost_data.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * @return 
  * - Zero on memory allocation failure
  * - Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_conn_incompatible_notification (nrx_context ctx,
                                             nrx_callback_t cb,
                                             void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable connection incompatible notification</b>
  *
  * The connection incompatible notification feature is disabled. No
  * further connection incompatible notifications will be made.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  *        nrx_register_conn_incompatible_notification.  The handle
  *        will no longer be valid after this call.
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_conn_incompatible_notification (nrx_context ctx,
                                           nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set an SNR threshold trigger</b>
  *
  * The callback is invoked every time (until disable) the Signal to noise
  * ratio passes the defined threshold in the direction specified.
  * Several thresholds can be defined. They are identified by the 
  * thr_id parameter. There is a dynamic limit to the number of
  * triggers that can exist in the system so this call may fail if the
  * limit would be passed. The limit depends on the available memory on the host.
  * 
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id output buffer. This identifies the threshold
  *              so that several thresholds can be defined. 
  *              The id value is filled in by the function call.
  *              
  * @param snr_thr The SNR threshold in dB. Minimum value is 0 and maximum is 
  *        40, which should be sufficient on current hardware.
  * @param chk_period The SNR threshold check period in milliseconds. The minimum value is 
  *        100 and maximum supported time is 35 minutes (2100000 ms). The SNR threshold 
  *        will be compared with the current SNR using this period.
  * @param dir Bitmask defining the trigger directions. The callback can be
  *            triggered when the value rises above or falls below the
  *            threshold, or both.
  * @param type Bitmask that defines if the threshold trigger should apply to the SNR
  *             of beacon or data frames.
  *                    
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - TODO if no more triggers can be created.
 */
int
nrx_enable_snr_threshold (nrx_context ctx,
                          int32_t *thr_id,
                          uint32_t snr_thr,
                          uint32_t chk_period,
                          nrx_thr_dir_t dir,
                          nrx_detection_target_t type);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable an SNR threshold</b>
  *
  * The specified SNR threshold trigger is disabled. The specified
  * trigger in the device is cleared and no further notifications from
  * this trigger will be made.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id identifying the threshold trigger that should
  *               be disabled.
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments or non-existing thr_id.
  */
int
nrx_disable_snr_threshold (nrx_context ctx,
                           int32_t thr_id);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register an SNR threshold callback</b>
  *
   * This will register a callback for a SNR threshold, see
   * nrx_enable_snr_threshold for further details. Several callbacks
  * can be registered to the same threshold.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id. This identifies the threshold
  *              so that several thresholds can be defined. 
  *              The thr_id value is obtained from nrx_enable_snr_threshold.
  * @param cb The callback function that is to be invoked by threshold notifications.
  *           The callback is invoked with operation NRX_CB_TRIGGER on a 
  *           successful notification whereupon event_data will be a pointer 
  *           to a nrx_event_mibtrigger structure which contains further 
  *           information. When the threshold is cancelled cb is called 
  *           with operation NRX_CB_CANCEL and event_data set to NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * 
  * @return A handle to a callback (an unsigned integer type). The only
  * use for this is to pass it to nrx_cancel_snr_threshold_callback
  * to cancel the callback.
  * @retval Zero on memory allocation failure.
  * @retval Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_snr_threshold_callback (nrx_context ctx,
                                     uint32_t thr_id,
                                     nrx_callback_t cb,
                                     void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel an SNR threshold callback</b>
  *
  * This will cancel the callback for a SNR threshold.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle Callback handle obtained from nrx_register_snr_threshold_callback.
  *
  * @return
  *  - 0 on success
  *  - EINVAL on invalid arguments, e.g. the callback is not registered.
  */
int
nrx_cancel_snr_threshold_callback (nrx_context ctx,
                                   nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set an RSSI threshold trigger</b>
  *
  * The callback is invoked every time (until disabled) the RSSI ratio passes
  * the defined threshold in the direction specified.  Several thresholds
  * can be defined. They are identified by the thr_id
  * parameter. There is a dynamic limit to the number of
  * triggers that can exist in the system so this call may fail if the
  * limit would be passed. The limit depends on the available memory on the host.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id output buffer. This identifies the threshold
  *              so that several thresholds can be defined.  
  *              The id value is filled in by the function call.
  *              
  * @param rssi_thr The RSSI threshold in dBm (signed). Useful RSSI thresholds lie
  *                 between 0 and -120.
  * @param chk_period The RSSI check period in milliseconds. The minimum value is 100 ms and 
  *        maximum supported time is 35 minutes (2100000 ms). The RSSI threshold will be compared with 
  *        the current RSSI value using this period.
  *
  * @param dir Bitmask defining the trigger directions. The callback can be triggered
  *            when the value rises above or falls below the threshold, or both.
  * @param type Defines if the threshold trigger should apply to the RSSI
  *             of beacon or data frames.
  *                    
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - ENOMEM if no more triggers can be created.
 */
int
nrx_enable_rssi_threshold (nrx_context ctx,
                           int32_t *thr_id,
                           int32_t rssi_thr,
                           uint32_t chk_period,
                           nrx_thr_dir_t dir,
                           nrx_detection_target_t type);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable an RSSI threshold</b>
  *
  * The specified RSSI threshold trigger is disabled. The specified
  * trigger in the device is cleared and no further notifications from
  * this trigger will be made.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id identifying the threshold trigger that should
  *               be disabled.
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments or non-existing thr_id.
  */
int
nrx_disable_rssi_threshold (nrx_context ctx,
                            int thr_id);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register an RSSI threshold callback</b>
  *
  * This will register a callback for a RSSI threshold, see
  * nrx_enable_rssi_threshold for further details. Several callbacks
  * can be registered to the same threshold.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id. This identifies the threshold
  *               so that several thresholds can be defined. 
  *               The thr_id value is obtained from nrx_enable_rssi_threshold.
  * @param cb The callback function that is to be invoked by threshold notifications.
  *           The callback is invoked with operation NRX_CB_TRIGGER on a 
  *           successful notification whereupon event_data will be a pointer 
  *           to a nrx_event_mibtrigger structure which contains further 
  *           information. When the threshold is cancelled cb is called 
  *           with operation NRX_CB_CANCEL and event_data set to NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * 
  * @return A handle to a callback (an unsigned integer type). The only
  * use for this is to pass it to nrx_cancel_rssi_threshold_callback
  * to cancel the callback.
  * @retval Zero on memory allocation failure.
  * @retval Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_rssi_threshold_callback (nrx_context ctx,
                                      uint32_t thr_id,
                                      nrx_callback_t cb,
                                      void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel an RSSI threshold callback</b>
  *
  * This will cancel the callback for a RSSI threshold.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle Callback handle obtained from nrx_register_rssi_threshold_callback.
  *
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_rssi_threshold_callback (nrx_context ctx,
                                    nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set the RSSI calculation window size</b>
  *
  * Configure the number of beacons and data frames that are
  * used for the RSSI averageing calculation. The windows
  * are specified as powers of two for performance reasons.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param win_size_beacon_order The RSSI calculation window for beacons.
  *        The window size is a power of two, the actual window used
  *        will be 2^win_size_beacon_order. Allowed values are  0, 1, 2, 3 and 4.
  * @param win_size_data_order The RSSI calculation window for data frames.
  *        The window size is a power of two, the actual window used
  *        will be 2^win_size_data_order. Allowed values are  0, 1, 2, 3 and 4.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
 */
int
nrx_conf_rssi (nrx_context ctx,
               uint8_t win_size_beacon_order,
               uint8_t win_size_data_order);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set the SNR calculation window size</b>
  *
  * Configure the number of beacons and data frames that are
  * used for the SNR averageing calculation. The windows
  * are specified as powers of two for performance reasons.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param win_size_beacon_order The SNR calculation window for beacons.
  *        The window size is a power of two, the actual window used
  *        will be 2^win_size_beacon_order. Allowed values are 0, 1, 2, 3 and 4.
  *
  * @param win_size_data_order The SNR calculation window for data frames.
  *        The window size is a power of two, the actual window used
  *        will be 2^win_size_data_order. Allowed values are 0, 1, 2, 3 and 4.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - TODO if no more triggers can be created.
 */
int
nrx_conf_snr (nrx_context ctx,
              uint8_t win_size_beacon_order,
              uint8_t win_size_data_order);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set a PER cutoff threshold</b>
  *
  * A packet error rate (PER) will only be considered valid when the
  * number of packets transmitted during the detection interval exceeds
  * PER cutoff threshold. If fewer than per_cutoff_thr packets have
  * been sent in the interval the PER will be 0.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param per_cutoff_thr The packet error rate cutoff expressed as
  *                       a number of packets. This parameter must be
  *                       a positive integer value. 0 is invalid.
  *                       Maximum value is 1000.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - TODO if no more triggers can be created.
 */
int
nrx_set_per_cutoff_thr (nrx_context ctx,
                        uint32_t per_cutoff_thr);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Set a PER threshold trigger</b>
  *
  * The callback is invoked every time (until disabled) the ratio PER
  * (Packet Error Rate) passes the defined threshold in the direction
  * specified.  Several thresholds can be defined. They are identified
  * by the thr_id parameter. There is a dynamic limit to the number of
  * triggers that can exist in the system so this call may fail if the
  * current limit is passed. The limit depends on the available memory
  * on the host.  The PER calculation is controlled by the configured
  * PER cutoff threshold set through nrx_set_per_cutoff_thr().
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id     The threshold id output buffer. This identifies
  *                   the threshold so that several thresholds can be
  *                   defined. The id value is filled in by the
  *                   function call.
  * @param chk_period The PER check period in milliseconds. The PER threshold
  *                   will periodically be compared with the current
  *                   PER with this interval. Minimum value is 100 ms
  *                   and maximum supported time is 35 minutes (2100000 ms).
  * @param per_thr    The PER threshold in percent (range 0 to 100).
  * @param dir        The trigger directions. The callback can either
  *                   be triggered when the value rises above or
  *                   falls below the threshold, but not both.
  *                    
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - TODO if no more triggers can be created.
 */
int
nrx_enable_per_threshold (nrx_context ctx,
                          int32_t *thr_id,
                          uint32_t chk_period,
                          uint32_t per_thr,
                          nrx_thr_dir_t dir);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable a PER threshold</b>
  *
  * The specified PER threshold trigger is disabled. The specified
  * trigger in the device is cleared and no further notifications from
  * this trigger will be made.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id identifying the threshold trigger that should
  *               be disabled.
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments or non-existing thr_id.
  */
int
nrx_disable_per_threshold (nrx_context ctx,
                           int thr_id);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register an PER threshold callback</b>
  *
  * This will register a callback for a PER threshold, see
  * nrx_enable_per_threshold for further details. Several callbacks
  * can be registered to the same threshold.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id. This identifies the threshold
  *              so that several thresholds can be defined. 
  *              The thr_id value is obtained from nrx_enable_per_threshold.
  * @param cb The callback function that is to be invoked by threshold notifications.
  *           The callback is invoked with operation NRX_CB_TRIGGER on a 
  *           successful notification whereupon event_data will be a pointer 
  *           to a nrx_event_mibtrigger structure which contains further 
  *           information. When the threshold is cancelled cb is called 
  *           with operation NRX_CB_CANCEL and event_data set to NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * 
  * @return A handle to a callback (an unsigned integer type). The only
  * use for this is to pass it to nrx_cancel_per_threshold_callback
  * to cancel the callback.
  * @retval Zero on memory allocation failure.
  * @retval Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_per_threshold_callback (nrx_context ctx,
                                     uint32_t thr_id,
                                     nrx_callback_t cb,
                                     void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel an PER threshold callback</b>
  *
  *
  * This will cancel the callback for a PER threshold.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle Callback handle obtained from nrx_register_per_threshold_callback.
  *
  *
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  *
  */
int
nrx_cancel_per_threshold_callback (nrx_context ctx,
                                   nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get the current PER</b>
  *
  * The most recently measured packet error rate is reported. The error
  * rate is defined as the ratio of failed transmissions and attempted
  * transmissions over some interval (default one second).
  *
  * This function requires that a  per trigger is registered
  * (nrx_enable_per_threshold()).
  *
  * @param [in]  ctx         context created by nrx_init_context().
  * @param [out] error_rate  Will hold current PER (in percent).
  *
  * @note Before this function is used, PER measurement must be configured 
  * with nrx_set_per_cutoff_thr(). When the total number of packets during 
  * an interval is less than configured, this function will report zero.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_per (nrx_context ctx,
             uint32_t *error_rate);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get current fragmentation threshold</b>
  *
  * Gets the current threshold for fragmentation of frames.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param fragment_thr Where the fragmentation threshold is stored.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_fragment_thr (nrx_context ctx,
                      int32_t *fragment_thr);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get current fragmentation threshold</b>
  *
  * Sets threshold for fragmentation of frames.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param fragment_thr New fragmentation threshold.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_set_fragment_thr (nrx_context ctx,
                      int32_t fragment_thr);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get current RTS threshold</b>
  *
  * Retreives the threshold where RTS/CTS protocol will be used.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param rts_thr Where the current RTS threshold is stored.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_rts_thr (nrx_context ctx,
                 int32_t *rts_thr);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get current RTS threshold</b>
  *
  * Below this threshold RTS/CTS protocol will be used.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param rts_thr New RTS threshold.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_set_rts_thr (nrx_context ctx,
                 int32_t rts_thr);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Get registry</b>
  *
  * Retreives current registry settings from driver as a text file.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param registry Buffer for text to be stored.
  * @param len Size of buffer.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_get_registry (nrx_context ctx,
                  char *registry,
                  size_t *len);

/*! 
  * @internal
  * @brief <b>Check existence of trigger</b>
  * 
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param trig_id Virtual trigger id.
  * @param mib_id Zero-terminated string specifying mib trigger is related to. If this is NULL, 
  *        no check on mib_id will be performed, i.e. only trig_id is 
  *        verified.
  *
  * @return
  *  - 0 on success
  *  - an error code on failure 
  */
int
nrx_check_trigger_existence (nrx_context ctx,
                             int32_t thr_id,
                             const char *mib_id,
                             nrx_bool *does_exist);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable TX rate threshold</b>
  *
  * A trigger will happen when TX rate falls below the threshold
  * level. The level is compared to the median over a specified number
  * of transmitted messages.
  *
  * Note: Only one threshold can be registered so far and its direction
  * must be falling.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id output buffer. This identifies the threshold.
  *        The id value is filled in by the function call.
  * @param sample_len Number of transmitted messages over which the
  *        median value is calculated.
  * @param thr_limit Threshold level specified as a native 802.11 rate, 
  *        i.e. in steps of 500kbps.
  * @param dir Must be NRX_THR_FALLING.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_enable_tx_rate_threshold (nrx_context ctx,
                              int32_t *thr_id,
                              uint32_t sample_len,
                              nrx_rate_t thr_limit,
                              nrx_thr_dir_t dir);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable TX rate threshold</b>
  *
  * No TX rate triggers will be issued after this API is used.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id Threshold id received in previous call to nrx_enable_tx_rate_threshold().
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_disable_tx_rate_threshold (nrx_context ctx,
                               int32_t thr_id);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register TX rate threshold callback</b>
  *
  * Notify the caller when TX rate has fallen below current threshold. The
  * callback will be invoked every time (until disabled) the condition
  * is met.
  *
  * This function can be called several times and it will not
  * unregister any other callbacks.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id Threshold ID to trigger on, can be zero to match all
  *        thresholds, or an id returned by
  *        nrx_enable_tx_rate_threshold().
  * @param cb Callback that is invoked to notify the caller that the
  *        rate is below threshold. The callback is invoked with
  *        operation NRX_CB_TRIGGER on a successful notification. It
  *        will be passed a pointer to nrx_rate_t, which contains
  *        the rate that triggered the indication.  Memory for
  *        event_data will be freed immediately after the callback
  *        has returned. When the threshold is canceled cb is
  *        called with operation NRX_CB_CANCEL and event_data set to
  *        NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *        be passed to the callback on invocation. This is for
  *        caller use only, it will not be parsed or modified in
  *        any way by this library. This parameter can be NULL.
  * @return 
  * - Zero on memory allocation failure
  * - Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_tx_rate_threshold_callback (nrx_context ctx,
                                         int32_t thr_id,
                                         nrx_callback_t cb,
                                         void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel TX rate threshold callback</b>
  *
  * The TX rate notification feature is disabled. No further
  * TX rate notifications will be made.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  *        nrx_register_tx_rate_threshold_callback(). The handle will no longer be
  *        valid after this call.
  * 
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_tx_rate_threshold_callback (nrx_context ctx,
                                       nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable RX rate threshold</b>
  *
  * A trigger will happen when RX rate falls below the threshold
  * level. The level is compared to the median over a specified number
  * of received messages.
  *
  * Note: Only one threshold can be registered so far and its direction
  * must be falling.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id output buffer. This identifies the threshold.
  *        The id value is filled in by the function call.
  * @param sample_len Number of received messages over which the
  *        median value is calculated.
  * @param thr_rate Threshold level specified as a native 802.11 rate,
  *        i.e. in steps of 500kbps.
  * @param dir Must be NRX_THR_FALLING.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_enable_rx_rate_threshold (nrx_context ctx,
                              int32_t *thr_id,
                              uint32_t sample_len,
                              nrx_rate_t thr_rate,
                              nrx_thr_dir_t dir);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable RX rate threshold</b>
  *
  * No RX rate indications will be sent after this API is used.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id Threshold id received in previous call to nrx_enable_rx_rate_threshold().
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_disable_rx_rate_threshold (nrx_context ctx,
                               int32_t thr_id);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register RX rate threshold callback</b>
  *
  * Notify the caller when RX rate has fallen below current threshold. The
  * callback will be invoked every time (until disabled) the condition
  * is met.
  *
  * This function can be called several times and it will not
  * unregister any other callbacks.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id Threshold ID to trigger on, can be zero to match all
  *        thresholds, or an id returned by
  *        nrx_enable_rx_rate_threshold().
  * @param cb Callback that is invoked to notify the caller that the
  *        rate is below threshold. The callback is invoked with
  *        operation NRX_CB_TRIGGER on a successful notification. It
  *        will be passed a pointer to nrx_rate_t, which contains
  *        the rate that triggered the indication.  Memory for
  *        event_data will be freed immediately after the callback
  *        has returned. When the threshold is canceled cb is
  *        called with operation NRX_CB_CANCEL and event_data set to
  *        NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *        be passed to the callback on invocation. This is for
  *        caller use only, it will not be parsed or modified in
  *        any way by this library. This parameter can be NULL.
  * @return 
  * - Zero on memory allocation failure
  * - Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_rx_rate_threshold_callback (nrx_context ctx,
                                         int32_t thr_id,
                                         nrx_callback_t cb,
                                         void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel RX rate threshold callback</b>
  *
  * The RX rate notification feature is disabled. No further
  * RX rate notifications will be made.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  *        nrx_register_rx_rate_threshold_callback(). The handle will no
  *        longer be valid after this call.
  * 
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_rx_rate_threshold_callback (nrx_context ctx,
                                       nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable TX fail notification</b>
  *
  * A notification will happen when the number of consecutive
  * transmissions that have failed equals the threshold level.
  *
  * Note: Only one threshold can be registered.
  *
  * @param ctx NRX context that was created by the call to
  *        nrx_init_context().
  * @param thr_limit Limit of consecutive failed transmissions
  *        thereafter notification will happen.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_enable_tx_fail_notification (nrx_context ctx,
                                 uint32_t thr_limit);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable TX fail notification</b>
  *
  * Notifications will no longer be done due to TX failure.
  *
  * Calling this function will not unregister any callbacks.
  *
  * @param ctx NRX context that was created by the call to
  *        nrx_init_context().
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_disable_tx_fail_notification (nrx_context ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register TX fail notification callback</b>
  *
  * Notify the caller when limit for transmission failure is fulfilled. The
  * callback will be invoked every time (until disabled) the condition
  * is met.
  *
  * This function can be called several times and it will not
  * unregister any other callbacks.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param cb Callback that is invoked to notify the caller that the
  *        number of failed transmissions equals the threshold. The
  *        callback is invoked with operation NRX_CB_TRIGGER on a
  *        successful notification. It will be passed a NULL pointer
  *        as event_data. When the threshold is canceled cb is
  *        called with operation NRX_CB_CANCEL and event_data set to
  *        NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *        be passed to the callback on invocation. This is for
  *        caller use only, it will not be parsed or modified in
  *        any way by this library. This parameter can be NULL.
  * @return 
  * - Zero on memory allocation failure
  * - Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_tx_fail_notification (nrx_context ctx,
                                   nrx_callback_t cb,
                                   void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel TX fail notification callback</b>
  *
  * The tx fail notification feature is disabled. No further
  * tx fail notifications will issued.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  *        nrx_register_tx_fail_notification(). The handle will no
  *        longer be valid after this call.
  * 
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_tx_fail_notification (nrx_context ctx,
                                 nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Enable missed beacon indication</b>
  *
  * A notification will happen when the number of consecutive
  * beacons that have not been received equals the threshold level.
  *
  * Note: Only one threshold can be registered.
  *
  * @param ctx NRX context that was created by the call to
  *        nrx_init_context().
  * @param thr_limit Limit of consecutive failed receptions of beacon
  *        thereafter notification will happen.
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_enable_missed_beacon_notification (nrx_context ctx,
                                       uint32_t thr_limit);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Disable missed beacon notification</b>
  *
  * Notifications will no longer be done due to missed beacons.
  *
  * Calling this function will not unregister any callbacks.
  *
  * @param ctx NRX context that was created by the call to
  *        nrx_init_context().
  *
  * @retval 0 on success
  * @retval EINVAL on invalid arguments
  */
int
nrx_disable_missed_beacon_notification (nrx_context ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Register missed beacon notification callback</b>
  *
  * Notify the caller when limit for missed beacons is fulfilled. The
  * callback will be invoked every time (until disabled) the condition
  * is met.
  *
  * This function can be called several times and it will not
  * unregister any other callbacks.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param cb Callback that is invoked to notify the caller that the
  *        number of missed beacons equals the threshold. The
  *        callback is invoked with operation NRX_CB_TRIGGER on a
  *        successful notification. It will be passed a NULL pointer
  *        as event_data. When the threshold is canceled cb is
  *        called with operation NRX_CB_CANCEL and event_data set to
  *        NULL.
  * @param cb_ctx Pointer to a user-defined callback context that will
  *        be passed to the callback on invocation. This is for
  *        caller use only, it will not be parsed or modified in
  *        any way by this library. This parameter can be NULL.
  * @return 
  * - Zero on memory allocation failure
  * - Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_missed_beacon_notification (nrx_context ctx,
                                         nrx_callback_t cb,
                                         void *cb_ctx);

/*!
  * @internal
  * @ingroup RADIO
  * @brief <b>Cancel missed beacon notification callback</b>
  *
  * The missed beacon notification feature is disabled. No further
  * missed beacon notifications will issued.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  *        nrx_register_missed_beacon_notification(). The handle will no
  *        longer be valid after this call.
  * 
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g the handle is not registered.
  */
int
nrx_cancel_missed_beacon_notification (nrx_context ctx,
                                       nrx_callback_handle handle);

/*!
  * @internal
  * @ingroup ROAM
  * @brief <b>Configure delay spread thresholds</b>
  *
  * Configure thresholds for initiating roaming and scanning based on
  * delay spread level.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @param enable When enabled, roaming and scanning will be performed when
  *        delay spread rises above the configured thresholds.
  *
  * @param roam_thr When the current delay spread level rises above this
  *        threshold, then romaing will be initiated. Range is [0, 100].
  *
  * @param scan_thr When the current delay spread level rises above this
  *        threshold, then scanning will be initiated. Range is [0, 100].
  *
  * @return
  *  - 0 on success
  *  - EINVAL on invalid parameters.
  */
int
nrx_conf_roam_ds_thr (nrx_context ctx,
                      nrx_bool enable,
                      uint32_t roam_thr,
                      uint32_t scan_thr);

/*!
  * @internal
  * @ingroup ROAM
  * @brief <b>Configure constants for calculating net link quality when electing
  *        AP</b>
  *
  * The constants will affect how much RSSI is prioritized compared to SNR when
  * electing the best net. The final link quality value will be normalize, so
  * k1 and k2 can have any value in the range of the data type.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param k1 RSSI weight.
  * @param k2 SNR weight.
  *
  * @return
  *  - 0 on success
  *  - EINVAL on invalid parameters.
  */
int
nrx_conf_roam_net_election (nrx_context ctx,
                            uint32_t k1,
                            uint32_t k2);

/*!
  * @internal
  * @ingroup ROAM
  * @brief <b>Configure delay spread calculation</b>
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @return
  *  - 0 on success
  *  - EINVAL on invalid parameters.
  */
int
nrx_conf_delay_spread (nrx_context ctx,
                       uint32_t thr,
                       uint16_t winsize);

/*!
  * @ingroup SCAN
  * @internal
  * @deprecated This interface is not available anymore.
  * @brief <b>Administrate SSID Pool</b>
  *
  * Adds/Removes SSID's to/from the SSID-pool in firmware
  *
  * @param action Defines if ssid should be added (NRX_SSID_ADD)
  *        or removed (NRX_SSID_REMOVE) to/from the pool.
  * @param ssid  String containing ssid octets, the string "any" will be passed as a NULL ssid.
  * @return always return EOPNOTSUPP 
  * 
  */
int
nrx_scan_adm_ssid_pool (nrx_context ctx,
                        nrx_ssid_action_t action,
                        nrx_ssid_t ssid);

/*!
  * @ingroup SCAN
  * @internal
  * @deprecated This interface is not available anymore.
  * @brief <b>Administrate connections of SSID to Job </b>
  *
  * Adds/Removes a SSID connection to/from a scan Job in firmware
  *
  * @param action Defines if the ssid should be added (NRX_SSID_ADD) or
  *        removed (NRX_SSID_REMOVE) to/from the scan job.
  * @param job_id Defines to which scan job the ssid shuold be Added/Removed to/from
  * @param ssid  String containing ssid octets, the string "any" will be passed as a NULL ssid.
  * @return always return EOPNOTSUPP 
  * 
  */
int
nrx_scan_adm_job_ssid (nrx_context ctx,
                       nrx_ssid_action_t action,
                       uint32_t job_id,
                       nrx_ssid_t ssid);

int
nrx_scan_get_event (nrx_context context,
                    void *scan_buf,
                    size_t scan_size,
                    void **event,
                    size_t *event_size);

int
nrx_get_nickname (nrx_context ctx,
                  char *ifname,
                  char *name,
                  size_t len);

int
nrx_check_wext (nrx_context ctx,
                char *ifname);

int
nrx_get_wxconfig (nrx_context ctx);

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
nrx_set_tx_retry_limits (nrx_context ctx,
                         uint8_t short_limit,
                         uint8_t long_limit);

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
nrx_get_curr_freq (nrx_context ctx,
                   uint32_t *frequency);

/* END GENERATED PROTOS */

#endif /* __nrx_priv_h__ */
