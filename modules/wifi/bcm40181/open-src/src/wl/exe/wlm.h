/*
 * Common (OS-independent) portion of
 * WLM (Wireless LAN Manufacturing) test library.
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlm.h,v 1.12.6.2 2010/12/15 05:18:05 Exp $
 */

#ifndef _wlm_h
#define _wlm_h

#ifdef __cplusplus
extern "C" {
#endif

/**
\mainpage mfgtest_api.h

Overview:

The MFG API DLL allows a user to talk to a DUT by providing functions
that abstract out the lower level details of the driver.

The goal of this API is to make testing easier by providing enough
functionality for the user to perform various tasks without
requiring the user to know anything about the driver internals and,
in addition, any differences there may be between drivers.

*/

#ifndef TRUE
#define TRUE 	1
#endif
#ifndef FALSE
#define FALSE 	0
#endif

#ifndef NULL
#define NULL 0
#endif

#if defined(WIN32)
#define WLM_FUNCTION __declspec(dllexport)
#else
#define WLM_FUNCTION
#endif /* WIN32 */

/* Supported DUT interfaces. */
/* Values are used to select the interface used to talk to the DUT. */
enum {
	WLM_DUT_LOCAL  = 0,  	/* client and DUT on same device */
	WLM_DUT_SERIAL = 1,		/* client to DUT via serial */
	WLM_DUT_SOCKET = 2, 	/* client to DUT via Ethernet */
	WLM_DUT_WIFI   = 3,   	/* client to DUT via WLAN */
	WLM_DUT_DONGLE = 4		/* client to DUT dongle via serial */
};
typedef int WLM_DUT_INTERFACE;

/* Supported server ports. */
/* Values are used to configure server port (for WLM_DUT_SOCKET only). */
enum {
	WLM_DEFAULT_DUT_SERVER_PORT = 8000	/* default */
};
typedef int WLM_DUT_SERVER_PORT;

/* Supported DUT OS. */
/* Values are used to select the OS of the DUT. */
enum {
	WLM_DUT_OS_LINUX = 1,  	/* Linux DUT */
	WLM_DUT_OS_WIN32 = 2 	/* Win32 DUT */
};
typedef int WLM_DUT_OS;

/* Supported join modes. */
/* Values are used to select the mode used to join a network. */
enum {
	WLM_MODE_IBSS = 0,   /* IBSS Mode. (Adhoc) */
	WLM_MODE_BSS = 1,    /* BSS Mode. */
	WLM_MODE_AUTO = 2    /* Auto. */
};
typedef int WLM_JOIN_MODE;

/* Supported bands. */
/* Values are used to select the band used for testing. */
enum {
	WLM_BAND_AUTO = 0,	/* auto select */
	WLM_BAND_5G = 1,        /* 5G Band. */
	WLM_BAND_2G = 2,        /* 2G Band. */
	WLM_BAND_DUAL = WLM_BAND_2G | WLM_BAND_5G	/* Dual Band. */
};
typedef int WLM_BAND;

/* Supported gmode. */
/* Values are used to select gmode used for testing. */
enum {
	WLM_GMODE_LEGACYB = 0,
	WLM_GMODE_AUTO = 1,
	WLM_GMODE_GONLY = 2,
	WLM_GMODE_BDEFERED = 3,
	WLM_GMODE_PERFORMANCE = 4,
	WLM_GMODE_LRS = 5
};
typedef int WLM_GMODE;

/* Supported legacy rates. */
/* Values are used to select the rate used for testing. */
enum {
	WLM_RATE_AUTO = 0,
	WLM_RATE_1M = 2,
	WLM_RATE_2M = 4,
	WLM_RATE_5M5 = 11,
	WLM_RATE_6M = 12,
	WLM_RATE_9M = 18,
	WLM_RATE_11M = 22,
	WLM_RATE_12M = 24,
	WLM_RATE_18M = 36,
	WLM_RATE_24M = 48,
	WLM_RATE_36M = 72,
	WLM_RATE_48M = 96,
	WLM_RATE_54M = 108
};
typedef int WLM_RATE;

/* Supported MCS rates */
enum {
	WLM_MCS_RATE_0 = 0,
	WLM_MCS_RATE_1 = 1,
	WLM_MCS_RATE_2 = 2,
	WLM_MCS_RATE_3 = 3,
	WLM_MCS_RATE_4 = 4,
	WLM_MCS_RATE_5 = 5,
	WLM_MCS_RATE_6 = 6,
	WLM_MCS_RATE_7 = 7,
	WLM_MCS_RATE_8 = 8,
	WLM_MCS_RATE_9 = 9,
	WLM_MCS_RATE_10 = 10,
	WLM_MCS_RATE_11 = 11,
	WLM_MCS_RATE_12 = 12,
	WLM_MCS_RATE_13 = 13,
	WLM_MCS_RATE_14 = 14,
	WLM_MCS_RATE_15 = 15,
	WLM_MCS_RATE_16 = 16,
	WLM_MCS_RATE_17 = 17,
	WLM_MCS_RATE_18 = 18,
	WLM_MCS_RATE_19 = 19,
	WLM_MCS_RATE_20 = 20,
	WLM_MCS_RATE_21 = 21,
	WLM_MCS_RATE_22 = 22,
	WLM_MCS_RATE_23 = 23,
	WLM_MCS_RATE_24 = 24,
	WLM_MCS_RATE_25 = 25,
	WLM_MCS_RATE_26 = 26,
	WLM_MCS_RATE_27 = 27,
	WLM_MCS_RATE_28 = 28,
	WLM_MCS_RATE_29 = 29,
	WLM_MCS_RATE_30 = 30,
	WLM_MCS_RATE_31 = 31,
	WLM_MCS_RATE_32 = 32
};

typedef int WLM_MCS_RATE;

/* Supported STF mode */
enum {
	WLM_STF_MODE_SISO = 0, 	/* stf mode SISO */
	WLM_STF_MODE_CDD = 1,  	/* stf mode CDD  */
	WLM_STF_MODE_STBC = 2,  /* stf mode STBC */
	WLM_STF_MODE_SDM  = 3	/* stf mode SDM  */
};
typedef int WLM_STF_MODE;


/* Supported PLCP preambles. */
/* Values are used to select the preamble used for testing. */
enum {
	WLM_PREAMBLE_AUTO = -1,
	WLM_PREAMBLE_SHORT = 0,
	WLM_PREAMBLE_LONG = 1
};
typedef int WLM_PREAMBLE;

/* Supported MIMO preamble types */
enum {
	WLM_MIMO_MIXEDMODE = 0,
	WLM_MIMO_GREENFIELD = 1
};
typedef int WLM_MIMO_PREAMBLE;

enum {
	WLM_CHAN_FREEQ_RANGE_2G = 0,
	WLM_CHAN_FREEQ_RANGE_5GL,
	WLM_CHAN_FREEQ_RANGE_5GM,
	WLM_CHAN_FREEQ_RANGE_5GH
};
typedef int WLM_BANDRANGE;

/* Supported image formats modes. */
/* Values are used to select the type of image to read/write. */
enum {
	WLM_TYPE_AUTO = 0,  /* Auto mode. */
	WLM_TYPE_SROM = 1,  /* SROM. */
	WLM_TYPE_OTP = 2    /* OTP. */
};
typedef int WLM_IMAGE_TYPE;

/* Supported authentication type. */
/* Values are used to select the authentication type used to join a network. */
enum {
	WLM_TYPE_OPEN = 0,    /* Open */
    WLM_TYPE_SHARED = 1   /* Shared */
};
typedef int WLM_AUTH_TYPE;

/* Supported authentication mode. */
/* Values are used to select the authentication mode used to join a network. */
enum {
	WLM_WPA_AUTH_DISABLED = 0x0000,	/* Legacy (i.e., non-WPA) */
	WLM_WPA_AUTH_NONE = 0x0001,		/* none (IBSS) */
	WLM_WPA_AUTH_PSK = 0x0004,		/* Pre-shared key */
	WLM_WPA2_AUTH_PSK = 0x0080		/* Pre-shared key */
};
typedef int WLM_AUTH_MODE;

/* WLAN Security Encryption. */
/* Values are used to select the type of encryption used for testing. */
enum {
	WLM_ENCRYPT_NONE = 0,    /* No encryption. */
	WLM_ENCRYPT_WEP = 1,     /* WEP encryption. */
	WLM_ENCRYPT_TKIP = 2,    /* TKIP encryption. */
	WLM_ENCRYPT_AES = 4,     /* AES encryption. */
	WLM_ENCRYPT_WSEC = 8,    /* Software WSEC encryption. */
	WLM_ENCRYPT_FIPS = 0x80  /* FIPS encryption. */
};
typedef int WLM_ENCRYPTION;

/* Country abbreviative code */
#define WLM_COUNTRY_ALL "ALL"	/* Default country code */
#define WLM_COUNTRY_JAPAN "JP"     /* Japan country code */
#define WLM_COUNTRY_KOREA "KR"     /* Korea country code */

/* number of 5G channel offsets */
#define CGA_5G_OFFSETS_LEN 24

/* number of 2G channel offsets */
#define CGA_2G_OFFSETS_LEN 14

/* Initialization related functions */


#define WLM_VERSION_STR "wlm version: 10.02.1 (support 4330 and 43236)"

/* Get the WLM version.
 * param[out] buffer version string for current wlm (dll, lib and dylib)
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmWLMVersionGet(const char **buffer);

/* Performs any initialization required internally by the DLL.
 * NOTE: This method needs to be called before any other API function.
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmApiInit(void);

/* Performs any cleanup required internally by the DLL. 
 * NOTE: This method needs to be called by the user at the end of the application.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmApiCleanup(void);

/* Selects the interface to use for talking to the DUT
 *
 * param[in] ifType The desired interface.
 * param[in] ifName Expected value depends on ifType:
 *   For ifType = WLM_DUT_DONGLE, ifName is the COM port to use (ex: "COM1").
 *   For ifType = WLM_DUT_SOCKET, ifName is the IP address to use
 *		(ex: "192.168.1.1" or "localhost").
 * param[in] dutServerPort Server port of DUT (used only for WLM_DUT_SOCKET).
 * param[in] dutOs Operating system of DUT.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSelectInterface(WLM_DUT_INTERFACE ifType, char *ifName,
	WLM_DUT_SERVER_PORT dutServerPort, WLM_DUT_OS dutOs);

/* Gets the WLAN driver version.
 *
 * param[in] buffer Buffer for the version string.
 * param[in] length Maximum length of buffer.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmVersionGet(char *buffer, int length);

/* Enables or disables wireless adapter.
 *
 * param[in] enable Set to true to bring adapter up, false to bring down.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmEnableAdapterUp(int enable);

/* Check if adapter is currently up.
 *
 * param[in] up Return the up/down state of adapter.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmIsAdapterUp(int *up);

/* Enables or disables Minimum Power Consumption (MPC).
 * MPC will automatically turn off the radio if not associated to a network.
 *
 * param[in] enable Set to true to enable MPC, false to disable MPC.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmMinPowerConsumption(int enable);

/* Sets the operating channel of the DUT.
 *
 * param[in] channel Desired channel.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmChannelSet(int channel);

/* Sets the operating legacy rate (bg_rate or a_rate) for the DUT.
 *
 * param[in] rate Desired rate.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRateSet(WLM_RATE rate);

/* Sets the operating legacy rate (nrate) of the DUT.
 *
 * param[in] rate Desired rate.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmLegacyRateSet(WLM_RATE rate);


/* Sets the operating mcs rate and STF of the DUT
 * param[in] Desired mcs rate [0, 7] for SISO (single-in-single-out) device.
 * param[in] Stf mode, 0=SISO, 1= CDD, 2=STBC, 3=SDM
 * return - True for sucess, false for failure.
*/
WLM_FUNCTION
int wlmMcsRateSet(WLM_MCS_RATE mcs_rate, WLM_STF_MODE stf_mode);

/* Sets the PLCP preamble.
 *
 * param[in] preamble Desired preamble.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPreambleSet(WLM_PREAMBLE preamble);

/* Set the band.
 *
 * param[in] The Desired band.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmBandSet(WLM_BAND band);

/* Get available bands.
 *
 * param[in] Available bands returned in the form of WLM_BANDs added.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmGetBandList(WLM_BAND * bands);

/* Set gmode.
 *
 * param[in] The desired gmode.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmGmodeSet(WLM_GMODE gmode);

/* Sets the receive antenna.
 *
 * param[in] antenna Desired antenna.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRxAntSet(int antenna);

/* Sets the transmit antenna.
 *
 * param[in] antenna Desired antenna.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxAntSet(int antenna);

/* Retrieves the current estimated power in milli-dBm.
 *
 * param[out] estPower Power value returned.
 * param[in] chain For MIMO device, specifies which chain to retrieve power value from.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmEstimatedPowerGet(int *estPower, int chain);

/* Retrieves the current TX power in milli-dB.
 *
 * param[in] power Power value returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxPowerGet(int *power);

/* Sets the current TX power.
 *
 * param[in] powerValue Desired power value. Expected to be in milli-dB. Use -1 to restore default.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxPowerSet(int powerValue);

/* Retrieves the tx power control parameters for SISO system.
 *
 * param[in] bandrange. The desired band range for getting PA parameters.
 * param[out] b0 PA parameter returned.
 * param[out] b1 PA parameter returned.
 * param[out] a1 PA parameter returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPaParametersGet(WLM_BANDRANGE bandrange,
	unsigned int *a1, unsigned int *b0, unsigned int *b1);

/* Sets tx power control parameters for SISO system.
 *
 * param[in] bandrange. The desired band range for getting PA parameters.
 * param[in] b0 PA parameter.
 * param[in] b1 PA parameter.
 * param[in] a1 PA parameter.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPaParametersSet(WLM_BANDRANGE bandrange,
	unsigned int a1, unsigned int b0, unsigned int b1);

/* Retrieves the tx power control parameters for MIMO system.
 *
 * param[in] bandrange The desired band range for getting PA parameters.
 * param[in] chain. The desired tx chain for getting PA parameters 
 * param[out] b0 PA parameter returned.
 * param[out] b1 PA parameter returned.
 * param[out] a1 PA parameter returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmMIMOPaParametersGet(WLM_BANDRANGE bandrange, int chain,
	unsigned int *a1, unsigned int *b0, unsigned int *b1);

/* Sets tx power control parameters for MIMO system.
 *
 * param[in] bandrange. The desired band range for getting PA parameters.
 * param[in] chain. The desired tx chain for getting PA parameters.
 * param[in] b0 PA parameter.
 * param[in] b1 PA parameter.
 * param[in] a1 PA parameter.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmMIMOPaParametersSet(WLM_BANDRANGE bandrange, int chain,
	unsigned int a1, unsigned int b0, unsigned int b1);

/* Retrieves the current MAC address of the device.
 *
 * param[in] macAddr MAC address returned.
 * param[in] length Length of macAddr buffer.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmMacAddrGet(char *macAddr, int length);

/* Sets the MAC address of the device.
 *
 * param[in] macAddr The desired MAC address. Expected format is "XX:XX:XX:XX:XX:XX".
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmMacAddrSet(const char* macAddr);

/* Enables or disables output of a carrier tone.
 *
 * param[in] enable Set to true to enable carrier tone, false to disable.
 * param[in] channel Desired channel. Ignored if <i>enable</i> is false.
 * 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmEnableCarrierTone(int enable, int channel);

/* Enables or disables an EVM test.
 *
 * param[in] enable Set to true to enable EVM test, false to disable.
 * param[in] channel Desired channel. Ignored if <i>enable</i> is false.
 * param[in] rate Desired rate. Ignored if <i>enable</i> is false.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmEnableEVMTest(int enable, WLM_RATE rate, int channel);

/* RX/TX related functions */

/* Starts sending packets with supplied parameters.
 *
 * param[in] shortPreamble Set to true to use a short preamble, false for a long preamble.
 * param[in] interPacketDelay Delay between each packet.
 * param[in] numPackets The number of packets transmitted.
 * param[in] packetLength Length of packet transmitted.
 * param[in] destMac The MAC address of the destination.
 * param[in] withAck Ack response expected for transmitted packet.
 * param[in] syncMode Enable synchronous mode to transmit packets before returning.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxPacketStart(unsigned int interPacketDelay,
	unsigned int numPackets, unsigned int packetLength,
	const char* destMac, int withAck, int syncMode);

/* Stops sending packets.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxPacketStop(void);

/* Starts receiving packets with supplied parameters.
 *
 * param[in] srcMac The MAC address of the packet source..
 * param[in] withAck Ack response expected for received packet.
 * param[in] syncMode Enable synchronous mode to receive packets before returning or timeout.
 * param[in] numPackets Number of receive packets before returning (sync mode only).
 * param[in] timeout Maximum timeout in msec (sync mode only).
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRxPacketStart(const char* srcMac, int withAck,
	int syncMode, unsigned int numPackets, unsigned int timeout);

/* Stops receiving packets.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRxPacketStop(void);

/* Returns number of packets that were ACKed successfully.
 *
 * param[in] count Packet count returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxGetAckedPackets(unsigned int *count);

/* Returns number of packets received successfully.
 *
 * param[in] count Packet count returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRxGetReceivedPackets(unsigned int *count);

/* Returns Receive Signal Strength Indicator (RSSI).
 *
 * param[in] rssi RSSI returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRssiGet(int *rssi);

/* Sequence related functions */

/* Initiates a command batch sequence.
 * Any commands issued after calling this function will be queued
 * instead of being executed. Call sequenceStop() to run queued commands.
 *
 * param[in] clientBatching Set to true to batch on client, false to batch on server/DUT.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSequenceStart(int clientBatching);

/* Signals the end of a command batch sequence.
 * Requires a previous call to sequenceStart(). Any commands
 * issued after previously calling sequenceStart() will be run.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSequenceStop(void);

/* Sets a delay between commands, in ms.
 * Requires a previous call to sequenceStart(). Once sequenceStart() is called,
 * calling sequenceDelay() after a command will insert a delay after the
 * previous command is complete.
 *
 * param[in] msec Amount of time to wait after previous command has complete.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSequenceDelay(int msec);

/* If a command sequence fails at a particular command, the remaining
 * commands will be aborted. Calling this function, will return the index
 * of which command failed. NOTE: Index starts at 1.
 *
 * param[in] index Failed command index returned.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSequenceErrorIndex(int *index);

/*  Image access (SROM/OTP) functions: */

/* Programs the device with a supplied image. Used to program the SROM/OTP.
 *
 * param[in] byteStream A stream of bytes loaded from an image file.
 * param[in] length The number of bytes to write.
 * param[in] imageType Use this to force a specific image type (ex: SROM, OTP, etc).
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmDeviceImageWrite(const char* byteStream, int length, WLM_IMAGE_TYPE imageType);

/* Dumps the image from the device. Used to read the SROM/OTP.
 *
 * param[out] byteStream A user supplied buffer.
 * param[in] length The size of the buffer supplied by byteStream.
 * param[in] imageType Use this to force a specific image type (ex: SROM, OTP, etc).
 *
 * return - Size of image in bytes. -1 if size of image is larger than supplied buffer.
 */
WLM_FUNCTION
int wlmDeviceImageRead(char* byteStream, unsigned int length, WLM_IMAGE_TYPE imageType);

/* Network related functions */

/* Set the current wireless security.
 *
 * param[in] authType Desired authentication type.
 * param[in] authMode Desired authentication mode.
 * param[in] encryption The desired encryption method.
 * param[in] key The encryption key string (null terminated).
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSecuritySet(WLM_AUTH_TYPE authType, WLM_AUTH_MODE authMode,
	WLM_ENCRYPTION encryption, const char *key);

/* Joins a  network.
 *
 * param[in] ssid The SSID of the network to join.
 * param[in] mode The mode used for the join.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmJoinNetwork(const char* ssid, WLM_JOIN_MODE mode);

/* Disassociates from a currently joined network.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmDisassociateNetwork(void);

/* Retrieves the current SSID of the AP.
 *
 * param[in] ssid SSID returned. SSID may be returned if not associated
 * (i.e. attempting to associate, roaming).
 * param[in] length Length of ssid buffer.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSsidGet(char *ssid, int length);

/* Retrieves the current BSSID of the AP.
 *
 * param[in] bssid BSSID returned. If the device is not associated, empty string returned.
 * param[in] length Length of bssid buffer.
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmBssidGet(char *ssid, int length);

/* Set/Get the MIMO Preamble mode
 * Valid options are:
 * WLC_N_PREAMBLE_MIXEDMODE 0
 *  WLC_N_PREAMBLE_GF 1
 */

WLM_FUNCTION
int wlmMimoPreambleSet(int type);

WLM_FUNCTION
int wlmMimoPreambleGet(int* type);

/* Set/Get the CGA coefficients in the 2.4G/5G band 
 * requires an array of 24 uint8s to be set
 * and a specified length of 24 to get
 * All of these CGA value query functions only applies to 4329 solution  
 */

WLM_FUNCTION
int wlmCga5gOffsetsSet(char* values, int len);

WLM_FUNCTION
int wlmCga5gOffsetsGet(char* buffer, int len);

WLM_FUNCTION
int wlmCga2gOffsetsSet(char* values, int len);

WLM_FUNCTION
int wlmCga2gOffsetsGet(char* buffer, int len);

/* Set Glacial Timer 
 * param[in] timer duration in msec
 *
 * return - True for success, false for failure.
*/
WLM_FUNCTION
int wlmGlacialTimerSet(int val);

/* Set Fast Timer 
 * param[in] timer duration in msec
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmFastTimerSet(int val);

/* Set Slow Timer 
 * param[in] timer duration in msec
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSlowTimerSet(int val);

/* Enable/Disable Scansuppress 
 * param[in] set to TRUE enables scansuppress, set to false disable scansuppress
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmScanSuppress(int val);

/* Set Country Code 
 * param[in] country code abbrv. Default use WLM_COUNTRY_CODE_ABBRV_ALL
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmCountryCodeSet(const char *country_name);

/* Set fullcal 
 * Trigger lpphy fullcal
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmFullCal(void);

/* Get Receiver IQ Estimation 
 * param[out] estimated rxiq power in dBm at 0.25dBm resolution
 * param[in] sampel count, 0 to 15
 * param[in] antenna, 0 to 3  
 * return - True for success, false for failure. 
*/
WLM_FUNCTION
int wlmRxIQEstGet(float *val, int sampleCount, int ant);

/* Get PHY txpwrindex  
 * param[out] txpwrindex 
 * param[in] chip id: 4325, 4329, 43291, 4330, 4336 and 43236 
 */
WLM_FUNCTION
int wlmPHYTxPowerIndexGet(unsigned int *val, const char *chipid);

/* Set PHY txpwrindex  
 * param[in] txpwrindex
 * param[in] chip id: 4325, 4329, 43291, 4330, 4336 and 43236 
 */
WLM_FUNCTION
int wlmPHYTxPowerIndexSet(unsigned int val, const char *chipid);

/* Enable/Disable RIFS  
 * param[in] Set RIFS mode. 1 = enable ; 0 = disable
 */
WLM_FUNCTION
int wlmRIFSEnable(int enable);

/* Get/Set IOCTL
 * param[in] cmd IOCTL command.
 * param[in] buf Get data returned or set data input.
 * param[in] len Length of buf.
 * return - True for success, false for failure.
 */
WLM_FUNCTION int wlmIoctlGet(int cmd, void *buf, int len);
WLM_FUNCTION int wlmIoctlSet(int cmd, void *buf, int len);

/* Get/Set IOVAR
 * param[in] iovar IOVAR command.
 * param[in] buf Get data returned or set data input.
 * param[in] len Length of buf.
 * return - True for success, false for failure.
 */
WLM_FUNCTION int wlmIovarGet(const char *iovar, void *buf, int len);
WLM_FUNCTION int wlmIovarSet(const char *iovar, void *buf, int len);

/* Get/Set IOVAR integer.
 * param[in] iovar IOVAR integer command.
 * param[out/in] val Get integer returned or set integer input.
 * return - True for success, false for failure.
 */
WLM_FUNCTION int wlmIovarIntegerGet(const char *iovar, int *val);
WLM_FUNCTION int wlmIovarIntegerSet(const char *iovar, int val);

/* Get/Set IOVAR using internal buffer.
 * param[in] iovar IOVAR command.
 * param[in] param Input parameters.
 * param[in] param_len Length of parameters.
 * param[out] bufptr Buffer returning get data.
 * return - True for success, false for failure.
 */
WLM_FUNCTION int wlmIovarBufferGet(const char *iovar, void *param, int param_len, void **bufptr);
WLM_FUNCTION int wlmIovarBufferSet(const char *iovar, void *param, int param_len);

#ifdef SERDOWNLOAD
WLM_FUNCTION int wlmDhdDownload(const char *firmware, const char* vars);
WLM_FUNCTION int wlmDhdInit(const char *chip);
#endif


/* Enables radio
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRadioOn(void);

/* Disable radio .
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRadioOff(void);

/* Set Power Saving Mode 
 * param[in] 0 = CAM, 1 =Power Save, 2 = Fast PS Mode 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPMmode(int val);

/* Enable roaming
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRoamingOff(void);

/* Disable roaming
 *
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRoamingOn(void);

/* Get roaming trigger level
 * param[out] roaming trigger level in dBm returned
 * param[in]  frequency band
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRoamTriggerLevelGet(int *val, WLM_BAND band);

/* Set roaming trigger level
 * param[in] roaming trigger level in dBm 
 * param[in]  frequence band
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRoamTriggerLevelSet(int val, WLM_BAND band);

/* Set framburst mode on
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmFrameBurstOn(void);

/* Set framburst mode off
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmFrameBurstOff(void);

/* Set beacon interval 
 * param[in] beacon interval in ms
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmBeaconIntervalSet(int val);

/* Set AMPDU mode on/off 
 * param[in] on = 1, off = 0
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmAMPDUModeSet(int val);

/* Set MIMO bandwidth capability 
 * param[in] mimo bandwidth capability. 0 = 20Mhz, 1 =40Mhz
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmMIMOBandwidthCapabilitySet(int val);

/* Set interference on/off
 * param[in] on = 1, 0ff = 0 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmInterferenceSet(int val);

/* Set interferenceoverride on/off
 * param[in] on = 1, 0ff = 0  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmInterferenceOverrideSet(int val);

/* Set MIMO transmit banddwith 
 * param[in] auto = -1, 2 = 20Mhz, 3 = 20Mhz upper , 4 =40 Mhz, 5 =40dup (mcs32 only)  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTransmitBandwidthSet(int val);

/* Set MIMO short guard intervaltransmit banddwith 
 * param[in] auto = -1, 1 = on, 0 = off  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmShortGuardIntervalSet(int val);

/* Set MIMO OBSS coex set 
 * param[in] auto = -1, 1 = on, 0 = off  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmObssCoexSet(int val);

/* Set PHY Periodical call 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPHYPeriodicalCalSet(void);

/* Set PHY Force call 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPHYForceCalSet(void);

/* Disable scrambler update 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPHYScramblerUpdateDisable(void);

/* Enable scrambler update 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPHYScramblerUpdateEnable(void);

/* Turn PHY watchdog on/off
 * param[in] on = 1, off = 0  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmPHYWatchdogSet(int val);

/* Disable temperature sensore 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTemperatureSensorDisable(void);

/* Enable temperature sensore 
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTemperatureSensorEnable(void);

/* Set transmit core
 * param[in] 
 * return - True for success, false for failure.
 */ 
WLM_FUNCTION
int wlmTransmitCoreSet(int val);

/* Get temperation sensor read
 * param[out] chip core temperature in F
 * return - True for success, false for failure.
 */ 
WLM_FUNCTION
int wlmPhyTempSenseGet(int *val);

/* Get chip OTP Fab ID
 * param[out] chip fab id
 * return - True for success, false for failure.
 */ 
WLM_FUNCTION
int wlmOtpFabidGet(int *val);

/* Set mimo channnel specifications
 * param[in] channel
 * param[in] bandwidth 20 = 20Mhz, 40 = 40Mhz
 * param[in] sideband 1 = upper, -1 = lower, 0 = none
 * return - True for success, false for failure.
 */ 
WLM_FUNCTION
int wlmChannelSpecSet(int channel, int bandwidth, int sideband);

/* Set rts threshold 
 * param[in] rts threshold value
 * return - True for success, false for failure.
 */ 
WLM_FUNCTION
int wlmRtsThresholdOverride(int val);

/* Turn STBC Tx mode on/off
 * param[in] on = 1, off = 0  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSTBCTxSet(int val);

/* Turn STBC Rx mode on/off
 * param[in] on = 1, off = 0  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmSTBCRxSet(int val);

/* MIMO single stream tx chain selection
 * param[in] chain 1 = 1, chain 2 = 2  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmTxChainSet(int val);

/* MIMO single stream rx chain selection
 * param[in] chain 1 = 1, chain 2 = 2  
 * return - True for success, false for failure.
 */
WLM_FUNCTION
int wlmRxChainSet(int val);

#ifdef __cplusplus
}
#endif

#endif /* _wlm_h */
