/* $Id: transport.h 19321 2011-05-31 12:45:58Z peva $ */


/*!
 * @defgroup transport_layer Transport Layer API
 *
 * This file declares the transport layer API. It is the glue layer to provide
 * WiFiEngine functionality to the SDIO driver and also to implement the
 * generic parts of a Linux network driver.
 *
 * @{
 */

#ifndef _TRANSPORT_H
#define _TRANSPORT_H


#include <linux/netdevice.h>

#define NANONET_SLEEP      1
#define NANONET_SLEEP_OFF  0
#define NANONET_SLEEP_ON   1
#define NANONET_SLEEP_TEST (~0U)

#define NANONET_BOOT    2
#define NANONET_BOOT_DISABLE  0
#define NANONET_BOOT_ENABLE   1
#define NANONET_BOOT_TEST  (~0U)

#define NANONET_INIT_SDIO       3

/* Called on driver unload after unplug, indicates that upper layers
 * will not initiate any new transactions. The transport driver should
 * probable disable interrupts and reset target. */
#define NANONET_SHUTDOWN        4

/* Called when the driver should enter or exit "hard shutdown" state.
 * If the control function is called with mode equal to NANONET_HARD_SHUTDOWN_ENTER
 * then the transport driver should disable interrupts and force target into
 * the state of minimum power consumption.
 * If the control function is called with mode equal to NANONET_HARD_SHUTDOWN_EXIT
 * then the transport driver should resume normal operation of the target.
 */
#define NANONET_HARD_SHUTDOWN     5
#define NANONET_HARD_SHUTDOWN_ENTER 0
#define NANONET_HARD_SHUTDOWN_EXIT  1
#define NANONET_HARD_SHUTDOWN_TEST  (~0U)

/* Target chip types */
typedef enum {
   CHIP_TYPE_UNKNOWN = 0,
   CHIP_TYPE_NRX700,
   CHIP_TYPE_NRX600,
   CHIP_TYPE_NRX900
} chip_type_t;

#define STR_CHIP_TYPE_UNKNOWN "unknown"
#define STR_CHIP_TYPE_NRX700  "NRX700"
#define STR_CHIP_TYPE_NRX600  "NRX600"
#define STR_CHIP_TYPE_NRX900  "NRX900"

/* mac_api/tags/20090917 */
struct nanonet_create_param {
   size_t size; /* size of this structure */
   chip_type_t chip_type; /* type of the target detected */
   int (*send)(struct sk_buff*, void*);
   int (*fw_download)(const void*, size_t, void*);
   int (*control)(uint32_t, uint32_t, void*);
   /* params for WiFiEngine_LoadWLANParameters */
   const void *params_buf;
   size_t params_len;
   /* params for WiFiEngine_SetMsgSizeAlignment */
   uint16_t min_size;
   uint16_t size_align;
   uint8_t header_size; /* HIC header plus HIC_data_req header size for tx and rx */
   uint8_t host_attention;
   uint8_t byte_swap_mode;
   uint8_t host_wakeup;
   uint8_t force_interval;
   uint8_t tx_headroom; /* SKB headroom need by the transport driver in send() to avoid copying */
};

/* nanonet_create_param.host_attention */
#define HIC_CTRL_ALIGN_HATTN_MASK_POLICY                  0x01
#define HIC_CTRL_ALIGN_HATTN_VAL_POLICY_GPIO              0x00
#define HIC_CTRL_ALIGN_HATTN_VAL_POLICY_NATIVE_SDIO       HIC_CTRL_ALIGN_HATTN_MASK_POLICY
#define HIC_CTRL_ALIGN_HATTN_MASK_OVERRIDE_DEFAULT_PARAM  0x02
#define HIC_CTRL_ALIGN_HATTN_VAL_USE_DEFAULT_PARAM        0x00
#define HIC_CTRL_ALIGN_HATTN_VAL_OVERRIDE_DEFAULT_PARAM   HIC_CTRL_ALIGN_HATTN_MASK_OVERRIDE_DEFAULT_PARAM
#define HIC_CTRL_ALIGN_HATTN_MASK_PARAMS                  0xFC
#define HIC_CTRL_ALIGN_HATTN_MASK_GPIOPARAMS_GPIO_TYPE    0x04
#define HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_STD 0x00
#define HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_EXT HIC_CTRL_ALIGN_HATTN_MASK_GPIOPARAMS_GPIO_TYPE
#define HIC_CTRL_ALIGN_HATTN_MASK_GPIOPARAMS_GPIO_ID      0xF8
#define HIC_CTRL_ALIGN_HATTN_OFFSET_GPIOPARAMS_GPIO_ID    3

/* nanonet_create_param.byte_swap_mode */
#define HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP     0x00
#define HIC_CTRL_ALIGN_SWAP_16BIT_BYTESWAP  0x01

/* nanonet_create_param.host_wakeup */
#define HIC_CTRL_ALIGN_HWAKEUP_NOT_USED      0xFF

/* nanonet_create_param.force_interval */
#define HIC_CTRL_ALIGN_FINTERVAL_NOT_USED 0xFF

/*!
 * @brief This function is used to initialize the transport layer and to
 * register a set of callback functions. This function should be called
 * when the sdio driver module is loaded. At that time the callback functions
 * for sending data, (send), downloading firmware (fw_download) and misc
 * functions such as power management (control) will be registered.
 *
 * @return Pointer to the net_device.
 *
 */
struct net_device *nanonet_create(struct device*, void*, struct nanonet_create_param*);

/*!
 * @brief This function is used to disable the transport layer. The function
 * should be called when the sdio driver module is unloaded.
 *
 * @return void
 *
 *
 */
void nanonet_destroy(struct net_device*);


/*!
 * @brief This function should be called by the sdio driver when any data
 * should be sent to the upper layers. Ns_net_rx can not be called from
 * interrupt context.
 *
 * @return void
 *
 */
void ns_net_rx(struct sk_buff *skb, struct net_device *dev);


#ifdef USE_IF_REINIT
void nrx_reg_inact_cb(int (*cb)(void *, size_t len));

void nrx_dereg_inact_cb(int (*cb)(void *, size_t len));

void nrx_drv_quiesce(void);

void nrx_drv_unquiesce(void);
#endif

void nanonet_attach(struct net_device*, void*);
void nanonet_detach(struct net_device*, void*);

#endif
