//===-- AMDGPUISelLowering.h - AMDGPU Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition of the TargetLowering class that is common
/// to all AMD GPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUISELLOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUISELLOWERING_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class AMDGPUMachineFunction;
class AMDGPUSubtarget;
struct ArgDescriptor;

class AMDGPUTargetLowering : public TargetLowering {
private:
  const AMDGPUSubtarget *Subtarget;

  /// \returns AMDGPUISD::FFBH_U32 node if the incoming \p Op may have been
  /// legalized from a smaller type VT. Need to match pre-legalized type because
  /// the generic legalization inserts the add/sub between the select and
  /// compare.
  SDValue getFFBX_U32(SelectionDAG &DAG, SDValue Op, const SDLoc &DL, unsigned Opc) const;

public:
  /// \returns The minimum number of bits needed to store the value of \Op as an
  /// unsigned integer. Truncating to this size and then zero-extending to the
  /// original size will not change the value.
  static unsigned numBitsUnsigned(SDValue Op, SelectionDAG &DAG);

  /// \returns The minimum number of bits needed to store the value of \Op as a
  /// signed integer. Truncating to this size and then sign-extending to the
  /// original size will not change the value.
  static unsigned numBitsSigned(SDValue Op, SelectionDAG &DAG);

protected:
  SDValue LowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const;
  /// Split a vector store into multiple scalar stores.
  /// \returns The resulting chain.

  SDValue LowerFREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFCEIL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFTRUNC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRINT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFNEARBYINT(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerFROUNDEVEN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFROUND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFFLOOR(SDValue Op, SelectionDAG &DAG) const;

  static bool allowApproxFunc(const SelectionDAG &DAG, SDNodeFlags Flags);
  static bool needsDenormHandlingF32(const SelectionDAG &DAG, SDValue Src,
                                     SDNodeFlags Flags);
  SDValue getIsLtSmallestNormal(SelectionDAG &DAG, SDValue Op,
                                SDNodeFlags Flags) const;
  SDValue getIsFinite(SelectionDAG &DAG, SDValue Op, SDNodeFlags Flags) const;
  std::pair<SDValue, SDValue> getScaledLogInput(SelectionDAG &DAG,
                                                const SDLoc SL, SDValue Op,
                                                SDNodeFlags Flags) const;

  SDValue LowerFLOG2(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFLOGCommon(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFLOG10(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFLOGUnsafe(SDValue Op, const SDLoc &SL, SelectionDAG &DAG,
                          bool IsLog10, SDNodeFlags Flags) const;
  SDValue lowerFEXP2(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerFEXPUnsafe(SDValue Op, const SDLoc &SL, SelectionDAG &DAG,
                          SDNodeFlags Flags) const;
  SDValue lowerFEXP10Unsafe(SDValue Op, const SDLoc &SL, SelectionDAG &DAG,
                            SDNodeFlags Flags) const;
  SDValue lowerFEXP(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerCTLZResults(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerCTLZ_CTTZ(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerINT_TO_FP32(SDValue Op, SelectionDAG &DAG, bool Signed) const;
  SDValue LowerINT_TO_FP64(SDValue Op, SelectionDAG &DAG, bool Signed) const;
  SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerFP_TO_INT64(SDValue Op, SelectionDAG &DAG, bool Signed) const;
  SDValue LowerFP_TO_FP16(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerSIGN_EXTEND_INREG(SDValue Op, SelectionDAG &DAG) const;

protected:
  bool shouldCombineMemoryType(EVT VT) const;
  SDValue performLoadCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performStoreCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performAssertSZExtCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performIntrinsicWOChainCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  SDValue splitBinaryBitConstantOpImpl(DAGCombinerInfo &DCI, const SDLoc &SL,
                                       unsigned Opc, SDValue LHS,
                                       uint32_t ValLo, uint32_t ValHi) const;
  SDValue performShlCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performSraCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performSrlCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performTruncateCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulLoHiCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulhsCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulhuCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performCtlz_CttzCombine(const SDLoc &SL, SDValue Cond, SDValue LHS,
                             SDValue RHS, DAGCombinerInfo &DCI) const;

  SDValue foldFreeOpFromSelect(TargetLowering::DAGCombinerInfo &DCI,
                               SDValue N) const;
  SDValue performSelectCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  TargetLowering::NegatibleCost
  getConstantNegateCost(const ConstantFPSDNode *C) const;

  bool isConstantCostlierToNegate(SDValue N) const;
  bool isConstantCheaperToNegate(SDValue N) const;
  SDValue performFNegCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performFAbsCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performRcpCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  static EVT getEquivalentMemType(LLVMContext &Context, EVT VT);

  virtual SDValue LowerGlobalAddress(AMDGPUMachineFunction *MFI, SDValue Op,
                                     SelectionDAG &DAG) const;

  /// Return 64-bit value Op as two 32-bit integers.
  std::pair<SDValue, SDValue> split64BitValue(SDValue Op,
                                              SelectionDAG &DAG) const;
  SDValue getLoHalf64(SDValue Op, SelectionDAG &DAG) const;
  SDValue getHiHalf64(SDValue Op, SelectionDAG &DAG) const;

  /// Split a vector type into two parts. The first part is a power of two
  /// vector. The second part is whatever is left over, and is a scalar if it
  /// would otherwise be a 1-vector.
  std::pair<EVT, EVT> getSplitDestVTs(const EVT &VT, SelectionDAG &DAG) const;

  /// Split a vector value into two parts of types LoVT and HiVT. HiVT could be
  /// scalar.
  std::pair<SDValue, SDValue> splitVector(const SDValue &N, const SDLoc &DL,
                                          const EVT &LoVT, const EVT &HighVT,
                                          SelectionDAG &DAG) const;

  /// Split a vector load into 2 loads of half the vector.
  SDValue SplitVectorLoad(SDValue Op, SelectionDAG &DAG) const;

  /// Widen a suitably aligned v3 load. For all other cases, split the input
  /// vector load.
  SDValue WidenOrSplitVectorLoad(SDValue Op, SelectionDAG &DAG) const;

  /// Split a vector store into 2 stores of half the vector.
  SDValue SplitVectorStore(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSDIVREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUDIVREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDIVREM24(SDValue Op, SelectionDAG &DAG, bool sign) const;
  void LowerUDIVREM64(SDValue Op, SelectionDAG &DAG,
                                    SmallVectorImpl<SDValue> &Results) const;

  void analyzeFormalArgumentsCompute(
    CCState &State,
    const SmallVectorImpl<ISD::InputArg> &Ins) const;

public:
  AMDGPUTargetLowering(const TargetMachine &TM, const AMDGPUSubtarget &STI);

  bool mayIgnoreSignedZero(SDValue Op) const;

  static inline SDValue stripBitcast(SDValue Val) {
    return Val.getOpcode() == ISD::BITCAST ? Val.getOperand(0) : Val;
  }

  static bool shouldFoldFNegIntoSrc(SDNode *FNeg, SDValue FNegSrc);
  static bool allUsesHaveSourceMods(const SDNode *N,
                                    unsigned CostThreshold = 4);
  bool isFAbsFree(EVT VT) const override;
  bool isFNegFree(EVT VT) const override;
  bool isTruncateFree(EVT Src, EVT Dest) const override;
  bool isTruncateFree(Type *Src, Type *Dest) const override;

  bool isZExtFree(Type *Src, Type *Dest) const override;
  bool isZExtFree(EVT Src, EVT Dest) const override;

  SDValue getNegatedExpression(SDValue Op, SelectionDAG &DAG,
                               bool LegalOperations, bool ForCodeSize,
                               NegatibleCost &Cost,
                               unsigned Depth) const override;

  bool isNarrowingProfitable(EVT SrcVT, EVT DestVT) const override;

  bool isDesirableToCommuteWithShift(const SDNode *N,
                                     CombineLevel Level) const override;

  EVT getTypeForExtReturn(LLVMContext &Context, EVT VT,
                          ISD::NodeType ExtendKind) const override;

  MVT getVectorIdxTy(const DataLayout &) const override;
  bool isSelectSupported(SelectSupportKind) const override;

  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override;
  bool ShouldShrinkFPConstant(EVT VT) const override;
  bool shouldReduceLoadWidth(SDNode *Load,
                             ISD::LoadExtType ExtType,
                             EVT ExtVT) const override;

  bool isLoadBitCastBeneficial(EVT, EVT, const SelectionDAG &DAG,
                               const MachineMemOperand &MMO) const final;

  bool storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT,
                                    unsigned NumElem,
                                    unsigned AS) const override;
  bool aggressivelyPreferBuildVectorSources(EVT VecVT) const override;
  bool isCheapToSpeculateCttz(Type *Ty) const override;
  bool isCheapToSpeculateCtlz(Type *Ty) const override;

  bool isSDNodeAlwaysUniform(const SDNode *N) const override;

  // FIXME: This hook should not exist
  AtomicExpansionKind shouldCastAtomicLoadInIR(LoadInst *LI) const override {
    return AtomicExpansionKind::None;
  }

  AtomicExpansionKind shouldCastAtomicStoreInIR(StoreInst *SI) const override {
    return AtomicExpansionKind::None;
  }

  AtomicExpansionKind shouldCastAtomicRMWIInIR(AtomicRMWInst *) const override {
    return AtomicExpansionKind::None;
  }

  static CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg);
  static CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC, bool IsVarArg);

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  SDValue addTokenForArgument(SDValue Chain,
                              SelectionDAG &DAG,
                              MachineFrameInfo &MFI,
                              int ClobberedFI) const;

  SDValue lowerUnhandledCall(CallLoweringInfo &CLI,
                             SmallVectorImpl<SDValue> &InVals,
                             StringRef Reason) const;
  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  void ReplaceNodeResults(SDNode * N,
                          SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue combineFMinMaxLegacyImpl(const SDLoc &DL, EVT VT, SDValue LHS,
                                   SDValue RHS, SDValue True, SDValue False,
                                   SDValue CC, DAGCombinerInfo &DCI) const;

  SDValue combineFMinMaxLegacy(const SDLoc &DL, EVT VT, SDValue LHS,
                               SDValue RHS, SDValue True, SDValue False,
                               SDValue CC, DAGCombinerInfo &DCI) const;

  const char* getTargetNodeName(unsigned Opcode) const override;

  // FIXME: Turn off MergeConsecutiveStores() before Instruction Selection for
  // AMDGPU.  Commit r319036,
  // (https://github.com/llvm/llvm-project/commit/db77e57ea86d941a4262ef60261692f4cb6893e6)
  // turned on MergeConsecutiveStores() before Instruction Selection for all
  // targets.  Enough AMDGPU compiles go into an infinite loop (
  // MergeConsecutiveStores() merges two stores; LegalizeStoreOps() un-merges;
  // MergeConsecutiveStores() re-merges, etc. ) to warrant turning it off for
  // now.
  bool mergeStoresAfterLegalization(EVT) const override { return false; }

  bool isFsqrtCheap(SDValue Operand, SelectionDAG &DAG) const override {
    return true;
  }
  SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                           int &RefinementSteps, bool &UseOneConstNR,
                           bool Reciprocal) const override;
  SDValue getRecipEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                           int &RefinementSteps) const override;

  virtual SDNode *PostISelFolding(MachineSDNode *N,
                                  SelectionDAG &DAG) const = 0;

  /// Determine which of the bits specified in \p Mask are known to be
  /// either zero or one and return them in the \p KnownZero and \p KnownOne
  /// bitsets.
  void computeKnownBitsForTargetNode(const SDValue Op,
                                     KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth = 0) const override;

  unsigned ComputeNumSignBitsForTargetNode(SDValue Op, const APInt &DemandedElts,
                                           const SelectionDAG &DAG,
                                           unsigned Depth = 0) const override;

  unsigned computeNumSignBitsForTargetInstr(GISelKnownBits &Analysis,
                                            Register R,
                                            const APInt &DemandedElts,
                                            const MachineRegisterInfo &MRI,
                                            unsigned Depth = 0) const override;

  bool isKnownNeverNaNForTargetNode(SDValue Op,
                                    const SelectionDAG &DAG,
                                    bool SNaN = false,
                                    unsigned Depth = 0) const override;

  bool isReassocProfitable(MachineRegisterInfo &MRI, Register N0,
                           Register N1) const override;

  /// Helper function that adds Reg to the LiveIn list of the DAG's
  /// MachineFunction.
  ///
  /// \returns a RegisterSDNode representing Reg if \p RawReg is true, otherwise
  /// a copy from the register.
  SDValue CreateLiveInRegister(SelectionDAG &DAG,
                               const TargetRegisterClass *RC,
                               Register Reg, EVT VT,
                               const SDLoc &SL,
                               bool RawReg = false) const;
  SDValue CreateLiveInRegister(SelectionDAG &DAG,
                               const TargetRegisterClass *RC,
                               Register Reg, EVT VT) const {
    return CreateLiveInRegister(DAG, RC, Reg, VT, SDLoc(DAG.getEntryNode()));
  }

  // Returns the raw live in register rather than a copy from it.
  SDValue CreateLiveInRegisterRaw(SelectionDAG &DAG,
                                  const TargetRegisterClass *RC,
                                  Register Reg, EVT VT) const {
    return CreateLiveInRegister(DAG, RC, Reg, VT, SDLoc(DAG.getEntryNode()), true);
  }

  /// Similar to CreateLiveInRegister, except value maybe loaded from a stack
  /// slot rather than passed in a register.
  SDValue loadStackInputValue(SelectionDAG &DAG,
                              EVT VT,
                              const SDLoc &SL,
                              int64_t Offset) const;

  SDValue storeStackInputValue(SelectionDAG &DAG,
                               const SDLoc &SL,
                               SDValue Chain,
                               SDValue ArgVal,
                               int64_t Offset) const;

  SDValue loadInputValue(SelectionDAG &DAG,
                         const TargetRegisterClass *RC,
                         EVT VT, const SDLoc &SL,
                         const ArgDescriptor &Arg) const;

  enum ImplicitParameter {
    FIRST_IMPLICIT,
    PRIVATE_BASE,
    SHARED_BASE,
    QUEUE_PTR,
  };

  /// Helper function that returns the byte offset of the given
  /// type of implicit parameter.
  uint32_t getImplicitParameterOffset(const MachineFunction &MF,
                                      const ImplicitParameter Param) const;
  uint32_t getImplicitParameterOffset(const uint64_t ExplicitKernArgSize,
                                      const ImplicitParameter Param) const;

  MVT getFenceOperandTy(const DataLayout &DL) const override {
    return MVT::i32;
  }

  AtomicExpansionKind shouldExpandAtomicRMWInIR(AtomicRMWInst *) const override;

  bool shouldSinkOperands(Instruction *I,
                          SmallVectorImpl<Use *> &Ops) const override;
};

namespace AMDGPUISD {

enum NodeType : unsigned {
  // AMDIL ISD Opcodes
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  UMUL, // 32bit unsigned multiplication
  BRANCH_COND,
  // End AMDIL ISD Opcodes

  // Function call.
  CALL,
  TC_RETURN,
  TC_RETURN_GFX,
  TC_RETURN_CHAIN,
  TRAP,

  // Masked control flow nodes.
  IF,
  ELSE,
  LOOP,

  // A uniform kernel return that terminates the wavefront.
  ENDPGM,

  // s_endpgm, but we may want to insert it in the middle of the block.
  ENDPGM_TRAP,

  // "s_trap 2" equivalent on hardware that does not support it.
  SIMULATED_TRAP,

  // Return to a shader part's epilog code.
  RETURN_TO_EPILOG,

  // Return with values from a non-entry function.
  RET_GLUE,

  // Convert a unswizzled wave uniform stack address to an address compatible
  // with a vector offset for use in stack access.
  WAVE_ADDRESS,

  DWORDADDR,
  FRACT,

  /// CLAMP value between 0.0 and 1.0. NaN clamped to 0, following clamp output
  /// modifier behavior with dx10_enable.
  CLAMP,

  // This is SETCC with the full mask result which is used for a compare with a
  // result bit per item in the wavefront.
  SETCC,
  SETREG,

  DENORM_MODE,

  // FP ops with input and output chain.
  FMA_W_CHAIN,
  FMUL_W_CHAIN,

  // SIN_HW, COS_HW - f32 for SI, 1 ULP max error, valid from -100 pi to 100 pi.
  // Denormals handled on some parts.
  COS_HW,
  SIN_HW,
  FMAX_LEGACY,
  FMIN_LEGACY,

  FMAX3,
  SMAX3,
  UMAX3,
  FMIN3,
  SMIN3,
  UMIN3,
  FMED3,
  SMED3,
  UMED3,
  FMAXIMUM3,
  FMINIMUM3,
  FDOT2,
  URECIP,
  DIV_SCALE,
  DIV_FMAS,
  DIV_FIXUP,
  // For emitting ISD::FMAD when f32 denormals are enabled because mac/mad is
  // treated as an illegal operation.
  FMAD_FTZ,

  // RCP, RSQ - For f32, 1 ULP max error, no denormal handling.
  //            For f64, max error 2^29 ULP, handles denormals.
  RCP,
  RSQ,
  RCP_LEGACY,
  RCP_IFLAG,

  // log2, no denormal handling for f32.
  LOG,

  // exp2, no denormal handling for f32.
  EXP,

  FMUL_LEGACY,
  RSQ_CLAMP,
  FP_CLASS,
  DOT4,
  CARRY,
  BORROW,
  BFE_U32,  // Extract range of bits with zero extension to 32-bits.
  BFE_I32,  // Extract range of bits with sign extension to 32-bits.
  BFI,      // (src0 & src1) | (~src0 & src2)
  BFM,      // Insert a range of bits into a 32-bit word.
  FFBH_U32, // ctlz with -1 if input is zero.
  FFBH_I32,
  FFBL_B32, // cttz with -1 if input is zero.
  MUL_U24,
  MUL_I24,
  MULHI_U24,
  MULHI_I24,
  MAD_U24,
  MAD_I24,
  MAD_U64_U32,
  MAD_I64_I32,
  PERM,
  TEXTURE_FETCH,
  R600_EXPORT,
  CONST_ADDRESS,
  REGISTER_LOAD,
  REGISTER_STORE,
  SAMPLE,
  SAMPLEB,
  SAMPLED,
  SAMPLEL,

  // These cvt_f32_ubyte* nodes need to remain consecutive and in order.
  CVT_F32_UBYTE0,
  CVT_F32_UBYTE1,
  CVT_F32_UBYTE2,
  CVT_F32_UBYTE3,

  // Convert two float 32 numbers into a single register holding two packed f16
  // with round to zero.
  CVT_PKRTZ_F16_F32,
  CVT_PKNORM_I16_F32,
  CVT_PKNORM_U16_F32,
  CVT_PK_I16_I32,
  CVT_PK_U16_U32,

  // Same as the standard node, except the high bits of the resulting integer
  // are known 0.
  FP_TO_FP16,

  /// This node is for VLIW targets and it is used to represent a vector
  /// that is stored in consecutive registers with the same channel.
  /// For example:
  ///   |X  |Y|Z|W|
  /// T0|v.x| | | |
  /// T1|v.y| | | |
  /// T2|v.z| | | |
  /// T3|v.w| | | |
  BUILD_VERTICAL_VECTOR,
  /// Pointer to the start of the shader's constant data.
  CONST_DATA_PTR,
  PC_ADD_REL_OFFSET,
  LDS,
  FPTRUNC_ROUND_UPWARD,
  FPTRUNC_ROUND_DOWNWARD,

  DUMMY_CHAIN,
  FIRST_MEM_OPCODE_NUMBER = ISD::FIRST_TARGET_MEMORY_OPCODE,
  LOAD_D16_HI,
  LOAD_D16_LO,
  LOAD_D16_HI_I8,
  LOAD_D16_HI_U8,
  LOAD_D16_LO_I8,
  LOAD_D16_LO_U8,

  STORE_MSKOR,
  LOAD_CONSTANT,
  TBUFFER_STORE_FORMAT,
  TBUFFER_STORE_FORMAT_D16,
  TBUFFER_LOAD_FORMAT,
  TBUFFER_LOAD_FORMAT_D16,
  DS_ORDERED_COUNT,
  ATOMIC_CMP_SWAP,
  BUFFER_LOAD,
  BUFFER_LOAD_UBYTE,
  BUFFER_LOAD_USHORT,
  BUFFER_LOAD_BYTE,
  BUFFER_LOAD_SHORT,
  BUFFER_LOAD_TFE,
  BUFFER_LOAD_UBYTE_TFE,
  BUFFER_LOAD_USHORT_TFE,
  BUFFER_LOAD_BYTE_TFE,
  BUFFER_LOAD_SHORT_TFE,
  BUFFER_LOAD_FORMAT,
  BUFFER_LOAD_FORMAT_TFE,
  BUFFER_LOAD_FORMAT_D16,
  SBUFFER_LOAD,
  SBUFFER_LOAD_BYTE,
  SBUFFER_LOAD_UBYTE,
  SBUFFER_LOAD_SHORT,
  SBUFFER_LOAD_USHORT,
  BUFFER_STORE,
  BUFFER_STORE_BYTE,
  BUFFER_STORE_SHORT,
  BUFFER_STORE_FORMAT,
  BUFFER_STORE_FORMAT_D16,
  BUFFER_ATOMIC_SWAP,
  BUFFER_ATOMIC_ADD,
  BUFFER_ATOMIC_SUB,
  BUFFER_ATOMIC_SMIN,
  BUFFER_ATOMIC_UMIN,
  BUFFER_ATOMIC_SMAX,
  BUFFER_ATOMIC_UMAX,
  BUFFER_ATOMIC_AND,
  BUFFER_ATOMIC_OR,
  BUFFER_ATOMIC_XOR,
  BUFFER_ATOMIC_INC,
  BUFFER_ATOMIC_DEC,
  BUFFER_ATOMIC_CMPSWAP,
  BUFFER_ATOMIC_CSUB,
  BUFFER_ATOMIC_FADD,
  BUFFER_ATOMIC_FMIN,
  BUFFER_ATOMIC_FMAX,
  BUFFER_ATOMIC_COND_SUB_U32,

  LAST_AMDGPU_ISD_NUMBER
};

} // End namespace AMDGPUISD

} // End namespace llvm

#endif
