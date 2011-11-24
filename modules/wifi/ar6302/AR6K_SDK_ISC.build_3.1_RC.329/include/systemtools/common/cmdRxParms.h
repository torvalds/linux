#if !defined(_CMD_RX_PARM_TEMPLATE)
#define _CMD_RX_PARM_TEMPLATE

// ---------------------------
// Important: There can ONLY be one typedef struct per each cmd parameter structure
//    Parser is not intelligent enough and needs this guarantee.
//

//#include "parmBinTemplate.h"

typedef enum {
    _rxParm_code_channel=0,
    _rxParm_code_act,
    _rxParm_code_enANI,
    _rxParm_code_antenna,
    _rxParm_code_wlanMode,
    _rxParm_expectedPkts,
    _rxParm_code_totalPkt,
    _rxParm_code_rssiInDBm,
    _rxParm_code_crcErrPkt,
    _rxParm_code_secErrPkt,
    _rxParm_code_antswitch1,
    _rxParm_code_antswitch2,
    _rxParm_code_addr,
} CMD_RX_PARMS_CODE;

#if defined(_HOST_SIM_TESTING)
#define ATH_MAC_LEN  6
#endif
typedef struct _rxParm {
    A_UINT32  channel;
    A_UINT32  rxMode;// A_UINT32  act;
    A_UINT32  enANI;
    A_UINT32  antenna;
    A_UINT32  wlanMode;
    A_UINT32  expectedPkts;
    A_UINT32  totalPkt;
    A_INT32   rssiInDBm;
    A_UINT32  crcErrPkt;
    A_UINT32  secErrPkt;
    A_UINT32  antswitch1;
    A_UINT32  antswitch2;
    A_UCHAR   addr[ATH_MAC_LEN/*6*/];  
} __ATTRIB_PACK _CMD_RX_PARMS;


extern _PARM_BIN_TEMPLATE _rxParm_bin_template[]; 

#endif // #if !defined(_CMD_RX_PARM_TEMPLATE)

