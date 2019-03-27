/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */


#include "opt_ah.h"

#ifdef AH_SUPPORT_AR9300

#include "ah.h"
#include "ah_internal.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"

#if ATH_SUPPORT_AIC

#define ATH_AIC_TEST_PATTERN    1

struct ath_aic_sram_info {
    HAL_BOOL        valid;
    u_int8_t    rot_quad_att_db;
    HAL_BOOL        vga_quad_sign;
    u_int8_t    rot_dir_att_db;
    HAL_BOOL        vga_dir_sign;
    u_int8_t    com_att_6db;
    };

struct ath_aic_out_info {
    int16_t     dir_path_gain_lin;
    int16_t     quad_path_gain_lin;
    struct ath_aic_sram_info sram;
    };

#define ATH_AIC_MAX_COM_ATT_DB_TABLE    6
#define ATH_AIC_MAX_AIC_LIN_TABLE       69
#define ATH_AIC_MIN_ROT_DIR_ATT_DB      0
#define ATH_AIC_MIN_ROT_QUAD_ATT_DB     0
#define ATH_AIC_MAX_ROT_DIR_ATT_DB      37
#define ATH_AIC_MAX_ROT_QUAD_ATT_DB     37
#define ATH_AIC_SRAM_AUTO_INCREMENT     0x80000000
#define ATH_AIC_SRAM_GAIN_TABLE_OFFSET  0x280
#define ATH_AIC_SRAM_CAL_OFFSET         0x140
#define ATH_AIC_MAX_CAL_COUNT           5
#define ATH_AIC_MEAS_MAG_THRESH         20
#define ATH_AIC_BT_JUPITER_CTRL         0x66820
#define ATH_AIC_BT_AIC_ENABLE           0x02


static const u_int8_t com_att_db_table[ATH_AIC_MAX_COM_ATT_DB_TABLE] = {
        0, 3, 9, 15, 21, 27};

static const u_int16_t aic_lin_table[ATH_AIC_MAX_AIC_LIN_TABLE] = {
        8191, 7300, 6506, 5799, 5168, 4606, 4105, 3659,
        3261, 2906, 2590, 2309, 2057, 1834, 1634, 1457,
        1298, 1157, 1031,  919,  819,  730,  651,  580,
         517,  461,  411,  366,  326,  291,  259,  231,
         206,  183,  163,  146,  130,  116,  103,   92,
          82,   73,   65,   58,   52,   46,   41,   37,
          33,   29,   26,   23,   21,   18,   16,   15,
          13,   12,   10,    9,    8,    7,    7,    6,
           5,    5,    4,    4,    3};

#if ATH_AIC_TEST_PATTERN
static const u_int32_t aic_test_pattern[ATH_AIC_MAX_BT_CHANNEL] = {
0x00000,    // 0
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x1918d,
0x1938d,    // 10
0x00000,
0x1978d,
0x19e8d,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,    // 20
0x00000,
0x00000,
0x1ce8f,
0x00000,
0x00000,
0x00000,
0x00000,
0x1ca93,
0x1c995,
0x00000,    // 30
0x1c897,
0x1c899,
0x00000,
0x00000,
0x1c79f,
0x00000,
0x1c7a5,
0x1c6ab,
0x00000,
0x00000,    // 40
0x00000,
0x00000,
0x1c63f,
0x00000,
0x1c52b,
0x1c525,
0x1c523,
0x00000,
0x00000,
0x00000,    // 50
0x00000,
0x00000,
0x1c617,
0x00000,
0x1c615,
0x1c613,
0x00000,
0x00000,
0x00000,
0x00000,    // 60
0x1c80f,
0x1c90f,
0x1c90f,
0x1ca0f,
0x1ca0d,
0x1cb0d,
0x00000,
0x00000,
0x00000,
0x00000,    // 70
0x1d00d,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000,
0x00000
};
#endif

static void
ar9300_aic_gain_table(struct ath_hal *ah)
{
    u_int32_t   aic_atten_word[19], i;

    /* Program gain table */
    aic_atten_word[0] = (0x1 & 0xf)<<14 | (0x1f & 0x1f)<<9 | (0x0 & 0xf)<<5 | 
                (0x1f & 0x1f); // -01 dB: 4'd1, 5'd31,  00 dB: 4'd0, 5'd31;
    aic_atten_word[1] = (0x3 & 0xf)<<14 | (0x1f & 0x1f)<<9 | (0x2 & 0xf)<<5 | 
                (0x1f & 0x1f); // -03 dB: 4'd3, 5'd31, -02 dB: 4'd2, 5'd31;
    aic_atten_word[2] = (0x5 & 0xf)<<14 | (0x1f & 0x1f)<<9 | (0x4 & 0xf)<<5 | 
                (0x1f & 0x1f); // -05 dB: 4'd5, 5'd31, -04 dB: 4'd4, 5'd31;
    aic_atten_word[3] = (0x1 & 0xf)<<14 | (0x1e & 0x1f)<<9 | (0x0 & 0xf)<<5 | 
                (0x1e & 0x1f); // -07 dB: 4'd1, 5'd30, -06 dB: 4'd0, 5'd30;
    aic_atten_word[4] = (0x3 & 0xf)<<14 | (0x1e & 0x1f)<<9 | (0x2 & 0xf)<<5 | 
                (0x1e & 0x1f); // -09 dB: 4'd3, 5'd30, -08 dB: 4'd2, 5'd30;
    aic_atten_word[5] = (0x5 & 0xf)<<14 | (0x1e & 0x1f)<<9 | (0x4 & 0xf)<<5 | 
                (0x1e & 0x1f); // -11 dB: 4'd5, 5'd30, -10 dB: 4'd4, 5'd30;
    aic_atten_word[6] = (0x1 & 0xf)<<14 | (0xf & 0x1f)<<9  | (0x0 & 0xf)<<5 | 
                (0xf & 0x1f);  // -13 dB: 4'd1, 5'd15, -12 dB: 4'd0, 5'd15;
    aic_atten_word[7] = (0x3 & 0xf)<<14 | (0xf & 0x1f)<<9  | (0x2 & 0xf)<<5 | 
                (0xf & 0x1f);  // -15 dB: 4'd3, 5'd15, -14 dB: 4'd2, 5'd15;
    aic_atten_word[8] = (0x5 & 0xf)<<14 | (0xf & 0x1f)<<9  | (0x4 & 0xf)<<5 | 
                (0xf & 0x1f);  // -17 dB: 4'd5, 5'd15, -16 dB: 4'd4, 5'd15;
    aic_atten_word[9] = (0x1 & 0xf)<<14 | (0x7 & 0x1f)<<9  | (0x0 & 0xf)<<5 | 
                (0x7 & 0x1f);  // -19 dB: 4'd1, 5'd07, -18 dB: 4'd0, 5'd07;
    aic_atten_word[10] =(0x3 & 0xf)<<14 | (0x7 & 0x1f)<<9  | (0x2 & 0xf)<<5 | 
                (0x7 & 0x1f);  // -21 dB: 4'd3, 5'd07, -20 dB: 4'd2, 5'd07;
    aic_atten_word[11] =(0x5 & 0xf)<<14 | (0x7 & 0x1f)<<9  | (0x4 & 0xf)<<5 | 
                (0x7 & 0x1f);  // -23 dB: 4'd5, 5'd07, -22 dB: 4'd4, 5'd07;
    aic_atten_word[12] =(0x7 & 0xf)<<14 | (0x7 & 0x1f)<<9  | (0x6 & 0xf)<<5 | 
                (0x7 & 0x1f);  // -25 dB: 4'd7, 5'd07, -24 dB: 4'd6, 5'd07;
    aic_atten_word[13] =(0x3 & 0xf)<<14 | (0x3 & 0x1f)<<9  | (0x2 & 0xf)<<5 | 
                (0x3 & 0x1f);  // -27 dB: 4'd3, 5'd03, -26 dB: 4'd2, 5'd03;
    aic_atten_word[14] =(0x5 & 0xf)<<14 | (0x3 & 0x1f)<<9  | (0x4 & 0xf)<<5 | 
                (0x3 & 0x1f);  // -29 dB: 4'd5, 5'd03, -28 dB: 4'd4, 5'd03;    
    aic_atten_word[15] =(0x1 & 0xf)<<14 | (0x1 & 0x1f)<<9  | (0x0 & 0xf)<<5 | 
                (0x1 & 0x1f);  // -31 dB: 4'd1, 5'd01, -30 dB: 4'd0, 5'd01;
    aic_atten_word[16] =(0x3 & 0xf)<<14 | (0x1 & 0x1f)<<9  | (0x2 & 0xf)<<5 | 
                (0x1 & 0x1f);  // -33 dB: 4'd3, 5'd01, -32 dB: 4'd2, 5'd01;
    aic_atten_word[17] =(0x5 & 0xf)<<14 | (0x1 & 0x1f)<<9  | (0x4 & 0xf)<<5 | 
                (0x1 & 0x1f);  // -35 dB: 4'd5, 5'd01, -34 dB: 4'd4, 5'd01;
    aic_atten_word[18] =(0x7 & 0xf)<<14 | (0x1 & 0x1f)<<9  | (0x6 & 0xf)<<5 | 
                (0x1 & 0x1f);  // -37 dB: 4'd7, 5'd01, -36 dB: 4'd6, 5'd01;

    /* Write to Gain table with auto increment enabled. */
    OS_REG_WRITE(ah, (AR_PHY_AIC_SRAM_ADDR_B0 + 0x3000), 
                 (ATH_AIC_SRAM_AUTO_INCREMENT | 
                  ATH_AIC_SRAM_GAIN_TABLE_OFFSET));

    for (i = 0; i < 19; i++) {
        OS_REG_WRITE(ah, (AR_PHY_AIC_SRAM_DATA_B0 + 0x3000),
                    aic_atten_word[i]);
    }

}

static int16_t 
ar9300_aic_find_valid (struct ath_aic_sram_info *cal_sram, 
                           HAL_BOOL dir,
                           u_int8_t index)
{
    int16_t i;

    if (dir) {
        /* search forward */
        for (i = index + 1; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
            if (cal_sram[i].valid) {
                break;
            }
        }
    }
    else {
        /* search backword */
        for (i = index - 1; i >= 0; i--) {
            if (cal_sram[i].valid) {
                break;
            }
        }
    }
    if ((i >= ATH_AIC_MAX_BT_CHANNEL) || (i < 0)) {
        i = -1;
    }

    return i;
}

static int16_t
ar9300_aic_find_index (u_int8_t type, int16_t value)
{
    int16_t i = -1;

    /* 
     * type 0: aic_lin_table, 1: com_att_db_table
     */

    if (type == 0) {
        /* Find in aic_lin_table */
        for (i = ATH_AIC_MAX_AIC_LIN_TABLE - 1; i >= 0; i--) {
            if (aic_lin_table[i] >= value) {
                break;
            }
        }
    }
    else if (type == 1) {
        /* find in com_att_db_table */
        for (i = 0; i < ATH_AIC_MAX_COM_ATT_DB_TABLE; i++) {
            if (com_att_db_table[i] > value) {
                i--;
                break;
            }
        }
        if (i >= ATH_AIC_MAX_COM_ATT_DB_TABLE) {
            i = -1;
        }
    }

    return i;
}

static HAL_BOOL
ar9300_aic_cal_post_process (struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ath_aic_sram_info cal_sram[ATH_AIC_MAX_BT_CHANNEL];
    struct ath_aic_out_info aic_sram[ATH_AIC_MAX_BT_CHANNEL];
    u_int32_t dir_path_gain_idx, quad_path_gain_idx, value;
    u_int32_t fixed_com_att_db;
    int8_t dir_path_sign, quad_path_sign;
    int16_t i;
    HAL_BOOL ret = AH_TRUE;

    /* Read CAL_SRAM and get valid values. */
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) CAL_SRAM:\n");

    for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
        OS_REG_WRITE(ah, AR_PHY_AIC_SRAM_ADDR_B1, 
                    (ATH_AIC_SRAM_CAL_OFFSET + i*4));
#if ATH_AIC_TEST_PATTERN
        value = aic_test_pattern[i];
#else
        value = OS_REG_READ(ah, AR_PHY_AIC_SRAM_DATA_B1);
#endif
        cal_sram[i].valid = MS(value, AR_PHY_AIC_SRAM_VALID);
        cal_sram[i].rot_quad_att_db = MS(value, 
                                         AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB);
        cal_sram[i].vga_quad_sign = MS(value, AR_PHY_AIC_SRAM_VGA_QUAD_SIGN);
        cal_sram[i].rot_dir_att_db = MS(value, AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB);
        cal_sram[i].vga_dir_sign = MS(value, AR_PHY_AIC_SRAM_VGA_DIR_SIGN);
        cal_sram[i].com_att_6db = MS(value, AR_PHY_AIC_SRAM_COM_ATT_6DB);

        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(AIC) %2d   %2d   %2d   %2d   %2d   %2d   %2d   0x%05x\n",
                i, cal_sram[i].vga_quad_sign,
                cal_sram[i].vga_dir_sign,
                cal_sram[i].rot_dir_att_db,
                cal_sram[i].rot_quad_att_db,
                cal_sram[i].com_att_6db,
                cal_sram[i].valid,
                value);

        if (cal_sram[i].valid) {
            dir_path_gain_idx = cal_sram[i].rot_dir_att_db +
                        com_att_db_table[cal_sram[i].com_att_6db];
            quad_path_gain_idx = cal_sram[i].rot_quad_att_db +
                        com_att_db_table[cal_sram[i].com_att_6db];
            dir_path_sign = (cal_sram[i].vga_dir_sign) ? 1 : -1;
            quad_path_sign = (cal_sram[i].vga_quad_sign) ? 1 : -1;
            aic_sram[i].dir_path_gain_lin = dir_path_sign *
                        aic_lin_table[dir_path_gain_idx];
            aic_sram[i].quad_path_gain_lin = quad_path_sign *
                        aic_lin_table[quad_path_gain_idx];
        }
    }

    for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
        int16_t start_idx, end_idx;

        if (cal_sram[i].valid) {
            continue;
        }

        start_idx = ar9300_aic_find_valid(cal_sram, 0, i);
        end_idx = ar9300_aic_find_valid(cal_sram, 1, i);

        if (start_idx < 0)
        {
            /* extrapolation */
            start_idx = end_idx;
            end_idx = ar9300_aic_find_valid(cal_sram, 1, start_idx);

            if (end_idx < 0) {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                        "(AIC) Error (1): i = %d, start_idx = %d \n",
                        i, start_idx);
                ret = AH_FALSE;
                break;
            }
            aic_sram[i].dir_path_gain_lin = 
                    ((aic_sram[start_idx].dir_path_gain_lin - 
                      aic_sram[end_idx].dir_path_gain_lin) *
                     (start_idx - i) + ((end_idx - i) >> 1)) / 
                     (end_idx - i) +
                     aic_sram[start_idx].dir_path_gain_lin;
            aic_sram[i].quad_path_gain_lin = 
                    ((aic_sram[start_idx].quad_path_gain_lin - 
                      aic_sram[end_idx].quad_path_gain_lin) *
                     (start_idx - i) + ((end_idx - i) >> 1)) / 
                     (end_idx - i) +
                     aic_sram[start_idx].quad_path_gain_lin;
        }
        if (end_idx < 0)
        {
            /* extrapolation */
            end_idx = ar9300_aic_find_valid(cal_sram, 0, start_idx);

            if (end_idx < 0) {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                        "(AIC) Error (2): i = %d, start_idx = %d\n",
                        i, start_idx);
                ret = AH_FALSE;
                break;
            }
            aic_sram[i].dir_path_gain_lin = 
                    ((aic_sram[start_idx].dir_path_gain_lin - 
                      aic_sram[end_idx].dir_path_gain_lin) *
                     (i - start_idx) + ((start_idx - end_idx) >> 1)) / 
                     (start_idx - end_idx) +
                     aic_sram[start_idx].dir_path_gain_lin;
            aic_sram[i].quad_path_gain_lin = 
                    ((aic_sram[start_idx].quad_path_gain_lin - 
                      aic_sram[end_idx].quad_path_gain_lin) *
                     (i - start_idx) + ((start_idx - end_idx) >> 1)) / 
                     (start_idx - end_idx) + 
                     aic_sram[start_idx].quad_path_gain_lin;
            
        }
        else {
            /* interpolation */
            aic_sram[i].dir_path_gain_lin = 
                    (((end_idx - i) * aic_sram[start_idx].dir_path_gain_lin) +
                     ((i - start_idx) * aic_sram[end_idx].dir_path_gain_lin) +
                     ((end_idx - start_idx) >> 1)) / 
                     (end_idx - start_idx);
            aic_sram[i].quad_path_gain_lin = 
                    (((end_idx - i) * aic_sram[start_idx].quad_path_gain_lin) +
                     ((i - start_idx) * aic_sram[end_idx].quad_path_gain_lin) +
                     ((end_idx - start_idx) >> 1))/ 
                     (end_idx - start_idx);
        }
    }

    /* From dir/quad_path_gain_lin to sram. */
    i = ar9300_aic_find_valid(cal_sram, 1, 0);
    if (i < 0) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(AIC) Error (3): can't find valid. Force it to 0.\n");
        i = 0;
        ret = AH_FALSE;
    }
    fixed_com_att_db = com_att_db_table[cal_sram[i].com_att_6db];

    for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
        int16_t rot_dir_path_att_db, rot_quad_path_att_db;

        aic_sram[i].sram.vga_dir_sign = (aic_sram[i].dir_path_gain_lin >= 0)
                                        ? 1 : 0;
        aic_sram[i].sram.vga_quad_sign= (aic_sram[i].quad_path_gain_lin >= 0) 
                                        ? 1 : 0;

        rot_dir_path_att_db = 
            ar9300_aic_find_index(0, abs(aic_sram[i].dir_path_gain_lin)) -
            fixed_com_att_db;
        rot_quad_path_att_db = 
            ar9300_aic_find_index(0, abs(aic_sram[i].quad_path_gain_lin)) -
            fixed_com_att_db;

        aic_sram[i].sram.com_att_6db = ar9300_aic_find_index(1, 
                                                            fixed_com_att_db);

        aic_sram[i].sram.valid = 1;
        aic_sram[i].sram.rot_dir_att_db = 
            MIN(MAX(rot_dir_path_att_db, ATH_AIC_MIN_ROT_DIR_ATT_DB),
                ATH_AIC_MAX_ROT_DIR_ATT_DB);
        aic_sram[i].sram.rot_quad_att_db = 
            MIN(MAX(rot_quad_path_att_db, ATH_AIC_MIN_ROT_QUAD_ATT_DB),
                ATH_AIC_MAX_ROT_QUAD_ATT_DB);
    }

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) Post processing results:\n");

    for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
        ahp->ah_aic_sram[i] = (SM(aic_sram[i].sram.vga_dir_sign, 
                                  AR_PHY_AIC_SRAM_VGA_DIR_SIGN) |
                               SM(aic_sram[i].sram.vga_quad_sign, 
                                  AR_PHY_AIC_SRAM_VGA_QUAD_SIGN) |
                               SM(aic_sram[i].sram.com_att_6db, 
                                  AR_PHY_AIC_SRAM_COM_ATT_6DB) |
                               SM(aic_sram[i].sram.valid, 
                                  AR_PHY_AIC_SRAM_VALID) |
                               SM(aic_sram[i].sram.rot_dir_att_db, 
                                  AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB) |
                               SM(aic_sram[i].sram.rot_quad_att_db, 
                                  AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB));


        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                "(AIC) ch%02d 0x%05x  %2d  %2d  %2d  %2d  %2d  %2d  %d  %d\n",
                i, 
                ahp->ah_aic_sram[i],
                aic_sram[i].sram.vga_quad_sign,
                aic_sram[i].sram.vga_dir_sign,
                aic_sram[i].sram.rot_dir_att_db,
                aic_sram[i].sram.rot_quad_att_db,
                aic_sram[i].sram.com_att_6db,
                aic_sram[i].sram.valid,
                aic_sram[i].dir_path_gain_lin,
                aic_sram[i].quad_path_gain_lin);
    }

    return ret;
}

u_int32_t
ar9300_aic_calibration(struct ath_hal *ah)
{
    u_int32_t   aic_ctrl_b0[5], aic_ctrl_b1[5];
    u_int32_t   aic_stat_b0[2], aic_stat_b1[2];
    u_int32_t   aic_stat, value;
    u_int32_t   i, cal_count = ATH_AIC_MAX_CAL_COUNT;
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (AR_SREV_JUPITER_10(ah)) {
        aic_ctrl_b0[0] = AR_PHY_AIC_CTRL_0_B0_10;
        aic_ctrl_b0[1] = AR_PHY_AIC_CTRL_1_B0_10;
        aic_ctrl_b0[2] = AR_PHY_AIC_CTRL_2_B0_10;
        aic_ctrl_b0[3] = AR_PHY_AIC_CTRL_3_B0_10;
        aic_ctrl_b1[0] = AR_PHY_AIC_CTRL_0_B1_10;
        aic_ctrl_b1[1] = AR_PHY_AIC_CTRL_1_B1_10;
        aic_stat_b0[0] = AR_PHY_AIC_STAT_0_B0_10;
        aic_stat_b0[1] = AR_PHY_AIC_STAT_1_B0_10;
        aic_stat_b1[0] = AR_PHY_AIC_STAT_0_B1_10;
        aic_stat_b1[1] = AR_PHY_AIC_STAT_1_B1_10;
    }
    else {
        aic_ctrl_b0[0] = AR_PHY_AIC_CTRL_0_B0_20;
        aic_ctrl_b0[1] = AR_PHY_AIC_CTRL_1_B0_20;
        aic_ctrl_b0[2] = AR_PHY_AIC_CTRL_2_B0_20;
        aic_ctrl_b0[3] = AR_PHY_AIC_CTRL_3_B0_20;
        aic_ctrl_b0[4] = AR_PHY_AIC_CTRL_4_B0_20;
        aic_ctrl_b1[0] = AR_PHY_AIC_CTRL_0_B1_20;
        aic_ctrl_b1[1] = AR_PHY_AIC_CTRL_1_B1_20;
        aic_ctrl_b1[4] = AR_PHY_AIC_CTRL_4_B1_20;
        aic_stat_b0[0] = AR_PHY_AIC_STAT_0_B0_20;
        aic_stat_b0[1] = AR_PHY_AIC_STAT_1_B0_20;
        aic_stat_b1[0] = AR_PHY_AIC_STAT_0_B1_20;
        aic_stat_b1[1] = AR_PHY_AIC_STAT_1_B1_20;
    }

    /* Config LNA gain difference */
    OS_REG_WRITE(ah, AR_PHY_BT_COEX_4, 0x22180600);
    OS_REG_WRITE(ah, AR_PHY_BT_COEX_5, 0x52443a2e);

    OS_REG_WRITE(ah, aic_ctrl_b0[0], 
                (SM(0, AR_PHY_AIC_MON_ENABLE) |
                 SM(40, AR_PHY_AIC_CAL_MAX_HOP_COUNT) |
                 SM(1, AR_PHY_AIC_CAL_MIN_VALID_COUNT) | //26
                 SM(37, AR_PHY_AIC_F_WLAN) |
                 SM(1, AR_PHY_AIC_CAL_CH_VALID_RESET) |
                 SM(0, AR_PHY_AIC_CAL_ENABLE) |
                 SM(0x40, AR_PHY_AIC_BTTX_PWR_THR) |
                 SM(0, AR_PHY_AIC_ENABLE)));

    OS_REG_WRITE(ah, aic_ctrl_b1[0], 
                (SM(0, AR_PHY_AIC_MON_ENABLE) |
                 SM(1, AR_PHY_AIC_CAL_CH_VALID_RESET) |
                 SM(0, AR_PHY_AIC_CAL_ENABLE) |
                 SM(0x40, AR_PHY_AIC_BTTX_PWR_THR) |
                 SM(0, AR_PHY_AIC_ENABLE)));

    OS_REG_WRITE(ah, aic_ctrl_b0[1], 
                (SM(8, AR_PHY_AIC_CAL_BT_REF_DELAY) |
                 SM(6, AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO) | 
                 SM(3, AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO) | 
                 SM(0, AR_PHY_AIC_BT_IDLE_CFG) |
                 SM(1, AR_PHY_AIC_STDBY_COND) |
                 SM(37, AR_PHY_AIC_STDBY_ROT_ATT_DB) |
                 SM(5, AR_PHY_AIC_STDBY_COM_ATT_DB) |
                 SM(15, AR_PHY_AIC_RSSI_MAX) |
                 SM(0, AR_PHY_AIC_RSSI_MIN)));

    OS_REG_WRITE(ah, aic_ctrl_b1[1], 
                (SM(6, AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO) |
                 SM(3, AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO) |
                 SM(15, AR_PHY_AIC_RSSI_MAX) |
                 SM(0, AR_PHY_AIC_RSSI_MIN)));

    OS_REG_WRITE(ah, aic_ctrl_b0[2], 
                (SM(44, AR_PHY_AIC_RADIO_DELAY) |
                 SM(7, AR_PHY_AIC_CAL_STEP_SIZE_CORR) |
                 SM(12, AR_PHY_AIC_CAL_ROT_IDX_CORR) |
                 SM(2, AR_PHY_AIC_CAL_CONV_CHECK_FACTOR) |
                 SM(5, AR_PHY_AIC_ROT_IDX_COUNT_MAX) |
                 SM(1, AR_PHY_AIC_CAL_SYNTH_TOGGLE) |
                 SM(1, AR_PHY_AIC_CAL_SYNTH_AFTER_BTRX) |
                 SM(200, AR_PHY_AIC_CAL_SYNTH_SETTLING)));

    OS_REG_WRITE(ah, aic_ctrl_b0[3], 
                (SM(20, AR_PHY_AIC_MON_MAX_HOP_COUNT) |
                 SM(10, AR_PHY_AIC_MON_MIN_STALE_COUNT) |
                 SM(1, AR_PHY_AIC_MON_PWR_EST_LONG) |
                 SM(2, AR_PHY_AIC_MON_PD_TALLY_SCALING) |
                 SM(18, AR_PHY_AIC_MON_PERF_THR) |
                 SM(1, AR_PHY_AIC_CAL_COM_ATT_DB_FIXED) |
                 SM(2, AR_PHY_AIC_CAL_TARGET_MAG_SETTING) |
                 SM(3, AR_PHY_AIC_CAL_PERF_CHECK_FACTOR) |
                 SM(1, AR_PHY_AIC_CAL_PWR_EST_LONG)));
    
    ar9300_aic_gain_table(ah);

    /* Need to enable AIC reference signal in BT modem. */
    OS_REG_WRITE(ah, ATH_AIC_BT_JUPITER_CTRL, 
                (OS_REG_READ(ah, ATH_AIC_BT_JUPITER_CTRL) | 
                 ATH_AIC_BT_AIC_ENABLE));

    while (cal_count)
    {
        /* Start calibration */
        OS_REG_CLR_BIT(ah, aic_ctrl_b1[0], AR_PHY_AIC_CAL_ENABLE);
        OS_REG_SET_BIT(ah, aic_ctrl_b1[0], AR_PHY_AIC_CAL_CH_VALID_RESET);
        OS_REG_SET_BIT(ah, aic_ctrl_b1[0], AR_PHY_AIC_CAL_ENABLE);

        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) Start calibration #%d\n",
                (ATH_AIC_MAX_CAL_COUNT - cal_count));

        /* Wait until calibration is completed. */
        for (i = 0; i < 10000; i++) {
            /* 
             * Use AR_PHY_AIC_CAL_ENABLE bit instead of AR_PHY_AIC_CAL_DONE.
             * Sometimes CAL_DONE bit is not asserted.
             */
            if ((OS_REG_READ(ah, aic_ctrl_b1[0]) & AR_PHY_AIC_CAL_ENABLE) == 0)
            {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) Cal is done at #%d\n", i);
                break;
            }
            OS_DELAY(1);
        }

        /* print out status registers */
        aic_stat = OS_REG_READ(ah, aic_stat_b1[0]);
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(AIC) CAL_DONE = %d, CAL_ACTIVE = %d, MEAS_COUNT = %d\n",
                MS(aic_stat, AR_PHY_AIC_CAL_DONE),
                MS(aic_stat, AR_PHY_AIC_CAL_ACTIVE),
                MS(aic_stat, AR_PHY_AIC_MEAS_COUNT));
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(AIC) ANT_ISO = %d, HOP_COUNT = %d, VALID_COUNT = %d\n",
                MS(aic_stat, AR_PHY_AIC_CAL_ANT_ISO_EST),
                MS(aic_stat, AR_PHY_AIC_CAL_HOP_COUNT),
                MS(aic_stat, AR_PHY_AIC_CAL_VALID_COUNT));
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(AIC) BT_WEAK = %d, BT_STRONG = %d, , \n",
                MS(aic_stat, AR_PHY_AIC_CAL_BT_TOO_WEAK_ERR),
                MS(aic_stat, AR_PHY_AIC_CAL_BT_TOO_STRONG_ERR));

        aic_stat = OS_REG_READ(ah, aic_stat_b1[1]);
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(AIC) MEAS_MAG_MIN = %d, CAL_AIC_SM = %d, AIC_SM = %d\n",
                MS(aic_stat, AR_PHY_AIC_MEAS_MAG_MIN),
                MS(aic_stat, AR_PHY_AIC_CAL_AIC_SM),
                MS(aic_stat, AR_PHY_AIC_SM));

        if (i >= 10000) {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) Calibration failed.\n");
            break;
        }

        /* print out calibration result */
        if (MS(aic_stat, AR_PHY_AIC_MEAS_MAG_MIN) < ATH_AIC_MEAS_MAG_THRESH) {
            for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
                OS_REG_WRITE(ah, AR_PHY_AIC_SRAM_ADDR_B1, 
                            (ATH_AIC_SRAM_CAL_OFFSET + i*4));
                value = OS_REG_READ(ah, AR_PHY_AIC_SRAM_DATA_B1);
                if (value & 0x01) {
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                             "(AIC) BT chan %02d: 0x%08x\n", i, value);
                }
            }
            break;
        }
        cal_count--;
    }

    if (!cal_count) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) Calibration failed2.\n");
    }

    /* Disable AIC reference signal in BT modem. */
    OS_REG_WRITE(ah, ATH_AIC_BT_JUPITER_CTRL, 
                (OS_REG_READ(ah, ATH_AIC_BT_JUPITER_CTRL) & 
                 ~ATH_AIC_BT_AIC_ENABLE));

    ahp->ah_aic_enabled = ar9300_aic_cal_post_process(ah) ? AH_TRUE : AH_FALSE;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) ah_aic_enable = %d\n",
             ahp->ah_aic_enabled);
    return 0;
}


u_int32_t
ar9300_aic_start_normal (struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t aic_ctrl0_b1, aic_ctrl1_b0, aic_ctrl1_b1;
    int16_t i;

    /* Config LNA gain difference */
    OS_REG_WRITE(ah, AR_PHY_BT_COEX_4, 0x22180600);
    OS_REG_WRITE(ah, AR_PHY_BT_COEX_5, 0x52443a2e);

    ar9300_aic_gain_table(ah);

    OS_REG_WRITE(ah, AR_PHY_AIC_SRAM_ADDR_B1, ATH_AIC_SRAM_AUTO_INCREMENT);

    for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
        OS_REG_WRITE(ah, AR_PHY_AIC_SRAM_DATA_B1, ahp->ah_aic_sram[i]);
    }

    if (AR_SREV_JUPITER_10(ah)) {
        aic_ctrl0_b1 = AR_PHY_AIC_CTRL_0_B1_10;
        aic_ctrl1_b0 = AR_PHY_AIC_CTRL_1_B0_10;
        aic_ctrl1_b1 = AR_PHY_AIC_CTRL_1_B1_10;
    }
    else {
        aic_ctrl0_b1 = AR_PHY_AIC_CTRL_0_B1_20;
        aic_ctrl1_b0 = AR_PHY_AIC_CTRL_1_B0_20;
        aic_ctrl1_b1 = AR_PHY_AIC_CTRL_1_B1_20;
    }

    OS_REG_WRITE(ah, aic_ctrl1_b0,
                (SM(0, AR_PHY_AIC_BT_IDLE_CFG) |
                 SM(1, AR_PHY_AIC_STDBY_COND) |
                 SM(37, AR_PHY_AIC_STDBY_ROT_ATT_DB) |
                 SM(5, AR_PHY_AIC_STDBY_COM_ATT_DB) |
                 SM(15, AR_PHY_AIC_RSSI_MAX) |
                 SM(0, AR_PHY_AIC_RSSI_MIN)));

    OS_REG_WRITE(ah, aic_ctrl1_b1,
                (SM(15, AR_PHY_AIC_RSSI_MAX) |
                 SM(0, AR_PHY_AIC_RSSI_MIN)));

    OS_REG_WRITE(ah, aic_ctrl0_b1, 
                (SM(0x40, AR_PHY_AIC_BTTX_PWR_THR) |
                 SM(1, AR_PHY_AIC_ENABLE)));

    ahp->ah_aic_enabled = AH_TRUE;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(AIC) Start normal operation mode.\n");
    return 0;
}
#endif

#endif


