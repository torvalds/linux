#if !defined(_CMD_CAL_PARMS_H)
#define _CMD_CAL_PARMS_H

// ---------------------------
// Important: There can ONLY be one typedef struct per each cmd parameter structure
//    Parser is not intelligent enough and needs this guarantee.
//

#include "whalExtensionGeneric.h"

//#define MAX_UTF_CAL_WRITE_REG 16

// to read ((writeMode & mask) >> shift)
// to write ((writeMode << shift) & mask)
#define ADDRMODE_ADDR_MASK        0x000FFFFF
#define ADDRMODE_ADDR_SHIFT       0
#define ADDRMODE_MODAL_MASK       0x00700000  // 11g LEG/HT20, 11gHT40, 11a LEG/HT20, 11aHT40
#define ADDRMODE_MODAL_SHIFT      20
#define ADDRMODE_MODE_MASK        0x00800000  // RMW, WRITE
#define ADDRMODE_MODE_SHIFT       23
#define ADDRMODE_FUTURE_MASK      0xFF000000  
#define ADDRMODE_FUTURE_SHIFT     24

typedef enum {
     _UTF_NO_MODAL =0,
     _UTF_11G_LEG_HT20,
     _UTF_11G_HT40,
     _UTF_11A_LEG_HT20,
     _UTF_11A_HT40,
     _UTF_11G,  // both HT20 and HT40
     _UTF_11A,  // both HT20 and HT40
} _UTF_REG_MODE;

#define _UTF_RMW    0
#define _UTF_WRITE  1

typedef enum {
    _calParm_code_addrMode0=0, 
    _calParm_code_addrMode1,   
    _calParm_code_addrMode2,   
    _calParm_code_addrMode3,   
    _calParm_code_addrMode4,   
    _calParm_code_addrMode5,   
    _calParm_code_addrMode6,   
    _calParm_code_addrMode7,   
    _calParm_code_addrMode8,   
    _calParm_code_addrMode9,   
    _calParm_code_addrMode10,
    _calParm_code_addrMode11,
    _calParm_code_addrMode12,
    _calParm_code_addrMode13,
    _calParm_code_addrMode14,
    _calParm_code_addrMode15,
    _calParm_code_value0,    
    _calParm_code_value1,    
    _calParm_code_value2,    
    _calParm_code_value3,    
    _calParm_code_value4,    
    _calParm_code_value5,    
    _calParm_code_value6,    
    _calParm_code_value7,    
    _calParm_code_value8,    
    _calParm_code_value9,    
    _calParm_code_value10,   
    _calParm_code_value11,   
    _calParm_code_value12,   
    _calParm_code_value13,   
    _calParm_code_value14,   
    _calParm_code_value15,   
} CMD_CAL_PARMS_CODE;


typedef struct _calParm {
    A_UINT32  addrMode[MAX_UTF_CAL_WRITE_REG/*16*/];
    A_UINT32  value[MAX_UTF_CAL_WRITE_REG/*16*/];
    A_UINT32  mask[MAX_UTF_CAL_WRITE_REG/*16*/];
} __ATTRIB_PACK _CMD_CAL_PARMS;

extern _PARM_BIN_TEMPLATE _calParm_bin_template[];

#endif // #if !defined(_CMD_CAL_PARMS_H)

