#include "tas3004.h"
#include "tas_eq_prefs.h"

static struct tas_drce_t eqp_17_1_0_drce={
    .enable     = 1,
    .above      = { .val = 3.0 * (1<<8), .expand = 0 },
    .below      = { .val = 1.0 * (1<<8), .expand = 0 },
    .threshold  = -19.12  * (1<<8),
    .energy     = 2.4     * (1<<12),
    .attack     = 0.013   * (1<<12),
    .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_17_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0fd0d4, 0xe05e56, 0x0fd0d4, 0xe05ee1, 0x0fa234 } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x0910d7, 0x088e1a, 0x030651, 0x01dcb1, 0x02c892 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0ff895, 0xe0970b, 0x0f7f00, 0xe0970b, 0x0f7795 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0fd1c4, 0xe1ac22, 0x0ec8cf, 0xe1ac22, 0x0e9a94 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x0f7c1c, 0xe3cc03, 0x0df786, 0xe3cc03, 0x0d73a2 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x11fb92, 0xf5a1a0, 0x073cd2, 0xf5a1a0, 0x093865 } } },
  { .channel = 0, .filter = 6, .data = { .coeff = { 0x0e17a9, 0x068b6c, 0x08a0e5, 0x068b6c, 0x06b88e } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0fd0d4, 0xe05e56, 0x0fd0d4, 0xe05ee1, 0x0fa234 } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x0910d7, 0x088e1a, 0x030651, 0x01dcb1, 0x02c892 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0ff895, 0xe0970b, 0x0f7f00, 0xe0970b, 0x0f7795 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0fd1c4, 0xe1ac22, 0x0ec8cf, 0xe1ac22, 0x0e9a94 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x0f7c1c, 0xe3cc03, 0x0df786, 0xe3cc03, 0x0d73a2 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x11fb92, 0xf5a1a0, 0x073cd2, 0xf5a1a0, 0x093865 } } },
  { .channel = 1, .filter = 6, .data = { .coeff = { 0x0e17a9, 0x068b6c, 0x08a0e5, 0x068b6c, 0x06b88e } } }
};

static struct tas_eq_pref_t eqp_17_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x17,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_17_1_0_drce,

  .filter_count  = 14,
  .biquads       = eqp_17_1_0_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_18_1_0_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -13.14  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_18_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0f5514, 0xe155d7, 0x0f5514, 0xe15cfa, 0x0eb14b } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x06ec33, 0x02abe3, 0x015eef, 0xf764d9, 0x03922d } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0ef5f2, 0xe67d1f, 0x0bcf37, 0xe67d1f, 0x0ac529 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0db050, 0xe5be4d, 0x0d0c78, 0xe5be4d, 0x0abcc8 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x0f1298, 0xe64ec6, 0x0cc03e, 0xe64ec6, 0x0bd2d7 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0c641a, 0x06537a, 0x08d155, 0x06537a, 0x053570 } } },
  { .channel = 0, .filter = 6, .data = { .coeff = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0f5514, 0xe155d7, 0x0f5514, 0xe15cfa, 0x0eb14b } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x06ec33, 0x02abe3, 0x015eef, 0xf764d9, 0x03922d } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0ef5f2, 0xe67d1f, 0x0bcf37, 0xe67d1f, 0x0ac529 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0db050, 0xe5be4d, 0x0d0c78, 0xe5be4d, 0x0abcc8 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x0f1298, 0xe64ec6, 0x0cc03e, 0xe64ec6, 0x0bd2d7 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0c641a, 0x06537a, 0x08d155, 0x06537a, 0x053570 } } },
  { .channel = 1, .filter = 6, .data = { .coeff = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 } } }
};

static struct tas_eq_pref_t eqp_18_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x18,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_18_1_0_drce,

  .filter_count  = 14,
  .biquads       = eqp_18_1_0_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_1a_1_0_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -10.75  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_1a_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0fb8fd, 0xe08e04, 0x0fb8fd, 0xe08f40, 0x0f7336 } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x06371d, 0x0c6e3a, 0x06371d, 0x05bfd3, 0x031ca2 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0fa1c0, 0xe18692, 0x0f030e, 0xe18692, 0x0ea4ce } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0fe495, 0xe17eff, 0x0f0452, 0xe17eff, 0x0ee8e7 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x100857, 0xe7e71c, 0x0e9599, 0xe7e71c, 0x0e9df1 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0fb26e, 0x06a82c, 0x0db2b4, 0x06a82c, 0x0d6522 } } },
  { .channel = 0, .filter = 6, .data = { .coeff = { 0x11419d, 0xf06cbf, 0x0a4f6e, 0xf06cbf, 0x0b910c } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0fb8fd, 0xe08e04, 0x0fb8fd, 0xe08f40, 0x0f7336 } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x06371d, 0x0c6e3a, 0x06371d, 0x05bfd3, 0x031ca2 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0fa1c0, 0xe18692, 0x0f030e, 0xe18692, 0x0ea4ce } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0fe495, 0xe17eff, 0x0f0452, 0xe17eff, 0x0ee8e7 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x100857, 0xe7e71c, 0x0e9599, 0xe7e71c, 0x0e9df1 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0fb26e, 0x06a82c, 0x0db2b4, 0x06a82c, 0x0d6522 } } },
  { .channel = 1, .filter = 6, .data = { .coeff = { 0x11419d, 0xf06cbf, 0x0a4f6e, 0xf06cbf, 0x0b910c } } }
};

static struct tas_eq_pref_t eqp_1a_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x1a,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_1a_1_0_drce,

  .filter_count  = 14,
  .biquads       = eqp_1a_1_0_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_1c_1_0_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -14.34  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_1c_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0f4f95, 0xe160d4, 0x0f4f95, 0xe1686e, 0x0ea6c5 } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x066b92, 0x0290d4, 0x0148a0, 0xf6853f, 0x03bfc7 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0f57dc, 0xe51c91, 0x0dd1cb, 0xe51c91, 0x0d29a8 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0df1cb, 0xe4fa84, 0x0d7cdc, 0xe4fa84, 0x0b6ea7 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x0eba36, 0xe6aa48, 0x0b9f52, 0xe6aa48, 0x0a5989 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0caf02, 0x05ef9d, 0x084beb, 0x05ef9d, 0x04faee } } },
  { .channel = 0, .filter = 6, .data = { .coeff = { 0x0fc686, 0xe22947, 0x0e4b5d, 0xe22947, 0x0e11e4 } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0f4f95, 0xe160d4, 0x0f4f95, 0xe1686e, 0x0ea6c5 } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x066b92, 0x0290d4, 0x0148a0, 0xf6853f, 0x03bfc7 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0f57dc, 0xe51c91, 0x0dd1cb, 0xe51c91, 0x0d29a8 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0df1cb, 0xe4fa84, 0x0d7cdc, 0xe4fa84, 0x0b6ea7 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x0eba36, 0xe6aa48, 0x0b9f52, 0xe6aa48, 0x0a5989 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0caf02, 0x05ef9d, 0x084beb, 0x05ef9d, 0x04faee } } },
  { .channel = 1, .filter = 6, .data = { .coeff = { 0x0fc686, 0xe22947, 0x0e4b5d, 0xe22947, 0x0e11e4 } } }
};

static struct tas_eq_pref_t eqp_1c_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x1c,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_1c_1_0_drce,

  .filter_count  = 14,
  .biquads       = eqp_1c_1_0_biquads
};

/* ======================================================================== */

static uint tas3004_master_tab[]={
	       0x0,       0x75,       0x9c,       0xbb,
	      0xdb,       0xfb,      0x11e,      0x143,
	     0x16b,      0x196,      0x1c3,      0x1f5,
	     0x229,      0x263,      0x29f,      0x2e1,
	     0x328,      0x373,      0x3c5,      0x41b,
	     0x478,      0x4dc,      0x547,      0x5b8,
	     0x633,      0x6b5,      0x740,      0x7d5,
	     0x873,      0x91c,      0x9d2,      0xa92,
	     0xb5e,      0xc39,      0xd22,      0xe19,
	     0xf20,     0x1037,     0x1161,     0x129e,
	    0x13ed,     0x1551,     0x16ca,     0x185d,
	    0x1a08,     0x1bcc,     0x1dac,     0x1fa7,
	    0x21c1,     0x23fa,     0x2655,     0x28d6,
	    0x2b7c,     0x2e4a,     0x3141,     0x3464,
	    0x37b4,     0x3b35,     0x3ee9,     0x42d3,
	    0x46f6,     0x4b53,     0x4ff0,     0x54ce,
	    0x59f2,     0x5f5f,     0x6519,     0x6b24,
	    0x7183,     0x783c,     0x7f53,     0x86cc,
	    0x8ead,     0x96fa,     0x9fba,     0xa8f2,
	    0xb2a7,     0xbce1,     0xc7a5,     0xd2fa,
	    0xdee8,     0xeb75,     0xf8aa,    0x1068e,
	   0x1152a,    0x12487,    0x134ad,    0x145a5,
	   0x1577b,    0x16a37,    0x17df5,    0x192bd,
	   0x1a890,    0x1bf7b,    0x1d78d,    0x1f0d1,
	   0x20b55,    0x22727,    0x24456,    0x262f2,
	   0x2830b
};

static uint tas3004_mixer_tab[]={
	       0x0,      0x748,      0x9be,      0xbaf,
	     0xda4,      0xfb1,     0x11de,     0x1431,
	    0x16ad,     0x1959,     0x1c37,     0x1f4b,
	    0x2298,     0x2628,     0x29fb,     0x2e12,
	    0x327d,     0x3734,     0x3c47,     0x41b4,
	    0x4787,     0x4dbe,     0x546d,     0x5b86,
	    0x632e,     0x6b52,     0x7400,     0x7d54,
	    0x873b,     0x91c6,     0x9d1a,     0xa920,
	    0xb5e5,     0xc38c,     0xd21b,     0xe18f,
	    0xf1f5,    0x1036a,    0x1160f,    0x129d6,
	   0x13ed0,    0x1550c,    0x16ca0,    0x185c9,
	   0x1a07b,    0x1bcc3,    0x1dab9,    0x1fa75,
	   0x21c0f,    0x23fa3,    0x26552,    0x28d64,
	   0x2b7c9,    0x2e4a2,    0x31411,    0x3463b,
	   0x37b44,    0x3b353,    0x3ee94,    0x42d30,
	   0x46f55,    0x4b533,    0x4fefc,    0x54ce5,
	   0x59f25,    0x5f5f6,    0x65193,    0x6b23c,
	   0x71835,    0x783c3,    0x7f52c,    0x86cc0,
	   0x8eacc,    0x96fa5,    0x9fba0,    0xa8f1a,
	   0xb2a71,    0xbce0a,    0xc7a4a,    0xd2fa0,
	   0xdee7b,    0xeb752,    0xf8a9f,   0x1068e4,
	  0x1152a3,   0x12486a,   0x134ac8,   0x145a55,
	  0x1577ac,   0x16a370,   0x17df51,   0x192bc2,
	  0x1a88f8,   0x1bf7b7,   0x1d78c9,   0x1f0d04,
	  0x20b542,   0x227268,   0x244564,   0x262f26,
	  0x2830af
};

static uint tas3004_treble_tab[]={
	      0x96,       0x95,       0x95,       0x94,
	      0x93,       0x92,       0x92,       0x91,
	      0x90,       0x90,       0x8f,       0x8e,
	      0x8d,       0x8d,       0x8c,       0x8b,
	      0x8a,       0x8a,       0x89,       0x88,
	      0x88,       0x87,       0x86,       0x85,
	      0x85,       0x84,       0x83,       0x83,
	      0x82,       0x81,       0x80,       0x80,
	      0x7f,       0x7e,       0x7e,       0x7d,
	      0x7c,       0x7b,       0x7b,       0x7a,
	      0x79,       0x78,       0x78,       0x77,
	      0x76,       0x76,       0x75,       0x74,
	      0x73,       0x73,       0x72,       0x71,
	      0x71,       0x68,       0x45,       0x5b,
	      0x6d,       0x6c,       0x6b,       0x6a,
	      0x69,       0x68,       0x67,       0x66,
	      0x65,       0x63,       0x62,       0x62,
	      0x60,       0x5e,       0x5c,       0x5b,
	      0x59,       0x57,       0x55,       0x53,
	      0x52,       0x4f,       0x4d,       0x4a,
	      0x48,       0x46,       0x43,       0x40,
	      0x3d,       0x3a,       0x36,       0x33,
	      0x2f,       0x2c,       0x27,       0x23,
	      0x1f,       0x1a,       0x15,        0xf,
	       0x8,        0x5,        0x2,        0x1,
	       0x1
};

static uint tas3004_bass_tab[]={
	      0x96,       0x95,       0x95,       0x94,
	      0x93,       0x92,       0x92,       0x91,
	      0x90,       0x90,       0x8f,       0x8e,
	      0x8d,       0x8d,       0x8c,       0x8b,
	      0x8a,       0x8a,       0x89,       0x88,
	      0x88,       0x87,       0x86,       0x85,
	      0x85,       0x84,       0x83,       0x83,
	      0x82,       0x81,       0x80,       0x80,
	      0x7f,       0x7e,       0x7e,       0x7d,
	      0x7c,       0x7b,       0x7b,       0x7a,
	      0x79,       0x78,       0x78,       0x77,
	      0x76,       0x76,       0x75,       0x74,
	      0x73,       0x73,       0x72,       0x71,
	      0x70,       0x6f,       0x6e,       0x6d,
	      0x6c,       0x6b,       0x6a,       0x6a,
	      0x69,       0x67,       0x66,       0x66,
	      0x65,       0x63,       0x62,       0x62,
	      0x61,       0x60,       0x5e,       0x5d,
	      0x5b,       0x59,       0x57,       0x55,
	      0x53,       0x51,       0x4f,       0x4c,
	      0x4a,       0x48,       0x46,       0x44,
	      0x41,       0x3e,       0x3b,       0x38,
	      0x36,       0x33,       0x2f,       0x2b,
	      0x28,       0x24,       0x20,       0x1c,
	      0x17,       0x12,        0xd,        0x7,
	       0x1
};

struct tas_gain_t tas3004_gain={
  .master  = tas3004_master_tab,
  .treble  = tas3004_treble_tab,
  .bass    = tas3004_bass_tab,
  .mixer   = tas3004_mixer_tab
};

struct tas_eq_pref_t *tas3004_eq_prefs[]={
  &eqp_17_1_0,
  &eqp_18_1_0,
  &eqp_1a_1_0,
  &eqp_1c_1_0,
  NULL
};
