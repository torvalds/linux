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
/*
 *  Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include "ah.h"
#include "ah_internal.h"
#include "ar9300phy.h"
#include "ar9300reg.h"
#include "ar9300eep.h"

#ifdef ATH_TX99_DIAG
void 
ar9300_tx99_tgt_channel_pwr_update(struct ath_hal *ah, HAL_CHANNEL *c, u_int32_t txpower)
{
#define PWR_MAS(_r, _s)     (((_r) & 0x3f) << (_s))
    static int16_t pPwrArray[ar9300_rate_size] = { 0 };
    int32_t i;
    //u_int8_t ht40PowerIncForPdadc = 2;
    
    for (i = 0; i < ar9300_rate_size; i++)
        pPwrArray[i] = txpower;

    OS_REG_WRITE(ah, AR_PHY_TX_FORCED_GAIN, 0);

    /* Write the OFDM power per rate set */
    /* 6 (LSB), 9, 12, 18 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(1),
        PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_6_24], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_6_24], 16)
          | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_6_24],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_6_24],  0)
    );
    /* 24 (LSB), 36, 48, 54 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(2),
        PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_54], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_48], 16)
          | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_36],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_6_24],  0)
    );

	/* Write the CCK power per rate set */
    /* 1L (LSB), reserved, 2L, 2S (MSB) */  
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(3),
	    PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_1L_5L], 24)
		  | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_1L_5L],  16)
//		  | PWR_MAS(txPowerTimes2,  8) /* this is reserved for Osprey */
		  | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_1L_5L],   0)
	);
    /* 5.5L (LSB), 5.5S, 11L, 11S (MSB) */
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(4),
	    PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_11S], 24)
		  | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_11L], 16)
		  | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_5S],  8)
		  | PWR_MAS(pPwrArray[ALL_TARGET_LEGACY_1L_5L],  0)
	);

    /* Write the HT20 power per rate set */
    /* 0/8/16 (LSB), 1-3/9-11/17-19, 4, 5 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(5),
        PWR_MAS(pPwrArray[ALL_TARGET_HT20_5], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_4],  16)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_1_3_9_11_17_19],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_0_8_16],   0)
    );
    
    /* 6 (LSB), 7, 12, 13 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(6),
        PWR_MAS(pPwrArray[ALL_TARGET_HT20_13], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_12],  16)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_7],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_6],   0)
    );

    /* 14 (LSB), 15, 20, 21 */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(10),
        PWR_MAS(pPwrArray[ALL_TARGET_HT20_21], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_20],  16)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_15],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_14],   0)
    );

    /* Mixed HT20 and HT40 rates */
    /* HT20 22 (LSB), HT20 23, HT40 22, HT40 23 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(11),
        PWR_MAS(pPwrArray[ALL_TARGET_HT40_23], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_22],  16)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_23],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT20_22],   0)
    );
    
    /* Write the HT40 power per rate set */
    // correct PAR difference between HT40 and HT20/LEGACY
    /* 0/8/16 (LSB), 1-3/9-11/17-19, 4, 5 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(7),
        PWR_MAS(pPwrArray[ALL_TARGET_HT40_5], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_4],  16)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_1_3_9_11_17_19],  8)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_0_8_16],   0)
    );

    /* 6 (LSB), 7, 12, 13 (MSB) */
    OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(8),
        PWR_MAS(pPwrArray[ALL_TARGET_HT40_13], 24)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_12],  16)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_7], 8)
          | PWR_MAS(pPwrArray[ALL_TARGET_HT40_6], 0)
    );

    /* 14 (LSB), 15, 20, 21 */
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE(12),
	    PWR_MAS(pPwrArray[ALL_TARGET_HT40_21], 24)
	      | PWR_MAS(pPwrArray[ALL_TARGET_HT40_20],  16)
	      | PWR_MAS(pPwrArray[ALL_TARGET_HT40_15],  8)
	      | PWR_MAS(pPwrArray[ALL_TARGET_HT40_14],   0)
	);  
#undef PWR_MAS
}

void
ar9300_tx99_tgt_chainmsk_setup(struct ath_hal *ah, int tx_chainmask)
{
    if (tx_chainmask == 0x5) {
        OS_REG_WRITE(ah, AR_PHY_ANALOG_SWAP, OS_REG_READ(ah, AR_PHY_ANALOG_SWAP) | AR_PHY_SWAP_ALT_CHAIN);
    }
    OS_REG_WRITE(ah, AR_PHY_RX_CHAINMASK, tx_chainmask);
    OS_REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, tx_chainmask);

    OS_REG_WRITE(ah, AR_SELFGEN_MASK, tx_chainmask);
    if (tx_chainmask == 0x5) {
        OS_REG_WRITE(ah, AR_PHY_ANALOG_SWAP, OS_REG_READ(ah, AR_PHY_ANALOG_SWAP) | AR_PHY_SWAP_ALT_CHAIN);
    }
}

void
ar9300_tx99_tgt_set_single_carrier(struct ath_hal *ah, int tx_chain_mask, int chtype)
{
    OS_REG_WRITE(ah, AR_PHY_TST_DAC_CONST, OS_REG_READ(ah, AR_PHY_TST_DAC_CONST) | (0x7ff<<11) | 0x7ff);
    OS_REG_WRITE(ah, AR_PHY_TEST_CTL_STATUS, OS_REG_READ(ah, AR_PHY_TEST_CTL_STATUS) | (1<<7) | (1<<1));
    OS_REG_WRITE(ah, AR_PHY_ADDAC_PARA_CTL, (OS_REG_READ(ah, AR_PHY_ADDAC_PARA_CTL) | (1<<31) | (1<<15)) & ~(1<<13));

    /* 11G mode */
    if (!chtype)
    {
        OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                                                    | (0x1 << 3) | (0x1 << 2));
        if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP)
                                                        & ~(0x1 << 4)); 
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, (OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
                                                        | (0x1 << 26)  | (0x7 << 24)) 
                                                        & ~(0x1 << 22));
        } else {
            OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, OS_REG_READ(ah, AR_HORNET_CH0_TOP)
                                                        & ~(0x1 << 4)); 
            OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, (OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
                                                        | (0x1 << 26)  | (0x7 << 24)) 
                                                        & ~(0x1 << 22));
        }                                                    
        
        /* chain zero */
    	if((tx_chain_mask & 0x01) == 0x01) {
    		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX1, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX1)
                                                          | (0x1 << 31) | (0x5 << 15) 
                                                          | (0x3 << 9)) & ~(0x1 << 27) 
                                                          & ~(0x1 << 12));
    		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                                                          | (0x1 << 12) | (0x1 << 10) 
                                                          | (0x1 << 9)  | (0x1 << 8)  
                                                          | (0x1 << 7)) & ~(0x1 << 11));                                      
    		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
                                                          | (0x1 << 29) | (0x1 << 25) 
                                                          | (0x1 << 23) | (0x1 << 19) 
                                                          | (0x1 << 10) | (0x1 << 9)  
                                                          | (0x1 << 8)  | (0x1 << 3))
                                                          & ~(0x1 << 28)& ~(0x1 << 24)
                                                          & ~(0x1 << 22)& ~(0x1 << 7));
    		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1)
                                                          | (0x1 << 23))& ~(0x1 << 21));
    		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB1, OS_REG_READ(ah, AR_PHY_65NM_CH0_BB1)
                                                          | (0x1 << 12) | (0x1 << 10)
                                                          | (0x1 << 9)  | (0x1 << 8)
                                                          | (0x1 << 6)  | (0x1 << 5)
                                                          | (0x1 << 4)  | (0x1 << 3)
                                                          | (0x1 << 2));                                                          
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB2, OS_REG_READ(ah, AR_PHY_65NM_CH0_BB2)
                                                          | (0x1 << 31));                                                 
        }
        if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
            /* chain one */
        	if ((tx_chain_mask & 0x02) == 0x02 ) {
        	    OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX1, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX1)
                                                              | (0x1 << 31) | (0x5 << 15) 
                                                              | (0x3 << 9)) & ~(0x1 << 27) 
                                                              & ~(0x1 << 12));
        		OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
                                                              | (0x1 << 12) | (0x1 << 10) 
                                                              | (0x1 << 9)  | (0x1 << 8)  
                                                              | (0x1 << 7)) & ~(0x1 << 11));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
                                                              | (0x1 << 29) | (0x1 << 25) 
                                                              | (0x1 << 23) | (0x1 << 19) 
                                                              | (0x1 << 10) | (0x1 << 9)  
                                                              | (0x1 << 8)  | (0x1 << 3))
                                                              & ~(0x1 << 28)& ~(0x1 << 24)
                                                              & ~(0x1 << 22)& ~(0x1 << 7));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, (OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1)
                                                              | (0x1 << 23))& ~(0x1 << 21));
        		OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB1, OS_REG_READ(ah, AR_PHY_65NM_CH1_BB1)
                                                              | (0x1 << 12) | (0x1 << 10)
                                                              | (0x1 << 9)  | (0x1 << 8)
                                                              | (0x1 << 6)  | (0x1 << 5)
                                                              | (0x1 << 4)  | (0x1 << 3)
                                                              | (0x1 << 2));                                            
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB2, OS_REG_READ(ah, AR_PHY_65NM_CH1_BB2)
                                                              | (0x1 << 31));	
        	}
    	}
    	if (AR_SREV_OSPREY(ah)) {
        	/* chain two */
        	if ((tx_chain_mask & 0x04) == 0x04 ) {
        	    OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX1, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX1)
                                                              | (0x1 << 31) | (0x5 << 15) 
                                                              | (0x3 << 9)) & ~(0x1 << 27)
                                                              & ~(0x1 << 12));
        		OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
                                                              | (0x1 << 12) | (0x1 << 10) 
                                                              | (0x1 << 9)  | (0x1 << 8)  
                                                              | (0x1 << 7)) & ~(0x1 << 11));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
                                                              | (0x1 << 29) | (0x1 << 25) 
                                                              | (0x1 << 23) | (0x1 << 19) 
                                                              | (0x1 << 10) | (0x1 << 9)  
                                                              | (0x1 << 8)  | (0x1 << 3)) 
                                                              & ~(0x1 << 28)& ~(0x1 << 24) 
                                                              & ~(0x1 << 22)& ~(0x1 << 7));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, (OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1)
                                                              | (0x1 << 23))& ~(0x1 << 21));
        		OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB1, OS_REG_READ(ah, AR_PHY_65NM_CH2_BB1)
                                                              | (0x1 << 12) | (0x1 << 10)
                                                              | (0x1 << 9)  | (0x1 << 8)
                                                              | (0x1 << 6)  | (0x1 << 5)
                                                              | (0x1 << 4)  | (0x1 << 3)
                                                              | (0x1 << 2));                                            
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB2, OS_REG_READ(ah, AR_PHY_65NM_CH2_BB2)
                                                              | (0x1 << 31));	
        	}
    	}
    	
    	OS_REG_WRITE(ah, AR_PHY_SWITCH_COM_2, 0x11111);
        OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, 0x111);      
    }
    else
    {
        /* chain zero */
    	if((tx_chain_mask & 0x01) == 0x01) {
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX1, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX1)
    		                                              | (0x1 << 31) | (0x1 << 27)
                                                          | (0x3 << 23) | (0x1 << 19)
                                                          | (0x1 << 15) | (0x3 << 9))
                                                          & ~(0x1 << 12));
    		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                                                          | (0x1 << 12) | (0x1 << 10) 
                                                          | (0x1 << 9)  | (0x1 << 8)  
                                                          | (0x1 << 7)  | (0x1 << 3)  
                                                          | (0x1 << 2)  | (0x1 << 1)) 
                                                          & ~(0x1 << 11)& ~(0x1 << 0));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
			                                              | (0x1 << 29) | (0x1 << 25) 
                                                          | (0x1 << 23) | (0x1 << 19) 
                                                          | (0x1 << 10) | (0x1 << 9)  
                                                          | (0x1 << 8)  | (0x1 << 3))
                                                          & ~(0x1 << 28)& ~(0x1 << 24)
                                                          & ~(0x1 << 22)& ~(0x1 << 7));
			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1)
			                                              | (0x1 << 23))& ~(0x1 << 21));
			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF2, OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF2)
                                                          | (0x3 << 3)  | (0x3 << 0));
			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF3, (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF3)
                                                          | (0x3 << 29) | (0x3 << 26)
                                                          | (0x2 << 23) | (0x2 << 20)
                                                          | (0x2 << 17))& ~(0x1 << 14));
			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB1, OS_REG_READ(ah, AR_PHY_65NM_CH0_BB1)
			                                              | (0x1 << 12) | (0x1 << 10)
                                                          | (0x1 << 9)  | (0x1 << 8)
                                                          | (0x1 << 6)  | (0x1 << 5)
                                                          | (0x1 << 4)  | (0x1 << 3)
                                                          | (0x1 << 2));
			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB2, OS_REG_READ(ah, AR_PHY_65NM_CH0_BB2)
                                                          | (0x1 << 31));
			if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP)
    			                                              & ~(0x1 << 4));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
    		                                                  | (0x1 << 26) | (0x7 << 24)
    		                                                  | (0x3 << 22));
            } else {
    			OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, OS_REG_READ(ah, AR_HORNET_CH0_TOP)
    			                                              & ~(0x1 << 4));
    			OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
    		                                                  | (0x1 << 26) | (0x7 << 24)
    		                                                  | (0x3 << 22));
		    }	                                                 
                                    
            if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
    			                                              | (0x1 << 3)  | (0x1 << 2)
                                                              | (0x1 << 1)) & ~(0x1 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
    			                                              | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1)
    			                                              | (0x1 << 23));
			}
			if (AR_SREV_OSPREY(ah)) {    		
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
    			                                              | (0x1 << 3)  | (0x1 << 2)
                                                              | (0x1 << 1)) & ~(0x1 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
    			                                              | (0x1 << 19) | (0x1 << 3));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1)
    			                                              | (0x1 << 23));
    	    }
    	}
    	if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
            /* chain one */
        	if ((tx_chain_mask & 0x02) == 0x02 ) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                                                              | (0x1 << 3)  | (0x1 << 2)
                                                              | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
    			                                              | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1)
    			                                              | (0x1 << 23));
    			if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
        			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP)
                                                                  & ~(0x1 << 4));
        			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
        		                                                  | (0x1 << 26) | (0x7 << 24)
        		                                                  | (0x3 << 22));
                } else {
        			OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, OS_REG_READ(ah, AR_HORNET_CH0_TOP)
                                                                  & ~(0x1 << 4));
        			OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
        		                                                  | (0x1 << 26) | (0x7 << 24)
        		                                                  | (0x3 << 22));
                }
                
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX1, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX1)
        		                                              | (0x1 << 31) | (0x1 << 27)
                                                              | (0x3 << 23) | (0x1 << 19)
                                                              | (0x1 << 15) | (0x3 << 9)) 
                                                              & ~(0x1 << 12));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
                                                              | (0x1 << 12) | (0x1 << 10) 
                                                              | (0x1 << 9)  | (0x1 << 8)  
                                                              | (0x1 << 7)  | (0x1 << 3)  
                                                              | (0x1 << 2)  | (0x1 << 1))  
                                                              & ~(0x1 << 11)& ~(0x1 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
    			                                              | (0x1 << 29) | (0x1 << 25) 
                                                              | (0x1 << 23) | (0x1 << 19) 
                                                              | (0x1 << 10) | (0x1 << 9)  
                                                              | (0x1 << 8)  | (0x1 << 3))
                                                              & ~(0x1 << 28)& ~(0x1 << 24)
                                                              & ~(0x1 << 22)& ~(0x1 << 7));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, (OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1)
    			                                              | (0x1 << 23))& ~(0x1 << 21));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF2, OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF2)
                                                              | (0x3 << 3)  | (0x3 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF3, (OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF3)
                                                              | (0x3 << 29) | (0x3 << 26)
                                                              | (0x2 << 23) | (0x2 << 20)
                                                              | (0x2 << 17))& ~(0x1 << 14));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB1, OS_REG_READ(ah, AR_PHY_65NM_CH1_BB1)
    			                                              | (0x1 << 12) | (0x1 << 10)
                                                              | (0x1 << 9)  | (0x1 << 8)
                                                              | (0x1 << 6)  | (0x1 << 5)
                                                              | (0x1 << 4)  | (0x1 << 3)
                                                              | (0x1 << 2));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB2, OS_REG_READ(ah, AR_PHY_65NM_CH1_BB2)
                                                              | (0x1 << 31));
    			
    			if (AR_SREV_OSPREY(ah)) {
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
        			                                              | (0x1 << 3)  | (0x1 << 2)
                                                                  | (0x1 << 1)) & ~(0x1 << 0));
        			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
        			                                              | (0x1 << 19) | (0x1 << 3));
        			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1)
        			                                              | (0x1 << 23));
        	    }
        	}
    	}
    	if (AR_SREV_OSPREY(ah)) {
        	/* chain two */
        	if ((tx_chain_mask & 0x04) == 0x04 ) {
        		OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                                                              | (0x1 << 3)  | (0x1 << 2)
                                                              | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
    			                                              | (0x1 << 19) | (0x1 << 3));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1)
    			                                              | (0x1 << 23));
    			if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
        			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP)
        			                                              & ~(0x1 << 4));
        			OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
        		                                                  | (0x1 << 26) | (0x7 << 24)
        		                                                  | (0x3 << 22));
    		    } else {
        			OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, OS_REG_READ(ah, AR_HORNET_CH0_TOP)
        			                                              & ~(0x1 << 4));
        			OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
        		                                                  | (0x1 << 26) | (0x7 << 24)
        		                                                  | (0x3 << 22));
    		    }
    		    
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
    			                                              | (0x1 << 3)  | (0x1 << 2)
                                                              | (0x1 << 1)) & ~(0x1 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
    			                                              | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1)
    			                                              | (0x1 << 23));
    			
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX1, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX1)
        		                                              | (0x1 << 31) | (0x1 << 27)
                                                              | (0x3 << 23) | (0x1 << 19)
                                                              | (0x1 << 15) | (0x3 << 9)) 
                                                              & ~(0x1 << 12));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
                                                              | (0x1 << 12) | (0x1 << 10) 
                                                              | (0x1 << 9)  | (0x1 << 8)  
                                                              | (0x1 << 7)  | (0x1 << 3)  
                                                              | (0x1 << 2)  | (0x1 << 1))  
                                                              & ~(0x1 << 11)& ~(0x1 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
    			                                              | (0x1 << 29) | (0x1 << 25) 
                                                              | (0x1 << 23) | (0x1 << 19) 
                                                              | (0x1 << 10) | (0x1 << 9)  
                                                              | (0x1 << 8)  | (0x1 << 3))
                                                              & ~(0x1 << 28)& ~(0x1 << 24)
                                                              & ~(0x1 << 22)& ~(0x1 << 7));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, (OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1)
    			                                              | (0x1 << 23))& ~(0x1 << 21));
        		OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF2, OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF2)
                                                              | (0x3 << 3)  | (0x3 << 0));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF3, (OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF3)
                                                              | (0x3 << 29) | (0x3 << 26)
                                                              | (0x2 << 23) | (0x2 << 20)
                                                              | (0x2 << 17))& ~(0x1 << 14));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB1, OS_REG_READ(ah, AR_PHY_65NM_CH2_BB1)
    			                                              | (0x1 << 12) | (0x1 << 10)
                                                              | (0x1 << 9)  | (0x1 << 8)
                                                              | (0x1 << 6)  | (0x1 << 5)
                                                              | (0x1 << 4)  | (0x1 << 3)
                                                              | (0x1 << 2));
    			OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB2, OS_REG_READ(ah, AR_PHY_65NM_CH2_BB2)
                                                              | (0x1 << 31));
    		}
		}
    	
        OS_REG_WRITE(ah, AR_PHY_SWITCH_COM_2, 0x22222);
        OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, 0x222);
    }
}

void 
ar9300_tx99_tgt_start(struct ath_hal *ah, u_int8_t data)
{
    a_uint32_t val;
    a_uint32_t qnum = (a_uint32_t)data;

    /* Disable AGC to A2 */
    OS_REG_WRITE(ah, AR_PHY_TEST, (OS_REG_READ(ah, AR_PHY_TEST) | PHY_AGC_CLR) );
    OS_REG_WRITE(ah, 0x9864, OS_REG_READ(ah, 0x9864) | 0x7f000);
    OS_REG_WRITE(ah, 0x9924, OS_REG_READ(ah, 0x9924) | 0x7f00fe);
    OS_REG_WRITE(ah, AR_DIAG_SW, OS_REG_READ(ah, AR_DIAG_SW) &~ AR_DIAG_RX_DIS);
    //OS_REG_WRITE(ah, AR_DIAG_SW, OS_REG_READ(ah, AR_DIAG_SW) | (AR_DIAG_FORCE_RX_CLEAR+AR_DIAG_IGNORE_VIRT_CS));
    OS_REG_WRITE(ah, AR_CR, AR_CR_RXD);     // set receive disable
    //set CW_MIN and CW_MAX both to 0, AIFS=2
    OS_REG_WRITE(ah, AR_DLCL_IFS(qnum), 0);
    OS_REG_WRITE(ah, AR_D_GBL_IFS_SIFS, 20); //50 OK
    OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, 20);
    OS_REG_WRITE(ah, AR_TIME_OUT, 0x00000400); //200 ok for HT20, 400 ok for HT40 
    OS_REG_WRITE(ah, AR_DRETRY_LIMIT(qnum), 0xffffffff);
    
    /* set QCU modes to early termination */
    val = OS_REG_READ(ah, AR_QMISC(qnum));
    OS_REG_WRITE(ah, AR_QMISC(qnum), val | AR_Q_MISC_DCU_EARLY_TERM_REQ);
}

void 
ar9300_tx99_tgt_stop(struct ath_hal *ah)
{
    OS_REG_WRITE(ah, AR_PHY_TEST, OS_REG_READ(ah, AR_PHY_TEST) &~ PHY_AGC_CLR);
    OS_REG_WRITE(ah, AR_DIAG_SW, OS_REG_READ(ah, AR_DIAG_SW) &~ (AR_DIAG_FORCE_RX_CLEAR | AR_DIAG_IGNORE_VIRT_CS));
}
#endif
