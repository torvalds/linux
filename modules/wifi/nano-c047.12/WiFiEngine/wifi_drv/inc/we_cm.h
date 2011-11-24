#ifndef __we_cm_h__
#define __we_cm_h__

#include "wifi_engine.h"

#define MAX_ALLOWED_NET_FILTER 10

void wei_cm_initialize(void **priv);
void wei_cm_plug(void *priv);
void wei_cm_unplug(void *priv);
void wei_cm_shutdown(void *priv);
void wei_cm_wpa_connect_wait(int enable);

void* we_cm_connect(WiFiEngine_net_t*);
int we_cm_disconnect(void*);

typedef int (*net_filter_t)(WiFiEngine_net_t* net, void* priv);

struct net_profile_t {
   m80211_ie_ssid_t ssid;
   m80211_mac_addr_t bssid;
#if 0
   net_filter_t filters[MAX_ALLOWED_NET_FILTER];
   void* filter_privs[MAX_ALLOWED_NET_FILTER];
#endif
};
typedef struct net_profile_t net_profile_s;

WiFiEngine_net_t* WiFiEngine_elect_net(net_filter_t filter_out, void *filter_priv, int use_standard_filters);

#endif /* __we_cm_h__ */

