#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-qlm.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include <cvmx.h>
#include <cvmx-qlm.h>
#else
#include "cvmx.h"
#include "cvmx-qlm.h"
#endif
#endif

const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn56xx[] =
{
    {"prbs_error_count",        267, 220},       // BIST/PRBS error count (only valid if pbrs_lock asserted)
    {"prbs_unlock_count",       219, 212},       // BIST/PRBS unlock count (only valid if pbrs_lock asserted)
    {"prbs_locked",             211, 211},       // BIST/PRBS lock (asserted after QLM achieves lock)
    {"reset_prbs",              210, 210},       // BIST/PRBS reset (write 0 to reset)
    {"run_prbs",                209, 209},       // run PRBS test pattern
    {"run_bist",                208, 208},       // run bist (May only work for PCIe ?)
    {"unknown",                 207, 202},       //
    {"biasdrvsel",              201,        199},           //   assign biasdrvsel          = fus_cfg_reg[201:199] ^ jtg_cfg_reg[201:199] ^ ((pi_qlm_cfg == 2'h0) ? 3'h4 : (pi_qlm_cfg == 2'h2) ? 3'h7 : 3'h2);
    {"biasbuffsel",             198,        196},           //   assign biasbuffsel         = fus_cfg_reg[198:196] ^ jtg_cfg_reg[198:196] ^ 3'h4;
    {"tcoeff",                  195,        192},           //   assign tcoeff              = fus_cfg_reg[195:192] ^ jtg_cfg_reg[195:192] ^ (pi_qlm_cfg[1] ? 4'h0 : 4'hc);
    {"mb5000",                  181,        181},           //   assign mb5000              = fus_cfg_reg[181]     ^ jtg_cfg_reg[181]     ^ 1'h0;
    {"interpbw",                180,        176},           //   assign interpbw            = fus_cfg_reg[180:176] ^ jtg_cfg_reg[180:176] ^ ((qlm_spd == 2'h0) ? 5'h1f : (qlm_spd == 2'h1) ? 5'h10 : 5'h0);
    {"mb",                      175,        172},           //   assign mb                  = fus_cfg_reg[175:172] ^ jtg_cfg_reg[175:172] ^ 4'h0;
    {"bwoff",                   171,        160},           //   assign bwoff               = fus_cfg_reg[171:160] ^ jtg_cfg_reg[171:160] ^ 12'h0;
    {"bg_ref_sel",              153,        153},           //   assign bg_ref_sel          = fus_cfg_reg[153]     ^ jtg_cfg_reg[153]     ^ 1'h0;
    {"div2en",                  152,        152},           //   assign div2en              = fus_cfg_reg[152]     ^ jtg_cfg_reg[152]     ^ 1'h0;
    {"trimen",                  151,        150},           //   assign trimen              = fus_cfg_reg[151:150] ^ jtg_cfg_reg[151:150] ^ 2'h0;
    {"clkr",                    149,        144},           //   assign clkr                = fus_cfg_reg[149:144] ^ jtg_cfg_reg[149:144] ^ 6'h0;
    {"clkf",                    143,        132},           //   assign clkf                = fus_cfg_reg[143:132] ^ jtg_cfg_reg[143:132] ^ 12'h18;
    {"bwadj",                   131,        120},           //   assign bwadj               = fus_cfg_reg[131:120] ^ jtg_cfg_reg[131:120] ^ 12'h30;
    {"shlpbck",                 119,        118},           //   assign shlpbck             = fus_cfg_reg[119:118] ^ jtg_cfg_reg[119:118] ^ 2'h0;
    {"serdes_pll_byp",          117,        117},           //   assign serdes_pll_byp      = fus_cfg_reg[117]     ^ jtg_cfg_reg[117]     ^ 1'h0;
    {"ic50dac",                 116,        112},           //   assign ic50dac             = fus_cfg_reg[116:112] ^ jtg_cfg_reg[116:112] ^ 5'h11;
    {"sl_posedge_sample",       111,        111},           //   assign sl_posedge_sample   = fus_cfg_reg[111]     ^ jtg_cfg_reg[111]     ^ 1'h0;
    {"sl_enable",               110,        110},           //   assign sl_enable           = fus_cfg_reg[110]     ^ jtg_cfg_reg[110]     ^ 1'h0;
    {"rx_rout_comp_bypass",     109,        109},           //   assign rx_rout_comp_bypass = fus_cfg_reg[109]     ^ jtg_cfg_reg[109]     ^ 1'h0;
    {"ir50dac",                 108,        104},           //   assign ir50dac             = fus_cfg_reg[108:104] ^ jtg_cfg_reg[108:104] ^ 5'h11;
    {"rx_res_offset",           103,        100},           //   assign rx_res_offset       = fus_cfg_reg[103:100] ^ jtg_cfg_reg[103:100] ^ 4'h2;
    {"rx_rout_comp_value",      99,         96},            //   assign rx_rout_comp_value  = fus_cfg_reg[99:96]   ^ jtg_cfg_reg[99:96]   ^ 4'h7;
    {"tx_rout_comp_value",      95,         92},            //   assign tx_rout_comp_value  = fus_cfg_reg[95:92]   ^ jtg_cfg_reg[95:92]   ^ 4'h7;
    {"tx_res_offset",           91,         88},            //   assign tx_res_offset       = fus_cfg_reg[91:88]   ^ jtg_cfg_reg[91:88]   ^ 4'h1;
    {"tx_rout_comp_bypass",     87,         87},            //   assign tx_rout_comp_bypass = fus_cfg_reg[87]      ^ jtg_cfg_reg[87]      ^ 1'h0;
    {"idle_dac",                86,         84},            //   assign idle_dac            = fus_cfg_reg[86:84]   ^ jtg_cfg_reg[86:84]   ^ 3'h4;
    {"hyst_en",                 83,         83},            //   assign hyst_en             = fus_cfg_reg[83]      ^ jtg_cfg_reg[83]      ^ 1'h1;
    {"rndt",                    82,         82},            //   assign rndt                = fus_cfg_reg[82]      ^ jtg_cfg_reg[82]      ^ 1'h0;
    {"cfg_tx_com",              79,         79},            //   CN52XX cfg_tx_com     = fus_cfg_reg[79] ^ jtg_cfg_reg[79] ^ 1'h0;
    {"cfg_cdr_errcor",          78,         78},            //   CN52XX cfg_cdr_errcor = fus_cfg_reg[78] ^ jtg_cfg_reg[78] ^ 1'h0;
    {"cfg_cdr_secord",          77,         77},            //   CN52XX cfg_cdr_secord = fus_cfg_reg[77] ^ jtg_cfg_reg[77] ^ 1'h1;
    {"cfg_cdr_rotate",          76,         76},            //   assign cfg_cdr_rotate      = fus_cfg_reg[76]      ^ jtg_cfg_reg[76]      ^ 1'h0;
    {"cfg_cdr_rqoffs",          75,         68},            //   assign cfg_cdr_rqoffs      = fus_cfg_reg[75:68]   ^ jtg_cfg_reg[75:68]   ^ 8'h40;
    {"cfg_cdr_incx",            67,         64},            //   assign cfg_cdr_incx        = fus_cfg_reg[67:64]   ^ jtg_cfg_reg[67:64]   ^ 4'h2;
    {"cfg_cdr_state",           63,         56},            //   assign cfg_cdr_state       = fus_cfg_reg[63:56]   ^ jtg_cfg_reg[63:56]   ^ 8'h0;
    {"cfg_cdr_bypass",          55,         55},            //   assign cfg_cdr_bypass      = fus_cfg_reg[55]      ^ jtg_cfg_reg[55]      ^ 1'h0;
    {"cfg_tx_byp",              54,         54},            //   assign cfg_tx_byp          = fus_cfg_reg[54]      ^ jtg_cfg_reg[54]      ^ 1'h0;
    {"cfg_tx_val",              53,         44},            //   assign cfg_tx_val          = fus_cfg_reg[53:44]   ^ jtg_cfg_reg[53:44]   ^ 10'h0;
    {"cfg_rx_pol_set",          43,         43},            //   assign cfg_rx_pol_set      = fus_cfg_reg[43]      ^ jtg_cfg_reg[43]      ^ 1'h0;
    {"cfg_rx_pol_clr",          42,         42},            //   assign cfg_rx_pol_clr      = fus_cfg_reg[42]      ^ jtg_cfg_reg[42]      ^ 1'h0;
    {"cfg_cdr_bw_ctl",          41,         40},            //   assign cfg_cdr_bw_ctl      = fus_cfg_reg[41:40]   ^ jtg_cfg_reg[41:40]   ^ 2'h0;
    {"cfg_rst_n_set",           39,         39},            //   assign cfg_rst_n_set       = fus_cfg_reg[39]      ^ jtg_cfg_reg[39]      ^ 1'h0;
    {"cfg_rst_n_clr",           38,         38},            //   assign cfg_rst_n_clr       = fus_cfg_reg[38]      ^ jtg_cfg_reg[38]      ^ 1'h0;
    {"cfg_tx_clk2",             37,         37},            //   assign cfg_tx_clk2         = fus_cfg_reg[37]      ^ jtg_cfg_reg[37]      ^ 1'h0;
    {"cfg_tx_clk1",             36,         36},            //   assign cfg_tx_clk1         = fus_cfg_reg[36]      ^ jtg_cfg_reg[36]      ^ 1'h0;
    {"cfg_tx_pol_set",          35,         35},            //   assign cfg_tx_pol_set      = fus_cfg_reg[35]      ^ jtg_cfg_reg[35]      ^ 1'h0;
    {"cfg_tx_pol_clr",          34,         34},            //   assign cfg_tx_pol_clr      = fus_cfg_reg[34]      ^ jtg_cfg_reg[34]      ^ 1'h0;
    {"cfg_tx_one",              33,         33},            //   assign cfg_tx_one          = fus_cfg_reg[33]      ^ jtg_cfg_reg[33]      ^ 1'h0;
    {"cfg_tx_zero",             32,         32},            //   assign cfg_tx_zero         = fus_cfg_reg[32]      ^ jtg_cfg_reg[32]      ^ 1'h0;
    {"cfg_rxd_wait",            31,         28},            //   assign cfg_rxd_wait        = fus_cfg_reg[31:28]   ^ jtg_cfg_reg[31:28]   ^ 4'h3;
    {"cfg_rxd_short",           27,         27},            //   assign cfg_rxd_short       = fus_cfg_reg[27]      ^ jtg_cfg_reg[27]      ^ 1'h0;
    {"cfg_rxd_set",             26,         26},            //   assign cfg_rxd_set         = fus_cfg_reg[26]      ^ jtg_cfg_reg[26]      ^ 1'h0;
    {"cfg_rxd_clr",             25,         25},            //   assign cfg_rxd_clr         = fus_cfg_reg[25]      ^ jtg_cfg_reg[25]      ^ 1'h0;
    {"cfg_loopback",            24,         24},            //   assign cfg_loopback        = fus_cfg_reg[24]      ^ jtg_cfg_reg[24]      ^ 1'h0;
    {"cfg_tx_idle_set",         23,         23},            //   assign cfg_tx_idle_set     = fus_cfg_reg[23]      ^ jtg_cfg_reg[23]      ^ 1'h0;
    {"cfg_tx_idle_clr",         22,         22},            //   assign cfg_tx_idle_clr     = fus_cfg_reg[22]      ^ jtg_cfg_reg[22]      ^ 1'h0;
    {"cfg_rx_idle_set",         21,         21},            //   assign cfg_rx_idle_set     = fus_cfg_reg[21]      ^ jtg_cfg_reg[21]      ^ 1'h0;
    {"cfg_rx_idle_clr",         20,         20},            //   assign cfg_rx_idle_clr     = fus_cfg_reg[20]      ^ jtg_cfg_reg[20]      ^ 1'h0;
    {"cfg_rx_idle_thr",         19,         16},            //   assign cfg_rx_idle_thr     = fus_cfg_reg[19:16]   ^ jtg_cfg_reg[19:16]   ^ 4'h0;
    {"cfg_com_thr",             15,         12},            //   assign cfg_com_thr         = fus_cfg_reg[15:12]   ^ jtg_cfg_reg[15:12]   ^ 4'h3;
    {"cfg_rx_offset",           11,         8},             //   assign cfg_rx_offset       = fus_cfg_reg[11:8]    ^ jtg_cfg_reg[11:8]    ^ 4'h4;
    {"cfg_skp_max",             7,          4},             //   assign cfg_skp_max         = fus_cfg_reg[7:4]     ^ jtg_cfg_reg[7:4]     ^ 4'hc;
    {"cfg_skp_min",             3,          0},             //   assign cfg_skp_min         = fus_cfg_reg[3:0]     ^ jtg_cfg_reg[3:0]     ^ 4'h4;
    {NULL,                      -1,         -1}
};

const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn52xx[] =
{
    {"prbs_error_count",        267, 220},       // BIST/PRBS error count (only valid if pbrs_lock asserted)
    {"prbs_unlock_count",       219, 212},       // BIST/PRBS unlock count (only valid if pbrs_lock asserted)
    {"prbs_locked",             211, 211},       // BIST/PRBS lock (asserted after QLM achieves lock)
    {"reset_prbs",              210, 210},       // BIST/PRBS reset (write 0 to reset)
    {"run_prbs",                209, 209},       // run PRBS test pattern
    {"run_bist",                208, 208},       // run bist (May only work for PCIe ?)
    {"unknown",                 207, 202},       //

    {"biasdrvsel",              201,        199},           //   assign biasdrvsel          = fus_cfg_reg[201:199] ^ jtg_cfg_reg[201:199] ^ ((pi_qlm_cfg == 2'h0) ? 3'h4 : (pi_qlm_cfg == 2'h2) ? 3'h7 : 3'h2);
    {"biasbuffsel",             198,        196},           //   assign biasbuffsel         = fus_cfg_reg[198:196] ^ jtg_cfg_reg[198:196] ^ 3'h4;
    {"tcoeff",                  195,        192},           //   assign tcoeff              = fus_cfg_reg[195:192] ^ jtg_cfg_reg[195:192] ^ (pi_qlm_cfg[1] ? 4'h0 : 4'hc);
    {"mb5000",                  181,        181},           //   assign mb5000              = fus_cfg_reg[181]     ^ jtg_cfg_reg[181]     ^ 1'h0;
    {"interpbw",                180,        176},           //   assign interpbw            = fus_cfg_reg[180:176] ^ jtg_cfg_reg[180:176] ^ ((qlm_spd == 2'h0) ? 5'h1f : (qlm_spd == 2'h1) ? 5'h10 : 5'h0);
    {"mb",                      175,        172},           //   assign mb                  = fus_cfg_reg[175:172] ^ jtg_cfg_reg[175:172] ^ 4'h0;
    {"bwoff",                   171,        160},           //   assign bwoff               = fus_cfg_reg[171:160] ^ jtg_cfg_reg[171:160] ^ 12'h0;
    {"bg_ref_sel",              153,        153},           //   assign bg_ref_sel          = fus_cfg_reg[153]     ^ jtg_cfg_reg[153]     ^ 1'h0;
    {"div2en",                  152,        152},           //   assign div2en              = fus_cfg_reg[152]     ^ jtg_cfg_reg[152]     ^ 1'h0;
    {"trimen",                  151,        150},           //   assign trimen              = fus_cfg_reg[151:150] ^ jtg_cfg_reg[151:150] ^ 2'h0;
    {"clkr",                    149,        144},           //   assign clkr                = fus_cfg_reg[149:144] ^ jtg_cfg_reg[149:144] ^ 6'h0;
    {"clkf",                    143,        132},           //   assign clkf                = fus_cfg_reg[143:132] ^ jtg_cfg_reg[143:132] ^ 12'h18;
    {"bwadj",                   131,        120},           //   assign bwadj               = fus_cfg_reg[131:120] ^ jtg_cfg_reg[131:120] ^ 12'h30;
    {"shlpbck",                 119,        118},           //   assign shlpbck             = fus_cfg_reg[119:118] ^ jtg_cfg_reg[119:118] ^ 2'h0;
    {"serdes_pll_byp",          117,        117},           //   assign serdes_pll_byp      = fus_cfg_reg[117]     ^ jtg_cfg_reg[117]     ^ 1'h0;
    {"ic50dac",                 116,        112},           //   assign ic50dac             = fus_cfg_reg[116:112] ^ jtg_cfg_reg[116:112] ^ 5'h11;
    {"sl_posedge_sample",       111,        111},           //   assign sl_posedge_sample   = fus_cfg_reg[111]     ^ jtg_cfg_reg[111]     ^ 1'h0;
    {"sl_enable",               110,        110},           //   assign sl_enable           = fus_cfg_reg[110]     ^ jtg_cfg_reg[110]     ^ 1'h0;
    {"rx_rout_comp_bypass",     109,        109},           //   assign rx_rout_comp_bypass = fus_cfg_reg[109]     ^ jtg_cfg_reg[109]     ^ 1'h0;
    {"ir50dac",                 108,        104},           //   assign ir50dac             = fus_cfg_reg[108:104] ^ jtg_cfg_reg[108:104] ^ 5'h11;
    {"rx_res_offset",           103,        100},           //   assign rx_res_offset       = fus_cfg_reg[103:100] ^ jtg_cfg_reg[103:100] ^ 4'h2;
    {"rx_rout_comp_value",      99,         96},            //   assign rx_rout_comp_value  = fus_cfg_reg[99:96]   ^ jtg_cfg_reg[99:96]   ^ 4'h7;
    {"tx_rout_comp_value",      95,         92},            //   assign tx_rout_comp_value  = fus_cfg_reg[95:92]   ^ jtg_cfg_reg[95:92]   ^ 4'h7;
    {"tx_res_offset",           91,         88},            //   assign tx_res_offset       = fus_cfg_reg[91:88]   ^ jtg_cfg_reg[91:88]   ^ 4'h1;
    {"tx_rout_comp_bypass",     87,         87},            //   assign tx_rout_comp_bypass = fus_cfg_reg[87]      ^ jtg_cfg_reg[87]      ^ 1'h0;
    {"idle_dac",                86,         84},            //   assign idle_dac            = fus_cfg_reg[86:84]   ^ jtg_cfg_reg[86:84]   ^ 3'h4;
    {"hyst_en",                 83,         83},            //   assign hyst_en             = fus_cfg_reg[83]      ^ jtg_cfg_reg[83]      ^ 1'h1;
    {"rndt",                    82,         82},            //   assign rndt                = fus_cfg_reg[82]      ^ jtg_cfg_reg[82]      ^ 1'h0;
    {"cfg_tx_com",              79,         79},            //   CN52XX cfg_tx_com     = fus_cfg_reg[79] ^ jtg_cfg_reg[79] ^ 1'h0;
    {"cfg_cdr_errcor",          78,         78},            //   CN52XX cfg_cdr_errcor = fus_cfg_reg[78] ^ jtg_cfg_reg[78] ^ 1'h0;
    {"cfg_cdr_secord",          77,         77},            //   CN52XX cfg_cdr_secord = fus_cfg_reg[77] ^ jtg_cfg_reg[77] ^ 1'h1;
    {"cfg_cdr_rotate",          76,         76},            //   assign cfg_cdr_rotate      = fus_cfg_reg[76]      ^ jtg_cfg_reg[76]      ^ 1'h0;
    {"cfg_cdr_rqoffs",          75,         68},            //   assign cfg_cdr_rqoffs      = fus_cfg_reg[75:68]   ^ jtg_cfg_reg[75:68]   ^ 8'h40;
    {"cfg_cdr_incx",            67,         64},            //   assign cfg_cdr_incx        = fus_cfg_reg[67:64]   ^ jtg_cfg_reg[67:64]   ^ 4'h2;
    {"cfg_cdr_state",           63,         56},            //   assign cfg_cdr_state       = fus_cfg_reg[63:56]   ^ jtg_cfg_reg[63:56]   ^ 8'h0;
    {"cfg_cdr_bypass",          55,         55},            //   assign cfg_cdr_bypass      = fus_cfg_reg[55]      ^ jtg_cfg_reg[55]      ^ 1'h0;
    {"cfg_tx_byp",              54,         54},            //   assign cfg_tx_byp          = fus_cfg_reg[54]      ^ jtg_cfg_reg[54]      ^ 1'h0;
    {"cfg_tx_val",              53,         44},            //   assign cfg_tx_val          = fus_cfg_reg[53:44]   ^ jtg_cfg_reg[53:44]   ^ 10'h0;
    {"cfg_rx_pol_set",          43,         43},            //   assign cfg_rx_pol_set      = fus_cfg_reg[43]      ^ jtg_cfg_reg[43]      ^ 1'h0;
    {"cfg_rx_pol_clr",          42,         42},            //   assign cfg_rx_pol_clr      = fus_cfg_reg[42]      ^ jtg_cfg_reg[42]      ^ 1'h0;
    {"cfg_cdr_bw_ctl",          41,         40},            //   assign cfg_cdr_bw_ctl      = fus_cfg_reg[41:40]   ^ jtg_cfg_reg[41:40]   ^ 2'h0;
    {"cfg_rst_n_set",           39,         39},            //   assign cfg_rst_n_set       = fus_cfg_reg[39]      ^ jtg_cfg_reg[39]      ^ 1'h0;
    {"cfg_rst_n_clr",           38,         38},            //   assign cfg_rst_n_clr       = fus_cfg_reg[38]      ^ jtg_cfg_reg[38]      ^ 1'h0;
    {"cfg_tx_clk2",             37,         37},            //   assign cfg_tx_clk2         = fus_cfg_reg[37]      ^ jtg_cfg_reg[37]      ^ 1'h0;
    {"cfg_tx_clk1",             36,         36},            //   assign cfg_tx_clk1         = fus_cfg_reg[36]      ^ jtg_cfg_reg[36]      ^ 1'h0;
    {"cfg_tx_pol_set",          35,         35},            //   assign cfg_tx_pol_set      = fus_cfg_reg[35]      ^ jtg_cfg_reg[35]      ^ 1'h0;
    {"cfg_tx_pol_clr",          34,         34},            //   assign cfg_tx_pol_clr      = fus_cfg_reg[34]      ^ jtg_cfg_reg[34]      ^ 1'h0;
    {"cfg_tx_one",              33,         33},            //   assign cfg_tx_one          = fus_cfg_reg[33]      ^ jtg_cfg_reg[33]      ^ 1'h0;
    {"cfg_tx_zero",             32,         32},            //   assign cfg_tx_zero         = fus_cfg_reg[32]      ^ jtg_cfg_reg[32]      ^ 1'h0;
    {"cfg_rxd_wait",            31,         28},            //   assign cfg_rxd_wait        = fus_cfg_reg[31:28]   ^ jtg_cfg_reg[31:28]   ^ 4'h3;
    {"cfg_rxd_short",           27,         27},            //   assign cfg_rxd_short       = fus_cfg_reg[27]      ^ jtg_cfg_reg[27]      ^ 1'h0;
    {"cfg_rxd_set",             26,         26},            //   assign cfg_rxd_set         = fus_cfg_reg[26]      ^ jtg_cfg_reg[26]      ^ 1'h0;
    {"cfg_rxd_clr",             25,         25},            //   assign cfg_rxd_clr         = fus_cfg_reg[25]      ^ jtg_cfg_reg[25]      ^ 1'h0;
    {"cfg_loopback",            24,         24},            //   assign cfg_loopback        = fus_cfg_reg[24]      ^ jtg_cfg_reg[24]      ^ 1'h0;
    {"cfg_tx_idle_set",         23,         23},            //   assign cfg_tx_idle_set     = fus_cfg_reg[23]      ^ jtg_cfg_reg[23]      ^ 1'h0;
    {"cfg_tx_idle_clr",         22,         22},            //   assign cfg_tx_idle_clr     = fus_cfg_reg[22]      ^ jtg_cfg_reg[22]      ^ 1'h0;
    {"cfg_rx_idle_set",         21,         21},            //   assign cfg_rx_idle_set     = fus_cfg_reg[21]      ^ jtg_cfg_reg[21]      ^ 1'h0;
    {"cfg_rx_idle_clr",         20,         20},            //   assign cfg_rx_idle_clr     = fus_cfg_reg[20]      ^ jtg_cfg_reg[20]      ^ 1'h0;
    {"cfg_rx_idle_thr",         19,         16},            //   assign cfg_rx_idle_thr     = fus_cfg_reg[19:16]   ^ jtg_cfg_reg[19:16]   ^ 4'h0;
    {"cfg_com_thr",             15,         12},            //   assign cfg_com_thr         = fus_cfg_reg[15:12]   ^ jtg_cfg_reg[15:12]   ^ 4'h3;
    {"cfg_rx_offset",           11,         8},             //   assign cfg_rx_offset       = fus_cfg_reg[11:8]    ^ jtg_cfg_reg[11:8]    ^ 4'h4;
    {"cfg_skp_max",             7,          4},             //   assign cfg_skp_max         = fus_cfg_reg[7:4]     ^ jtg_cfg_reg[7:4]     ^ 4'hc;
    {"cfg_skp_min",             3,          0},             //   assign cfg_skp_min         = fus_cfg_reg[3:0]     ^ jtg_cfg_reg[3:0]     ^ 4'h4;
    {NULL,                      -1,         -1}
};


const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn63xx[] =
{
    {"prbs_err_cnt",        299, 252},  // prbs_err_cnt[47..0]
    {"prbs_lock",           251, 251},  // prbs_lock
    {"jtg_prbs_rst_n",      250, 250},  // jtg_prbs_rst_n
    {"jtg_run_prbs31",      249, 249},  // jtg_run_prbs31
    {"jtg_run_prbs7",       248, 248},  // jtg_run_prbs7
    {"Unused1",             247, 245},  // 0
    {"cfg_pwrup_set",       244, 244},  // cfg_pwrup_set
    {"cfg_pwrup_clr",       243, 243},  // cfg_pwrup_clr
    {"cfg_rst_n_set",       242, 242},  // cfg_rst_n_set
    {"cfg_rst_n_clr",       241, 241},  // cfg_rst_n_clr
    {"cfg_tx_idle_set",     240, 240},  // cfg_tx_idle_set
    {"cfg_tx_idle_clr",     239, 239},  // cfg_tx_idle_clr
    {"cfg_tx_byp",          238, 238},  // cfg_tx_byp
    {"cfg_tx_byp_inv",      237, 237},  // cfg_tx_byp_inv
    {"cfg_tx_byp_val",      236, 227},  // cfg_tx_byp_val[9..0]
    {"cfg_loopback",        226, 226},  // cfg_loopback
    {"shlpbck",             225, 224},  // shlpbck[1..0]
    {"sl_enable",           223, 223},  // sl_enable
    {"sl_posedge_sample",   222, 222},  // sl_posedge_sample
    {"trimen",              221, 220},  // trimen[1..0]
    {"serdes_tx_byp",       219, 219},  // serdes_tx_byp
    {"serdes_pll_byp",      218, 218},  // serdes_pll_byp
    {"lowf_byp",            217, 217},  // lowf_byp
    {"spdsel_byp",          216, 216},  // spdsel_byp
    {"div4_byp",            215, 215},  // div4_byp
    {"clkf_byp",            214, 208},  // clkf_byp[6..0]
    {"Unused2",             207, 206},  // 0
    {"biasdrv_hs_ls_byp",   205, 201},  // biasdrv_hs_ls_byp[4..0]
    {"tcoeff_hf_ls_byp",    200, 197},  // tcoeff_hf_ls_byp[3..0]
    {"biasdrv_hf_byp",      196, 192},  // biasdrv_hf_byp[4..0]
    {"tcoeff_hf_byp",       191, 188},  // tcoeff_hf_byp[3..0]
    {"Unused3",             187, 186},  // 0
    {"biasdrv_lf_ls_byp",   185, 181},  // biasdrv_lf_ls_byp[4..0]
    {"tcoeff_lf_ls_byp",    180, 177},  // tcoeff_lf_ls_byp[3..0]
    {"biasdrv_lf_byp",      176, 172},  // biasdrv_lf_byp[4..0]
    {"tcoeff_lf_byp",       171, 168},  // tcoeff_lf_byp[3..0]
    {"Unused4",             167, 167},  // 0
    {"interpbw",            166, 162},  // interpbw[4..0]
    {"pll_cpb",             161, 159},  // pll_cpb[2..0]
    {"pll_cps",             158, 156},  // pll_cps[2..0]
    {"pll_diffamp",         155, 152},  // pll_diffamp[3..0]
    {"Unused5",             151, 150},  // 0
    {"cfg_rx_idle_set",     149, 149},  // cfg_rx_idle_set
    {"cfg_rx_idle_clr",     148, 148},  // cfg_rx_idle_clr
    {"cfg_rx_idle_thr",     147, 144},  // cfg_rx_idle_thr[3..0]
    {"cfg_com_thr",         143, 140},  // cfg_com_thr[3..0]
    {"cfg_rx_offset",       139, 136},  // cfg_rx_offset[3..0]
    {"cfg_skp_max",         135, 132},  // cfg_skp_max[3..0]
    {"cfg_skp_min",         131, 128},  // cfg_skp_min[3..0]
    {"cfg_fast_pwrup",      127, 127},  // cfg_fast_pwrup
    {"Unused6",             126, 100},  // 0
    {"detected_n",           99,  99},  // detected_n
    {"detected_p",           98,  98},  // detected_p
    {"dbg_res_rx",           97,  94},  // dbg_res_rx[3..0]
    {"dbg_res_tx",           93,  90},  // dbg_res_tx[3..0]
    {"cfg_tx_pol_set",       89,  89},  // cfg_tx_pol_set
    {"cfg_tx_pol_clr",       88,  88},  // cfg_tx_pol_clr
    {"cfg_rx_pol_set",       87,  87},  // cfg_rx_pol_set
    {"cfg_rx_pol_clr",       86,  86},  // cfg_rx_pol_clr
    {"cfg_rxd_set",          85,  85},  // cfg_rxd_set
    {"cfg_rxd_clr",          84,  84},  // cfg_rxd_clr
    {"cfg_rxd_wait",         83,  80},  // cfg_rxd_wait[3..0]
    {"cfg_cdr_limit",        79,  79},  // cfg_cdr_limit
    {"cfg_cdr_rotate",       78,  78},  // cfg_cdr_rotate
    {"cfg_cdr_bw_ctl",       77,  76},  // cfg_cdr_bw_ctl[1..0]
    {"cfg_cdr_trunc",        75,  74},  // cfg_cdr_trunc[1..0]
    {"cfg_cdr_rqoffs",       73,  64},  // cfg_cdr_rqoffs[9..0]
    {"cfg_cdr_inc2",         63,  58},  // cfg_cdr_inc2[5..0]
    {"cfg_cdr_inc1",         57,  52},  // cfg_cdr_inc1[5..0]
    {"fusopt_voter_sync",    51,  51},  // fusopt_voter_sync
    {"rndt",                 50,  50},  // rndt
    {"hcya",                 49,  49},  // hcya
    {"hyst",                 48,  48},  // hyst
    {"idle_dac",             47,  45},  // idle_dac[2..0]
    {"bg_ref_sel",           44,  44},  // bg_ref_sel
    {"ic50dac",              43,  39},  // ic50dac[4..0]
    {"ir50dac",              38,  34},  // ir50dac[4..0]
    {"tx_rout_comp_bypass",  33,  33},  // tx_rout_comp_bypass
    {"tx_rout_comp_value",   32,  29},  // tx_rout_comp_value[3..0]
    {"tx_res_offset",        28,  25},  // tx_res_offset[3..0]
    {"rx_rout_comp_bypass",  24,  24},  // rx_rout_comp_bypass
    {"rx_rout_comp_value",   23,  20},  // rx_rout_comp_value[3..0]
    {"rx_res_offset",        19,  16},  // rx_res_offset[3..0]
    {"rx_cap_gen2",          15,  12},  // rx_cap_gen2[3..0]
    {"rx_eq_gen2",           11,   8},  // rx_eq_gen2[3..0]
    {"rx_cap_gen1",           7,   4},  // rx_cap_gen1[3..0]
    {"rx_eq_gen1",            3,   0},  // rx_eq_gen1[3..0]
    {NULL, -1, -1}
};

const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn66xx[] =
{
    {"prbs_err_cnt",        303, 256},  // prbs_err_cnt[47..0]
    {"prbs_lock",           255, 255},  // prbs_lock
    {"jtg_prbs_rx_rst_n",   254, 254},  // jtg_prbs_rx_rst_n
    {"jtg_prbs_tx_rst_n",   253, 253},  // jtg_prbs_tx_rst_n
    {"jtg_prbs_mode",       252, 251},  // jtg_prbs_mode[252:251]
    {"jtg_prbs_rst_n",      250, 250},  // jtg_prbs_rst_n
    {"jtg_run_prbs31",      249, 249},  // jtg_run_prbs31 - Use jtg_prbs_mode instead
    {"jtg_run_prbs7",       248, 248},  // jtg_run_prbs7 - Use jtg_prbs_mode instead
    {"Unused1",             247, 246},  // 0
    {"div5_byp",            245, 245},  // div5_byp
    {"cfg_pwrup_set",       244, 244},  // cfg_pwrup_set
    {"cfg_pwrup_clr",       243, 243},  // cfg_pwrup_clr
    {"cfg_rst_n_set",       242, 242},  // cfg_rst_n_set
    {"cfg_rst_n_clr",       241, 241},  // cfg_rst_n_clr
    {"cfg_tx_idle_set",     240, 240},  // cfg_tx_idle_set
    {"cfg_tx_idle_clr",     239, 239},  // cfg_tx_idle_clr
    {"cfg_tx_byp",          238, 238},  // cfg_tx_byp
    {"cfg_tx_byp_inv",      237, 237},  // cfg_tx_byp_inv
    {"cfg_tx_byp_val",      236, 227},  // cfg_tx_byp_val[9..0]
    {"cfg_loopback",        226, 226},  // cfg_loopback
    {"shlpbck",             225, 224},  // shlpbck[1..0]
    {"sl_enable",           223, 223},  // sl_enable
    {"sl_posedge_sample",   222, 222},  // sl_posedge_sample
    {"trimen",              221, 220},  // trimen[1..0]
    {"serdes_tx_byp",       219, 219},  // serdes_tx_byp
    {"serdes_pll_byp",      218, 218},  // serdes_pll_byp
    {"lowf_byp",            217, 217},  // lowf_byp
    {"spdsel_byp",          216, 216},  // spdsel_byp
    {"div4_byp",            215, 215},  // div4_byp
    {"clkf_byp",            214, 208},  // clkf_byp[6..0]
    {"biasdrv_hs_ls_byp",   207, 203},  // biasdrv_hs_ls_byp[4..0]
    {"tcoeff_hf_ls_byp",    202, 198},  // tcoeff_hf_ls_byp[4..0]
    {"biasdrv_hf_byp",      197, 193},  // biasdrv_hf_byp[4..0]
    {"tcoeff_hf_byp",       192, 188},  // tcoeff_hf_byp[4..0]
    {"biasdrv_lf_ls_byp",   187, 183},  // biasdrv_lf_ls_byp[4..0]
    {"tcoeff_lf_ls_byp",    182, 178},  // tcoeff_lf_ls_byp[4..0]
    {"biasdrv_lf_byp",      177, 173},  // biasdrv_lf_byp[4..0]
    {"tcoeff_lf_byp",       172, 168},  // tcoeff_lf_byp[4..0]
    {"Unused4",             167, 167},  // 0
    {"interpbw",            166, 162},  // interpbw[4..0]
    {"pll_cpb",             161, 159},  // pll_cpb[2..0]
    {"pll_cps",             158, 156},  // pll_cps[2..0]
    {"pll_diffamp",         155, 152},  // pll_diffamp[3..0]
    {"cfg_err_thr",         151, 150},  // cfg_err_thr
    {"cfg_rx_idle_set",     149, 149},  // cfg_rx_idle_set
    {"cfg_rx_idle_clr",     148, 148},  // cfg_rx_idle_clr
    {"cfg_rx_idle_thr",     147, 144},  // cfg_rx_idle_thr[3..0]
    {"cfg_com_thr",         143, 140},  // cfg_com_thr[3..0]
    {"cfg_rx_offset",       139, 136},  // cfg_rx_offset[3..0]
    {"cfg_skp_max",         135, 132},  // cfg_skp_max[3..0]
    {"cfg_skp_min",         131, 128},  // cfg_skp_min[3..0]
    {"cfg_fast_pwrup",      127, 127},  // cfg_fast_pwrup
    {"Unused6",             126, 101},  // 0
    {"cfg_indep_dis",       100, 100},  // cfg_indep_dis
    {"detected_n",           99,  99},  // detected_n
    {"detected_p",           98,  98},  // detected_p
    {"dbg_res_rx",           97,  94},  // dbg_res_rx[3..0]
    {"dbg_res_tx",           93,  90},  // dbg_res_tx[3..0]
    {"cfg_tx_pol_set",       89,  89},  // cfg_tx_pol_set
    {"cfg_tx_pol_clr",       88,  88},  // cfg_tx_pol_clr
    {"cfg_rx_pol_set",       87,  87},  // cfg_rx_pol_set
    {"cfg_rx_pol_clr",       86,  86},  // cfg_rx_pol_clr
    {"cfg_rxd_set",          85,  85},  // cfg_rxd_set
    {"cfg_rxd_clr",          84,  84},  // cfg_rxd_clr
    {"cfg_rxd_wait",         83,  80},  // cfg_rxd_wait[3..0]
    {"cfg_cdr_limit",        79,  79},  // cfg_cdr_limit
    {"cfg_cdr_rotate",       78,  78},  // cfg_cdr_rotate
    {"cfg_cdr_bw_ctl",       77,  76},  // cfg_cdr_bw_ctl[1..0]
    {"cfg_cdr_trunc",        75,  74},  // cfg_cdr_trunc[1..0]
    {"cfg_cdr_rqoffs",       73,  64},  // cfg_cdr_rqoffs[9..0]
    {"cfg_cdr_inc2",         63,  58},  // cfg_cdr_inc2[5..0]
    {"cfg_cdr_inc1",         57,  52},  // cfg_cdr_inc1[5..0]
    {"fusopt_voter_sync",    51,  51},  // fusopt_voter_sync
    {"rndt",                 50,  50},  // rndt
    {"hcya",                 49,  49},  // hcya
    {"hyst",                 48,  48},  // hyst
    {"idle_dac",             47,  45},  // idle_dac[2..0]
    {"bg_ref_sel",           44,  44},  // bg_ref_sel
    {"ic50dac",              43,  39},  // ic50dac[4..0]
    {"ir50dac",              38,  34},  // ir50dac[4..0]
    {"tx_rout_comp_bypass",  33,  33},  // tx_rout_comp_bypass
    {"tx_rout_comp_value",   32,  29},  // tx_rout_comp_value[3..0]
    {"tx_res_offset",        28,  25},  // tx_res_offset[3..0]
    {"rx_rout_comp_bypass",  24,  24},  // rx_rout_comp_bypass
    {"rx_rout_comp_value",   23,  20},  // rx_rout_comp_value[3..0]
    {"rx_res_offset",        19,  16},  // rx_res_offset[3..0]
    {"rx_cap_gen2",          15,  12},  // rx_cap_gen2[3..0]
    {"rx_eq_gen2",           11,   8},  // rx_eq_gen2[3..0]
    {"rx_cap_gen1",           7,   4},  // rx_cap_gen1[3..0]
    {"rx_eq_gen1",            3,   0},  // rx_eq_gen1[3..0]
    {NULL, -1, -1}
};

const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn68xx[] =
{
    {"prbs_err_cnt",        303, 256},  // prbs_err_cnt[47..0]
    {"prbs_lock",           255, 255},  // prbs_lock
    {"jtg_prbs_rx_rst_n",   254, 254},  // jtg_prbs_rx_rst_n
    {"jtg_prbs_tx_rst_n",   253, 253},  // jtg_prbs_tx_rst_n
    {"jtg_prbs_mode",       252, 251},  // jtg_prbs_mode[252:251]
    {"jtg_prbs_rst_n",      250, 250},  // jtg_prbs_rst_n
    {"jtg_run_prbs31",      249, 249},  // jtg_run_prbs31 - Use jtg_prbs_mode instead
    {"jtg_run_prbs7",       248, 248},  // jtg_run_prbs7 - Use jtg_prbs_mode instead
    {"Unused1",             247, 245},  // 0
    {"cfg_pwrup_set",       244, 244},  // cfg_pwrup_set
    {"cfg_pwrup_clr",       243, 243},  // cfg_pwrup_clr
    {"cfg_rst_n_set",       242, 242},  // cfg_rst_n_set
    {"cfg_rst_n_clr",       241, 241},  // cfg_rst_n_clr
    {"cfg_tx_idle_set",     240, 240},  // cfg_tx_idle_set
    {"cfg_tx_idle_clr",     239, 239},  // cfg_tx_idle_clr
    {"cfg_tx_byp",          238, 238},  // cfg_tx_byp
    {"cfg_tx_byp_inv",      237, 237},  // cfg_tx_byp_inv
    {"cfg_tx_byp_val",      236, 227},  // cfg_tx_byp_val[9..0]
    {"cfg_loopback",        226, 226},  // cfg_loopback
    {"shlpbck",             225, 224},  // shlpbck[1..0]
    {"sl_enable",           223, 223},  // sl_enable
    {"sl_posedge_sample",   222, 222},  // sl_posedge_sample
    {"trimen",              221, 220},  // trimen[1..0]
    {"serdes_tx_byp",       219, 219},  // serdes_tx_byp
    {"serdes_pll_byp",      218, 218},  // serdes_pll_byp
    {"lowf_byp",            217, 217},  // lowf_byp
    {"spdsel_byp",          216, 216},  // spdsel_byp
    {"div4_byp",            215, 215},  // div4_byp
    {"clkf_byp",            214, 208},  // clkf_byp[6..0]
    {"biasdrv_hs_ls_byp",   207, 203},  // biasdrv_hs_ls_byp[4..0]
    {"tcoeff_hf_ls_byp",    202, 198},  // tcoeff_hf_ls_byp[4..0]
    {"biasdrv_hf_byp",      197, 193},  // biasdrv_hf_byp[4..0]
    {"tcoeff_hf_byp",       192, 188},  // tcoeff_hf_byp[4..0]
    {"biasdrv_lf_ls_byp",   187, 183},  // biasdrv_lf_ls_byp[4..0]
    {"tcoeff_lf_ls_byp",    182, 178},  // tcoeff_lf_ls_byp[4..0]
    {"biasdrv_lf_byp",      177, 173},  // biasdrv_lf_byp[4..0]
    {"tcoeff_lf_byp",       172, 168},  // tcoeff_lf_byp[4..0]
    {"Unused4",             167, 167},  // 0
    {"interpbw",            166, 162},  // interpbw[4..0]
    {"pll_cpb",             161, 159},  // pll_cpb[2..0]
    {"pll_cps",             158, 156},  // pll_cps[2..0]
    {"pll_diffamp",         155, 152},  // pll_diffamp[3..0]
    {"cfg_err_thr",         151, 150},  // cfg_err_thr
    {"cfg_rx_idle_set",     149, 149},  // cfg_rx_idle_set
    {"cfg_rx_idle_clr",     148, 148},  // cfg_rx_idle_clr
    {"cfg_rx_idle_thr",     147, 144},  // cfg_rx_idle_thr[3..0]
    {"cfg_com_thr",         143, 140},  // cfg_com_thr[3..0]
    {"cfg_rx_offset",       139, 136},  // cfg_rx_offset[3..0]
    {"cfg_skp_max",         135, 132},  // cfg_skp_max[3..0]
    {"cfg_skp_min",         131, 128},  // cfg_skp_min[3..0]
    {"cfg_fast_pwrup",      127, 127},  // cfg_fast_pwrup
    {"Unused6",             126, 100},  // 0
    {"detected_n",           99,  99},  // detected_n
    {"detected_p",           98,  98},  // detected_p
    {"dbg_res_rx",           97,  94},  // dbg_res_rx[3..0]
    {"dbg_res_tx",           93,  90},  // dbg_res_tx[3..0]
    {"cfg_tx_pol_set",       89,  89},  // cfg_tx_pol_set
    {"cfg_tx_pol_clr",       88,  88},  // cfg_tx_pol_clr
    {"cfg_rx_pol_set",       87,  87},  // cfg_rx_pol_set
    {"cfg_rx_pol_clr",       86,  86},  // cfg_rx_pol_clr
    {"cfg_rxd_set",          85,  85},  // cfg_rxd_set
    {"cfg_rxd_clr",          84,  84},  // cfg_rxd_clr
    {"cfg_rxd_wait",         83,  80},  // cfg_rxd_wait[3..0]
    {"cfg_cdr_limit",        79,  79},  // cfg_cdr_limit
    {"cfg_cdr_rotate",       78,  78},  // cfg_cdr_rotate
    {"cfg_cdr_bw_ctl",       77,  76},  // cfg_cdr_bw_ctl[1..0]
    {"cfg_cdr_trunc",        75,  74},  // cfg_cdr_trunc[1..0]
    {"cfg_cdr_rqoffs",       73,  64},  // cfg_cdr_rqoffs[9..0]
    {"cfg_cdr_inc2",         63,  58},  // cfg_cdr_inc2[5..0]
    {"cfg_cdr_inc1",         57,  52},  // cfg_cdr_inc1[5..0]
    {"fusopt_voter_sync",    51,  51},  // fusopt_voter_sync
    {"rndt",                 50,  50},  // rndt
    {"hcya",                 49,  49},  // hcya
    {"hyst",                 48,  48},  // hyst
    {"idle_dac",             47,  45},  // idle_dac[2..0]
    {"bg_ref_sel",           44,  44},  // bg_ref_sel
    {"ic50dac",              43,  39},  // ic50dac[4..0]
    {"ir50dac",              38,  34},  // ir50dac[4..0]
    {"tx_rout_comp_bypass",  33,  33},  // tx_rout_comp_bypass
    {"tx_rout_comp_value",   32,  29},  // tx_rout_comp_value[3..0]
    {"tx_res_offset",        28,  25},  // tx_res_offset[3..0]
    {"rx_rout_comp_bypass",  24,  24},  // rx_rout_comp_bypass
    {"rx_rout_comp_value",   23,  20},  // rx_rout_comp_value[3..0]
    {"rx_res_offset",        19,  16},  // rx_res_offset[3..0]
    {"rx_cap_gen2",          15,  12},  // rx_cap_gen2[3..0]
    {"rx_eq_gen2",           11,   8},  // rx_eq_gen2[3..0]
    {"rx_cap_gen1",           7,   4},  // rx_cap_gen1[3..0]
    {"rx_eq_gen1",            3,   0},  // rx_eq_gen1[3..0]
    {NULL, -1, -1}
};

