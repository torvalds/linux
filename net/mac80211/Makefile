obj-$(CONFIG_MAC80211) += mac80211.o

# objects for PID algorithm
rc80211_pid-y := rc80211_pid_algo.o
rc80211_pid-$(CONFIG_MAC80211_DEBUGFS) += rc80211_pid_debugfs.o

# build helper for PID algorithm
rc-pid-y := $(rc80211_pid-y)
rc-pid-m := rc80211_pid.o

# mac80211 objects
mac80211-y := \
	ieee80211.o \
	ieee80211_ioctl.o \
	sta_info.o \
	wep.o \
	wpa.o \
	ieee80211_sta.o \
	ieee80211_iface.o \
	ieee80211_rate.o \
	michael.o \
	regdomain.o \
	tkip.o \
	aes_ccm.o \
	cfg.o \
	rx.o \
	tx.o \
	key.o \
	util.o \
	event.o

mac80211-$(CONFIG_MAC80211_LEDS) += ieee80211_led.o
mac80211-$(CONFIG_NET_SCHED) += wme.o
mac80211-$(CONFIG_MAC80211_DEBUGFS) += \
	debugfs.o \
	debugfs_sta.o \
	debugfs_netdev.o \
	debugfs_key.o


# Build rate control algorithm(s)
CFLAGS_rc80211_simple.o += -DRC80211_SIMPLE_COMPILE
CFLAGS_rc80211_pid_algo.o += -DRC80211_PID_COMPILE
mac80211-$(CONFIG_MAC80211_RC_SIMPLE) += rc80211_simple.o
mac80211-$(CONFIG_MAC80211_RC_PID) += $(rc-pid-$(CONFIG_MAC80211_RC_PID))

# Modular rate algorithms are assigned to mac80211-m - make separate modules
obj-m += $(mac80211-m)
