


AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E
CXX		= $(CROSS_COMPILE)g++
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump

export AS LD CC CPP CXX AR NM STRIP OBJCOPY OBJDUMP CROSS_COMPILE LDFLAGS 

ifneq ($(CONFIG),release)
	DEBUG_FLAGS += -DWIFI_DEBUG_ON -DWIFI_DUMP_PACKETS
	DEBUG_FLAGS += -fno-strict-aliasing -fno-common
endif

DEFS += -DPLATFORM_$(PLATFORM)
DEFS +=	-DEXPORT_SYMTAB
DEFS += -DHAVE_ANSI_INTTYPES
DEFS +=	-D_LARGEFILE64_SOURCE

## please don't remove these warning options, 
## try to fix the problem instead
DEFS += -Wall
DEFS += -Wextra
## these mask warnings that cause large amounts of warnings, these
## should be fixed, and these options removed
DEFS += -Wno-sign-compare
DEFS += -Wno-unused-parameter
##
#DEFS += -Werror

NANO_SPI_OBJ = \
kernel/spi/spi.o \
kernel/spi/$(HOST_PLATFORM_SRC_DIR)/host.o

NANO_SPI_INC = \
kernel/spi \
kernel/spi/$(HOST_PLATFORM_SRC_DIR) \

NANO_KSPI_OBJ = \
kernel/kspi/kspi.o \
kernel/kspi/lnx_spi_api.o \
kernel/kspi/$(HOST_PLATFORM_SRC_DIR)/host.o

NANO_KSPI_INC = \
kernel/kspi \
kernel/kspi/$(HOST_PLATFORM_SRC_DIR) \

NANO_PCI_OBJ = \
kernel/pci/nano_pci.o

NANO_PCI_INC = \
kernel/pci \

NANO_SDIO_OBJ = \
kernel/sdio/sdio.o \
kernel/sdio/$(HOST_PLATFORM_SRC_DIR)/host.o

NANO_SDIO_INC = \
kernel/sdio \
kernel/sdio/$(HOST_PLATFORM_SRC_DIR) \

NANO_IF_OBJ = \
kernel/ic/nanonet.o \
kernel/ic/nanoutil.o \
kernel/ic/nanoproc.o \
kernel/ic/nanoiw.o \
kernel/ic/scan.o \
kernel/ic/nanoioctl.o \
kernel/ic/firmware.o \
kernel/ic/stream.o \
kernel/ic/mib.o \
kernel/ic/event.o \
kernel/ic/dump.o \
kernel/ic/px.o \
kernel/ic/pcap.o \
kernel/ic/log_proc.o \
kernel/ic/registry.o \
kernel/ic/config.o \
kernel/ic/mac.o

NANO_IF_INC = \
kernel/ic

WIFI_ENGINE_OBJ = \
driverenv/src/application.o \
driverenv/src/driverenv.o \
driverenv/src/de_common.o \
driverenv/src/de_crypto.o \
WiFiEngine/wifi_drv/src/mibtable.o \
WiFiEngine/wifi_drv/src/we_dlm.o \
WiFiEngine/wifi_drv/src/we_dlm_dynamic.o \
WiFiEngine/wifi_drv/src/we_wps.o \
WiFiEngine/wifi_drv/src/we_ind.o \
WiFiEngine/mac_wrapper/src/macWrapper.o \
WiFiEngine/hic_wrapper/src/hicWrapper.o \
WiFiEngine/hic_wrapper/src/packer.o \
WiFiEngine/mlme_proxy/src/mlme_proxy.o \
WiFiEngine/reg/src/registryAccess.o \
WiFiEngine/reg/src/registry.o \
WiFiEngine/reg/src/defaultreg.o \
WiFiEngine/ucos/src/ucos.o \
WiFiEngine/ucos/src/ucos_data.o \
WiFiEngine/wifi_drv/src/hmg_ps.o \
WiFiEngine/wifi_drv/src/hmg_traffic.o \
WiFiEngine/wifi_drv/src/WiFi_engine_internal.o \
WiFiEngine/wifi_drv/src/pkt_debug.o \
WiFiEngine/wifi_drv/src/state_trace.o \
WiFiEngine/wifi_drv/src/wei_netlist.o \
WiFiEngine/wifi_drv/src/wei_list.o \
WiFiEngine/wifi_drv/src/we_arp_filter.o \
WiFiEngine/wifi_drv/src/wei_asscache.o \
WiFiEngine/wifi_drv/src/we_assoc.o \
WiFiEngine/wifi_drv/src/we_scan.o \
WiFiEngine/wifi_drv/src/we_auth.o \
WiFiEngine/wifi_drv/src/we_cb.o \
WiFiEngine/wifi_drv/src/we_chan.o \
WiFiEngine/wifi_drv/src/we_ccx.o \
WiFiEngine/wifi_drv/src/we_data.o \
WiFiEngine/wifi_drv/src/we_dbg.o \
WiFiEngine/wifi_drv/src/we_dump.o \
WiFiEngine/wifi_drv/src/we_init.o \
WiFiEngine/wifi_drv/src/we_interface_handler.o \
WiFiEngine/wifi_drv/src/we_mcast.o \
WiFiEngine/wifi_drv/src/we_mib.o \
WiFiEngine/wifi_drv/src/we_nr.o \
WiFiEngine/wifi_drv/src/we_pmkid.o \
WiFiEngine/wifi_drv/src/we_prop.o \
WiFiEngine/wifi_drv/src/we_param.o \
WiFiEngine/wifi_drv/src/we_ps.o \
WiFiEngine/wifi_drv/src/we_swm500.o \
WiFiEngine/wifi_drv/src/we_trig.o \
WiFiEngine/wifi_drv/src/we_util.o \
WiFiEngine/wifi_drv/src/we_filter.o \
WiFiEngine/wifi_drv/src/we_ibss.o \
WiFiEngine/wifi_drv/src/we_roam.o \
WiFiEngine/wifi_drv/src/we_cm.o \
WiFiEngine/wifi_drv/src/we_cm_scan.o \
WiFiEngine/wifi_drv/src/we_lqm.o \
WiFiEngine/wifi_drv/src/we_multi_net.o \
WiFiEngine/logger/src/fifo.o \
WiFiEngine/logger/src/log.o \
WiFiEngine/wifi_drv/src/we_rate.o \
WiFiEngine/wifi_drv/src/we_scan_suite.o \
WiFiEngine/wifi_drv/src/we_ratemon.o \
WiFiEngine/wifi_drv/src/we_console.o \
WiFiEngine/pal/src/hmg_pal.o \
WiFiEngine/pal/src/pal_command_parser.o \
WiFiEngine/pal/src/we_pal.o


WPA_OBJ =					\
WiFiEngine/ewpa/src/dhm_xyssl.o                 \
WiFiEngine/ewpa/src/bignum_xyssl.o              \
WiFiEngine/ewpa/src/wsc_common.o		\
WiFiEngine/ewpa/src/eap_wsc.o			\
WiFiEngine/ewpa/src/wps_scan_and_connect.o	\
WiFiEngine/ewpa/src/aes_wrap.o			\
WiFiEngine/ewpa/src/asn1.o			\
WiFiEngine/ewpa/src/base64.o			\
WiFiEngine/ewpa/src/bignum.o			\
WiFiEngine/ewpa/src/common.o			\
WiFiEngine/ewpa/src/config.o		\
WiFiEngine/ewpa/src/config_wifiengine.o		\
WiFiEngine/ewpa/src/crypto_internal.o		\
WiFiEngine/ewpa/src/des.o			\
WiFiEngine/ewpa/src/drivers.o			\
WiFiEngine/ewpa/src/driver_wifiengine.o			\
WiFiEngine/ewpa/src/eap.o			\
WiFiEngine/ewpa/src/eap_methods.o		\
WiFiEngine/ewpa/src/eap_mschapv2.o		\
WiFiEngine/ewpa/src/eap_md5.o			\
WiFiEngine/ewpa/src/eap_peap.o			\
WiFiEngine/ewpa/src/eap_tls.o			\
WiFiEngine/ewpa/src/eap_tls_common.o		\
WiFiEngine/ewpa/src/eap_tlv.o			\
WiFiEngine/ewpa/src/eap_ttls.o			\
WiFiEngine/ewpa/src/eapol_sm.o			\
WiFiEngine/ewpa/src/eloop_wifiengine.o			\
WiFiEngine/ewpa/src/events.o			\
WiFiEngine/ewpa/src/l2_packet_none.o		\
WiFiEngine/ewpa/src/main_wifiengine.o			\
WiFiEngine/ewpa/src/md4.o			\
WiFiEngine/ewpa/src/md5.o			\
WiFiEngine/ewpa/src/ms_funcs.o			\
WiFiEngine/ewpa/src/os_wifiengine.o			\
WiFiEngine/ewpa/src/pmksa_cache.o		\
WiFiEngine/ewpa/src/preauth.o			\
WiFiEngine/ewpa/src/rc4.o			\
WiFiEngine/ewpa/src/rsa.o			\
WiFiEngine/ewpa/src/sha1.o			\
WiFiEngine/ewpa/src/sha256.o			\
WiFiEngine/ewpa/src/tls_internal.o		\
WiFiEngine/ewpa/src/tlsv1_client.o		\
WiFiEngine/ewpa/src/tlsv1_common.o		\
WiFiEngine/ewpa/src/wpa.o			\
WiFiEngine/ewpa/src/wpa_supplicant.o		\
WiFiEngine/ewpa/src/pcsc_funcs.o                \
WiFiEngine/ewpa/src/eap_sim_common.o            \
WiFiEngine/ewpa/src/eap_sim.o                   \
WiFiEngine/ewpa/src/x509v3.o

WIFI_ENGINE_INC =			\
driverenv/inc			\
WiFiEngine/802/inc			\
WiFiEngine/80211/inc			\
WiFiEngine/80211macmgmt/inc		\
WiFiEngine/80211macmib/inc		\
WiFiEngine/driverenv/common/inc		\
WiFiEngine/mac_api/inc			\
WiFiEngine/pal/inc			\
WiFiEngine/hic/inc			\
WiFiEngine/hic_wrapper/inc      	\
WiFiEngine/hic_ctrl_msg/inc		\
WiFiEngine/mac_wrapper/inc		\
WiFiEngine/mlme_proxy/inc		\
WiFiEngine/reg/inc			\
WiFiEngine/sys/inc			\
WiFiEngine/ucos/inc			\
WiFiEngine/ucos_port_winhost/inc	\
WiFiEngine/wifi_drv/inc			\
WiFiEngine/ewpa/inc			\
WiFiEngine/logger/inc
