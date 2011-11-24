#if !defined(_WHAL_EXTENSION_GENERIC_H)
#define  _WHAL_EXTENSION_GENERIC_H

// defines
#define MAX_UTF_CAL_WRITE_REG 16
#define TX_PWR_DEFAULT        50   // 10dBm 

#if 0
#define whalSetTxPowerLimitPlus(pwrGain) \
    if (_PostWhalResetSettings.pPostSetPwrProc) { \
        whalSetTxPowerLimit(TX_PWR_DEFAULT); \
        (_PostWhalResetSettings.pPostSetPwrProc)(pwrGain); \
        return(TX_PWR_DEFAULT);\
    } \
    else { \
        whalSetTxPowerLimit(pwrGain); \
        return(pwrGain);\
    }
#endif
    
#define whalResetPlus(opmode, chan, resetFlags) \
    whalReset((opmode), (chan), (resetFlags));      \
    if (_PostWhalResetSettings.numOfRegs_AlsoMeantPostProcInstalledIfNonZero) { \
        if (_PostWhalResetSettings.pPostProc) { \
            (_PostWhalResetSettings.pPostProc)(NULL); \
        } \
    } \

extern void tcmd_main(void);
#define utfMain      tcmd_main
#define utfTxCmd     tcmd_cont_tx_cmd 
#define utfRxCmd     tcmd_cont_rx_cmd 
#define _utfRxStat_t tcmd_rx_stat_t

typedef struct _postWhalResetSettings {
    A_UINT32  addrMode[MAX_UTF_CAL_WRITE_REG/*16*/];
    A_UINT32  value[MAX_UTF_CAL_WRITE_REG/*16*/];
    A_UINT32  mask[MAX_UTF_CAL_WRITE_REG/*16*/];
    void      (*pPostProc)(void* pParms);                   // a different func can be installed for sweeping power in bench characterization, e.g.
    A_UINT32  numOfRegs_AlsoMeantPostProcInstalledIfNonZero;
    void      (*pPostSetPwrProc)(A_UINT16 pcDac);                   // a different func can be installed for sweeping power in bench characterization, e.g.
} _POST_WHAL_RESET_SETTINGS;

// Public data
extern _POST_WHAL_RESET_SETTINGS _PostWhalResetSettings;

// Exported APIs
extern void whalResetPostProcessing(void);
extern A_UINT16 whalSetTxPowerLimitPlus(A_UINT16 pwrGain); 

#endif //#if !defined(_WHAL_EXTENSION_GENERIC_H)

