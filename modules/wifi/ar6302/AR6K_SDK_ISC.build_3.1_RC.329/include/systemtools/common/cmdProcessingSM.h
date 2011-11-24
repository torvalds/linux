#if !defined(_CMDPROCESSINGSM_H)
#define  _CMDPROCESSINGSM_H 

#if defined(_HOST_SIM_TESTING)
#define A_TIMER  int
#endif //#if defined(_HOST_SIM_TESTING)

#include "cmdOpcodes.h"

#define RX_SYNC_CMD_TIMEOUT_MS   5000
#define RX_CMD_TIMEOUT_MS        10000
#define CAL_DATA_POLL_PERIOD_MS  90000   /// will be long for real build

typedef enum {
    CMDPROC_RC_NULL=0,
    CMDPROC_RC_SYNC_SUCC,
    CMDPROC_RC_SYNC_FAIL,
    CMDPROC_RC_ASYNC,
    CMDPROC_RC_ASYNC_TIMEOUT,
    CMDPROC_RC_ASYNC_SUCC,
    CMDPROC_RC_ASYNC_FAIL,
} _CMDPROC_RC;

typedef enum {                         // STATE :
    CMD_PROCESSING_STATE = 0, //0 
    RX_SYNC_STATE,            //1
    TX_STATE,                 //2       
    RX_STATE,                 //3              
    CAL_OR_CHAR_STATE,        //4             
    WAIT_4_CAL_DATA_STATE,    //5            
    SAME_STATE,               // 6: size, SAME_STATE doesn't have an entry in the STATE table               

    NULL_STATE = SAME_STATE,   // also functions as the size of the STATE table
    LAST_STATE = NULL_STATE, 
    STATE_MAX  = LAST_STATE,
} _CMDPROC_STATE;

//#define MAX_EVT_HANDLED  5
typedef enum {              // EVENT
    EVT_ENTRY_CMDPROC =0,      //0

    EVT_RX_SYNC_TIMEOUT,       //1
    EVT_RX_TIMEOUT,            //2            // different handling than RX SYNC
    EVT_POLL_CAL_DATA_TIMEOUT, //3            // could be long

    EVT_CMD_DONE,              //4
    EVT_CMD_FAILURE,           //5
    EVT_CMD_TIMEOUT,           //6

    EVT_RX_SYNC_AVAIL,         //7
    EVT_RX_AVAIL,              //8

    EVT_CAL_DATA_AVAIL,        //9

    EVT_RX_SYNC_CMD,           //10
    EVT_TX_CMD,                //11
    EVT_RX_CMD,                //12
    EVT_CAL_CMD,               //13
    EVT_CAL_DONE_CMD,          //14
 
    EVT_NULL,                  //15 
    EVT_CMDPROC_LAST_EVENT = EVT_NULL,    //15
    CMDPROC_EVT_MAX = EVT_CMDPROC_LAST_EVENT,

} _CMDPROC_EVENT;

typedef enum {
    REPEAT_TEST_NOT = 0,
    REPEAT_TEST_FINITE,
    REPEAT_TEST_FOREVER,
} _REPEAT_TESTING;

typedef enum {
    FAIL_RETRY_FROM_BEGINNING=0,
    FAIL_RETRY_BACK_N_STEPS,
    FAIL_RETRY_NOT,
} _FAIL_RETRY;

#define MAX_CMD_FAILURE   255
#define MAX_CMD_TIMEOUT   255

#define TEST_CONFIG_CAL_W_TGTPWR    0x00000001

typedef struct _utfCmdprocessStateInfo {
    _CMDPROC_STATE       state;
    _CMD_OPCODES         curCmd;
    _CMDPROC_EVENT       curEvt;
    _CMD_OPCODES         nextCmd;

    A_TIMER              rxSyncTimer;
    A_TIMER              rxTimer;
    A_TIMER              calDataPollTimer;

    _REPEAT_TESTING      repeatTesting;
    A_UINT16             repeatNum;
    A_UINT16             repeatCount;

    _FAIL_RETRY          failRetry;
    A_UINT8              failRetrySteps;
    A_UINT8              failRetryCount;
    A_UINT16             paddingFail;

    _FAIL_RETRY          timeoutRetry;
    A_UINT8              timeoutRetrySteps;
    A_UINT8              timeoutRetryCount;
    A_UINT16             paddingTimeout;

    _PARSED_BIN_CMD_STREAM_INFO *pCmdStreamInfo;

    A_UINT32             curEvtIdx;

    A_UINT32             testConfig;

} __ATTRIB_PACK _CMDPROC_STATE_INFO;

typedef struct _utfEvtSMTable {
    _CMDPROC_EVENT    evtHandled;
    _CMDPROC_RC       (*pCurStateProcessing)(_CMDPROC_STATE_INFO *);
    _CMDPROC_STATE    nextState;
} _UTF_EVT_SM_TABLE;
typedef struct _utfCmdprocessSMTable {
     A_UINT32           numEvt;
    _UTF_EVT_SM_TABLE   evtTbl[CMDPROC_EVT_MAX];
} _UTF_CMDPROCESS_SM_TABLE;

typedef enum {
    _CMDPROC_NULL = 0,
    _CMDPROC_CONT,
    _CMDPROC_END,
    _CMDPROC_RESET,
} _CMDPROC_DECISION;

typedef struct _mapOpcode2StateEvt {
    _CMDPROC_STATE state;
    _CMDPROC_EVENT evt;
} _OP_2_STATE_EVT;


#if 0
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
#endif

#define ATH_RATE_BASE    ATH_RATE_1M     // corresponding to pwr base

// Rx notify callback
typedef struct _rxNotifyInfo {
    A_UINT32           expectedPkts;
    A_UINT32           lastSnapshotPkts;
    _CMDPROC_EVENT     evt2Notify;
    A_UINT32           shouldHaveNotifiedTimes;
} _RX_NOTIFY_INFO;

typedef struct _txNotifyInfo {
    A_UINT32           numPkts2Send;
} _TX_NOTIFY_INFO;
#define RX_SYNC_EXPECTED_NUM_PACKETS   1

typedef struct _cmdHandlers {
    _CMDPROC_RC (*_CmdProcessor)(_CMD_ALL_PARMS *parms);
    A_BOOL (*_CmdParmsParser)(A_UINT8 * cmdParmBuf, _CMD_ALL_PARMS **parms, A_UINT8 numOfParms);
} __ATTRIB_PACK _CMD_HANDLERS;

extern _CMD_HANDLERS CmdHandlers[];
extern _OP_2_STATE_EVT Op2StateEvt[];

_CMDPROC_RC cmdProcessingSM(_CMDPROC_STATE_INFO *stateInfo);
extern void cmdProcessingThread(_CMDPROC_STATE_INFO *pCmdStateInfo);
//extern void rxSyncNotify(void *pInput, void *pReturn);
extern void rxNotify(void *pInput, void *pReturn);
extern void txNotify(void *pInput, void *pReturn);
extern void rxNotifyReg(void *pInput);
extern void createTimers(void);

extern _UTF_CMDPROCESS_SM_TABLE CmdProcSM[STATE_MAX]; 
extern _CMDPROC_RC evtCmdHandler(_CMDPROC_STATE_INFO *pCmdStateInfo);

#endif //#if !defined(_CMDPROCESSINGSM_H)

