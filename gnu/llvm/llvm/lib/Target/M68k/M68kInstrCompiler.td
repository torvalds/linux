//===-- M68kInstrCompiler.td - Pseudos and Patterns --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the various pseudo instructions used by the compiler,
/// as well as Pat patterns used during instruction selection.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ConstantPool, GlobalAddress, ExternalSymbol, and JumpTable
//===----------------------------------------------------------------------===//

def : Pat<(i32 (MxWrapper tconstpool    :$src)), (MOV32ri tconstpool    :$src)>;
def : Pat<(i32 (MxWrapper tglobaladdr   :$src)), (MOV32ri tglobaladdr   :$src)>;
def : Pat<(i32 (MxWrapper texternalsym  :$src)), (MOV32ri texternalsym  :$src)>;
def : Pat<(i32 (MxWrapper tjumptable    :$src)), (MOV32ri tjumptable    :$src)>;
def : Pat<(i32 (MxWrapper tblockaddress :$src)), (MOV32ri tblockaddress :$src)>;

def : Pat<(add MxDRD32:$src, (MxWrapper tconstpool:$opd)),
          (ADD32di MxDRD32:$src, tconstpool:$opd)>;
def : Pat<(add MxARD32:$src, (MxWrapper tjumptable:$opd)),
          (ADD32ai MxARD32:$src, tjumptable:$opd)>;
def : Pat<(add MxARD32:$src, (MxWrapper tglobaladdr :$opd)),
          (ADD32ai MxARD32:$src, tglobaladdr:$opd)>;
def : Pat<(add MxARD32:$src, (MxWrapper texternalsym:$opd)),
          (ADD32ai MxARD32:$src, texternalsym:$opd)>;
def : Pat<(add MxARD32:$src, (MxWrapper tblockaddress:$opd)),
          (ADD32ai MxARD32:$src, tblockaddress:$opd)>;

def : Pat<(store (i32 (MxWrapper tglobaladdr:$src)), iPTR:$dst),
          (MOV32ji MxARI32:$dst, tglobaladdr:$src)>;
def : Pat<(store (i32 (MxWrapper texternalsym:$src)), iPTR:$dst),
          (MOV32ji MxARI32:$dst, texternalsym:$src)>;
def : Pat<(store (i32 (MxWrapper tblockaddress:$src)), iPTR:$dst),
          (MOV32ji MxARI32:$dst, tblockaddress:$src)>;

def : Pat<(i32 (MxWrapperPC tconstpool    :$src)), (LEA32q tconstpool    :$src)>;
def : Pat<(i32 (MxWrapperPC tglobaladdr   :$src)), (LEA32q tglobaladdr   :$src)>;
def : Pat<(i32 (MxWrapperPC texternalsym  :$src)), (LEA32q texternalsym  :$src)>;
def : Pat<(i32 (MxWrapperPC tjumptable    :$src)), (LEA32q tjumptable    :$src)>;
def : Pat<(i32 (MxWrapperPC tblockaddress :$src)), (LEA32q tblockaddress :$src)>;


//===----------------------------------------------------------------------===//
// Conditional Move Pseudo Instructions
//
// CMOV* - Used to implement the SELECT DAG operation. Expanded after
// instruction selection into a branch sequence.
//===----------------------------------------------------------------------===//

let usesCustomInserter = 1, Uses = [CCR] in
class MxCMove<MxType TYPE>
    : MxPseudo<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$t, TYPE.ROp:$f, i8imm:$cond),
               [(set TYPE.VT:$dst,
                     (TYPE.VT (MxCmov TYPE.VT:$t, TYPE.VT:$f, imm:$cond, CCR)))]>;

def CMOV8d  : MxCMove<MxType8d>;
def CMOV16d : MxCMove<MxType16d>;
def CMOV32r : MxCMove<MxType32r>;


//===----------------------------------------------------------------------===//
// Calls
//===----------------------------------------------------------------------===//

// ADJCALLSTACKDOWN/UP implicitly use/def %SP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber CCR.
let Defs = [SP, CCR], Uses = [SP] in {

  def ADJCALLSTACKDOWN
    : MxPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
               [(MxCallSeqStart timm:$amt1, timm:$amt2)]>;

  def ADJCALLSTACKUP
    : MxPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
               [(MxCallSeqEnd timm:$amt1, timm:$amt2)]>;

} // Defs

//===----------------------------------------------------------------------===//
// Tail Call
//===----------------------------------------------------------------------===//

// Tailcall stuff. The TCRETURN instructions execute after the epilog, so they
// can never use callee-saved registers. That is the purpose of the XR32_TC
// register classes.

// FIXME TC is disabled for PIC mode because the global base
// register which is part of the address mode may be assigned a
// callee-saved register.
def : Pat<(MxTCRet (load MxCP_ARII:$dst), imm:$adj),
          (TCRETURNj (MOV32af_TC MxARII32:$dst), imm:$adj)>,
      Requires<[IsNotPIC]>;

def : Pat<(MxTCRet AR32_TC:$dst, imm:$adj),
          (TCRETURNj MxARI32_TC:$dst, imm:$adj)>;

def : Pat<(MxTCRet (i32 tglobaladdr:$dst), imm:$adj),
          (TCRETURNq MxPCD32:$dst, imm:$adj)>;

def : Pat<(MxTCRet (i32 texternalsym:$dst), imm:$adj),
          (TCRETURNq MxPCD32:$dst, imm:$adj)>;


//===----------------------------------------------------------------------===//
// Segmented Stack
//
// When using segmented stacks these are lowered into instructions which first
// check if the current stacklet has enough free memory. If it does, memory is
// allocated by bumping the stack pointer. Otherwise memory is allocated from
// the heap.
//===----------------------------------------------------------------------===//

let Defs = [SP, CCR], Uses = [SP] in
let usesCustomInserter = 1 in
def SALLOCA : MxPseudo<(outs MxARD32:$dst), (ins MxARD32:$size),
                       [(set iPTR:$dst, (MxSegAlloca iPTR:$size))]>;
