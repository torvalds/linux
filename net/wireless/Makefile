obj-$(CONFIG_WIRELESS_EXT) += wext.o
obj-$(CONFIG_CFG80211) += cfg80211.o
obj-$(CONFIG_LIB80211) += lib80211.o
obj-$(CONFIG_LIB80211_CRYPT_WEP) += lib80211_crypt_wep.o
obj-$(CONFIG_LIB80211_CRYPT_CCMP) += lib80211_crypt_ccmp.o
obj-$(CONFIG_LIB80211_CRYPT_TKIP) += lib80211_crypt_tkip.o

cfg80211-y += core.o sysfs.o radiotap.o util.o reg.o scan.o nl80211.o
cfg80211-y += mlme.o ibss.o sme.o chan.o
cfg80211-$(CONFIG_CFG80211_DEBUGFS) += debugfs.o
cfg80211-$(CONFIG_WIRELESS_EXT) += wext-compat.o wext-sme.o

ccflags-y += -D__CHECK_ENDIAN__
