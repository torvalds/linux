/*
 * This file define a set of standard wireless extensions
 *
 * Version :	22	16.3.07
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2007 Jean Tourrilhes, All Rights Reserved.
 */

#ifndef _LINUX_WIRELESS_H
#define _LINUX_WIRELESS_H

/************************** DOCUMENTATION **************************/
/*
 * Initial APIs (1996 -> onward) :
 * -----------------------------
 * Basically, the wireless extensions are for now a set of standard ioctl
 * call + /proc/net/wireless
 *
 * The entry /proc/net/wireless give statistics and information on the
 * driver.
 * This is better than having each driver having its entry because
 * its centralised and we may remove the driver module safely.
 *
 * Ioctl are used to configure the driver and issue commands.  This is
 * better than command line options of insmod because we may want to
 * change dynamically (while the driver is running) some parameters.
 *
 * The ioctl mechanimsm are copied from standard devices ioctl.
 * We have the list of command plus a structure descibing the
 * data exchanged...
 * Note that to add these ioctl, I was obliged to modify :
 *	# net/core/dev.c (two place + add include)
 *	# net/ipv4/af_inet.c (one place + add include)
 *
 * /proc/net/wireless is a copy of /proc/net/dev.
 * We have a structure for data passed from the driver to /proc/net/wireless
 * Too add this, I've modified :
 *	# net/core/dev.c (two other places)
 *	# include/linux/netdevice.h (one place)
 *	# include/linux/proc_fs.h (one place)
 *
 * New driver API (2002 -> onward) :
 * -------------------------------
 * This file is only concerned with the user space API and common definitions.
 * The new driver API is defined and documented in :
 *	# include/net/iw_handler.h
 *
 * Note as well that /proc/net/wireless implementation has now moved in :
 *	# net/core/wireless.c
 *
 * Wireless Events (2002 -> onward) :
 * --------------------------------
 * Events are defined at the end of this file, and implemented in :
 *	# net/core/wireless.c
 *
 * Other comments :
 * --------------
 * Do not add here things that are redundant with other mechanisms
 * (drivers init, ifconfig, /proc/net/dev, ...) and with are not
 * wireless specific.
 *
 * These wireless extensions are not magic : each driver has to provide
 * support for them...
 *
 * IMPORTANT NOTE : As everything in the kernel, this is very much a
 * work in progress. Contact me if you have ideas of improvements...
 */

/***************************** INCLUDES *****************************/

/* This header is used in user-space, therefore need to be sanitised
 * for that purpose. Those includes are usually not compatible with glibc.
 * To know which includes to use in user-space, check iwlib.h. */
#ifdef __KERNEL__
#include <linux/types.h>		/* for "caddr_t" et al		*/
#include <linux/socket.h>		/* for "struct sockaddr" et al	*/
#include <linux/if.h>			/* for IFNAMSIZ and co... */
#endif	/* __KERNEL__ */

/***************************** VERSION *****************************/
/*
 * This constant is used to know the availability of the wireless
 * extensions and to know which version of wireless extensions it is
 * (there is some stuff that will be added in the future...)
 * I just plan to increment with each new version.
 */
#define WIRELESS_EXT	22

/*
 * Changes :
 *
 * V2 to V3
 * --------
 *	Alan Cox start some incompatibles changes. I've integrated a bit more.
 *	- Encryption renamed to Encode to avoid US regulation problems
 *	- Frequency changed from float to struct to avoid problems on old 386
 *
 * V3 to V4
 * --------
 *	- Add sensitivity
 *
 * V4 to V5
 * --------
 *	- Missing encoding definitions in range
 *	- Access points stuff
 *
 * V5 to V6
 * --------
 *	- 802.11 support (ESSID ioctls)
 *
 * V6 to V7
 * --------
 *	- define IW_ESSID_MAX_SIZE and IW_MAX_AP
 *
 * V7 to V8
 * --------
 *	- Changed my e-mail address
 *	- More 802.11 support (nickname, rate, rts, frag)
 *	- List index in frequencies
 *
 * V8 to V9
 * --------
 *	- Support for 'mode of operation' (ad-hoc, managed...)
 *	- Support for unicast and multicast power saving
 *	- Change encoding to support larger tokens (>64 bits)
 *	- Updated iw_params (disable, flags) and use it for NWID
 *	- Extracted iw_point from iwreq for clarity
 *
 * V9 to V10
 * ---------
 *	- Add PM capability to range structure
 *	- Add PM modifier : MAX/MIN/RELATIVE
 *	- Add encoding option : IW_ENCODE_NOKEY
 *	- Add TxPower ioctls (work like TxRate)
 *
 * V10 to V11
 * ----------
 *	- Add WE version in range (help backward/forward compatibility)
 *	- Add retry ioctls (work like PM)
 *
 * V11 to V12
 * ----------
 *	- Add SIOCSIWSTATS to get /proc/net/wireless programatically
 *	- Add DEV PRIVATE IOCTL to avoid collisions in SIOCDEVPRIVATE space
 *	- Add new statistics (frag, retry, beacon)
 *	- Add average quality (for user space calibration)
 *
 * V12 to V13
 * ----------
 *	- Document creation of new driver API.
 *	- Extract union iwreq_data from struct iwreq (for new driver API).
 *	- Rename SIOCSIWNAME as SIOCSIWCOMMIT
 *
 * V13 to V14
 * ----------
 *	- Wireless Events support : define struct iw_event
 *	- Define additional specific event numbers
 *	- Add "addr" and "param" fields in union iwreq_data
 *	- AP scanning stuff (SIOCSIWSCAN and friends)
 *
 * V14 to V15
 * ----------
 *	- Add IW_PRIV_TYPE_ADDR for struct sockaddr private arg
 *	- Make struct iw_freq signed (both m & e), add explicit padding
 *	- Add IWEVCUSTOM for driver specific event/scanning token
 *	- Add IW_MAX_GET_SPY for driver returning a lot of addresses
 *	- Add IW_TXPOW_RANGE for range of Tx Powers
 *	- Add IWEVREGISTERED & IWEVEXPIRED events for Access Points
 *	- Add IW_MODE_MONITOR for passive monitor
 *
 * V15 to V16
 * ----------
 *	- Increase the number of bitrates in iw_range to 32 (for 802.11g)
 *	- Increase the number of frequencies in iw_range to 32 (for 802.11b+a)
 *	- Reshuffle struct iw_range for increases, add filler
 *	- Increase IW_MAX_AP to 64 for driver returning a lot of addresses
 *	- Remove IW_MAX_GET_SPY because conflict with enhanced spy support
 *	- Add SIOCSIWTHRSPY/SIOCGIWTHRSPY and "struct iw_thrspy"
 *	- Add IW_ENCODE_TEMP and iw_range->encoding_login_index
 *
 * V16 to V17
 * ----------
 *	- Add flags to frequency -> auto/fixed
 *	- Document (struct iw_quality *)->updated, add new flags (INVALID)
 *	- Wireless Event capability in struct iw_range
 *	- Add support for relative TxPower (yick !)
 *
 * V17 to V18 (From Jouni Malinen <j@w1.fi>)
 * ----------
 *	- Add support for WPA/WPA2
 *	- Add extended encoding configuration (SIOCSIWENCODEEXT and
 *	  SIOCGIWENCODEEXT)
 *	- Add SIOCSIWGENIE/SIOCGIWGENIE
 *	- Add SIOCSIWMLME
 *	- Add SIOCSIWPMKSA
 *	- Add struct iw_range bit field for supported encoding capabilities
 *	- Add optional scan request parameters for SIOCSIWSCAN
 *	- Add SIOCSIWAUTH/SIOCGIWAUTH for setting authentication and WPA
 *	  related parameters (extensible up to 4096 parameter values)
 *	- Add wireless events: IWEVGENIE, IWEVMICHAELMICFAILURE,
 *	  IWEVASSOCREQIE, IWEVASSOCRESPIE, IWEVPMKIDCAND
 *
 * V18 to V19
 * ----------
 *	- Remove (struct iw_point *)->pointer from events and streams
 *	- Remove header includes to help user space
 *	- Increase IW_ENCODING_TOKEN_MAX from 32 to 64
 *	- Add IW_QUAL_ALL_UPDATED and IW_QUAL_ALL_INVALID macros
 *	- Add explicit flag to tell stats are in dBm : IW_QUAL_DBM
 *	- Add IW_IOCTL_IDX() and IW_EVENT_IDX() macros
 *
 * V19 to V20
 * ----------
 *	- RtNetlink requests support (SET/GET)
 *
 * V20 to V21
 * ----------
 *	- Remove (struct net_device *)->get_wireless_stats()
 *	- Change length in ESSID and NICK to strlen() instead of strlen()+1
 *	- Add IW_RETRY_SHORT/IW_RETRY_LONG retry modifiers
 *	- Power/Retry relative values no longer * 100000
 *	- Add explicit flag to tell stats are in 802.11k RCPI : IW_QUAL_RCPI
 *
 * V21 to V22
 * ----------
 *	- Prevent leaking of kernel space in stream on 64 bits.
 */

/**************************** CONSTANTS ****************************/

/* -------------------------- IOCTL LIST -------------------------- */

/* Wireless Identification */
#define SIOCSIWCOMMIT	0x8B00		/* Commit pending changes to driver */
#define SIOCGIWNAME	0x8B01		/* get name == wireless protocol */
/* SIOCGIWNAME is used to verify the presence of Wireless Extensions.
 * Common values : "IEEE 802.11-DS", "IEEE 802.11-FH", "IEEE 802.11b"...
 * Don't put the name of your driver there, it's useless. */

/* Basic operations */
#define SIOCSIWNWID	0x8B02		/* set network id (pre-802.11) */
#define SIOCGIWNWID	0x8B03		/* get network id (the cell) */
#define SIOCSIWFREQ	0x8B04		/* set channel/frequency (Hz) */
#define SIOCGIWFREQ	0x8B05		/* get channel/frequency (Hz) */
#define SIOCSIWMODE	0x8B06		/* set operation mode */
#define SIOCGIWMODE	0x8B07		/* get operation mode */
#define SIOCSIWSENS	0x8B08		/* set sensitivity (dBm) */
#define SIOCGIWSENS	0x8B09		/* get sensitivity (dBm) */

/* Informative stuff */
#define SIOCSIWRANGE	0x8B0A		/* Unused */
#define SIOCGIWRANGE	0x8B0B		/* Get range of parameters */
#define SIOCSIWPRIV	0x8B0C		/* Unused */
#define SIOCGIWPRIV	0x8B0D		/* get private ioctl interface info */
#define SIOCSIWSTATS	0x8B0E		/* Unused */
#define SIOCGIWSTATS	0x8B0F		/* Get /proc/net/wireless stats */
/* SIOCGIWSTATS is strictly used between user space and the kernel, and
 * is never passed to the driver (i.e. the driver will never see it). */

/* Spy support (statistics per MAC address - used for Mobile IP support) */
#define SIOCSIWSPY	0x8B10		/* set spy addresses */
#define SIOCGIWSPY	0x8B11		/* get spy info (quality of link) */
#define SIOCSIWTHRSPY	0x8B12		/* set spy threshold (spy event) */
#define SIOCGIWTHRSPY	0x8B13		/* get spy threshold */

/* Access Point manipulation */
#define SIOCSIWAP	0x8B14		/* set access point MAC addresses */
#define SIOCGIWAP	0x8B15		/* get access point MAC addresses */
#define SIOCGIWAPLIST	0x8B17		/* Deprecated in favor of scanning */
#define SIOCSIWSCAN	0x8B18		/* trigger scanning (list cells) */
#define SIOCGIWSCAN	0x8B19		/* get scanning results */

/* 802.11 specific support */
#define SIOCSIWESSID	0x8B1A		/* set ESSID (network name) */
#define SIOCGIWESSID	0x8B1B		/* get ESSID */
#define SIOCSIWNICKN	0x8B1C		/* set node name/nickname */
#define SIOCGIWNICKN	0x8B1D		/* get node name/nickname */
/* As the ESSID and NICKN are strings up to 32 bytes long, it doesn't fit
 * within the 'iwreq' structure, so we need to use the 'data' member to
 * point to a string in user space, like it is done for RANGE... */

/* Other parameters useful in 802.11 and some other devices */
#define SIOCSIWRATE	0x8B20		/* set default bit rate (bps) */
#define SIOCGIWRATE	0x8B21		/* get default bit rate (bps) */
#define SIOCSIWRTS	0x8B22		/* set RTS/CTS threshold (bytes) */
#define SIOCGIWRTS	0x8B23		/* get RTS/CTS threshold (bytes) */
#define SIOCSIWFRAG	0x8B24		/* set fragmentation thr (bytes) */
#define SIOCGIWFRAG	0x8B25		/* get fragmentation thr (bytes) */
#define SIOCSIWTXPOW	0x8B26		/* set transmit power (dBm) */
#define SIOCGIWTXPOW	0x8B27		/* get transmit power (dBm) */
#define SIOCSIWRETRY	0x8B28		/* set retry limits and lifetime */
#define SIOCGIWRETRY	0x8B29		/* get retry limits and lifetime */

/* Encoding stuff (scrambling, hardware security, WEP...) */
#define SIOCSIWENCODE	0x8B2A		/* set encoding token & mode */
#define SIOCGIWENCODE	0x8B2B		/* get encoding token & mode */
/* Power saving stuff (power management, unicast and multicast) */
#define SIOCSIWPOWER	0x8B2C		/* set Power Management settings */
#define SIOCGIWPOWER	0x8B2D		/* get Power Management settings */

/* WPA : Generic IEEE 802.11 informatiom element (e.g., for WPA/RSN/WMM).
 * This ioctl uses struct iw_point and data buffer that includes IE id and len
 * fields. More than one IE may be included in the request. Setting the generic
 * IE to empty buffer (len=0) removes the generic IE from the driver. Drivers
 * are allowed to generate their own WPA/RSN IEs, but in these cases, drivers
 * are required to report the used IE as a wireless event, e.g., when
 * associating with an AP. */
#define SIOCSIWGENIE	0x8B30		/* set generic IE */
#define SIOCGIWGENIE	0x8B31		/* get generic IE */

/* WPA : IEEE 802.11 MLME requests */
#define SIOCSIWMLME	0x8B16		/* request MLME operation; uses
					 * struct iw_mlme */
/* WPA : Authentication mode parameters */
#define SIOCSIWAUTH	0x8B32		/* set authentication mode params */
#define SIOCGIWAUTH	0x8B33		/* get authentication mode params */

/* WPA : Extended version of encoding configuration */
#define SIOCSIWENCODEEXT 0x8B34		/* set encoding token & mode */
#define SIOCGIWENCODEEXT 0x8B35		/* get encoding token & mode */

/* WPA2 : PMKSA cache management */
#define SIOCSIWPMKSA	0x8B36		/* PMKSA cache operation */

/* -------------------- DEV PRIVATE IOCTL LIST -------------------- */

/* These 32 ioctl are wireless device private, for 16 commands.
 * Each driver is free to use them for whatever purpose it chooses,
 * however the driver *must* export the description of those ioctls
 * with SIOCGIWPRIV and *must* use arguments as defined below.
 * If you don't follow those rules, DaveM is going to hate you (reason :
 * it make mixed 32/64bit operation impossible).
 */
#define SIOCIWFIRSTPRIV	0x8BE0
#define SIOCIWLASTPRIV	0x8BFF
/* Previously, we were using SIOCDEVPRIVATE, but we now have our
 * separate range because of collisions with other tools such as
 * 'mii-tool'.
 * We now have 32 commands, so a bit more space ;-).
 * Also, all 'even' commands are only usable by root and don't return the
 * content of ifr/iwr to user (but you are not obliged to use the set/get
 * convention, just use every other two command). More details in iwpriv.c.
 * And I repeat : you are not forced to use them with iwpriv, but you
 * must be compliant with it.
 */

/* ------------------------- IOCTL STUFF ------------------------- */

/* The first and the last (range) */
#define SIOCIWFIRST	0x8B00
#define SIOCIWLAST	SIOCIWLASTPRIV		/* 0x8BFF */
#define IW_IOCTL_IDX(cmd)	((cmd) - SIOCIWFIRST)

/* Odd : get (world access), even : set (root access) */
#define IW_IS_SET(cmd)	(!((cmd) & 0x1))
#define IW_IS_GET(cmd)	((cmd) & 0x1)

/* ----------------------- WIRELESS EVENTS ----------------------- */
/* Those are *NOT* ioctls, do not issue request on them !!! */
/* Most events use the same identifier as ioctl requests */

#define IWEVTXDROP	0x8C00		/* Packet dropped to excessive retry */
#define IWEVQUAL	0x8C01		/* Quality part of statistics (scan) */
#define IWEVCUSTOM	0x8C02		/* Driver specific ascii string */
#define IWEVREGISTERED	0x8C03		/* Discovered a new node (AP mode) */
#define IWEVEXPIRED	0x8C04		/* Expired a node (AP mode) */
#define IWEVGENIE	0x8C05		/* Generic IE (WPA, RSN, WMM, ..)
					 * (scan results); This includes id and
					 * length fields. One IWEVGENIE may
					 * contain more than one IE. Scan
					 * results may contain one or more
					 * IWEVGENIE events. */
#define IWEVMICHAELMICFAILURE 0x8C06	/* Michael MIC failure
					 * (struct iw_michaelmicfailure)
					 */
#define IWEVASSOCREQIE	0x8C07		/* IEs used in (Re)Association Request.
					 * The data includes id and length
					 * fields and may contain more than one
					 * IE. This event is required in
					 * Managed mode if the driver
					 * generates its own WPA/RSN IE. This
					 * should be sent just before
					 * IWEVREGISTERED event for the
					 * association. */
#define IWEVASSOCRESPIE	0x8C08		/* IEs used in (Re)Association
					 * Response. The data includes id and
					 * length fields and may contain more
					 * than one IE. This may be sent
					 * between IWEVASSOCREQIE and
					 * IWEVREGISTERED events for the
					 * association. */
#define IWEVPMKIDCAND	0x8C09		/* PMKID candidate for RSN
					 * pre-authentication
					 * (struct iw_pmkid_cand) */

#define IWEVFIRST	0x8C00
#define IW_EVENT_IDX(cmd)	((cmd) - IWEVFIRST)

/* ------------------------- PRIVATE INFO ------------------------- */
/*
 * The following is used with SIOCGIWPRIV. It allow a driver to define
 * the interface (name, type of data) for its private ioctl.
 * Privates ioctl are SIOCIWFIRSTPRIV -> SIOCIWLASTPRIV
 */

#define IW_PRIV_TYPE_MASK	0x7000	/* Type of arguments */
#define IW_PRIV_TYPE_NONE	0x0000
#define IW_PRIV_TYPE_BYTE	0x1000	/* Char as number */
#define IW_PRIV_TYPE_CHAR	0x2000	/* Char as character */
#define IW_PRIV_TYPE_INT	0x4000	/* 32 bits int */
#define IW_PRIV_TYPE_FLOAT	0x5000	/* struct iw_freq */
#define IW_PRIV_TYPE_ADDR	0x6000	/* struct sockaddr */

#define IW_PRIV_SIZE_FIXED	0x0800	/* Variable or fixed number of args */

#define IW_PRIV_SIZE_MASK	0x07FF	/* Max number of those args */

/*
 * Note : if the number of args is fixed and the size < 16 octets,
 * instead of passing a pointer we will put args in the iwreq struct...
 */

/* ----------------------- OTHER CONSTANTS ----------------------- */

/* Maximum frequencies in the range struct */
#define IW_MAX_FREQUENCIES	32
/* Note : if you have something like 80 frequencies,
 * don't increase this constant and don't fill the frequency list.
 * The user will be able to set by channel anyway... */

/* Maximum bit rates in the range struct */
#define IW_MAX_BITRATES		32

/* Maximum tx powers in the range struct */
#define IW_MAX_TXPOWER		8
/* Note : if you more than 8 TXPowers, just set the max and min or
 * a few of them in the struct iw_range. */

/* Maximum of address that you may set with SPY */
#define IW_MAX_SPY		8

/* Maximum of address that you may get in the
   list of access points in range */
#define IW_MAX_AP		64

/* Maximum size of the ESSID and NICKN strings */
#define IW_ESSID_MAX_SIZE	32

/* Modes of operation */
#define IW_MODE_AUTO	0	/* Let the driver decides */
#define IW_MODE_ADHOC	1	/* Single cell network */
#define IW_MODE_INFRA	2	/* Multi cell network, roaming, ... */
#define IW_MODE_MASTER	3	/* Synchronisation master or Access Point */
#define IW_MODE_REPEAT	4	/* Wireless Repeater (forwarder) */
#define IW_MODE_SECOND	5	/* Secondary master/repeater (backup) */
#define IW_MODE_MONITOR	6	/* Passive monitor (listen only) */

/* Statistics flags (bitmask in updated) */
#define IW_QUAL_QUAL_UPDATED	0x01	/* Value was updated since last read */
#define IW_QUAL_LEVEL_UPDATED	0x02
#define IW_QUAL_NOISE_UPDATED	0x04
#define IW_QUAL_ALL_UPDATED	0x07
#define IW_QUAL_DBM		0x08	/* Level + Noise are dBm */
#define IW_QUAL_QUAL_INVALID	0x10	/* Driver doesn't provide value */
#define IW_QUAL_LEVEL_INVALID	0x20
#define IW_QUAL_NOISE_INVALID	0x40
#define IW_QUAL_RCPI		0x80	/* Level + Noise are 802.11k RCPI */
#define IW_QUAL_ALL_INVALID	0x70

/* Frequency flags */
#define IW_FREQ_AUTO		0x00	/* Let the driver decides */
#define IW_FREQ_FIXED		0x01	/* Force a specific value */

/* Maximum number of size of encoding token available
 * they are listed in the range structure */
#define IW_MAX_ENCODING_SIZES	8

/* Maximum size of the encoding token in bytes */
#define IW_ENCODING_TOKEN_MAX	64	/* 512 bits (for now) */

/* Flags for encoding (along with the token) */
#define IW_ENCODE_INDEX		0x00FF	/* Token index (if needed) */
#define IW_ENCODE_FLAGS		0xFF00	/* Flags defined below */
#define IW_ENCODE_MODE		0xF000	/* Modes defined below */
#define IW_ENCODE_DISABLED	0x8000	/* Encoding disabled */
#define IW_ENCODE_ENABLED	0x0000	/* Encoding enabled */
#define IW_ENCODE_RESTRICTED	0x4000	/* Refuse non-encoded packets */
#define IW_ENCODE_OPEN		0x2000	/* Accept non-encoded packets */
#define IW_ENCODE_NOKEY		0x0800  /* Key is write only, so not present */
#define IW_ENCODE_TEMP		0x0400  /* Temporary key */

/* Power management flags available (along with the value, if any) */
#define IW_POWER_ON		0x0000	/* No details... */
#define IW_POWER_TYPE		0xF000	/* Type of parameter */
#define IW_POWER_PERIOD		0x1000	/* Value is a period/duration of  */
#define IW_POWER_TIMEOUT	0x2000	/* Value is a timeout (to go asleep) */
#define IW_POWER_MODE		0x0F00	/* Power Management mode */
#define IW_POWER_UNICAST_R	0x0100	/* Receive only unicast messages */
#define IW_POWER_MULTICAST_R	0x0200	/* Receive only multicast messages */
#define IW_POWER_ALL_R		0x0300	/* Receive all messages though PM */
#define IW_POWER_FORCE_S	0x0400	/* Force PM procedure for sending unicast */
#define IW_POWER_REPEATER	0x0800	/* Repeat broadcast messages in PM period */
#define IW_POWER_MODIFIER	0x000F	/* Modify a parameter */
#define IW_POWER_MIN		0x0001	/* Value is a minimum  */
#define IW_POWER_MAX		0x0002	/* Value is a maximum */
#define IW_POWER_RELATIVE	0x0004	/* Value is not in seconds/ms/us */

/* Transmit Power flags available */
#define IW_TXPOW_TYPE		0x00FF	/* Type of value */
#define IW_TXPOW_DBM		0x0000	/* Value is in dBm */
#define IW_TXPOW_MWATT		0x0001	/* Value is in mW */
#define IW_TXPOW_RELATIVE	0x0002	/* Value is in arbitrary units */
#define IW_TXPOW_RANGE		0x1000	/* Range of value between min/max */

/* Retry limits and lifetime flags available */
#define IW_RETRY_ON		0x0000	/* No details... */
#define IW_RETRY_TYPE		0xF000	/* Type of parameter */
#define IW_RETRY_LIMIT		0x1000	/* Maximum number of retries*/
#define IW_RETRY_LIFETIME	0x2000	/* Maximum duration of retries in us */
#define IW_RETRY_MODIFIER	0x00FF	/* Modify a parameter */
#define IW_RETRY_MIN		0x0001	/* Value is a minimum  */
#define IW_RETRY_MAX		0x0002	/* Value is a maximum */
#define IW_RETRY_RELATIVE	0x0004	/* Value is not in seconds/ms/us */
#define IW_RETRY_SHORT		0x0010	/* Value is for short packets  */
#define IW_RETRY_LONG		0x0020	/* Value is for long packets */

/* Scanning request flags */
#define IW_SCAN_DEFAULT		0x0000	/* Default scan of the driver */
#define IW_SCAN_ALL_ESSID	0x0001	/* Scan all ESSIDs */
#define IW_SCAN_THIS_ESSID	0x0002	/* Scan only this ESSID */
#define IW_SCAN_ALL_FREQ	0x0004	/* Scan all Frequencies */
#define IW_SCAN_THIS_FREQ	0x0008	/* Scan only this Frequency */
#define IW_SCAN_ALL_MODE	0x0010	/* Scan all Modes */
#define IW_SCAN_THIS_MODE	0x0020	/* Scan only this Mode */
#define IW_SCAN_ALL_RATE	0x0040	/* Scan all Bit-Rates */
#define IW_SCAN_THIS_RATE	0x0080	/* Scan only this Bit-Rate */
/* struct iw_scan_req scan_type */
#define IW_SCAN_TYPE_ACTIVE 0
#define IW_SCAN_TYPE_PASSIVE 1
/* Maximum size of returned data */
#define IW_SCAN_MAX_DATA	4096	/* In bytes */

/* Scan capability flags - in (struct iw_range *)->scan_capa */
#define IW_SCAN_CAPA_NONE		0x00
#define IW_SCAN_CAPA_ESSID		0x01
#define IW_SCAN_CAPA_BSSID		0x02
#define IW_SCAN_CAPA_CHANNEL	0x04
#define IW_SCAN_CAPA_MODE		0x08
#define IW_SCAN_CAPA_RATE		0x10
#define IW_SCAN_CAPA_TYPE		0x20
#define IW_SCAN_CAPA_TIME		0x40

/* Max number of char in custom event - use multiple of them if needed */
#define IW_CUSTOM_MAX		256	/* In bytes */

/* Generic information element */
#define IW_GENERIC_IE_MAX	1024

/* MLME requests (SIOCSIWMLME / struct iw_mlme) */
#define IW_MLME_DEAUTH		0
#define IW_MLME_DISASSOC	1
#define IW_MLME_AUTH		2
#define IW_MLME_ASSOC		3

/* SIOCSIWAUTH/SIOCGIWAUTH struct iw_param flags */
#define IW_AUTH_INDEX		0x0FFF
#define IW_AUTH_FLAGS		0xF000
/* SIOCSIWAUTH/SIOCGIWAUTH parameters (0 .. 4095)
 * (IW_AUTH_INDEX mask in struct iw_param flags; this is the index of the
 * parameter that is being set/get to; value will be read/written to
 * struct iw_param value field) */
#define IW_AUTH_WPA_VERSION		0
#define IW_AUTH_CIPHER_PAIRWISE		1
#define IW_AUTH_CIPHER_GROUP		2
#define IW_AUTH_KEY_MGMT		3
#define IW_AUTH_TKIP_COUNTERMEASURES	4
#define IW_AUTH_DROP_UNENCRYPTED	5
#define IW_AUTH_80211_AUTH_ALG		6
#define IW_AUTH_WPA_ENABLED		7
#define IW_AUTH_RX_UNENCRYPTED_EAPOL	8
#define IW_AUTH_ROAMING_CONTROL		9
#define IW_AUTH_PRIVACY_INVOKED		10

/* IW_AUTH_WPA_VERSION values (bit field) */
#define IW_AUTH_WPA_VERSION_DISABLED	0x00000001
#define IW_AUTH_WPA_VERSION_WPA		0x00000002
#define IW_AUTH_WPA_VERSION_WPA2	0x00000004

/* IW_AUTH_PAIRWISE_CIPHER and IW_AUTH_GROUP_CIPHER values (bit field) */
#define IW_AUTH_CIPHER_NONE	0x00000001
#define IW_AUTH_CIPHER_WEP40	0x00000002
#define IW_AUTH_CIPHER_TKIP	0x00000004
#define IW_AUTH_CIPHER_CCMP	0x00000008
#define IW_AUTH_CIPHER_WEP104	0x00000010

/* IW_AUTH_KEY_MGMT values (bit field) */
#define IW_AUTH_KEY_MGMT_802_1X	1
#define IW_AUTH_KEY_MGMT_PSK	2

/* IW_AUTH_80211_AUTH_ALG values (bit field) */
#define IW_AUTH_ALG_OPEN_SYSTEM	0x00000001
#define IW_AUTH_ALG_SHARED_KEY	0x00000002
#define IW_AUTH_ALG_LEAP	0x00000004

/* IW_AUTH_ROAMING_CONTROL values */
#define IW_AUTH_ROAMING_ENABLE	0	/* driver/firmware based roaming */
#define IW_AUTH_ROAMING_DISABLE	1	/* user space program used for roaming
					 * control */

/* SIOCSIWENCODEEXT definitions */
#define IW_ENCODE_SEQ_MAX_SIZE	8
/* struct iw_encode_ext ->alg */
#define IW_ENCODE_ALG_NONE	0
#define IW_ENCODE_ALG_WEP	1
#define IW_ENCODE_ALG_TKIP	2
#define IW_ENCODE_ALG_CCMP	3
/* struct iw_encode_ext ->ext_flags */
#define IW_ENCODE_EXT_TX_SEQ_VALID	0x00000001
#define IW_ENCODE_EXT_RX_SEQ_VALID	0x00000002
#define IW_ENCODE_EXT_GROUP_KEY		0x00000004
#define IW_ENCODE_EXT_SET_TX_KEY	0x00000008

/* IWEVMICHAELMICFAILURE : struct iw_michaelmicfailure ->flags */
#define IW_MICFAILURE_KEY_ID	0x00000003 /* Key ID 0..3 */
#define IW_MICFAILURE_GROUP	0x00000004
#define IW_MICFAILURE_PAIRWISE	0x00000008
#define IW_MICFAILURE_STAKEY	0x00000010
#define IW_MICFAILURE_COUNT	0x00000060 /* 1 or 2 (0 = count not supported)
					    */

/* Bit field values for enc_capa in struct iw_range */
#define IW_ENC_CAPA_WPA		0x00000001
#define IW_ENC_CAPA_WPA2	0x00000002
#define IW_ENC_CAPA_CIPHER_TKIP	0x00000004
#define IW_ENC_CAPA_CIPHER_CCMP	0x00000008

/* Event capability macros - in (struct iw_range *)->event_capa
 * Because we have more than 32 possible events, we use an array of
 * 32 bit bitmasks. Note : 32 bits = 0x20 = 2^5. */
#define IW_EVENT_CAPA_BASE(cmd)		((cmd >= SIOCIWFIRSTPRIV) ? \
					 (cmd - SIOCIWFIRSTPRIV + 0x60) : \
					 (cmd - SIOCSIWCOMMIT))
#define IW_EVENT_CAPA_INDEX(cmd)	(IW_EVENT_CAPA_BASE(cmd) >> 5)
#define IW_EVENT_CAPA_MASK(cmd)		(1 << (IW_EVENT_CAPA_BASE(cmd) & 0x1F))
/* Event capability constants - event autogenerated by the kernel
 * This list is valid for most 802.11 devices, customise as needed... */
#define IW_EVENT_CAPA_K_0	(IW_EVENT_CAPA_MASK(0x8B04) | \
				 IW_EVENT_CAPA_MASK(0x8B06) | \
				 IW_EVENT_CAPA_MASK(0x8B1A))
#define IW_EVENT_CAPA_K_1	(IW_EVENT_CAPA_MASK(0x8B2A))
/* "Easy" macro to set events in iw_range (less efficient) */
#define IW_EVENT_CAPA_SET(event_capa, cmd) (event_capa[IW_EVENT_CAPA_INDEX(cmd)] |= IW_EVENT_CAPA_MASK(cmd))
#define IW_EVENT_CAPA_SET_KERNEL(event_capa) {event_capa[0] |= IW_EVENT_CAPA_K_0; event_capa[1] |= IW_EVENT_CAPA_K_1; }


/****************************** TYPES ******************************/

/* --------------------------- SUBTYPES --------------------------- */
/*
 *	Generic format for most parameters that fit in an int
 */
struct	iw_param
{
  __s32		value;		/* The value of the parameter itself */
  __u8		fixed;		/* Hardware should not use auto select */
  __u8		disabled;	/* Disable the feature */
  __u16		flags;		/* Various specifc flags (if any) */
};

/*
 *	For all data larger than 16 octets, we need to use a
 *	pointer to memory allocated in user space.
 */
struct	iw_point
{
  void __user	*pointer;	/* Pointer to the data  (in user space) */
  __u16		length;		/* number of fields or size in bytes */
  __u16		flags;		/* Optional params */
};

/*
 *	A frequency
 *	For numbers lower than 10^9, we encode the number in 'm' and
 *	set 'e' to 0
 *	For number greater than 10^9, we divide it by the lowest power
 *	of 10 to get 'm' lower than 10^9, with 'm'= f / (10^'e')...
 *	The power of 10 is in 'e', the result of the division is in 'm'.
 */
struct	iw_freq
{
	__s32		m;		/* Mantissa */
	__s16		e;		/* Exponent */
	__u8		i;		/* List index (when in range struct) */
	__u8		flags;		/* Flags (fixed/auto) */
};

/*
 *	Quality of the link
 */
struct	iw_quality
{
	__u8		qual;		/* link quality (%retries, SNR,
					   %missed beacons or better...) */
	__u8		level;		/* signal level (dBm) */
	__u8		noise;		/* noise level (dBm) */
	__u8		updated;	/* Flags to know if updated */
};

/*
 *	Packet discarded in the wireless adapter due to
 *	"wireless" specific problems...
 *	Note : the list of counter and statistics in net_device_stats
 *	is already pretty exhaustive, and you should use that first.
 *	This is only additional stats...
 */
struct	iw_discarded
{
	__u32		nwid;		/* Rx : Wrong nwid/essid */
	__u32		code;		/* Rx : Unable to code/decode (WEP) */
	__u32		fragment;	/* Rx : Can't perform MAC reassembly */
	__u32		retries;	/* Tx : Max MAC retries num reached */
	__u32		misc;		/* Others cases */
};

/*
 *	Packet/Time period missed in the wireless adapter due to
 *	"wireless" specific problems...
 */
struct	iw_missed
{
	__u32		beacon;		/* Missed beacons/superframe */
};

/*
 *	Quality range (for spy threshold)
 */
struct	iw_thrspy
{
	struct sockaddr		addr;		/* Source address (hw/mac) */
	struct iw_quality	qual;		/* Quality of the link */
	struct iw_quality	low;		/* Low threshold */
	struct iw_quality	high;		/* High threshold */
};

/*
 *	Optional data for scan request
 *
 *	Note: these optional parameters are controlling parameters for the
 *	scanning behavior, these do not apply to getting scan results
 *	(SIOCGIWSCAN). Drivers are expected to keep a local BSS table and
 *	provide a merged results with all BSSes even if the previous scan
 *	request limited scanning to a subset, e.g., by specifying an SSID.
 *	Especially, scan results are required to include an entry for the
 *	current BSS if the driver is in Managed mode and associated with an AP.
 */
struct	iw_scan_req
{
	__u8		scan_type; /* IW_SCAN_TYPE_{ACTIVE,PASSIVE} */
	__u8		essid_len;
	__u8		num_channels; /* num entries in channel_list;
				       * 0 = scan all allowed channels */
	__u8		flags; /* reserved as padding; use zero, this may
				* be used in the future for adding flags
				* to request different scan behavior */
	struct sockaddr	bssid; /* ff:ff:ff:ff:ff:ff for broadcast BSSID or
				* individual address of a specific BSS */

	/*
	 * Use this ESSID if IW_SCAN_THIS_ESSID flag is used instead of using
	 * the current ESSID. This allows scan requests for specific ESSID
	 * without having to change the current ESSID and potentially breaking
	 * the current association.
	 */
	__u8		essid[IW_ESSID_MAX_SIZE];

	/*
	 * Optional parameters for changing the default scanning behavior.
	 * These are based on the MLME-SCAN.request from IEEE Std 802.11.
	 * TU is 1.024 ms. If these are set to 0, driver is expected to use
	 * reasonable default values. min_channel_time defines the time that
	 * will be used to wait for the first reply on each channel. If no
	 * replies are received, next channel will be scanned after this. If
	 * replies are received, total time waited on the channel is defined by
	 * max_channel_time.
	 */
	__u32		min_channel_time; /* in TU */
	__u32		max_channel_time; /* in TU */

	struct iw_freq	channel_list[IW_MAX_FREQUENCIES];
};

/* ------------------------- WPA SUPPORT ------------------------- */

/*
 *	Extended data structure for get/set encoding (this is used with
 *	SIOCSIWENCODEEXT/SIOCGIWENCODEEXT. struct iw_point and IW_ENCODE_*
 *	flags are used in the same way as with SIOCSIWENCODE/SIOCGIWENCODE and
 *	only the data contents changes (key data -> this structure, including
 *	key data).
 *
 *	If the new key is the first group key, it will be set as the default
 *	TX key. Otherwise, default TX key index is only changed if
 *	IW_ENCODE_EXT_SET_TX_KEY flag is set.
 *
 *	Key will be changed with SIOCSIWENCODEEXT in all cases except for
 *	special "change TX key index" operation which is indicated by setting
 *	key_len = 0 and ext_flags |= IW_ENCODE_EXT_SET_TX_KEY.
 *
 *	tx_seq/rx_seq are only used when respective
 *	IW_ENCODE_EXT_{TX,RX}_SEQ_VALID flag is set in ext_flags. Normal
 *	TKIP/CCMP operation is to set RX seq with SIOCSIWENCODEEXT and start
 *	TX seq from zero whenever key is changed. SIOCGIWENCODEEXT is normally
 *	used only by an Authenticator (AP or an IBSS station) to get the
 *	current TX sequence number. Using TX_SEQ_VALID for SIOCSIWENCODEEXT and
 *	RX_SEQ_VALID for SIOCGIWENCODEEXT are optional, but can be useful for
 *	debugging/testing.
 */
struct	iw_encode_ext
{
	__u32		ext_flags; /* IW_ENCODE_EXT_* */
	__u8		tx_seq[IW_ENCODE_SEQ_MAX_SIZE]; /* LSB first */
	__u8		rx_seq[IW_ENCODE_SEQ_MAX_SIZE]; /* LSB first */
	struct sockaddr	addr; /* ff:ff:ff:ff:ff:ff for broadcast/multicast
			       * (group) keys or unicast address for
			       * individual keys */
	__u16		alg; /* IW_ENCODE_ALG_* */
	__u16		key_len;
	__u8		key[0];
};

/* SIOCSIWMLME data */
struct	iw_mlme
{
	__u16		cmd; /* IW_MLME_* */
	__u16		reason_code;
	struct sockaddr	addr;
};

/* SIOCSIWPMKSA data */
#define IW_PMKSA_ADD		1
#define IW_PMKSA_REMOVE		2
#define IW_PMKSA_FLUSH		3

#define IW_PMKID_LEN	16

struct	iw_pmksa
{
	__u32		cmd; /* IW_PMKSA_* */
	struct sockaddr	bssid;
	__u8		pmkid[IW_PMKID_LEN];
};

/* IWEVMICHAELMICFAILURE data */
struct	iw_michaelmicfailure
{
	__u32		flags;
	struct sockaddr	src_addr;
	__u8		tsc[IW_ENCODE_SEQ_MAX_SIZE]; /* LSB first */
};

/* IWEVPMKIDCAND data */
#define IW_PMKID_CAND_PREAUTH	0x00000001 /* RNS pre-authentication enabled */
struct	iw_pmkid_cand
{
	__u32		flags; /* IW_PMKID_CAND_* */
	__u32		index; /* the smaller the index, the higher the
				* priority */
	struct sockaddr	bssid;
};

/* ------------------------ WIRELESS STATS ------------------------ */
/*
 * Wireless statistics (used for /proc/net/wireless)
 */
struct	iw_statistics
{
	__u16		status;		/* Status
					 * - device dependent for now */

	struct iw_quality	qual;		/* Quality of the link
						 * (instant/mean/max) */
	struct iw_discarded	discard;	/* Packet discarded counts */
	struct iw_missed	miss;		/* Packet missed counts */
};

/* ------------------------ IOCTL REQUEST ------------------------ */
/*
 * This structure defines the payload of an ioctl, and is used 
 * below.
 *
 * Note that this structure should fit on the memory footprint
 * of iwreq (which is the same as ifreq), which mean a max size of
 * 16 octets = 128 bits. Warning, pointers might be 64 bits wide...
 * You should check this when increasing the structures defined
 * above in this file...
 */
union	iwreq_data
{
	/* Config - generic */
	char		name[IFNAMSIZ];
	/* Name : used to verify the presence of  wireless extensions.
	 * Name of the protocol/provider... */

	struct iw_point	essid;		/* Extended network name */
	struct iw_param	nwid;		/* network id (or domain - the cell) */
	struct iw_freq	freq;		/* frequency or channel :
					 * 0-1000 = channel
					 * > 1000 = frequency in Hz */

	struct iw_param	sens;		/* signal level threshold */
	struct iw_param	bitrate;	/* default bit rate */
	struct iw_param	txpower;	/* default transmit power */
	struct iw_param	rts;		/* RTS threshold threshold */
	struct iw_param	frag;		/* Fragmentation threshold */
	__u32		mode;		/* Operation mode */
	struct iw_param	retry;		/* Retry limits & lifetime */

	struct iw_point	encoding;	/* Encoding stuff : tokens */
	struct iw_param	power;		/* PM duration/timeout */
	struct iw_quality qual;		/* Quality part of statistics */

	struct sockaddr	ap_addr;	/* Access point address */
	struct sockaddr	addr;		/* Destination address (hw/mac) */

	struct iw_param	param;		/* Other small parameters */
	struct iw_point	data;		/* Other large parameters */
};

/*
 * The structure to exchange data for ioctl.
 * This structure is the same as 'struct ifreq', but (re)defined for
 * convenience...
 * Do I need to remind you about structure size (32 octets) ?
 */
struct	iwreq 
{
	union
	{
		char	ifrn_name[IFNAMSIZ];	/* if name, e.g. "eth0" */
	} ifr_ifrn;

	/* Data part (defined just above) */
	union	iwreq_data	u;
};

/* -------------------------- IOCTL DATA -------------------------- */
/*
 *	For those ioctl which want to exchange mode data that what could
 *	fit in the above structure...
 */

/*
 *	Range of parameters
 */

struct	iw_range
{
	/* Informative stuff (to choose between different interface) */
	__u32		throughput;	/* To give an idea... */
	/* In theory this value should be the maximum benchmarked
	 * TCP/IP throughput, because with most of these devices the
	 * bit rate is meaningless (overhead an co) to estimate how
	 * fast the connection will go and pick the fastest one.
	 * I suggest people to play with Netperf or any benchmark...
	 */

	/* NWID (or domain id) */
	__u32		min_nwid;	/* Minimal NWID we are able to set */
	__u32		max_nwid;	/* Maximal NWID we are able to set */

	/* Old Frequency (backward compat - moved lower ) */
	__u16		old_num_channels;
	__u8		old_num_frequency;

	/* Scan capabilities */
	__u8		scan_capa; 	/* IW_SCAN_CAPA_* bit field */

	/* Wireless event capability bitmasks */
	__u32		event_capa[6];

	/* signal level threshold range */
	__s32		sensitivity;

	/* Quality of link & SNR stuff */
	/* Quality range (link, level, noise)
	 * If the quality is absolute, it will be in the range [0 ; max_qual],
	 * if the quality is dBm, it will be in the range [max_qual ; 0].
	 * Don't forget that we use 8 bit arithmetics... */
	struct iw_quality	max_qual;	/* Quality of the link */
	/* This should contain the average/typical values of the quality
	 * indicator. This should be the threshold between a "good" and
	 * a "bad" link (example : monitor going from green to orange).
	 * Currently, user space apps like quality monitors don't have any
	 * way to calibrate the measurement. With this, they can split
	 * the range between 0 and max_qual in different quality level
	 * (using a geometric subdivision centered on the average).
	 * I expect that people doing the user space apps will feedback
	 * us on which value we need to put in each driver... */
	struct iw_quality	avg_qual;	/* Quality of the link */

	/* Rates */
	__u8		num_bitrates;	/* Number of entries in the list */
	__s32		bitrate[IW_MAX_BITRATES];	/* list, in bps */

	/* RTS threshold */
	__s32		min_rts;	/* Minimal RTS threshold */
	__s32		max_rts;	/* Maximal RTS threshold */

	/* Frag threshold */
	__s32		min_frag;	/* Minimal frag threshold */
	__s32		max_frag;	/* Maximal frag threshold */

	/* Power Management duration & timeout */
	__s32		min_pmp;	/* Minimal PM period */
	__s32		max_pmp;	/* Maximal PM period */
	__s32		min_pmt;	/* Minimal PM timeout */
	__s32		max_pmt;	/* Maximal PM timeout */
	__u16		pmp_flags;	/* How to decode max/min PM period */
	__u16		pmt_flags;	/* How to decode max/min PM timeout */
	__u16		pm_capa;	/* What PM options are supported */

	/* Encoder stuff */
	__u16	encoding_size[IW_MAX_ENCODING_SIZES];	/* Different token sizes */
	__u8	num_encoding_sizes;	/* Number of entry in the list */
	__u8	max_encoding_tokens;	/* Max number of tokens */
	/* For drivers that need a "login/passwd" form */
	__u8	encoding_login_index;	/* token index for login token */

	/* Transmit power */
	__u16		txpower_capa;	/* What options are supported */
	__u8		num_txpower;	/* Number of entries in the list */
	__s32		txpower[IW_MAX_TXPOWER];	/* list, in bps */

	/* Wireless Extension version info */
	__u8		we_version_compiled;	/* Must be WIRELESS_EXT */
	__u8		we_version_source;	/* Last update of source */

	/* Retry limits and lifetime */
	__u16		retry_capa;	/* What retry options are supported */
	__u16		retry_flags;	/* How to decode max/min retry limit */
	__u16		r_time_flags;	/* How to decode max/min retry life */
	__s32		min_retry;	/* Minimal number of retries */
	__s32		max_retry;	/* Maximal number of retries */
	__s32		min_r_time;	/* Minimal retry lifetime */
	__s32		max_r_time;	/* Maximal retry lifetime */

	/* Frequency */
	__u16		num_channels;	/* Number of channels [0; num - 1] */
	__u8		num_frequency;	/* Number of entry in the list */
	struct iw_freq	freq[IW_MAX_FREQUENCIES];	/* list */
	/* Note : this frequency list doesn't need to fit channel numbers,
	 * because each entry contain its channel index */

	__u32		enc_capa;	/* IW_ENC_CAPA_* bit field */
};

/*
 * Private ioctl interface information
 */
 
struct	iw_priv_args
{
	__u32		cmd;		/* Number of the ioctl to issue */
	__u16		set_args;	/* Type and number of args */
	__u16		get_args;	/* Type and number of args */
	char		name[IFNAMSIZ];	/* Name of the extension */
};

/* ----------------------- WIRELESS EVENTS ----------------------- */
/*
 * Wireless events are carried through the rtnetlink socket to user
 * space. They are encapsulated in the IFLA_WIRELESS field of
 * a RTM_NEWLINK message.
 */

/*
 * A Wireless Event. Contains basically the same data as the ioctl...
 */
struct iw_event
{
	__u16		len;			/* Real length of this stuff */
	__u16		cmd;			/* Wireless IOCTL */
	union iwreq_data	u;		/* IOCTL fixed payload */
};

/* Size of the Event prefix (including padding and alignement junk) */
#define IW_EV_LCP_LEN	(sizeof(struct iw_event) - sizeof(union iwreq_data))
/* Size of the various events */
#define IW_EV_CHAR_LEN	(IW_EV_LCP_LEN + IFNAMSIZ)
#define IW_EV_UINT_LEN	(IW_EV_LCP_LEN + sizeof(__u32))
#define IW_EV_FREQ_LEN	(IW_EV_LCP_LEN + sizeof(struct iw_freq))
#define IW_EV_PARAM_LEN	(IW_EV_LCP_LEN + sizeof(struct iw_param))
#define IW_EV_ADDR_LEN	(IW_EV_LCP_LEN + sizeof(struct sockaddr))
#define IW_EV_QUAL_LEN	(IW_EV_LCP_LEN + sizeof(struct iw_quality))

/* iw_point events are special. First, the payload (extra data) come at
 * the end of the event, so they are bigger than IW_EV_POINT_LEN. Second,
 * we omit the pointer, so start at an offset. */
#define IW_EV_POINT_OFF (((char *) &(((struct iw_point *) NULL)->length)) - \
			  (char *) NULL)
#define IW_EV_POINT_LEN	(IW_EV_LCP_LEN + sizeof(struct iw_point) - \
			 IW_EV_POINT_OFF)

/* Size of the Event prefix when packed in stream */
#define IW_EV_LCP_PK_LEN	(4)
/* Size of the various events when packed in stream */
#define IW_EV_CHAR_PK_LEN	(IW_EV_LCP_PK_LEN + IFNAMSIZ)
#define IW_EV_UINT_PK_LEN	(IW_EV_LCP_PK_LEN + sizeof(__u32))
#define IW_EV_FREQ_PK_LEN	(IW_EV_LCP_PK_LEN + sizeof(struct iw_freq))
#define IW_EV_PARAM_PK_LEN	(IW_EV_LCP_PK_LEN + sizeof(struct iw_param))
#define IW_EV_ADDR_PK_LEN	(IW_EV_LCP_PK_LEN + sizeof(struct sockaddr))
#define IW_EV_QUAL_PK_LEN	(IW_EV_LCP_PK_LEN + sizeof(struct iw_quality))
#define IW_EV_POINT_PK_LEN	(IW_EV_LCP_LEN + 4)

#endif	/* _LINUX_WIRELESS_H */
