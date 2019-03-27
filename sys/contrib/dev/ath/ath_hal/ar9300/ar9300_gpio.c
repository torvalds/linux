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
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"

#define AR_GPIO_BIT(_gpio)                      (1 << (_gpio))

/*
 * Configure GPIO Output Mux control
 */
#if UMAC_SUPPORT_SMARTANTENNA
static void  ar9340_soc_gpio_cfg_output_mux(
    struct ath_hal *ah, 
    u_int32_t gpio, 
    u_int32_t ah_signal_type)
{
#define ADDR_READ(addr)      (*((volatile u_int32_t *)(addr)))
#define ADDR_WRITE(addr, b)   (void)((*(volatile u_int32_t *) (addr)) = (b))
#define AR9340_SOC_GPIO_FUN0    0xB804002c
#define AR9340_SOC_GPIO_OE      0xB8040000
#if ATH_SMARTANTENNA_DISABLE_JTAG
#define AR9340_SOC_GPIO_FUNCTION   (volatile u_int32_t*) 0xB804006c
#define WASP_DISABLE_JTAG  0x2
#define MAX_JTAG_GPIO_PIN 1
#endif
    u_int8_t out_func, shift;
    u_int32_t  flags;
    volatile u_int32_t* address;

    if (!ah_signal_type){
        return;
    }
#if ATH_SMARTANTENNA_DISABLE_JTAG
/* 
 * To use GPIO pins 0 and 1 for controling antennas, JTAG needs to disabled.
 */
    if (gpio <= MAX_JTAG_GPIO_PIN) {
        flags = ADDR_READ(AR9340_SOC_GPIO_FUNCTION);
        flags |= WASP_DISABLE_JTAG; 
        ADDR_WRITE(AR9340_SOC_GPIO_FUNCTION, flags);
    }
#endif
    out_func = gpio / 4;
    shift = (gpio % 4);
    address = (volatile u_int32_t *)(AR9340_SOC_GPIO_FUN0 + (out_func*4));

    flags = ADDR_READ(address);
    flags |= ah_signal_type << (8*shift); 
    ADDR_WRITE(address, flags);
    flags = ADDR_READ(AR9340_SOC_GPIO_OE);
    flags &= ~(1 << gpio);
    ADDR_WRITE(AR9340_SOC_GPIO_OE, flags);

}
#endif

static void
ar9300_gpio_cfg_output_mux(struct ath_hal *ah, u_int32_t gpio, u_int32_t type)
{
    int          addr;
    u_int32_t    gpio_shift;

    /* each MUX controls 6 GPIO pins */
    if (gpio > 11) {
        addr = AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX3);
    } else if (gpio > 5) {
        addr = AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX2);
    } else {
        addr = AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX1);
    }

    /*
     * 5 bits per GPIO pin.
     * Bits 0..4 for 1st pin in that mux,
     * bits 5..9 for 2nd pin, etc.
     */
    gpio_shift = (gpio % 6) * 5;

    OS_REG_RMW(ah, addr, (type << gpio_shift), (0x1f << gpio_shift));
}

/*
 * Configure GPIO Output lines
 */
HAL_BOOL
ar9300_gpio_cfg_output(
    struct ath_hal *ah,
    u_int32_t gpio,
    HAL_GPIO_MUX_TYPE hal_signal_type)
{
    u_int32_t    ah_signal_type;
    u_int32_t    gpio_shift;
    u_int8_t    smart_ant = 0;
    static const u_int32_t    mux_signal_conversion_table[] = {
                    /* HAL_GPIO_OUTPUT_MUX_AS_OUTPUT             */
        AR_GPIO_OUTPUT_MUX_AS_OUTPUT,
                    /* HAL_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED */
        AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED,
                    /* HAL_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED     */
        AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED,
                    /* HAL_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED    */
        AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED,
                    /* HAL_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED      */
        AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED,
                    /* HAL_GPIO_OUTPUT_MUX_AS_WLAN_ACTIVE        */
        AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL,
                    /* HAL_GPIO_OUTPUT_MUX_AS_TX_FRAME           */
        AR_GPIO_OUTPUT_MUX_AS_TX_FRAME,
                    /* HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA      */
        AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA,
                    /* HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK       */
        AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK,
                    /* HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA        */
        AR_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA,
                    /* HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK         */
        AR_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK,
	            /* HAL_GPIO_OUTPUT_MUX_AS_WL_IN_TX           */
        AR_GPIO_OUTPUT_MUX_AS_WL_IN_TX,
                    /* HAL_GPIO_OUTPUT_MUX_AS_WL_IN_RX           */
        AR_GPIO_OUTPUT_MUX_AS_WL_IN_RX,
                    /* HAL_GPIO_OUTPUT_MUX_AS_BT_IN_TX           */
        AR_GPIO_OUTPUT_MUX_AS_BT_IN_TX,
                    /* HAL_GPIO_OUTPUT_MUX_AS_BT_IN_RX           */
        AR_GPIO_OUTPUT_MUX_AS_BT_IN_RX,
                    /* HAL_GPIO_OUTPUT_MUX_AS_RUCKUS_STROBE      */
        AR_GPIO_OUTPUT_MUX_AS_RUCKUS_STROBE,
                    /* HAL_GPIO_OUTPUT_MUX_AS_RUCKUS_DATA        */
        AR_GPIO_OUTPUT_MUX_AS_RUCKUS_DATA,
                    /* HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0     */
        AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0,
                    /* HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1     */
        AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1,
                    /* HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2     */
        AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2,
                    /* HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_SWCOM3    */
        AR_GPIO_OUTPUT_MUX_AS_SWCOM3,
    };

    HALASSERT(gpio < AH_PRIVATE(ah)->ah_caps.halNumGpioPins);
    if ((gpio == AR9382_GPIO_PIN_8_RESERVED)  ||
        (gpio == AR9382_GPIO_9_INPUT_ONLY))
    {
        return AH_FALSE;
    }

    /* Convert HAL signal type definitions to hardware-specific values. */
    if ((int) hal_signal_type < ARRAY_LENGTH(mux_signal_conversion_table))
    {
        ah_signal_type = mux_signal_conversion_table[hal_signal_type];
    } else {
        return AH_FALSE;
    }

    if (gpio <= AR9382_MAX_JTAG_GPIO_PIN_NUM) {
        OS_REG_SET_BIT(ah,
            AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL), AR_GPIO_JTAG_DISABLE);
    }

#if UMAC_SUPPORT_SMARTANTENNA
    /* Get the pin and func values for smart antenna */
    switch (ah_signal_type)
    {
        case AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0:
            gpio = ATH_GPIOPIN_ANTCHAIN0;
            ah_signal_type = ATH_GPIOFUNC_ANTCHAIN0;
            smart_ant = 1;
            break; 
        case AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1:
            gpio = ATH_GPIOPIN_ANTCHAIN1;
            ah_signal_type = ATH_GPIOFUNC_ANTCHAIN1;
            smart_ant = 1;
            break;
        case AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2:    
            gpio = ATH_GPIOPIN_ANTCHAIN2;
            ah_signal_type = ATH_GPIOFUNC_ANTCHAIN2;
            smart_ant = 1;
            break;
#if ATH_SMARTANTENNA_ROUTE_SWCOM_TO_GPIO
        case AR_GPIO_OUTPUT_MUX_AS_SWCOM3:
            gpio = ATH_GPIOPIN_ROUTE_SWCOM3;
            ah_signal_type = ATH_GPIOFUNC_ROUTE_SWCOM3;
            smart_ant = 1;
            break;
#endif
        default:
            break;
    }
#endif

    if (smart_ant && (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)))
    {
#if UMAC_SUPPORT_SMARTANTENNA
        ar9340_soc_gpio_cfg_output_mux(ah, gpio, ah_signal_type);
#endif
        return AH_TRUE;
    } else
    {
        /* Configure the MUX */
        ar9300_gpio_cfg_output_mux(ah, gpio, ah_signal_type);
    }

    /* 2 bits per output mode */
    gpio_shift = 2 * gpio;

    OS_REG_RMW(ah,
               AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT),
               (AR_GPIO_OE_OUT_DRV_ALL << gpio_shift),
               (AR_GPIO_OE_OUT_DRV << gpio_shift));
    return AH_TRUE;
}

/*
 * Configure GPIO Output lines -LED off
 */
HAL_BOOL
ar9300_gpio_cfg_output_led_off(
    struct ath_hal *ah,
    u_int32_t gpio,
    HAL_GPIO_MUX_TYPE halSignalType)
{
#define N(a)    (sizeof(a) / sizeof(a[0]))
    u_int32_t    ah_signal_type;
    u_int32_t    gpio_shift;
    u_int8_t    smart_ant = 0;

    static const u_int32_t    mux_signal_conversion_table[] = {
        /* HAL_GPIO_OUTPUT_MUX_AS_OUTPUT             */
        AR_GPIO_OUTPUT_MUX_AS_OUTPUT,
        /* HAL_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED */
        AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED,
        /* HAL_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED     */
        AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED,
        /* HAL_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED    */
        AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED,
        /* HAL_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED      */
        AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED,
        /* HAL_GPIO_OUTPUT_MUX_AS_WLAN_ACTIVE        */
        AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL,
        /* HAL_GPIO_OUTPUT_MUX_AS_TX_FRAME           */
        AR_GPIO_OUTPUT_MUX_AS_TX_FRAME,
        /* HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA      */
        AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA,
        /* HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK       */
        AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK,
        /* HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA        */
        AR_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA,
        /* HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK         */
        AR_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK,
        /* HAL_GPIO_OUTPUT_MUX_AS_WL_IN_TX           */
        AR_GPIO_OUTPUT_MUX_AS_WL_IN_TX,
        /* HAL_GPIO_OUTPUT_MUX_AS_WL_IN_RX           */
        AR_GPIO_OUTPUT_MUX_AS_WL_IN_RX,
        /* HAL_GPIO_OUTPUT_MUX_AS_BT_IN_TX           */
        AR_GPIO_OUTPUT_MUX_AS_BT_IN_TX,
        /* HAL_GPIO_OUTPUT_MUX_AS_BT_IN_RX           */
        AR_GPIO_OUTPUT_MUX_AS_BT_IN_RX,
        AR_GPIO_OUTPUT_MUX_AS_RUCKUS_STROBE,
        AR_GPIO_OUTPUT_MUX_AS_RUCKUS_DATA,
        AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0,
        AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1,
        AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2
    };
    HALASSERT(gpio < AH_PRIVATE(ah)->ah_caps.hal_num_gpio_pins);

    /* Convert HAL signal type definitions to hardware-specific values. */
    if ((int) halSignalType < ARRAY_LENGTH(mux_signal_conversion_table))
    {
        ah_signal_type = mux_signal_conversion_table[halSignalType];
    } else {
        return AH_FALSE;
    }
#if UMAC_SUPPORT_SMARTANTENNA
    /* Get the pin and func values for smart antenna */
    switch (halSignalType)
    {
        case AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0:
            gpio = ATH_GPIOPIN_ANTCHAIN0;
            ah_signal_type = ATH_GPIOFUNC_ANTCHAIN0;
            smart_ant = 1;
            break; 
        case AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1:
            gpio = ATH_GPIOPIN_ANTCHAIN1;
            ah_signal_type = ATH_GPIOFUNC_ANTCHAIN1;
            smart_ant = 1;
            break;
        case AR_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2:    
            gpio = ATH_GPIOPIN_ANTCHAIN2;
            ah_signal_type = ATH_GPIOFUNC_ANTCHAIN2;
            smart_ant = 1;
            break;
        default:
            break;
    }
#endif

    if (smart_ant && AR_SREV_WASP(ah))
    {
        return AH_FALSE;
    }

    // Configure the MUX
    ar9300_gpio_cfg_output_mux(ah, gpio, ah_signal_type);

    // 2 bits per output mode
    gpio_shift = 2*gpio;
    
    OS_REG_RMW(ah,
               AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT),
               (AR_GPIO_OE_OUT_DRV_NO << gpio_shift),
               (AR_GPIO_OE_OUT_DRV << gpio_shift));

    return AH_TRUE;
#undef N
}

/*
 * Configure GPIO Input lines
 */
HAL_BOOL
ar9300_gpio_cfg_input(struct ath_hal *ah, u_int32_t gpio)
{
    u_int32_t    gpio_shift;

    HALASSERT(gpio < AH_PRIVATE(ah)->ah_caps.halNumGpioPins);
    if ((gpio == AR9382_GPIO_PIN_8_RESERVED)  ||
        (gpio > AR9382_MAX_GPIO_INPUT_PIN_NUM))
    {
        return AH_FALSE;
    }

    if (gpio <= AR9382_MAX_JTAG_GPIO_PIN_NUM) {
        OS_REG_SET_BIT(ah,
            AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL), AR_GPIO_JTAG_DISABLE);
    }
    /* TODO: configure input mux for AR9300 */
    /* If configured as input, set output to tristate */
    gpio_shift = 2 * gpio;

    OS_REG_RMW(ah,
               AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT),
               (AR_GPIO_OE_OUT_DRV_NO << gpio_shift),
               (AR_GPIO_OE_OUT_DRV << gpio_shift));
    return AH_TRUE;
}

/*
 * Once configured for I/O - set output lines
 * output the level of GPio PIN without care work mode 
 */
HAL_BOOL
ar9300_gpio_set(struct ath_hal *ah, u_int32_t gpio, u_int32_t val)
{
    HALASSERT(gpio < AH_PRIVATE(ah)->ah_caps.halNumGpioPins);
    if ((gpio == AR9382_GPIO_PIN_8_RESERVED)  ||
        (gpio == AR9382_GPIO_9_INPUT_ONLY))
    {
        return AH_FALSE;
    }
    OS_REG_RMW(ah, AR_HOSTIF_REG(ah, AR_GPIO_OUT),
        ((val & 1) << gpio), AR_GPIO_BIT(gpio));

    return AH_TRUE;
}

/*
 * Once configured for I/O - get input lines
 */
u_int32_t
ar9300_gpio_get(struct ath_hal *ah, u_int32_t gpio)
{
    u_int32_t gpio_in;
    HALASSERT(gpio < AH_PRIVATE(ah)->ah_caps.halNumGpioPins);
    if (gpio == AR9382_GPIO_PIN_8_RESERVED)
    {
        return 0xffffffff;
    }

    gpio_in = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_IN));
    OS_REG_RMW(ah, AR_HOSTIF_REG(ah, AR_GPIO_IN),
        (1 << gpio), AR_GPIO_BIT(gpio));
    return (MS(gpio_in, AR_GPIO_IN_VAL) & AR_GPIO_BIT(gpio)) != 0;
}

u_int32_t
ar9300_gpio_get_intr(struct ath_hal *ah)
{
    unsigned int mask = 0;
    struct ath_hal_9300 *ahp = AH9300(ah);

    mask = ahp->ah_gpio_cause;
    return mask;
}

/*
 * Set the GPIO Interrupt
 * Sync and Async interrupts are both set/cleared.
 * Async GPIO interrupts may not be raised when the chip is put to sleep.
 */
void
ar9300_gpio_set_intr(struct ath_hal *ah, u_int gpio, u_int32_t ilevel)
{


    int i, reg_bit;
    u_int32_t reg_val;
    u_int32_t regs[2], shifts[2];

#ifdef AH_ASSERT
    u_int32_t gpio_mask;
    u_int32_t old_field_val = 0, field_val = 0;
#endif

#ifdef ATH_GPIO_USE_ASYNC_CAUSE
    regs[0] = AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE);
    regs[1] = AR_HOSTIF_REG(ah, AR_INTR_ASYNC_MASK);
    shifts[0] = AR_INTR_ASYNC_ENABLE_GPIO_S;
    shifts[1] = AR_INTR_ASYNC_MASK_GPIO_S;
#else
    regs[0] = AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE);
    regs[1] = AR_HOSTIF_REG(ah, AR_INTR_SYNC_MASK);
    shifts[0] = AR_INTR_SYNC_ENABLE_GPIO_S;
    shifts[1] = AR_INTR_SYNC_MASK_GPIO_S;
#endif

    HALASSERT(gpio < AH_PRIVATE(ah)->ah_caps.halNumGpioPins);

    if ((gpio == AR9382_GPIO_PIN_8_RESERVED) ||
        (gpio > AR9382_MAX_GPIO_INPUT_PIN_NUM))
    {
        return;
    }

#ifdef AH_ASSERT
    gpio_mask = (1 << AH_PRIVATE(ah)->ah_caps.halNumGpioPins) - 1;
#endif
    if (ilevel == HAL_GPIO_INTR_DISABLE) {
        /* clear this GPIO's bit in the interrupt registers */
        for (i = 0; i < ARRAY_LENGTH(regs); i++) {
            reg_val = OS_REG_READ(ah, regs[i]);
            reg_bit = shifts[i] + gpio;
            reg_val &= ~(1 << reg_bit);
            OS_REG_WRITE(ah, regs[i], reg_val);

            /* check that each register has same GPIOs enabled */
#ifdef AH_ASSERT
            field_val = (reg_val >> shifts[i]) & gpio_mask;
            HALASSERT(i == 0 || old_field_val == field_val);
            old_field_val = field_val;
#endif
        }

    } else {
        reg_val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL));
        reg_bit = gpio;
        if (ilevel == HAL_GPIO_INTR_HIGH) {
            /* 0 == interrupt on pin high */
            reg_val &= ~(1 << reg_bit);
        } else if (ilevel == HAL_GPIO_INTR_LOW) {
            /* 1 == interrupt on pin low */
            reg_val |= (1 << reg_bit);
        }
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL), reg_val);

        /* set this GPIO's bit in the interrupt registers */
        for (i = 0; i < ARRAY_LENGTH(regs); i++) {
            reg_val = OS_REG_READ(ah, regs[i]);
            reg_bit = shifts[i] + gpio;
            reg_val |= (1 << reg_bit);
            OS_REG_WRITE(ah, regs[i], reg_val);

            /* check that each register has same GPIOs enabled */
#ifdef AH_ASSERT
            field_val = (reg_val >> shifts[i]) & gpio_mask;
            HALASSERT(i == 0 || old_field_val == field_val);
            old_field_val = field_val;
#endif
        }
    }
}

u_int32_t
ar9300_gpio_get_polarity(struct ath_hal *ah)
{
    return OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL));
 
}

void
ar9300_gpio_set_polarity(struct ath_hal *ah, u_int32_t pol_map,
                         u_int32_t changed_mask)
{
    u_int32_t gpio_mask;

    gpio_mask = (1 << AH_PRIVATE(ah)->ah_caps.halNumGpioPins) - 1;
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL), gpio_mask & pol_map);

#ifndef ATH_GPIO_USE_ASYNC_CAUSE
    /*
     * For SYNC_CAUSE type interrupts, we need to clear the cause register
     * explicitly. Otherwise an interrupt with the original polarity setting
     * will come up immediately (if there is already an interrupt source),
     * which is not what we want usually.
     */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE_CLR),
                 changed_mask << AR_INTR_SYNC_ENABLE_GPIO_S);
    OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE_CLR));
#endif
}

/*
 * get the GPIO input pin mask
 * gpio0 - gpio13
 * gpio8, gpio11, regard as reserved by the chip ar9382
 */

u_int32_t
ar9300_gpio_get_mask(struct ath_hal *ah)
{
    u_int32_t mask = (1 << (AR9382_MAX_GPIO_INPUT_PIN_NUM + 1) ) - 1;

    if (AH_PRIVATE(ah)->ah_devid == AR9300_DEVID_AR9380_PCIE) {
        mask = (1 << AR9382_MAX_GPIO_PIN_NUM) - 1;
        mask &= ~(1 << AR9382_GPIO_PIN_8_RESERVED);
    }
    return mask;
}

int
ar9300_gpio_set_mask(struct ath_hal *ah, u_int32_t mask, u_int32_t pol_map)
{
    u_int32_t invalid = ~((1 << (AR9382_MAX_GPIO_INPUT_PIN_NUM + 1)) - 1);

    if (AH_PRIVATE(ah)->ah_devid == AR9300_DEVID_AR9380_PCIE) {
        invalid = ~((1 << AR9382_MAX_GPIO_PIN_NUM) - 1);
        invalid |= 1 << AR9382_GPIO_PIN_8_RESERVED;
    }
    if (mask & invalid) {
        ath_hal_printf(ah, "%s: invalid GPIO mask 0x%x\n", __func__, mask);
        return -1;
    }
    AH9300(ah)->ah_gpio_mask = mask;
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL), mask & pol_map);

    return 0;
}

#ifdef AH_DEBUG
void ar9300_gpio_show(struct ath_hal *ah); 
void ar9300_gpio_show(struct ath_hal *ah) 
{
    ath_hal_printf(ah, "--- 9382 GPIOs ---(ah=%p)\n", ah );
    ath_hal_printf(ah,
        "AH9300(_ah)->ah_hostifregs:%p\r\n", &(AH9300(ah)->ah_hostifregs));
    ath_hal_printf(ah,
        "GPIO_OUT:         0x%08X\n",
        OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OUT)));
    ath_hal_printf(ah,
        "GPIO_IN:          0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_IN)));
    ath_hal_printf(ah,
        "GPIO_OE:          0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT)));
    ath_hal_printf(ah,
        "GPIO_OE1_OUT:     0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OE1_OUT)));
    ath_hal_printf(ah,
        "GPIO_INTR_POLAR:  0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL)));
    ath_hal_printf(ah,
        "GPIO_INPUT_VALUE: 0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL)));
    ath_hal_printf(ah,
        "GPIO_INPUT_MUX1:  0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX1)));
    ath_hal_printf(ah,
        "GPIO_INPUT_MUX2:  0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX2)));
    ath_hal_printf(ah,
        "GPIO_OUTPUT_MUX1: 0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX1)));
    ath_hal_printf(ah,
        "GPIO_OUTPUT_MUX2: 0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX2)));
    ath_hal_printf(ah,
        "GPIO_OUTPUT_MUX3: 0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX3)));
    ath_hal_printf(ah,
        "GPIO_INPUT_STATE: 0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INPUT_STATE)));
    ath_hal_printf(ah,
        "GPIO_PDPU:        0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_PDPU)));
    ath_hal_printf(ah,
        "GPIO_DS:          0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_DS)));
    ath_hal_printf(ah,
        "AR_INTR_ASYNC_ENABLE: 0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE)));
    ath_hal_printf(ah,
        "AR_INTR_ASYNC_MASK:   0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_MASK)));
    ath_hal_printf(ah,
        "AR_INTR_SYNC_ENABLE:  0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE)));
    ath_hal_printf(ah,
        "AR_INTR_SYNC_MASK:    0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_MASK)));
    ath_hal_printf(ah,
        "AR_INTR_ASYNC_CAUSE:  0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE)));
    ath_hal_printf(ah,
        "AR_INTR_SYNC_CAUSE:   0x%08X\n",
         OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE)));

}
#endif /*AH_DEBUG*/
