#if !defined(_CMDOPCODES_H)
#define _CMDOPCODES_H


// command structure: cmdOpcode(1B) cmdNumParms(1B) parm1(6B)
//
// command opcodes
typedef enum {
    _OP_TEST_CONFIG = 255,
    _OP_SYNC = 0,               // 0
    _OP_TX,                     // 1
    _OP_RX,                     // 2
    _OP_CAL,                    // 3
    _OP_CAL_DONE,               // 4
    _OP_TESTSCRIPT_LAST,        // 5, basically the size of testscript I/F cmds

    _OP_REMAINING = _OP_TESTSCRIPT_LAST,
    _OP_SLEEP,
    _OP_RESET,
    _OP_OTP,
    _OP_EEPROM,
    _OP_READ_REG,
    _OP_WRITE_REG,
    _OP_READ_MEM,
    _OP_WRITE_MEM,    

    _OP_MAX,
} _CMD_OPCODES;


#endif // #if !defined(_CMDOPCODES_H)
