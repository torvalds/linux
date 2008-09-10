#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H

extern const struct ieee80211_regdomain world_regdom;
#ifdef CONFIG_WIRELESS_OLD_REGULATORY
extern const struct ieee80211_regdomain us_regdom;
extern const struct ieee80211_regdomain jp_regdom;
extern const struct ieee80211_regdomain eu_regdom;
#endif

extern struct ieee80211_regdomain *cfg80211_regdomain;
extern struct ieee80211_regdomain *cfg80211_world_regdom;
extern struct list_head regulatory_requests;

struct regdom_last_setby {
	struct wiphy *wiphy;
	u8 initiator;
};

/* wiphy is set if this request's initiator is REGDOM_SET_BY_DRIVER */
struct regulatory_request {
	struct list_head list;
	struct wiphy *wiphy;
	int granted;
	enum reg_set_by initiator;
	char alpha2[2];
};

bool is_world_regdom(char *alpha2);
bool reg_is_valid_request(char *alpha2);

int set_regdom(struct ieee80211_regdomain *rd);
int __regulatory_hint_alpha2(struct wiphy *wiphy, enum reg_set_by set_by,
		      const char *alpha2);

int regulatory_init(void);
void regulatory_exit(void);

void print_regdomain_info(struct ieee80211_regdomain *);

/* If a char is A-Z */
#define IS_ALPHA(letter) (letter >= 65 && letter <= 90)

#endif  /* __NET_WIRELESS_REG_H */
