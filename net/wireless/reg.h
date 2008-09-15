#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H

extern struct mutex cfg80211_reg_mutex;
bool is_world_regdom(char *alpha2);
bool reg_is_valid_request(char *alpha2);

int regulatory_init(void);
void regulatory_exit(void);

int set_regdom(struct ieee80211_regdomain *rd);

#endif  /* __NET_WIRELESS_REG_H */
