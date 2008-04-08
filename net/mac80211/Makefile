obj-$(CONFIG_MAC80211) += mac80211.o

# objects for PID algorithm
rc80211_pid-y := rc80211_pid_algo.o
rc80211_pid-$(CONFIG_MAC80211_DEBUGFS) += rc80211_pid_debugfs.o

# build helper for PID algorithm
rc-pid-y := $(rc80211_pid-y)
rc-pid-m := rc80211_pid.o

# mac80211 objects
mac80211-y := \
	main.o \
	wext.o \
	sta_info.o \
	wep.o \
	wpa.o \
	mlme.o \
	iface.o \
	rate.o \
	michael.o \
	tkip.o \
	aes_ccm.o \
	cfg.o \
	rx.o \
	tx.o \
	key.o \
	util.o \
	event.o

mac80211-$(CONFIG_MAC80211_LEDS) += led.o
mac80211-$(CONFIG_NET_SCHED) += wme.o
mac80211-$(CONFIG_MAC80211_DEBUGFS) += \
	debugfs.o \
	debugfs_sta.o \
	debugfs_netdev.o \
	debugfs_key.o

mac80211-$(CONFIG_MAC80211_MESH) += \
	mesh.o \
	mesh_pathtbl.o \
	mesh_plink.o \
	mesh_hwmp.o


# Build rate control algorithm(s)
CFLAGS_rc80211_pid_algo.o += -DRC80211_PID_COMPILE
mac80211-$(CONFIG_MAC80211_RC_PID) += $(rc-pid-$(CONFIG_MAC80211_RC_PID))

# Modular rate algorithms are assigned to mac80211-m - make separate modules
obj-m += $(mac80211-m)
