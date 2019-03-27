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

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"
#ifdef AH_DEBUG
#include "ah_desc.h"                    /* NB: for HAL_PHYERR* */
#endif
#include "ar9300/ar9300.h"
#include "ar9300/ar9300eep.h"
#include "ar9300/ar9300template_generic.h"
#include "ar9300/ar9300template_xb112.h"
#include "ar9300/ar9300template_hb116.h"
#include "ar9300/ar9300template_xb113.h"
#include "ar9300/ar9300template_hb112.h"
#include "ar9300/ar9300template_ap121.h"
#include "ar9300/ar9300template_osprey_k31.h"
#include "ar9300/ar9300template_wasp_2.h"
#include "ar9300/ar9300template_wasp_k31.h"
#include "ar9300/ar9300template_aphrodite.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"



#if AH_BYTE_ORDER == AH_BIG_ENDIAN
void ar9300_swap_eeprom(ar9300_eeprom_t *eep);
void ar9300_eeprom_template_swap(void);
#endif

static u_int16_t ar9300_eeprom_get_spur_chan(struct ath_hal *ah,
    int spur_chan, HAL_BOOL is_2ghz);
#ifdef UNUSED
static inline HAL_BOOL ar9300_fill_eeprom(struct ath_hal *ah);
static inline HAL_STATUS ar9300_check_eeprom(struct ath_hal *ah);
#endif

static ar9300_eeprom_t *default9300[] =
{
    &ar9300_template_generic,
    &ar9300_template_xb112,
    &ar9300_template_hb116,
    &ar9300_template_hb112,
    &ar9300_template_xb113,
    &ar9300_template_ap121,
    &ar9300_template_wasp_2,
    &ar9300_template_wasp_k31,
    &ar9300_template_osprey_k31,
    &ar9300_template_aphrodite,
};

/*
 * Different types of memory where the calibration data might be stored.
 * All types are searched in ar9300_eeprom_restore()
 * in the order flash, eeprom, otp.
 * To disable searching a type, set its parameter to 0.
 */

/*
 * This is where we look for the calibration data.
 * must be set before ath_attach() is called
 */
static int calibration_data_try = calibration_data_none;
static int calibration_data_try_address = 0;

/*
 * Set the type of memory used to store calibration data.
 * Used by nart to force reading/writing of a specific type.
 * The driver can normally allow autodetection
 * by setting source to calibration_data_none=0.
 */
void ar9300_calibration_data_set(struct ath_hal *ah, int32_t source)
{
    if (ah != 0) {
        AH9300(ah)->calibration_data_source = source;
    } else {
        calibration_data_try = source;
    }
}

int32_t ar9300_calibration_data_get(struct ath_hal *ah)
{
    if (ah != 0) {
        return AH9300(ah)->calibration_data_source;
    } else {
        return calibration_data_try;
    }
}

/*
 * Set the address of first byte used to store calibration data.
 * Used by nart to force reading/writing at a specific address.
 * The driver can normally allow autodetection by setting size=0.
 */
void ar9300_calibration_data_address_set(struct ath_hal *ah, int32_t size)
{
    if (ah != 0) {
        AH9300(ah)->calibration_data_source_address = size;
    } else {
        calibration_data_try_address = size;
    }
}

int32_t ar9300_calibration_data_address_get(struct ath_hal *ah)
{
    if (ah != 0) {
        return AH9300(ah)->calibration_data_source_address;
    } else {
        return calibration_data_try_address;
    }
}

/*
 * This is the template that is loaded if ar9300_eeprom_restore()
 * can't find valid data in the memory.
 */
static int Ar9300_eeprom_template_preference = ar9300_eeprom_template_generic;

void ar9300_eeprom_template_preference(int32_t value)
{
    Ar9300_eeprom_template_preference = value;
}

/*
 * Install the specified default template.
 * Overwrites any existing calibration and configuration information in memory.
 */
int32_t ar9300_eeprom_template_install(struct ath_hal *ah, int32_t value)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    ar9300_eeprom_t *mptr, *dptr;
    int mdata_size;

    mptr = &ahp->ah_eeprom;
    mdata_size = ar9300_eeprom_struct_size();
    if (mptr != 0) {
#if 0
        calibration_data_source = calibration_data_none;
        calibration_data_source_address = 0;
#endif
        dptr = ar9300_eeprom_struct_default_find_by_id(value);
        if (dptr != 0) {
            OS_MEMCPY(mptr, dptr, mdata_size);
            return 0;
        }
    }
    return -1;
}

static int
ar9300_eeprom_restore_something(struct ath_hal *ah, ar9300_eeprom_t *mptr,
    int mdata_size)
{
    int it;
    ar9300_eeprom_t *dptr;
    int nptr;

    nptr = -1; 
    /*
     * if we didn't find any blocks in the memory,
     * put the prefered template in place
     */
    if (nptr < 0) {
        AH9300(ah)->calibration_data_source = calibration_data_none;
        AH9300(ah)->calibration_data_source_address = 0;
        dptr = ar9300_eeprom_struct_default_find_by_id(
            Ar9300_eeprom_template_preference);
        if (dptr != 0) {
            OS_MEMCPY(mptr, dptr, mdata_size);    
            nptr = 0;
        }
    }
    /*
     * if we didn't find the prefered one,
     * put the normal default template in place
     */
    if (nptr < 0) {
        AH9300(ah)->calibration_data_source = calibration_data_none;
        AH9300(ah)->calibration_data_source_address = 0;
        dptr = ar9300_eeprom_struct_default_find_by_id(
            ar9300_eeprom_template_default);
        if (dptr != 0) {
            OS_MEMCPY(mptr, dptr, mdata_size);    
            nptr = 0;
        }
    }
    /*
     * if we can't find the best template, put any old template in place
     * presume that newer ones are better, so search backwards
     */
    if (nptr < 0) {
        AH9300(ah)->calibration_data_source = calibration_data_none;
        AH9300(ah)->calibration_data_source_address = 0;
        for (it = ar9300_eeprom_struct_default_many() - 1; it >= 0; it--) {
            dptr = ar9300_eeprom_struct_default(it);
            if (dptr != 0) {
                OS_MEMCPY(mptr, dptr, mdata_size);    
                nptr = 0;
                break;
            }
        }
    }
    return nptr;
}

/*
 * Read 16 bits of data from offset into *data
 */
HAL_BOOL
ar9300_eeprom_read_word(struct ath_hal *ah, u_int off, u_int16_t *data)
{
    if (AR_SREV_OSPREY(ah) || AR_SREV_POSEIDON(ah))
    {
        (void) OS_REG_READ(ah, AR9300_EEPROM_OFFSET + (off << AR9300_EEPROM_S));
        if (!ath_hal_wait(ah,
			  AR_HOSTIF_REG(ah, AR_EEPROM_STATUS_DATA),
			  AR_EEPROM_STATUS_DATA_BUSY | AR_EEPROM_STATUS_DATA_PROT_ACCESS,
			  0))
	{
            return AH_FALSE;
	}
        *data = MS(OS_REG_READ(ah,
			       AR_HOSTIF_REG(ah, AR_EEPROM_STATUS_DATA)), AR_EEPROM_STATUS_DATA_VAL);
       return AH_TRUE;
    }
    else
    {
        *data = 0;
        return AH_FALSE;
    }
}


HAL_BOOL
ar9300_otp_read(struct ath_hal *ah, u_int off, u_int32_t *data, HAL_BOOL is_wifi)
{
    int time_out = 1000;
    int status = 0;
    u_int32_t addr;

    if (AR_SREV_HONEYBEE(ah)){ /* no OTP for Honeybee */
        return false;
    }
    addr = (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah))?
        OTP_MEM_START_ADDRESS_WASP : OTP_MEM_START_ADDRESS;
	if (!is_wifi) {
        addr = BTOTP_MEM_START_ADDRESS;
    }
    addr += off * 4; /* OTP is 32 bit addressable */
    (void) OS_REG_READ(ah, addr);

    addr = (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) ?
        OTP_STATUS0_OTP_SM_BUSY_WASP : OTP_STATUS0_OTP_SM_BUSY;
	if (!is_wifi) {
        addr = BTOTP_STATUS0_OTP_SM_BUSY;
    }
    while ((time_out > 0) && (!status)) { /* wait for access complete */
        /* Read data valid, access not busy, sm not busy */
        status = ((OS_REG_READ(ah, addr) & 0x7) == 0x4) ? 1 : 0;
        time_out--;
    }
    if (time_out == 0) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: Timed out during OTP Status0 validation\n", __func__);
        return AH_FALSE;
    }

    addr = (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) ?
        OTP_STATUS1_EFUSE_READ_DATA_WASP : OTP_STATUS1_EFUSE_READ_DATA;
	if (!is_wifi) {
        addr = BTOTP_STATUS1_EFUSE_READ_DATA;
    }
    *data = OS_REG_READ(ah, addr);
    return AH_TRUE;
}




static HAL_STATUS
ar9300_flash_map(struct ath_hal *ah)
{
    /* XXX disable flash remapping for now (ie, SoC support) */
    ath_hal_printf(ah, "%s: unimplemented for now\n", __func__);
#if 0
    struct ath_hal_9300 *ahp = AH9300(ah);
#if defined(AR9100) || defined(__NetBSD__)
    ahp->ah_cal_mem = OS_REMAP(ah, AR9300_EEPROM_START_ADDR, AR9300_EEPROM_MAX);
#else
    ahp->ah_cal_mem = OS_REMAP((uintptr_t)(AH_PRIVATE(ah)->ah_st),
        (AR9300_EEPROM_MAX + AR9300_FLASH_CAL_START_OFFSET));
#endif
    if (!ahp->ah_cal_mem) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: cannot remap eeprom region \n", __func__);
        return HAL_EIO;
    }
#endif
    return HAL_OK;
}

HAL_BOOL
ar9300_flash_read(struct ath_hal *ah, u_int off, u_int16_t *data)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    *data = ((u_int16_t *)ahp->ah_cal_mem)[off];
    return AH_TRUE;
}

HAL_BOOL
ar9300_flash_write(struct ath_hal *ah, u_int off, u_int16_t data)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ((u_int16_t *)ahp->ah_cal_mem)[off] = data;
    return AH_TRUE;
}

HAL_STATUS
ar9300_eeprom_attach(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    ahp->try_dram = 1;
    ahp->try_eeprom = 1;
    ahp->try_otp = 1;
#ifdef ATH_CAL_NAND_FLASH
    ahp->try_nand = 1;
#else
    ahp->try_flash = 1;
#endif
    ahp->calibration_data_source = calibration_data_none;
    ahp->calibration_data_source_address = 0;
    ahp->calibration_data_try = calibration_data_try;
    ahp->calibration_data_try_address = 0;

    /*
     * In case flash will be used for EEPROM. Otherwise ahp->ah_cal_mem
     * must be set to NULL or the real EEPROM address.
     */
    ar9300_flash_map(ah);
    /*
     * ###### This function always return NO SPUR.
     * This is not true for many board designs.
     * Does anyone use this?
     */
    AH_PRIVATE(ah)->ah_getSpurChan = ar9300_eeprom_get_spur_chan;

#ifdef OLDCODE
    /* XXX Needs to be moved for dynamic selection */
    ahp->ah_eeprom = *(default9300[ar9300_eeprom_template_default]);


    if (AR_SREV_HORNET(ah)) {
        /* Set default values for Hornet. */
        ahp->ah_eeprom.base_eep_header.op_cap_flags.op_flags =
            AR9300_OPFLAGS_11G;
        ahp->ah_eeprom.base_eep_header.txrx_mask = 0x11;
    } else if (AR_SREV_POSEIDON(ah)) {
        /* Set default values for Poseidon. */
        ahp->ah_eeprom.base_eep_header.op_cap_flags.op_flags =
            AR9300_OPFLAGS_11G;
        ahp->ah_eeprom.base_eep_header.txrx_mask = 0x11;
    }

    if (AH_PRIVATE(ah)->ah_config.ath_hal_skip_eeprom_read) {
        ahp->ah_emu_eeprom = 1;
        return HAL_OK;
    }

    ahp->ah_emu_eeprom = 1;

#ifdef UNUSED
#endif
    
    if (!ar9300_fill_eeprom(ah)) {
        return HAL_EIO;
    }

    return HAL_OK;
    /* return ar9300_check_eeprom(ah); */
#else
    ahp->ah_emu_eeprom = 1;

#if 0
/*#ifdef MDK_AP*/ /* MDK_AP is defined only in NART AP build */
    u_int8_t buffer[10];
    int caldata_check = 0;

    ar9300_calibration_data_read_flash(
        ah, FLASH_BASE_CALDATA_OFFSET, buffer, 4);
    printf("flash caldata:: %x\n", buffer[0]);
    if (buffer[0] != 0xff) {
        caldata_check = 1;
    }
    if (!caldata_check) {
        ar9300_eeprom_t *mptr;
        int mdata_size;
        if (AR_SREV_HORNET(ah)) {
            /* XXX: For initial testing */
            mptr = &ahp->ah_eeprom;
            mdata_size = ar9300_eeprom_struct_size();
            ahp->ah_eeprom = ar9300_template_ap121;
            ahp->ah_emu_eeprom = 1;
            /* need it to let art save in to flash ????? */
            calibration_data_source = calibration_data_flash;
        } else if (AR_SREV_WASP(ah)) {
            /* XXX: For initial testing */
            ath_hal_printf(ah, " wasp eep attach\n");
            mptr = &ahp->ah_eeprom;
            mdata_size = ar9300_eeprom_struct_size();
            ahp->ah_eeprom = ar9300_template_generic;
            ahp->ah_eeprom.mac_addr[0] = 0x00;
            ahp->ah_eeprom.mac_addr[1] = 0x03;
            ahp->ah_eeprom.mac_addr[2] = 0x7F;
            ahp->ah_eeprom.mac_addr[3] = 0xBA;
            ahp->ah_eeprom.mac_addr[4] = 0xD0;
            ahp->ah_eeprom.mac_addr[5] = 0x00;
            ahp->ah_emu_eeprom = 1;
            ahp->ah_eeprom.base_eep_header.txrx_mask = 0x33;
            ahp->ah_eeprom.base_eep_header.txrxgain = 0x10;
            /* need it to let art save in to flash ????? */
            calibration_data_source = calibration_data_flash;
        }
        return HAL_OK;
    }
#endif
    if (AR_SREV_HORNET(ah) || AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)
        || AR_SREV_HONEYBEE(ah)) {
        ahp->try_eeprom = 0;
    }

    if (AR_SREV_HONEYBEE(ah)) {
        ahp->try_otp = 0;
    }

    if (!ar9300_eeprom_restore(ah)) {
        return HAL_EIO;
    }
    return HAL_OK;
#endif
}

u_int32_t
ar9300_eeprom_get(struct ath_hal_9300 *ahp, EEPROM_PARAM param)
{
    ar9300_eeprom_t *eep = &ahp->ah_eeprom;
    OSPREY_BASE_EEP_HEADER *p_base = &eep->base_eep_header;
    OSPREY_BASE_EXTENSION_1 *base_ext1 = &eep->base_ext1;

    switch (param) {
#ifdef NOTYET
    case EEP_NFTHRESH_5:
        return p_modal[0].noise_floor_thresh_ch[0];
    case EEP_NFTHRESH_2:
        return p_modal[1].noise_floor_thresh_ch[0];
#endif
    case EEP_MAC_LSW:
        return eep->mac_addr[0] << 8 | eep->mac_addr[1];
    case EEP_MAC_MID:
        return eep->mac_addr[2] << 8 | eep->mac_addr[3];
    case EEP_MAC_MSW:
        return eep->mac_addr[4] << 8 | eep->mac_addr[5];
    case EEP_REG_0:
        return p_base->reg_dmn[0];
    case EEP_REG_1:
        return p_base->reg_dmn[1];
    case EEP_OP_CAP:
        return p_base->device_cap;
    case EEP_OP_MODE:
        return p_base->op_cap_flags.op_flags;
    case EEP_RF_SILENT:
        return p_base->rf_silent;
#ifdef NOTYET
    case EEP_OB_5:
        return p_modal[0].ob;
    case EEP_DB_5:
        return p_modal[0].db;
    case EEP_OB_2:
        return p_modal[1].ob;
    case EEP_DB_2:
        return p_modal[1].db;
    case EEP_MINOR_REV:
        return p_base->eeprom_version & AR9300_EEP_VER_MINOR_MASK;
#endif
    case EEP_TX_MASK:
        return (p_base->txrx_mask >> 4) & 0xf;
    case EEP_RX_MASK:
        return p_base->txrx_mask & 0xf;
#ifdef NOTYET
    case EEP_FSTCLK_5G:
        return p_base->fast_clk5g;
    case EEP_RXGAIN_TYPE:
        return p_base->rx_gain_type;
#endif
    case EEP_DRIVE_STRENGTH:
#define AR9300_EEP_BASE_DRIVE_STRENGTH    0x1 
        return p_base->misc_configuration & AR9300_EEP_BASE_DRIVE_STRENGTH;
    case EEP_INTERNAL_REGULATOR:
        /* Bit 4 is internal regulator flag */
        return ((p_base->feature_enable & 0x10) >> 4);
    case EEP_SWREG:
        return (p_base->swreg);
    case EEP_PAPRD_ENABLED:
        /* Bit 5 is paprd flag */
        return ((p_base->feature_enable & 0x20) >> 5);
    case EEP_ANTDIV_control:
        return (u_int32_t)(base_ext1->ant_div_control);
    case EEP_CHAIN_MASK_REDUCE:
        return ((p_base->misc_configuration >> 3) & 0x1);
    case EEP_OL_PWRCTRL:
        return 0;
     case EEP_DEV_TYPE:
        return p_base->device_type;
    default:
        HALASSERT(0);
        return 0;
    }
}



/******************************************************************************/
/*!
**  \brief EEPROM fixup code for INI values
**
** This routine provides a place to insert "fixup" code for specific devices
** that need to modify INI values based on EEPROM values, BEFORE the INI values
** are written.
** Certain registers in the INI file can only be written once without
** undesired side effects, and this provides a place for EEPROM overrides
** in these cases.
**
** This is called at attach time once.  It should not affect run time
** performance at all
**
**  \param ah       Pointer to HAL object (this)
**  \param p_eep_data Pointer to (filled in) eeprom data structure
**  \param reg      register being inspected on this call
**  \param value    value in INI file
**
**  \return Updated value for INI file.
*/
u_int32_t
ar9300_ini_fixup(struct ath_hal *ah, ar9300_eeprom_t *p_eep_data,
    u_int32_t reg, u_int32_t value)
{
    HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
        "ar9300_eeprom_def_ini_fixup: FIXME\n");
#if 0
    BASE_EEPDEF_HEADER  *p_base  = &(p_eep_data->base_eep_header);

    switch (AH_PRIVATE(ah)->ah_devid)
    {
    case AR9300_DEVID_AR9300_PCI:
        /*
        ** Need to set the external/internal regulator bit to the proper value.
        ** Can only write this ONCE.
        */

        if ( reg == 0x7894 )
        {
            /*
            ** Check for an EEPROM data structure of "0x0b" or better
            */

            HALDEBUG(ah, HAL_DEBUG_EEPROM, "ini VAL: %x  EEPROM: %x\n",
                     value, (p_base->version & 0xff));

            if ( (p_base->version & 0xff) > 0x0a) {
                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "PWDCLKIND: %d\n", p_base->pwdclkind);
                value &= ~AR_AN_TOP2_PWDCLKIND;
                value |=
                    AR_AN_TOP2_PWDCLKIND &
                    (p_base->pwdclkind <<  AR_AN_TOP2_PWDCLKIND_S);
            } else {
                HALDEBUG(ah, HAL_DEBUG_EEPROM, "PWDCLKIND Earlier Rev\n");
            }

            HALDEBUG(ah, HAL_DEBUG_EEPROM, "final ini VAL: %x\n", value);
        }
        break;

    }

    return (value);
#else
    return 0;
#endif
}

/*
 * Returns the interpolated y value corresponding to the specified x value
 * from the np ordered pairs of data (px,py).
 * The pairs do not have to be in any order.
 * If the specified x value is less than any of the px,
 * the returned y value is equal to the py for the lowest px.
 * If the specified x value is greater than any of the px,
 * the returned y value is equal to the py for the highest px.
 */
static int
interpolate(int32_t x, int32_t *px, int32_t *py, u_int16_t np)
{
    int ip = 0;
    int lx = 0, ly = 0, lhave = 0;
    int hx = 0, hy = 0, hhave = 0;
    int dx = 0;
    int y = 0;
    int bf, factor, plus;

    lhave = 0;
    hhave = 0;
    /*
     * identify best lower and higher x calibration measurement
     */
    for (ip = 0; ip < np; ip++) {
        dx = x - px[ip];
        /* this measurement is higher than our desired x */
        if (dx <= 0) {
            if (!hhave || dx > (x - hx)) {
                /* new best higher x measurement */
                hx = px[ip];
                hy = py[ip];
                hhave = 1;
            }
        }
        /* this measurement is lower than our desired x */
        if (dx >= 0) {
            if (!lhave || dx < (x - lx)) {
                /* new best lower x measurement */
                lx = px[ip];
                ly = py[ip];
                lhave = 1;
            }
        }
    }
    /* the low x is good */
    if (lhave) {
        /* so is the high x */
        if (hhave) {
            /* they're the same, so just pick one */
            if (hx == lx) {
                y = ly;
            } else {
                /* interpolate with round off */
                bf = (2 * (hy - ly) * (x - lx)) / (hx - lx);
                plus = (bf % 2);
                factor = bf / 2;
                y = ly + factor + plus;
            }
        } else {
            /* only low is good, use it */
            y = ly;
        }
    } else if (hhave) {
        /* only high is good, use it */
        y = hy;
    } else {
        /* nothing is good,this should never happen unless np=0, ????  */
        y = -(1 << 30);
    }

    return y;
}

u_int8_t
ar9300_eeprom_get_legacy_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index,
    u_int16_t freq, HAL_BOOL is_2ghz)
{
    u_int16_t            num_piers, i;
    int32_t              target_power_array[OSPREY_NUM_5G_20_TARGET_POWERS];
    int32_t              freq_array[OSPREY_NUM_5G_20_TARGET_POWERS]; 
    u_int8_t             *p_freq_bin;
    ar9300_eeprom_t      *eep = &AH9300(ah)->ah_eeprom;
    CAL_TARGET_POWER_LEG *p_eeprom_target_pwr;

    if (is_2ghz) {
        num_piers = OSPREY_NUM_2G_20_TARGET_POWERS;    
        p_eeprom_target_pwr = eep->cal_target_power_2g;
        p_freq_bin = eep->cal_target_freqbin_2g;
    } else {
        num_piers = OSPREY_NUM_5G_20_TARGET_POWERS;
        p_eeprom_target_pwr = eep->cal_target_power_5g;
        p_freq_bin = eep->cal_target_freqbin_5g;
   }

    /*
     * create array of channels and targetpower from
     * targetpower piers stored on eeprom
     */
    for (i = 0; i < num_piers; i++) {
        freq_array[i] = FBIN2FREQ(p_freq_bin[i], is_2ghz);
        target_power_array[i] = p_eeprom_target_pwr[i].t_pow2x[rate_index];
    }

    /* interpolate to get target power for given frequency */
    return
        ((u_int8_t)interpolate(
            (int32_t)freq, freq_array, target_power_array, num_piers));
}

u_int8_t
ar9300_eeprom_get_ht20_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index,
    u_int16_t freq, HAL_BOOL is_2ghz)
{
    u_int16_t               num_piers, i;
    int32_t                 target_power_array[OSPREY_NUM_5G_20_TARGET_POWERS];
    int32_t                 freq_array[OSPREY_NUM_5G_20_TARGET_POWERS]; 
    u_int8_t                *p_freq_bin;
    ar9300_eeprom_t         *eep = &AH9300(ah)->ah_eeprom;
    OSP_CAL_TARGET_POWER_HT *p_eeprom_target_pwr;

    if (is_2ghz) {
        num_piers = OSPREY_NUM_2G_20_TARGET_POWERS;    
        p_eeprom_target_pwr = eep->cal_target_power_2g_ht20;
        p_freq_bin = eep->cal_target_freqbin_2g_ht20;
    } else {
        num_piers = OSPREY_NUM_5G_20_TARGET_POWERS;
        p_eeprom_target_pwr = eep->cal_target_power_5g_ht20;
        p_freq_bin = eep->cal_target_freqbin_5g_ht20;
    }

    /*
     * create array of channels and targetpower from
     * targetpower piers stored on eeprom
     */
    for (i = 0; i < num_piers; i++) {
        freq_array[i] = FBIN2FREQ(p_freq_bin[i], is_2ghz);
        target_power_array[i] = p_eeprom_target_pwr[i].t_pow2x[rate_index];
    }

    /* interpolate to get target power for given frequency */
    return
        ((u_int8_t)interpolate(
            (int32_t)freq, freq_array, target_power_array, num_piers));
}

u_int8_t
ar9300_eeprom_get_ht40_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index,
    u_int16_t freq, HAL_BOOL is_2ghz)
{
    u_int16_t               num_piers, i;
    int32_t                 target_power_array[OSPREY_NUM_5G_40_TARGET_POWERS];
    int32_t                 freq_array[OSPREY_NUM_5G_40_TARGET_POWERS]; 
    u_int8_t                *p_freq_bin;
    ar9300_eeprom_t         *eep = &AH9300(ah)->ah_eeprom;
    OSP_CAL_TARGET_POWER_HT *p_eeprom_target_pwr;

    if (is_2ghz) {
        num_piers = OSPREY_NUM_2G_40_TARGET_POWERS;    
        p_eeprom_target_pwr = eep->cal_target_power_2g_ht40;
        p_freq_bin = eep->cal_target_freqbin_2g_ht40;
    } else {
        num_piers = OSPREY_NUM_5G_40_TARGET_POWERS;
        p_eeprom_target_pwr = eep->cal_target_power_5g_ht40;
        p_freq_bin = eep->cal_target_freqbin_5g_ht40;
    }

    /*
     * create array of channels and targetpower from
     * targetpower piers stored on eeprom
     */
    for (i = 0; i < num_piers; i++) {
        freq_array[i] = FBIN2FREQ(p_freq_bin[i], is_2ghz);
        target_power_array[i] = p_eeprom_target_pwr[i].t_pow2x[rate_index];
    }

    /* interpolate to get target power for given frequency */
    return
        ((u_int8_t)interpolate(
            (int32_t)freq, freq_array, target_power_array, num_piers));
}

u_int8_t
ar9300_eeprom_get_cck_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index,
    u_int16_t freq)
{
    u_int16_t            num_piers = OSPREY_NUM_2G_CCK_TARGET_POWERS, i;
    int32_t              target_power_array[OSPREY_NUM_2G_CCK_TARGET_POWERS];
    int32_t              freq_array[OSPREY_NUM_2G_CCK_TARGET_POWERS]; 
    ar9300_eeprom_t      *eep = &AH9300(ah)->ah_eeprom;
    u_int8_t             *p_freq_bin = eep->cal_target_freqbin_cck;
    CAL_TARGET_POWER_LEG *p_eeprom_target_pwr = eep->cal_target_power_cck;

    /*
     * create array of channels and targetpower from
     * targetpower piers stored on eeprom
     */
    for (i = 0; i < num_piers; i++) {
        freq_array[i] = FBIN2FREQ(p_freq_bin[i], 1);
        target_power_array[i] = p_eeprom_target_pwr[i].t_pow2x[rate_index];
    }

    /* interpolate to get target power for given frequency */
    return
        ((u_int8_t)interpolate(
            (int32_t)freq, freq_array, target_power_array, num_piers));
}

/*
 * Set tx power registers to array of values passed in
 */
int
ar9300_transmit_power_reg_write(struct ath_hal *ah, u_int8_t *p_pwr_array) 
{   
#define POW_SM(_r, _s)     (((_r) & 0x3f) << (_s))
    /* make sure forced gain is not set */
#if 0
    field_write("force_dac_gain", 0);
    OS_REG_WRITE(ah, 0xa3f8, 0);
    field_write("force_tx_gain", 0);
#endif

    OS_REG_WRITE(ah, 0xa458, 0);

    /* Write the OFDM power per rate set */
    /* 6 (LSB), 9, 12, 18 (MSB) */
    OS_REG_WRITE(ah, 0xa3c0,
        POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], 16)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24],  0)
    );
    /* 24 (LSB), 36, 48, 54 (MSB) */
    OS_REG_WRITE(ah, 0xa3c4,
        POW_SM(p_pwr_array[ALL_TARGET_LEGACY_54], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_48], 16)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_36],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24],  0)
    );

    /* Write the CCK power per rate set */
    /* 1L (LSB), reserved, 2L, 2S (MSB) */  
    OS_REG_WRITE(ah, 0xa3c8,
        POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  16)
/*          | POW_SM(tx_power_times2,  8)*/ /* this is reserved for Osprey */
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],   0)
    );
    /* 5.5L (LSB), 5.5S, 11L, 11S (MSB) */
    OS_REG_WRITE(ah, 0xa3cc,
        POW_SM(p_pwr_array[ALL_TARGET_LEGACY_11S], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_11L], 16)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_5S],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  0)
    );

	/* write the power for duplicated frames - HT40 */
	/* dup40_cck (LSB), dup40_ofdm, ext20_cck, ext20_ofdm  (MSB) */
    OS_REG_WRITE(ah, 0xa3e0,
        POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], 16)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  0)
    );

    /* Write the HT20 power per rate set */
    /* 0/8/16 (LSB), 1-3/9-11/17-19, 4, 5 (MSB) */
    OS_REG_WRITE(ah, 0xa3d0,
        POW_SM(p_pwr_array[ALL_TARGET_HT20_5], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_4],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_1_3_9_11_17_19],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_0_8_16],   0)
    );
    
    /* 6 (LSB), 7, 12, 13 (MSB) */
    OS_REG_WRITE(ah, 0xa3d4,
        POW_SM(p_pwr_array[ALL_TARGET_HT20_13], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_12],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_7],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_6],   0)
    );

    /* 14 (LSB), 15, 20, 21 */
    OS_REG_WRITE(ah, 0xa3e4,
        POW_SM(p_pwr_array[ALL_TARGET_HT20_21], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_20],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_15],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_14],   0)
    );

    /* Mixed HT20 and HT40 rates */
    /* HT20 22 (LSB), HT20 23, HT40 22, HT40 23 (MSB) */
    OS_REG_WRITE(ah, 0xa3e8,
        POW_SM(p_pwr_array[ALL_TARGET_HT40_23], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_22],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_23],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT20_22],   0)
    );
    
    /* Write the HT40 power per rate set */
    /* correct PAR difference between HT40 and HT20/LEGACY */
    /* 0/8/16 (LSB), 1-3/9-11/17-19, 4, 5 (MSB) */
    OS_REG_WRITE(ah, 0xa3d8,
        POW_SM(p_pwr_array[ALL_TARGET_HT40_5], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_4],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_1_3_9_11_17_19],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_0_8_16],   0)
    );

    /* 6 (LSB), 7, 12, 13 (MSB) */
    OS_REG_WRITE(ah, 0xa3dc,
        POW_SM(p_pwr_array[ALL_TARGET_HT40_13], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_12],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_7], 8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_6], 0)
    );

    /* 14 (LSB), 15, 20, 21 */
    OS_REG_WRITE(ah, 0xa3ec,
        POW_SM(p_pwr_array[ALL_TARGET_HT40_21], 24)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_20],  16)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_15],  8)
          | POW_SM(p_pwr_array[ALL_TARGET_HT40_14],   0)
    );

    return 0;
#undef POW_SM    
}

static void
ar9300_selfgen_tpc_reg_write(struct ath_hal *ah, const struct ieee80211_channel *chan,
                             u_int8_t *p_pwr_array) 
{
    u_int32_t tpc_reg_val;

    /* Set the target power values for self generated frames (ACK,RTS/CTS) to
     * be within limits. This is just a safety measure.With per packet TPC mode
     * enabled the target power value used with self generated frames will be
     * MIN( TPC reg, BB_powertx_rate register)
     */
    
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        tpc_reg_val = (SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], AR_TPC_ACK) |
                       SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], AR_TPC_CTS) |
                       SM(0x3f, AR_TPC_CHIRP) |
                       SM(0x3f, AR_TPC_RPT));
    } else {
        tpc_reg_val = (SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], AR_TPC_ACK) |
                       SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], AR_TPC_CTS) |
                       SM(0x3f, AR_TPC_CHIRP) |
                       SM(0x3f, AR_TPC_RPT));
    }
    OS_REG_WRITE(ah, AR_TPC, tpc_reg_val);
}

void
ar9300_set_target_power_from_eeprom(struct ath_hal *ah, u_int16_t freq,
    u_int8_t *target_power_val_t2)
{
    /* hard code for now, need to get from eeprom struct */
    u_int8_t ht40_power_inc_for_pdadc = 0;
    HAL_BOOL  is_2ghz = 0;
    
    if (freq < 4000) {
        is_2ghz = 1;
    }

    target_power_val_t2[ALL_TARGET_LEGACY_6_24] =
        ar9300_eeprom_get_legacy_trgt_pwr(
            ah, LEGACY_TARGET_RATE_6_24, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_LEGACY_36] =
        ar9300_eeprom_get_legacy_trgt_pwr(
            ah, LEGACY_TARGET_RATE_36, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_LEGACY_48] =
        ar9300_eeprom_get_legacy_trgt_pwr(
            ah, LEGACY_TARGET_RATE_48, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_LEGACY_54] =
        ar9300_eeprom_get_legacy_trgt_pwr(
            ah, LEGACY_TARGET_RATE_54, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_LEGACY_1L_5L] =
        ar9300_eeprom_get_cck_trgt_pwr(
            ah, LEGACY_TARGET_RATE_1L_5L, freq);
    target_power_val_t2[ALL_TARGET_LEGACY_5S] =
        ar9300_eeprom_get_cck_trgt_pwr(
            ah, LEGACY_TARGET_RATE_5S, freq);
    target_power_val_t2[ALL_TARGET_LEGACY_11L] =
        ar9300_eeprom_get_cck_trgt_pwr(
            ah, LEGACY_TARGET_RATE_11L, freq);
    target_power_val_t2[ALL_TARGET_LEGACY_11S] =
        ar9300_eeprom_get_cck_trgt_pwr(
            ah, LEGACY_TARGET_RATE_11S, freq);
    target_power_val_t2[ALL_TARGET_HT20_0_8_16] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_0_8_16, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_1_3_9_11_17_19] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_1_3_9_11_17_19, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_4] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_4, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_5] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_5, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_6] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_6, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_7] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_7, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_12] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_12, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_13] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_13, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_14] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_14, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_15] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_15, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_20] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_20, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_21] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_21, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_22] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_22, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT20_23] =
        ar9300_eeprom_get_ht20_trgt_pwr(
            ah, HT_TARGET_RATE_23, freq, is_2ghz);
    target_power_val_t2[ALL_TARGET_HT40_0_8_16] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_0_8_16, freq, is_2ghz) +
        ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_1_3_9_11_17_19] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_1_3_9_11_17_19, freq, is_2ghz) +
        ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_4] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_4, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_5] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_5, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_6] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_6, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_7] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_7, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_12] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_12, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_13] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_13, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_14] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_14, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_15] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_15, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_20] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_20, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_21] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_21, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_22] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_22, freq, is_2ghz) + ht40_power_inc_for_pdadc;
    target_power_val_t2[ALL_TARGET_HT40_23] =
        ar9300_eeprom_get_ht40_trgt_pwr(
            ah, HT_TARGET_RATE_23, freq, is_2ghz) + ht40_power_inc_for_pdadc;

#ifdef AH_DEBUG
    {
        int  i = 0;

        HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: APPLYING TARGET POWERS\n", __func__);
        while (i < ar9300_rate_size) {
            HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: TPC[%02d] 0x%08x ",
                     __func__, i, target_power_val_t2[i]);
            i++;
			if (i == ar9300_rate_size) {
                break;
			}
            HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: TPC[%02d] 0x%08x ",
                     __func__, i, target_power_val_t2[i]);
            i++;
			if (i == ar9300_rate_size) {
                break;
			}
            HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: TPC[%02d] 0x%08x ",
                     __func__, i, target_power_val_t2[i]);
            i++;
			if (i == ar9300_rate_size) {
                break;
			}
            HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: TPC[%02d] 0x%08x \n",
                     __func__, i, target_power_val_t2[i]);
            i++;
        }
    }
#endif
} 

u_int16_t *ar9300_regulatory_domain_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    return eep->base_eep_header.reg_dmn;
}


int32_t 
ar9300_eeprom_write_enable_gpio_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    return eep->base_eep_header.eeprom_write_enable_gpio;
}

int32_t 
ar9300_wlan_disable_gpio_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    return eep->base_eep_header.wlan_disable_gpio;
}

int32_t 
ar9300_wlan_led_gpio_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    return eep->base_eep_header.wlan_led_gpio;
}

int32_t 
ar9300_rx_band_select_gpio_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    return eep->base_eep_header.rx_band_select_gpio;
}

/*
 * since valid noise floor values are negative, returns 1 on error
 */
int32_t
ar9300_noise_floor_cal_or_power_get(struct ath_hal *ah, int32_t frequency,
    int32_t ichain, HAL_BOOL use_cal)
{
    int     nf_use = 1; /* start with an error return value */
    int32_t fx[OSPREY_NUM_5G_CAL_PIERS + OSPREY_NUM_2G_CAL_PIERS];
    int32_t nf[OSPREY_NUM_5G_CAL_PIERS + OSPREY_NUM_2G_CAL_PIERS];
    int     nnf;
    int     is_2ghz;
    int     ipier, npier;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    u_int8_t        *p_cal_pier;
    OSP_CAL_DATA_PER_FREQ_OP_LOOP *p_cal_pier_struct;

    /*
     * check chain value
     */
    if (ichain < 0 || ichain >= OSPREY_MAX_CHAINS) {
        return 1;
    }

    /* figure out which band we're using */
    is_2ghz = (frequency < 4000);
    if (is_2ghz) {
        npier = OSPREY_NUM_2G_CAL_PIERS;
        p_cal_pier = eep->cal_freq_pier_2g;
        p_cal_pier_struct = eep->cal_pier_data_2g[ichain];
    } else {
        npier = OSPREY_NUM_5G_CAL_PIERS;
        p_cal_pier = eep->cal_freq_pier_5g;
        p_cal_pier_struct = eep->cal_pier_data_5g[ichain];
    }
    /* look for valid noise floor values */
    nnf = 0;
    for (ipier = 0; ipier < npier; ipier++) {
        fx[nnf] = FBIN2FREQ(p_cal_pier[ipier], is_2ghz);
        nf[nnf] = use_cal ?
            p_cal_pier_struct[ipier].rx_noisefloor_cal :
            p_cal_pier_struct[ipier].rx_noisefloor_power;
        if (nf[nnf] < 0) {
            nnf++;
        }
    }
    /*
     * If we have some valid values, interpolate to find the value
     * at the desired frequency.
     */
    if (nnf > 0) {
        nf_use = interpolate(frequency, fx, nf, nnf);
    }

    return nf_use;
}

/*
 * Return the Rx NF offset for specific channel.
 * The values saved in EEPROM/OTP/Flash is converted through the following way:
 *     ((_p) - NOISE_PWR_DATA_OFFSET) << 2
 * So we need to convert back to the original values.
 */
int ar9300_get_rx_nf_offset(struct ath_hal *ah, struct ieee80211_channel *chan, int8_t *nf_pwr, int8_t *nf_cal) {
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    int8_t rx_nf_pwr, rx_nf_cal;
    int i; 
    //HALASSERT(ichan);

    /* Fill 0 if valid internal channel is not found */
    if (ichan == AH_NULL) {
        OS_MEMZERO(nf_pwr, sizeof(nf_pwr[0])*OSPREY_MAX_CHAINS);
        OS_MEMZERO(nf_cal, sizeof(nf_cal[0])*OSPREY_MAX_CHAINS);
        return -1;
    }

    for (i = 0; i < OSPREY_MAX_CHAINS; i++) {
	    if ((rx_nf_pwr = ar9300_noise_floor_cal_or_power_get(ah, ichan->channel, i, 0)) == 1) {
	        nf_pwr[i] = 0;
	    } else {
	        //printk("%s: raw nf_pwr[%d] = %d\n", __func__, i, rx_nf_pwr);
            nf_pwr[i] = NOISE_PWR_DBM_2_INT(rx_nf_pwr);
	    }

	    if ((rx_nf_cal = ar9300_noise_floor_cal_or_power_get(ah, ichan->channel, i, 1)) == 1) {
	        nf_cal[i] = 0;
	    } else {
	        //printk("%s: raw nf_cal[%d] = %d\n", __func__, i, rx_nf_cal);
            nf_cal[i] = NOISE_PWR_DBM_2_INT(rx_nf_cal);
	    }
    }

    return 0;
}

int32_t ar9300_rx_gain_index_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    return (eep->base_eep_header.txrxgain) & 0xf;        /* bits 3:0 */
}


int32_t ar9300_tx_gain_index_get(struct ath_hal *ah)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    return (eep->base_eep_header.txrxgain >> 4) & 0xf;    /* bits 7:4 */
}

HAL_BOOL ar9300_internal_regulator_apply(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int internal_regulator = ar9300_eeprom_get(ahp, EEP_INTERNAL_REGULATOR);
    int reg_pmu1, reg_pmu2, reg_pmu1_set, reg_pmu2_set;
    u_int32_t reg_PMU1, reg_PMU2;
    unsigned long eep_addr;
    u_int32_t reg_val, reg_usb = 0, reg_pmu = 0;
    int usb_valid = 0, pmu_valid = 0;
    unsigned char pmu_refv; 

    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        reg_PMU1 = AR_PHY_PMU1_JUPITER;
        reg_PMU2 = AR_PHY_PMU2_JUPITER;
    }
    else {
        reg_PMU1 = AR_PHY_PMU1;
        reg_PMU2 = AR_PHY_PMU2;
    }

    if (internal_regulator) {
        if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah)) {
            if (AR_SREV_HORNET(ah)) {
                /* Read OTP first */
                for (eep_addr = 0x14; ; eep_addr -= 0x10) {

                    ar9300_otp_read(ah, eep_addr / 4, &reg_val, 1);

                    if ((reg_val & 0x80) == 0x80){
                        usb_valid = 1;
                        reg_usb = reg_val & 0x000000ff;
                    }
                    
                    if ((reg_val & 0x80000000) == 0x80000000){
                        pmu_valid = 1;
                        reg_pmu = (reg_val & 0xff000000) >> 24;
                    }

                    if (eep_addr == 0x4) {
                        break;
                    }
                }

                if (pmu_valid) {
                    pmu_refv = reg_pmu & 0xf;
                } else {
                    pmu_refv = 0x8;
                }

                /*
                 * If (valid) {
                 *   Usb_phy_ctrl2_tx_cal_en -> 0
                 *   Usb_phy_ctrl2_tx_cal_sel -> 0
                 *   Usb_phy_ctrl2_tx_man_cal -> 0, 1, 3, 7 or 15 from OTP
                 * }
                 */
                if (usb_valid) {
                    OS_REG_RMW_FIELD(ah, 0x16c88, AR_PHY_CTRL2_TX_CAL_EN, 0x0);
                    OS_REG_RMW_FIELD(ah, 0x16c88, AR_PHY_CTRL2_TX_CAL_SEL, 0x0);
                    OS_REG_RMW_FIELD(ah, 0x16c88, 
                        AR_PHY_CTRL2_TX_MAN_CAL, (reg_usb & 0xf));
                }

            } else {
                pmu_refv = 0x8;
            }
            /*#ifndef USE_HIF*/
            /* Follow the MDK settings for Hornet PMU.
             * my $pwd               = 0x0;
             * my $Nfdiv             = 0x3;  # xtal_freq = 25MHz
             * my $Nfdiv             = 0x4;  # xtal_freq = 40MHz
             * my $Refv              = 0x7;  # 0x5:1.22V; 0x8:1.29V
             * my $Gm1               = 0x3;  #Poseidon $Gm1=1
             * my $classb            = 0x0;
             * my $Cc                = 0x1;  #Poseidon $Cc=7
             * my $Rc                = 0x6;
             * my $ramp_slope        = 0x1;
             * my $Segm              = 0x3;
             * my $use_local_osc     = 0x0;
             * my $force_xosc_stable = 0x0;
             * my $Selfb             = 0x0;  #Poseidon $Selfb=1
             * my $Filterfb          = 0x3;  #Poseidon $Filterfb=0
             * my $Filtervc          = 0x0;
             * my $disc              = 0x0;
             * my $discdel           = 0x4;
             * my $spare             = 0x0;
             * $reg_PMU1 =
             *     $pwd | ($Nfdiv<<1) | ($Refv<<4) | ($Gm1<<8) |
             *     ($classb<<11) | ($Cc<<14) | ($Rc<<17) | ($ramp_slope<<20) |
             *     ($Segm<<24) | ($use_local_osc<<26) |
             *     ($force_xosc_stable<<27) | ($Selfb<<28) | ($Filterfb<<29);
             * $reg_PMU2 = $handle->reg_rd("ch0_PMU2");
             * $reg_PMU2 = ($reg_PMU2 & 0xfe3fffff) | ($Filtervc<<22);
             * $reg_PMU2 = ($reg_PMU2 & 0xe3ffffff) | ($discdel<<26);
             * $reg_PMU2 = ($reg_PMU2 & 0x1fffffff) | ($spare<<29); 
             */
            if (ahp->clk_25mhz) {
                reg_pmu1_set = 0 |
                    (3 <<  1) | (pmu_refv << 4) | (3 <<  8) | (0 << 11) |
                    (1 << 14) | (6 << 17) | (1 << 20) | (3 << 24) |
                    (0 << 26) | (0 << 27) | (0 << 28) | (0 << 29);
            } else {
                if (AR_SREV_POSEIDON(ah)) {
                    reg_pmu1_set = 0 | 
                        (5 <<  1) | (7 <<  4) | (2 <<  8) | (0 << 11) |
                        (2 << 14) | (6 << 17) | (1 << 20) | (3 << 24) |
                        (0 << 26) | (0 << 27) | (1 << 28) | (0 << 29) ;
                } else {
                    reg_pmu1_set = 0 |
                        (4 <<  1) | (7 <<  4) | (3 <<  8) | (0 << 11) |
                        (1 << 14) | (6 << 17) | (1 << 20) | (3 << 24) |
                        (0 << 26) | (0 << 27) | (0 << 28) | (0 << 29) ;
                } 
            }
            OS_REG_RMW_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM, 0x0);

            OS_REG_WRITE(ah, reg_PMU1, reg_pmu1_set);   /* 0x638c8376 */
            reg_pmu1 = OS_REG_READ(ah, reg_PMU1);
            while (reg_pmu1 != reg_pmu1_set) {
                OS_REG_WRITE(ah, reg_PMU1, reg_pmu1_set);  /* 0x638c8376 */
                OS_DELAY(10);
                reg_pmu1 = OS_REG_READ(ah, reg_PMU1);
            }
                                
            reg_pmu2_set =
                 (OS_REG_READ(ah, reg_PMU2) & (~0xFFC00000)) | (4 << 26);
            OS_REG_WRITE(ah, reg_PMU2, reg_pmu2_set);
            reg_pmu2 = OS_REG_READ(ah, reg_PMU2);
            while (reg_pmu2 != reg_pmu2_set) {
                OS_REG_WRITE(ah, reg_PMU2, reg_pmu2_set);
                OS_DELAY(10);
                reg_pmu2 = OS_REG_READ(ah, reg_PMU2);
            }
            reg_pmu2_set =
                 (OS_REG_READ(ah, reg_PMU2) & (~0x00200000)) | (1 << 21);
            OS_REG_WRITE(ah, reg_PMU2, reg_pmu2_set);
            reg_pmu2 = OS_REG_READ(ah, reg_PMU2);
            while (reg_pmu2 != reg_pmu2_set) {
                OS_REG_WRITE(ah, reg_PMU2, reg_pmu2_set);
                OS_DELAY(10);
                reg_pmu2 = OS_REG_READ(ah, reg_PMU2);
            }
            /*#endif*/
        } else if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            /* Internal regulator is ON. Write swreg register. */
            int swreg = ar9300_eeprom_get(ahp, EEP_SWREG);
            OS_REG_WRITE(ah, reg_PMU1, swreg);
        } else {
            /* Internal regulator is ON. Write swreg register. */
            int swreg = ar9300_eeprom_get(ahp, EEP_SWREG);
            OS_REG_WRITE(ah, AR_RTC_REG_CONTROL1,
                         OS_REG_READ(ah, AR_RTC_REG_CONTROL1) &
                         (~AR_RTC_REG_CONTROL1_SWREG_PROGRAM));
            OS_REG_WRITE(ah, AR_RTC_REG_CONTROL0, swreg);
            /* Set REG_CONTROL1.SWREG_PROGRAM */
            OS_REG_WRITE(ah, AR_RTC_REG_CONTROL1,
                OS_REG_READ(ah, AR_RTC_REG_CONTROL1) |
                AR_RTC_REG_CONTROL1_SWREG_PROGRAM);
        }
    } else {
        if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah)) {
            OS_REG_RMW_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM, 0x0);
            reg_pmu2 = OS_REG_READ_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM);
            while (reg_pmu2) {
                OS_DELAY(10);
                reg_pmu2 = OS_REG_READ_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM);
            }
            OS_REG_RMW_FIELD(ah, reg_PMU1, AR_PHY_PMU1_PWD, 0x1);
            reg_pmu1 = OS_REG_READ_FIELD(ah, reg_PMU1, AR_PHY_PMU1_PWD);
            while (!reg_pmu1) {
                OS_DELAY(10);
                reg_pmu1 = OS_REG_READ_FIELD(ah, reg_PMU1, AR_PHY_PMU1_PWD);
            }
            OS_REG_RMW_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM, 0x1);
            reg_pmu2 = OS_REG_READ_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM);
            while (!reg_pmu2) {
                OS_DELAY(10);
                reg_pmu2 = OS_REG_READ_FIELD(ah, reg_PMU2, AR_PHY_PMU2_PGM);
            }
        } else if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            OS_REG_RMW_FIELD(ah, reg_PMU1, AR_PHY_PMU1_PWD, 0x1);
        } else {
            OS_REG_WRITE(ah, AR_RTC_SLEEP_CLK,
                (OS_REG_READ(ah, AR_RTC_SLEEP_CLK) |
                AR_RTC_FORCE_SWREG_PRD | AR_RTC_PCIE_RST_PWDN_EN));
        }
    }

    return 0;  
}

HAL_BOOL ar9300_drive_strength_apply(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int drive_strength;
    unsigned long reg;

    drive_strength = ar9300_eeprom_get(ahp, EEP_DRIVE_STRENGTH);
    if (drive_strength) {
        reg = OS_REG_READ(ah, AR_PHY_65NM_CH0_BIAS1);
        reg &= ~0x00ffffc0;
        reg |= 0x5 << 21;
        reg |= 0x5 << 18;
        reg |= 0x5 << 15;
        reg |= 0x5 << 12;
        reg |= 0x5 << 9;
        reg |= 0x5 << 6;
        OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BIAS1, reg);

        reg = OS_REG_READ(ah, AR_PHY_65NM_CH0_BIAS2);
        reg &= ~0xffffffe0;
        reg |= 0x5 << 29;
        reg |= 0x5 << 26;
        reg |= 0x5 << 23;
        reg |= 0x5 << 20;
        reg |= 0x5 << 17;
        reg |= 0x5 << 14;
        reg |= 0x5 << 11;
        reg |= 0x5 << 8;
        reg |= 0x5 << 5;
        OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BIAS2, reg);

        reg = OS_REG_READ(ah, AR_PHY_65NM_CH0_BIAS4);
        reg &= ~0xff800000;
        reg |= 0x5 << 29;
        reg |= 0x5 << 26;
        reg |= 0x5 << 23;
        OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BIAS4, reg);
    }
    return 0;
}

int32_t ar9300_xpa_bias_level_get(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (is_2ghz) {
        return eep->modal_header_2g.xpa_bias_lvl;
    } else {
        return eep->modal_header_5g.xpa_bias_lvl;
    }
}

HAL_BOOL ar9300_xpa_bias_level_apply(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    /*
     * In ar9330 emu, we can't access radio registers, 
     * merlin is used for radio part.
     */
    int bias;
    bias = ar9300_xpa_bias_level_get(ah, is_2ghz);

    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_WASP(ah)) {
        OS_REG_RMW_FIELD(ah,
            AR_HORNET_CH0_TOP2, AR_HORNET_CH0_TOP2_XPABIASLVL, bias);
    } else if (AR_SREV_SCORPION(ah)) {
        OS_REG_RMW_FIELD(ah,
            AR_SCORPION_CH0_TOP, AR_SCORPION_CH0_TOP_XPABIASLVL, bias);
    } else if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_TOP_JUPITER, AR_PHY_65NM_CH0_TOP_XPABIASLVL, bias);
    } else {
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_TOP, AR_PHY_65NM_CH0_TOP_XPABIASLVL, bias);
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_THERM, AR_PHY_65NM_CH0_THERM_XPABIASLVL_MSB,
            bias >> 2);
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_THERM, AR_PHY_65NM_CH0_THERM_XPASHORT2GND, 1);
    }
    return 0;
}

u_int32_t ar9300_ant_ctrl_common_get(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (is_2ghz) {
        return eep->modal_header_2g.ant_ctrl_common;
    } else {
        return eep->modal_header_5g.ant_ctrl_common;
    }
}
static u_int16_t 
ar9300_switch_com_spdt_get(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (is_2ghz) {
        return eep->modal_header_2g.switchcomspdt;
    } else {
        return eep->modal_header_5g.switchcomspdt;
    }
}
u_int32_t ar9300_ant_ctrl_common2_get(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (is_2ghz) {
        return eep->modal_header_2g.ant_ctrl_common2;
    } else {
        return eep->modal_header_5g.ant_ctrl_common2;
    }
}

u_int16_t ar9300_ant_ctrl_chain_get(struct ath_hal *ah, int chain,
    HAL_BOOL is_2ghz)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (chain >= 0 && chain < OSPREY_MAX_CHAINS) {
        if (is_2ghz) {
            return eep->modal_header_2g.ant_ctrl_chain[chain];
        } else {
            return eep->modal_header_5g.ant_ctrl_chain[chain];
        }
    }
    return 0;
}

/*
 * Select the usage of antenna via the RF switch.
 * Default values are loaded from eeprom.
 */
HAL_BOOL ar9300_ant_swcom_sel(struct ath_hal *ah, u_int8_t ops,
                        u_int32_t *common_tbl1, u_int32_t *common_tbl2)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    struct ath_hal_private  *ap  = AH_PRIVATE(ah);
    const struct ieee80211_channel *curchan = ap->ah_curchan;
    enum {
        ANT_SELECT_OPS_GET,
        ANT_SELECT_OPS_SET,
    };

    if (AR_SREV_JUPITER(ah) || AR_SREV_SCORPION(ah))
        return AH_FALSE;

    if (!curchan)
        return AH_FALSE;

#define AR_SWITCH_TABLE_COM_ALL (0xffff)
#define AR_SWITCH_TABLE_COM_ALL_S (0)
#define AR_SWITCH_TABLE_COM2_ALL (0xffffff)
#define AR_SWITCH_TABLE_COM2_ALL_S (0)
    switch (ops) {
    case ANT_SELECT_OPS_GET:
        *common_tbl1 = OS_REG_READ_FIELD(ah, AR_PHY_SWITCH_COM,
                            AR_SWITCH_TABLE_COM_ALL);
        *common_tbl2 = OS_REG_READ_FIELD(ah, AR_PHY_SWITCH_COM_2,
                            AR_SWITCH_TABLE_COM2_ALL);
        break;
    case ANT_SELECT_OPS_SET:
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM,
            AR_SWITCH_TABLE_COM_ALL, *common_tbl1);
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM_2,
            AR_SWITCH_TABLE_COM2_ALL, *common_tbl2);

        /* write back to eeprom */
        if (IEEE80211_IS_CHAN_2GHZ(curchan)) {
            eep->modal_header_2g.ant_ctrl_common = *common_tbl1;
            eep->modal_header_2g.ant_ctrl_common2 = *common_tbl2;
        } else {
            eep->modal_header_5g.ant_ctrl_common = *common_tbl1;
            eep->modal_header_5g.ant_ctrl_common2 = *common_tbl2;
        }

        break;
    default:
        break;
    }

    return AH_TRUE;
}

HAL_BOOL ar9300_ant_ctrl_apply(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    u_int32_t value;
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t regval;
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
#if ATH_ANT_DIV_COMB
    HAL_CAPABILITIES *pcap = &ahpriv->ah_caps;
#endif  /* ATH_ANT_DIV_COMB */
    u_int32_t xlan_gpio_cfg;
    u_int8_t  i;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: use_bt_ant_enable=%d\n",
      __func__, ahp->ah_lna_div_use_bt_ant_enable);

    /* XXX TODO: only if rx_gain_idx == 0 */
    if (AR_SREV_POSEIDON(ah)) {
        xlan_gpio_cfg = ah->ah_config.ath_hal_ext_lna_ctl_gpio;
        if (xlan_gpio_cfg) {
            for (i = 0; i < 32; i++) {
                if (xlan_gpio_cfg & (1 << i)) {
                    ath_hal_gpioCfgOutput(ah, i, 
                        HAL_GPIO_OUTPUT_MUX_PCIE_ATTENTION_LED);
                }
            }
        }    
    }
#define AR_SWITCH_TABLE_COM_ALL (0xffff)
#define AR_SWITCH_TABLE_COM_ALL_S (0)
#define AR_SWITCH_TABLE_COM_JUPITER_ALL (0xffffff)
#define AR_SWITCH_TABLE_COM_JUPITER_ALL_S (0)
#define AR_SWITCH_TABLE_COM_SCORPION_ALL (0xffffff)
#define AR_SWITCH_TABLE_COM_SCORPION_ALL_S (0)
#define AR_SWITCH_TABLE_COM_HONEYBEE_ALL (0xffffff)
#define AR_SWITCH_TABLE_COM_HONEYBEE_ALL_S (0)
#define AR_SWITCH_TABLE_COM_SPDT (0x00f00000)
    value = ar9300_ant_ctrl_common_get(ah, is_2ghz);
    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        if (AR_SREV_JUPITER_10(ah)) {
            /* Force SPDT setting for Jupiter 1.0 chips. */
            value &= ~AR_SWITCH_TABLE_COM_SPDT;
            value |= 0x00100000;
        }
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM, 
            AR_SWITCH_TABLE_COM_JUPITER_ALL, value);
    }
    else if (AR_SREV_SCORPION(ah)) {
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM, 
            AR_SWITCH_TABLE_COM_SCORPION_ALL, value);
    }
    else if (AR_SREV_HONEYBEE(ah)) {
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM, 
            AR_SWITCH_TABLE_COM_HONEYBEE_ALL, value);
    }
    else {
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM, 
            AR_SWITCH_TABLE_COM_ALL, value);
    }
/*
*   Jupiter2.0 defines new switch table for BT/WLAN, 
*	here's new field name in WB222.ref for both 2G and 5G.
*   Register: [GLB_CONTROL] GLB_CONTROL (@0x20044)
*   15:12	R/W	SWITCH_TABLE_COM_SPDT_WLAN_RX	SWITCH_TABLE_COM_SPDT_WLAN_RX 
*   11:8	R/W	SWITCH_TABLE_COM_SPDT_WLAN_TX	SWITCH_TABLE_COM_SPDT_WLAN_TX
*   7:4	R/W	SWITCH_TABLE_COM_SPDT_WLAN_IDLE	SWITCH_TABLE_COM_SPDT_WLAN_IDLE	
*/
#define AR_SWITCH_TABLE_COM_SPDT_ALL (0x0000fff0)
#define AR_SWITCH_TABLE_COM_SPDT_ALL_S (4)
    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        value = ar9300_switch_com_spdt_get(ah, is_2ghz);
        OS_REG_RMW_FIELD(ah, AR_GLB_CONTROL, 
            AR_SWITCH_TABLE_COM_SPDT_ALL, value);

        OS_REG_SET_BIT(ah, AR_GLB_CONTROL,
            AR_BTCOEX_CTRL_SPDT_ENABLE);
        //OS_REG_SET_BIT(ah, AR_GLB_CONTROL,
        //    AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL);
    }

#define AR_SWITCH_TABLE_COM2_ALL (0xffffff)
#define AR_SWITCH_TABLE_COM2_ALL_S (0)
    value = ar9300_ant_ctrl_common2_get(ah, is_2ghz);
#if ATH_ANT_DIV_COMB
    if ( AR_SREV_POSEIDON(ah) && (ahp->ah_lna_div_use_bt_ant_enable == TRUE) ) {
        value &= ~AR_SWITCH_TABLE_COM2_ALL;
        value |= ah->ah_config.ath_hal_ant_ctrl_comm2g_switch_enable;
	HALDEBUG(ah, HAL_DEBUG_RESET, "%s: com2=0x%08x\n", __func__, value)
    }
#endif  /* ATH_ANT_DIV_COMB */
    OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM_2, AR_SWITCH_TABLE_COM2_ALL, value);

#define AR_SWITCH_TABLE_ALL (0xfff)
#define AR_SWITCH_TABLE_ALL_S (0)
    value = ar9300_ant_ctrl_chain_get(ah, 0, is_2ghz);
    OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_CHAIN_0, AR_SWITCH_TABLE_ALL, value);

    if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah) && !AR_SREV_APHRODITE(ah)) {
        value = ar9300_ant_ctrl_chain_get(ah, 1, is_2ghz);
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_CHAIN_1, AR_SWITCH_TABLE_ALL, value);

        if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah)) {
            value = ar9300_ant_ctrl_chain_get(ah, 2, is_2ghz);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SWITCH_CHAIN_2, AR_SWITCH_TABLE_ALL, value);
        }
    }
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
        value = ar9300_eeprom_get(ahp, EEP_ANTDIV_control);
        /* main_lnaconf, alt_lnaconf, main_tb, alt_tb */
        regval = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
        regval &= (~ANT_DIV_CONTROL_ALL); /* clear bit 25~30 */     
        regval |= (value & 0x3f) << ANT_DIV_CONTROL_ALL_S; 
        /* enable_lnadiv */
        regval &= (~MULTICHAIN_GAIN_CTRL__ENABLE_ANT_DIV_LNADIV__MASK);
        regval |= ((value >> 6) & 0x1) << 
                  MULTICHAIN_GAIN_CTRL__ENABLE_ANT_DIV_LNADIV__SHIFT; 
#if ATH_ANT_DIV_COMB
        if ( AR_SREV_POSEIDON(ah) && (ahp->ah_lna_div_use_bt_ant_enable == TRUE) ) {
            regval |= ANT_DIV_ENABLE;
        }
        if (AR_SREV_APHRODITE(ah)) {
                if (ahp->ah_lna_div_use_bt_ant_enable) {
                        regval |= (1 << MULTICHAIN_GAIN_CTRL__ENABLE_ANT_SW_RX_PROT__SHIFT);

                        OS_REG_SET_BIT(ah, AR_PHY_RESTART,
                                    RESTART__ENABLE_ANT_FAST_DIV_M2FLAG__MASK);

                        /* Force WLAN LNA diversity ON */
                        OS_REG_SET_BIT(ah, AR_BTCOEX_WL_LNADIV,
                                    AR_BTCOEX_WL_LNADIV_FORCE_ON);
                } else {
                        regval &= ~(1 << MULTICHAIN_GAIN_CTRL__ENABLE_ANT_DIV_LNADIV__SHIFT);
                        regval &= ~(1 << MULTICHAIN_GAIN_CTRL__ENABLE_ANT_SW_RX_PROT__SHIFT);

                        OS_REG_CLR_BIT(ah, AR_PHY_MC_GAIN_CTRL,
                                    (1 << MULTICHAIN_GAIN_CTRL__ENABLE_ANT_SW_RX_PROT__SHIFT));

                        /* Force WLAN LNA diversity OFF */
                        OS_REG_CLR_BIT(ah, AR_BTCOEX_WL_LNADIV,
                                    AR_BTCOEX_WL_LNADIV_FORCE_ON);
                }
        }

#endif  /* ATH_ANT_DIV_COMB */
        OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
        
        /* enable fast_div */
        regval = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
        regval &= (~BBB_SIG_DETECT__ENABLE_ANT_FAST_DIV__MASK);
        regval |= ((value >> 7) & 0x1) << 
                  BBB_SIG_DETECT__ENABLE_ANT_FAST_DIV__SHIFT;
#if ATH_ANT_DIV_COMB
        if ((AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah))
          && (ahp->ah_lna_div_use_bt_ant_enable == TRUE) ) {
            regval |= FAST_DIV_ENABLE;
        }
#endif  /* ATH_ANT_DIV_COMB */
        OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, regval);        
    }

#if ATH_ANT_DIV_COMB    
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON_11_OR_LATER(ah)) {
        if (pcap->halAntDivCombSupport) {
            /* If support DivComb, set MAIN to LNA1, ALT to LNA2 at beginning */
            regval = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
            /* clear bit 25~30 main_lnaconf, alt_lnaconf, main_tb, alt_tb */
            regval &= (~(MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__MASK | 
                         MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__MASK | 
                         MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_GAINTB__MASK | 
                         MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_GAINTB__MASK)); 
            regval |= (HAL_ANT_DIV_COMB_LNA1 << 
                       MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__SHIFT); 
            regval |= (HAL_ANT_DIV_COMB_LNA2 << 
                       MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__SHIFT); 
            OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
        }

    }
#endif /* ATH_ANT_DIV_COMB */
    if (AR_SREV_POSEIDON(ah) && ( ahp->ah_diversity_control == HAL_ANT_FIXED_A 
	     || ahp->ah_diversity_control == HAL_ANT_FIXED_B))
    {
        u_int32_t reg_val = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
        reg_val &=  ~(MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__MASK | 
                    MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__MASK |
                    MULTICHAIN_GAIN_CTRL__ANT_FAST_DIV_BIAS__MASK |
    		        MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_GAINTB__MASK |
    		        MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_GAINTB__MASK );

        switch (ahp->ah_diversity_control) {
        case HAL_ANT_FIXED_A:
            /* Enable first antenna only */
            reg_val |= (HAL_ANT_DIV_COMB_LNA1 << 
                       MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__SHIFT); 
            reg_val |= (HAL_ANT_DIV_COMB_LNA2 << 
                       MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__SHIFT); 
            /* main/alt gain table and Fast Div Bias all set to 0 */
            OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, reg_val);
            regval = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
            regval &= (~BBB_SIG_DETECT__ENABLE_ANT_FAST_DIV__MASK);
            OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, regval);        
            break;
        case HAL_ANT_FIXED_B:
            /* Enable second antenna only, after checking capability */
            reg_val |= (HAL_ANT_DIV_COMB_LNA2 << 
                       MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__SHIFT); 
            reg_val |= (HAL_ANT_DIV_COMB_LNA1 << 
                       MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__SHIFT); 
            /* main/alt gain table and Fast Div all set to 0 */
            OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, reg_val);
            regval = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
            regval &= (~BBB_SIG_DETECT__ENABLE_ANT_FAST_DIV__MASK);
            OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, regval);        
            /* For WB225, need to swith ANT2 from BT to Wifi
             * This will not affect HB125 LNA diversity feature.
             */
	     HALDEBUG(ah, HAL_DEBUG_RESET, "%s: com2=0x%08x\n", __func__,
	         ah->ah_config.ath_hal_ant_ctrl_comm2g_switch_enable)
            OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM_2, AR_SWITCH_TABLE_COM2_ALL, 
                ah->ah_config.ath_hal_ant_ctrl_comm2g_switch_enable);
            break;
        default:
            break;
        }
    }    
    return 0;
}

static u_int16_t
ar9300_attenuation_chain_get(struct ath_hal *ah, int chain, u_int16_t channel)
{
    int32_t f[3], t[3];
    u_int16_t value;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (chain >= 0 && chain < OSPREY_MAX_CHAINS) {
        if (channel < 4000) {
            return eep->modal_header_2g.xatten1_db[chain];
        } else {
            if (eep->base_ext2.xatten1_db_low[chain] != 0) {        
                t[0] = eep->base_ext2.xatten1_db_low[chain];
                f[0] = 5180;
                t[1] = eep->modal_header_5g.xatten1_db[chain];
                f[1] = 5500;
                t[2] = eep->base_ext2.xatten1_db_high[chain];
                f[2] = 5785;
                value = interpolate(channel, f, t, 3);
                return value;
            } else {
                return eep->modal_header_5g.xatten1_db[chain];
            }
        }
    }
    return 0;
}

static u_int16_t
ar9300_attenuation_margin_chain_get(struct ath_hal *ah, int chain,
    u_int16_t channel)
{
    int32_t f[3], t[3];
    u_int16_t value;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if (chain >= 0 && chain < OSPREY_MAX_CHAINS) {
        if (channel < 4000) {
            return eep->modal_header_2g.xatten1_margin[chain];
        } else {
            if (eep->base_ext2.xatten1_margin_low[chain] != 0) {    
                t[0] = eep->base_ext2.xatten1_margin_low[chain];
                f[0] = 5180;
                t[1] = eep->modal_header_5g.xatten1_margin[chain];
                f[1] = 5500;
                t[2] = eep->base_ext2.xatten1_margin_high[chain];
                f[2] = 5785;
                value = interpolate(channel, f, t, 3);
                return value;
            } else {
                return eep->modal_header_5g.xatten1_margin[chain];
            }
        }
    }
    return 0;
}

#if 0
HAL_BOOL ar9300_attenuation_apply(struct ath_hal *ah, u_int16_t channel)
{
    u_int32_t value;
//    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);

    /* Test value. if 0 then attenuation is unused. Don't load anything. */
    value = ar9300_attenuation_chain_get(ah, 0, channel);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_EXT_ATTEN_CTL_0, AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB, value);
    value = ar9300_attenuation_margin_chain_get(ah, 0, channel);
    if (ar9300_rx_gain_index_get(ah) == 0
        && ah->ah_config.ath_hal_ext_atten_margin_cfg)
    {
        value = 5;
    }
    OS_REG_RMW_FIELD(ah,
        AR_PHY_EXT_ATTEN_CTL_0, AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN, value);

    if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
        value = ar9300_attenuation_chain_get(ah, 1, channel);
        OS_REG_RMW_FIELD(ah,
            AR_PHY_EXT_ATTEN_CTL_1, AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB, value);
        value = ar9300_attenuation_margin_chain_get(ah, 1, channel);
        OS_REG_RMW_FIELD(ah,
            AR_PHY_EXT_ATTEN_CTL_1, AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN,
            value);
        if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah)&& !AR_SREV_HONEYBEE(ah) ) {
            value = ar9300_attenuation_chain_get(ah, 2, channel);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_EXT_ATTEN_CTL_2, AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB, value);
            value = ar9300_attenuation_margin_chain_get(ah, 2, channel);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_EXT_ATTEN_CTL_2, AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN,
                value);
        }
    }
    return 0;
}
#endif
HAL_BOOL
ar9300_attenuation_apply(struct ath_hal *ah, u_int16_t channel)
{
	int i;
	uint32_t value;
	uint32_t ext_atten_reg[3] = {
	    AR_PHY_EXT_ATTEN_CTL_0,
	    AR_PHY_EXT_ATTEN_CTL_1,
	    AR_PHY_EXT_ATTEN_CTL_2
	};

	/*
	 * If it's an AR9462 and we're receiving on the second
	 * chain only, set the chain 0 details from chain 1
	 * calibration.
	 *
	 * This is from ath9k.
	 */
	if (AR_SREV_JUPITER(ah) && (AH9300(ah)->ah_rx_chainmask == 0x2)) {
		value = ar9300_attenuation_chain_get(ah, 1, channel);
		OS_REG_RMW_FIELD(ah, ext_atten_reg[0],
		    AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB, value);
		value = ar9300_attenuation_margin_chain_get(ah, 1, channel);
		OS_REG_RMW_FIELD(ah, ext_atten_reg[0],
		    AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN, value);
	}

	/*
	 * Now, loop over the configured transmit chains and
	 * load in the attenuation/margin settings as appropriate.
	 */
	for (i = 0; i < 3; i++) {
		if ((AH9300(ah)->ah_tx_chainmask & (1 << i)) == 0)
			continue;

		value = ar9300_attenuation_chain_get(ah, i, channel);
		OS_REG_RMW_FIELD(ah, ext_atten_reg[i],
		    AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB,
		    value);

		if (AR_SREV_POSEIDON(ah) &&
		    (ar9300_rx_gain_index_get(ah) == 0) &&
		    ah->ah_config.ath_hal_ext_atten_margin_cfg) {
			value = 5;
		} else {
			value = ar9300_attenuation_margin_chain_get(ah, 0,
			    channel);
		}

		/*
		 * I'm not sure why it's loading in this setting into
		 * the chain 0 margin regardless of the current chain.
		 */
		if (ah->ah_config.ath_hal_min_gainidx)
			OS_REG_RMW_FIELD(ah,
			    AR_PHY_EXT_ATTEN_CTL_0,
			    AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN,
			    value);

		OS_REG_RMW_FIELD(ah,
		    ext_atten_reg[i],
		    AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN,
		    value);
	}

	return (0);
}


static u_int16_t ar9300_quick_drop_get(struct ath_hal *ah, 
								int chain, u_int16_t channel)
{
    int32_t f[3], t[3];
    u_int16_t value;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    if (channel < 4000) {
        return eep->modal_header_2g.quick_drop;
    } else {
        t[0] = eep->base_ext1.quick_drop_low;
        f[0] = 5180;
        t[1] = eep->modal_header_5g.quick_drop;
        f[1] = 5500;
        t[2] = eep->base_ext1.quick_drop_high;
        f[2] = 5785;
        value = interpolate(channel, f, t, 3);
        return value;
    }
}


static HAL_BOOL ar9300_quick_drop_apply(struct ath_hal *ah, u_int16_t channel)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    u_int32_t value;
    //
    // Test value. if 0 then quickDrop is unused. Don't load anything.
    //
    if (eep->base_eep_header.misc_configuration & 0x10)
	{
        if (AR_SREV_OSPREY(ah) || AR_SREV_AR9580(ah) || AR_SREV_WASP(ah)) 
        {
            value = ar9300_quick_drop_get(ah, 0, channel);
            OS_REG_RMW_FIELD(ah, AR_PHY_AGC, AR_PHY_AGC_QUICK_DROP, value);
        }
    }
    return 0;
}

static u_int16_t ar9300_tx_end_to_xpa_off_get(struct ath_hal *ah, u_int16_t channel)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    if (channel < 4000) {
        return eep->modal_header_2g.tx_end_to_xpa_off;
    } else {
        return eep->modal_header_5g.tx_end_to_xpa_off;
    }
}

static HAL_BOOL ar9300_tx_end_to_xpab_off_apply(struct ath_hal *ah, u_int16_t channel)
{
    u_int32_t value;

    value = ar9300_tx_end_to_xpa_off_get(ah, channel);
    /* Apply to both xpaa and xpab */
    if (AR_SREV_OSPREY(ah) || AR_SREV_AR9580(ah) || AR_SREV_WASP(ah)) 
    {
        OS_REG_RMW_FIELD(ah, AR_PHY_XPA_TIMING_CTL, 
            AR_PHY_XPA_TIMING_CTL_TX_END_XPAB_OFF, value);
        OS_REG_RMW_FIELD(ah, AR_PHY_XPA_TIMING_CTL, 
            AR_PHY_XPA_TIMING_CTL_TX_END_XPAA_OFF, value);
    }
    return 0;
}

static int
ar9300_eeprom_cal_pier_get(struct ath_hal *ah, int mode, int ipier, int ichain, 
    int *pfrequency, int *pcorrection, int *ptemperature, int *pvoltage)
{
    u_int8_t *p_cal_pier;
    OSP_CAL_DATA_PER_FREQ_OP_LOOP *p_cal_pier_struct;
    int is_2ghz;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    if (ichain >= OSPREY_MAX_CHAINS) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: Invalid chain index, must be less than %d\n",
            __func__, OSPREY_MAX_CHAINS);
        return -1;
    }

    if (mode) {/* 5GHz */
        if (ipier >= OSPREY_NUM_5G_CAL_PIERS){
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: Invalid 5GHz cal pier index, must be less than %d\n",
                __func__, OSPREY_NUM_5G_CAL_PIERS);
            return -1;
        }
        p_cal_pier = &(eep->cal_freq_pier_5g[ipier]);
        p_cal_pier_struct = &(eep->cal_pier_data_5g[ichain][ipier]);
        is_2ghz = 0;
    } else {
        if (ipier >= OSPREY_NUM_2G_CAL_PIERS){
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: Invalid 2GHz cal pier index, must be less than %d\n",
                __func__, OSPREY_NUM_2G_CAL_PIERS);
            return -1;
        }

        p_cal_pier = &(eep->cal_freq_pier_2g[ipier]);
        p_cal_pier_struct = &(eep->cal_pier_data_2g[ichain][ipier]);
        is_2ghz = 1;
    }
    *pfrequency = FBIN2FREQ(*p_cal_pier, is_2ghz);
    *pcorrection = p_cal_pier_struct->ref_power;
    *ptemperature = p_cal_pier_struct->temp_meas;
    *pvoltage = p_cal_pier_struct->volt_meas;
    return 0;
}

/*
 * Apply the recorded correction values.
 */
static int
ar9300_calibration_apply(struct ath_hal *ah, int frequency)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    int ichain, ipier, npier;
    int mode;
    int fdiff;
    int pfrequency, pcorrection, ptemperature, pvoltage; 
    int bf, factor, plus;

    int lfrequency[AR9300_MAX_CHAINS];
    int hfrequency[AR9300_MAX_CHAINS];

    int lcorrection[AR9300_MAX_CHAINS];
    int hcorrection[AR9300_MAX_CHAINS];
    int correction[AR9300_MAX_CHAINS];

    int ltemperature[AR9300_MAX_CHAINS];
    int htemperature[AR9300_MAX_CHAINS];
    int temperature[AR9300_MAX_CHAINS];

    int lvoltage[AR9300_MAX_CHAINS];
    int hvoltage[AR9300_MAX_CHAINS];
    int voltage[AR9300_MAX_CHAINS];

    mode = (frequency >= 4000);
    npier = (mode) ?  OSPREY_NUM_5G_CAL_PIERS : OSPREY_NUM_2G_CAL_PIERS;

    for (ichain = 0; ichain < AR9300_MAX_CHAINS; ichain++) {
        lfrequency[ichain] = 0;
        hfrequency[ichain] = 100000;
    }
    /*
     * identify best lower and higher frequency calibration measurement
     */
    for (ichain = 0; ichain < AR9300_MAX_CHAINS; ichain++) {
        for (ipier = 0; ipier < npier; ipier++) {
            if (ar9300_eeprom_cal_pier_get(
                    ah, mode, ipier, ichain,
                    &pfrequency, &pcorrection, &ptemperature, &pvoltage) == 0)
            {
                fdiff = frequency - pfrequency;
                /*
                 * this measurement is higher than our desired frequency
                 */
                if (fdiff <= 0) {
                    if (hfrequency[ichain] <= 0 ||
                        hfrequency[ichain] >= 100000 ||
                        fdiff > (frequency - hfrequency[ichain]))
                    {
                        /*
                         * new best higher frequency measurement
                         */
                        hfrequency[ichain] = pfrequency;
                        hcorrection[ichain] = pcorrection;
                        htemperature[ichain] = ptemperature;
                        hvoltage[ichain] = pvoltage;
                    }
                }
                if (fdiff >= 0) {
                    if (lfrequency[ichain] <= 0 ||
                        fdiff < (frequency - lfrequency[ichain]))
                    {
                        /*
                         * new best lower frequency measurement
                         */
                        lfrequency[ichain] = pfrequency;
                        lcorrection[ichain] = pcorrection;
                        ltemperature[ichain] = ptemperature;
                        lvoltage[ichain] = pvoltage;
                    }
                }
            }
        }
    }
    /* interpolate */
    for (ichain = 0; ichain < AR9300_MAX_CHAINS; ichain++) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: ch=%d f=%d low=%d %d h=%d %d\n",
            __func__, ichain, frequency,
            lfrequency[ichain], lcorrection[ichain],
            hfrequency[ichain], hcorrection[ichain]);
        /*
         * they're the same, so just pick one
         */ 
        if (hfrequency[ichain] == lfrequency[ichain]) {
            correction[ichain] = lcorrection[ichain];
            voltage[ichain] = lvoltage[ichain];
            temperature[ichain] = ltemperature[ichain];
        } else if (frequency - lfrequency[ichain] < 1000) {
            /* the low frequency is good */
            if (hfrequency[ichain] - frequency < 1000) {
                /*
                 * The high frequency is good too -
                 * interpolate with round off.
                 */
                int mult, div, diff;
                mult = frequency - lfrequency[ichain];
                div = hfrequency[ichain] - lfrequency[ichain];

                diff = hcorrection[ichain] - lcorrection[ichain];
                bf = 2 * diff * mult / div;
                plus = (bf % 2);
                factor = bf / 2;
                correction[ichain] = lcorrection[ichain] + factor + plus;

                diff = htemperature[ichain] - ltemperature[ichain];
                bf = 2 * diff * mult / div;
                plus = (bf % 2);
                factor = bf / 2;
                temperature[ichain] = ltemperature[ichain] + factor + plus;

                diff = hvoltage[ichain] - lvoltage[ichain];
                bf = 2 * diff * mult / div;
                plus = (bf % 2);
                factor = bf / 2;
                voltage[ichain] = lvoltage[ichain] + factor + plus;
            } else {
                /* only low is good, use it */
                correction[ichain] = lcorrection[ichain];
                temperature[ichain] = ltemperature[ichain];
                voltage[ichain] = lvoltage[ichain];
            }
        } else if (hfrequency[ichain] - frequency < 1000) {
            /* only high is good, use it */
            correction[ichain] = hcorrection[ichain];
            temperature[ichain] = htemperature[ichain];
            voltage[ichain] = hvoltage[ichain];
        } else {
            /* nothing is good, presume 0???? */
            correction[ichain] = 0;
            temperature[ichain] = 0;
            voltage[ichain] = 0;
        }
    }

    /* GreenTx isn't currently supported */
    /* GreenTx */
    if (ah->ah_config.ath_hal_sta_update_tx_pwr_enable) {
        if (AR_SREV_POSEIDON(ah)) {
            /* Get calibrated OLPC gain delta value for GreenTx */
            ahp->ah_db2[POSEIDON_STORED_REG_G2_OLPC_OFFSET] = 
                (u_int32_t) correction[0];
        }
    }

    ar9300_power_control_override(
        ah, frequency, correction, voltage, temperature);
    HALDEBUG(ah, HAL_DEBUG_EEPROM,
        "%s: for frequency=%d, calibration correction = %d %d %d\n",
         __func__, frequency, correction[0], correction[1], correction[2]);

    return 0;
}

int
ar9300_power_control_override(struct ath_hal *ah, int frequency,
    int *correction, int *voltage, int *temperature)
{
    int temp_slope = 0;
    int temp_slope_1 = 0;
    int temp_slope_2 = 0;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    int32_t f[8], t[8],t1[3], t2[3];
	int i;

    OS_REG_RMW(ah, AR_PHY_TPC_11_B0,
        (correction[0] << AR_PHY_TPC_OLPC_GAIN_DELTA_S),
        AR_PHY_TPC_OLPC_GAIN_DELTA);
    if (!AR_SREV_POSEIDON(ah)) {
        OS_REG_RMW(ah, AR_PHY_TPC_11_B1,
            (correction[1] << AR_PHY_TPC_OLPC_GAIN_DELTA_S),
            AR_PHY_TPC_OLPC_GAIN_DELTA);
        if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah) ) {
            OS_REG_RMW(ah, AR_PHY_TPC_11_B2, 
                (correction[2] << AR_PHY_TPC_OLPC_GAIN_DELTA_S),
                AR_PHY_TPC_OLPC_GAIN_DELTA);
        }
    }
    /*
     * enable open loop power control on chip
     */
    OS_REG_RMW(ah, AR_PHY_TPC_6_B0,
        (3 << AR_PHY_TPC_6_ERROR_EST_MODE_S), AR_PHY_TPC_6_ERROR_EST_MODE);
    if (!AR_SREV_POSEIDON(ah)) {
        OS_REG_RMW(ah, AR_PHY_TPC_6_B1, 
            (3 << AR_PHY_TPC_6_ERROR_EST_MODE_S), AR_PHY_TPC_6_ERROR_EST_MODE);
        if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah)  ) {
            OS_REG_RMW(ah, AR_PHY_TPC_6_B2, 
                (3 << AR_PHY_TPC_6_ERROR_EST_MODE_S),
                AR_PHY_TPC_6_ERROR_EST_MODE);
        }
    }

    /*
     * Enable temperature compensation
     * Need to use register names
     */
    if (frequency < 4000) {
        temp_slope = eep->modal_header_2g.temp_slope;
    } else {
		if ((eep->base_eep_header.misc_configuration & 0x20) != 0)
		{
				for(i=0;i<8;i++)
				{
					t[i]=eep->base_ext1.tempslopextension[i];
					f[i]=FBIN2FREQ(eep->cal_freq_pier_5g[i], 0);
				}
				temp_slope=interpolate(frequency,f,t,8);
		}
		else
		{
        if(!AR_SREV_SCORPION(ah)) {
          if (eep->base_ext2.temp_slope_low != 0) {
             t[0] = eep->base_ext2.temp_slope_low;
             f[0] = 5180;
             t[1] = eep->modal_header_5g.temp_slope;
             f[1] = 5500;
             t[2] = eep->base_ext2.temp_slope_high;
             f[2] = 5785;
             temp_slope = interpolate(frequency, f, t, 3);
           } else {
             temp_slope = eep->modal_header_5g.temp_slope;
           }
         } else {
            /*
             * Scorpion has individual chain tempslope values
             */
             t[0] = eep->base_ext1.tempslopextension[2];
             t1[0]= eep->base_ext1.tempslopextension[3];
             t2[0]= eep->base_ext1.tempslopextension[4];
             f[0] = 5180;
             t[1] = eep->modal_header_5g.temp_slope;
             t1[1]= eep->base_ext1.tempslopextension[0];
             t2[1]= eep->base_ext1.tempslopextension[1];
             f[1] = 5500;
             t[2] = eep->base_ext1.tempslopextension[5];
             t1[2]= eep->base_ext1.tempslopextension[6];
             t2[2]= eep->base_ext1.tempslopextension[7];
             f[2] = 5785;
             temp_slope = interpolate(frequency, f, t, 3);
             temp_slope_1=interpolate(frequency, f, t1,3);
             temp_slope_2=interpolate(frequency, f, t2,3);
       } 
	 }
  }

    if (!AR_SREV_SCORPION(ah) && !AR_SREV_HONEYBEE(ah)) {
        OS_REG_RMW_FIELD(ah,
            AR_PHY_TPC_19, AR_PHY_TPC_19_ALPHA_THERM, temp_slope);
    } else {
        /*Scorpion and Honeybee has tempSlope register for each chain*/
        /*Check whether temp_compensation feature is enabled or not*/
        if (eep->base_eep_header.feature_enable & 0x1){
	    if(frequency < 4000) {
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x1) {
		    OS_REG_RMW_FIELD(ah,
				    AR_PHY_TPC_19, AR_PHY_TPC_19_ALPHA_THERM, 
				    eep->base_ext2.temp_slope_low);
		    } 
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x2) {
		    OS_REG_RMW_FIELD(ah,
				    AR_SCORPION_PHY_TPC_19_B1, AR_PHY_TPC_19_ALPHA_THERM, 
				    temp_slope);
		    } 
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x4) {
		    OS_REG_RMW_FIELD(ah,
				    AR_SCORPION_PHY_TPC_19_B2, AR_PHY_TPC_19_ALPHA_THERM, 
				    eep->base_ext2.temp_slope_high);
		     } 	
	    } else {
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x1) {
		    OS_REG_RMW_FIELD(ah,
				    AR_PHY_TPC_19, AR_PHY_TPC_19_ALPHA_THERM, 
				    temp_slope);
			}
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x2) {
		    OS_REG_RMW_FIELD(ah,
				    AR_SCORPION_PHY_TPC_19_B1, AR_PHY_TPC_19_ALPHA_THERM, 
				    temp_slope_1);
		}
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x4) {
		    OS_REG_RMW_FIELD(ah,
				    AR_SCORPION_PHY_TPC_19_B2, AR_PHY_TPC_19_ALPHA_THERM, 
				    temp_slope_2);
			} 
	    }
        }else {
        	/* If temp compensation is not enabled, set all registers to 0*/
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x1) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TPC_19, AR_PHY_TPC_19_ALPHA_THERM, 0);
		    }
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x2) {
            OS_REG_RMW_FIELD(ah,
                AR_SCORPION_PHY_TPC_19_B1, AR_PHY_TPC_19_ALPHA_THERM, 0);
		    }  
		if (((eep->base_eep_header.txrx_mask & 0xf0) >> 4) & 0x4) {
            OS_REG_RMW_FIELD(ah,
                AR_SCORPION_PHY_TPC_19_B2, AR_PHY_TPC_19_ALPHA_THERM, 0);
		} 
        }
    }
    OS_REG_RMW_FIELD(ah,
        AR_PHY_TPC_18, AR_PHY_TPC_18_THERM_CAL_VALUE, temperature[0]);

    return 0;
}

/**************************************************************
 * ar9300_eep_def_get_max_edge_power
 *
 * Find the maximum conformance test limit for the given channel and CTL info
 */
static inline u_int16_t
ar9300_eep_def_get_max_edge_power(ar9300_eeprom_t *p_eep_data, u_int16_t freq,
    int idx, HAL_BOOL is_2ghz)
{
    u_int16_t twice_max_edge_power = AR9300_MAX_RATE_POWER;
    u_int8_t *ctl_freqbin = is_2ghz ?
        &p_eep_data->ctl_freqbin_2G[idx][0] :
        &p_eep_data->ctl_freqbin_5G[idx][0];
    u_int16_t num_edges = is_2ghz ?
        OSPREY_NUM_BAND_EDGES_2G : OSPREY_NUM_BAND_EDGES_5G;
    int i;

    /* Get the edge power */
    for (i = 0; (i < num_edges) && (ctl_freqbin[i] != AR9300_BCHAN_UNUSED); i++)
    {
        /*
         * If there's an exact channel match or an inband flag set
         * on the lower channel use the given rd_edge_power
         */
        if (freq == fbin2freq(ctl_freqbin[i], is_2ghz)) {
            if (is_2ghz) {
                twice_max_edge_power =
                    p_eep_data->ctl_power_data_2g[idx].ctl_edges[i].t_power;
            } else {       
                twice_max_edge_power =
                    p_eep_data->ctl_power_data_5g[idx].ctl_edges[i].t_power;
            }
            break;
        } else if ((i > 0) && (freq < fbin2freq(ctl_freqbin[i], is_2ghz))) {
            if (is_2ghz) {
                if (fbin2freq(ctl_freqbin[i - 1], 1) < freq &&
                    p_eep_data->ctl_power_data_2g[idx].ctl_edges[i - 1].flag)
                {
                    twice_max_edge_power =
                        p_eep_data->ctl_power_data_2g[idx].
                            ctl_edges[i - 1].t_power;
                }
            } else {
                if (fbin2freq(ctl_freqbin[i - 1], 0) < freq &&
                    p_eep_data->ctl_power_data_5g[idx].ctl_edges[i - 1].flag)
                {
                    twice_max_edge_power =
                        p_eep_data->ctl_power_data_5g[idx].
                            ctl_edges[i - 1].t_power;
                }
            }
            /*
             * Leave loop - no more affecting edges possible
             * in this monotonic increasing list
             */
            break;
        }
    }
    /*
     * EV89475: EEPROM might contain 0 txpower in CTL table for certain
     * 2.4GHz channels. We workaround it by overwriting 60 (30 dBm) here.
     */
    if (is_2ghz && (twice_max_edge_power == 0)) {
        twice_max_edge_power = 60;
    }

    HALASSERT(twice_max_edge_power > 0);
    return twice_max_edge_power;
}

HAL_BOOL
ar9300_eeprom_set_power_per_rate_table(
    struct ath_hal *ah,
    ar9300_eeprom_t *p_eep_data,
    const struct ieee80211_channel *chan,
    u_int8_t *p_pwr_array,
    u_int16_t cfg_ctl,
    u_int16_t antenna_reduction,
    u_int16_t twice_max_regulatory_power,
    u_int16_t power_limit,
    u_int8_t chainmask)
{
    /* Local defines to distinguish between extension and control CTL's */
#define EXT_ADDITIVE (0x8000)
#define CTL_11A_EXT (CTL_11A | EXT_ADDITIVE)
#define CTL_11G_EXT (CTL_11G | EXT_ADDITIVE)
#define CTL_11B_EXT (CTL_11B | EXT_ADDITIVE)
#define REDUCE_SCALED_POWER_BY_TWO_CHAIN     6  /* 10*log10(2)*2 */
#define REDUCE_SCALED_POWER_BY_THREE_CHAIN  10  /* 10*log10(3)*2 */
#define PWRINCR_3_TO_1_CHAIN      9             /* 10*log(3)*2 */
#define PWRINCR_3_TO_2_CHAIN      3             /* floor(10*log(3/2)*2) */
#define PWRINCR_2_TO_1_CHAIN      6             /* 10*log(2)*2 */

    static const u_int16_t tp_scale_reduction_table[5] =
        { 0, 3, 6, 9, AR9300_MAX_RATE_POWER };
    int i;
    int16_t twice_largest_antenna;
    u_int16_t twice_antenna_reduction = 2*antenna_reduction ;
    int16_t scaled_power = 0, min_ctl_power, max_reg_allowed_power;
#define SUB_NUM_CTL_MODES_AT_5G_40 2    /* excluding HT40, EXT-OFDM */
#define SUB_NUM_CTL_MODES_AT_2G_40 3    /* excluding HT40, EXT-OFDM, EXT-CCK */
    u_int16_t ctl_modes_for11a[] =
        {CTL_11A, CTL_5GHT20, CTL_11A_EXT, CTL_5GHT40};
    u_int16_t ctl_modes_for11g[] =
        {CTL_11B, CTL_11G, CTL_2GHT20, CTL_11B_EXT, CTL_11G_EXT, CTL_2GHT40};
    u_int16_t num_ctl_modes, *p_ctl_mode, ctl_mode, freq;
    CHAN_CENTERS centers;
    int tx_chainmask;
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int8_t *ctl_index;
    u_int8_t ctl_num;
    u_int16_t twice_min_edge_power;
    u_int16_t twice_max_edge_power = AR9300_MAX_RATE_POWER;
#ifdef	AH_DEBUG
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
#endif

    if (chainmask)
        tx_chainmask = chainmask;
    else
        tx_chainmask = ahp->ah_tx_chainmaskopt ?
                            ahp->ah_tx_chainmaskopt :ahp->ah_tx_chainmask;

    ar9300_get_channel_centers(ah, chan, &centers);

#if 1
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        ahp->twice_antenna_gain = p_eep_data->modal_header_2g.antenna_gain;
    } else {
        ahp->twice_antenna_gain = p_eep_data->modal_header_5g.antenna_gain;
    }

#else
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        ahp->twice_antenna_gain = AH_MAX(p_eep_data->modal_header_2g.antenna_gain,
                                         AH_PRIVATE(ah)->ah_antenna_gain_2g);
    } else {
        ahp->twice_antenna_gain = AH_MAX(p_eep_data->modal_header_5g.antenna_gain,
                                         AH_PRIVATE(ah)->ah_antenna_gain_5g);
    }
#endif

    /* Save max allowed antenna gain to ease future lookups */
    ahp->twice_antenna_reduction = twice_antenna_reduction; 

    /*  Deduct antenna gain from  EIRP to get the upper limit */
    twice_largest_antenna = (int16_t)AH_MIN((twice_antenna_reduction -
                                       ahp->twice_antenna_gain), 0);
    max_reg_allowed_power = twice_max_regulatory_power + twice_largest_antenna;

    /* Use ah_tp_scale - see bug 30070. */
    if (AH_PRIVATE(ah)->ah_tpScale != HAL_TP_SCALE_MAX) { 
        max_reg_allowed_power -=
            (tp_scale_reduction_table[(AH_PRIVATE(ah)->ah_tpScale)] * 2);
    }

    scaled_power = AH_MIN(power_limit, max_reg_allowed_power);

    /*
     * Reduce scaled Power by number of chains active to get to
     * per chain tx power level
     */
    /* TODO: better value than these? */
    switch (ar9300_get_ntxchains(tx_chainmask)) {
    case 1:
        ahp->upper_limit[0] = AH_MAX(0, scaled_power);
        break;
    case 2:
        scaled_power -= REDUCE_SCALED_POWER_BY_TWO_CHAIN;
        ahp->upper_limit[1] = AH_MAX(0, scaled_power);
        break;
    case 3:
        scaled_power -= REDUCE_SCALED_POWER_BY_THREE_CHAIN;
        ahp->upper_limit[2] = AH_MAX(0, scaled_power);
        break;
    default:
        HALASSERT(0); /* Unsupported number of chains */
    }

    scaled_power = AH_MAX(0, scaled_power);

    /* Get target powers from EEPROM - our baseline for TX Power */
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        /* Setup for CTL modes */
        /* CTL_11B, CTL_11G, CTL_2GHT20 */
        num_ctl_modes =
            ARRAY_LENGTH(ctl_modes_for11g) - SUB_NUM_CTL_MODES_AT_2G_40;
        p_ctl_mode = ctl_modes_for11g;

        if (IEEE80211_IS_CHAN_HT40(chan)) {
            num_ctl_modes = ARRAY_LENGTH(ctl_modes_for11g); /* All 2G CTL's */
        }
    } else {
        /* Setup for CTL modes */
        /* CTL_11A, CTL_5GHT20 */
        num_ctl_modes =
            ARRAY_LENGTH(ctl_modes_for11a) - SUB_NUM_CTL_MODES_AT_5G_40;
        p_ctl_mode = ctl_modes_for11a;

        if (IEEE80211_IS_CHAN_HT40(chan)) {
            num_ctl_modes = ARRAY_LENGTH(ctl_modes_for11a); /* All 5G CTL's */
        }
    }

    /*
     * For MIMO, need to apply regulatory caps individually across dynamically
     * running modes: CCK, OFDM, HT20, HT40
     *
     * The outer loop walks through each possible applicable runtime mode.
     * The inner loop walks through each ctl_index entry in EEPROM.
     * The ctl value is encoded as [7:4] == test group, [3:0] == test mode.
     *
     */
    for (ctl_mode = 0; ctl_mode < num_ctl_modes; ctl_mode++) {
        HAL_BOOL is_ht40_ctl_mode =
            (p_ctl_mode[ctl_mode] == CTL_5GHT40) ||
            (p_ctl_mode[ctl_mode] == CTL_2GHT40);
        if (is_ht40_ctl_mode) {
            freq = centers.synth_center;
        } else if (p_ctl_mode[ctl_mode] & EXT_ADDITIVE) {
            freq = centers.ext_center;
        } else {
            freq = centers.ctl_center;
        }

        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
            "LOOP-Mode ctl_mode %d < %d, "
            "is_ht40_ctl_mode %d, EXT_ADDITIVE %d\n",
            ctl_mode, num_ctl_modes, is_ht40_ctl_mode,
            (p_ctl_mode[ctl_mode] & EXT_ADDITIVE));
        /* walk through each CTL index stored in EEPROM */
        if (IEEE80211_IS_CHAN_2GHZ(chan)) {
            ctl_index = p_eep_data->ctl_index_2g;
            ctl_num = OSPREY_NUM_CTLS_2G;
        } else {
            ctl_index = p_eep_data->ctl_index_5g;
            ctl_num = OSPREY_NUM_CTLS_5G;
        }

        for (i = 0; (i < ctl_num) && ctl_index[i]; i++) {
            HALDEBUG(ah, HAL_DEBUG_POWER_MGMT, 
                "  LOOP-Ctlidx %d: cfg_ctl 0x%2.2x p_ctl_mode 0x%2.2x "
                "ctl_index 0x%2.2x chan %d chanctl 0x%x\n",
                i, cfg_ctl, p_ctl_mode[ctl_mode], ctl_index[i], 
                ichan->channel, ath_hal_getctl(ah, chan));


            /* 
             * compare test group from regulatory channel list
             * with test mode from p_ctl_mode list
             */
            if ((((cfg_ctl & ~CTL_MODE_M) |
                  (p_ctl_mode[ctl_mode] & CTL_MODE_M)) == ctl_index[i]) ||
                (((cfg_ctl & ~CTL_MODE_M) |
                  (p_ctl_mode[ctl_mode] & CTL_MODE_M)) ==
                 ((ctl_index[i] & CTL_MODE_M) | SD_NO_CTL)))
            {
                twice_min_edge_power =
                    ar9300_eep_def_get_max_edge_power(
                        p_eep_data, freq, i, IEEE80211_IS_CHAN_2GHZ(chan));

                HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
                    "    MATCH-EE_IDX %d: ch %d is2 %d "
                    "2xMinEdge %d chainmask %d chains %d\n",
                    i, freq, IEEE80211_IS_CHAN_2GHZ(chan),
                    twice_min_edge_power, tx_chainmask,
                    ar9300_get_ntxchains(tx_chainmask));

                if ((cfg_ctl & ~CTL_MODE_M) == SD_NO_CTL) {
                    /*
                     * Find the minimum of all CTL edge powers
                     * that apply to this channel
                     */
                    twice_max_edge_power =
                        AH_MIN(twice_max_edge_power, twice_min_edge_power);
                } else {
                    /* specific */
                    twice_max_edge_power = twice_min_edge_power;
                    break;
                }
            }
        }

        min_ctl_power = (u_int8_t)AH_MIN(twice_max_edge_power, scaled_power);

        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
            "    SEL-Min ctl_mode %d p_ctl_mode %d "
            "2xMaxEdge %d sP %d min_ctl_pwr %d\n",
            ctl_mode, p_ctl_mode[ctl_mode],
            twice_max_edge_power, scaled_power, min_ctl_power);

        /* Apply ctl mode to correct target power set */
        switch (p_ctl_mode[ctl_mode]) {
        case CTL_11B:
            for (i = ALL_TARGET_LEGACY_1L_5L; i <= ALL_TARGET_LEGACY_11S; i++) {
                p_pwr_array[i] =
                    (u_int8_t)AH_MIN(p_pwr_array[i], min_ctl_power);
            }
            break;
        case CTL_11A:
        case CTL_11G:
            for (i = ALL_TARGET_LEGACY_6_24; i <= ALL_TARGET_LEGACY_54; i++) {
                p_pwr_array[i] =
                    (u_int8_t)AH_MIN(p_pwr_array[i], min_ctl_power);
#ifdef ATH_BT_COEX
                if ((ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_3WIRE) ||
                    (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_MCI))
                {
                    if ((ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_LOWER_TX_PWR)
                        && (ahp->ah_bt_wlan_isolation 
                         < HAL_BT_COEX_ISOLATION_FOR_NO_COEX))
                    {

                        u_int8_t reduce_pow;
                        
                        reduce_pow = (HAL_BT_COEX_ISOLATION_FOR_NO_COEX 
                                     - ahp->ah_bt_wlan_isolation) << 1;

                        if (reduce_pow <= p_pwr_array[i]) {
                            p_pwr_array[i] -= reduce_pow;
                        }
                    }
                    if ((ahp->ah_bt_coex_flag & 
                          HAL_BT_COEX_FLAG_LOW_ACK_PWR) &&
                          (i != ALL_TARGET_LEGACY_36) &&
                          (i != ALL_TARGET_LEGACY_48) &&
                          (i != ALL_TARGET_LEGACY_54) &&
                          (p_ctl_mode[ctl_mode] == CTL_11G))
                    {
                        p_pwr_array[i] = 0;
                    }
                }
#endif
            }
            break;
        case CTL_5GHT20:
        case CTL_2GHT20:
            for (i = ALL_TARGET_HT20_0_8_16; i <= ALL_TARGET_HT20_23; i++) {
                p_pwr_array[i] =
                    (u_int8_t)AH_MIN(p_pwr_array[i], min_ctl_power);
#ifdef ATH_BT_COEX
                if (((ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_3WIRE) ||
                     (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_MCI)) &&
                    (ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_LOWER_TX_PWR) && 
                    (ahp->ah_bt_wlan_isolation 
                        < HAL_BT_COEX_ISOLATION_FOR_NO_COEX)) {

                    u_int8_t reduce_pow = (HAL_BT_COEX_ISOLATION_FOR_NO_COEX 
                                           - ahp->ah_bt_wlan_isolation) << 1;

                    if (reduce_pow <= p_pwr_array[i]) {
                        p_pwr_array[i] -= reduce_pow;
                    }
                }
#if ATH_SUPPORT_MCI
                else if ((ahp->ah_bt_coex_flag & 
                          HAL_BT_COEX_FLAG_MCI_MAX_TX_PWR) &&
                         (p_ctl_mode[ctl_mode] == CTL_2GHT20) &&
                         (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_MCI))
                {
                    u_int8_t max_pwr;

                    max_pwr = MS(mci_concur_tx_max_pwr[2][1],
                                 ATH_MCI_CONCUR_TX_LOWEST_PWR_MASK);
                    if (p_pwr_array[i] > max_pwr) {
                        p_pwr_array[i] = max_pwr;
                    }
                }
#endif
#endif
            }
            break;
        case CTL_11B_EXT:
#ifdef NOT_YET
            target_power_cck_ext.t_pow2x[0] = (u_int8_t)
                AH_MIN(target_power_cck_ext.t_pow2x[0], min_ctl_power);
#endif /* NOT_YET */
            break;
        case CTL_11A_EXT:
        case CTL_11G_EXT:
#ifdef NOT_YET
            target_power_ofdm_ext.t_pow2x[0] = (u_int8_t)
                AH_MIN(target_power_ofdm_ext.t_pow2x[0], min_ctl_power);
#endif /* NOT_YET */
            break;
        case CTL_5GHT40:
        case CTL_2GHT40:
            for (i = ALL_TARGET_HT40_0_8_16; i <= ALL_TARGET_HT40_23; i++) {
                p_pwr_array[i] = (u_int8_t)
                    AH_MIN(p_pwr_array[i], min_ctl_power);
#ifdef ATH_BT_COEX
                if (((ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_3WIRE) ||
                     (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_MCI)) &&
                    (ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_LOWER_TX_PWR) && 
                    (ahp->ah_bt_wlan_isolation 
                        < HAL_BT_COEX_ISOLATION_FOR_NO_COEX)) {

                    u_int8_t reduce_pow = (HAL_BT_COEX_ISOLATION_FOR_NO_COEX 
                                              - ahp->ah_bt_wlan_isolation) << 1;

                    if (reduce_pow <= p_pwr_array[i]) {
                        p_pwr_array[i] -= reduce_pow;
                    }
                }
#if ATH_SUPPORT_MCI
                else if ((ahp->ah_bt_coex_flag & 
                          HAL_BT_COEX_FLAG_MCI_MAX_TX_PWR) &&
                         (p_ctl_mode[ctl_mode] == CTL_2GHT40) &&
                         (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_MCI))
                {
                    u_int8_t max_pwr;

                    max_pwr = MS(mci_concur_tx_max_pwr[3][1],
                                 ATH_MCI_CONCUR_TX_LOWEST_PWR_MASK);
                    if (p_pwr_array[i] > max_pwr) {
                        p_pwr_array[i] = max_pwr;
                    }
                }
#endif
#endif
            }
            break;
        default:
            HALASSERT(0);
            break;
        }
    } /* end ctl mode checking */

    return AH_TRUE;
#undef EXT_ADDITIVE
#undef CTL_11A_EXT
#undef CTL_11G_EXT
#undef CTL_11B_EXT
#undef REDUCE_SCALED_POWER_BY_TWO_CHAIN
#undef REDUCE_SCALED_POWER_BY_THREE_CHAIN
}

/**************************************************************
 * ar9300_eeprom_set_transmit_power
 *
 * Set the transmit power in the baseband for the given
 * operating channel and mode.
 */
HAL_STATUS
ar9300_eeprom_set_transmit_power(struct ath_hal *ah,
    ar9300_eeprom_t *p_eep_data, const struct ieee80211_channel *chan, u_int16_t cfg_ctl,
    u_int16_t antenna_reduction, u_int16_t twice_max_regulatory_power,
    u_int16_t power_limit)
{
#define ABS(_x, _y) ((int)_x > (int)_y ? (int)_x - (int)_y : (int)_y - (int)_x)
#define INCREASE_MAXPOW_BY_TWO_CHAIN     6  /* 10*log10(2)*2 */
#define INCREASE_MAXPOW_BY_THREE_CHAIN   10 /* 10*log10(3)*2 */
    u_int8_t target_power_val_t2[ar9300_rate_size];
    u_int8_t target_power_val_t2_eep[ar9300_rate_size];
    int16_t twice_array_gain = 0, max_power_level = 0;
    struct ath_hal_9300 *ahp = AH9300(ah);
    int  i = 0;
    u_int32_t tmp_paprd_rate_mask = 0, *tmp_ptr = NULL;
    int      paprd_scale_factor = 5;
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    u_int8_t *ptr_mcs_rate2power_table_index;
    u_int8_t mcs_rate2power_table_index_ht20[24] =
    {
        ALL_TARGET_HT20_0_8_16,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_4,
        ALL_TARGET_HT20_5,
        ALL_TARGET_HT20_6,
        ALL_TARGET_HT20_7,
        ALL_TARGET_HT20_0_8_16,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_12,
        ALL_TARGET_HT20_13,
        ALL_TARGET_HT20_14,
        ALL_TARGET_HT20_15,
        ALL_TARGET_HT20_0_8_16,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_1_3_9_11_17_19,
        ALL_TARGET_HT20_20,
        ALL_TARGET_HT20_21,
        ALL_TARGET_HT20_22,
        ALL_TARGET_HT20_23
    };

    u_int8_t mcs_rate2power_table_index_ht40[24] =
    {
        ALL_TARGET_HT40_0_8_16,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_4,
        ALL_TARGET_HT40_5,
        ALL_TARGET_HT40_6,
        ALL_TARGET_HT40_7,
        ALL_TARGET_HT40_0_8_16,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_12,
        ALL_TARGET_HT40_13,
        ALL_TARGET_HT40_14,
        ALL_TARGET_HT40_15,
        ALL_TARGET_HT40_0_8_16,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_1_3_9_11_17_19,
        ALL_TARGET_HT40_20,
        ALL_TARGET_HT40_21,
        ALL_TARGET_HT40_22,
        ALL_TARGET_HT40_23,
    };

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s[%d] +++chan %d,cfgctl 0x%04x  "
        "antenna_reduction 0x%04x, twice_max_regulatory_power 0x%04x "
        "power_limit 0x%04x\n",
        __func__, __LINE__, ichan->channel, cfg_ctl,
        antenna_reduction, twice_max_regulatory_power, power_limit);
    ar9300_set_target_power_from_eeprom(ah, ichan->channel, target_power_val_t2);

    if (ar9300_eeprom_get(ahp, EEP_PAPRD_ENABLED)) {
        if (IEEE80211_IS_CHAN_2GHZ(chan)) {
            if (IEEE80211_IS_CHAN_HT40(chan)) {
                tmp_paprd_rate_mask =
                    p_eep_data->modal_header_2g.paprd_rate_mask_ht40;
                tmp_ptr = &AH9300(ah)->ah_2g_paprd_rate_mask_ht40;
            } else {
                tmp_paprd_rate_mask =
                    p_eep_data->modal_header_2g.paprd_rate_mask_ht20;
                tmp_ptr = &AH9300(ah)->ah_2g_paprd_rate_mask_ht20;
            }
        } else {
            if (IEEE80211_IS_CHAN_HT40(chan)) {
                tmp_paprd_rate_mask =
                    p_eep_data->modal_header_5g.paprd_rate_mask_ht40;
                tmp_ptr = &AH9300(ah)->ah_5g_paprd_rate_mask_ht40;
            } else {
                tmp_paprd_rate_mask =
                    p_eep_data->modal_header_5g.paprd_rate_mask_ht20;
                tmp_ptr = &AH9300(ah)->ah_5g_paprd_rate_mask_ht20;
            }
        }
        AH_PAPRD_GET_SCALE_FACTOR(
            paprd_scale_factor, p_eep_data, IEEE80211_IS_CHAN_2GHZ(chan), ichan->channel);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] paprd_scale_factor %d\n",
            __func__, __LINE__, paprd_scale_factor);
        /* PAPRD is not done yet, Scale down the EEP power */
        if (IEEE80211_IS_CHAN_HT40(chan)) {
            ptr_mcs_rate2power_table_index =
                &mcs_rate2power_table_index_ht40[0];
        } else {
            ptr_mcs_rate2power_table_index =
                &mcs_rate2power_table_index_ht20[0];
        }
        if (! ichan->paprd_table_write_done) {
            for (i = 0;  i < 24; i++) {
                /* PAPRD is done yet, so Scale down Power for PAPRD Rates*/
                if (tmp_paprd_rate_mask & (1 << i)) {
                    target_power_val_t2[ptr_mcs_rate2power_table_index[i]] -=
                        paprd_scale_factor;
                    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                        "%s[%d]: Chan %d "
                        "Scale down target_power_val_t2[%d] = 0x%04x\n",
                        __func__, __LINE__,
                        ichan->channel, i, target_power_val_t2[i]);
                }
            }
        } else {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s[%d]: PAPRD Done No TGT PWR Scaling\n", __func__, __LINE__);
        }
    }

    /* Save the Target power for future use */
    OS_MEMCPY(target_power_val_t2_eep, target_power_val_t2,
                                   sizeof(target_power_val_t2));
    ar9300_eeprom_set_power_per_rate_table(ah, p_eep_data, chan,
                                     target_power_val_t2, cfg_ctl,
                                     antenna_reduction,
                                     twice_max_regulatory_power,
                                     power_limit, 0);
    
    /* Save this for quick lookup */
    ahp->reg_dmn = ath_hal_getctl(ah, chan);

    /*
     * Always use CDD/direct per rate power table for register based approach.
     * For FCC, CDD calculations should factor in the array gain, hence 
     * this adjust call. ETSI and MKK does not have this requirement.
     */
    if (is_reg_dmn_fcc(ahp->reg_dmn)) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: FCC regdomain, calling reg_txpower_cdd\n",
            __func__);
        ar9300_adjust_reg_txpower_cdd(ah, target_power_val_t2);
    }

    if (ar9300_eeprom_get(ahp, EEP_PAPRD_ENABLED)) {
        for (i = 0;  i < ar9300_rate_size; i++) {
            /*
             * EEPROM TGT PWR is not same as current TGT PWR,
             * so Disable PAPRD for this rate.
             * Some of APs might ask to reduce Target Power,
             * if target power drops significantly,
             * disable PAPRD for that rate.
             */
            if (tmp_paprd_rate_mask & (1 << i)) {
                if (ABS(target_power_val_t2_eep[i], target_power_val_t2[i]) >
                    paprd_scale_factor)
                {
                    tmp_paprd_rate_mask &= ~(1 << i);
                    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                        "%s: EEP TPC[%02d] 0x%08x "
                        "Curr TPC[%02d] 0x%08x mask = 0x%08x\n",
                        __func__, i, target_power_val_t2_eep[i], i,
                        target_power_val_t2[i], tmp_paprd_rate_mask);
                }
            }
            
        }
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Chan %d After tmp_paprd_rate_mask = 0x%08x\n",
            __func__, ichan->channel, tmp_paprd_rate_mask);
        if (tmp_ptr) {
            *tmp_ptr = tmp_paprd_rate_mask;
        }
    }

    /* Write target power array to registers */
    ar9300_transmit_power_reg_write(ah, target_power_val_t2);
    
    /* Write target power for self generated frames to the TPC register */
    ar9300_selfgen_tpc_reg_write(ah, chan, target_power_val_t2);

    /* GreenTx or Paprd */
    if (ah->ah_config.ath_hal_sta_update_tx_pwr_enable || 
        AH_PRIVATE(ah)->ah_caps.halPaprdEnabled) 
    {
        if (AR_SREV_POSEIDON(ah)) {
            /*For HAL_RSSI_TX_POWER_NONE array*/
            OS_MEMCPY(ahp->ah_default_tx_power, 
                target_power_val_t2, 
                sizeof(target_power_val_t2));
            /* Get defautl tx related register setting for GreenTx */
            /* Record OB/DB */
            ahp->ah_ob_db1[POSEIDON_STORED_REG_OBDB] = 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF2);
            /* Record TPC settting */
            ahp->ah_ob_db1[POSEIDON_STORED_REG_TPC] =
                OS_REG_READ(ah, AR_TPC);
            /* Record BB_powertx_rate9 setting */ 
            ahp->ah_ob_db1[POSEIDON_STORED_REG_BB_PWRTX_RATE9] = 
                OS_REG_READ(ah, AR_PHY_BB_POWERTX_RATE9);
        }
    }

    /*
     * Return tx power used to iwconfig.
     * Since power is rate dependent, use one of the indices from the
     * AR9300_Rates enum to select an entry from target_power_val_t2[]
     * to report.
     * Currently returns the power for HT40 MCS 0, HT20 MCS 0, or OFDM 6 Mbps
     * as CCK power is less interesting (?).
     */
    i = ALL_TARGET_LEGACY_6_24;         /* legacy */
    if (IEEE80211_IS_CHAN_HT40(chan)) {
        i = ALL_TARGET_HT40_0_8_16;     /* ht40 */
    } else if (IEEE80211_IS_CHAN_HT20(chan)) {
        i = ALL_TARGET_HT20_0_8_16;     /* ht20 */
    }
    max_power_level = target_power_val_t2[i];
    /* Adjusting the ah_max_power_level based on chains and antennaGain*/
    switch (ar9300_get_ntxchains(((ahp->ah_tx_chainmaskopt > 0) ?
                                    ahp->ah_tx_chainmaskopt : ahp->ah_tx_chainmask)))
    {
        case 1:
            break;
        case 2:
            twice_array_gain = (ahp->twice_antenna_gain >= ahp->twice_antenna_reduction)? 0: 
                               ((int16_t)AH_MIN((ahp->twice_antenna_reduction -
                                   (ahp->twice_antenna_gain + INCREASE_MAXPOW_BY_TWO_CHAIN)), 0));
            /* Adjusting maxpower with antennaGain */
            max_power_level -= twice_array_gain;
            /* Adjusting maxpower based on chain */
            max_power_level += INCREASE_MAXPOW_BY_TWO_CHAIN;
            break;
        case 3:
            twice_array_gain = (ahp->twice_antenna_gain >= ahp->twice_antenna_reduction)? 0:
                               ((int16_t)AH_MIN((ahp->twice_antenna_reduction -
                                   (ahp->twice_antenna_gain + INCREASE_MAXPOW_BY_THREE_CHAIN)), 0));

            /* Adjusting maxpower with antennaGain */
            max_power_level -= twice_array_gain;
            /* Adjusting maxpower based on chain */
            max_power_level += INCREASE_MAXPOW_BY_THREE_CHAIN;
            break;
        default:
            HALASSERT(0); /* Unsupported number of chains */
    }
    AH_PRIVATE(ah)->ah_maxPowerLevel = (int8_t)max_power_level;

    ar9300_calibration_apply(ah, ichan->channel);
#undef ABS

    /* Handle per packet TPC initializations */
    if (ah->ah_config.ath_hal_desc_tpc) {
        /* Transmit Power per-rate per-chain  are  computed here. A separate
         * power table is maintained for different MIMO modes (i.e. TXBF ON,
         * STBC) to enable easy lookup during packet transmit. 
         * The reason for maintaing each of these tables per chain is that
         * the transmit power used for different number of chains is different
         * depending on whether the power has been limited by the target power,
         * the regulatory domain  or the CTL limits.
         */
        u_int mode = ath_hal_get_curmode(ah, chan);
        u_int32_t val = 0;
        u_int8_t chainmasks[AR9300_MAX_CHAINS] =
            {OSPREY_1_CHAINMASK, OSPREY_2LOHI_CHAINMASK, OSPREY_3_CHAINMASK};
        for (i = 0; i < AR9300_MAX_CHAINS; i++) {
            OS_MEMCPY(target_power_val_t2, target_power_val_t2_eep,
                                   sizeof(target_power_val_t2_eep));
            ar9300_eeprom_set_power_per_rate_table(ah, p_eep_data, chan,
                                     target_power_val_t2, cfg_ctl,
                                     antenna_reduction,
                                     twice_max_regulatory_power,
                                     power_limit, chainmasks[i]);
            HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
                 " Channel = %d Chainmask = %d, Upper Limit = [%2d.%1d dBm]\n",
                                       ichan->channel, i, ahp->upper_limit[i]/2,
                                       ahp->upper_limit[i]%2 * 5);
            ar9300_init_rate_txpower(ah, mode, chan, target_power_val_t2,
                                                           chainmasks[i]);
                                     
        }

        /* Enable TPC */
        OS_REG_WRITE(ah, AR_PHY_PWRTX_MAX, AR_PHY_PWRTX_MAX_TPC_ENABLE);
        /*
         * Disable per chain power reduction since we are already 
         * accounting for this in our calculations 
         */
        val = OS_REG_READ(ah, AR_PHY_POWER_TX_SUB);
        if (AR_SREV_WASP(ah)) {
            OS_REG_WRITE(ah, AR_PHY_POWER_TX_SUB,
                       val & AR_PHY_POWER_TX_SUB_2_DISABLE);
        } else {
            OS_REG_WRITE(ah, AR_PHY_POWER_TX_SUB,
                       val & AR_PHY_POWER_TX_SUB_3_DISABLE);
        }
    }

    return HAL_OK;
}

/**************************************************************
 * ar9300_eeprom_set_addac
 *
 * Set the ADDAC from eeprom.
 */
void
ar9300_eeprom_set_addac(struct ath_hal *ah, struct ieee80211_channel *chan)
{

    HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
                 "FIXME: ar9300_eeprom_def_set_addac called\n");
#if 0
    MODAL_EEPDEF_HEADER *p_modal;
    struct ath_hal_9300 *ahp = AH9300(ah);
    ar9300_eeprom_t *eep = &ahp->ah_eeprom.def;
    u_int8_t biaslevel;

    if (AH_PRIVATE(ah)->ah_macVersion != AR_SREV_VERSION_SOWL) {
        return;
    }

    HALASSERT(owl_get_eepdef_ver(ahp) == AR9300_EEP_VER);

    /* Xpa bias levels in eeprom are valid from rev 14.7 */
    if (owl_get_eepdef_rev(ahp) < AR9300_EEP_MINOR_VER_7) {
        return;
    }

    if (ahp->ah_emu_eeprom) {
        return;
    }

    p_modal = &(eep->modal_header[IEEE80211_IS_CHAN_2GHZ(chan)]);

    if (p_modal->xpa_bias_lvl != 0xff) {
        biaslevel = p_modal->xpa_bias_lvl;
    } else {
        /* Use freqeuncy specific xpa bias level */
        u_int16_t reset_freq_bin, freq_bin, freq_count = 0;
        CHAN_CENTERS centers;

        ar9300_get_channel_centers(ah, chan, &centers);

        reset_freq_bin = FREQ2FBIN(centers.synth_center, IEEE80211_IS_CHAN_2GHZ(chan));
        freq_bin = p_modal->xpa_bias_lvl_freq[0] & 0xff;
        biaslevel = (u_int8_t)(p_modal->xpa_bias_lvl_freq[0] >> 14);

        freq_count++;

        while (freq_count < 3) {
            if (p_modal->xpa_bias_lvl_freq[freq_count] == 0x0) {
                break;
            }

            freq_bin = p_modal->xpa_bias_lvl_freq[freq_count] & 0xff;
            if (reset_freq_bin >= freq_bin) {
                biaslevel =
                    (u_int8_t)(p_modal->xpa_bias_lvl_freq[freq_count] >> 14);
            } else {
                break;
            }
            freq_count++;
        }
    }

    /* Apply bias level to the ADDAC values in the INI array */
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        INI_RA(&ahp->ah_ini_addac, 7, 1) =
            (INI_RA(&ahp->ah_ini_addac, 7, 1) & (~0x18)) | biaslevel << 3;
    } else {
        INI_RA(&ahp->ah_ini_addac, 6, 1) =
            (INI_RA(&ahp->ah_ini_addac, 6, 1) & (~0xc0)) | biaslevel << 6;
    }
#endif
}

u_int
ar9300_eeprom_dump_support(struct ath_hal *ah, void **pp_e)
{
    *pp_e = &(AH9300(ah)->ah_eeprom);
    return sizeof(ar9300_eeprom_t);
}

u_int8_t
ar9300_eeprom_get_num_ant_config(struct ath_hal_9300 *ahp,
    HAL_FREQ_BAND freq_band)
{
#if 0
    ar9300_eeprom_t  *eep = &ahp->ah_eeprom.def;
    MODAL_EEPDEF_HEADER *p_modal =
        &(eep->modal_header[HAL_FREQ_BAND_2GHZ == freq_band]);
    BASE_EEPDEF_HEADER  *p_base  = &eep->base_eep_header;
    u_int8_t         num_ant_config;

    num_ant_config = 1; /* default antenna configuration */

    if (p_base->version >= 0x0E0D) {
        if (p_modal->use_ant1) {
            num_ant_config += 1;
        }
    }

    return num_ant_config;
#else
    return 1;
#endif
}

HAL_STATUS
ar9300_eeprom_get_ant_cfg(struct ath_hal_9300 *ahp,
  const struct ieee80211_channel *chan,
  u_int8_t index, u_int16_t *config)
{
#if 0
    ar9300_eeprom_t  *eep = &ahp->ah_eeprom.def;
    MODAL_EEPDEF_HEADER *p_modal = &(eep->modal_header[IEEE80211_IS_CHAN_2GHZ(chan)]);
    BASE_EEPDEF_HEADER  *p_base  = &eep->base_eep_header;

    switch (index) {
    case 0:
        *config = p_modal->ant_ctrl_common & 0xFFFF;
        return HAL_OK;
    case 1:
        if (p_base->version >= 0x0E0D) {
            if (p_modal->use_ant1) {
                *config = ((p_modal->ant_ctrl_common & 0xFFFF0000) >> 16);
                return HAL_OK;
            }
        }
        break;
    default:
        break;
    }
#endif
    return HAL_EINVAL;
}

u_int8_t*
ar9300_eeprom_get_cust_data(struct ath_hal_9300 *ahp)
{
    return (u_int8_t *)ahp;
}

#ifdef UNUSED
static inline HAL_STATUS
ar9300_check_eeprom(struct ath_hal *ah)
{
#if 0
    u_int32_t sum = 0, el;
    u_int16_t *eepdata;
    int i;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL need_swap = AH_FALSE;
    ar9300_eeprom_t *eep = (ar9300_eeprom_t *)&ahp->ah_eeprom.def;
    u_int16_t magic, magic2;
    int addr;
    u_int16_t temp;

    /*
    ** We need to check the EEPROM data regardless of if it's in flash or
    ** in EEPROM.
    */

    if (!ahp->ah_priv.priv.ah_eeprom_read(
            ah, AR9300_EEPROM_MAGIC_OFFSET, &magic))
    {
        HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: Reading Magic # failed\n", __func__);
        return AH_FALSE;
    }

    HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: Read Magic = 0x%04X\n", __func__, magic);

    if (!ar9300_eep_data_in_flash(ah)) {

        if (magic != AR9300_EEPROM_MAGIC) {
            magic2 = SWAP16(magic);

            if (magic2 == AR9300_EEPROM_MAGIC) {
                need_swap = AH_TRUE;
                eepdata = (u_int16_t *)(&ahp->ah_eeprom);

                for (addr = 0;
                     addr < sizeof(ar9300_eeprom_t) / sizeof(u_int16_t);
                     addr++)
                {
                    temp = SWAP16(*eepdata);
                    *eepdata = temp;
                    eepdata++;

                    HALDEBUG(ah, HAL_DEBUG_EEPROM_DUMP, "0x%04X  ", *eepdata);
                    if (((addr + 1) % 6) == 0) {
                        HALDEBUG(ah, HAL_DEBUG_EEPROM_DUMP, "\n");
                    }
                }
            } else {
                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "Invalid EEPROM Magic. endianness missmatch.\n");
                return HAL_EEBADSUM;
            }
        }
    } else {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
                 "EEPROM being read from flash @0x%p\n", AH_PRIVATE(ah)->ah_st);
    }

    HALDEBUG(ah, HAL_DEBUG_EEPROM, "need_swap = %s.\n", need_swap?"True":"False");

    if (need_swap) {
        el = SWAP16(ahp->ah_eeprom.def.base_eep_header.length);
    } else {
        el = ahp->ah_eeprom.def.base_eep_header.length;
    }

    eepdata = (u_int16_t *)(&ahp->ah_eeprom.def);
    for (i = 0;
         i < AH_MIN(el, sizeof(ar9300_eeprom_t)) / sizeof(u_int16_t);
         i++) {
        sum ^= *eepdata++;
    }

    if (need_swap) {
        /*
        *  preddy: EEPROM endianness does not match. So change it
        *  8bit values in eeprom data structure does not need to be swapped
        *  Only >8bits (16 & 32) values need to be swapped
        *  If a new 16 or 32 bit field is added to the EEPROM contents,
        *  please make sure to swap the field here
        */
        u_int32_t integer, j;
        u_int16_t word;

        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "EEPROM Endianness is not native.. Changing \n");

        /* convert Base Eep header */
        word = SWAP16(eep->base_eep_header.length);
        eep->base_eep_header.length = word;

        word = SWAP16(eep->base_eep_header.checksum);
        eep->base_eep_header.checksum = word;

        word = SWAP16(eep->base_eep_header.version);
        eep->base_eep_header.version = word;

        word = SWAP16(eep->base_eep_header.reg_dmn[0]);
        eep->base_eep_header.reg_dmn[0] = word;

        word = SWAP16(eep->base_eep_header.reg_dmn[1]);
        eep->base_eep_header.reg_dmn[1] = word;

        word = SWAP16(eep->base_eep_header.rf_silent);
        eep->base_eep_header.rf_silent = word;

        word = SWAP16(eep->base_eep_header.blue_tooth_options);
        eep->base_eep_header.blue_tooth_options = word;

        word = SWAP16(eep->base_eep_header.device_cap);
        eep->base_eep_header.device_cap = word;

        /* convert Modal Eep header */
        for (j = 0; j < ARRAY_LENGTH(eep->modal_header); j++) {
            MODAL_EEPDEF_HEADER *p_modal = &eep->modal_header[j];
            integer = SWAP32(p_modal->ant_ctrl_common);
            p_modal->ant_ctrl_common = integer;

            for (i = 0; i < AR9300_MAX_CHAINS; i++) {
                integer = SWAP32(p_modal->ant_ctrl_chain[i]);
                p_modal->ant_ctrl_chain[i] = integer;
            }

            for (i = 0; i < AR9300_EEPROM_MODAL_SPURS; i++) {
                word = SWAP16(p_modal->spur_chans[i].spur_chan);
                p_modal->spur_chans[i].spur_chan = word;
            }
        }
    }

    /* Check CRC - Attach should fail on a bad checksum */
    if (sum != 0xffff || owl_get_eepdef_ver(ahp) != AR9300_EEP_VER ||
        owl_get_eepdef_rev(ahp) < AR9300_EEP_NO_BACK_VER) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "Bad EEPROM checksum 0x%x or revision 0x%04x\n",
            sum, owl_get_eepdef_ver(ahp));
        return HAL_EEBADSUM;
    }
#ifdef EEPROM_DUMP
    ar9300_eeprom_def_dump(ah, eep);
#endif

#if 0
#ifdef AH_AR9300_OVRD_TGT_PWR

    /*
     * 14.4 EEPROM contains low target powers.
     * Hardcode until EEPROM > 14.4
     */
    if (owl_get_eepdef_ver(ahp) == 14 && owl_get_eepdef_rev(ahp) <= 4) {
        MODAL_EEPDEF_HEADER *p_modal;

#ifdef EEPROM_DUMP
        HALDEBUG(ah,  HAL_DEBUG_POWER_OVERRIDE, "Original Target Powers\n");
        ar9300_eep_def_dump_tgt_power(ah, eep);
#endif
        HALDEBUG(ah,  HAL_DEBUG_POWER_OVERRIDE, 
                "Override Target Powers. EEPROM Version is %d.%d, "
                "Device Type %d\n",
                owl_get_eepdef_ver(ahp),
                owl_get_eepdef_rev(ahp),
                eep->base_eep_header.device_type);


        ar9300_eep_def_override_tgt_power(ah, eep);

        if (eep->base_eep_header.device_type == 5) {
            /* for xb72 only: improve transmit EVM for interop */
            p_modal = &eep->modal_header[1];
            p_modal->tx_frame_to_data_start = 0x23;
            p_modal->tx_frame_to_xpa_on = 0x23;
            p_modal->tx_frame_to_pa_on = 0x23;
    }

#ifdef EEPROM_DUMP
        HALDEBUG(ah, HAL_DEBUG_POWER_OVERRIDE, "Modified Target Powers\n");
        ar9300_eep_def_dump_tgt_power(ah, eep);
#endif
        }
#endif /* AH_AR9300_OVRD_TGT_PWR */
#endif
#endif
    return HAL_OK;
}
#endif

static u_int16_t
ar9300_eeprom_get_spur_chan(struct ath_hal *ah, int i, HAL_BOOL is_2ghz)
{
    u_int16_t   spur_val = AR_NO_SPUR;
#if 0
    struct ath_hal_9300 *ahp = AH9300(ah);
    ar9300_eeprom_t *eep = (ar9300_eeprom_t *)&ahp->ah_eeprom;

    HALASSERT(i <  AR_EEPROM_MODAL_SPURS );

    HALDEBUG(ah, HAL_DEBUG_ANI,
             "Getting spur idx %d is2Ghz. %d val %x\n",
             i, is_2ghz,
             AH_PRIVATE(ah)->ah_config.ath_hal_spur_chans[i][is_2ghz]);

    switch (AH_PRIVATE(ah)->ah_config.ath_hal_spur_mode) {
    case SPUR_DISABLE:
        /* returns AR_NO_SPUR */
        break;
    case SPUR_ENABLE_IOCTL:
        spur_val = AH_PRIVATE(ah)->ah_config.ath_hal_spur_chans[i][is_2ghz];
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "Getting spur val from new loc. %d\n", spur_val);
        break;
    case SPUR_ENABLE_EEPROM:
        spur_val = eep->modal_header[is_2ghz].spur_chans[i].spur_chan;
        break;

    }
#endif
    return spur_val;
}

#ifdef UNUSED
static inline HAL_BOOL
ar9300_fill_eeprom(struct ath_hal *ah)
{
    return ar9300_eeprom_restore(ah);
}
#endif

u_int16_t
ar9300_eeprom_struct_size(void) 
{
    return sizeof(ar9300_eeprom_t);
}

int ar9300_eeprom_struct_default_many(void)
{
    return ARRAY_LENGTH(default9300);
}


ar9300_eeprom_t *
ar9300_eeprom_struct_default(int default_index) 
{
    if (default_index >= 0 &&
        default_index < ARRAY_LENGTH(default9300))
    {
        return default9300[default_index];
    } else {
        return 0;
    }
}

ar9300_eeprom_t *
ar9300_eeprom_struct_default_find_by_id(int id) 
{
    int it;

    for (it = 0; it < ARRAY_LENGTH(default9300); it++) {
        if (default9300[it] != 0 && default9300[it]->template_version == id) {
            return default9300[it];
        }
    }
    return 0;
}


HAL_BOOL
ar9300_calibration_data_read_flash(struct ath_hal *ah, long address,
    u_int8_t *buffer, int many)
{

    if (((address) < 0) || ((address + many) > AR9300_EEPROM_SIZE - 1)) {
        return AH_FALSE;
    }
    return AH_FALSE;
}

HAL_BOOL
ar9300_calibration_data_read_eeprom(struct ath_hal *ah, long address,
    u_int8_t *buffer, int many)
{
    int i;
    u_int8_t value[2];
    unsigned long eep_addr;
    unsigned long byte_addr;
    u_int16_t *svalue;

    if (((address) < 0) || ((address + many) > AR9300_EEPROM_SIZE)) {
        return AH_FALSE;
    }

    for (i = 0; i < many; i++) {
        eep_addr = (u_int16_t) (address + i) / 2;
        byte_addr = (u_int16_t) (address + i) % 2;
        svalue = (u_int16_t *) value;
        if (! ath_hal_eepromRead(ah, eep_addr, svalue)) {
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: Unable to read eeprom region \n", __func__);
            return AH_FALSE;
        }  
        buffer[i] = (*svalue >> (8 * byte_addr)) & 0xff;
    }  
    return AH_TRUE;
}

HAL_BOOL
ar9300_calibration_data_read_otp(struct ath_hal *ah, long address,
    u_int8_t *buffer, int many, HAL_BOOL is_wifi)
{
    int i;
    unsigned long eep_addr;
    unsigned long byte_addr;
    u_int32_t svalue;

    if (((address) < 0) || ((address + many) > 0x400)) {
        return AH_FALSE;
    }

    for (i = 0; i < many; i++) {
        eep_addr = (u_int16_t) (address + i) / 4; /* otp is 4 bytes long???? */
        byte_addr = (u_int16_t) (address + i) % 4;
        if (!ar9300_otp_read(ah, eep_addr, &svalue, is_wifi)) {
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: Unable to read otp region \n", __func__);
            return AH_FALSE;
        }  
        buffer[i] = (svalue >> (8 * byte_addr)) & 0xff;
    }  
    return AH_TRUE;
}

#ifdef ATH_CAL_NAND_FLASH
HAL_BOOL
ar9300_calibration_data_read_nand(struct ath_hal *ah, long address,
    u_int8_t *buffer, int many)
{
    int ret_len;
    int ret_val = 1;
    
      /* Calling OS based API to read NAND */
    ret_val = OS_NAND_FLASH_READ(ATH_CAL_NAND_PARTITION, address, many, &ret_len, buffer);
    
    return (ret_val ? AH_FALSE: AH_TRUE);
}
#endif

HAL_BOOL
ar9300_calibration_data_read(struct ath_hal *ah, long address,
    u_int8_t *buffer, int many)
{
    switch (AH9300(ah)->calibration_data_source) {
    case calibration_data_flash:
        return ar9300_calibration_data_read_flash(ah, address, buffer, many);
    case calibration_data_eeprom:
        return ar9300_calibration_data_read_eeprom(ah, address, buffer, many);
    case calibration_data_otp:
        return ar9300_calibration_data_read_otp(ah, address, buffer, many, 1);
#ifdef ATH_CAL_NAND_FLASH
    case calibration_data_nand:
        return ar9300_calibration_data_read_nand(ah,address,buffer,many);
#endif

    }
    return AH_FALSE;
}


HAL_BOOL 
ar9300_calibration_data_read_array(struct ath_hal *ah, int address,
    u_int8_t *buffer, int many)
{
    int it;

    for (it = 0; it < many; it++) {
        (void)ar9300_calibration_data_read(ah, address - it, buffer + it, 1);
    }
    return AH_TRUE;
}


/*
 * the address where the first configuration block is written
 */
static const int base_address = 0x3ff;                /* 1KB */
static const int base_address_512 = 0x1ff;            /* 512Bytes */

/*
 * the address where the NAND first configuration block is written
 */
#ifdef ATH_CAL_NAND_FLASH
static const int base_address_nand = AR9300_FLASH_CAL_START_OFFSET;
#endif


/*
 * the lower limit on configuration data
 */
static const int low_limit = 0x040;

/*
 * returns size of the physical eeprom in bytes.
 * 1024 and 2048 are normal sizes. 
 * 0 means there is no eeprom. 
 */ 
int32_t 
ar9300_eeprom_size(struct ath_hal *ah)
{
    u_int16_t data;
    /*
     * first we'll try for 4096 bytes eeprom
     */
    if (ar9300_eeprom_read_word(ah, 2047, &data)) {
        if (data != 0) {
            return 4096;
        }
    }
    /*
     * then we'll try for 2048 bytes eeprom
     */
    if (ar9300_eeprom_read_word(ah, 1023, &data)) {
        if (data != 0) {
            return 2048;
        }
    }
    /*
     * then we'll try for 1024 bytes eeprom
     */
    if (ar9300_eeprom_read_word(ah, 511, &data)) {
        if (data != 0) {
            return 1024;
        }
    }
    return 0;
}

/*
 * returns size of the physical otp in bytes.
 * 1024 and 2048 are normal sizes. 
 * 0 means there is no eeprom. 
 */ 
int32_t 
ar9300_otp_size(struct ath_hal *ah)
{
    if (AR_SREV_POSEIDON(ah) || AR_SREV_HORNET(ah)) {
        return base_address_512+1;
    } else {
        return base_address+1;
    }
}


/*
 * find top of memory
 */
int
ar9300_eeprom_base_address(struct ath_hal *ah)
{
    int size;

    if (AH9300(ah)->calibration_data_source == calibration_data_otp) {
		return ar9300_otp_size(ah)-1;
	}
	else
	{
		size = ar9300_eeprom_size(ah);
		if (size > 0) {
			return size - 1;
		} else {
			return ar9300_otp_size(ah)-1;
		}
	}
}

int
ar9300_eeprom_volatile(struct ath_hal *ah)
{
    if (AH9300(ah)->calibration_data_source == calibration_data_otp) {
        return 0;        /* no eeprom, use otp */
    } else {
        return 1;        /* board has eeprom or flash */
    }
}

/*
 * need to change this to look for the pcie data in the low parts of memory
 * cal data needs to stop a few locations above 
 */
int
ar9300_eeprom_low_limit(struct ath_hal *ah)
{
    return low_limit;
}

u_int16_t
ar9300_compression_checksum(u_int8_t *data, int dsize)
{
    int it;
    int checksum = 0;

    for (it = 0; it < dsize; it++) {
        checksum += data[it];
        checksum &= 0xffff;
    }

    return checksum;
}

int
ar9300_compression_header_unpack(u_int8_t *best, int *code, int *reference,
    int *length, int *major, int *minor)
{
    unsigned long value[4];

    value[0] = best[0];
    value[1] = best[1];
    value[2] = best[2];
    value[3] = best[3];
    *code = ((value[0] >> 5) & 0x0007);
    *reference = (value[0] & 0x001f) | ((value[1] >> 2) & 0x0020);
    *length = ((value[1] << 4) & 0x07f0) | ((value[2] >> 4) & 0x000f);
    *major = (value[2] & 0x000f);
    *minor = (value[3] & 0x00ff);

    return 4;
}


static HAL_BOOL
ar9300_uncompress_block(struct ath_hal *ah, u_int8_t *mptr, int mdata_size,
    u_int8_t *block, int size)
{
    int it;
    int spot;
    int offset;
    int length;

    spot = 0;
    for (it = 0; it < size; it += (length + 2)) {
        offset = block[it];
        offset &= 0xff;
        spot += offset;
        length = block[it + 1];
        length &= 0xff;
        if (length > 0 && spot >= 0 && spot + length <= mdata_size) {
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: Restore at %d: spot=%d offset=%d length=%d\n",
                __func__, it, spot, offset, length);
            OS_MEMCPY(&mptr[spot], &block[it + 2], length);
            spot += length;
        } else if (length > 0) {
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: Bad restore at %d: spot=%d offset=%d length=%d\n",
                __func__, it, spot, offset, length);
            return AH_FALSE;
        }
    }
    return AH_TRUE;
}

static int
ar9300_eeprom_restore_internal_address(struct ath_hal *ah,
    ar9300_eeprom_t *mptr, int mdata_size, int cptr, u_int8_t blank)
{
    u_int8_t word[MOUTPUT]; 
    ar9300_eeprom_t *dptr; /* was uint8 */
    int code;
    int reference, length, major, minor;
    int osize;
    int it;
    int restored;
    u_int16_t checksum, mchecksum;

    restored = 0;
    for (it = 0; it < MSTATE; it++) {            
        (void) ar9300_calibration_data_read_array(
            ah, cptr, word, compression_header_length);
        if (word[0] == blank && word[1] == blank && word[2] == blank && word[3] == blank)
        {
            break;
        }
        ar9300_compression_header_unpack(
            word, &code, &reference, &length, &major, &minor);
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: Found block at %x: "
            "code=%d ref=%d length=%d major=%d minor=%d\n",
            __func__, cptr, code, reference, length, major, minor);
#ifdef DONTUSE
        if (length >= 1024) {
            HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: Skipping bad header\n", __func__);
            cptr -= compression_header_length;
            continue;
        }
#endif
        osize = length;                
        (void) ar9300_calibration_data_read_array(
            ah, cptr, word,
            compression_header_length + osize + compression_checksum_length);
        checksum = ar9300_compression_checksum(
            &word[compression_header_length], length);
        mchecksum =
            word[compression_header_length + osize] |
            (word[compression_header_length + osize + 1] << 8);
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: checksum %x %x\n", __func__, checksum, mchecksum);
        if (checksum == mchecksum) {
            switch (code) {
            case _compress_none:
                if (length != mdata_size) {
                    HALDEBUG(ah, HAL_DEBUG_EEPROM,
                        "%s: EEPROM structure size mismatch "
                        "memory=%d eeprom=%d\n", __func__, mdata_size, length);
                    return -1;
                }
                OS_MEMCPY((u_int8_t *)mptr,
                    (u_int8_t *)(word + compression_header_length), length);
                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "%s: restored eeprom %d: uncompressed, length %d\n",
                    __func__, it, length);
                restored = 1;
                break;
#ifdef UNUSED
            case _compress_lzma:
                if (reference == reference_current) {
                    dptr = mptr;
                } else {
                    dptr = (u_int8_t *)ar9300_eeprom_struct_default_find_by_id(
                        reference);
                    if (dptr == 0) {
                        HALDEBUG(ah, HAL_DEBUG_EEPROM,
                            "%s: Can't find reference eeprom struct %d\n",
                            __func__, reference);
                        goto done;
                    }
                }
                usize = -1;
                if (usize != mdata_size) {
                    HALDEBUG(ah, HAL_DEBUG_EEPROM,
                        "%s: uncompressed data is wrong size %d %d\n",
                        __func__, usize, mdata_size);
                    goto done;
                }

                for (ib = 0; ib < mdata_size; ib++) {
                    mptr[ib] = dptr[ib] ^ word[ib + overhead];
                }
                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "%s: restored eeprom %d: compressed, "
                    "reference %d, length %d\n",
                    __func__, it, reference, length);
                break;
            case _compress_pairs:
                if (reference == reference_current) {
                    dptr = mptr;
                } else {
                    dptr = (u_int8_t *)ar9300_eeprom_struct_default_find_by_id(
                        reference);
                    if (dptr == 0) {
                        HALDEBUG(ah, HAL_DEBUG_EEPROM,
                            "%s: Can't find the reference "
                            "eeprom structure %d\n",
                            __func__, reference);
                        goto done;
                    }
                }
                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "%s: restored eeprom %d: "
                    "pairs, reference %d, length %d,\n",
                    __func__, it, reference, length);
                break;
#endif
            case _compress_block:
                if (reference == reference_current) {
                    dptr = mptr;
                } else {
                    dptr = ar9300_eeprom_struct_default_find_by_id(reference);
                    if (dptr == 0) {
                        HALDEBUG(ah, HAL_DEBUG_EEPROM,
                            "%s: cant find reference eeprom struct %d\n",
                            __func__, reference);
                        break;
                    }
                    OS_MEMCPY(mptr, dptr, mdata_size);
                }

                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "%s: restore eeprom %d: block, reference %d, length %d\n",
                    __func__, it, reference, length);
                (void) ar9300_uncompress_block(ah,
                    (u_int8_t *) mptr, mdata_size,
                    (u_int8_t *) (word + compression_header_length), length);
                restored = 1;
                break;
            default:
                HALDEBUG(ah, HAL_DEBUG_EEPROM,
                    "%s: unknown compression code %d\n", __func__, code);
                break;
            }
        } else {
            HALDEBUG(ah, HAL_DEBUG_EEPROM,
                "%s: skipping block with bad checksum\n", __func__);
        }
        cptr -= compression_header_length + osize + compression_checksum_length;
    }

    if (!restored) {
        cptr = -1;
    }
    return cptr;
}

static int
ar9300_eeprom_restore_from_dram(struct ath_hal *ah, ar9300_eeprom_t *mptr,
    int mdata_size)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
#if !defined(USE_PLATFORM_FRAMEWORK)
    char *cal_ptr;
#endif

    HALASSERT(mdata_size > 0);

    /* if cal_in_flash is AH_TRUE, the address sent by LMAC to HAL
       (i.e. ah->ah_st) is corresponding to Flash. so return from 
       here if ar9300_eep_data_in_flash(ah) returns AH_TRUE */
    if(ar9300_eep_data_in_flash(ah))
        return -1;

#if 0
    /* check if LMAC sent DRAM address is valid */
    if (!(uintptr_t)(AH_PRIVATE(ah)->ah_st)) {
        return -1;
    }
#endif

    /* When calibration data is from host, Host will copy the 
       compressed data to the predefined DRAM location saved at ah->ah_st */
#if 0
    ath_hal_printf(ah, "Restoring Cal data from DRAM\n");
    ahp->ah_cal_mem = OS_REMAP((uintptr_t)(AH_PRIVATE(ah)->ah_st), 
							HOST_CALDATA_SIZE);
#endif
    if (!ahp->ah_cal_mem)
    {
       HALDEBUG(ah, HAL_DEBUG_EEPROM,"%s: can't remap dram region\n", __func__);
       return -1;
    }
#if !defined(USE_PLATFORM_FRAMEWORK)
    cal_ptr = &((char *)(ahp->ah_cal_mem))[AR9300_FLASH_CAL_START_OFFSET];
    OS_MEMCPY(mptr, cal_ptr, mdata_size);
#else
    OS_MEMCPY(mptr, ahp->ah_cal_mem, mdata_size);
#endif

    if (mptr->eeprom_version   == 0xff ||
        mptr->template_version == 0xff ||
        mptr->eeprom_version   == 0    ||
        mptr->template_version == 0)
    {
        /* The board is uncalibrated */
        return -1;
    }
    if (mptr->eeprom_version != 0x2)
    {
        return -1;
    }

    return mdata_size;

}

static int
ar9300_eeprom_restore_from_flash(struct ath_hal *ah, ar9300_eeprom_t *mptr,
    int mdata_size)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    char *cal_ptr;

    HALASSERT(mdata_size > 0);

    if (!ahp->ah_cal_mem) {
        return -1;
    }

    ath_hal_printf(ah, "Restoring Cal data from Flash\n");
    /*
     * When calibration data is saved in flash, read
     * uncompressed eeprom structure from flash and return
     */
    cal_ptr = &((char *)(ahp->ah_cal_mem))[AR9300_FLASH_CAL_START_OFFSET];
    OS_MEMCPY(mptr, cal_ptr, mdata_size);
#if 0
    ar9300_swap_eeprom((ar9300_eeprom_t *)mptr); DONE IN ar9300_restore()
#endif
    if (mptr->eeprom_version   == 0xff ||
        mptr->template_version == 0xff ||
        mptr->eeprom_version   == 0    ||
        mptr->template_version == 0)
    {   
        /* The board is uncalibrated */
        return -1;
    } 
    if (mptr->eeprom_version != 0x2)
    {
        return -1;
    }
    return mdata_size;
}

/*
 * Read the configuration data from the storage. We try the order with:
 * EEPROM, Flash, OTP. If all of above failed, use the default template.
 * The data can be put in any specified memory buffer.
 *
 * Returns -1 on error. 
 * Returns address of next memory location on success.
 */
int
ar9300_eeprom_restore_internal(struct ath_hal *ah, ar9300_eeprom_t *mptr,
    int mdata_size)
{
    int nptr;

    nptr = -1;    

    if ((AH9300(ah)->calibration_data_try == calibration_data_none ||
         AH9300(ah)->calibration_data_try == calibration_data_dram) &&
         AH9300(ah)->try_dram && nptr < 0)
    {   
        ath_hal_printf(ah, "Restoring Cal data from DRAM\n");
        AH9300(ah)->calibration_data_source = calibration_data_dram;
        AH9300(ah)->calibration_data_source_address = 0;
        nptr = ar9300_eeprom_restore_from_dram(ah, mptr, mdata_size);
        if (nptr < 0) {
            AH9300(ah)->calibration_data_source = calibration_data_none;
            AH9300(ah)->calibration_data_source_address = 0;
        }
    }
    
    if ((AH9300(ah)->calibration_data_try == calibration_data_none ||
         AH9300(ah)->calibration_data_try == calibration_data_eeprom) &&
        AH9300(ah)->try_eeprom && nptr < 0)
    {
        /*
         * need to look at highest eeprom address as well as at
         * base_address=0x3ff where we used to write the data
         */
        ath_hal_printf(ah, "Restoring Cal data from EEPROM\n");
        AH9300(ah)->calibration_data_source = calibration_data_eeprom;
        if (AH9300(ah)->calibration_data_try_address != 0) {
            AH9300(ah)->calibration_data_source_address =
                AH9300(ah)->calibration_data_try_address;
            nptr = ar9300_eeprom_restore_internal_address(
                ah, mptr, mdata_size,
                AH9300(ah)->calibration_data_source_address, 0xff);
        } else {
            AH9300(ah)->calibration_data_source_address =
                ar9300_eeprom_base_address(ah);
            nptr = ar9300_eeprom_restore_internal_address(
                ah, mptr, mdata_size,
                AH9300(ah)->calibration_data_source_address, 0xff);
            if (nptr < 0 &&
                AH9300(ah)->calibration_data_source_address != base_address)
            {
                AH9300(ah)->calibration_data_source_address = base_address;
                nptr = ar9300_eeprom_restore_internal_address(
                    ah, mptr, mdata_size,
                    AH9300(ah)->calibration_data_source_address, 0xff);
            }
        }
        if (nptr < 0) {
            AH9300(ah)->calibration_data_source = calibration_data_none;
            AH9300(ah)->calibration_data_source_address = 0;
        }
    }

    /*
     * ##### should be an ifdef test for any AP usage,
     * either in driver or in nart
     */
    if ((AH9300(ah)->calibration_data_try == calibration_data_none ||
         AH9300(ah)->calibration_data_try == calibration_data_flash) &&
        AH9300(ah)->try_flash && nptr < 0)
    {
        ath_hal_printf(ah, "Restoring Cal data from Flash\n");
        AH9300(ah)->calibration_data_source = calibration_data_flash;
        /* how are we supposed to set this for flash? */
        AH9300(ah)->calibration_data_source_address = 0;
        nptr = ar9300_eeprom_restore_from_flash(ah, mptr, mdata_size);
        if (nptr < 0) {
            AH9300(ah)->calibration_data_source = calibration_data_none;
            AH9300(ah)->calibration_data_source_address = 0;
        }
    }

    if ((AH9300(ah)->calibration_data_try == calibration_data_none ||
         AH9300(ah)->calibration_data_try == calibration_data_otp) &&
        AH9300(ah)->try_otp && nptr < 0)
    {
        ath_hal_printf(ah, "Restoring Cal data from OTP\n");
        AH9300(ah)->calibration_data_source = calibration_data_otp;
        if (AH9300(ah)->calibration_data_try_address != 0) {
            AH9300(ah)->calibration_data_source_address =
                AH9300(ah)->calibration_data_try_address;
		} else {
            AH9300(ah)->calibration_data_source_address =
                ar9300_eeprom_base_address(ah);
		}
        nptr = ar9300_eeprom_restore_internal_address(
            ah, mptr, mdata_size, AH9300(ah)->calibration_data_source_address, 0);
        if (nptr < 0) {
            AH9300(ah)->calibration_data_source = calibration_data_none;
            AH9300(ah)->calibration_data_source_address = 0;
        }
    }

#ifdef ATH_CAL_NAND_FLASH
    if ((AH9300(ah)->calibration_data_try == calibration_data_none ||
         AH9300(ah)->calibration_data_try == calibration_data_nand) &&
        AH9300(ah)->try_nand && nptr < 0)
    {
        AH9300(ah)->calibration_data_source = calibration_data_nand;
        AH9300(ah)->calibration_data_source_address = ((unsigned int)(AH_PRIVATE(ah)->ah_st)) + base_address_nand;
        if(ar9300_calibration_data_read(
            ah, AH9300(ah)->calibration_data_source_address, 
            (u_int8_t *)mptr, mdata_size) == AH_TRUE)
        {
            nptr = mdata_size;
        }
        /*nptr=ar9300EepromRestoreInternalAddress(ah, mptr, mdataSize, CalibrationDataSourceAddress);*/
        if(nptr < 0)
        {
            AH9300(ah)->calibration_data_source = calibration_data_none;
            AH9300(ah)->calibration_data_source_address = 0;
        }
    }
#endif
    if (nptr < 0) {
        ath_hal_printf(ah, "%s[%d] No vaid CAL, calling default template\n",
            __func__, __LINE__);
        nptr = ar9300_eeprom_restore_something(ah, mptr, mdata_size);
    }

    return nptr;
}

/******************************************************************************/
/*!
**  \brief Eeprom Swapping Function
**
**  This function will swap the contents of the "longer" EEPROM data items
**  to ensure they are consistent with the endian requirements for the platform
**  they are being compiled for
**
**  \param eh    Pointer to the EEPROM data structure
**  \return N/A
*/
#if AH_BYTE_ORDER == AH_BIG_ENDIAN
void
ar9300_swap_eeprom(ar9300_eeprom_t *eep)
{
    u_int32_t dword;
    u_int16_t word;
    int          i;

    word = __bswap16(eep->base_eep_header.reg_dmn[0]);
    eep->base_eep_header.reg_dmn[0] = word;

    word = __bswap16(eep->base_eep_header.reg_dmn[1]);
    eep->base_eep_header.reg_dmn[1] = word;

    dword = __bswap32(eep->base_eep_header.swreg);
    eep->base_eep_header.swreg = dword;

    dword = __bswap32(eep->modal_header_2g.ant_ctrl_common);
    eep->modal_header_2g.ant_ctrl_common = dword;

    dword = __bswap32(eep->modal_header_2g.ant_ctrl_common2);
    eep->modal_header_2g.ant_ctrl_common2 = dword;

    dword = __bswap32(eep->modal_header_2g.paprd_rate_mask_ht20);
    eep->modal_header_2g.paprd_rate_mask_ht20 = dword;

    dword = __bswap32(eep->modal_header_2g.paprd_rate_mask_ht40);
    eep->modal_header_2g.paprd_rate_mask_ht40 = dword;

    dword = __bswap32(eep->modal_header_5g.ant_ctrl_common);
    eep->modal_header_5g.ant_ctrl_common = dword;

    dword = __bswap32(eep->modal_header_5g.ant_ctrl_common2);
    eep->modal_header_5g.ant_ctrl_common2 = dword;

    dword = __bswap32(eep->modal_header_5g.paprd_rate_mask_ht20);
    eep->modal_header_5g.paprd_rate_mask_ht20 = dword;

    dword = __bswap32(eep->modal_header_5g.paprd_rate_mask_ht40);
    eep->modal_header_5g.paprd_rate_mask_ht40 = dword;

    for (i = 0; i < OSPREY_MAX_CHAINS; i++) {
        word = __bswap16(eep->modal_header_2g.ant_ctrl_chain[i]);
        eep->modal_header_2g.ant_ctrl_chain[i] = word;

        word = __bswap16(eep->modal_header_5g.ant_ctrl_chain[i]);
        eep->modal_header_5g.ant_ctrl_chain[i] = word;
    }
}

void ar9300_eeprom_template_swap(void)
{
    int it;
    ar9300_eeprom_t *dptr;

    for (it = 0; it < ARRAY_LENGTH(default9300); it++) {
        dptr = ar9300_eeprom_struct_default(it);
        if (dptr != 0) {
            ar9300_swap_eeprom(dptr);
        }
    }
}
#endif


/*
 * Restore the configuration structure by reading the eeprom.
 * This function destroys any existing in-memory structure content.
 */
HAL_BOOL
ar9300_eeprom_restore(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    ar9300_eeprom_t *mptr;
    int mdata_size;
    HAL_BOOL status = AH_FALSE;

    mptr = &ahp->ah_eeprom;
    mdata_size = ar9300_eeprom_struct_size();

    if (mptr != 0 && mdata_size > 0) {
#if AH_BYTE_ORDER == AH_BIG_ENDIAN
        ar9300_eeprom_template_swap();
        ar9300_swap_eeprom(mptr);
#endif
        /*
         * At this point, mptr points to the eeprom data structure
         * in its "default" state.  If this is big endian, swap the
         * data structures back to "little endian" form.
         */
        if (ar9300_eeprom_restore_internal(ah, mptr, mdata_size) >= 0) {
            status = AH_TRUE;
        }

#if AH_BYTE_ORDER == AH_BIG_ENDIAN
        /* Second Swap, back to Big Endian */
        ar9300_eeprom_template_swap();
        ar9300_swap_eeprom(mptr);
#endif

    }
    ahp->ah_2g_paprd_rate_mask_ht40 =
        mptr->modal_header_2g.paprd_rate_mask_ht40;
    ahp->ah_2g_paprd_rate_mask_ht20 =
        mptr->modal_header_2g.paprd_rate_mask_ht20;
    ahp->ah_5g_paprd_rate_mask_ht40 =
        mptr->modal_header_5g.paprd_rate_mask_ht40;
    ahp->ah_5g_paprd_rate_mask_ht20 =
        mptr->modal_header_5g.paprd_rate_mask_ht20;
    return status;
}

int32_t ar9300_thermometer_get(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int thermometer;
    thermometer =
        (ahp->ah_eeprom.base_eep_header.misc_configuration >> 1) & 0x3;
    thermometer--;
    return thermometer;
}

HAL_BOOL ar9300_thermometer_apply(struct ath_hal *ah)
{
    int thermometer = ar9300_thermometer_get(ah);

/* ch0_RXTX4 */
/*#define AR_PHY_65NM_CH0_RXTX4       AR_PHY_65NM(ch0_RXTX4)*/
#define AR_PHY_65NM_CH1_RXTX4       AR_PHY_65NM(ch1_RXTX4)
#define AR_PHY_65NM_CH2_RXTX4       AR_PHY_65NM(ch2_RXTX4)
/*#define AR_PHY_65NM_CH0_RXTX4_THERM_ON          0x10000000*/
/*#define AR_PHY_65NM_CH0_RXTX4_THERM_ON_S        28*/
#define AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR_S      29
#define AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR        \
    (0x1<<AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR_S)

    if (thermometer < 0) {
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR, 0);
        if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR, 0);
            if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah)  ) {
                OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX4,
                    AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR, 0);
            }
        }
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
        if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
            if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah) ) {
                OS_REG_RMW_FIELD(ah,
                    AR_PHY_65NM_CH2_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
            }
        }
    } else {
        OS_REG_RMW_FIELD(ah,
            AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR, 1);
        if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR, 1);
            if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah)  ) {
                OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX4,
                    AR_PHY_65NM_CH0_RXTX4_THERM_ON_OVR, 1);
            }
        }
        if (thermometer == 0) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 1);
            if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
                OS_REG_RMW_FIELD(ah,
                    AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
                if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah) ) {
                    OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX4,
                        AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
                }
            }
        } else if (thermometer == 1) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
            if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
                OS_REG_RMW_FIELD(ah,
                    AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 1);
                if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah) ) {
                    OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX4,
                        AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
                }
            }
        } else if (thermometer == 2) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
            if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah)) {
                OS_REG_RMW_FIELD(ah,
                    AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_CH0_RXTX4_THERM_ON, 0);
                if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah) ) {
                    OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX4,
                        AR_PHY_65NM_CH0_RXTX4_THERM_ON, 1);
                }
            }
        }
    }
    return AH_TRUE;
}

static int32_t ar9300_tuning_caps_params_get(struct ath_hal *ah)
{
    int tuning_caps_params;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    tuning_caps_params = eep->base_eep_header.params_for_tuning_caps[0];
    return tuning_caps_params;
}

/*
 * Read the tuning caps params from eeprom and set to correct register.
 * To regulation the frequency accuracy.
 */
HAL_BOOL ar9300_tuning_caps_apply(struct ath_hal *ah)
{
    int tuning_caps_params;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    tuning_caps_params = ar9300_tuning_caps_params_get(ah);
    if ((eep->base_eep_header.feature_enable & 0x40) >> 6) {
        tuning_caps_params &= 0x7f;

        if (AR_SREV_POSEIDON(ah) || AR_SREV_WASP(ah) || AR_SREV_HONEYBEE(ah)) {
            return true;
        } else if (AR_SREV_HORNET(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_HORNET_CH0_XTAL, AR_OSPREY_CHO_XTAL_CAPINDAC,
                tuning_caps_params);
            OS_REG_RMW_FIELD(ah,
                AR_HORNET_CH0_XTAL, AR_OSPREY_CHO_XTAL_CAPOUTDAC,
                tuning_caps_params);
        } else if (AR_SREV_SCORPION(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_SCORPION_CH0_XTAL, AR_OSPREY_CHO_XTAL_CAPINDAC,
                tuning_caps_params);
            OS_REG_RMW_FIELD(ah,
                AR_SCORPION_CH0_XTAL, AR_OSPREY_CHO_XTAL_CAPOUTDAC,
                tuning_caps_params);
        } else {
            OS_REG_RMW_FIELD(ah,
                AR_OSPREY_CH0_XTAL, AR_OSPREY_CHO_XTAL_CAPINDAC,
                tuning_caps_params);
            OS_REG_RMW_FIELD(ah,
                AR_OSPREY_CH0_XTAL, AR_OSPREY_CHO_XTAL_CAPOUTDAC,
                tuning_caps_params);
        }

    }
    return AH_TRUE;
}

/*
 * Read the tx_frame_to_xpa_on param from eeprom and apply the value to 
 * correct register.
 */
HAL_BOOL ar9300_xpa_timing_control_apply(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    u_int8_t xpa_timing_control;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    if ((eep->base_eep_header.feature_enable & 0x80) >> 7) {
		if (AR_SREV_OSPREY(ah) || AR_SREV_AR9580(ah) || AR_SREV_WASP(ah) || AR_SREV_HONEYBEE(ah)) {
			if (is_2ghz) {
                xpa_timing_control = eep->modal_header_2g.tx_frame_to_xpa_on;
                OS_REG_RMW_FIELD(ah,
						AR_PHY_XPA_TIMING_CTL, AR_PHY_XPA_TIMING_CTL_FRAME_XPAB_ON,
						xpa_timing_control);
			} else {
                xpa_timing_control = eep->modal_header_5g.tx_frame_to_xpa_on;
                OS_REG_RMW_FIELD(ah,
						AR_PHY_XPA_TIMING_CTL, AR_PHY_XPA_TIMING_CTL_FRAME_XPAA_ON,
						xpa_timing_control);
			}
		}
	}
    return AH_TRUE;
}


/*
 * Read the xLNA_bias_strength param from eeprom and apply the value to 
 * correct register.
 */ 
HAL_BOOL ar9300_x_lNA_bias_strength_apply(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    u_int8_t x_lNABias;
    u_int32_t value = 0;
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    if ((eep->base_eep_header.misc_configuration & 0x40) >> 6) {
        if (AR_SREV_OSPREY(ah)) {
            if (is_2ghz) {
                x_lNABias = eep->modal_header_2g.xLNA_bias_strength;
            } else {
                x_lNABias = eep->modal_header_5g.xLNA_bias_strength;
            }
            value = x_lNABias & ( 0x03 );	// bit0,1 for chain0
            OS_REG_RMW_FIELD(ah,
					AR_PHY_65NM_CH0_RXTX4, AR_PHY_65NM_RXTX4_XLNA_BIAS, value);
            value = (x_lNABias >> 2) & ( 0x03 );	// bit2,3 for chain1
            OS_REG_RMW_FIELD(ah,
					AR_PHY_65NM_CH1_RXTX4, AR_PHY_65NM_RXTX4_XLNA_BIAS, value);
            value = (x_lNABias >> 4) & ( 0x03 );	// bit4,5 for chain2
            OS_REG_RMW_FIELD(ah,
					AR_PHY_65NM_CH2_RXTX4, AR_PHY_65NM_RXTX4_XLNA_BIAS, value);
        }
    }
    return AH_TRUE;
}


/*
 * Read EEPROM header info and program the device for correct operation
 * given the channel value.
 */
HAL_BOOL
ar9300_eeprom_set_board_values(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    ar9300_xpa_bias_level_apply(ah, IEEE80211_IS_CHAN_2GHZ(chan));

    ar9300_xpa_timing_control_apply(ah, IEEE80211_IS_CHAN_2GHZ(chan));

    ar9300_ant_ctrl_apply(ah, IEEE80211_IS_CHAN_2GHZ(chan));
    ar9300_drive_strength_apply(ah);

    ar9300_x_lNA_bias_strength_apply(ah, IEEE80211_IS_CHAN_2GHZ(chan));

	/* wait for Poseidon internal regular turnning */
    /* for Hornet we move it before initPLL to avoid an access issue */
    /* Function not used when EMULATION. */
    if (!AR_SREV_HORNET(ah) && !AR_SREV_WASP(ah) && !AR_SREV_HONEYBEE(ah)) {
        ar9300_internal_regulator_apply(ah);
    }

    ar9300_attenuation_apply(ah, ichan->channel);
    ar9300_quick_drop_apply(ah, ichan->channel);
    ar9300_thermometer_apply(ah);
    if(!AR_SREV_WASP(ah))
    {
        ar9300_tuning_caps_apply(ah);
    }

    ar9300_tx_end_to_xpab_off_apply(ah, ichan->channel);

    return AH_TRUE;
}

u_int8_t *
ar9300_eeprom_get_spur_chans_ptr(struct ath_hal *ah, HAL_BOOL is_2ghz)
{
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

    if (is_2ghz) {
        return &(eep->modal_header_2g.spur_chans[0]);
    } else {
        return &(eep->modal_header_5g.spur_chans[0]);
    }
}

static u_int8_t ar9300_eeprom_get_tx_gain_table_number_max(struct ath_hal *ah)
{
    unsigned long tx_gain_table_max;
    tx_gain_table_max = OS_REG_READ_FIELD(ah,
        AR_PHY_TPC_7, AR_PHY_TPC_7_TX_GAIN_TABLE_MAX);
    return tx_gain_table_max;
}

u_int8_t ar9300_eeprom_tx_gain_table_index_max_apply(struct ath_hal *ah, u_int16_t channel)
{
    unsigned int index;
    ar9300_eeprom_t *ahp_Eeprom;
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp_Eeprom = &ahp->ah_eeprom;

    if (ahp_Eeprom->base_ext1.misc_enable == 0)
        return AH_FALSE;

    if (channel < 4000) 
    {
        index = ahp_Eeprom->modal_header_2g.tx_gain_cap;
    }
    else
    {
        index = ahp_Eeprom->modal_header_5g.tx_gain_cap;
    }

    OS_REG_RMW_FIELD(ah,
        AR_PHY_TPC_7, AR_PHY_TPC_7_TX_GAIN_TABLE_MAX, index);
    return AH_TRUE;
}

static u_int8_t ar9300_eeprom_get_pcdac_tx_gain_table_i(struct ath_hal *ah, 
                                               int i, u_int8_t *pcdac)
{
    unsigned long tx_gain;
    u_int8_t tx_gain_table_max;
    tx_gain_table_max = ar9300_eeprom_get_tx_gain_table_number_max(ah);
    if (i <= 0 || i > tx_gain_table_max) {
        *pcdac = 0;
        return AH_FALSE;
    }

    tx_gain = OS_REG_READ(ah, AR_PHY_TXGAIN_TAB(1) + i * 4);
    *pcdac = ((tx_gain >> 24) & 0xff);
    return AH_TRUE;
}

u_int8_t ar9300_eeprom_set_tx_gain_cap(struct ath_hal *ah, 
                                               int *tx_gain_max)
// pcdac read back from reg, read back value depends on reset 2GHz/5GHz ini 
// tx_gain_table, this function will be called twice after each 
// band's calibration.
// after 2GHz cal, tx_gain_max[0] has 2GHz, calibration max txgain, 
// tx_gain_max[1]=-100
// after 5GHz cal, tx_gain_max[0],tx_gain_max[1] have calibration 
// value for both band
// reset is on 5GHz, reg reading from tx_gain_table is for 5GHz,
// so program can't recalculate 2g.tx_gain_cap at this point.
{
    int i = 0, ig, im = 0;
    u_int8_t pcdac = 0;
    u_int8_t tx_gain_table_max;
    ar9300_eeprom_t *ahp_Eeprom;
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp_Eeprom = &ahp->ah_eeprom;

    if (ahp_Eeprom->base_ext1.misc_enable == 0)
        return AH_FALSE;

    tx_gain_table_max = ar9300_eeprom_get_tx_gain_table_number_max(ah);

    for (i = 0; i < 2; i++) {
        if (tx_gain_max[i]>-100) {	// -100 didn't cal that band.
            if ( i== 0) {
                if (tx_gain_max[1]>-100) {
                    continue;
                    // both band are calibrated, skip 2GHz 2g.tx_gain_cap reset
                }
            }
            for (ig = 1; ig <= tx_gain_table_max; ig++) {
                if (ah != 0 && ah->ah_reset != 0)
                {
                    ar9300_eeprom_get_pcdac_tx_gain_table_i(ah, ig, &pcdac);
                    if (pcdac >= tx_gain_max[i])
                        break;
                }
            }
            if (ig+1 <= tx_gain_table_max) {
                if (pcdac == tx_gain_max[i])
                    im = ig;
                else
                    im = ig + 1;
                if (i == 0) {
                    ahp_Eeprom->modal_header_2g.tx_gain_cap = im;
                } else {
                    ahp_Eeprom->modal_header_5g.tx_gain_cap = im;
                }
            } else {
                if (i == 0) {
                    ahp_Eeprom->modal_header_2g.tx_gain_cap = ig;
                } else {
                    ahp_Eeprom->modal_header_5g.tx_gain_cap = ig;
                }
            }
        }
    }
    return AH_TRUE;
}
