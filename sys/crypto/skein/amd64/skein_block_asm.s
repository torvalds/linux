#
#----------------------------------------------------------------
# 64-bit x86 assembler code (gnu as) for Skein block functions
#
# Author: Doug Whiting, Hifn/Exar
#
# This code is released to the public domain.
#----------------------------------------------------------------
# $FreeBSD$
#
    .text
    .altmacro
    .psize 0,128                            #list file has no page boundaries
#
_MASK_ALL_  =  (256+512+1024)               #all three algorithm bits
_MAX_FRAME_ =  240
#
#################
.ifndef SKEIN_USE_ASM
_USE_ASM_         = _MASK_ALL_
.else
_USE_ASM_         = SKEIN_USE_ASM
.endif
#################
.ifndef SKEIN_LOOP                          #configure loop unrolling
_SKEIN_LOOP       =   2                     #default is fully unrolled for 256/512, twice for 1024
.else
_SKEIN_LOOP       = SKEIN_LOOP
  .irp _NN_,%_SKEIN_LOOP                #only display loop unrolling if default changed on command line
#.print  "+++ SKEIN_LOOP = \_NN_"
  .endr
.endif
# the unroll counts (0 --> fully unrolled)
SKEIN_UNROLL_256  = (_SKEIN_LOOP / 100) % 10
SKEIN_UNROLL_512  = (_SKEIN_LOOP /  10) % 10
SKEIN_UNROLL_1024 = (_SKEIN_LOOP      ) % 10
#
SKEIN_ASM_UNROLL  = 0
  .irp _NN_,256,512,1024
    .if (SKEIN_UNROLL_\_NN_) == 0
SKEIN_ASM_UNROLL  = SKEIN_ASM_UNROLL + \_NN_
    .endif
  .endr
#################
#
.ifndef SKEIN_ROUNDS
ROUNDS_256  =   72
ROUNDS_512  =   72
ROUNDS_1024 =   80
.else
ROUNDS_256  = 8*((((SKEIN_ROUNDS / 100) + 5) % 10) + 5)
ROUNDS_512  = 8*((((SKEIN_ROUNDS /  10) + 5) % 10) + 5)
ROUNDS_1024 = 8*((((SKEIN_ROUNDS      ) + 5) % 10) + 5)
# only display rounds if default size is changed on command line
.irp _NN_,256,512,1024
  .if _USE_ASM_ && \_NN_
    .irp _RR_,%(ROUNDS_\_NN_)
      .if _NN_ < 1024
.print  "+++ SKEIN_ROUNDS_\_NN_  = \_RR_"
      .else
.print  "+++ SKEIN_ROUNDS_\_NN_ = \_RR_"
      .endif
    .endr
  .endif
.endr
.endif
#################
#
.ifdef SKEIN_CODE_SIZE
_SKEIN_CODE_SIZE = (1)
.else
.ifdef  SKEIN_PERF                           #use code size if SKEIN_PERF is defined
_SKEIN_CODE_SIZE = (1)
.else
_SKEIN_CODE_SIZE = (0)
.endif
.endif
#
#################
#
.ifndef SKEIN_DEBUG
_SKEIN_DEBUG      = 0
.else
_SKEIN_DEBUG      = 1
.endif
#################
#
# define offsets of fields in hash context structure
#
HASH_BITS   =   0                   #bits of hash output
BCNT        =   8 + HASH_BITS       #number of bytes in BUFFER[]
TWEAK       =   8 + BCNT            #tweak values[0..1]
X_VARS      =  16 + TWEAK           #chaining vars
#
#(Note: buffer[] in context structure is NOT needed here :-)
#
KW_PARITY   =   0x1BD11BDAA9FC1A22  #overall parity of key schedule words
FIRST_MASK  =   ~ (1 <<  6)
FIRST_MASK64=   ~ (1 << 62)
#
# rotation constants for Skein
#
RC_256_0_0  = 14
RC_256_0_1  = 16

RC_256_1_0  = 52
RC_256_1_1  = 57

RC_256_2_0  = 23
RC_256_2_1  = 40

RC_256_3_0  =  5
RC_256_3_1  = 37

RC_256_4_0  = 25
RC_256_4_1  = 33

RC_256_5_0  = 46
RC_256_5_1  = 12

RC_256_6_0  = 58
RC_256_6_1  = 22

RC_256_7_0  = 32
RC_256_7_1  = 32

RC_512_0_0  = 46
RC_512_0_1  = 36
RC_512_0_2  = 19
RC_512_0_3  = 37

RC_512_1_0  = 33
RC_512_1_1  = 27
RC_512_1_2  = 14
RC_512_1_3  = 42

RC_512_2_0  = 17
RC_512_2_1  = 49
RC_512_2_2  = 36
RC_512_2_3  = 39

RC_512_3_0  = 44
RC_512_3_1  =  9
RC_512_3_2  = 54
RC_512_3_3  = 56

RC_512_4_0  = 39
RC_512_4_1  = 30
RC_512_4_2  = 34
RC_512_4_3  = 24

RC_512_5_0  = 13
RC_512_5_1  = 50
RC_512_5_2  = 10
RC_512_5_3  = 17

RC_512_6_0  = 25
RC_512_6_1  = 29
RC_512_6_2  = 39
RC_512_6_3  = 43

RC_512_7_0  =  8
RC_512_7_1  = 35
RC_512_7_2  = 56
RC_512_7_3  = 22

RC_1024_0_0 = 24
RC_1024_0_1 = 13
RC_1024_0_2 =  8
RC_1024_0_3 = 47
RC_1024_0_4 =  8
RC_1024_0_5 = 17
RC_1024_0_6 = 22
RC_1024_0_7 = 37

RC_1024_1_0 = 38
RC_1024_1_1 = 19
RC_1024_1_2 = 10
RC_1024_1_3 = 55
RC_1024_1_4 = 49
RC_1024_1_5 = 18
RC_1024_1_6 = 23
RC_1024_1_7 = 52

RC_1024_2_0 = 33
RC_1024_2_1 =  4
RC_1024_2_2 = 51
RC_1024_2_3 = 13
RC_1024_2_4 = 34
RC_1024_2_5 = 41
RC_1024_2_6 = 59
RC_1024_2_7 = 17

RC_1024_3_0 =  5
RC_1024_3_1 = 20
RC_1024_3_2 = 48
RC_1024_3_3 = 41
RC_1024_3_4 = 47
RC_1024_3_5 = 28
RC_1024_3_6 = 16
RC_1024_3_7 = 25

RC_1024_4_0 = 41
RC_1024_4_1 =  9
RC_1024_4_2 = 37
RC_1024_4_3 = 31
RC_1024_4_4 = 12
RC_1024_4_5 = 47
RC_1024_4_6 = 44
RC_1024_4_7 = 30

RC_1024_5_0 = 16
RC_1024_5_1 = 34
RC_1024_5_2 = 56
RC_1024_5_3 = 51
RC_1024_5_4 =  4
RC_1024_5_5 = 53
RC_1024_5_6 = 42
RC_1024_5_7 = 41

RC_1024_6_0 = 31
RC_1024_6_1 = 44
RC_1024_6_2 = 47
RC_1024_6_3 = 46
RC_1024_6_4 = 19
RC_1024_6_5 = 42
RC_1024_6_6 = 44
RC_1024_6_7 = 25

RC_1024_7_0 =  9
RC_1024_7_1 = 48
RC_1024_7_2 = 35
RC_1024_7_3 = 52
RC_1024_7_4 = 23
RC_1024_7_5 = 31
RC_1024_7_6 = 37
RC_1024_7_7 = 20
#
#  Input:  reg
# Output: <reg> <<< RC_BlkSize_roundNum_mixNum, BlkSize=256/512/1024
#
.macro RotL64   reg,BLK_SIZE,ROUND_NUM,MIX_NUM
_RCNT_ = RC_\BLK_SIZE&_\ROUND_NUM&_\MIX_NUM
  .if _RCNT_  #is there anything to do?
    rolq    $_RCNT_,%\reg
  .endif
.endm
#
#----------------------------------------------------------------
#
# MACROS: define local vars and configure stack
#
#----------------------------------------------------------------
# declare allocated space on the stack
.macro StackVar localName,localSize
\localName  =   _STK_OFFS_
_STK_OFFS_  =   _STK_OFFS_+(\localSize)
.endm #StackVar
#
#----------------------------------------------------------------
#
# MACRO: Configure stack frame, allocate local vars
#
.macro Setup_Stack BLK_BITS,KS_CNT,debugCnt
    WCNT    =    (\BLK_BITS)/64
#
_PushCnt_   =   0                   #save nonvolatile regs on stack
  .irp _reg_,rbp,rbx,r12,r13,r14,r15
       pushq    %\_reg_
_PushCnt_ = _PushCnt_ + 1           #track count to keep alignment
  .endr
#
_STK_OFFS_  =   0                   #starting offset from rsp
    #---- local  variables         #<-- rsp
    StackVar    X_stk  ,8*(WCNT)    #local context vars
    StackVar    ksTwk  ,8*3         #key schedule: tweak words
    StackVar    ksKey  ,8*(WCNT)+8  #key schedule: key   words
  .if (SKEIN_ASM_UNROLL && (\BLK_BITS)) == 0
    StackVar    ksRot ,16*(\KS_CNT) #leave space for "rotation" to happen
  .endif
    StackVar    Wcopy  ,8*(WCNT)    #copy of input block    
  .if _SKEIN_DEBUG
  .if \debugCnt + 0                 #temp location for debug X[] info
    StackVar    xDebug_\BLK_BITS ,8*(\debugCnt)
  .endif
  .endif
  .if ((8*_PushCnt_ + _STK_OFFS_) % 8) == 0
    StackVar    align16,8           #keep 16-byte aligned (adjust for retAddr?)
tmpStk_\BLK_BITS = align16          #use this
  .endif
    #---- saved caller parameters (from regs rdi, rsi, rdx, rcx)
    StackVar    ctxPtr ,8           #context ptr
    StackVar    blkPtr ,8           #pointer to block data
    StackVar    blkCnt ,8           #number of full blocks to process
    StackVar    bitAdd ,8           #bit count to add to tweak
LOCAL_SIZE  =   _STK_OFFS_          #size of "local" vars
    #---- 
    StackVar    savRegs,8*_PushCnt_ #saved registers
    StackVar    retAddr,8           #return address
    #---- caller's stack frame (aligned mod 16)
#
# set up the stack frame pointer (rbp)
#
FRAME_OFFS  =   ksTwk + 128         #allow short (negative) offset to ksTwk, kwKey
  .if FRAME_OFFS > _STK_OFFS_       #keep rbp in the "locals" range
FRAME_OFFS  =      _STK_OFFS_
  .endif
F_O         =   -FRAME_OFFS
#
  #put some useful defines in the .lst file (for grep)
__STK_LCL_SIZE_\BLK_BITS = LOCAL_SIZE
__STK_TOT_SIZE_\BLK_BITS = _STK_OFFS_
__STK_FRM_OFFS_\BLK_BITS = FRAME_OFFS
#
# Notes on stack frame setup:
#   * the most frequently used variable is X_stk[], based at [rsp+0]
#   * the next most used is the key schedule arrays, ksKey and ksTwk
#       so rbp is "centered" there, allowing short offsets to the key 
#       schedule even in 1024-bit Skein case
#   * the Wcopy variables are infrequently accessed, but they have long 
#       offsets from both rsp and rbp only in the 1024-bit case.
#   * all other local vars and calling parameters can be accessed 
#       with short offsets, except in the 1024-bit case
#
    subq    $LOCAL_SIZE,%rsp        #make room for the locals
    leaq    FRAME_OFFS(%rsp),%rbp   #maximize use of short offsets
    movq    %rdi, ctxPtr+F_O(%rbp)  #save caller's parameters on the stack
    movq    %rsi, blkPtr+F_O(%rbp)
    movq    %rdx, blkCnt+F_O(%rbp)
    movq    %rcx, bitAdd+F_O(%rbp)
#
.endm #Setup_Stack
#
#----------------------------------------------------------------
#
.macro Reset_Stack
    addq    $LOCAL_SIZE,%rsp        #get rid of locals (wipe??)
  .irp _reg_,r15,r14,r13,r12,rbx,rbp
    popq    %\_reg_                 #restore caller's regs
_PushCnt_ = _PushCnt_ - 1
  .endr
  .if _PushCnt_
    .error  "Mismatched push/pops?"
  .endif
.endm # Reset_Stack
#
#----------------------------------------------------------------
# macros to help debug internals
#
.if _SKEIN_DEBUG
    .extern  Skein_Show_Block     #calls to C routines
    .extern  Skein_Show_Round
#
SKEIN_RND_SPECIAL       =   1000
SKEIN_RND_KEY_INITIAL   =   SKEIN_RND_SPECIAL+0
SKEIN_RND_KEY_INJECT    =   SKEIN_RND_SPECIAL+1
SKEIN_RND_FEED_FWD      =   SKEIN_RND_SPECIAL+2
#
.macro Skein_Debug_Block BLK_BITS
#
#void Skein_Show_Block(uint_t bits,const Skein_Ctxt_Hdr_t *h,const u64b_t *X,
#                     const u08b_t *blkPtr, const u64b_t *wPtr, 
#                     const u64b_t *ksPtr,const u64b_t *tsPtr)
#
_NN_ = 0
  .irp _reg_,rax,rcx,rdx,rsi,rdi,r8,r9,r10,r11
    pushq   %\_reg_                 #save all volatile regs on tack before the call
_NN_ = _NN_ + 1
  .endr
    # get and push call parameters
    movq    $\BLK_BITS      ,%rdi   #bits
    movq    ctxPtr+F_O(%rbp),%rsi   #h (pointer)
    leaq    X_VARS    (%rsi),%rdx   #X (pointer)
    movq    blkPtr+F_O(%rbp),%rcx   #blkPtr
    leaq    Wcopy +F_O(%rbp),%r8    #wPtr
    leaq    ksKey +F_O(%rbp),%r9    #key pointer
    leaq    ksTwk +F_O(%rbp),%rax   #tweak pointer
    pushq   %rax                    #   (pass on the stack)
    call    Skein_Show_Block        #call external debug handler
    addq    $8*1,%rsp               #discard parameters on stack
  .if (_NN_ % 2 ) == 0              #check stack alignment
    .error "Stack misalignment problem in Skein_Debug_Block_\_BLK_BITS"
  .endif
  .irp _reg_,r11,r10,r9,r8,rdi,rsi,rdx,rcx,rax
    popq    %\_reg_                 #restore regs
_NN_ = _NN_ - 1
  .endr
  .if _NN_
    .error "Push/pop mismatch problem in Skein_Debug_Block_\_BLK_BITS"
  .endif
.endm # Skein_Debug_Block
#
# the macro to "call" to debug a round
#
.macro Skein_Debug_Round BLK_BITS,R,RDI_OFFS,afterOp
    # call the appropriate (local) debug "function"
    pushq   %rdx                    #save rdx, so we can use it for round "number"
  .if (SKEIN_ASM_UNROLL && \BLK_BITS) || (\R >= SKEIN_RND_SPECIAL)
    movq    $\R,%rdx
  .else                             #compute round number using edi
_rOffs_ = \RDI_OFFS + 0
   .if \BLK_BITS == 1024
    movq    rIdx_offs+8(%rsp),%rdx  #get rIdx off the stack (adjust for pushq rdx above)
    leaq    1+(((\R)-1) && 3)+_rOffs_(,%rdx,4),%rdx
   .else
    leaq    1+(((\R)-1) && 3)+_rOffs_(,%rdi,4),%rdx
   .endif
  .endif
    call    Skein_Debug_Round_\BLK_BITS
    popq    %rdx                    #restore origianl rdx value
#
    afterOp
.endm  #  Skein_Debug_Round
.else  #------- _SKEIN_DEBUG (dummy macros if debug not enabled)
.macro Skein_Debug_Block BLK_BITS
.endm
#
.macro Skein_Debug_Round BLK_BITS,R,RDI_OFFS,afterOp
.endm
#
.endif # _SKEIN_DEBUG
#
#----------------------------------------------------------------
#
.macro  addReg dstReg,srcReg_A,srcReg_B,useAddOp,immOffs
  .if \immOffs + 0
       leaq    \immOffs(%\srcReg_A\srcReg_B,%\dstReg),%\dstReg
  .elseif ((\useAddOp + 0) == 0)
    .ifndef ASM_NO_LEA  #lea seems to be faster on Core 2 Duo CPUs!
       leaq   (%\srcReg_A\srcReg_B,%\dstReg),%\dstReg
    .else
       addq    %\srcReg_A\srcReg_B,%\dstReg
    .endif
  .else
       addq    %\srcReg_A\srcReg_B,%\dstReg
  .endif
.endm

# keep Intel-style ordering here, to match addReg
.macro  xorReg dstReg,srcReg_A,srcReg_B
        xorq   %\srcReg_A\srcReg_B,%\dstReg
.endm
#
#----------------------------------------------------------------
#
.macro C_label lName
 \lName:        #use both "genders" to work across linkage conventions
_\lName:
    .global  \lName
    .global _\lName
.endm
#
#=================================== Skein_256 =============================================
#
.if _USE_ASM_ & 256
#
# void Skein_256_Process_Block(Skein_256_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t bitcntAdd)#
#
#################
#
# code
#
C_label Skein_256_Process_Block
    Setup_Stack 256,((ROUNDS_256/8)+1)
    movq    TWEAK+8(%rdi),%r14
    jmp     Skein_256_block_loop
    .p2align 4
    # main hash loop for Skein_256
Skein_256_block_loop:
    #
    # general register usage:
    #   RAX..RDX        = X0..X3    
    #   R08..R12        = ks[0..4]
    #   R13..R15        = ts[0..2]
    #   RSP, RBP        = stack/frame pointers
    #   RDI             = round counter or context pointer
    #   RSI             = temp
    #
    movq    TWEAK+0(%rdi)     ,%r13
    addq    bitAdd+F_O(%rbp)  ,%r13  #computed updated tweak value T0
    movq    %r14              ,%r15
    xorq    %r13              ,%r15  #now %r13.%r15 is set as the tweak 

    movq    $KW_PARITY        ,%r12
    movq       X_VARS+ 0(%rdi),%r8
    movq       X_VARS+ 8(%rdi),%r9 
    movq       X_VARS+16(%rdi),%r10
    movq       X_VARS+24(%rdi),%r11
    movq    %r13,TWEAK+0(%rdi)       #save updated tweak value ctx->h.T[0]
    xorq    %r8               ,%r12  #start accumulating overall parity

    movq    blkPtr +F_O(%rbp) ,%rsi  #esi --> input block
    xorq    %r9               ,%r12
    movq     0(%rsi)          ,%rax  #get X[0..3]
    xorq    %r10              ,%r12
    movq     8(%rsi)          ,%rbx
    xorq    %r11              ,%r12
    movq    16(%rsi)          ,%rcx
    movq    24(%rsi)          ,%rdx

    movq    %rax,Wcopy+ 0+F_O(%rbp)  #save copy of input block
    movq    %rbx,Wcopy+ 8+F_O(%rbp)    
    movq    %rcx,Wcopy+16+F_O(%rbp)    
    movq    %rdx,Wcopy+24+F_O(%rbp)    

    addq    %r8 ,%rax                #initial key injection
    addq    %r9 ,%rbx 
    addq    %r10,%rcx
    addq    %r11,%rdx
    addq    %r13,%rbx
    addq    %r14,%rcx

.if _SKEIN_DEBUG
    movq    %r14,TWEAK+ 8(%rdi)      #save updated tweak T[1] (start bit cleared?)
    movq    %r8 ,ksKey+ 0+F_O(%rbp)  #save key schedule on stack for Skein_Debug_Block
    movq    %r9 ,ksKey+ 8+F_O(%rbp)    
    movq    %r10,ksKey+16+F_O(%rbp)    
    movq    %r11,ksKey+24+F_O(%rbp)    
    movq    %r12,ksKey+32+F_O(%rbp)    
                                       
    movq    %r13,ksTwk+ 0+F_O(%rbp)    
    movq    %r14,ksTwk+ 8+F_O(%rbp)    
    movq    %r15,ksTwk+16+F_O(%rbp)    
                                       
    movq    %rax,X_stk + 0(%rsp)     #save X[] on stack for Skein_Debug_Block
    movq    %rbx,X_stk + 8(%rsp)       
    movq    %rcx,X_stk +16(%rsp)       
    movq    %rdx,X_stk +24(%rsp)       

    Skein_Debug_Block 256            #debug dump
    Skein_Debug_Round 256,SKEIN_RND_KEY_INITIAL
.endif
#
.if ((SKEIN_ASM_UNROLL & 256) == 0)
    movq    %r8 ,ksKey+40+F_O(%rbp)  #save key schedule on stack for looping code
    movq    %r9 ,ksKey+ 8+F_O(%rbp)    
    movq    %r10,ksKey+16+F_O(%rbp)    
    movq    %r11,ksKey+24+F_O(%rbp)    
    movq    %r12,ksKey+32+F_O(%rbp)    
                                       
    movq    %r13,ksTwk+24+F_O(%rbp)    
    movq    %r14,ksTwk+ 8+F_O(%rbp)    
    movq    %r15,ksTwk+16+F_O(%rbp)    
.endif
    addq    $WCNT*8,%rsi             #skip the block
    movq    %rsi,blkPtr  +F_O(%rbp)  #update block pointer
    #
    # now the key schedule is computed. Start the rounds
    #
.if SKEIN_ASM_UNROLL & 256
_UNROLL_CNT =   ROUNDS_256/8
.else
_UNROLL_CNT =   SKEIN_UNROLL_256
  .if ((ROUNDS_256/8) % _UNROLL_CNT)
    .error "Invalid SKEIN_UNROLL_256"
  .endif
    xorq    %rdi,%rdi                #rdi = iteration count
Skein_256_round_loop:
.endif
_Rbase_ = 0
.rept _UNROLL_CNT*2
    # all X and ks vars in regs      # (ops to "rotate" ks vars, via mem, if not unrolled)
    # round 4*_RBase_ + 0
    addReg  rax, rbx
    RotL64  rbx, 256,%((4*_Rbase_+0) % 8),0
    addReg  rcx, rdx
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    movq ksKey+8*1+F_O(%rbp,%rdi,8),%r8
                .endif
    xorReg  rbx, rax
    RotL64  rdx, 256,%((4*_Rbase_+0) % 8),1
    xorReg  rdx, rcx
  .if SKEIN_ASM_UNROLL & 256
    .irp _r0_,%( 8+(_Rbase_+3) % 5)
    .irp _r1_,%(13+(_Rbase_+2) % 3)
      leaq   (%r\_r0_,%r\_r1_),%rdi    #precompute key injection value for %rcx
    .endr
    .endr
  .endif
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    movq ksTwk+8*1+F_O(%rbp,%rdi,8),%r13
                .endif
    Skein_Debug_Round 256,%(4*_Rbase_+1)

    # round 4*_Rbase_ + 1
    addReg  rax, rdx
    RotL64  rdx, 256,%((4*_Rbase_+1) % 8),0
    xorReg  rdx, rax
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    movq ksKey+8*2+F_O(%rbp,%rdi,8),%r9
                .endif
    addReg  rcx, rbx
    RotL64  rbx, 256,%((4*_Rbase_+1) % 8),1
    xorReg  rbx, rcx
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    movq ksKey+8*4+F_O(%rbp,%rdi,8),%r11
                .endif
    Skein_Debug_Round 256,%(4*_Rbase_+2)
 .if SKEIN_ASM_UNROLL & 256
    .irp _r0_,%( 8+(_Rbase_+2) % 5)
    .irp _r1_,%(13+(_Rbase_+1) % 3)
      leaq   (%r\_r0_,%r\_r1_),%rsi     #precompute key injection value for %rbx
    .endr
    .endr
 .endif
    # round 4*_Rbase_ + 2
    addReg  rax, rbx
    RotL64  rbx, 256,%((4*_Rbase_+2) % 8),0
    addReg  rcx, rdx
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    movq ksKey+8*3+F_O(%rbp,%rdi,8),%r10
                .endif
    xorReg  rbx, rax
    RotL64  rdx, 256,%((4*_Rbase_+2) % 8),1
    xorReg  rdx, rcx
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    movq %r8,ksKey+8*6+F_O(%rbp,%rdi,8)  #"rotate" the key
                    leaq 1(%r11,%rdi),%r11               #precompute key + tweak
                .endif
    Skein_Debug_Round 256,%(4*_Rbase_+3)
    # round 4*_Rbase_ + 3
    addReg  rax, rdx
    RotL64  rdx, 256,%((4*_Rbase_+3) % 8),0
    addReg  rcx, rbx
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    addq      ksTwk+8*2+F_O(%rbp,%rdi,8),%r10  #precompute key + tweak
                    movq %r13,ksTwk+8*4+F_O(%rbp,%rdi,8)       #"rotate" the tweak
                .endif
    xorReg  rdx, rax
    RotL64  rbx, 256,%((4*_Rbase_+3) % 8),1
    xorReg  rbx, rcx
    Skein_Debug_Round 256,%(4*_Rbase_+4)
                .if (SKEIN_ASM_UNROLL & 256) == 0
                    addReg r9 ,r13           #precompute key+tweak
                .endif
      #inject key schedule words
_Rbase_ = _Rbase_+1
  .if SKEIN_ASM_UNROLL & 256
    addReg    rax,r,%(8+((_Rbase_+0) % 5))
    addReg    rbx,rsi
    addReg    rcx,rdi
    addReg    rdx,r,%(8+((_Rbase_+3) % 5)),,_Rbase_
  .else
    incq      %rdi
    addReg    rax,r8 
    addReg    rcx,r10
    addReg    rbx,r9 
    addReg    rdx,r11
  .endif
    Skein_Debug_Round 256,SKEIN_RND_KEY_INJECT
.endr #rept _UNROLL_CNT
#
.if (SKEIN_ASM_UNROLL & 256) == 0
    cmpq    $2*(ROUNDS_256/8),%rdi
    jb      Skein_256_round_loop
.endif # (SKEIN_ASM_UNROLL & 256) == 0
    movq    ctxPtr +F_O(%rbp),%rdi           #restore rdi --> context

    #----------------------------
    # feedforward:   ctx->X[i] = X[i] ^ w[i], {i=0..3}
    movq    $FIRST_MASK64 ,%r14
    xorq    Wcopy + 0+F_O (%rbp),%rax
    xorq    Wcopy + 8+F_O (%rbp),%rbx
    xorq    Wcopy +16+F_O (%rbp),%rcx
    xorq    Wcopy +24+F_O (%rbp),%rdx
    andq    TWEAK + 8     (%rdi),%r14
    movq    %rax,X_VARS+ 0(%rdi)             #store final result
    movq    %rbx,X_VARS+ 8(%rdi)        
    movq    %rcx,X_VARS+16(%rdi)        
    movq    %rdx,X_VARS+24(%rdi)        

    Skein_Debug_Round 256,SKEIN_RND_FEED_FWD

    # go back for more blocks, if needed
    decq    blkCnt+F_O(%rbp)
    jnz     Skein_256_block_loop
    movq    %r14,TWEAK + 8(%rdi)
    Reset_Stack
    ret
Skein_256_Process_Block_End:

  .if _SKEIN_DEBUG
Skein_Debug_Round_256:               #here with rdx == round "number" from macro
    pushq   %rsi                     #save two regs for BLK_BITS-specific parms
    pushq   %rdi
    movq    24(%rsp),%rdi            #get back original rdx (pushed on stack in macro call) to rdi
    movq    %rax,X_stk+ 0+F_O(%rbp)  #save X[] state on stack so debug routines can access it
    movq    %rbx,X_stk+ 8+F_O(%rbp)  #(use FP_ since rsp has changed!)
    movq    %rcx,X_stk+16+F_O(%rbp)
    movq    %rdi,X_stk+24+F_O(%rbp)

    movq    ctxPtr+F_O(%rbp),%rsi    #ctx_hdr_ptr
    movq    $256,%rdi                #now <rdi,rsi,rdx> are set for the call
    jmp     Skein_Debug_Round_Common
  .endif
#
.if _SKEIN_CODE_SIZE
C_label  Skein_256_Process_Block_CodeSize
    movq    $(Skein_256_Process_Block_End-Skein_256_Process_Block),%rax
    ret
#
C_label Skein_256_Unroll_Cnt
  .if _UNROLL_CNT <> ROUNDS_256/8
    movq    $_UNROLL_CNT,%rax
  .else
    xorq    %rax,%rax
  .endif
    ret
.endif
#
.endif #_USE_ASM_ & 256
#
#=================================== Skein_512 =============================================
#
.if _USE_ASM_ & 512
#
# void Skein_512_Process_Block(Skein_512_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t bitcntAdd)
#
# X[i] == %r[8+i]          #register assignments for X[] values during rounds (i=0..7)
#
#################
# MACRO: one round for 512-bit blocks
#
.macro R_512_OneRound rn0,rn1,rn2,rn3,rn4,rn5,rn6,rn7,_Rn_,op1,op2,op3,op4
#
    addReg      r\rn0, r\rn1
    RotL64      r\rn1, 512,%((_Rn_) % 8),0
    xorReg      r\rn1, r\rn0
            op1
    addReg      r\rn2, r\rn3
    RotL64      r\rn3, 512,%((_Rn_) % 8),1
    xorReg      r\rn3, r\rn2
            op2
    addReg      r\rn4, r\rn5
    RotL64      r\rn5, 512,%((_Rn_) % 8),2
    xorReg      r\rn5, r\rn4
            op3
    addReg      r\rn6, r\rn7
    RotL64      r\rn7, 512,%((_Rn_) % 8),3
    xorReg      r\rn7, r\rn6
            op4
    Skein_Debug_Round 512,%(_Rn_+1),-4
#
.endm #R_512_OneRound
#
#################
# MACRO: eight rounds for 512-bit blocks
#
.macro R_512_FourRounds _RR_    #RR = base round number (0 % 8)
  .if (SKEIN_ASM_UNROLL && 512)
    # here for fully unrolled case.
    _II_ = ((_RR_)/4) + 1       #key injection counter
    R_512_OneRound  8, 9,10,11,12,13,14,15,%((_RR_)+0),<movq ksKey+8*(((_II_)+3) % 9)+F_O(%rbp),%rax>,,<movq ksKey+8*(((_II_)+4) % 9)+F_O(%rbp),%rbx>
    R_512_OneRound 10, 9,12,15,14,13, 8,11,%((_RR_)+1),<movq ksKey+8*(((_II_)+5) % 9)+F_O(%rbp),%rcx>,,<movq ksKey+8*(((_II_)+6) % 9)+F_O(%rbp),%rdx>
    R_512_OneRound 12, 9,14,11, 8,13,10,15,%((_RR_)+2),<movq ksKey+8*(((_II_)+7) % 9)+F_O(%rbp),%rsi>,,<addq ksTwk+8*(((_II_)+0) % 3)+F_O(%rbp),%rcx>
    R_512_OneRound 14, 9, 8,15,10,13,12,11,%((_RR_)+3),<addq ksTwk+8*(((_II_)+1) % 3)+F_O(%rbp),%rdx>,
    # inject the key schedule
    addq    ksKey+8*(((_II_)+0)%9)+F_O(%rbp),%r8
    addReg   r11, rax
    addq    ksKey+8*(((_II_)+1)%9)+F_O(%rbp),%r9
    addReg   r12, rbx
    addq    ksKey+8*(((_II_)+2)%9)+F_O(%rbp),%r10
    addReg   r13, rcx
    addReg   r14, rdx
    addReg   r15, rsi,,,(_II_)
  .else
    # here for looping case                                                    #"rotate" key/tweak schedule (move up on stack)
    incq    %rdi                 #bump key injection counter
    R_512_OneRound  8, 9,10,11,12,13,14,15,%((_RR_)+0),<movq ksKey+8*6+F_O(%rbp,%rdi,8),%rdx>,<movq      ksTwk-8*1+F_O(%rbp,%rdi,8),%rax>,<movq      ksKey-8*1+F_O(%rbp,%rdi,8),%rsi>
    R_512_OneRound 10, 9,12,15,14,13, 8,11,%((_RR_)+1),<movq ksKey+8*5+F_O(%rbp,%rdi,8),%rcx>,<movq %rax,ksTwk+8*2+F_O(%rbp,%rdi,8)     >,<movq %rsi,ksKey+8*8+F_O(%rbp,%rdi,8)>
    R_512_OneRound 12, 9,14,11, 8,13,10,15,%((_RR_)+2),<movq ksKey+8*4+F_O(%rbp,%rdi,8),%rbx>,<addq      ksTwk+8*1+F_O(%rbp,%rdi,8),%rdx>,<movq      ksKey+8*7+F_O(%rbp,%rdi,8),%rsi>    
    R_512_OneRound 14, 9, 8,15,10,13,12,11,%((_RR_)+3),<movq ksKey+8*3+F_O(%rbp,%rdi,8),%rax>,<addq      ksTwk+8*0+F_O(%rbp,%rdi,8),%rcx>
    # inject the key schedule
    addq    ksKey+8*0+F_O(%rbp,%rdi,8),%r8
    addReg   r11, rax
    addReg   r12, rbx
    addq    ksKey+8*1+F_O(%rbp,%rdi,8),%r9
    addReg   r13, rcx
    addReg   r14, rdx
    addq    ksKey+8*2+F_O(%rbp,%rdi,8),%r10
    addReg   r15, rsi
    addReg   r15, rdi              #inject the round number
  .endif

    #show the result of the key injection
    Skein_Debug_Round 512,SKEIN_RND_KEY_INJECT
.endm #R_512_EightRounds
#
#################
# instantiated code
#
C_label Skein_512_Process_Block
    Setup_Stack 512,ROUNDS_512/8
    movq    TWEAK+ 8(%rdi),%rbx
    jmp     Skein_512_block_loop
    .p2align 4
    # main hash loop for Skein_512
Skein_512_block_loop:
    # general register usage:
    #   RAX..RDX       = temps for key schedule pre-loads
    #   R8 ..R15       = X0..X7
    #   RSP, RBP       = stack/frame pointers
    #   RDI            = round counter or context pointer
    #   RSI            = temp
    #
    movq    TWEAK +  0(%rdi),%rax
    addq    bitAdd+F_O(%rbp),%rax     #computed updated tweak value T0
    movq    %rbx,%rcx
    xorq    %rax,%rcx                 #%rax/%rbx/%rcx = tweak schedule
    movq    %rax,TWEAK+ 0    (%rdi)   #save updated tweak value ctx->h.T[0]
    movq    %rax,ksTwk+ 0+F_O(%rbp)
    movq    $KW_PARITY,%rdx
    movq    blkPtr +F_O(%rbp),%rsi    #%rsi --> input block
    movq    %rbx,ksTwk+ 8+F_O(%rbp)
    movq    %rcx,ksTwk+16+F_O(%rbp)
    .irp _Rn_,8,9,10,11,12,13,14,15
      movq  X_VARS+8*(_Rn_-8)(%rdi),%r\_Rn_
      xorq  %r\_Rn_,%rdx              #compute overall parity
      movq  %r\_Rn_,ksKey+8*(_Rn_-8)+F_O(%rbp)
    .endr                             #load state into %r8 ..%r15, compute parity
      movq  %rdx,ksKey+8*(8)+F_O(%rbp)#save key schedule parity

    addReg   r13,rax                  #precompute key injection for tweak
    addReg   r14, rbx
.if _SKEIN_DEBUG
    movq    %rbx,TWEAK+ 8(%rdi)       #save updated tweak value ctx->h.T[1] for Skein_Debug_Block below
.endif
    movq     0(%rsi),%rax             #load input block
    movq     8(%rsi),%rbx 
    movq    16(%rsi),%rcx 
    movq    24(%rsi),%rdx 
    addReg   r8 , rax                 #do initial key injection
    addReg   r9 , rbx
    movq    %rax,Wcopy+ 0+F_O(%rbp)   #keep local copy for feedforward
    movq    %rbx,Wcopy+ 8+F_O(%rbp)
    addReg   r10, rcx
    addReg   r11, rdx
    movq    %rcx,Wcopy+16+F_O(%rbp)
    movq    %rdx,Wcopy+24+F_O(%rbp)

    movq    32(%rsi),%rax
    movq    40(%rsi),%rbx 
    movq    48(%rsi),%rcx 
    movq    56(%rsi),%rdx
    addReg   r12, rax
    addReg   r13, rbx
    addReg   r14, rcx
    addReg   r15, rdx
    movq    %rax,Wcopy+32+F_O(%rbp)    
    movq    %rbx,Wcopy+40+F_O(%rbp)    
    movq    %rcx,Wcopy+48+F_O(%rbp)    
    movq    %rdx,Wcopy+56+F_O(%rbp)    

.if _SKEIN_DEBUG
    .irp _Rn_,8,9,10,11,12,13,14,15   #save values on stack for debug output
      movq  %r\_Rn_,X_stk+8*(_Rn_-8)(%rsp)
    .endr

    Skein_Debug_Block 512             #debug dump
    Skein_Debug_Round 512,SKEIN_RND_KEY_INITIAL
.endif
    addq    $8*WCNT,%rsi              #skip the block
    movq    %rsi,blkPtr+F_O(%rbp)     #update block pointer
    #
    #################
    # now the key schedule is computed. Start the rounds
    #
.if SKEIN_ASM_UNROLL & 512
_UNROLL_CNT =   ROUNDS_512/8
.else
_UNROLL_CNT =   SKEIN_UNROLL_512
  .if ((ROUNDS_512/8) % _UNROLL_CNT)
    .error "Invalid SKEIN_UNROLL_512"
  .endif
    xorq    %rdi,%rdi                 #rdi = round counter
Skein_512_round_loop:
.endif
#
_Rbase_ = 0
.rept _UNROLL_CNT*2
      R_512_FourRounds %(4*_Rbase_+00)
_Rbase_ = _Rbase_+1
.endr #rept _UNROLL_CNT
#
.if (SKEIN_ASM_UNROLL & 512) == 0
    cmpq    $2*(ROUNDS_512/8),%rdi
    jb      Skein_512_round_loop
    movq    ctxPtr +F_O(%rbp),%rdi           #restore rdi --> context
.endif
    # end of rounds
    #################
    # feedforward:   ctx->X[i] = X[i] ^ w[i], {i=0..7}
    .irp _Rn_,8,9,10,11,12,13,14,15
  .if (_Rn_ == 8)
    movq    $FIRST_MASK64,%rbx
  .endif
      xorq  Wcopy+8*(_Rn_-8)+F_O(%rbp),%r\_Rn_  #feedforward XOR
      movq  %r\_Rn_,X_VARS+8*(_Rn_-8)(%rdi)     #and store result
  .if (_Rn_ == 14)
    andq    TWEAK+ 8(%rdi),%rbx
  .endif
    .endr
    Skein_Debug_Round 512,SKEIN_RND_FEED_FWD

    # go back for more blocks, if needed
    decq    blkCnt+F_O(%rbp)
    jnz     Skein_512_block_loop
    movq    %rbx,TWEAK + 8(%rdi)

    Reset_Stack
    ret
Skein_512_Process_Block_End:
#
  .if _SKEIN_DEBUG
# call here with rdx  = "round number"
Skein_Debug_Round_512:
    pushq   %rsi                     #save two regs for BLK_BITS-specific parms
    pushq   %rdi
  .irp _Rn_,8,9,10,11,12,13,14,15    #save X[] state on stack so debug routines can access it
    movq    %r\_Rn_,X_stk+8*(_Rn_-8)+F_O(%rbp)
  .endr
    movq    ctxPtr+F_O(%rbp),%rsi    #ctx_hdr_ptr
    movq    $512,%rdi                #now <rdi,rsi,rdx> are set for the call
    jmp     Skein_Debug_Round_Common
  .endif
#
.if _SKEIN_CODE_SIZE
C_label Skein_512_Process_Block_CodeSize
    movq    $(Skein_512_Process_Block_End-Skein_512_Process_Block),%rax
    ret
#
C_label Skein_512_Unroll_Cnt
  .if _UNROLL_CNT <> (ROUNDS_512/8)
    movq    $_UNROLL_CNT,%rax
  .else
    xorq    %rax,%rax
  .endif
    ret
.endif
#
.endif # _USE_ASM_ & 512
#
#=================================== Skein1024 =============================================
.if _USE_ASM_ & 1024
#
# void Skein1024_Process_Block(Skein_1024_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t bitcntAdd)#
#
#################
# use details of permutation to make register assignments
# 
o1K_rdi =  0        #offsets in X[] associated with each register
o1K_rsi =  1 
o1K_rbp =  2 
o1K_rax =  3 
o1K_rcx =  4        #rcx is "shared" with X6, since X4/X6 alternate
o1K_rbx =  5 
o1K_rdx =  7 
o1K_r8  =  8  
o1K_r9  =  9  
o1K_r10 = 10
o1K_r11 = 11
o1K_r12 = 12
o1K_r13 = 13
o1K_r14 = 14
o1K_r15 = 15
#
rIdx_offs = tmpStk_1024
#
.macro r1024_Mix w0,w1,reg0,reg1,_RN0_,_Rn1_,op1
    addReg      \reg0 , \reg1                      #perform the MIX
    RotL64      \reg1 , 1024,%((_RN0_) % 8),_Rn1_
    xorReg      \reg1 , \reg0
.if ((_RN0_) && 3) == 3         #time to do key injection?
 .if _SKEIN_DEBUG
    movq       %\reg0 , xDebug_1024+8*w0(%rsp)     #save intermediate values for Debug_Round
    movq       %\reg1 , xDebug_1024+8*w1(%rsp)     # (before inline key injection)
 .endif
_II_ = ((_RN0_)/4)+1            #injection count
 .if SKEIN_ASM_UNROLL && 1024   #here to do fully unrolled key injection
    addq        ksKey+ 8*((_II_+w0) % 17)(%rsp),%\reg0
    addq        ksKey+ 8*((_II_+w1) % 17)(%rsp),%\reg1
  .if     w1 == 13                                 #tweak injection
    addq        ksTwk+ 8*((_II_+ 0) %  3)(%rsp),%\reg1
  .elseif w0 == 14
    addq        ksTwk+ 8*((_II_+ 1) %  3)(%rsp),%\reg0
  .elseif w1 == 15
    addq        $_II_, %\reg1                      #(injection counter)
  .endif
 .else                          #here to do looping  key injection
  .if  (w0 == 0)
    movq        %rdi, X_stk+8*w0(%rsp)             #if so, store N0 so we can use reg as index
    movq         rIdx_offs(%rsp),%rdi              #get the injection counter index into rdi
  .else
    addq         ksKey+8+8*w0(%rsp,%rdi,8),%\reg0  #even key injection
  .endif
  .if     w1 == 13                                 #tweak injection
    addq         ksTwk+8+8* 0(%rsp,%rdi,8),%\reg1  
  .elseif w0 == 14
    addq         ksTwk+8+8* 1(%rsp,%rdi,8),%\reg0  
  .elseif w1 == 15
    addReg      \reg1,rdi,,,1                      #(injection counter)
  .endif
    addq         ksKey+8+8*w1(%rsp,%rdi,8),%\reg1  #odd key injection
 .endif
.endif
    # insert the op provided, .if any
    op1
.endm
#################
# MACRO: four rounds for 1024-bit blocks
#
.macro r1024_FourRounds _RR_    #RR = base round number (0 mod 4)
    # should be here with X4 set properly, X6 stored on stack
_Rn_ = (_RR_) + 0
        r1024_Mix  0, 1,rdi,rsi,_Rn_,0
        r1024_Mix  2, 3,rbp,rax,_Rn_,1
        r1024_Mix  4, 5,rcx,rbx,_Rn_,2,<movq %rcx,X_stk+8*4(%rsp)>       #save X4  on  stack (x4/x6 alternate)
        r1024_Mix  8, 9,r8 ,r9 ,_Rn_,4,<movq      X_stk+8*6(%rsp),%rcx>  #load X6 from stack 
        r1024_Mix 10,11,r10,r11,_Rn_,5
        r1024_Mix 12,13,r12,r13,_Rn_,6
        r1024_Mix  6, 7,rcx,rdx,_Rn_,3
        r1024_Mix 14,15,r14,r15,_Rn_,7
    .if _SKEIN_DEBUG
      Skein_Debug_Round 1024,%(_Rn_+1)
    .endif
_Rn_ = (_RR_) + 1
        r1024_Mix  0, 9,rdi,r9 ,_Rn_,0
        r1024_Mix  2,13,rbp,r13,_Rn_,1
        r1024_Mix  6,11,rcx,r11,_Rn_,2,<movq %rcx,X_stk+8*6(%rsp)>       #save X6  on  stack (x4/x6 alternate)
        r1024_Mix 10, 7,r10,rdx,_Rn_,4,<movq      X_stk+8*4(%rsp),%rcx>  #load X4 from stack 
        r1024_Mix 12, 3,r12,rax,_Rn_,5
        r1024_Mix 14, 5,r14,rbx,_Rn_,6
        r1024_Mix  4,15,rcx,r15,_Rn_,3
        r1024_Mix  8, 1,r8 ,rsi,_Rn_,7
    .if _SKEIN_DEBUG
      Skein_Debug_Round 1024,%(_Rn_+1)
    .endif
_Rn_ = (_RR_) + 2
        r1024_Mix  0, 7,rdi,rdx,_Rn_,0
        r1024_Mix  2, 5,rbp,rbx,_Rn_,1
        r1024_Mix  4, 3,rcx,rax,_Rn_,2,<movq %rcx,X_stk+8*4(%rsp)>       #save X4  on  stack (x4/x6 alternate)
        r1024_Mix 12,15,r12,r15,_Rn_,4,<movq      X_stk+8*6(%rsp),%rcx>  #load X6 from stack 
        r1024_Mix 14,13,r14,r13,_Rn_,5
        r1024_Mix  8,11,r8 ,r11,_Rn_,6
        r1024_Mix  6, 1,rcx,rsi,_Rn_,3
        r1024_Mix 10, 9,r10,r9 ,_Rn_,7
    .if _SKEIN_DEBUG
      Skein_Debug_Round 1024,%(_Rn_+1)
    .endif
_Rn_ = (_RR_) + 3
        r1024_Mix  0,15,rdi,r15,_Rn_,0
        r1024_Mix  2,11,rbp,r11,_Rn_,1
        r1024_Mix  6,13,rcx,r13,_Rn_,2,<movq %rcx,X_stk+8*6(%rsp)>       #save X6  on  stack (x4/x6 alternate)
        r1024_Mix 14, 1,r14,rsi,_Rn_,4,<movq      X_stk+8*4(%rsp),%rcx>  #load X4 from stack 
        r1024_Mix  8, 5,r8 ,rbx,_Rn_,5
        r1024_Mix 10, 3,r10,rax,_Rn_,6
        r1024_Mix  4, 9,rcx,r9 ,_Rn_,3
        r1024_Mix 12, 7,r12,rdx,_Rn_,7
    .if _SKEIN_DEBUG
      Skein_Debug_Round 1024,%(_Rn_+1)
    .endif

  .if (SKEIN_ASM_UNROLL && 1024) == 0           #here with rdi == rIdx, X0 on stack
    #"rotate" the key schedule on the stack
i8 = o1K_r8
i0 = o1K_rdi
    movq    %r8 , X_stk+8*i8(%rsp)              #free up a register (save it on the stack)
    movq          ksKey+8* 0(%rsp,%rdi,8),%r8   #get  key  word
    movq    %r8 , ksKey+8*17(%rsp,%rdi,8)       #rotate key (must do key first or tweak clobbers it!)
    movq          ksTwk+8* 0(%rsp,%rdi,8),%r8   #get tweak word
    movq    %r8 , ksTwk+8* 3(%rsp,%rdi,8)       #rotate tweak (onto the stack)
    movq          X_stk+8*i8(%rsp)       ,%r8   #get the reg back
    incq    %rdi                                #bump the index
    movq    %rdi, rIdx_offs (%rsp)              #save rdi again
    movq          ksKey+8*i0(%rsp,%rdi,8),%rdi  #get the key schedule word for X0 back
    addq          X_stk+8*i0(%rsp)       ,%rdi  #perform the X0 key injection
  .endif
    #show the result of the key injection
    Skein_Debug_Round 1024,SKEIN_RND_KEY_INJECT
.endm #r1024_FourRounds
#
################
# code
#
C_label Skein1024_Process_Block
#
    Setup_Stack 1024,ROUNDS_1024/8,WCNT
    movq    TWEAK+ 8(%rdi),%r9
    jmp     Skein1024_block_loop
    # main hash loop for Skein1024
    .p2align 4
Skein1024_block_loop:
    # general register usage:
    #   RSP              = stack pointer
    #   RAX..RDX,RSI,RDI = X1, X3..X7 (state words)
    #   R8 ..R15         = X8..X15    (state words)
    #   RBP              = temp (used for X0 and X2)
    #
  .if (SKEIN_ASM_UNROLL & 1024) == 0
    xorq    %rax,%rax                      #init loop index on the stack
    movq    %rax,rIdx_offs(%rsp)
  .endif
    movq         TWEAK+     0(%rdi),%r8
    addq         bitAdd+  F_O(%rbp),%r8    #computed updated tweak value T0
    movq    %r9 ,%r10 
    xorq    %r8 ,%r10                      #%rax/%rbx/%rcx = tweak schedule
    movq    %r8 ,TWEAK+     0(%rdi)        #save updated tweak value ctx->h.T[0]
    movq    %r8 ,ksTwk+ 0+F_O(%rbp)
    movq    %r9 ,ksTwk+ 8+F_O(%rbp)        #keep values in %r8 ,%r9  for initial tweak injection below
    movq    %r10,ksTwk+16+F_O(%rbp)
  .if _SKEIN_DEBUG
    movq    %r9 ,TWEAK+     8(%rdi)        #save updated tweak value ctx->h.T[1] for Skein_Debug_Block
  .endif
    movq         blkPtr +F_O(%rbp),%rsi    # rsi --> input block
    movq        $KW_PARITY        ,%rax    #overall key schedule parity

    # the logic here assumes the set {rdi,rsi,rbp,rax} = X[0,1,2,3]
    .irp _rN_,0,1,2,3,4,6                  #process the "initial" words, using r14/r15 as temps
      movq       X_VARS+8*_rN_(%rdi),%r14  #get state word
      movq              8*_rN_(%rsi),%r15  #get msg   word
      xorq  %r14,%rax                      #update key schedule overall parity
      movq  %r14,ksKey +8*_rN_+F_O(%rbp)   #save key schedule word on stack
      movq  %r15,Wcopy +8*_rN_+F_O(%rbp)   #save local msg Wcopy 
      addq  %r15,%r14                      #do the initial key injection
      movq  %r14,X_stk +8*_rN_    (%rsp)   #save initial state var on stack
    .endr
    # now process the rest, using the "real" registers 
    #     (MUST do it in reverse order to inject tweaks r8/r9 first)
    .irp _rr_,r15,r14,r13,r12,r11,r10,r9,r8,rdx,rbx
_oo_ = o1K_\_rr_                           #offset assocated with the register
      movq  X_VARS+8*_oo_(%rdi),%\_rr_     #get key schedule word from context
      movq         8*_oo_(%rsi),%rcx       #get next input msg word
      movq  %\_rr_, ksKey +8*_oo_(%rsp)    #save key schedule on stack
      xorq  %\_rr_, %rax                   #accumulate key schedule parity
      movq  %rcx,Wcopy+8*_oo_+F_O(%rbp)    #save copy of msg word for feedforward
      addq  %rcx,%\_rr_                    #do the initial  key  injection
      .if    _oo_ == 13                    #do the initial tweak injection
        addReg _rr_,r8                     #          (only in words 13/14)
      .elseif _oo_ == 14
        addReg _rr_,r9 
      .endif
    .endr
    movq    %rax,ksKey+8*WCNT+F_O(%rbp)    #save key schedule parity
.if _SKEIN_DEBUG
    Skein_Debug_Block 1024                 #initial debug dump
.endif
    addq     $8*WCNT,%rsi                  #bump the msg ptr
    movq     %rsi,blkPtr+F_O(%rbp)         #save bumped msg ptr
    # re-load words 0..4 from stack, enter the main loop
    .irp _rr_,rdi,rsi,rbp,rax,rcx          #(no need to re-load x6, already on stack)
      movq  X_stk+8*o1K_\_rr_(%rsp),%\_rr_ #re-load state and get ready to go!
    .endr
.if _SKEIN_DEBUG
    Skein_Debug_Round 1024,SKEIN_RND_KEY_INITIAL        #show state after initial key injection
.endif
    #
    #################
    # now the key schedule is computed. Start the rounds
    #
.if SKEIN_ASM_UNROLL & 1024
_UNROLL_CNT =   ROUNDS_1024/8
.else
_UNROLL_CNT =   SKEIN_UNROLL_1024
  .if ((ROUNDS_1024/8) % _UNROLL_CNT)
    .error "Invalid SKEIN_UNROLL_1024"
  .endif
Skein1024_round_loop:
.endif
#
_Rbase_ = 0
.rept _UNROLL_CNT*2                        #implement the rounds, 4 at a time
      r1024_FourRounds %(4*_Rbase_+00)
_Rbase_ = _Rbase_+1
.endr #rept _UNROLL_CNT
#
.if (SKEIN_ASM_UNROLL & 1024) == 0
    cmpq    $2*(ROUNDS_1024/8),tmpStk_1024(%rsp) #see .if we are done
    jb      Skein1024_round_loop    
.endif
    # end of rounds
    #################
    #
    # feedforward:   ctx->X[i] = X[i] ^ w[i], {i=0..15}
    movq    %rdx,X_stk+8*o1K_rdx(%rsp) #we need a register. x6 already on stack
    movq       ctxPtr(%rsp),%rdx
    
    .irp _rr_,rdi,rsi,rbp,rax,rcx,rbx,r8,r9,r10,r11,r12,r13,r14,r15   #do all but x6,x7
_oo_ = o1K_\_rr_
      xorq  Wcopy +8*_oo_(%rsp),%\_rr_ #feedforward XOR
      movq  %\_rr_,X_VARS+8*_oo_(%rdx) #save result into context
      .if (_oo_ ==  9)
        movq   $FIRST_MASK64 ,%r9
      .endif
      .if (_oo_ == 14)
        andq   TWEAK+ 8(%rdx),%r9
      .endif
    .endr
    # 
    movq         X_stk +8*6(%rsp),%rax #now process x6,x7 (skipped in .irp above)
    movq         X_stk +8*7(%rsp),%rbx
    xorq         Wcopy +8*6(%rsp),%rax
    xorq         Wcopy +8*7(%rsp),%rbx
    movq    %rax,X_VARS+8*6(%rdx)
    decq             blkCnt(%rsp)      #set zero flag iff done
    movq    %rbx,X_VARS+8*7(%rdx)

    Skein_Debug_Round 1024,SKEIN_RND_FEED_FWD,,<cmpq $0,blkCnt(%rsp)>
    # go back for more blocks, if needed
    movq             ctxPtr(%rsp),%rdi #don't muck with the flags here!
    lea          FRAME_OFFS(%rsp),%rbp
    jnz     Skein1024_block_loop
    movq    %r9 ,TWEAK+   8(%rdx)
    Reset_Stack
    ret
#
Skein1024_Process_Block_End:
#
.if _SKEIN_DEBUG
Skein_Debug_Round_1024:
    # call here with rdx  = "round number",
_SP_OFFS_ = 8*2                     #stack "offset" here: rdx, return addr
    #
  #save rest of X[] state on stack so debug routines can access it
  .irp _rr_,rsi,rbp,rax,rbx,r8,r9,r10,r11,r12,r13,r14,r15
    movq    %\_rr_,X_stk+8*o1K_\_rr_+_SP_OFFS_(%rsp)
  .endr
    # Figure out what to do with x0 (rdi).  When rdx == 0 mod 4, it's already on stack
    cmpq    $SKEIN_RND_SPECIAL,%rdx #special rounds always save
    jae     save_x0
    testq   $3,%rdx                 #otherwise only if rdx != 0 mod 4
    jz      save_x0_not
save_x0:
    movq    %rdi,X_stk+8*o1K_rdi+_SP_OFFS_(%rsp)
save_x0_not:
    #figure out the x4/x6 swapping state and save the correct one!
    cmpq    $SKEIN_RND_SPECIAL,%rdx #special rounds always do x4
    jae     save_x4
    testq   $1,%rdx                  #and even ones have r4 as well
    jz      save_x4
    movq    %rcx,X_stk+8*6+_SP_OFFS_(%rsp)
    jmp     debug_1024_go
save_x4:
    movq    %rcx,X_stk+8*4+_SP_OFFS_(%rsp)
debug_1024_go:
    #now all is saved in Xstk[] except for rdx
    push    %rsi                    #save two regs for BLK_BITS-specific parms
    push    %rdi
_SP_OFFS_ = _SP_OFFS_ + 16          #adjust stack offset accordingly (now 32)

    movq    _SP_OFFS_-8(%rsp),%rsi  #get back original %rdx (pushed on stack in macro call)
    movq    %rsi,X_stk+8*o1K_rdx+_SP_OFFS_(%rsp) #and save it in its rightful place in X_stk[]

    movq    ctxPtr+_SP_OFFS_(%rsp),%rsi  #rsi = ctx_hdr_ptr
    movq    $1024,%rdi                   #rdi = block size
    jmp     Skein_Debug_Round_Common
.endif
#
.if _SKEIN_CODE_SIZE
C_label Skein1024_Process_Block_CodeSize
    movq    $(Skein1024_Process_Block_End-Skein1024_Process_Block),%rax
    ret
#
C_label Skein1024_Unroll_Cnt
  .if _UNROLL_CNT <> (ROUNDS_1024/8)
    movq    $_UNROLL_CNT,%rax
  .else
    xorq    %rax,%rax
  .endif
    ret
.endif
#
.endif # _USE_ASM_ and 1024
#
.if _SKEIN_DEBUG
#----------------------------------------------------------------
#local debug routine to set up for calls to:
#  void Skein_Show_Round(uint_t bits,const Skein_Ctxt_Hdr_t *h,int r,const u64b_t *X)
#                       [       rdi                        rsi   rdx              rcx]
#
# here with %rdx = round number
#           %rsi = ctx_hdr_ptr
#           %rdi = block size (256/512/1024)
# on stack: saved rdi, saved rsi, retAddr, saved rdx  
#
Skein_Debug_Round_Common:
_SP_OFFS_ = 32                        #account for four words on stack already
  .irp _rr_,rax,rbx,rcx,rbp,r8,r9,r10,r11,r12,r13,r14,r15  #save the rest of the regs
    pushq %\_rr_
_SP_OFFS_ = _SP_OFFS_+8
  .endr
  .if (_SP_OFFS_ % 16)                # make sure stack is still 16-byte aligned here
    .error  "Debug_Round_Common: stack alignment"
  .endif
    # compute %rcx  = ptr to the X[] array on the stack (final parameter to call)
    leaq    X_stk+_SP_OFFS_(%rsp),%rcx #adjust for reg pushes, return address
    cmpq    $SKEIN_RND_FEED_FWD,%rdx   #special handling for feedforward "round"?
    jnz     _got_rcxA
    leaq    X_VARS(%rsi),%rcx
_got_rcxA:
  .if _USE_ASM_ & 1024
    # special handling for 1024-bit case
    #    (for rounds right before with key injection: 
    #        use xDebug_1024[] instead of X_stk[])
    cmpq    $SKEIN_RND_SPECIAL,%rdx
    jae     _got_rcxB               #must be a normal round
    orq     %rdx,%rdx
    jz      _got_rcxB               #just before key injection
    test    $3,%rdx
    jne     _got_rcxB
    cmp     $1024,%rdi              #only 1024-bit(s) for now
    jne     _got_rcxB
    leaq    xDebug_1024+_SP_OFFS_(%rsp),%rcx
_got_rcxB:
  .endif
    call    Skein_Show_Round        #call external debug handler

  .irp _rr_,r15,r14,r13,r12,r11,r10,r9,r8,rbp,rcx,rbx,rax  #restore regs
    popq  %\_rr_
_SP_OFFS_ = _SP_OFFS_-8
  .endr
  .if _SP_OFFS_ - 32
    .error   "Debug_Round_Common: push/pop misalignment!"
  .endif    
    popq    %rdi
    popq    %rsi
    ret
.endif
#----------------------------------------------------------------
    .section .note.GNU-stack,"",@progbits

    .end
