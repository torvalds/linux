..
    **************************************************
    *                                                *
    *   Automatically generated file, do not edit!   *
    *                                                *
    **************************************************

.. _amdgpu_synid_gfx11_msg_e37f7b:

msg
===

A 16-bit message code. The bits of this operand have the following meaning:

    ============ =============================== ===============
    Bits         Description                     Value Range
    ============ =============================== ===============
    3:0          Message *type*.                 0..15
    6:4          Optional *operation*.           0..7
    7:7          Must be 0.                      0
    9:8          Optional *stream*.              0..3
    15:10        Unused.                         \-
    ============ =============================== ===============

This operand may be specified as one of the following:

* An :ref:`integer_number<amdgpu_synid_integer_number>` or an :ref:`absolute_expression<amdgpu_synid_absolute_expression>`. The value must be in the range from 0 to 0xFFFF.
* A *sendmsg* value which is described below.

    ==================================== ====================================================
    Sendmsg Value Syntax                 Description
    ==================================== ====================================================
    sendmsg(<*type*>)                    A message identified by its *type*.
    sendmsg(<*type*>,<*op*>)             A message identified by its *type* and *operation*.
    sendmsg(<*type*>,<*op*>,<*stream*>)  A message identified by its *type* and *operation*
                                         with a stream *id*.
    ==================================== ====================================================

*Type* may be specified using message *name* or message *id*.

*Op* may be specified using operation *name* or operation *id*.

Stream *id* is an integer in the range from 0 to 3.

Numeric values may be specified as positive :ref:`integer numbers<amdgpu_synid_integer_number>`
or :ref:`absolute expressions<amdgpu_synid_absolute_expression>`.

Each message type supports specific operations:

    ====================== ========== ============================== ============ ==========
    Message name           Message Id Supported Operations           Operation Id Stream Id
    ====================== ========== ============================== ============ ==========
    MSG_INTERRUPT          1          \-                             \-           \-
    MSG_HS_TESSFACTOR      2          \-                             \-           \-
    MSG_DEALLOC_VGPRS      3          \-                             \-           \-
    MSG_STALL_WAVE_GEN     5          \-                             \-           \-
    MSG_HALT_WAVES         6          \-                             \-           \-
    MSG_GS_ALLOC_REQ       9          \-                             \-           \-
    MSG_SYSMSG             15         SYSMSG_OP_ECC_ERR_INTERRUPT    1            \-
    \                                 SYSMSG_OP_REG_RD               2            \-
    \                                 SYSMSG_OP_TTRACE_PC            4            \-
    ====================== ========== ============================== ============ ==========

*Sendmsg* arguments are validated depending on how *type* value is specified:

* If message *type* is specified by name, arguments values must satisfy limitations detailed in the table above.
* If message *type* is specified as a number, each argument must not exceed the corresponding value range (see the first table).

Examples:

.. parsed-literal::

    // numeric message code
    msg = 0x10
    s_sendmsg 0x12
    s_sendmsg msg + 2

    // sendmsg with strict arguments validation
    s_sendmsg sendmsg(MSG_INTERRUPT)
    s_sendmsg sendmsg(MSG_SYSMSG, SYSMSG_OP_TTRACE_PC)

    // sendmsg with validation of value range only
    msg = 2
    op = 3
    s_sendmsg sendmsg(msg, op)
