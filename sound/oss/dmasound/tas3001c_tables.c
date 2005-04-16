#include "tas_common.h"
#include "tas_eq_prefs.h"

static struct tas_drce_t eqp_0e_2_1_drce = {
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -15.33  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_0e_2_1_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0FCAD3, 0xE06A58, 0x0FCAD3, 0xE06B09, 0x0F9657 } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x041731, 0x082E63, 0x041731, 0xFD8D08, 0x02CFBD } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0FFDC7, 0xE0524C, 0x0FBFAA, 0xE0524C, 0x0FBD72 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0F3D35, 0xE228CA, 0x0EC7B2, 0xE228CA, 0x0E04E8 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x0FCEBF, 0xE181C2, 0x0F2656, 0xE181C2, 0x0EF516 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0EC417, 0x073E22, 0x0B0633, 0x073E22, 0x09CA4A } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0FCAD3, 0xE06A58, 0x0FCAD3, 0xE06B09, 0x0F9657 } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x041731, 0x082E63, 0x041731, 0xFD8D08, 0x02CFBD } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0FFDC7, 0xE0524C, 0x0FBFAA, 0xE0524C, 0x0FBD72 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0F3D35, 0xE228CA, 0x0EC7B2, 0xE228CA, 0x0E04E8 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x0FCEBF, 0xE181C2, 0x0F2656, 0xE181C2, 0x0EF516 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0EC417, 0x073E22, 0x0B0633, 0x073E22, 0x09CA4A } } },
};

static struct tas_eq_pref_t eqp_0e_2_1 = {
  .sample_rate   = 44100,
  .device_id     = 0x0e,
  .output_id     = TAS_OUTPUT_EXTERNAL_SPKR,
  .speaker_id    = 0x01,

  .drce          = &eqp_0e_2_1_drce,

  .filter_count  = 12,
  .biquads       = eqp_0e_2_1_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_10_1_0_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -12.46  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_10_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0F4A12, 0xE16BDA, 0x0F4A12, 0xE173F0, 0x0E9C3A } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x02DD54, 0x05BAA8, 0x02DD54, 0xF8001D, 0x037532 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0E2FC7, 0xE4D5DC, 0x0D7477, 0xE4D5DC, 0x0BA43F } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0E7899, 0xE67CCA, 0x0D0E93, 0xE67CCA, 0x0B872D } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0F4A12, 0xE16BDA, 0x0F4A12, 0xE173F0, 0x0E9C3A } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x02DD54, 0x05BAA8, 0x02DD54, 0xF8001D, 0x037532 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0E2FC7, 0xE4D5DC, 0x0D7477, 0xE4D5DC, 0x0BA43F } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0E7899, 0xE67CCA, 0x0D0E93, 0xE67CCA, 0x0B872D } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 } } },
};

static struct tas_eq_pref_t eqp_10_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x10,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_10_1_0_drce,

  .filter_count  = 12,
  .biquads       = eqp_10_1_0_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_15_2_1_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -15.33  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_15_2_1_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0FE143, 0xE05204, 0x0FCCC5, 0xE05266, 0x0FAE6B } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x102383, 0xE03A03, 0x0FA325, 0xE03A03, 0x0FC6A8 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0FF2AB, 0xE06285, 0x0FB20A, 0xE06285, 0x0FA4B5 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0F544D, 0xE35971, 0x0D8F3A, 0xE35971, 0x0CE388 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x13E1D3, 0xF3ECB5, 0x042227, 0xF3ECB5, 0x0803FA } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0AC119, 0x034181, 0x078AB1, 0x034181, 0x024BCA } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0FE143, 0xE05204, 0x0FCCC5, 0xE05266, 0x0FAE6B } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x102383, 0xE03A03, 0x0FA325, 0xE03A03, 0x0FC6A8 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0FF2AB, 0xE06285, 0x0FB20A, 0xE06285, 0x0FA4B5 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0F544D, 0xE35971, 0x0D8F3A, 0xE35971, 0x0CE388 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x13E1D3, 0xF3ECB5, 0x042227, 0xF3ECB5, 0x0803FA } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0AC119, 0x034181, 0x078AB1, 0x034181, 0x024BCA } } },
};

static struct tas_eq_pref_t eqp_15_2_1 = {
  .sample_rate   = 44100,
  .device_id     = 0x15,
  .output_id     = TAS_OUTPUT_EXTERNAL_SPKR,
  .speaker_id    = 0x01,

  .drce          = &eqp_15_2_1_drce,

  .filter_count  = 12,
  .biquads       = eqp_15_2_1_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_15_1_0_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = 0.0     * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_15_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0FAD08, 0xE0A5EF, 0x0FAD08, 0xE0A79D, 0x0F5BBE } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x04B38D, 0x09671B, 0x04B38D, 0x000F71, 0x02BEC5 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0FDD32, 0xE0A56F, 0x0F8A69, 0xE0A56F, 0x0F679C } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0FD284, 0xE135FB, 0x0F2161, 0xE135FB, 0x0EF3E5 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x0E81B1, 0xE6283F, 0x0CE49D, 0xE6283F, 0x0B664F } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0F2D62, 0xE98797, 0x0D1E19, 0xE98797, 0x0C4B7B } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0FAD08, 0xE0A5EF, 0x0FAD08, 0xE0A79D, 0x0F5BBE } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x04B38D, 0x09671B, 0x04B38D, 0x000F71, 0x02BEC5 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0FDD32, 0xE0A56F, 0x0F8A69, 0xE0A56F, 0x0F679C } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0FD284, 0xE135FB, 0x0F2161, 0xE135FB, 0x0EF3E5 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x0E81B1, 0xE6283F, 0x0CE49D, 0xE6283F, 0x0B664F } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0F2D62, 0xE98797, 0x0D1E19, 0xE98797, 0x0C4B7B } } },
};

static struct tas_eq_pref_t eqp_15_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x15,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_15_1_0_drce,

  .filter_count  = 12,
  .biquads       = eqp_15_1_0_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_0f_2_1_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -15.33  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_0f_2_1_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0FE143, 0xE05204, 0x0FCCC5, 0xE05266, 0x0FAE6B } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x102383, 0xE03A03, 0x0FA325, 0xE03A03, 0x0FC6A8 } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0FF2AB, 0xE06285, 0x0FB20A, 0xE06285, 0x0FA4B5 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0F544D, 0xE35971, 0x0D8F3A, 0xE35971, 0x0CE388 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x13E1D3, 0xF3ECB5, 0x042227, 0xF3ECB5, 0x0803FA } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0AC119, 0x034181, 0x078AB1, 0x034181, 0x024BCA } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0FE143, 0xE05204, 0x0FCCC5, 0xE05266, 0x0FAE6B } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x102383, 0xE03A03, 0x0FA325, 0xE03A03, 0x0FC6A8 } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0FF2AB, 0xE06285, 0x0FB20A, 0xE06285, 0x0FA4B5 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0F544D, 0xE35971, 0x0D8F3A, 0xE35971, 0x0CE388 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x13E1D3, 0xF3ECB5, 0x042227, 0xF3ECB5, 0x0803FA } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0AC119, 0x034181, 0x078AB1, 0x034181, 0x024BCA } } },
};

static struct tas_eq_pref_t eqp_0f_2_1 = {
  .sample_rate   = 44100,
  .device_id     = 0x0f,
  .output_id     = TAS_OUTPUT_EXTERNAL_SPKR,
  .speaker_id    = 0x01,

  .drce          = &eqp_0f_2_1_drce,

  .filter_count  = 12,
  .biquads       = eqp_0f_2_1_biquads
};

/* ======================================================================== */

static struct tas_drce_t eqp_0f_1_0_drce={
  .enable     = 1,
  .above      = { .val = 3.0 * (1<<8), .expand = 0 },
  .below      = { .val = 1.0 * (1<<8), .expand = 0 },
  .threshold  = -15.33  * (1<<8),
  .energy     = 2.4     * (1<<12),
  .attack     = 0.013   * (1<<12),
  .decay      = 0.212   * (1<<12),
};

static struct tas_biquad_ctrl_t eqp_0f_1_0_biquads[]={
  { .channel = 0, .filter = 0, .data = { .coeff = { 0x0FCAD3, 0xE06A58, 0x0FCAD3, 0xE06B09, 0x0F9657 } } },
  { .channel = 0, .filter = 1, .data = { .coeff = { 0x041731, 0x082E63, 0x041731, 0xFD8D08, 0x02CFBD } } },
  { .channel = 0, .filter = 2, .data = { .coeff = { 0x0FFDC7, 0xE0524C, 0x0FBFAA, 0xE0524C, 0x0FBD72 } } },
  { .channel = 0, .filter = 3, .data = { .coeff = { 0x0F3D35, 0xE228CA, 0x0EC7B2, 0xE228CA, 0x0E04E8 } } },
  { .channel = 0, .filter = 4, .data = { .coeff = { 0x0FCEBF, 0xE181C2, 0x0F2656, 0xE181C2, 0x0EF516 } } },
  { .channel = 0, .filter = 5, .data = { .coeff = { 0x0EC417, 0x073E22, 0x0B0633, 0x073E22, 0x09CA4A } } },

  { .channel = 1, .filter = 0, .data = { .coeff = { 0x0FCAD3, 0xE06A58, 0x0FCAD3, 0xE06B09, 0x0F9657 } } },
  { .channel = 1, .filter = 1, .data = { .coeff = { 0x041731, 0x082E63, 0x041731, 0xFD8D08, 0x02CFBD } } },
  { .channel = 1, .filter = 2, .data = { .coeff = { 0x0FFDC7, 0xE0524C, 0x0FBFAA, 0xE0524C, 0x0FBD72 } } },
  { .channel = 1, .filter = 3, .data = { .coeff = { 0x0F3D35, 0xE228CA, 0x0EC7B2, 0xE228CA, 0x0E04E8 } } },
  { .channel = 1, .filter = 4, .data = { .coeff = { 0x0FCEBF, 0xE181C2, 0x0F2656, 0xE181C2, 0x0EF516 } } },
  { .channel = 1, .filter = 5, .data = { .coeff = { 0x0EC417, 0x073E22, 0x0B0633, 0x073E22, 0x09CA4A } } },
};

static struct tas_eq_pref_t eqp_0f_1_0 = {
  .sample_rate   = 44100,
  .device_id     = 0x0f,
  .output_id     = TAS_OUTPUT_INTERNAL_SPKR,
  .speaker_id    = 0x00,

  .drce          = &eqp_0f_1_0_drce,

  .filter_count  = 12,
  .biquads       = eqp_0f_1_0_biquads
};

/* ======================================================================== */

static uint tas3001c_master_tab[]={
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

static uint tas3001c_mixer_tab[]={
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

static uint tas3001c_treble_tab[]={
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
	      0x71,       0x70,       0x6e,       0x6d,
	      0x6d,       0x6c,       0x6b,       0x6a,
	      0x69,       0x68,       0x67,       0x66,
	      0x65,       0x63,       0x62,       0x62,
	      0x60,       0x5f,       0x5d,       0x5c,
	      0x5a,       0x58,       0x56,       0x55,
	      0x53,       0x51,       0x4f,       0x4c,
	      0x4a,       0x48,       0x45,       0x43,
	      0x40,       0x3d,       0x3a,       0x37,
	      0x35,       0x32,       0x2e,       0x2a,
	      0x27,       0x22,       0x1e,       0x1a,
	      0x15,       0x11,        0xc,        0x7,
	       0x1
};

static uint tas3001c_bass_tab[]={
	      0x86,       0x83,       0x81,       0x7f,
	      0x7d,       0x7b,       0x79,       0x78,
	      0x76,       0x75,       0x74,       0x72,
	      0x71,       0x6f,       0x6e,       0x6d,
	      0x6c,       0x6b,       0x69,       0x67,
	      0x65,       0x64,       0x61,       0x60,
	      0x5e,       0x5d,       0x5c,       0x5b,
	      0x5a,       0x59,       0x58,       0x57,
	      0x56,       0x55,       0x55,       0x54,
	      0x53,       0x52,       0x50,       0x4f,
	      0x4d,       0x4c,       0x4b,       0x49,
	      0x47,       0x45,       0x44,       0x42,
	      0x41,       0x3f,       0x3e,       0x3d,
	      0x3c,       0x3b,       0x39,       0x38,
	      0x37,       0x36,       0x35,       0x34,
	      0x33,       0x31,       0x30,       0x2f,
	      0x2e,       0x2c,       0x2b,       0x2b,
	      0x29,       0x28,       0x27,       0x26,
	      0x25,       0x24,       0x22,       0x21,
	      0x20,       0x1e,       0x1c,       0x19,
	      0x18,       0x18,       0x17,       0x16,
	      0x15,       0x14,       0x13,       0x12,
	      0x11,       0x10,        0xf,        0xe,
	       0xd,        0xb,        0xa,        0x9,
	       0x8,        0x6,        0x4,        0x2,
	       0x1
};

struct tas_gain_t tas3001c_gain = {
  .master  = tas3001c_master_tab,
  .treble  = tas3001c_treble_tab,
  .bass    = tas3001c_bass_tab,
  .mixer   = tas3001c_mixer_tab
};

struct tas_eq_pref_t *tas3001c_eq_prefs[]={
  &eqp_0e_2_1,
  &eqp_10_1_0,
  &eqp_15_2_1,
  &eqp_15_1_0,
  &eqp_0f_2_1,
  &eqp_0f_1_0,
  NULL
};
