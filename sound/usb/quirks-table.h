/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ALSA USB Audio Driver
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>,
 *                       Clemens Ladisch <clemens@ladisch.de>
 */

/*
 * The contents of this file are part of the driver's id_table.
 *
 * In a perfect world, this file would be empty.
 */

/*
 * Use this for devices where other interfaces are standard compliant,
 * to prevent the quirk being applied to those interfaces. (To work with
 * hotplugging, bDeviceClass must be set to USB_CLASS_PER_INTERFACE.)
 */
#define USB_DEVICE_VENDOR_SPEC(vend, prod) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR | \
		       USB_DEVICE_ID_MATCH_PRODUCT | \
		       USB_DEVICE_ID_MATCH_INT_CLASS, \
	.idVendor = vend, \
	.idProduct = prod, \
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC

/* A standard entry matching with vid/pid and the audio class/subclass */
#define USB_AUDIO_DEVICE(vend, prod) \
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE | \
		       USB_DEVICE_ID_MATCH_INT_CLASS | \
		       USB_DEVICE_ID_MATCH_INT_SUBCLASS, \
	.idVendor = vend, \
	.idProduct = prod, \
	.bInterfaceClass = USB_CLASS_AUDIO, \
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL

/* Quirk .driver_info, followed by the definition of the quirk entry;
 * put like QUIRK_DRIVER_INFO { ... } in each entry of the quirk table
 */
#define QUIRK_DRIVER_INFO \
	.driver_info = (unsigned long)&(const struct snd_usb_audio_quirk)

/*
 * Macros for quirk data entries
 */

/* Quirk data entry for ignoring the interface */
#define QUIRK_DATA_IGNORE(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_IGNORE_INTERFACE
/* Quirk data entry for a standard audio interface */
#define QUIRK_DATA_STANDARD_AUDIO(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_AUDIO_STANDARD_INTERFACE
/* Quirk data entry for a standard MIDI interface */
#define QUIRK_DATA_STANDARD_MIDI(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_MIDI_STANDARD_INTERFACE
/* Quirk data entry for a standard mixer interface */
#define QUIRK_DATA_STANDARD_MIXER(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_AUDIO_STANDARD_MIXER

/* Quirk data entry for Yamaha MIDI */
#define QUIRK_DATA_MIDI_YAMAHA(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_MIDI_YAMAHA
/* Quirk data entry for Edirol UAxx */
#define QUIRK_DATA_EDIROL_UAXX(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_AUDIO_EDIROL_UAXX
/* Quirk data entry for raw bytes interface */
#define QUIRK_DATA_RAW_BYTES(_ifno) \
	.ifnum = (_ifno), .type = QUIRK_MIDI_RAW_BYTES

/* Quirk composite array terminator */
#define QUIRK_COMPOSITE_END	{ .ifnum = -1 }

/* Quirk data entry for composite quirks;
 * followed by the quirk array that is terminated with QUIRK_COMPOSITE_END
 * e.g. QUIRK_DATA_COMPOSITE { { quirk1 }, { quirk2 },..., QUIRK_COMPOSITE_END }
 */
#define QUIRK_DATA_COMPOSITE \
	.ifnum = QUIRK_ANY_INTERFACE, \
	.type = QUIRK_COMPOSITE, \
	.data = &(const struct snd_usb_audio_quirk[])

/* Quirk data entry for a fixed audio endpoint;
 * followed by audioformat definition
 * e.g. QUIRK_DATA_AUDIOFORMAT(n) { .formats = xxx, ... }
 */
#define QUIRK_DATA_AUDIOFORMAT(_ifno)	    \
	.ifnum = (_ifno),		    \
	.type = QUIRK_AUDIO_FIXED_ENDPOINT, \
	.data = &(const struct audioformat)

/* Quirk data entry for a fixed MIDI endpoint;
 * followed by snd_usb_midi_endpoint_info definition
 * e.g. QUIRK_DATA_MIDI_FIXED_ENDPOINT(n) { .out_cables = x, .in_cables = y }
 */
#define QUIRK_DATA_MIDI_FIXED_ENDPOINT(_ifno) \
	.ifnum = (_ifno),		      \
	.type = QUIRK_MIDI_FIXED_ENDPOINT,    \
	.data = &(const struct snd_usb_midi_endpoint_info)
/* Quirk data entry for a MIDIMAN MIDI endpoint */
#define QUIRK_DATA_MIDI_MIDIMAN(_ifno) \
	.ifnum = (_ifno),	       \
	.type = QUIRK_MIDI_MIDIMAN,    \
	.data = &(const struct snd_usb_midi_endpoint_info)
/* Quirk data entry for a EMAGIC MIDI endpoint */
#define QUIRK_DATA_MIDI_EMAGIC(_ifno) \
	.ifnum = (_ifno),	      \
	.type = QUIRK_MIDI_EMAGIC,    \
	.data = &(const struct snd_usb_midi_endpoint_info)

/*
 * Here we go... the quirk table definition begins:
 */

/* FTDI devices */
{
	USB_DEVICE(0x0403, 0xb8d8),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "STARR LABS", */
		/* .product_name = "Starr Labs MIDI USB device", */
		.ifnum = 0,
		.type = QUIRK_MIDI_FTDI
	}
},

{
	/* Creative BT-D1 */
	USB_DEVICE(0x041e, 0x0005),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_AUDIOFORMAT(1) {
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels = 2,
			.iface = 1,
			.altsetting = 1,
			.altset_idx = 1,
			.endpoint = 0x03,
			.ep_attr = USB_ENDPOINT_XFER_ISOC,
			.attributes = 0,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min = 48000,
			.rate_max = 48000,
		}
	}
},

/* E-Mu 0202 USB */
{ USB_DEVICE_VENDOR_SPEC(0x041e, 0x3f02) },
/* E-Mu 0404 USB */
{ USB_DEVICE_VENDOR_SPEC(0x041e, 0x3f04) },
/* E-Mu Tracker Pre */
{ USB_DEVICE_VENDOR_SPEC(0x041e, 0x3f0a) },
/* E-Mu 0204 USB */
{ USB_DEVICE_VENDOR_SPEC(0x041e, 0x3f19) },
/* Ktmicro Usb_audio device */
{ USB_DEVICE_VENDOR_SPEC(0x31b2, 0x0011) },

/*
 * Creative Technology, Ltd Live! Cam Sync HD [VF0770]
 * The device advertises 8 formats, but only a rate of 48kHz is honored by the
 * hardware and 24 bits give chopped audio, so only report the one working
 * combination.
 */
{
	USB_AUDIO_DEVICE(0x041e, 0x4095),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(2) },
			{
				QUIRK_DATA_AUDIOFORMAT(3) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 2,
					.fmt_bits = 16,
					.iface = 3,
					.altsetting = 4,
					.altset_idx = 4,
					.endpoint = 0x82,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 },
				},
			},
			QUIRK_COMPOSITE_END
		},
	},
},

/*
 * HP Wireless Audio
 * When not ignored, causes instability issues for some users, forcing them to
 * skip the entire module.
 */
{
	USB_DEVICE(0x0424, 0xb832),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Standard Microsystems Corp.",
		.product_name = "HP Wireless Audio",
		QUIRK_DATA_COMPOSITE {
			/* Mixer */
			{ QUIRK_DATA_IGNORE(0) },
			/* Playback */
			{ QUIRK_DATA_IGNORE(1) },
			/* Capture */
			{ QUIRK_DATA_IGNORE(2) },
			/* HID Device, .ifnum = 3 */
			QUIRK_COMPOSITE_END
		}
	}
},

/*
 * Logitech QuickCam: bDeviceClass is vendor-specific, so generic interface
 * class matches do not take effect without an explicit ID match.
 */
{ USB_AUDIO_DEVICE(0x046d, 0x0850) },
{ USB_AUDIO_DEVICE(0x046d, 0x08ae) },
{ USB_AUDIO_DEVICE(0x046d, 0x08c6) },
{ USB_AUDIO_DEVICE(0x046d, 0x08f0) },
{ USB_AUDIO_DEVICE(0x046d, 0x08f5) },
{ USB_AUDIO_DEVICE(0x046d, 0x08f6) },
{ USB_AUDIO_DEVICE(0x046d, 0x0990) },

/*
 * Yamaha devices
 */

#define YAMAHA_DEVICE(id, name) { \
	USB_DEVICE(0x0499, id), \
	QUIRK_DRIVER_INFO { \
		.vendor_name = "Yamaha", \
		.product_name = name, \
		QUIRK_DATA_MIDI_YAMAHA(QUIRK_ANY_INTERFACE) \
	} \
}
#define YAMAHA_INTERFACE(id, intf, name) { \
	USB_DEVICE_VENDOR_SPEC(0x0499, id), \
	QUIRK_DRIVER_INFO { \
		.vendor_name = "Yamaha", \
		.product_name = name, \
		QUIRK_DATA_MIDI_YAMAHA(intf) \
	} \
}
YAMAHA_DEVICE(0x1000, "UX256"),
YAMAHA_DEVICE(0x1001, "MU1000"),
YAMAHA_DEVICE(0x1002, "MU2000"),
YAMAHA_DEVICE(0x1003, "MU500"),
YAMAHA_INTERFACE(0x1004, 3, "UW500"),
YAMAHA_DEVICE(0x1005, "MOTIF6"),
YAMAHA_DEVICE(0x1006, "MOTIF7"),
YAMAHA_DEVICE(0x1007, "MOTIF8"),
YAMAHA_DEVICE(0x1008, "UX96"),
YAMAHA_DEVICE(0x1009, "UX16"),
YAMAHA_INTERFACE(0x100a, 3, "EOS BX"),
YAMAHA_DEVICE(0x100c, "UC-MX"),
YAMAHA_DEVICE(0x100d, "UC-KX"),
YAMAHA_DEVICE(0x100e, "S08"),
YAMAHA_DEVICE(0x100f, "CLP-150"),
YAMAHA_DEVICE(0x1010, "CLP-170"),
YAMAHA_DEVICE(0x1011, "P-250"),
YAMAHA_DEVICE(0x1012, "TYROS"),
YAMAHA_DEVICE(0x1013, "PF-500"),
YAMAHA_DEVICE(0x1014, "S90"),
YAMAHA_DEVICE(0x1015, "MOTIF-R"),
YAMAHA_DEVICE(0x1016, "MDP-5"),
YAMAHA_DEVICE(0x1017, "CVP-204"),
YAMAHA_DEVICE(0x1018, "CVP-206"),
YAMAHA_DEVICE(0x1019, "CVP-208"),
YAMAHA_DEVICE(0x101a, "CVP-210"),
YAMAHA_DEVICE(0x101b, "PSR-1100"),
YAMAHA_DEVICE(0x101c, "PSR-2100"),
YAMAHA_DEVICE(0x101d, "CLP-175"),
YAMAHA_DEVICE(0x101e, "PSR-K1"),
YAMAHA_DEVICE(0x101f, "EZ-J24"),
YAMAHA_DEVICE(0x1020, "EZ-250i"),
YAMAHA_DEVICE(0x1021, "MOTIF ES 6"),
YAMAHA_DEVICE(0x1022, "MOTIF ES 7"),
YAMAHA_DEVICE(0x1023, "MOTIF ES 8"),
YAMAHA_DEVICE(0x1024, "CVP-301"),
YAMAHA_DEVICE(0x1025, "CVP-303"),
YAMAHA_DEVICE(0x1026, "CVP-305"),
YAMAHA_DEVICE(0x1027, "CVP-307"),
YAMAHA_DEVICE(0x1028, "CVP-309"),
YAMAHA_DEVICE(0x1029, "CVP-309GP"),
YAMAHA_DEVICE(0x102a, "PSR-1500"),
YAMAHA_DEVICE(0x102b, "PSR-3000"),
YAMAHA_DEVICE(0x102e, "ELS-01/01C"),
YAMAHA_DEVICE(0x1030, "PSR-295/293"),
YAMAHA_DEVICE(0x1031, "DGX-205/203"),
YAMAHA_DEVICE(0x1032, "DGX-305"),
YAMAHA_DEVICE(0x1033, "DGX-505"),
YAMAHA_DEVICE(0x1034, NULL),
YAMAHA_DEVICE(0x1035, NULL),
YAMAHA_DEVICE(0x1036, NULL),
YAMAHA_DEVICE(0x1037, NULL),
YAMAHA_DEVICE(0x1038, NULL),
YAMAHA_DEVICE(0x1039, NULL),
YAMAHA_DEVICE(0x103a, NULL),
YAMAHA_DEVICE(0x103b, NULL),
YAMAHA_DEVICE(0x103c, NULL),
YAMAHA_DEVICE(0x103d, NULL),
YAMAHA_DEVICE(0x103e, NULL),
YAMAHA_DEVICE(0x103f, NULL),
YAMAHA_DEVICE(0x1040, NULL),
YAMAHA_DEVICE(0x1041, NULL),
YAMAHA_DEVICE(0x1042, NULL),
YAMAHA_DEVICE(0x1043, NULL),
YAMAHA_DEVICE(0x1044, NULL),
YAMAHA_DEVICE(0x1045, NULL),
YAMAHA_INTERFACE(0x104e, 0, NULL),
YAMAHA_DEVICE(0x104f, NULL),
YAMAHA_DEVICE(0x1050, NULL),
YAMAHA_DEVICE(0x1051, NULL),
YAMAHA_DEVICE(0x1052, NULL),
YAMAHA_INTERFACE(0x1053, 0, NULL),
YAMAHA_INTERFACE(0x1054, 0, NULL),
YAMAHA_DEVICE(0x1055, NULL),
YAMAHA_DEVICE(0x1056, NULL),
YAMAHA_DEVICE(0x1057, NULL),
YAMAHA_DEVICE(0x1058, NULL),
YAMAHA_DEVICE(0x1059, NULL),
YAMAHA_DEVICE(0x105a, NULL),
YAMAHA_DEVICE(0x105b, NULL),
YAMAHA_DEVICE(0x105c, NULL),
YAMAHA_DEVICE(0x105d, NULL),
{
	USB_DEVICE(0x0499, 0x1503),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Yamaha", */
		/* .product_name = "MOX6/MOX8", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_MIDI_YAMAHA(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0499, 0x1507),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Yamaha", */
		/* .product_name = "THR10", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_MIDI_YAMAHA(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0499, 0x1509),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Yamaha", */
		/* .product_name = "Steinberg UR22", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_MIDI_YAMAHA(3) },
			{ QUIRK_DATA_IGNORE(4) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0499, 0x150a),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Yamaha", */
		/* .product_name = "THR5A", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_MIDI_YAMAHA(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0499, 0x150c),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Yamaha", */
		/* .product_name = "THR10C", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_MIDI_YAMAHA(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0499, 0x1718),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Yamaha", */
		/* .product_name = "P-125", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_MIDI_YAMAHA(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
YAMAHA_DEVICE(0x2000, "DGP-7"),
YAMAHA_DEVICE(0x2001, "DGP-5"),
YAMAHA_DEVICE(0x2002, NULL),
YAMAHA_DEVICE(0x2003, NULL),
YAMAHA_DEVICE(0x5000, "CS1D"),
YAMAHA_DEVICE(0x5001, "DSP1D"),
YAMAHA_DEVICE(0x5002, "DME32"),
YAMAHA_DEVICE(0x5003, "DM2000"),
YAMAHA_DEVICE(0x5004, "02R96"),
YAMAHA_DEVICE(0x5005, "ACU16-C"),
YAMAHA_DEVICE(0x5006, "NHB32-C"),
YAMAHA_DEVICE(0x5007, "DM1000"),
YAMAHA_DEVICE(0x5008, "01V96"),
YAMAHA_DEVICE(0x5009, "SPX2000"),
YAMAHA_DEVICE(0x500a, "PM5D"),
YAMAHA_DEVICE(0x500b, "DME64N"),
YAMAHA_DEVICE(0x500c, "DME24N"),
YAMAHA_DEVICE(0x500d, NULL),
YAMAHA_DEVICE(0x500e, NULL),
YAMAHA_DEVICE(0x500f, NULL),
YAMAHA_DEVICE(0x7000, "DTX"),
YAMAHA_DEVICE(0x7010, "UB99"),
#undef YAMAHA_DEVICE
#undef YAMAHA_INTERFACE
/* this catches most recent vendor-specific Yamaha devices */
{
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR |
	               USB_DEVICE_ID_MATCH_INT_CLASS,
	.idVendor = 0x0499,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	QUIRK_DRIVER_INFO {
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_AUTODETECT
	}
},

/*
 * Roland/RolandED/Edirol/BOSS devices
 */
{
	USB_DEVICE(0x0582, 0x0000),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "UA-100",
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x01,
					.ep_attr = 0x09,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_FILL_MAX,
					.endpoint = 0x81,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0007,
					.in_cables  = 0x0007
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0002),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UM-4",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x000f,
					.in_cables  = 0x000f
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0003),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "SC-8850",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x003f,
					.in_cables  = 0x003f
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0004),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "U-8",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0005,
					.in_cables  = 0x0005
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Has ID 0x0099 when not in "Advanced Driver" mode.
	 * The UM-2EX has only one input, but we cannot detect this. */
	USB_DEVICE(0x0582, 0x0005),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UM-2",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0003,
					.in_cables  = 0x0003
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0007),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "SC-8820",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0013,
					.in_cables  = 0x0013
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0008),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "PC-300",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x009d when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0009),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UM-1",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x000b),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "SK-500",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0013,
					.in_cables  = 0x0013
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* thanks to Emiliano Grilli <emillo@libero.it>
	 * for helping researching this data */
	USB_DEVICE(0x0582, 0x000c),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "SC-D70",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0007,
					.in_cables  = 0x0007
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{	/*
	 * This quirk is for the "Advanced Driver" mode of the Edirol UA-5.
	 * If the advanced mode switch at the back of the unit is off, the
	 * UA-5 has ID 0x0582/0x0011 and is standard compliant (no quirks),
	 * but offers only 16-bit PCM.
	 * In advanced mode, the UA-5 will output S24_3LE samples (two
	 * channels) at the rate indicated on the front switch, including
	 * the 96kHz sample rate.
	 */
	USB_DEVICE(0x0582, 0x0010),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-5",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x0013 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0012),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "XV-5050",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x0015 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0014),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UM-880",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	/* has ID 0x0017 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0016),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "SD-90",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x000f,
					.in_cables  = 0x000f
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x001c when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x001b),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "MMP-2",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x001e when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x001d),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "V-SYNTH",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x0024 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0023),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UM-550",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x003f,
			.in_cables  = 0x003f
		}
	}
},
{
	/*
	 * This quirk is for the "Advanced Driver" mode. If off, the UA-20
	 * has ID 0x0026 and is standard compliant, but has only 16-bit PCM
	 * and no MIDI.
	 */
	USB_DEVICE(0x0582, 0x0025),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-20",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x01,
					.ep_attr = 0x01,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 2,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x82,
					.ep_attr = 0x01,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(3) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x0028 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0027),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "SD-20",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0003,
			.in_cables  = 0x0007
		}
	}
},
{
	/* has ID 0x002a when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0029),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "SD-80",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{	/*
	 * This quirk is for the "Advanced" modes of the Edirol UA-700.
	 * If the sample format switch is not in an advanced setting, the
	 * UA-700 has ID 0x0582/0x002c and is standard compliant (no quirks),
	 * but offers only 16-bit PCM and no MIDI.
	 */
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x002b),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-700",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_EDIROL_UAXX(1) },
			{ QUIRK_DATA_EDIROL_UAXX(2) },
			{ QUIRK_DATA_EDIROL_UAXX(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x002e when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x002d),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "XV-2020",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x0030 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x002f),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "VariOS",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0007,
			.in_cables  = 0x0007
		}
	}
},
{
	/* has ID 0x0034 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0033),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "PCR",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0003,
			.in_cables  = 0x0007
		}
	}
},
{
	/*
	 * Has ID 0x0038 when not in "Advanced Driver" mode;
	 * later revisions use IDs 0x0054 and 0x00a2.
	 */
	USB_DEVICE(0x0582, 0x0037),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "Digital Piano",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/*
	 * This quirk is for the "Advanced Driver" mode.  If off, the GS-10
	 * has ID 0x003c and is standard compliant, but has only 16-bit PCM
	 * and no MIDI.
	 */
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x003b),
	QUIRK_DRIVER_INFO {
		.vendor_name = "BOSS",
		.product_name = "GS-10",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{ QUIRK_DATA_STANDARD_MIDI(3) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x0041 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0040),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "GI-20",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x0043 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0042),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "RS-70",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x0049 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0047),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "EDIROL", */
		/* .product_name = "UR-80", */
		QUIRK_DATA_COMPOSITE {
			/* in the 96 kHz modes, only interface 1 is there */
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x004a when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0048),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "EDIROL", */
		/* .product_name = "UR-80", */
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0003,
			.in_cables  = 0x0007
		}
	}
},
{
	/* has ID 0x004e when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x004c),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "PCR-A",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x004f when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x004d),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "PCR-A",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0003,
			.in_cables  = 0x0007
		}
	}
},
{
	/*
	 * This quirk is for the "Advanced Driver" mode. If off, the UA-3FX
	 * is standard compliant, but has only 16-bit PCM.
	 */
	USB_DEVICE(0x0582, 0x0050),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-3FX",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0052),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UM-1SX",
		QUIRK_DATA_STANDARD_MIDI(0)
	}
},
{
	USB_DEVICE(0x0582, 0x0060),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "EXR Series",
		QUIRK_DATA_STANDARD_MIDI(0)
	}
},
{
	/* has ID 0x0066 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0064),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "EDIROL", */
		/* .product_name = "PCR-1", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x0067 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0065),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "EDIROL", */
		/* .product_name = "PCR-1", */
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0003
		}
	}
},
{
	/* has ID 0x006e when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x006d),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "FANTOM-X",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{	/*
	 * This quirk is for the "Advanced" modes of the Edirol UA-25.
	 * If the switch is not in an advanced setting, the UA-25 has
	 * ID 0x0582/0x0073 and is standard compliant (no quirks), but
	 * offers only 16-bit PCM at 44.1 kHz and no MIDI.
	 */
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0074),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-25",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_EDIROL_UAXX(0) },
			{ QUIRK_DATA_EDIROL_UAXX(1) },
			{ QUIRK_DATA_EDIROL_UAXX(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* has ID 0x0076 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0075),
	QUIRK_DRIVER_INFO {
		.vendor_name = "BOSS",
		.product_name = "DR-880",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x007b when not in "Advanced Driver" mode */
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x007a),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		/* "RD" or "RD-700SX"? */
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	/* has ID 0x0081 when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x0080),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Roland",
		.product_name = "G-70",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/* has ID 0x008c when not in "Advanced Driver" mode */
	USB_DEVICE(0x0582, 0x008b),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "PC-50",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	/*
	 * This quirk is for the "Advanced Driver" mode. If off, the UA-4FX
	 * is standard compliant, but has only 16-bit PCM and no MIDI.
	 */
	USB_DEVICE(0x0582, 0x00a3),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-4FX",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_EDIROL_UAXX(0) },
			{ QUIRK_DATA_EDIROL_UAXX(1) },
			{ QUIRK_DATA_EDIROL_UAXX(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Edirol M-16DX */
	USB_DEVICE(0x0582, 0x00c4),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Advanced modes of the Edirol UA-25EX.
	 * For the standard mode, UA-25EX has ID 0582:00e7, which
	 * offers only 16-bit PCM at 44.1 kHz and no MIDI.
	 */
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x00e6),
	QUIRK_DRIVER_INFO {
		.vendor_name = "EDIROL",
		.product_name = "UA-25EX",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_EDIROL_UAXX(0) },
			{ QUIRK_DATA_EDIROL_UAXX(1) },
			{ QUIRK_DATA_EDIROL_UAXX(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Edirol UM-3G */
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0108),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(0) {
			.out_cables = 0x0007,
			.in_cables  = 0x0007
		}
	}
},
{
	/* BOSS ME-25 */
	USB_DEVICE(0x0582, 0x0113),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* only 44.1 kHz works at the moment */
	USB_DEVICE(0x0582, 0x0120),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Roland", */
		/* .product_name = "OCTO-CAPTURE", */
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 10,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x05,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 12,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x85,
					.ep_attr = 0x25,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			{ QUIRK_DATA_IGNORE(3) },
			{ QUIRK_DATA_IGNORE(4) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* only 44.1 kHz works at the moment */
	USB_DEVICE(0x0582, 0x012f),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Roland", */
		/* .product_name = "QUAD-CAPTURE", */
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x05,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 6,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x85,
					.ep_attr = 0x25,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			{ QUIRK_DATA_IGNORE(3) },
			{ QUIRK_DATA_IGNORE(4) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0159),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Roland", */
		/* .product_name = "UA-22", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(2) {
					.out_cables = 0x0001,
					.in_cables = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/* UA101 and co are supported by another driver */
{
	USB_DEVICE(0x0582, 0x0044), /* UA-1000 high speed */
	QUIRK_DRIVER_INFO {
		.ifnum = QUIRK_NODEV_INTERFACE
	},
},
{
	USB_DEVICE(0x0582, 0x007d), /* UA-101 high speed */
	QUIRK_DRIVER_INFO {
		.ifnum = QUIRK_NODEV_INTERFACE
	},
},
{
	USB_DEVICE(0x0582, 0x008d), /* UA-101 full speed */
	QUIRK_DRIVER_INFO {
		.ifnum = QUIRK_NODEV_INTERFACE
	},
},

/* this catches most recent vendor-specific Roland devices */
{
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR |
	               USB_DEVICE_ID_MATCH_INT_CLASS,
	.idVendor = 0x0582,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	QUIRK_DRIVER_INFO {
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_AUTODETECT
	}
},

/* Guillemot devices */
{
	/*
	 * This is for the "Windows Edition" where the external MIDI ports are
	 * the only MIDI ports; the control data is reported through HID
	 * interfaces.  The "Macintosh Edition" has ID 0xd002 and uses standard
	 * compliant USB MIDI ports for external MIDI and controls.
	 */
	USB_DEVICE_VENDOR_SPEC(0x06f8, 0xb000),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Hercules",
		.product_name = "DJ Console (WE)",
		QUIRK_DATA_MIDI_FIXED_ENDPOINT(4) {
			.out_cables = 0x0001,
			.in_cables = 0x0001
		}
	}
},

/* Midiman/M-Audio devices */
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1002),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 2x2",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1011),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 1x1",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1015),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "Keystation",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1021),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 4x4",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	/*
	 * For hardware revision 1.05; in the later revisions (1.10 and
	 * 1.21), 0x1031 is the ID for the device without firmware.
	 * Thanks to Olaf Giesbrecht <Olaf_Giesbrecht@yahoo.de>
	 */
	USB_DEVICE_VER(0x0763, 0x1031, 0x0100, 0x0109),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 8x8",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1033),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 8x8",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1041),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 2x4",
		QUIRK_DATA_MIDI_MIDIMAN(QUIRK_ANY_INTERFACE) {
			.out_cables = 0x000f,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2001),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "Quattro",
		QUIRK_DATA_COMPOSITE {
			/*
			 * Interfaces 0-2 are "Windows-compatible", 16-bit only,
			 * and share endpoints with the other interfaces.
			 * Ignore them.  The other interfaces can do 24 bits,
			 * but captured samples are big-endian (see usbaudio.c).
			 */
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{ QUIRK_DATA_IGNORE(2) },
			{ QUIRK_DATA_IGNORE(3) },
			{ QUIRK_DATA_STANDARD_AUDIO(4) },
			{ QUIRK_DATA_STANDARD_AUDIO(5) },
			{ QUIRK_DATA_IGNORE(6) },
			{ QUIRK_DATA_STANDARD_AUDIO(7) },
			{ QUIRK_DATA_STANDARD_AUDIO(8) },
			{
				QUIRK_DATA_MIDI_MIDIMAN(9) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2003),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "AudioPhile",
		QUIRK_DATA_MIDI_MIDIMAN(6) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2008),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "Ozone",
		QUIRK_DATA_MIDI_MIDIMAN(3) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x200d),
	QUIRK_DRIVER_INFO {
		.vendor_name = "M-Audio",
		.product_name = "OmniStudio",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{ QUIRK_DATA_IGNORE(2) },
			{ QUIRK_DATA_IGNORE(3) },
			{ QUIRK_DATA_STANDARD_AUDIO(4) },
			{ QUIRK_DATA_STANDARD_AUDIO(5) },
			{ QUIRK_DATA_IGNORE(6) },
			{ QUIRK_DATA_STANDARD_AUDIO(7) },
			{ QUIRK_DATA_STANDARD_AUDIO(8) },
			{
				QUIRK_DATA_MIDI_MIDIMAN(9) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x0763, 0x2019),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "M-Audio", */
		/* .product_name = "Ozone Academic", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{
				QUIRK_DATA_MIDI_MIDIMAN(3) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* M-Audio Micro */
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x201a),
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2030),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "M-Audio", */
		/* .product_name = "Fast Track C400", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(1) },
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 6,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = 0x09,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
							44100, 48000, 88200, 96000
					},
					.clock = 0x80,
				}
			},
			/* Capture */
			{
				QUIRK_DATA_AUDIOFORMAT(3) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4,
					.iface = 3,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x81,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					},
					.clock = 0x80,
				}
			},
			/* MIDI: Interface = 4*/
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2031),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "M-Audio", */
		/* .product_name = "Fast Track C600", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(1) },
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = 0x09,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
							44100, 48000, 88200, 96000
					},
					.clock = 0x80,
				}
			},
			/* Capture */
			{
				QUIRK_DATA_AUDIOFORMAT(3) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 6,
					.iface = 3,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x81,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					},
					.clock = 0x80,
				}
			},
			/* MIDI: Interface = 4 */
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2080),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "M-Audio", */
		/* .product_name = "Fast Track Ultra", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = 0x09,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x81,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					}
				}
			},
			/* interface 3 (MIDI) is standard compliant */
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2081),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "M-Audio", */
		/* .product_name = "Fast Track Ultra 8R", */
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = 0x09,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
							44100, 48000, 88200, 96000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x81,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					}
				}
			},
			/* interface 3 (MIDI) is standard compliant */
			QUIRK_COMPOSITE_END
		}
	}
},

/* Casio devices */
{
	USB_DEVICE(0x07cf, 0x6801),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Casio",
		.product_name = "PL-40R",
		QUIRK_DATA_MIDI_YAMAHA(0)
	}
},
{
	/* this ID is used by several devices without a product ID */
	USB_DEVICE(0x07cf, 0x6802),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Casio",
		.product_name = "Keyboard",
		QUIRK_DATA_MIDI_YAMAHA(0)
	}
},

/* Mark of the Unicorn devices */
{
	/* thanks to Robert A. Lerche <ral 'at' msbit.com> */
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR |
		       USB_DEVICE_ID_MATCH_PRODUCT |
		       USB_DEVICE_ID_MATCH_DEV_SUBCLASS,
	.idVendor = 0x07fd,
	.idProduct = 0x0001,
	.bDeviceSubClass = 2,
	QUIRK_DRIVER_INFO {
		.vendor_name = "MOTU",
		.product_name = "Fastlane",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_RAW_BYTES(0) },
			{ QUIRK_DATA_IGNORE(1) },
			QUIRK_COMPOSITE_END
		}
	}
},

/* Emagic devices */
{
	USB_DEVICE(0x086a, 0x0001),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Emagic",
		.product_name = "Unitor8",
		QUIRK_DATA_MIDI_EMAGIC(2) {
			.out_cables = 0x80ff,
			.in_cables  = 0x80ff
		}
	}
},
{
	USB_DEVICE(0x086a, 0x0002),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Emagic",
		/* .product_name = "AMT8", */
		QUIRK_DATA_MIDI_EMAGIC(2) {
			.out_cables = 0x80ff,
			.in_cables  = 0x80ff
		}
	}
},
{
	USB_DEVICE(0x086a, 0x0003),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Emagic",
		/* .product_name = "MT4", */
		QUIRK_DATA_MIDI_EMAGIC(2) {
			.out_cables = 0x800f,
			.in_cables  = 0x8003
		}
	}
},

/* KORG devices */
{
	USB_DEVICE_VENDOR_SPEC(0x0944, 0x0200),
	QUIRK_DRIVER_INFO {
		.vendor_name = "KORG, Inc.",
		/* .product_name = "PANDORA PX5D", */
		QUIRK_DATA_STANDARD_MIDI(3)
	}
},

{
	USB_DEVICE_VENDOR_SPEC(0x0944, 0x0201),
	QUIRK_DRIVER_INFO {
		.vendor_name = "KORG, Inc.",
		/* .product_name = "ToneLab ST", */
		QUIRK_DATA_STANDARD_MIDI(3)
	}
},

{
	USB_DEVICE_VENDOR_SPEC(0x0944, 0x0204),
	QUIRK_DRIVER_INFO {
		.vendor_name = "KORG, Inc.",
		/* .product_name = "ToneLab EX", */
		QUIRK_DATA_STANDARD_MIDI(3)
	}
},

/* AKAI devices */
{
	USB_DEVICE(0x09e8, 0x0062),
	QUIRK_DRIVER_INFO {
		.vendor_name = "AKAI",
		.product_name = "MPD16",
		.ifnum = 0,
		.type = QUIRK_MIDI_AKAI,
	}
},

{
	/* Akai MPC Element */
	USB_DEVICE(0x09e8, 0x0021),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_STANDARD_MIDI(1) },
			QUIRK_COMPOSITE_END
		}
	}
},

/* Steinberg devices */
{
	/* Steinberg MI2 */
	USB_DEVICE_VENDOR_SPEC(0x0a4e, 0x2040),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(3) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Steinberg MI4 */
	USB_DEVICE_VENDOR_SPEC(0x0a4e, 0x4040),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(3) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/* TerraTec devices */
{
	USB_DEVICE_VENDOR_SPEC(0x0ccd, 0x0012),
	QUIRK_DRIVER_INFO {
		.vendor_name = "TerraTec",
		.product_name = "PHASE 26",
		QUIRK_DATA_STANDARD_MIDI(3)
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0ccd, 0x0013),
	QUIRK_DRIVER_INFO {
		.vendor_name = "TerraTec",
		.product_name = "PHASE 26",
		QUIRK_DATA_STANDARD_MIDI(3)
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0ccd, 0x0014),
	QUIRK_DRIVER_INFO {
		.vendor_name = "TerraTec",
		.product_name = "PHASE 26",
		QUIRK_DATA_STANDARD_MIDI(3)
	}
},
{
	USB_DEVICE(0x0ccd, 0x0035),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Miditech",
		.product_name = "Play'n Roll",
		.ifnum = 0,
		.type = QUIRK_MIDI_CME
	}
},

/* Stanton ScratchAmp */
{ USB_DEVICE(0x103d, 0x0100) },
{ USB_DEVICE(0x103d, 0x0101) },

/* Novation EMS devices */
{
	USB_DEVICE_VENDOR_SPEC(0x1235, 0x0001),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Novation",
		.product_name = "ReMOTE Audio/XStation",
		.ifnum = 4,
		.type = QUIRK_MIDI_NOVATION
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x1235, 0x0002),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Novation",
		.product_name = "Speedio",
		.ifnum = 3,
		.type = QUIRK_MIDI_NOVATION
	}
},
{
	USB_DEVICE(0x1235, 0x000a),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Novation", */
		/* .product_name = "Nocturn", */
		QUIRK_DATA_RAW_BYTES(0)
	}
},
{
	USB_DEVICE(0x1235, 0x000e),
	QUIRK_DRIVER_INFO {
		/* .vendor_name = "Novation", */
		/* .product_name = "Launchpad", */
		QUIRK_DATA_RAW_BYTES(0)
	}
},
{
	USB_DEVICE(0x1235, 0x0010),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Focusrite",
		.product_name = "Saffire 6 USB",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC,
					.datainterval = 1,
					.maxpacksize = 0x024c,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000,
					.rate_min = 44100,
					.rate_max = 48000,
					.nr_rates = 2,
					.rate_table = (unsigned int[]) {
						44100, 48000
					},
					.sync_ep = 0x82,
					.sync_iface = 0,
					.sync_altsetting = 1,
					.sync_ep_idx = 1,
					.implicit_fb = 1,
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 2,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC,
					.datainterval = 1,
					.maxpacksize = 0x0126,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000,
					.rate_min = 44100,
					.rate_max = 48000,
					.nr_rates = 2,
					.rate_table = (unsigned int[]) {
						44100, 48000
					}
				}
			},
			{ QUIRK_DATA_RAW_BYTES(1) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE(0x1235, 0x0018),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Novation",
		.product_name = "Twitch",
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000,
					.rate_min = 44100,
					.rate_max = 48000,
					.nr_rates = 2,
					.rate_table = (unsigned int[]) {
						44100, 48000
					}
				}
			},
			{ QUIRK_DATA_RAW_BYTES(1) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x1235, 0x4661),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Novation",
		.product_name = "ReMOTE25",
		.ifnum = 0,
		.type = QUIRK_MIDI_NOVATION
	}
},

/* Access Music devices */
{
	/* VirusTI Desktop */
	USB_DEVICE_VENDOR_SPEC(0x133e, 0x0815),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(3) {
					.out_cables = 0x0003,
					.in_cables  = 0x0003
				}
			},
			{ QUIRK_DATA_IGNORE(4) },
			QUIRK_COMPOSITE_END
		}
	}
},

/* Native Instruments MK2 series */
{
	/* Komplete Audio 6 */
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor = 0x17cc,
	.idProduct = 0x1000,
},
{
	/* Traktor Audio 6 */
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor = 0x17cc,
	.idProduct = 0x1010,
},
{
	/* Traktor Audio 10 */
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor = 0x17cc,
	.idProduct = 0x1020,
},

/* QinHeng devices */
{
	USB_DEVICE(0x1a86, 0x752d),
	QUIRK_DRIVER_INFO {
		.vendor_name = "QinHeng",
		.product_name = "CH345",
		.ifnum = 1,
		.type = QUIRK_MIDI_CH345
	}
},

/* KeithMcMillen Stringport */
{ USB_DEVICE(0x1f38, 0x0001) }, /* FIXME: should be more restrictive matching */

/* Miditech devices */
{
	USB_DEVICE(0x4752, 0x0011),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Miditech",
		.product_name = "Midistart-2",
		.ifnum = 0,
		.type = QUIRK_MIDI_CME
	}
},

/* Central Music devices */
{
	/* this ID used by both Miditech MidiStudio-2 and CME UF-x */
	USB_DEVICE(0x7104, 0x2202),
	QUIRK_DRIVER_INFO {
		.ifnum = 0,
		.type = QUIRK_MIDI_CME
	}
},

/* Digidesign Mbox */
{
	/* Thanks to Clemens Ladisch <clemens@ladisch.de> */
	USB_DEVICE(0x0dba, 0x1000),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Digidesign",
		.product_name = "MBox",
		QUIRK_DATA_COMPOSITE{
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3BE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0x4,
					.endpoint = 0x02,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_SYNC,
					.maxpacksize = 0x130,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3BE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0x4,
					.endpoint = 0x81,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.maxpacksize = 0x130,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/* DIGIDESIGN MBOX 2 */
{
	USB_DEVICE(0x0dba, 0x3000),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Digidesign",
		.product_name = "Mbox 2",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3BE,
					.channels = 2,
					.iface = 2,
					.altsetting = 2,
					.altset_idx = 1,
					.attributes = 0x00,
					.endpoint = 0x03,
					.ep_attr = USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			{ QUIRK_DATA_IGNORE(3) },
			{
				QUIRK_DATA_AUDIOFORMAT(4) {
					.formats = SNDRV_PCM_FMTBIT_S24_3BE,
					.channels = 2,
					.iface = 4,
					.altsetting = 2,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x85,
					.ep_attr = USB_ENDPOINT_SYNC_SYNC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			{ QUIRK_DATA_IGNORE(5) },
			{
				QUIRK_DATA_MIDI_MIDIMAN(6) {
					.out_ep =  0x02,
					.out_cables = 0x0001,
					.in_ep = 0x81,
					.in_interval = 0x01,
					.in_cables = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
/* DIGIDESIGN MBOX 3 */
{
	USB_DEVICE(0x0dba, 0x5000),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Digidesign",
		.product_name = "Mbox 3",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_IGNORE(1) },
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.fmt_bits = 24,
					.channels = 4,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0x00,
					.endpoint = USB_RECIP_INTERFACE | USB_DIR_OUT,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
							SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					},
					.sync_ep = USB_RECIP_INTERFACE | USB_DIR_IN,
					.sync_iface = 3,
					.sync_altsetting = 1,
					.sync_ep_idx = 1,
					.implicit_fb = 1,
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(3) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.fmt_bits = 24,
					.channels = 4,
					.iface = 3,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0x00,
					.endpoint = USB_RECIP_INTERFACE | USB_DIR_IN,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.maxpacksize = 0x009c,
					.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
							SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					},
					.implicit_fb = 0,
				}
			},
			{
				QUIRK_DATA_MIDI_FIXED_ENDPOINT(4) {
					.out_cables = 0x0001,
					.in_cables  = 0x0001
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Tascam US122 MKII - playback-only support */
	USB_DEVICE_VENDOR_SPEC(0x0644, 0x8021),
	QUIRK_DRIVER_INFO {
		.vendor_name = "TASCAM",
		.product_name = "US122 MKII",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x02,
					.ep_attr = USB_ENDPOINT_XFER_ISOC,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_88200 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 4,
					.rate_table = (unsigned int[]) {
						44100, 48000, 88200, 96000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/* Denon DN-X1600 */
{
	USB_AUDIO_DEVICE(0x154e, 0x500e),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Denon",
		.product_name = "DN-X1600",
		QUIRK_DATA_COMPOSITE{
			{ QUIRK_DATA_IGNORE(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0x0,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ADAPTIVE,
					.maxpacksize = 0x138,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0x0,
					.endpoint = 0x85,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ADAPTIVE,
					.maxpacksize = 0x138,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			{ QUIRK_DATA_STANDARD_MIDI(4) },
			QUIRK_COMPOSITE_END
		}
	}
},

/* Microsoft XboxLive Headset/Xbox Communicator */
{
	USB_DEVICE(0x045e, 0x0283),
	.bInterfaceClass = USB_CLASS_PER_INTERFACE,
	QUIRK_DRIVER_INFO {
		.vendor_name = "Microsoft",
		.product_name = "XboxLive Headset/Xbox Communicator",
		QUIRK_DATA_COMPOSITE {
			{
				/* playback */
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 1,
					.iface = 0,
					.altsetting = 0,
					.altset_idx = 0,
					.attributes = 0,
					.endpoint = 0x04,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 22050,
					.rate_max = 22050
				}
			},
			{
				/* capture */
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 1,
					.iface = 1,
					.altsetting = 0,
					.altset_idx = 0,
					.attributes = 0,
					.endpoint = 0x85,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 16000,
					.rate_max = 16000
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/* Reloop Play */
{
	USB_DEVICE(0x200c, 0x100b),
	.bInterfaceClass = USB_CLASS_PER_INTERFACE,
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_SYNC_ADAPTIVE,
					.rates = SNDRV_PCM_RATE_44100 |
						 SNDRV_PCM_RATE_48000,
					.rate_min = 44100,
					.rate_max = 48000,
					.nr_rates = 2,
					.rate_table = (unsigned int[]) {
						44100, 48000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * ZOOM R16/24 in audio interface mode.
	 * Playback requires an extra four byte LE length indicator
	 * at the start of each isochronous packet. This quirk is
	 * enabled in create_standard_audio_quirk().
	 */
	USB_DEVICE(0x1686, 0x00dd),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(1) }, /* Playback  */
			{ QUIRK_DATA_STANDARD_AUDIO(2) }, /* Capture */
			{ QUIRK_DATA_STANDARD_MIDI(3) }, /* Midi */
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * Some USB MIDI devices don't have an audio control interface,
	 * so we have to grab MIDI streaming interfaces here.
	 */
	.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS |
		       USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_MIDISTREAMING,
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_STANDARD_MIDI(QUIRK_ANY_INTERFACE)
	}
},

/* Rane SL-1 */
{
	USB_DEVICE(0x13e5, 0x0001),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_STANDARD_AUDIO(QUIRK_ANY_INTERFACE)
        }
},

/* disabled due to regression for other devices;
 * see https://bugzilla.kernel.org/show_bug.cgi?id=199905
 */
#if 0
{
	/*
	 * Nura's first gen headphones use Cambridge Silicon Radio's vendor
	 * ID, but it looks like the product ID actually is only for Nura.
	 * The capture interface does not work at all (even on Windows),
	 * and only the 48 kHz sample rate works for the playback interface.
	 */
	USB_DEVICE(0x0a12, 0x1243),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{ QUIRK_DATA_IGNORE(1) }, /* Capture */
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 2,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_FILL_MAX |
						UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x03,
					.ep_attr = USB_ENDPOINT_XFER_ISOC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
#endif /* disabled */

{
	/*
	 * Bower's & Wilkins PX headphones only support the 48 kHz sample rate
	 * even though it advertises more. The capture interface doesn't work
	 * even on windows.
	 */
	USB_DEVICE(0x19b5, 0x0021),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = UAC_EP_CS_ATTR_FILL_MAX |
						UAC_EP_CS_ATTR_SAMPLE_RATE,
					.endpoint = 0x03,
					.ep_attr = USB_ENDPOINT_XFER_ISOC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
/* MOTU Microbook II */
{
	USB_DEVICE_VENDOR_SPEC(0x07fd, 0x0004),
	QUIRK_DRIVER_INFO {
		.vendor_name = "MOTU",
		.product_name = "MicroBookII",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3BE,
					.channels = 6,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x84,
					.rates = SNDRV_PCM_RATE_96000,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC,
					.rate_min = 96000,
					.rate_max = 96000,
					.nr_rates = 1,
					.maxpacksize = 0x00d8,
					.rate_table = (unsigned int[]) {
						96000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3BE,
					.channels = 8,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x03,
					.ep_idx = 1,
					.rates = SNDRV_PCM_RATE_96000,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC,
					.rate_min = 96000,
					.rate_max = 96000,
					.nr_rates = 1,
					.maxpacksize = 0x0120,
					.rate_table = (unsigned int[]) {
						96000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * PIONEER DJ DDJ-SX3
	 * PCM is 12 channels out, 10 channels in @ 44.1 fixed
	 * interface 0, vendor class alt setting 1 for endpoints 5 and 0x86
	 * The feedback for the output is the input.
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x0023),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 12,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x05,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						   USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 10,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x86,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						 USB_ENDPOINT_SYNC_ASYNC|
						 USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Pioneer DJ DJM-250MK2
	 * PCM is 8 channels out @ 48 fixed (endpoint 0x01)
	 * and 8 channels in @ 48 fixed (endpoint 0x82).
	 *
	 * Both playback and recording is working, even simultaneously.
	 *
	 * Playback channels could be mapped to:
	 *  - CH1
	 *  - CH2
	 *  - AUX
	 *
	 * Recording channels could be mapped to:
	 *  - Post CH1 Fader
	 *  - Post CH2 Fader
	 *  - Cross Fader A
	 *  - Cross Fader B
	 *  - MIC
	 *  - AUX
	 *  - REC OUT
	 *
	 * There is remaining problem with recording directly from PHONO/LINE.
	 * If we map a channel to:
	 *  - CH1 Control Tone PHONO
	 *  - CH1 Control Tone LINE
	 *  - CH2 Control Tone PHONO
	 *  - CH2 Control Tone LINE
	 * it is silent.
	 * There is no signal even on other operating systems with official drivers.
	 * The signal appears only when a supported application is started.
	 * This needs to be investigated yet...
	 * (there is quite a lot communication on the USB in both directions)
	 *
	 * In current version this mixer could be used for playback
	 * and for recording from vinyls (through Post CH* Fader)
	 * but not for DVS (Digital Vinyl Systems) like in Mixxx.
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x0017),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8, // outputs
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 }
					}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8, // inputs
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
						USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * PIONEER DJ DDJ-RB
	 * PCM is 4 channels out, 2 dummy channels in @ 44.1 fixed
	 * The feedback for the output is the dummy input.
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x000e),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						   USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 2,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						 USB_ENDPOINT_SYNC_ASYNC|
						 USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * PIONEER DJ DDJ-RR
	 * PCM is 6 channels out & 4 channels in @ 44.1 fixed
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x000d),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 6, //Master, Headphones & Booth
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						   USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4, //2x RCA inputs (CH1 & CH2)
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						 USB_ENDPOINT_SYNC_ASYNC|
						 USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * PIONEER DJ DDJ-SR2
	 * PCM is 4 channels out, 6 channels in @ 44.1 fixed
	 * The Feedback for the output is the input
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x001e),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 6,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
					USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * Pioneer DJ DJM-900NXS2
	 * 10 channels playback & 12 channels capture @ 44.1/48/96kHz S24LE
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x000a),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 10,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100|
					    SNDRV_PCM_RATE_48000|
					    SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						44100, 48000, 96000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 12,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC|
					    USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100|
					    SNDRV_PCM_RATE_48000|
					    SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						44100, 48000, 96000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * PIONEER DJ DDJ-800
	 * PCM is 6 channels out, 6 channels in @ 44.1 fixed
	 * The Feedback for the output is the input
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x0029),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 6,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 6,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
					USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100,
					.rate_min = 44100,
					.rate_max = 44100,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 44100 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

{
	/*
	 * Pioneer DJ / AlphaTheta DJM-A9
	 * 10 channels playback & 12 channels capture @ 44.1/48/96kHz S24LE
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x003c),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 10,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100|
					    SNDRV_PCM_RATE_48000|
					    SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						44100, 48000, 96000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 12,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC|
					    USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100|
					    SNDRV_PCM_RATE_48000|
					    SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						44100, 48000, 96000
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/*
 * MacroSilicon MS2100/MS2106 based AV capture cards
 *
 * These claim 96kHz 1ch in the descriptors, but are actually 48kHz 2ch.
 * They also need QUIRK_FLAG_ALIGN_TRANSFER, which makes one wonder if
 * they pretend to be 96kHz mono as a workaround for stereo being broken
 * by that...
 *
 * They also have an issue with initial stream alignment that causes the
 * channels to be swapped and out of phase, which is dealt with in quirks.c.
 */
{
	USB_AUDIO_DEVICE(0x534d, 0x0021),
	QUIRK_DRIVER_INFO {
		.vendor_name = "MacroSilicon",
		.product_name = "MS210x",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(2) },
			{
				QUIRK_DATA_AUDIOFORMAT(3) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 2,
					.iface = 3,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x82,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 48000,
					.rate_max = 48000,
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},

/*
 * MacroSilicon MS2109 based HDMI capture cards
 *
 * These claim 96kHz 1ch in the descriptors, but are actually 48kHz 2ch.
 * They also need QUIRK_FLAG_ALIGN_TRANSFER, which makes one wonder if
 * they pretend to be 96kHz mono as a workaround for stereo being broken
 * by that...
 *
 * They also have an issue with initial stream alignment that causes the
 * channels to be swapped and out of phase, which is dealt with in quirks.c.
 */
{
	USB_AUDIO_DEVICE(0x534d, 0x2109),
	QUIRK_DRIVER_INFO {
		.vendor_name = "MacroSilicon",
		.product_name = "MS2109",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_MIXER(2) },
			{
				QUIRK_DATA_AUDIOFORMAT(3) {
					.formats = SNDRV_PCM_FMTBIT_S16_LE,
					.channels = 2,
					.iface = 3,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x82,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 48000,
					.rate_max = 48000,
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Pioneer DJ DJM-750
	 * 8 channels playback & 8 channels capture @ 44.1/48/96kHz S24LE
	 */
	USB_DEVICE_VENDOR_SPEC(0x08e4, 0x017f),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x05,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_44100|
						SNDRV_PCM_RATE_48000|
						SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) { 44100, 48000, 96000 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x86,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
						USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_44100|
						SNDRV_PCM_RATE_48000|
						SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) { 44100, 48000, 96000 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Pioneer DJ DJM-750MK2
	 * 10 channels playback & 12 channels capture @ 48kHz S24LE
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x001b),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 10,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) {
						48000
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 12,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
						USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Pioneer DJ DJM-850
	 * 8 channels playback and 8 channels capture @ 44.1/48/96kHz S24LE
	 * Playback on EP 0x05
	 * Capture on EP 0x86
	 */
	USB_DEVICE_VENDOR_SPEC(0x08e4, 0x0163),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x05,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
					    USB_ENDPOINT_SYNC_ASYNC|
						USB_ENDPOINT_USAGE_DATA,
					.rates = SNDRV_PCM_RATE_44100|
						SNDRV_PCM_RATE_48000|
						SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) { 44100, 48000, 96000 }
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x86,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
						USB_ENDPOINT_USAGE_DATA,
					.rates = SNDRV_PCM_RATE_44100|
						SNDRV_PCM_RATE_48000|
						SNDRV_PCM_RATE_96000,
					.rate_min = 44100,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) { 44100, 48000, 96000 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Pioneer DJ DJM-450
	 * PCM is 8 channels out @ 48 fixed (endpoint 0x01)
	 * and 8 channels in @ 48 fixed (endpoint 0x82).
	 */
	USB_DEVICE_VENDOR_SPEC(0x2b73, 0x0013),
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8, // outputs
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 }
					}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S24_3LE,
					.channels = 8, // inputs
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC|
						USB_ENDPOINT_SYNC_ASYNC|
						USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 }
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Sennheiser GSP670
	 * Change order of interfaces loaded
	 */
	USB_DEVICE(0x1395, 0x0300),
	.bInterfaceClass = USB_CLASS_PER_INTERFACE,
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			// Communication
			{ QUIRK_DATA_STANDARD_AUDIO(3) },
			// Recording
			{ QUIRK_DATA_STANDARD_AUDIO(4) },
			// Main
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Fiero SC-01 (firmware v1.0.0 @ 48 kHz)
	 */
	USB_DEVICE(0x2b53, 0x0023),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Fiero",
		.product_name = "SC-01",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 2,
					.fmt_bits = 24,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 },
					.clock = 0x29
				}
			},
			/* Capture */
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 2,
					.fmt_bits = 24,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC |
						   USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_48000,
					.rate_min = 48000,
					.rate_max = 48000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 48000 },
					.clock = 0x29
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Fiero SC-01 (firmware v1.0.0 @ 96 kHz)
	 */
	USB_DEVICE(0x2b53, 0x0024),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Fiero",
		.product_name = "SC-01",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 2,
					.fmt_bits = 24,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_96000,
					.rate_min = 96000,
					.rate_max = 96000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 96000 },
					.clock = 0x29
				}
			},
			/* Capture */
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 2,
					.fmt_bits = 24,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC |
						   USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_96000,
					.rate_min = 96000,
					.rate_max = 96000,
					.nr_rates = 1,
					.rate_table = (unsigned int[]) { 96000 },
					.clock = 0x29
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/*
	 * Fiero SC-01 (firmware v1.1.0)
	 */
	USB_DEVICE(0x2b53, 0x0031),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Fiero",
		.product_name = "SC-01",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_STANDARD_AUDIO(0) },
			/* Playback */
			{
				QUIRK_DATA_AUDIOFORMAT(1) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 2,
					.fmt_bits = 24,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x01,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 48000,
					.rate_max = 96000,
					.nr_rates = 2,
					.rate_table = (unsigned int[]) { 48000, 96000 },
					.clock = 0x29
				}
			},
			/* Capture */
			{
				QUIRK_DATA_AUDIOFORMAT(2) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 2,
					.fmt_bits = 24,
					.iface = 2,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x82,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						   USB_ENDPOINT_SYNC_ASYNC |
						   USB_ENDPOINT_USAGE_IMPLICIT_FB,
					.rates = SNDRV_PCM_RATE_48000 |
						 SNDRV_PCM_RATE_96000,
					.rate_min = 48000,
					.rate_max = 96000,
					.nr_rates = 2,
					.rate_table = (unsigned int[]) { 48000, 96000 },
					.clock = 0x29
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Advanced modes of the Mythware XA001AU.
	 * For the standard mode, Mythware XA001AU has ID ffad:a001
	 */
	USB_DEVICE_VENDOR_SPEC(0xffad, 0xa001),
	QUIRK_DRIVER_INFO {
		.vendor_name = "Mythware",
		.product_name = "XA001AU",
		QUIRK_DATA_COMPOSITE {
			{ QUIRK_DATA_IGNORE(0) },
			{ QUIRK_DATA_STANDARD_AUDIO(1) },
			{ QUIRK_DATA_STANDARD_AUDIO(2) },
			QUIRK_COMPOSITE_END
		}
	}
},
{
	/* Only claim interface 0 */
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR |
		       USB_DEVICE_ID_MATCH_PRODUCT |
		       USB_DEVICE_ID_MATCH_INT_CLASS |
		       USB_DEVICE_ID_MATCH_INT_NUMBER,
	.idVendor = 0x2a39,
	.idProduct = 0x3f8c,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceNumber = 0,
	QUIRK_DRIVER_INFO {
		QUIRK_DATA_COMPOSITE {
			/*
			 * Three modes depending on sample rate band,
			 * with different channel counts for in/out
			 */
			{ QUIRK_DATA_STANDARD_MIXER(0) },
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 34, // outputs
					.fmt_bits = 24,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x02,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_44100 |
						SNDRV_PCM_RATE_48000,
					.rate_min = 32000,
					.rate_max = 48000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						32000, 44100, 48000,
					},
					.sync_ep = 0x81,
					.sync_iface = 0,
					.sync_altsetting = 1,
					.sync_ep_idx = 0,
					.implicit_fb = 1,
				},
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 18, // outputs
					.fmt_bits = 24,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x02,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_64000 |
						SNDRV_PCM_RATE_88200 |
						SNDRV_PCM_RATE_96000,
					.rate_min = 64000,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						64000, 88200, 96000,
					},
					.sync_ep = 0x81,
					.sync_iface = 0,
					.sync_altsetting = 1,
					.sync_ep_idx = 0,
					.implicit_fb = 1,
				},
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 10, // outputs
					.fmt_bits = 24,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x02,
					.ep_idx = 1,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_KNOT |
						SNDRV_PCM_RATE_176400 |
						SNDRV_PCM_RATE_192000,
					.rate_min = 128000,
					.rate_max = 192000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						128000, 176400, 192000,
					},
					.sync_ep = 0x81,
					.sync_iface = 0,
					.sync_altsetting = 1,
					.sync_ep_idx = 0,
					.implicit_fb = 1,
				},
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 32, // inputs
					.fmt_bits = 24,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x81,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_44100 |
						SNDRV_PCM_RATE_48000,
					.rate_min = 32000,
					.rate_max = 48000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						32000, 44100, 48000,
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 16, // inputs
					.fmt_bits = 24,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x81,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_64000 |
						SNDRV_PCM_RATE_88200 |
						SNDRV_PCM_RATE_96000,
					.rate_min = 64000,
					.rate_max = 96000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						64000, 88200, 96000,
					}
				}
			},
			{
				QUIRK_DATA_AUDIOFORMAT(0) {
					.formats = SNDRV_PCM_FMTBIT_S32_LE,
					.channels = 8, // inputs
					.fmt_bits = 24,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.endpoint = 0x81,
					.ep_attr = USB_ENDPOINT_XFER_ISOC |
						USB_ENDPOINT_SYNC_ASYNC,
					.rates = SNDRV_PCM_RATE_KNOT |
						SNDRV_PCM_RATE_176400 |
						SNDRV_PCM_RATE_192000,
					.rate_min = 128000,
					.rate_max = 192000,
					.nr_rates = 3,
					.rate_table = (unsigned int[]) {
						128000, 176400, 192000,
					}
				}
			},
			QUIRK_COMPOSITE_END
		}
	}
},
#undef USB_DEVICE_VENDOR_SPEC
#undef USB_AUDIO_DEVICE
