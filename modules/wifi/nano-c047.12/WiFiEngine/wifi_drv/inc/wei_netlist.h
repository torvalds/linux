
#ifndef WEI_NETLIST_H
#define WEI_NETLIST_H

#include "driverenv.h"
#include "registry.h"

/*! Holds all currently known nets and their disposition */
typedef struct
{
      WiFiEngine_net_t     *active_list_ref;     /**< Prioritized network list. */
      WiFiEngine_net_t     *sta_list_ref;        /**< Station list */
      WiFiEngine_net_t     *current_net;         /**< Net currently in processing. will be pressent from beginning of join until final connect */
} wei_netlist_state_t;

#define wei_netlist_get_net_state()        (&(wifiEngineState.net_state))
#define wei_netlist_get_current_net()      (wifiEngineState.net_state.current_net)

#define wei_netlist_inc_ref_cnt(net) (net->ref_cnt++)



void wei_netlist_free_net_safe(WiFiEngine_net_t *net);

void wei_netlist_clear_active_nets(void);

void wei_netlist_clear_sta_nets(void);

void wei_netlist_free_all_nets(void);

void wei_netlist_add_reference(WiFiEngine_net_t **dst, WiFiEngine_net_t *net);

void wei_netlist_remove_reference(WiFiEngine_net_t **dst);

WiFiEngine_net_t *wei_netlist_create_new_net(void);

WiFiEngine_net_t* wei_netlist_add_net_to_active_list(mlme_bss_description_t *newp);

WiFiEngine_net_t*  wei_netlist_add_net_to_sta_list(m80211_nrp_mlme_peer_status_ind_t *new_sta);

WiFiEngine_net_t *wei_netlist_get_next_net_by_ssid(m80211_ie_ssid_t ssid, WiFiEngine_net_t *net);

WiFiEngine_net_t *wei_netlist_get_net_by_ssid(m80211_ie_ssid_t ssid);

void wei_netlist_get_nets_ch_by_ssid(m80211_ie_ssid_t ssid, channel_list_t *channel_list);

WiFiEngine_net_t *wei_netlist_get_net_by_bssid(m80211_mac_addr_t bssid);

WiFiEngine_net_t *wei_netlist_get_sta_net_by_peer_mac_address(rBSSID bssid);

WiFiEngine_net_t *wei_netlist_get_joinable_net(void);

void wei_netlist_remove_from_active(WiFiEngine_net_t *net);

void wei_netlist_remove_from_sta(WiFiEngine_net_t *net);

void wei_netlist_remove_current_net(void);

void wei_netlist_add_to_active(WiFiEngine_net_t *net);

void wei_netlist_add_to_sta(WiFiEngine_net_t *net);

void wei_netlist_make_current_net(WiFiEngine_net_t *net);

void wei_netlist_make_associated_net(WiFiEngine_net_t *net);

void wei_netlist_make_elected_net(WiFiEngine_net_t *net);

int wei_netlist_count_active(void);

int wei_netlist_count_sta(void);

int wei_netlist_expire_net(WiFiEngine_net_t *net, driver_tick_t current_time, driver_tick_t expire_age);

int wei_netlist_get_size_of_ies(WiFiEngine_net_t *net);

void wei_limit_netlist(int size);

#endif /* WEI_NETLIST_H */

