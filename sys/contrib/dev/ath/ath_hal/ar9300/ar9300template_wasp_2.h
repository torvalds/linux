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
 * READ THIS NOTICE!
 *
 * Values defined in this file may only be changed under exceptional circumstances.
 *
 * Please ask Fiona Cain before making any changes.
 */

#ifndef __ar9300template_wasp_2_h__
#define __ar9300template_wasp_2_h__
static ar9300_eeprom_t ar9300_template_wasp_2=
{

	2, //  eepromVersion;

    ar9300_eeprom_template_wasp_2, //  templateVersion;

	{0x00,0x03,0x7f,0x0,0x0,0x0}, //mac_addr[6];

    //static  A_UINT8   custData[OSPREY_CUSTOMER_DATA_SIZE]=

	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},

    //static OSPREY_BASE_EEP_HEADER baseEepHeader=

	{
		    {0,0x1f},	//   regDmn[2]; //Does this need to be outside of this structure, if it gets written after calibration
		    0x33,	//   txrxMask;  //4 bits tx and 4 bits rx
		    {AR9300_OPFLAGS_11G | AR9300_OPFLAGS_11A, 3},	//   opCapFlags;
		    0,		//   rfSilent;
		    0,		//   blueToothOptions;
		    0,		//   deviceCap;
		    4,		//   deviceType; // takes lower byte in eeprom location
		    OSPREY_PWR_TABLE_OFFSET,	//    pwrTableOffset; // offset in dB to be added to beginning of pdadc table in calibration
			{0,0},	//   params_for_tuning_caps[2];  //placeholder, get more details from Don
            0x0c,     //featureEnable; //bit0 - enable tx temp comp 
                             //bit1 - enable tx volt comp
                             //bit2 - enable fastClock - default to 1
                             //bit3 - enable doubling - default to 1
 							 //bit4 - enable internal regulator - default to 0
    		0,       //miscConfiguration: bit0 - turn down drivestrength
			3,		// eepromWriteEnableGpio
			0,		// wlanDisableGpio
			8,		// wlanLedGpio
			0xff,		// rxBandSelectGpio
			0,			// txrxgain
            0,		//   swreg
	},


	//static OSPREY_MODAL_EEP_HEADER modalHeader2G=
	{

		    0x220,			//  antCtrlCommon;                         // 4   idle, t1, t2, b (4 bits per setting)
		    0x88888,		//  antCtrlCommon2;                        // 4    ra1l1, ra2l1, ra1l2, ra2l2, ra12
		    {0x150,0x150,0x150},	//  antCtrlChain[OSPREY_MAX_CHAINS];       // 6   idle, t, r, rx1, rx12, b (2 bits each)
		    {0,0,0},			//   xatten1DB[OSPREY_MAX_CHAINS];           // 3  //xatten1_db for merlin (0xa20c/b20c 5:0)
		    {0,0,0},			//   xatten1Margin[OSPREY_MAX_CHAINS];          // 3  //xatten1_margin for merlin (0xa20c/b20c 16:12
			36,				//    tempSlope;
			0,				//    voltSlope;
		    {0,0,0,0,0}, // spurChans[OSPREY_EEPROM_MODAL_SPURS];  // spur channels in usual fbin coding format
		    {-1,0,0},			//    noiseFloorThreshCh[OSPREY_MAX_CHAINS]; // 3    //Check if the register is per chain
            {0, 0, 0, 0, 0, 0,0,0,0,0,0},               // reserved
            0,                                          // quick drop  
		    0,				//   xpaBiasLvl;                            // 1
		    0x0e,			//   txFrameToDataStart;                    // 1
		    0x0e,			//   txFrameToPaOn;                         // 1
		    3,				//   txClip;                                     // 4 bits tx_clip, 4 bits dac_scale_cck
		    0,				//    antennaGain;                           // 1
		    0x2c,			//   switchSettling;                        // 1
		    -30,			//    adcDesiredSize;                        // 1
		    0,				//   txEndToXpaOff;                         // 1
		    0x2,			//   txEndToRxOn;                           // 1
		    0xe,			//   txFrameToXpaOn;                        // 1
		    28,				//   thresh62;                              // 1
			0x0c80C080,		//	 papdRateMaskHt20						// 4
  			0x0080C080,		//	 papdRateMaskHt40	
		    0,				//   switchcomspdt;                         // 2
			0,				// bit: 0,1:chain0, 2,3:chain1, 4,5:chain2
			0,				//  rf_gain_cap
			0,				//  tx_gain_cap
			{0,0,0,0,0}    //futureModal[5];
	},

	{
		0,									//   ant_div_control
			{0,0},					// base_ext1
			0,						// misc_enable
			{0,0,0,0,0,0,0,0},		// temp slop extension
		0,									// quick drop low
		0,									// quick drop high
	},

	//static A_UINT8 calFreqPier2G[OSPREY_NUM_2G_CAL_PIERS]=
	{
		FREQ2FBIN(2412, 1),
		FREQ2FBIN(2437, 1),
		FREQ2FBIN(2472, 1)
	},

	//static OSP_CAL_DATA_PER_FREQ_OP_LOOP calPierData2G[OSPREY_MAX_CHAINS][OSPREY_NUM_2G_CAL_PIERS]=

	{	{{0,0,0,0,0,0},  {0,0,0,0,0,0},  {0,0,0,0,0,0}},
		{{0,0,0,0,0,0},  {0,0,0,0,0,0},  {0,0,0,0,0,0}},
		{{0,0,0,0,0,0},  {0,0,0,0,0,0},  {0,0,0,0,0,0}},
	},

	//A_UINT8 calTarget_freqbin_Cck[OSPREY_NUM_2G_CCK_TARGET_POWERS];

	{
		FREQ2FBIN(2412, 1),
		FREQ2FBIN(2484, 1)
	},

	//static CAL_TARGET_POWER_LEG calTarget_freqbin_2G[OSPREY_NUM_2G_20_TARGET_POWERS]
	{
		FREQ2FBIN(2412, 1),
		FREQ2FBIN(2437, 1),
		FREQ2FBIN(2472, 1)
	},

	//static   OSP_CAL_TARGET_POWER_HT  calTarget_freqbin_2GHT20[OSPREY_NUM_2G_20_TARGET_POWERS]
	{
		FREQ2FBIN(2412, 1),
		FREQ2FBIN(2437, 1),
		FREQ2FBIN(2472, 1)
	},

	//static   OSP_CAL_TARGET_POWER_HT  calTarget_freqbin_2GHT40[OSPREY_NUM_2G_40_TARGET_POWERS]
	{
		FREQ2FBIN(2412, 1),
		FREQ2FBIN(2437, 1),
		FREQ2FBIN(2472, 1)
	},

	//static CAL_TARGET_POWER_LEG calTargetPowerCck[OSPREY_NUM_2G_CCK_TARGET_POWERS]=
	{
		//1L-5L,5S,11L,11S
        {{36,36,36,36}},
	 	{{36,36,36,36}}
	 },

	//static CAL_TARGET_POWER_LEG calTargetPower2G[OSPREY_NUM_2G_20_TARGET_POWERS]=
	{
        //6-24,36,48,54
		{{32,32,28,24}},
		{{32,32,28,24}},
		{{32,32,28,24}},
	},

	//static   OSP_CAL_TARGET_POWER_HT  calTargetPower2GHT20[OSPREY_NUM_2G_20_TARGET_POWERS]=
	{
        //0_8_16,1-3_9-11_17-19,
        //      4,5,6,7,12,13,14,15,20,21,22,23
		{{32,32,32,32,28,20,32,32,28,20,32,32,28,20}},
		{{32,32,32,32,28,20,32,32,28,20,32,32,28,20}},
		{{32,32,32,32,28,20,32,32,28,20,32,32,28,20}},
	},

	//static    OSP_CAL_TARGET_POWER_HT  calTargetPower2GHT40[OSPREY_NUM_2G_40_TARGET_POWERS]=
	{
        //0_8_16,1-3_9-11_17-19,
        //      4,5,6,7,12,13,14,15,20,21,22,23
		{{32,32,32,32,28,20,32,32,28,20,32,32,28,20}},
		{{32,32,32,32,28,20,32,32,28,20,32,32,28,20}},
		{{32,32,32,32,28,20,32,32,28,20,32,32,28,20}},
	},

//static    A_UINT8            ctlIndex_2G[OSPREY_NUM_CTLS_2G]=

	{

		    0x11,
    		0x12,
    		0x15,
    		0x17,
    		0x41,
    		0x42,
   			0x45,
    		0x47,
   			0x31,
    		0x32,
    		0x35,
    		0x37

    },

//A_UINT8   ctl_freqbin_2G[OSPREY_NUM_CTLS_2G][OSPREY_NUM_BAND_EDGES_2G];

	{
		{FREQ2FBIN(2412, 1),
		 FREQ2FBIN(2417, 1),
		 FREQ2FBIN(2457, 1),
		 FREQ2FBIN(2462, 1)},

		{FREQ2FBIN(2412, 1),
		 FREQ2FBIN(2417, 1),
		 FREQ2FBIN(2462, 1),
		 0xFF},

		{FREQ2FBIN(2412, 1),
		 FREQ2FBIN(2417, 1),
		 FREQ2FBIN(2462, 1),
		 0xFF},

		{FREQ2FBIN(2422, 1),
		 FREQ2FBIN(2427, 1),
		 FREQ2FBIN(2447, 1),
		 FREQ2FBIN(2452, 1)},

		{/*Data[4].ctlEdges[0].bChannel*/FREQ2FBIN(2412, 1),
		/*Data[4].ctlEdges[1].bChannel*/FREQ2FBIN(2417, 1),
		/*Data[4].ctlEdges[2].bChannel*/FREQ2FBIN(2472, 1),
		/*Data[4].ctlEdges[3].bChannel*/FREQ2FBIN(2484, 1)},

		{/*Data[5].ctlEdges[0].bChannel*/FREQ2FBIN(2412, 1),
		 /*Data[5].ctlEdges[1].bChannel*/FREQ2FBIN(2417, 1),
		 /*Data[5].ctlEdges[2].bChannel*/FREQ2FBIN(2472, 1),
		 0},

		{/*Data[6].ctlEdges[0].bChannel*/FREQ2FBIN(2412, 1),
		 /*Data[6].ctlEdges[1].bChannel*/FREQ2FBIN(2417, 1),
		 FREQ2FBIN(2472, 1),
		 0},

		{/*Data[7].ctlEdges[0].bChannel*/FREQ2FBIN(2422, 1),
		 /*Data[7].ctlEdges[1].bChannel*/FREQ2FBIN(2427, 1),
		 /*Data[7].ctlEdges[2].bChannel*/FREQ2FBIN(2447, 1),
		 /*Data[7].ctlEdges[3].bChannel*/FREQ2FBIN(2462, 1)},

		{/*Data[8].ctlEdges[0].bChannel*/FREQ2FBIN(2412, 1),
		 /*Data[8].ctlEdges[1].bChannel*/FREQ2FBIN(2417, 1),
		 /*Data[8].ctlEdges[2].bChannel*/FREQ2FBIN(2472, 1),
		 0},

		{/*Data[9].ctlEdges[0].bChannel*/FREQ2FBIN(2412, 1),
		 /*Data[9].ctlEdges[1].bChannel*/FREQ2FBIN(2417, 1),
		 /*Data[9].ctlEdges[2].bChannel*/FREQ2FBIN(2472, 1),
		 0},

		{/*Data[10].ctlEdges[0].bChannel*/FREQ2FBIN(2412, 1),
		 /*Data[10].ctlEdges[1].bChannel*/FREQ2FBIN(2417, 1),
		 /*Data[10].ctlEdges[2].bChannel*/FREQ2FBIN(2472, 1),
		 0},

		{/*Data[11].ctlEdges[0].bChannel*/FREQ2FBIN(2422, 1),
		 /*Data[11].ctlEdges[1].bChannel*/FREQ2FBIN(2427, 1),
		 /*Data[11].ctlEdges[2].bChannel*/FREQ2FBIN(2447, 1),
		 /*Data[11].ctlEdges[3].bChannel*/FREQ2FBIN(2462, 1)}
	},


//OSP_CAL_CTL_DATA_2G   ctlPowerData_2G[OSPREY_NUM_CTLS_2G];

#if AH_BYTE_ORDER == AH_BIG_ENDIAN
    {

	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}},
	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}}, 
	    {{{1, 60}, {0, 60}, {0, 60}, {1, 60}}},

	    {{{1, 60}, {0, 60}, {0, 60}, {0, 60}}},
	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}},
	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}},

	    {{{0, 60}, {1, 60}, {1, 60}, {0, 60}}},
	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}},
	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}},

	    {{{0, 60}, {1, 60}, {0, 60}, {0, 60}}},
	    {{{0, 60}, {1, 60}, {1, 60}, {1, 60}}},
	    {{{0, 60}, {1, 60}, {1, 60}, {1, 60}}},
        
    },
#else
	{
	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}},
	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}}, 
	    {{{60, 1}, {60, 0}, {60, 0}, {60, 1}}},

	    {{{60, 1}, {60, 0}, {60, 0}, {60, 0}}},
	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}},
	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}},

	    {{{60, 0}, {60, 1}, {60, 1}, {60, 0}}},
	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}},
	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}},

	    {{{60, 0}, {60, 1}, {60, 0}, {60, 0}}},
	    {{{60, 0}, {60, 1}, {60, 1}, {60, 1}}},
	    {{{60, 0}, {60, 1}, {60, 1}, {60, 1}}},
	},
#endif

//static    OSPREY_MODAL_EEP_HEADER   modalHeader5G=

	{

		    0x440,			//  antCtrlCommon;                         // 4   idle, t1, t2, b (4 bits per setting)
		    0x11111,		//  antCtrlCommon2;                        // 4    ra1l1, ra2l1, ra1l2, ra2l2, ra12
		    {0x000,0x000,0x000},	//  antCtrlChain[OSPREY_MAX_CHAINS];       // 6   idle, t, r, rx1, rx12, b (2 bits each)
		    {0,0,0},			//   xatten1DB[OSPREY_MAX_CHAINS];           // 3  //xatten1_db for merlin (0xa20c/b20c 5:0)
		    {0,0,0},			//   xatten1Margin[OSPREY_MAX_CHAINS];          // 3  //xatten1_margin for merlin (0xa20c/b20c 16:12
			68,				//    tempSlope;
			0,				//    voltSlope;
		    {0,0,0,0,0}, // spurChans[OSPREY_EEPROM_MODAL_SPURS];  // spur channels in usual fbin coding format
		    {-1,0,0},			//    noiseFloorThreshCh[OSPREY_MAX_CHAINS]; // 3    //Check if the register is per chain
			{0, 0, 0, 0, 0, 0,0,0,0,0,0},				// reserved
			0,											// quick drop  
		    0,				//   xpaBiasLvl;                            // 1
		    0x0e,			//   txFrameToDataStart;                    // 1
		    0x0e,			//   txFrameToPaOn;                         // 1
		    3,				//   txClip;                                     // 4 bits tx_clip, 4 bits dac_scale_cck
		    0,				//    antennaGain;                           // 1
		    0x2d,			//   switchSettling;                        // 1
		    -30,			//    adcDesiredSize;                        // 1
		    0,				//   txEndToXpaOff;                         // 1
		    0x2,			//   txEndToRxOn;                           // 1
		    0xe,			//   txFrameToXpaOn;                        // 1
		    28,				//   thresh62;                              // 1
  			0x0cf0e0e0,		//	 papdRateMaskHt20						// 4
  			0x6cf0e0e0,		//	 papdRateMaskHt40						// 4
		    0,				//   switchcomspdt;                         // 2
			0,				// bit: 0,1:chain0, 2,3:chain1, 4,5:chain2
			0,				//  rf_gain_cap
			0,				//  tx_gain_cap
			{0,0,0,0,0}    //futureModal[5];
	},

	{			// base_ext2
		0,
		0,
		{0,0,0},
		{0,0,0},
		{0,0,0},
		{0,0,0}
	},						

//static    A_UINT8            calFreqPier5G[OSPREY_NUM_5G_CAL_PIERS]=
	{
		    //pPiers[0] =
		    FREQ2FBIN(5180, 0),
		    //pPiers[1] =
		    FREQ2FBIN(5220, 0),
		    //pPiers[2] =
		    FREQ2FBIN(5320, 0),
		    //pPiers[3] =
		    FREQ2FBIN(5400, 0),
		    //pPiers[4] =
		    FREQ2FBIN(5500, 0),
		    //pPiers[5] =
		    FREQ2FBIN(5600, 0),
		    //pPiers[6] =
		    FREQ2FBIN(5725, 0),
    		//pPiers[7] =
    		FREQ2FBIN(5825, 0)
	},

//static    OSP_CAL_DATA_PER_FREQ_OP_LOOP calPierData5G[OSPREY_MAX_CHAINS][OSPREY_NUM_5G_CAL_PIERS]=

	{
		{{0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},    {0,0,0,0,0},  {0,0,0,0,0}},
		{{0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},    {0,0,0,0,0},  {0,0,0,0,0}},
		{{0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},  {0,0,0,0,0},    {0,0,0,0,0},  {0,0,0,0,0}},

	},

//static    CAL_TARGET_POWER_LEG calTarget_freqbin_5G[OSPREY_NUM_5G_20_TARGET_POWERS]=

	{
			FREQ2FBIN(5180, 0),
			FREQ2FBIN(5220, 0),
			FREQ2FBIN(5320, 0),
			FREQ2FBIN(5400, 0),
			FREQ2FBIN(5500, 0),
			FREQ2FBIN(5600, 0),
			FREQ2FBIN(5725, 0),
			FREQ2FBIN(5825, 0)
	},

//static    OSP_CAL_TARGET_POWER_HT  calTargetPower5GHT20[OSPREY_NUM_5G_20_TARGET_POWERS]=

	{
			FREQ2FBIN(5180, 0),
			FREQ2FBIN(5240, 0),
			FREQ2FBIN(5320, 0),
			FREQ2FBIN(5500, 0),
			FREQ2FBIN(5700, 0),
			FREQ2FBIN(5745, 0),
			FREQ2FBIN(5725, 0),
			FREQ2FBIN(5825, 0)
	},

//static    OSP_CAL_TARGET_POWER_HT  calTargetPower5GHT40[OSPREY_NUM_5G_40_TARGET_POWERS]=

	{
			FREQ2FBIN(5180, 0),
			FREQ2FBIN(5240, 0),
			FREQ2FBIN(5320, 0),
			FREQ2FBIN(5500, 0),
			FREQ2FBIN(5700, 0),
			FREQ2FBIN(5745, 0),
			FREQ2FBIN(5725, 0),
			FREQ2FBIN(5825, 0)
	},


//static    CAL_TARGET_POWER_LEG calTargetPower5G[OSPREY_NUM_5G_20_TARGET_POWERS]=


	{
        //6-24,36,48,54
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	    {{20,20,20,10}},
	},

//static    OSP_CAL_TARGET_POWER_HT  calTargetPower5GHT20[OSPREY_NUM_5G_20_TARGET_POWERS]=

	{
        //0_8_16,1-3_9-11_17-19,
        //      4,5,6,7,12,13,14,15,20,21,22,23
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	},

//static    OSP_CAL_TARGET_POWER_HT  calTargetPower5GHT40[OSPREY_NUM_5G_40_TARGET_POWERS]=
	{
        //0_8_16,1-3_9-11_17-19,
        //      4,5,6,7,12,13,14,15,20,21,22,23
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	    {{20,20,10,10,0,0,10,10,0,0,10,10,0,0}},
	},

//static    A_UINT8            ctlIndex_5G[OSPREY_NUM_CTLS_5G]=

	{
		    //pCtlIndex[0] =
		    0x10,
		    //pCtlIndex[1] =
		    0x16,
		    //pCtlIndex[2] =
		    0x18,
		    //pCtlIndex[3] =
		    0x40,
		    //pCtlIndex[4] =
		    0x46,
		    //pCtlIndex[5] =
		    0x48,
		    //pCtlIndex[6] =
		    0x30,
		    //pCtlIndex[7] =
		    0x36,
    		//pCtlIndex[8] =
    		0x38
	},

//    A_UINT8   ctl_freqbin_5G[OSPREY_NUM_CTLS_5G][OSPREY_NUM_BAND_EDGES_5G];

	{
	    {/* Data[0].ctlEdges[0].bChannel*/FREQ2FBIN(5180, 0),
	    /* Data[0].ctlEdges[1].bChannel*/FREQ2FBIN(5260, 0),
	    /* Data[0].ctlEdges[2].bChannel*/FREQ2FBIN(5280, 0),
	    /* Data[0].ctlEdges[3].bChannel*/FREQ2FBIN(5500, 0),
	    /* Data[0].ctlEdges[4].bChannel*/FREQ2FBIN(5600, 0),
	    /* Data[0].ctlEdges[5].bChannel*/FREQ2FBIN(5700, 0),
	    /* Data[0].ctlEdges[6].bChannel*/FREQ2FBIN(5745, 0),
	    /* Data[0].ctlEdges[7].bChannel*/FREQ2FBIN(5825, 0)},

	    {/* Data[1].ctlEdges[0].bChannel*/FREQ2FBIN(5180, 0),
	    /* Data[1].ctlEdges[1].bChannel*/FREQ2FBIN(5260, 0),
	    /* Data[1].ctlEdges[2].bChannel*/FREQ2FBIN(5280, 0),
	    /* Data[1].ctlEdges[3].bChannel*/FREQ2FBIN(5500, 0),
	    /* Data[1].ctlEdges[4].bChannel*/FREQ2FBIN(5520, 0),
	    /* Data[1].ctlEdges[5].bChannel*/FREQ2FBIN(5700, 0),
	    /* Data[1].ctlEdges[6].bChannel*/FREQ2FBIN(5745, 0),
	    /* Data[1].ctlEdges[7].bChannel*/FREQ2FBIN(5825, 0)},

	    {/* Data[2].ctlEdges[0].bChannel*/FREQ2FBIN(5190, 0),
	    /* Data[2].ctlEdges[1].bChannel*/FREQ2FBIN(5230, 0),
	    /* Data[2].ctlEdges[2].bChannel*/FREQ2FBIN(5270, 0),
	    /* Data[2].ctlEdges[3].bChannel*/FREQ2FBIN(5310, 0),
	    /* Data[2].ctlEdges[4].bChannel*/FREQ2FBIN(5510, 0),
	    /* Data[2].ctlEdges[5].bChannel*/FREQ2FBIN(5550, 0),
	    /* Data[2].ctlEdges[6].bChannel*/FREQ2FBIN(5670, 0),
	    /* Data[2].ctlEdges[7].bChannel*/FREQ2FBIN(5755, 0)},

	    {/* Data[3].ctlEdges[0].bChannel*/FREQ2FBIN(5180, 0),
	    /* Data[3].ctlEdges[1].bChannel*/FREQ2FBIN(5200, 0),
	    /* Data[3].ctlEdges[2].bChannel*/FREQ2FBIN(5260, 0),
	    /* Data[3].ctlEdges[3].bChannel*/FREQ2FBIN(5320, 0),
	    /* Data[3].ctlEdges[4].bChannel*/FREQ2FBIN(5500, 0),
	    /* Data[3].ctlEdges[5].bChannel*/FREQ2FBIN(5700, 0),
	    /* Data[3].ctlEdges[6].bChannel*/0xFF,
	    /* Data[3].ctlEdges[7].bChannel*/0xFF},

	    {/* Data[4].ctlEdges[0].bChannel*/FREQ2FBIN(5180, 0),
	    /* Data[4].ctlEdges[1].bChannel*/FREQ2FBIN(5260, 0),
	    /* Data[4].ctlEdges[2].bChannel*/FREQ2FBIN(5500, 0),
	    /* Data[4].ctlEdges[3].bChannel*/FREQ2FBIN(5700, 0),
	    /* Data[4].ctlEdges[4].bChannel*/0xFF,
	    /* Data[4].ctlEdges[5].bChannel*/0xFF,
	    /* Data[4].ctlEdges[6].bChannel*/0xFF,
	    /* Data[4].ctlEdges[7].bChannel*/0xFF},

	    {/* Data[5].ctlEdges[0].bChannel*/FREQ2FBIN(5190, 0),
	    /* Data[5].ctlEdges[1].bChannel*/FREQ2FBIN(5270, 0),
	    /* Data[5].ctlEdges[2].bChannel*/FREQ2FBIN(5310, 0),
	    /* Data[5].ctlEdges[3].bChannel*/FREQ2FBIN(5510, 0),
	    /* Data[5].ctlEdges[4].bChannel*/FREQ2FBIN(5590, 0),
	    /* Data[5].ctlEdges[5].bChannel*/FREQ2FBIN(5670, 0),
	    /* Data[5].ctlEdges[6].bChannel*/0xFF,
	    /* Data[5].ctlEdges[7].bChannel*/0xFF},

	    {/* Data[6].ctlEdges[0].bChannel*/FREQ2FBIN(5180, 0),
	    /* Data[6].ctlEdges[1].bChannel*/FREQ2FBIN(5200, 0),
	    /* Data[6].ctlEdges[2].bChannel*/FREQ2FBIN(5220, 0),
	    /* Data[6].ctlEdges[3].bChannel*/FREQ2FBIN(5260, 0),
	    /* Data[6].ctlEdges[4].bChannel*/FREQ2FBIN(5500, 0),
	    /* Data[6].ctlEdges[5].bChannel*/FREQ2FBIN(5600, 0),
	    /* Data[6].ctlEdges[6].bChannel*/FREQ2FBIN(5700, 0),
	    /* Data[6].ctlEdges[7].bChannel*/FREQ2FBIN(5745, 0)},

	    {/* Data[7].ctlEdges[0].bChannel*/FREQ2FBIN(5180, 0),
	    /* Data[7].ctlEdges[1].bChannel*/FREQ2FBIN(5260, 0),
	    /* Data[7].ctlEdges[2].bChannel*/FREQ2FBIN(5320, 0),
	    /* Data[7].ctlEdges[3].bChannel*/FREQ2FBIN(5500, 0),
	    /* Data[7].ctlEdges[4].bChannel*/FREQ2FBIN(5560, 0),
	    /* Data[7].ctlEdges[5].bChannel*/FREQ2FBIN(5700, 0),
	    /* Data[7].ctlEdges[6].bChannel*/FREQ2FBIN(5745, 0),
	    /* Data[7].ctlEdges[7].bChannel*/FREQ2FBIN(5825, 0)},

	    {/* Data[8].ctlEdges[0].bChannel*/FREQ2FBIN(5190, 0),
	    /* Data[8].ctlEdges[1].bChannel*/FREQ2FBIN(5230, 0),
	    /* Data[8].ctlEdges[2].bChannel*/FREQ2FBIN(5270, 0),
	    /* Data[8].ctlEdges[3].bChannel*/FREQ2FBIN(5510, 0),
	    /* Data[8].ctlEdges[4].bChannel*/FREQ2FBIN(5550, 0),
	    /* Data[8].ctlEdges[5].bChannel*/FREQ2FBIN(5670, 0),
	    /* Data[8].ctlEdges[6].bChannel*/FREQ2FBIN(5755, 0),
	    /* Data[8].ctlEdges[7].bChannel*/FREQ2FBIN(5795, 0)}
	},

//static    OSP_CAL_CTL_DATA_5G   ctlData_5G[OSPREY_NUM_CTLS_5G]=

#if AH_BYTE_ORDER == AH_BIG_ENDIAN
	{
	    {{{1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60}}},

	    {{{1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60}}},

	    {{{0, 60},
	      {1, 60},
	      {0, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60}}},
	    
	    {{{0, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60},
	      {1, 60},
	      {0, 60},
	      {0, 60},
	      {0, 60}}},

	    {{{1, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60},
	      {0, 60},
	      {0, 60},
	      {0, 60},
	      {0, 60}}},

	    {{{1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60},
	      {0, 60},
	      {0, 60}}},

	    {{{1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60}}},

	    {{{1, 60},
	      {1, 60},
	      {0, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60}}},

	    {{{1, 60},
	      {0, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {1, 60},
	      {0, 60},
	      {1, 60}}},
	}
#else
	{
	    {{{60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 0}}},

	    {{{60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 0}}},

	    {{{60, 0},
	      {60, 1},
	      {60, 0},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1}}},
	    
	    {{{60, 0},
	      {60, 1},
	      {60, 1},
	      {60, 0},
	      {60, 1},
	      {60, 0},
	      {60, 0},
	      {60, 0}}},

	    {{{60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 0},
	      {60, 0},
	      {60, 0},
	      {60, 0},
	      {60, 0}}},

	    {{{60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 0},
	      {60, 0},
	      {60, 0}}},

	    {{{60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1}}},

	    {{{60, 1},
	      {60, 1},
	      {60, 0},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 0}}},

	    {{{60, 1},
	      {60, 0},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 1},
	      {60, 0},
	      {60, 1}}},
	}
#endif
};

#endif
