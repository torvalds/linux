#if !defined(_TEST_CMD_STREAM_H)
#define _TEST_CMD_STREAM_H

#include "cmdOpcodes.h"

// test cmd sequence stream: version(1B) header(1B) length(2B) checksum(2B) headerDependentValue(2B) headerExtended(4B) cmdPayload(variable, 4xnB) 

// header
#define REPEAT_TEST_SHIFT 0x0
#define REPEAT_TEST_MASK  0x00FF
#define FAIL_RETRY_SHIFT  8
#define FAIL_RETRY_MASK   0x0F00
#define TIMEOUT_RETRY_SHIFT  12
#define TIMEOUT_RETRY_MASK   0xF000
#define CMD_STREAM_HEADER_REPEAT_TEST           0x00000001  //  headerDepValue lower B: value=0-> infinite, non-zero val is the repeated count
#define CMD_STREAM_HEADER_FAIL_RETRY            0x00000002  // headerDepValue higher B lower nible: value=0->beginning    , non-zero, value is the back steps
#define CMD_STREAM_HEADER_TIMEOUT_RETRY         0x00000004  // headerDepValue higher B higher nible: value=0->beginning    , non-zero, value is the back steps
#define CMD_STREAM_HEADER_CAL_W_TGTPWR          0x00000008  // header bit 0 -> tx gain CAL, 1 -> tgt power CAL  
#define CMD_STERAM_HEADER_EXTENDED_RESERVED     0x00000080

// headerDepValue

// header extended value
#define CMD_STREAM_HEADER_EXTENDED_BYTESTREAM_BYPASS     0x00000001 

// others
#define TEST_FAIL_RETRY_STEPS                 2

// version
#define CMD_STREAM_VER1                       1     
#define CMD_STREAM_VER_LATEST                 CMD_STREAM_VER1                     

// cmd payload
#define CMD_PAYLOAD_LEN_MAX                   8 * 1024
#define PARM_BUF_LEN_MAX                      CMD_PAYLOAD_LEN_MAX - 2 -2   // 4B boundary

// one parm -----------
// parm structure: parmType(4b)|parmCode(12b) parmReserved(2B) parmValue(4B)
#define MAX_NUM_VALUES  ATH_MAC_LEN

typedef enum {
    _PARM_RESERVED = 0,
    _PARM_U8,
    _PARM_U16,
    _PARM_U32,
    _PARM_S8,
    _PARM_S16,
    _PARM_S32,
} _PARM_TYPE;

#if defined(WIN32) || defined(WIN64)
#pragma pack (push, 1)
#endif
typedef struct _parmVal {
    A_UINT16    val16;
    A_UINT32    val32;
} __ATTRIB_PACK _PARM_VAL;
#if defined(WIN32) || defined(WIN64)
#pragma pack (pop)
#endif	

typedef struct _parmOneOf {
    A_UINT8     parmCode;
    A_UINT8     parmType;
    union {
        A_UINT8 addr[MAX_NUM_VALUES];
        _PARM_VAL value;
    } parmValue;
} __ATTRIB_PACK  _PARM_ONEOF;
#define PARM_ONEOF_SIZE                       sizeof(struct _parmOneOf)
#define PARM_NUM_MAX            ((PARM_BUF_LEN_MAX) / (PARM_ONEOF_SIZE))

// one cmd -----------
// command structure: cmdOpcode(1B) numOfParms(1B) parm1(8B) ... parmN(8B)
// refer to "cmdOpcodes.h"
typedef struct _oneCmdHeader {
    A_UINT8     opCode;
    A_UINT8     numOfParms;
    A_UINT16    headerReserved;
} __ATTRIB_PACK _ONE_CMD_HEADER;

typedef struct _cmdOneOf {
    _ONE_CMD_HEADER  cmdHeader;
    union {
        A_UINT8      parmBuf[PARM_BUF_LEN_MAX];
        _PARM_ONEOF  parm[PARM_NUM_MAX];
    }                parms;
} __ATTRIB_PACK  _CMD_ONEOF;
#define CMD_ONOF_SIZE  sizeof(struct _cmdOnOf)

// test flow cmd stream ------------ 
// test cmd sequence stream: version(1B) header(1B) length(2B) checksum(2B) headerDependentValue(2B) headerExtended(4B) cmdPayload(variable, 4xnB) 
typedef struct _testFlowCmdStreamHeader {
    A_UINT8     version;
    A_UINT8     header;
    A_UINT16    length; // payload length
    A_UINT16    checkSum;
    A_UINT16    headerDepValue;
    A_UINT32    headerExtended;
} __ATTRIB_PACK  _TESTFLOW_CMD_STREAM_HEADER; 

typedef struct _testFlowCmdStream {
    _TESTFLOW_CMD_STREAM_HEADER cmdStreamHeader;
    A_UINT8                     payload[CMD_PAYLOAD_LEN_MAX];
} __ATTRIB_PACK  _TESTFLOW_CMD_STREAM;

#endif // #if !defined(_TEST_CMD_STREAM_H)

