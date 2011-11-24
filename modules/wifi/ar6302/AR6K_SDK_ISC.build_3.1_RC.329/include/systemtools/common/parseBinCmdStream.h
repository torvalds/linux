#if !defined(_PARSE_BIN_CMD_STREAM_H)
#define  _PARSE_BIN_CMD_STREAM_H

#define AVERAGE_NUM_CMD_PARMS   10
#define CMD_QUEUE_AVERAGE  (CMD_PAYLOAD_LEN_MAX / (AVERAGE_NUM_CMD_PARMS * sizeof(_PARM_ONEOF)))
#define CMD_QUEUE_MAX      ((CMD_QUEUE_AVERAGE > 256) ? (256) : (CMD_QUEUE_AVERAGE))

#define CMDPROC_QUEUE_ABSOLUTE_BEGIN 0

typedef struct _cmdQueue {
    A_UINT16  opCode;
    A_UINT16  numOfParms;
    A_UINT8   *cmdParmBuf;
} __ATTRIB_PACK _CMD_QUEUE;
    
typedef struct _parsedBinCmdStreamInfo {
    A_UINT16     header;
    A_UINT16     numOfCmds;
    A_UINT16     headerDepValue;
    A_UINT16     padding;
    A_UINT32     headerExtended;
    A_UINT16     curQBegin;
    A_UINT16     curQEnd;
    A_UINT16     doneQBegin;
    A_UINT16     doneQEnd;
    _CMD_QUEUE   curCmdQueue[CMD_QUEUE_MAX];
    _CMD_QUEUE   doneCmdQueue[CMD_QUEUE_MAX];
} __ATTRIB_PACK _PARSED_BIN_CMD_STREAM_INFO;


extern _PARSED_BIN_CMD_STREAM_INFO CmdStreamInfo;
extern A_BOOL parseBinCmdStream(A_UINT8 *stream, A_UINT32 readStreamLen, A_UINT8 **pPayload, A_UINT16 *payloadLen); 

#endif // #if !defined(_PARSE_BIN_CMD_STREAM_H)

