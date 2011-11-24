#if !defined(_CMD_ALL_PARMS_H)
#define  _CMD_ALL_PARMS_H

#include "cmdTxParms.h"
#include "cmdRxParms.h"
#include "cmdCalParms.h"

typedef struct _cmdAllParms {
    
    union {
        _CMD_TX_PARMS  _cmdTxParms; 
        _CMD_RX_PARMS  _cmdRxParms; 
        _CMD_CAL_PARMS _cmdCalParms; 
    } __ATTRIB_PACK _cmdParmU;

} __ATTRIB_PACK _CMD_ALL_PARMS;

#endif //#if !defined(_CMD_ALL_PARMS_H)

