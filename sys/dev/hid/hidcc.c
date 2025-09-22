/*	$OpenBSD: hidcc.c,v 1.5 2022/11/14 00:16:44 deraadt Exp $	*/

/*
 * Copyright (c) 2022 Anton Lindqvist <anton@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/hid/hidccvar.h>
#include <dev/hid/hid.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#define DEVNAME(sc)	((sc)->sc_dev->dv_xname)

/* #define HIDCC_DEBUG */
#ifdef HIDCC_DEBUG
#define DPRINTF(x...)	do { if (hidcc_debug) printf(x); } while (0)
struct hidcc;
void	hidcc_dump(struct hidcc *, const char *, uint8_t *, u_int);
int	hidcc_debug = 1;
#else
#define DPRINTF(x...)
#define hidcc_dump(sc, prefix, data, len)
#endif

struct hidcc {
	struct device			 *sc_dev;
	struct device			 *sc_wskbddev;

	/* Key mappings used in translating mode. */
	keysym_t			 *sc_map;
	u_int				  sc_maplen;
	u_int				  sc_mapsiz;

	/* Key mappings used in raw mode. */
	const struct hidcc_keysym	**sc_raw;
	u_int				  sc_rawsiz;

	u_int				  sc_nusages;
	int				  sc_isarray;
	int				  sc_mode;

	/*
	 * Slice of the interrupt buffer which represents a pressed key.
	 * See section 8 (Report Protocol) of the HID specification v1.11.
	 */
	struct {
		uint8_t	*i_buf;
		uint32_t i_bufsiz;
		uint32_t i_off;		/* offset in bits */
		uint32_t i_len;		/* length in bits */
	} sc_input;

	struct {
		uint32_t	v_inc;	/* volume increment bit offset */
		uint32_t	v_dec;	/* volume decrement bit offset */
		uint32_t	v_off;	/* offset in bits */
		uint32_t	v_len;	/* length in bits */
	} sc_volume;

	/* Last pressed key. */
	union {
		int	sc_last_translate;
		u_char	sc_last_raw;
	};

	/*
	 * Only the first element is populated whereas the second remains zeroed
	 * out since such trailing sentinel is required by wskbd_load_keymap().
	 */
	struct wscons_keydesc		  sc_keydesc[2];
	struct wskbd_mapdata		  sc_keymap;

	int				  (*sc_enable)(void *, int);
	void				 *sc_arg;
};

struct hidcc_keysym {
#ifdef HIDCC_DEBUG
	const char	*ks_name;
#endif
	int32_t		 ks_usage;
	keysym_t	 ks_key;
	u_char		 ks_raw;
};

/*
 * Mapping of HID Consumer Control usages to key symbols based on the HID Usage
 * Tables 1.21 specification. The same usages can also be found at
 * /usr/share/misc/usb_hid_usages.
 * The raw scan codes are taken from X11, see the media_nav_acpi_common symbols
 * in dist/xkeyboard-config/symbols/inet.
 * Then use dist/xkeyboard-config/keycodes/xfree86 to resolve keys to the
 * corresponding raw scan code.
 */
static const struct hidcc_keysym hidcc_keysyms[] = {
#ifdef HIDCC_DEBUG
#define Y(usage, name, key, raw)	{ name, usage, key, raw },
#else
#define Y(usage, name, key, raw)	{ usage, key, raw },
#endif
#define N(usage, name, key, raw)
	/* 0x0000 Unassigned */
	N(0x0001,	"Consumer Control",				0,		0)
	N(0x0002,	"Numeric Key Pad",				0,		0)
	N(0x0003,	"Programmable Buttons",				0,		0)
	N(0x0004,	"Microphone",					0,		0)
	N(0x0005,	"Headphone",					0,		0)
	N(0x0006,	"Graphic Equalizer",				0,		0)
	/* 0x0007-0x001F Reserved */
	N(0x0020,	"+10",						0,		0)
	N(0x0021,	"+100",						0,		0)
	N(0x0022,	"AM/PM",					0,		0)
	/* 0x0023-0x002F Reserved */
	Y(0x0030,	"Power",					0,		222 /* I5E = XF86PowerOff */)
	N(0x0031,	"Reset",					0,		0)
	Y(0x0032,	"Sleep",					0,		150 /* I16 = XF86Sleep */)
	N(0x0033,	"Sleep After",					0,		0)
	N(0x0034,	"Sleep Mode",					0,		0)
	N(0x0035,	"Illumination",					0,		0)
	N(0x0036,	"Function Buttons",				0,		0)
	/* 0x0037-0x003F Reserved */
	N(0x0040,	"Menu",						0,		0)
	N(0x0041,	"Menu Pick",					0,		0)
	N(0x0042,	"Menu Up",					0,		0)
	N(0x0043,	"Menu Down",					0,		0)
	N(0x0044,	"Menu Left",					0,		0)
	N(0x0045,	"Menu Right",					0,		0)
	N(0x0046,	"Menu Escape",					0,		0)
	N(0x0047,	"Menu Value Increase",				0,		0)
	N(0x0048,	"Menu Value Decrease",				0,		0)
	/* 0x0049-0x005F Reserved */
	N(0x0060,	"Data On Screen",				0,		0)
	N(0x0061,	"Closed Caption",				0,		0)
	N(0x0062,	"Closed Caption Select",			0,		0)
	N(0x0063,	"VCR/TV",					0,		0)
	N(0x0064,	"Broadcast Mode",				0,		0)
	N(0x0065,	"Snapshot",					0,		0)
	N(0x0066,	"Still",					0,		0)
	N(0x0067,	"Picture-in-Picture Toggle",			0,		0)
	N(0x0068,	"Picture-in-Picture Swap",			0,		0)
	N(0x0069,	"Red Menu Button",				0,		0)
	N(0x006A,	"Green Menu Button",				0,		0)
	N(0x006B,	"Blue Menu Button",				0,		0)
	N(0x006C,	"Yellow Menu Button",				0,		0)
	N(0x006D,	"Aspect",					0,		0)
	N(0x006E,	"3D Mode Select",				0,		0)
	Y(0x006F,	"Display Brightness Increment",			KS_Cmd_BrightnessUp,		0)
	Y(0x0070,	"Display Brightness Decrement",			KS_Cmd_BrightnessDown,		0)
	N(0x0071,	"Display Brightness",				0,		0)
	N(0x0072,	"Display Backlight Toggle",			0,		0)
	N(0x0073,	"Display Set Brightness to Minimum",		0,		0)
	N(0x0074,	"Display Set Brightness to Maximum",		0,		0)
	N(0x0075,	"Display Set Auto Brightness",			0,		0)
	N(0x0076,	"Camera Access Enabled",			0,		0)
	N(0x0077,	"Camera Access Disabled",			0,		0)
	N(0x0078,	"Camera Access Toggle",				0,		0)
	N(0x0079,	"Keyboard Brightness Increment",		0,		0)
	N(0x007A,	"Keyboard Brightness Decrement",		0,		0)
	N(0x007B,	"Keyboard Backlight Set Level",			0,		0)
	N(0x007C,	"Keyboard Backlight OOC",			0,		0)
	N(0x007D,	"Keyboard Backlight Set Minimum",		0,		0)
	N(0x007E,	"Keyboard Backlight Set Maximum",		0,		0)
	N(0x007F,	"Keyboard Backlight Auto",			0,		0)
	N(0x0080,	"Selection",					0,		0)
	N(0x0081,	"Assign Selection",				0,		0)
	N(0x0082,	"Mode Step",					0,		0)
	N(0x0083,	"Recall Last",					0,		0)
	N(0x0084,	"Enter Channel",				0,		0)
	N(0x0085,	"Order Movie",					0,		0)
	N(0x0086,	"Channel",					0,		0)
	N(0x0087,	"Media Selection",				0,		0)
	N(0x0088,	"Media Select Computer",			0,		0)
	N(0x0089,	"Media Select TV",				0,		0)
	N(0x008A,	"Media Select WWW",				0,		0)
	N(0x008B,	"Media Select DVD",				0,		0)
	N(0x008C,	"Media Select Telephone",			0,		0)
	N(0x008D,	"Media Select Program Guide",			0,		0)
	N(0x008E,	"Media Select Video Phone",			0,		0)
	N(0x008F,	"Media Select Games",				0,		0)
	N(0x0090,	"Media Select Messages",			0,		0)
	N(0x0091,	"Media Select CD",				0,		0)
	N(0x0092,	"Media Select VCR",				0,		0)
	N(0x0093,	"Media Select Tuner",				0,		0)
	N(0x0094,	"Quit",						0,		0)
	N(0x0095,	"Help",						0,		0)
	N(0x0096,	"Media Select Tape",				0,		0)
	N(0x0097,	"Media Select Cable",				0,		0)
	N(0x0098,	"Media Select Satellite",			0,		0)
	N(0x0099,	"Media Select Security",			0,		0)
	N(0x009A,	"Media Select Home",				0,		0)
	N(0x009B,	"Media Select Call",				0,		0)
	N(0x009C,	"Channel Increment",				0,		0)
	N(0x009D,	"Channel Decrement",				0,		0)
	N(0x009E,	"Media Select SAP",				0,		0)
	/* 0x009F-0x009F Reserved */
	N(0x00A0,	"VCR Plus",					0,		0)
	N(0x00A1,	"Once",						0,		0)
	N(0x00A2,	"Daily",					0,		0)
	N(0x00A3,	"Weekly",					0,		0)
	N(0x00A4,	"Monthly",					0,		0)
	/* 0x00A5-0x00AF Reserved */
	N(0x00B0,	"Play",						0,		0)
	N(0x00B1,	"Pause",					0,		0)
	N(0x00B2,	"Record",					0,		0)
	N(0x00B3,	"Fast Forward",					0,		0)
	N(0x00B4,	"Rewind",					0,		0)
	Y(0x00B5,	"Scan Next Track",				0,		153 /* I19 = XF86AudioNext */)
	Y(0x00B6,	"Scan Previous Track",				0,		144 /* I10 = XF86AudioPrev */)
	Y(0x00B7,	"Stop",						0,		164 /* I24 = XF86AudioStop */)
	Y(0x00B8,	"Eject",					0,		170 /* K5A = XF86Eject */)
	N(0x00B9,	"Random Play",					0,		0)
	N(0x00BA,	"Select Disc",					0,		0)
	N(0x00BB,	"Enter Disc",					0,		0)
	N(0x00BC,	"Repeat",					0,		0)
	N(0x00BD,	"Tracking",					0,		0)
	N(0x00BE,	"Track Normal",					0,		0)
	N(0x00BF,	"Slow Tracking",				0,		0)
	N(0x00C0,	"Frame Forward",				0,		0)
	N(0x00C1,	"Frame Back",					0,		0)
	N(0x00C2,	"Mark",						0,		0)
	N(0x00C3,	"Clear Mark",					0,		0)
	N(0x00C4,	"Repeat From Mark",				0,		0)
	N(0x00C5,	"Return To Mark",				0,		0)
	N(0x00C6,	"Search Mark Forward",				0,		0)
	N(0x00C7,	"Search Mark Backwards",			0,		0)
	N(0x00C8,	"Counter Reset",				0,		0)
	N(0x00C9,	"Show Counter",					0,		0)
	N(0x00CA,	"Tracking Increment",				0,		0)
	N(0x00CB,	"Tracking Decrement",				0,		0)
	N(0x00CC,	"Stop/Eject",					0,		0)
	Y(0x00CD,	"Play/Pause",					0,		162 /* I22 = XF86AudioPlay */)
	N(0x00CE,	"Play/Skip",					0,		0)
	N(0x00CF,	"Voice Command",				0,		0)
	N(0x00D0,	"Invoke Capture Interface",			0,		0)
	N(0x00D1,	"Start or Stop Game Recording",			0,		0)
	N(0x00D2,	"Historical Game Capture",			0,		0)
	N(0x00D3,	"Capture Game Screenshot",			0,		0)
	N(0x00D4,	"Show or Hide Recording Indicator",		0,		0)
	N(0x00D5,	"Start or Stop Microphone Capture",		0,		0)
	N(0x00D6,	"Start or Stop Camera Capture",			0,		0)
	N(0x00D7,	"Start or Stop Game Broadcast",			0,		0)
	/* 0x00D8-0x00DF Reserved */
	N(0x00E0,	"Volume",					0,		0)
	N(0x00E1,	"Balance",					0,		0)
	Y(0x00E2,	"Mute",						KS_AudioMute,	160 /* I20 = XF86AudioMute */)
	N(0x00E3,	"Bass",						0,		0)
	N(0x00E4,	"Treble",					0,		0)
	N(0x00E5,	"Bass Boost",					0,		0)
	N(0x00E6,	"Surround Mode",				0,		0)
	N(0x00E7,	"Loudness",					0,		0)
	N(0x00E8,	"MPX",						0,		0)
	Y(0x00E9,	"Volume Increment",				KS_AudioRaise,	176 /* I30 = XF86AudioRaiseVolume */)
	Y(0x00EA,	"Volume Decrement",				KS_AudioLower,	174 /* I2E = XF86AudioLowerVolume */)
	/* 0x00EB-0x00EF Reserved */
	N(0x00F0,	"Speed Select",					0,		0)
	N(0x00F1,	"Playback Speed",				0,		0)
	N(0x00F2,	"Standard Play",				0,		0)
	N(0x00F3,	"Long Play",					0,		0)
	N(0x00F4,	"Extended Play",				0,		0)
	N(0x00F5,	"Slow",						0,		0)
	/* 0x00F6-0x00FF Reserved */
	N(0x0100,	"Fan Enable",					0,		0)
	N(0x0101,	"Fan Speed",					0,		0)
	N(0x0102,	"Light Enable",					0,		0)
	N(0x0103,	"Light Illumination Level",			0,		0)
	N(0x0104,	"Climate Control Enable",			0,		0)
	N(0x0105,	"Room Temperature",				0,		0)
	N(0x0106,	"Security Enable",				0,		0)
	N(0x0107,	"Fire Alarm",					0,		0)
	N(0x0108,	"Police Alarm",					0,		0)
	N(0x0109,	"Proximity",					0,		0)
	N(0x010A,	"Motion",					0,		0)
	N(0x010B,	"Duress Alarm",					0,		0)
	N(0x010C,	"Holdup Alarm",					0,		0)
	N(0x010D,	"Medical Alarm",				0,		0)
	/* 0x010E-0x014F Reserved */
	N(0x0150,	"Balance Right",				0,		0)
	N(0x0151,	"Balance Left",					0,		0)
	N(0x0152,	"Bass Increment",				0,		0)
	N(0x0153,	"Bass Decrement",				0,		0)
	N(0x0154,	"Treble Increment",				0,		0)
	N(0x0155,	"Treble Decrement",				0,		0)
	/* 0x0156-0x015F Reserved */
	N(0x0160,	"Speaker System",				0,		0)
	N(0x0161,	"Channel Left",					0,		0)
	N(0x0162,	"Channel Right",				0,		0)
	N(0x0163,	"Channel Center",				0,		0)
	N(0x0164,	"Channel Front",				0,		0)
	N(0x0165,	"Channel Center Front",				0,		0)
	N(0x0166,	"Channel Side",					0,		0)
	N(0x0167,	"Channel Surround",				0,		0)
	N(0x0168,	"Channel Low Frequency Enhancement",		0,		0)
	N(0x0169,	"Channel Top",					0,		0)
	N(0x016A,	"Channel Unknown",				0,		0)
	/* 0x016B-0x016F Reserved */
	N(0x0170,	"Sub-channel",					0,		0)
	N(0x0171,	"Sub-channel Increment",			0,		0)
	N(0x0172,	"Sub-channel Decrement",			0,		0)
	N(0x0173,	"Alternate Audio Increment",			0,		0)
	N(0x0174,	"Alternate Audio Decrement",			0,		0)
	/* 0x0175-0x017F Reserved */
	N(0x0180,	"Application Launch Buttons",			0,		0)
	N(0x0181,	"AL Launch Button Configuration Tool",		0,		0)
	N(0x0182,	"AL Programmable Button Configuration",		0,		0)
	N(0x0183,	"AL Consumer Control Configuration",		0,		0)
	N(0x0184,	"AL Word Processor",				0,		0)
	N(0x0185,	"AL Text Editor",				0,		0)
	N(0x0186,	"AL Spreadsheet",				0,		0)
	N(0x0187,	"AL Graphics Editor",				0,		0)
	N(0x0188,	"AL Presentation App",				0,		0)
	N(0x0189,	"AL Database App",				0,		0)
	Y(0x018A,	"AL Email Reader",				0,		235 /* I6C = XF86Mail */)
	N(0x018B,	"AL Newsreader",				0,		0)
	N(0x018C,	"AL Voicemail",					0,		0)
	N(0x018D,	"AL Contacts/Address Book",			0,		0)
	N(0x018E,	"AL Calendar/Schedule",				0,		0)
	N(0x018F,	"AL Task/Project Manager",			0,		0)
	N(0x0190,	"AL Log/Journal/Timecard",			0,		0)
	N(0x0191,	"AL Checkbook/Finance",				0,		0)
	Y(0x0192,	"AL Calculator",				0,		161 /* I21 = XF86Calculator */)
	N(0x0193,	"AL A/V Capture/Playback",			0,		0)
	N(0x0194,	"AL Local Machine Browser",			0,		0)
	N(0x0195,	"AL LAN/WAN Browser",				0,		0)
	Y(0x0196,	"AL Internet Browser",				0,		178 /* I32 = XF86WWW */)
	N(0x0197,	"AL Remote Networking/ISP Connect",		0,		0)
	N(0x0198,	"AL Network Conference",			0,		0)
	N(0x0199,	"AL Network Chat",				0,		0)
	N(0x019A,	"AL Telephony/Dialer",				0,		0)
	N(0x019B,	"AL Logon",					0,		0)
	N(0x019C,	"AL Logoff",					0,		0)
	N(0x019D,	"AL Logon/Logoff",				0,		0)
	N(0x019E,	"AL Terminal Lock/Screensaver",			0,		0)
	N(0x019F,	"AL Control Panel",				0,		0)
	N(0x01A0,	"AL Command Line Processor/Run",		0,		0)
	N(0x01A1,	"AL Process/Task Manager",			0,		0)
	N(0x01A2,	"AL Select Task/Application",			0,		0)
	N(0x01A3,	"AL Next Task/Application",			0,		0)
	N(0x01A4,	"AL Previous Task/Application",			0,		0)
	N(0x01A5,	"AL Preemptive Halt Task/Application",		0,		0)
	N(0x01A6,	"AL Integrated Help Center",			0,		0)
	N(0x01A7,	"AL My Documents",				0,		0)
	N(0x01A8,	"AL Thesaurus",					0,		0)
	N(0x01A9,	"AL Dictionary",				0,		0)
	N(0x01AA,	"AL Desktop",					0,		0)
	N(0x01AB,	"AC Spell",					0,		0)
	N(0x01AC,	"AL Grammar Check",				0,		0)
	N(0x01AD,	"AL Wireless Status",				0,		0)
	N(0x01AE,	"AL Keyboard Layout",				0,		0)
	N(0x01AF,	"AL Virus Protection",				0,		0)
	N(0x01B0,	"AL Encryption",				0,		0)
	N(0x01B1,	"AL Screen Saver",				0,		0)
	N(0x01B2,	"AL Alarms",					0,		0)
	N(0x01B3,	"AL Clock",					0,		0)
	N(0x01B4,	"AL File Browser",				0,		0)
	N(0x01B5,	"AL Power Status",				0,		0)
	N(0x01B6,	"AL My Pictures",				0,		0)
	N(0x01B7,	"AL My Music",					0,		0)
	N(0x01B8,	"AL Movie Browser",				0,		0)
	N(0x01B9,	"AL Digital Rights Manager",			0,		0)
	N(0x01BA,	"AL Digital Wallet",				0,		0)
	/* 0x01BB-0x01BB Reserved */
	N(0x01BC,	"AL Instant Messaging",				0,		0)
	N(0x01BD,	"AL OEM Feature/Tips/Tutorial Browser",		0,		0)
	N(0x01BE,	"AL OEM Help",					0,		0)
	N(0x01BF,	"AL Online Community",				0,		0)
	N(0x01C0,	"AL Entertainment Content Browser",		0,		0)
	N(0x01C1,	"AL Online Shopping Browser",			0,		0)
	N(0x01C2,	"AL SmartCard Information/Help",		0,		0)
	N(0x01C3,	"AL Market Monitor/Finance Browser",		0,		0)
	N(0x01C4,	"AL Customized Corporate News Browser",		0,		0)
	N(0x01C5,	"AL Online Activity Browser",			0,		0)
	Y(0x01C6,	"AL Research/Search Browser",			0,		229 /* I65 = XF86Search */)
	N(0x01C7,	"AL Audio Player",				0,		0)
	N(0x01C8,	"AL Message Status",				0,		0)
	N(0x01C9,	"AL Contact Sync",				0,		0)
	N(0x01CA,	"AL Navigation",				0,		0)
	N(0x01CB,	"AL Context-aware Desktop Assistant",		0,		0)
	/* 0x01CC-0x01FF Reserved */
	N(0x0200,	"Generic GUI Application Controls",		0,		0)
	N(0x0201,	"AC New",					0,		0)
	N(0x0202,	"AC Open",					0,		0)
	N(0x0203,	"AC Close",					0,		0)
	N(0x0204,	"AC Exit",					0,		0)
	N(0x0205,	"AC Maximize",					0,		0)
	N(0x0206,	"AC Minimize",					0,		0)
	N(0x0207,	"AC Save",					0,		0)
	N(0x0208,	"AC Print",					0,		0)
	N(0x0209,	"AC Properties",				0,		0)
	/* 0x020A-0x0219 Reserved */
	N(0x021A,	"AC Undo",					0,		0)
	N(0x021B,	"AC Copy",					0,		0)
	N(0x021C,	"AC Cut",					0,		0)
	N(0x021D,	"AC Paste",					0,		0)
	N(0x021E,	"AC Select All",				0,		0)
	N(0x021F,	"AC Find",					0,		0)
	N(0x0220,	"AC Find and Replace",				0,		0)
	N(0x0221,	"AC Search",					0,		0)
	N(0x0222,	"AC Go To",					0,		0)
	N(0x0223,	"AC Home",					0,		0)
	Y(0x0224,	"AC Back",					0,		234 /* I6A = XF86Back */)
	Y(0x0225,	"AC Forward",					0,		233 /* I69 = XF86Forward */)
	Y(0x0226,	"AC Stop",					0,		232 /* I68 = XF86Stop */)
	Y(0x0227,	"AC Refresh",					0,		231 /* I67 = XF86Reload */)
	N(0x0228,	"AC Previous Link",				0,		0)
	N(0x0229,	"AC Next Link",					0,		0)
	N(0x022A,	"AC Bookmarks",					0,		0)
	N(0x022B,	"AC History",					0,		0)
	N(0x022C,	"AC Subscriptions",				0,		0)
	N(0x022D,	"AC Zoom In",					0,		0)
	N(0x022E,	"AC Zoom Out",					0,		0)
	N(0x022F,	"AC Zoom",					0,		0)
	N(0x0230,	"AC Full Screen View",				0,		0)
	N(0x0231,	"AC Normal View",				0,		0)
	N(0x0232,	"AC View Toggle",				0,		0)
	N(0x0233,	"AC Scroll Up",					0,		0)
	N(0x0234,	"AC Scroll Down",				0,		0)
	N(0x0235,	"AC Scroll",					0,		0)
	N(0x0236,	"AC Pan Left",					0,		0)
	N(0x0237,	"AC Pan Right",					0,		0)
	N(0x0238,	"AC Pan",					0,		0)
	N(0x0239,	"AC New Window",				0,		0)
	N(0x023A,	"AC Tile Horizontally",				0,		0)
	N(0x023B,	"AC Tile Vertically",				0,		0)
	N(0x023C,	"AC Format",					0,		0)
	N(0x023D,	"AC Edit",					0,		0)
	N(0x023E,	"AC Bold",					0,		0)
	N(0x023F,	"AC Italics",					0,		0)
	N(0x0240,	"AC Underline",					0,		0)
	N(0x0241,	"AC Strikethrough",				0,		0)
	N(0x0242,	"AC Subscript",					0,		0)
	N(0x0243,	"AC Superscript",				0,		0)
	N(0x0244,	"AC All Caps",					0,		0)
	N(0x0245,	"AC Rotate",					0,		0)
	N(0x0246,	"AC Resize",					0,		0)
	N(0x0247,	"AC Flip Horizontal",				0,		0)
	N(0x0248,	"AC Flip Vertical",				0,		0)
	N(0x0249,	"AC Mirror Horizontal",				0,		0)
	N(0x024A,	"AC Mirror Vertical",				0,		0)
	N(0x024B,	"AC Font Select",				0,		0)
	N(0x024C,	"AC Font Color",				0,		0)
	N(0x024D,	"AC Font Size",					0,		0)
	N(0x024E,	"AC Justify Left",				0,		0)
	N(0x024F,	"AC Justify Center H",				0,		0)
	N(0x0250,	"AC Justify Right",				0,		0)
	N(0x0251,	"AC Justify Block H",				0,		0)
	N(0x0252,	"AC Justify Top",				0,		0)
	N(0x0253,	"AC Justify Center V",				0,		0)
	N(0x0254,	"AC Justify Bottom",				0,		0)
	N(0x0255,	"AC Justify Block V",				0,		0)
	N(0x0256,	"AC Justify Decrease",				0,		0)
	N(0x0257,	"AC Justify Increase",				0,		0)
	N(0x0258,	"AC Numbered List",				0,		0)
	N(0x0259,	"AC Restart Numbering",				0,		0)
	N(0x025A,	"AC Bulleted List",				0,		0)
	N(0x025B,	"AC Promote",					0,		0)
	N(0x025C,	"AC Demote",					0,		0)
	N(0x025D,	"AC Yes",					0,		0)
	N(0x025E,	"AC No",					0,		0)
	N(0x025F,	"AC Cancel",					0,		0)
	N(0x0260,	"AC Catalog",					0,		0)
	N(0x0261,	"AC Buy/Checkout",				0,		0)
	N(0x0262,	"AC Add to Cart",				0,		0)
	N(0x0263,	"AC Expand",					0,		0)
	N(0x0264,	"AC Expand All",				0,		0)
	N(0x0265,	"AC Collapse",					0,		0)
	N(0x0266,	"AC Collapse All",				0,		0)
	N(0x0267,	"AC Print Preview",				0,		0)
	N(0x0268,	"AC Paste Special",				0,		0)
	N(0x0269,	"AC Insert Mode",				0,		0)
	N(0x026A,	"AC Delete",					0,		0)
	N(0x026B,	"AC Lock",					0,		0)
	N(0x026C,	"AC Unlock",					0,		0)
	N(0x026D,	"AC Protect",					0,		0)
	N(0x026E,	"AC Unprotect",					0,		0)
	N(0x026F,	"AC Attach Comment",				0,		0)
	N(0x0270,	"AC Delete Comment",				0,		0)
	N(0x0271,	"AC View Comment",				0,		0)
	N(0x0272,	"AC Select Word",				0,		0)
	N(0x0273,	"AC Select Sentence",				0,		0)
	N(0x0274,	"AC Select Paragraph",				0,		0)
	N(0x0275,	"AC Select Column",				0,		0)
	N(0x0276,	"AC Select Row",				0,		0)
	N(0x0277,	"AC Select Table",				0,		0)
	N(0x0278,	"AC Select Object",				0,		0)
	N(0x0279,	"AC Redo/Repeat",				0,		0)
	N(0x027A,	"AC Sort",					0,		0)
	N(0x027B,	"AC Sort Ascending",				0,		0)
	N(0x027C,	"AC Sort Descending",				0,		0)
	N(0x027D,	"AC Filter",					0,		0)
	N(0x027E,	"AC Set Clock",					0,		0)
	N(0x027F,	"AC View Clock",				0,		0)
	N(0x0280,	"AC Select Time Zone",				0,		0)
	N(0x0281,	"AC Edit Time Zones",				0,		0)
	N(0x0282,	"AC Set Alarm",					0,		0)
	N(0x0283,	"AC Clear Alarm",				0,		0)
	N(0x0284,	"AC Snooze Alarm",				0,		0)
	N(0x0285,	"AC Reset Alarm",				0,		0)
	N(0x0286,	"AC Synchronize",				0,		0)
	N(0x0287,	"AC Send/Receive",				0,		0)
	N(0x0288,	"AC Send To",					0,		0)
	N(0x0289,	"AC Reply",					0,		0)
	N(0x028A,	"AC Reply All",					0,		0)
	N(0x028B,	"AC Forward Message",				0,		0)
	N(0x028C,	"AC Send",					0,		0)
	N(0x028D,	"AC Attach File",				0,		0)
	N(0x028E,	"AC Upload",					0,		0)
	N(0x028F,	"AC Download (Save Target As)",			0,		0)
	N(0x0290,	"AC Set Borders",				0,		0)
	N(0x0291,	"AC Insert Row",				0,		0)
	N(0x0292,	"AC Insert Column",				0,		0)
	N(0x0293,	"AC Insert File",				0,		0)
	N(0x0294,	"AC Insert Picture",				0,		0)
	N(0x0295,	"AC Insert Object",				0,		0)
	N(0x0296,	"AC Insert Symbol",				0,		0)
	N(0x0297,	"AC Save and Close",				0,		0)
	N(0x0298,	"AC Rename",					0,		0)
	N(0x0299,	"AC Merge",					0,		0)
	N(0x029A,	"AC Split",					0,		0)
	N(0x029B,	"AC Distribute Horizontally",			0,		0)
	N(0x029C,	"AC Distribute Vertically",			0,		0)
	N(0x029D,	"AC Next Keyboard Layout Select",		0,		0)
	N(0x029E,	"AC Navigation Guidance",			0,		0)
	N(0x029F,	"AC Desktop Show All Windows",			0,		0)
	N(0x02A0,	"AC Soft Key Left",				0,		0)
	N(0x02A1,	"AC Soft Key Right",				0,		0)
	N(0x02A2,	"AC Desktop Show All Applications",		0,		0)
	/* 0x02A3-0x02AF Reserved */
	N(0x02B0,	"AC Idle Keep Alive",				0,		0)
	/* 0x02B1-0x02BF Reserved */
	N(0x02C0,	"Extended Keyboard Attributes Collection",	0,		0)
	N(0x02C1,	"Keyboard Form Factor",				0,		0)
	N(0x02C2,	"Keyboard Key Type",				0,		0)
	N(0x02C3,	"Keyboard Physical Layout",			0,		0)
	N(0x02C4,	"Vendor-Specific Keyboard Physical Layout",	0,		0)
	N(0x02C5,	"Keyboard IETF Language Tag Index",		0,		0)
	N(0x02C6,	"Implemented Keyboard Input Assist Controls",	0,		0)
	N(0x02C7,	"Keyboard Input Assist Previous",		0,		0)
	N(0x02C8,	"Keyboard Input Assist Next",			0,		0)
	N(0x02C9,	"Keyboard Input Assist Previous Group",		0,		0)
	N(0x02CA,	"Keyboard Input Assist Next Group",		0,		0)
	N(0x02CB,	"Keyboard Input Assist Accept",			0,		0)
	N(0x02CC,	"Keyboard Input Assist Cancel",			0,		0)
	/* 0x02CD-0x02CF Reserved */
	N(0x02D0,	"Privacy Screen Toggle",			0,		0)
	N(0x02D1,	"Privacy Screen Level Decrement",		0,		0)
	N(0x02D2,	"Privacy Screen Level Increment",		0,		0)
	N(0x02D3,	"Privacy Screen Level Minimum",			0,		0)
	N(0x02D4,	"Privacy Screen Level Maximum",			0,		0)
	/* 0x02D5-0x04FF Reserved */
	N(0x0500,	"Contact Edited",				0,		0)
	N(0x0501,	"Contact Added",				0,		0)
	N(0x0502,	"Contact Record Active",			0,		0)
	N(0x0503,	"Contact Index",				0,		0)
	N(0x0504,	"Contact Nickname",				0,		0)
	N(0x0505,	"Contact First Name",				0,		0)
	N(0x0506,	"Contact Last Name",				0,		0)
	N(0x0507,	"Contact Full Name",				0,		0)
	N(0x0508,	"Contact Phone Number Personal",		0,		0)
	N(0x0509,	"Contact Phone Number Business",		0,		0)
	N(0x050A,	"Contact Phone Number Mobile",			0,		0)
	N(0x050B,	"Contact Phone Number Pager",			0,		0)
	N(0x050C,	"Contact Phone Number Fax",			0,		0)
	N(0x050D,	"Contact Phone Number Other",			0,		0)
	N(0x050E,	"Contact Email Personal",			0,		0)
	N(0x050F,	"Contact Email Business",			0,		0)
	N(0x0510,	"Contact Email Other",				0,		0)
	N(0x0511,	"Contact Email Main",				0,		0)
	N(0x0512,	"Contact Speed Dial Number",			0,		0)
	N(0x0513,	"Contact Status Flag",				0,		0)
	N(0x0514,	"Contact Misc.",				0,		0)
	/* 0x0515-0xFFFF Reserved */
#undef Y
#undef N
};

void	hidcc_attach_wskbd(struct hidcc *);
int	hidcc_enable(void *, int);
void	hidcc_set_leds(void *, int);
int	hidcc_ioctl(void *, u_long, caddr_t, int, struct proc *);

int	hidcc_parse(struct hidcc *, void *, int, int, int);
int	hidcc_parse_array(struct hidcc *, const struct hid_item *);
int	hidcc_is_array(const struct hid_item *);
int	hidcc_add_key(struct hidcc *, int32_t, u_int);
int	hidcc_add_key_volume(struct hidcc *, const struct hid_item *, uint32_t,
    u_int);
int	hidcc_bit_to_sym(struct hidcc *, u_int, const struct hidcc_keysym **);
int	hidcc_usage_to_sym(int32_t, const struct hidcc_keysym **);
int	hidcc_bits_to_int(uint8_t *, u_int, int32_t *);
int	hidcc_bits_to_volume(struct hidcc *, uint8_t *, int, u_int *);
int	hidcc_intr_slice(struct hidcc *, uint8_t *, uint8_t *, int *);
void	hidcc_input(struct hidcc *, u_int, int);
void	hidcc_rawinput(struct hidcc *, u_char, int);
int	hidcc_setbits(struct hidcc *, uint8_t *, int, u_int *);

/*
 * Returns non-zero if the given report ID has at least one Consumer Control
 * usage.
 */
int
hidcc_match(void *desc, int descsiz, uint8_t repid)
{
	struct hid_item hi;
	struct hid_data *hd;
	int32_t maxusage = 0;

	hd = hid_start_parse(desc, descsiz, hid_input);
	while (hid_get_item(hd, &hi)) {
		if (hi.report_ID == repid &&
		    hi.kind == hid_input &&
		    HID_GET_USAGE_PAGE(hi.usage) == HUP_CONSUMER) {
			if (HID_GET_USAGE(hi.usage_maximum) > maxusage)
				maxusage = HID_GET_USAGE(hi.usage_maximum);
			else if (HID_GET_USAGE(hi.usage) > maxusage)
				maxusage = HID_GET_USAGE(hi.usage);
		}
	}
	hid_end_parse(hd);
	return maxusage > 0;
}

struct hidcc *
hidcc_attach(const struct hidcc_attach_arg *hca)
{
	struct hidcc *sc;
	int error;

	sc = malloc(sizeof(*sc), M_USBDEV, M_WAITOK | M_ZERO);
	sc->sc_dev = hca->device;
	sc->sc_mode = WSKBD_TRANSLATED;
	sc->sc_last_translate = -1;
	sc->sc_enable = hca->enable;
	sc->sc_arg = hca->arg;

	error = hidcc_parse(sc, hca->desc, hca->descsiz, hca->repid,
	    hca->isize);
	if (error) {
		printf(": hid error %d\n", error);
		free(sc, M_USBDEV, sizeof(*sc));
		return NULL;
	}

	printf(": %d usage%s, %d key%s, %s\n",
	    sc->sc_nusages, sc->sc_nusages == 1 ? "" : "s",
	    sc->sc_maplen / 2, sc->sc_maplen / 2 == 1 ? "" : "s",
	    sc->sc_isarray ? "array" : "enum");

	/* Cannot load an empty map. */
	if (sc->sc_maplen > 0)
		hidcc_attach_wskbd(sc);

	return sc;
}

int
hidcc_detach(struct hidcc *sc, int flags)
{
	int error = 0;

	if (sc->sc_wskbddev != NULL)
		error = config_detach(sc->sc_wskbddev, flags);
	free(sc->sc_input.i_buf, M_USBDEV, sc->sc_input.i_bufsiz);
	free(sc->sc_map, M_USBDEV, sc->sc_mapsiz * sizeof(*sc->sc_map));
	free(sc->sc_raw, M_USBDEV, sc->sc_rawsiz * sizeof(*sc->sc_raw));
	free(sc, M_USBDEV, sizeof(*sc));
	return error;
}

void
hidcc_intr(struct hidcc *sc, void *data, u_int len)
{
	const struct hidcc_keysym *ks;
	uint8_t *buf = sc->sc_input.i_buf;
	int raw = sc->sc_mode == WSKBD_RAW;
	int error;
	u_int bit = 0;

	hidcc_dump(sc, __func__, data, len);

	if (len > sc->sc_input.i_bufsiz)
		len = sc->sc_input.i_bufsiz;
	error = hidcc_intr_slice(sc, data, buf, &len);
	if (error) {
		DPRINTF("%s: slice failure: error %d\n", DEVNAME(sc), error);
		return;
	}

	/* Dump the buffer again after slicing. */
	hidcc_dump(sc, __func__, buf, len);

	if (hidcc_setbits(sc, buf, len, &bit)) {
		/* All zeroes, assume key up event. */
		if (raw) {
			if (sc->sc_last_raw != 0) {
				hidcc_rawinput(sc, sc->sc_last_raw, 1);
				sc->sc_last_raw = 0;
			}
		} else {
			if (sc->sc_last_translate != -1) {
				hidcc_input(sc, sc->sc_last_translate, 1);
				sc->sc_last_translate = -1;
			}
		}
		return;
	} else if (sc->sc_isarray) {
		int32_t usage;

		if (hidcc_bits_to_int(buf, len, &usage) ||
		    hidcc_usage_to_sym(usage, &ks))
			goto unknown;
		bit = ks->ks_usage;
	} else if (raw) {
		if (hidcc_bit_to_sym(sc, bit, &ks))
			goto unknown;
	}

	if (raw) {
		hidcc_rawinput(sc, ks->ks_raw, 0);
		sc->sc_last_raw = ks->ks_raw;
		/*
		 * Feed both raw and translating input for keys that have both
		 * defined. This is only the case for volume related keys.
		 */
		if (ks->ks_key == 0)
			return;
	}

	hidcc_input(sc, bit, 0);
	if (!raw)
		sc->sc_last_translate = bit;
	return;

unknown:
	DPRINTF("%s: unknown key: bit %d\n", DEVNAME(sc), bit);
}

void
hidcc_attach_wskbd(struct hidcc *sc)
{
	static const struct wskbd_accessops accessops = {
		.enable		= hidcc_enable,
		.set_leds	= hidcc_set_leds,
		.ioctl		= hidcc_ioctl,
	};
	struct wskbddev_attach_args a = {
		.console	= 0,
		.keymap		= &sc->sc_keymap,
		.accessops	= &accessops,
		.accesscookie	= sc,
		.audiocookie	= NULL,	/* XXX audio_cookie */
	};

	sc->sc_keydesc[0].name = KB_US;
	sc->sc_keydesc[0].base = 0;
	sc->sc_keydesc[0].map_size = sc->sc_maplen;
	sc->sc_keydesc[0].map = sc->sc_map;
	sc->sc_keymap.keydesc = sc->sc_keydesc;
	sc->sc_keymap.layout = KB_US | KB_NOENCODING;
	sc->sc_wskbddev = config_found(sc->sc_dev, &a, wskbddevprint);
}

int
hidcc_enable(void *v, int on)
{
	struct hidcc *sc = (struct hidcc *)v;

	return sc->sc_enable(sc->sc_arg, on);
}

void
hidcc_set_leds(void *v, int leds)
{
}

int
hidcc_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	switch (cmd) {
	/* wsconsctl(8) stub */
	case WSKBDIO_GTYPE:
		*(int *)data = WSKBD_TYPE_USB;
		return 0;

	/* wsconsctl(8) stub */
	case WSKBDIO_GETLEDS:
		*(int *)data = 0;
		return 0;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE: {
		struct hidcc *sc = (struct hidcc *)v;

		sc->sc_mode = *(int *)data;
		return 0;
	}
#endif
	}

	return -1;
}

/*
 * Parse the HID report and construct a mapping between the bits in the input
 * report and the corresponding pressed key.
 */
int
hidcc_parse(struct hidcc *sc, void *desc, int descsiz, int repid, int isize)
{
	enum { OFFSET, LENGTH } istate = OFFSET;
	struct hid_item hi;
	struct hid_data *hd;
	u_int bit = 0;
	int error = 0;
	int nsyms = nitems(hidcc_keysyms);
	int nbits;

	/*
	 * The size of the input report is expressed in bytes where each bit in
	 * turn represents a pressed key. It's likely that the number of keys is
	 * less than this generous estimate.
	 */
	nbits = isize * 8;
	if (nbits == 0)
		return ENXIO;

	/* Allocate buffer used to slice interrupt data. */
	sc->sc_input.i_bufsiz = isize;
	sc->sc_input.i_buf = malloc(sc->sc_input.i_bufsiz, M_USBDEV, M_WAITOK);

	/*
	 * Create mapping between each input bit and the corresponding usage,
	 * used in translating mode. Two entries are needed per bit in order
	 * construct a mapping. Note that at most all known usages as given by
	 * hidcc_keysyms can be inserted into this map.
	 */
	sc->sc_mapsiz = nsyms * 2;
	sc->sc_map = mallocarray(nsyms, 2 * sizeof(*sc->sc_map), M_USBDEV,
	    M_WAITOK | M_ZERO);

	/*
	 * Create mapping between each input bit and the corresponding usage,
	 * used in raw mode.
	 */
	sc->sc_rawsiz = nbits;
	sc->sc_raw = mallocarray(nbits, sizeof(*sc->sc_raw), M_USBDEV,
	    M_WAITOK | M_ZERO);

	hd = hid_start_parse(desc, descsiz, hid_input);
	while (hid_get_item(hd, &hi)) {
		uint32_t off;
		int32_t usage;

		if (hi.report_ID != repid || hi.kind != hid_input)
			continue;

		if (HID_GET_USAGE_PAGE(hi.usage) != HUP_CONSUMER) {
			uint32_t len = hi.loc.size * hi.loc.count;

			switch (istate) {
			case OFFSET:
				sc->sc_input.i_off = hi.loc.pos + len;
				break;
			case LENGTH:
				/* Constant padding. */
				if (hi.flags & HIO_CONST)
					sc->sc_input.i_len += len;
				break;
			}
			continue;
		}

		/* Signal that the input offset is reached. */
		istate = LENGTH;
		off = sc->sc_input.i_len;
		sc->sc_input.i_len += hi.loc.size * hi.loc.count;

		/*
		 * The usages could be expressed as an array instead of
		 * enumerating all supported ones.
		 */
		if (hidcc_is_array(&hi)) {
			error = hidcc_parse_array(sc, &hi);
			break;
		}

		usage = HID_GET_USAGE(hi.usage);
		if (usage == HUC_VOLUME)
			error = hidcc_add_key_volume(sc, &hi, off, bit);
		else
			error = hidcc_add_key(sc, usage, bit);
		if (error)
			break;
		sc->sc_nusages++;
		bit += hi.loc.size * hi.loc.count;
	}
	hid_end_parse(hd);

	DPRINTF("%s: input: off %d, len %d\n", DEVNAME(sc),
	    sc->sc_input.i_off, sc->sc_input.i_len);

	return error;
}

int
hidcc_parse_array(struct hidcc *sc, const struct hid_item *hi)
{
	int32_t max, min, usage;

	min = HID_GET_USAGE(hi->usage_minimum);
	max = HID_GET_USAGE(hi->usage_maximum);

	sc->sc_nusages = (max - min) + 1;
	sc->sc_isarray = 1;

	for (usage = min; usage <= max; usage++) {
		int error;

		error = hidcc_add_key(sc, usage, 0);
		if (error)
			return error;
	}

	return 0;
}

int
hidcc_is_array(const struct hid_item *hi)
{
	int32_t max, min;

	min = HID_GET_USAGE(hi->usage_minimum);
	max = HID_GET_USAGE(hi->usage_maximum);
	return min >= 0 && max > 0 && min < max;
}

int
hidcc_add_key(struct hidcc *sc, int32_t usage, u_int bit)
{
	const struct hidcc_keysym *ks;

	if (hidcc_usage_to_sym(usage, &ks))
		return 0;

	if (sc->sc_maplen + 2 > sc->sc_mapsiz)
		return ENOMEM;
	sc->sc_map[sc->sc_maplen++] = KS_KEYCODE(sc->sc_isarray ? usage : bit);
	sc->sc_map[sc->sc_maplen++] = ks->ks_key;

	if (!sc->sc_isarray) {
		if (bit >= sc->sc_rawsiz)
			return ENOMEM;
		sc->sc_raw[bit] = ks;
	}

	DPRINTF("%s: bit %d, usage \"%s\"\n", DEVNAME(sc),
	    bit, ks->ks_name);
	return 0;
}

/*
 * Add key mappings for the volume usage which differs compared to the volume
 * increment/decrement usages in which each volume change direction is
 * represented using a distinct usage. The volume usage instead uses bits of the
 * interrupt buffer to represent the wanted volume. The same bits should be
 * within the bounds given by the logical min/max associated with the HID item.
 */
int
hidcc_add_key_volume(struct hidcc *sc, const struct hid_item *hi,
    uint32_t off, u_int bit)
{
	uint32_t len;
	int error;

	/*
	 * Since the volume usage is internally represented using two key
	 * mappings, make sure enough bits are available to avoid any ambiguity.
	 */
	len = hi->loc.size * hi->loc.count;
	if (len <= 1)
		return 1;

	sc->sc_volume.v_inc = bit;
	sc->sc_volume.v_dec = bit + 1;
	sc->sc_volume.v_off = off;
	sc->sc_volume.v_len = len;

	DPRINTF("%s: inc %d, dec %d, off %d, len %d, min %d, max %d\n",
	    DEVNAME(sc), sc->sc_volume.v_inc, sc->sc_volume.v_dec,
	    sc->sc_volume.v_off, sc->sc_volume.v_len,
	    hi->logical_minimum, hi->logical_maximum);

	error = hidcc_add_key(sc, HUC_VOL_INC, sc->sc_volume.v_inc);
	if (error)
		return error;
	error = hidcc_add_key(sc, HUC_VOL_DEC, sc->sc_volume.v_dec);
	if (error)
		return error;
	return 0;
}

int
hidcc_bit_to_sym(struct hidcc *sc, u_int bit, const struct hidcc_keysym **ks)
{
	if (bit >= sc->sc_rawsiz || sc->sc_raw[bit] == NULL)
		return 1;
	*ks = sc->sc_raw[bit];
	return 0;
}

int
hidcc_usage_to_sym(int32_t usage, const struct hidcc_keysym **ks)
{
	int len = nitems(hidcc_keysyms);
	int i;

	for (i = 0; i < len; i++) {
		if (hidcc_keysyms[i].ks_usage == usage) {
			*ks = &hidcc_keysyms[i];
			return 0;
		}
	}
	return 1;
}

int
hidcc_bits_to_int(uint8_t *buf, u_int buflen, int32_t *usage)
{
	int32_t x = 0;
	int i;

	if (buflen == 0 || buflen > sizeof(*usage))
		return 1;

	for (i = buflen - 1; i >= 0; i--) {
		x |= buf[i];
		if (i > 0)
			x <<= 8;
	}
	*usage = x;
	return 0;
}

int
hidcc_bits_to_volume(struct hidcc *sc, uint8_t *buf, int buflen, u_int *bit)
{
	uint32_t vlen = sc->sc_volume.v_len;
	uint32_t voff = sc->sc_volume.v_off;
	int32_t vol;
	int sign;

	if (vlen == 0)
		return 1;
	if (hidcc_bits_to_int(buf, buflen, &vol))
		return 1;
	vol = (vol >> voff) & ((1 << vlen) - 1);
	if (vol == 0)
		return 1;

	/*
	 * Interpret the volume as a relative change by only looking at the sign
	 * in order to determine the change direction.
	 */
	sign = vol & (1 << (vlen - 1)) ? -1 : 1;
	if (sign < 0)
		vol = (1 << vlen) - vol;
	vol *= sign;
	if (vol > 0)
		*bit = sc->sc_volume.v_inc;
	else
		*bit = sc->sc_volume.v_dec;
	return 0;
}

int
hidcc_intr_slice(struct hidcc *sc, uint8_t *src, uint8_t *dst, int *len)
{
	int ilen = sc->sc_input.i_len;
	int ioff = sc->sc_input.i_off;
	int di, si;
	int maxlen = *len;

	if (maxlen == 0)
		return 1;

	memset(dst, 0, maxlen);
	si = ioff;
	di = 0;
	for (; ilen > 0; ilen--) {
		int db, sb;

		sb = si / 8;
		db = di / 8;
		if (sb >= maxlen || db >= maxlen)
			return 1;

		if (src[sb] & (1 << (si % 8)))
			dst[db] |= 1 << (di % 8);
		si++;
		di++;
	}

	*len = (sc->sc_input.i_len + 7) / 8;
	return 0;
}

void
hidcc_input(struct hidcc *sc, u_int bit, int release)
{
	int s;

	s = spltty();
	wskbd_input(sc->sc_wskbddev,
	    release ? WSCONS_EVENT_KEY_UP : WSCONS_EVENT_KEY_DOWN, bit);
	splx(s);
}

void
hidcc_rawinput(struct hidcc *sc, u_char c, int release)
{
#ifdef WSDISPLAY_COMPAT_RAWKBD
	u_char buf[2];
	int len = 0;
	int s;

	if (c & 0x80)
		buf[len++] = 0xe0;
	buf[len++] = c & 0x7f;
	if (release)
		buf[len - 1] |= 0x80;

	s = spltty();
	wskbd_rawinput(sc->sc_wskbddev, buf, len);
	splx(s);
#endif
}

int
hidcc_setbits(struct hidcc *sc, uint8_t *data, int len, u_int *bit)
{
	int i, j;

	if (hidcc_bits_to_volume(sc, data, len, bit) == 0)
		return 0;

	for (i = 0; i < len; i++) {
		if (data[i] == 0)
			continue;

		for (j = 0; j < 8; j++) {
			if (data[i] & (1 << j)) {
				*bit = (i * 8) + j;
				return 0;
			}
		}
	}

	return 1;
}

#ifdef HIDCC_DEBUG

void
hidcc_dump(struct hidcc *sc, const char *prefix, uint8_t *data, u_int len)
{
	u_int i;

	if (hidcc_debug == 0)
		return;

	printf("%s: %s:", DEVNAME(sc), prefix);
	for (i = 0; i < len; i++)
		printf(" %02x", data[i]);
	printf("\n");
}

#endif
