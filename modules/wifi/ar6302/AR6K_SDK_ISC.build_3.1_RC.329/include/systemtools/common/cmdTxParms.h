#if !defined(_CMD_TX_PARMS_H)
#define _CMD_TX_PARMS_H

// ---------------------------
// Important: There can ONLY be one typedef struct per each cmd parameter structure
//    Parser is not intelligent enough and needs this guarantee.
//

//#include "parmBinTemplate.h"


// pwrGainType
#define PWRGAINTYPE_TGTPWR       0x1
#define PWRGAINTYPE_FORCED_PWR   0x2
#define PWRGAINTYPE_FORCED_GAIN  0x4
#define PWRGAINTYPE_RESERVED     0x8
#define PWRGAINTYPE_SHIFT        28
#define PWRGAINTYPE_MASK         0xF

// pwrGainBase (Low and High), outdated, now 1B each with -127 to 127dBm, after considering sweeping power
#define PWRGAINBASE_SHIFT        28
#define PWRGAINBASE_MASK         0xF
#define PWRGAIN_COMBINE(low,high)    ((((high) << 8) & 0xF0) | ((low) & 0x0F))

// pwrGain: pwr 0.5dB step, gain(PCDAC) 1=0.5dB

// 
// Map data rate to power/gain
//
#define RATE_MASK_ROW_MAX    2
#define RATE_MASK_BIT_MAX    32
#define PWRGAIN_ROW_MAX      ((RATE_MASK_ROW_MAX * RATE_MASK_BIT_MAX) / 4)
#define PWRGAIN_MASK                  0xFF
#define PWRGAIN_MASK_SIGN_BIT_MASK    0x80  
#define NEGATE(x) (((x) & (PWRGAIN_MASK_SIGN_BIT_MASK)) ? ((x) = 128 - (x)) : (x))
#define PWRGAIN_PER_PWRGAIN_ROW      4
#define PWRGAIN_PER_MASK_ROW         8

typedef enum {
    _txParm_code_channel=0,
    _txParm_code_txMode,
    _txParm_code_rateMask0,
    _txParm_code_rateMask1,
    _txParm_code_pwrGainStart0,   // cck 
    _txParm_code_pwrGainStart1,   // ofdm
    _txParm_code_pwrGainStart2,   // ht20-1
    _txParm_code_pwrGainStart3,   // ht40-1
    _txParm_code_pwrGainStart4,   // ht20-2
    _txParm_code_pwrGainStart5,   // ht40-2
    _txParm_code_pwrGainStart6,   // ht40-2
    _txParm_code_pwrGainStart7,   // ht40-2
    _txParm_code_pwrGainStart8,   // ht40-2
    _txParm_code_pwrGainStart9,   // ht40-2
    _txParm_code_pwrGainStart10,   // ht40-2
    _txParm_code_pwrGainStart11,   // ht40-2
    _txParm_code_pwrGainStart12,   // ht40-2
    _txParm_code_pwrGainStart13,   // ht40-2
    _txParm_code_pwrGainStart14,   // ht40-2
    _txParm_code_pwrGainStart15,   // ht40-2
    _txParm_code_pwrGainEnd0,     // cck 
    _txParm_code_pwrGainEnd1,     // ofdm
    _txParm_code_pwrGainEnd2,     // ht20-1
    _txParm_code_pwrGainEnd3,     // ht40-1
    _txParm_code_pwrGainEnd4,     // ht20-2
    _txParm_code_pwrGainEnd5,     // ht40-2
    _txParm_code_pwrGainEnd6,     // ht40-2
    _txParm_code_pwrGainEnd7,     // ht40-2
    _txParm_code_pwrGainEnd8,     // ht40-2
    _txParm_code_pwrGainEnd9,     // ht40-2
    _txParm_code_pwrGainEnd10,     // ht40-2
    _txParm_code_pwrGainEnd11,     // ht40-2
    _txParm_code_pwrGainEnd12,     // ht40-2
    _txParm_code_pwrGainEnd13,     // ht40-2
    _txParm_code_pwrGainEnd14,     // ht40-2
    _txParm_code_pwrGainEnd15,     // ht40-2
    _txParm_code_pwrGainStep0,    // cck 
    _txParm_code_pwrGainStep1,    // ofdm
    _txParm_code_pwrGainStep2,    // ht20-1
    _txParm_code_pwrGainStep3,    // ht40-1
    _txParm_code_pwrGainStep4,    // ht20-2
    _txParm_code_pwrGainStep5,    // ht40-2
    _txParm_code_pwrGainStep6,    // ht40-2
    _txParm_code_pwrGainStep7,    // ht40-2
    _txParm_code_pwrGainStep8,    // ht40-2
    _txParm_code_pwrGainStep9,    // ht40-2
    _txParm_code_pwrGainStep10,    // ht40-2
    _txParm_code_pwrGainStep11,    // ht40-2
    _txParm_code_pwrGainStep12,    // ht40-2
    _txParm_code_pwrGainStep13,    // ht40-2
    _txParm_code_pwrGainStep14,    // ht40-2
    _txParm_code_pwrGainStep15,    // ht40-2
    _txParm_code_antenna,
    _txParm_code_enANI,
    _txParm_code_scramblerOff,
    _txParm_code_aifsn,
    _txParm_code_pktSz,
    _txParm_code_txPattern,
    _txParm_code_shortGuard,
    _txParm_code_numPackets,
    _txParm_code_wlanMode,
} CMD_TX_PARMS_CODE;


typedef struct _txParm {
    A_UINT32  channel;
    A_UINT32  txMode;
    A_UINT32  rateMask[RATE_MASK_ROW_MAX/*2*/];
    A_INT32   pwrGainStart[PWRGAIN_ROW_MAX/*16*/];  
    A_INT32   pwrGainEnd[PWRGAIN_ROW_MAX/*16*/];
    A_INT32   pwrGainStep[PWRGAIN_ROW_MAX/*16*/];
    A_UINT32  antenna;
    A_UINT32  enANI;
    A_UINT32  scramblerOff;
    A_UINT32  aifsn;
    A_UINT32  pktSz;
    A_UINT32  txPattern;
    A_UINT32  shortGuard;
    A_UINT32  numPackets;
    A_UINT32  wlanMode;
} __ATTRIB_PACK _CMD_TX_PARMS;

extern _PARM_BIN_TEMPLATE _txParm_bin_template[];

#endif // #if !defined(_CMD_TX_PARMS_H)

