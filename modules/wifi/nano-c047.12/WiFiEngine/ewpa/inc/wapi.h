#ifndef WAPI_H
#define WAPI_H

#include "defs.h"
#include "wapi_common.h"

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

#define WAPI_INFO_ELEM 0x44
#define WAPI_FLAG_LEN 1
#define GETSHORT(frm, v) do { (v) = (((frm[0]) <<8) | (frm[1]))& 0xffff;} while (0)
#define SETSHORT(frm, v) do{(frm[0])=((v)>>8)&0xff;(frm[1])=((v))&0xff;}while(0)

enum wapi_sm_conf_params {
        WAPI_PARAM_PROTO,
        WAPI_PARAM_PAIRWISE,
        WAPI_PARAM_GROUP,
        WAPI_PARAM_KEY_MGMT,
        WAPI_PARAM_MGMT_GROUP
};

struct wapi_ie_data {
  int proto;
  int pairwise_cipher;
  int group_cipher;
  int key_mgmt;
  int capabilities;
  int num_pmkid;
  const u8 *pmkid;
  int mgmt_group_cipher;
};

struct wapi_sm;
struct wpa_ssid;
struct eapol_sm;
struct wpa_config_blob;

struct wapi_sm_ctx {
        void *ctx; /* pointer to arbitrary upper level context */

        void (*set_state)(void *ctx, wpa_states state);
        wpa_states (*get_state)(void *ctx);
        void (*deauthenticate)(void * ctx, int reason_code);
        void (*disassociate)(void *ctx, int reason_code);
        int (*set_key)(void *ctx, wpa_alg alg,
                       const u8 *addr, int key_idx, int set_tx,
                       const u8 *seq, size_t seq_len,
                       const u8 *key, size_t key_len);
        void (*scan)(void *eloop_ctx, void *timeout_ctx);
        struct wpa_ssid * (*get_ssid)(void *ctx);
        int (*get_bssid)(void *ctx, u8 *bssid);
        int (*ether_send)(void *ctx, const u8 *dest, u16 proto, const u8 *buf,
                          size_t len);
        int (*get_beacon_ie)(void *ctx);
        void (*cancel_auth_timeout)(void *ctx);
        u8 * (*alloc_eapol)(void *ctx, u8 type, const void *data, u16 data_len,
                            size_t *msg_len, void **data_pos);
        int (*add_pmkid)(void *ctx, const u8 *bssid, const u8 *pmkid);
        int (*remove_pmkid)(void *ctx, const u8 *bssid, const u8 *pmkid);
        void (*set_config_blob)(void *ctx, struct wpa_config_blob *blob);
        const struct wpa_config_blob * (*get_config_blob)(void *ctx,
                                                          const char *name);
        int (*mlme_setprotection)(void *ctx, const u8 *addr,
                                  int protection_type, int key_type);
};

void *get_buffer(int len);
void *free_buffer(void *buffer, int len);

struct wapi_sm * wapi_sm_init(struct wapi_sm_ctx *ctx);
void wapi_sm_deinit(struct wapi_sm *sm);
void wapi_sm_set_own_addr(struct wapi_sm *sm, const u8 *addr);
void wapi_sm_set_ifname(struct wapi_sm *sm, const char *ifname,
                       const char *bridge_ifname);
void wapi_sm_set_eapol(struct wapi_sm *sm, struct eapol_sm *eapol);
int wapi_sm_get_status(struct wapi_sm *sm, char *buf, size_t buflen,
		       int verbose);
void wapi_sm_notify_assoc(struct wapi_sm *sm, const u8 *bssid);
void wapi_sm_notify_disassoc(struct wapi_sm *sm);
int wapi_sm_set_param(struct wapi_sm *sm, enum wapi_sm_conf_params param,
		      unsigned int value);
void wapi_sm_set_config(struct wapi_sm *sm, struct wpa_ssid *config);
void wapi_sm_set_pmk(struct wapi_sm *sm, const u8 *pmk, size_t pmk_len);
int wapi_sm_set_ap_ie(struct wapi_sm *sm, const u8 *ie, size_t len);
int wapi_parse_ie(const u8 *wapi_ie, size_t wapi_ie_len,
                  struct wapi_ie_data *data);
int wapi_sm_parse_own_ie(struct wapi_sm *sm, struct wapi_ie_data *data);

int wapi_sm_set_assoc_ie(struct wapi_sm *sm, const u8 *ie, size_t len);
int wapi_sm_set_assoc_ie_default(struct wapi_sm *sm, u8 *wapi_ie,
                                    size_t *wapi_ie_len);
int wapi_sm_rx_eapol(struct wapi_sm *sm, const u8 *src_addr,
		     const u8 *buf, size_t len);

#endif /* WAPI_H */

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
