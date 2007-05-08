obj-$(CONFIG_MAC80211) += mac80211.o rc80211_simple.o

mac80211-objs-$(CONFIG_MAC80211_LEDS) += ieee80211_led.o
mac80211-objs-$(CONFIG_MAC80211_DEBUGFS) += debugfs.o debugfs_sta.o debugfs_netdev.o debugfs_key.o

mac80211-objs := \
	ieee80211.o \
	ieee80211_ioctl.o \
	sta_info.o \
	wep.o \
	wpa.o \
	ieee80211_sta.o \
	ieee80211_iface.o \
	ieee80211_rate.o \
	michael.o \
	tkip.o \
	aes_ccm.o \
	wme.o \
	ieee80211_cfg.o \
	$(mac80211-objs-y)
