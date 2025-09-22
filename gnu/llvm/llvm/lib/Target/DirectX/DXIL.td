//- DXIL.td - Describe DXIL operation -------------------------*- tablegen -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This is a target description file for DXIL operations.
///
//===----------------------------------------------------------------------===//

include "llvm/IR/Intrinsics.td"

class DXILOpClass;

// Following is a set of DXIL Operation classes whose names appear to be
// arbitrary, yet need to be a substring of the function name used during
// lowering to DXIL Operation calls. These class name strings are specified
// as the third argument of add_dixil_op in utils/hct/hctdb.py and case converted
// in utils/hct/hctdb_instrhelp.py of DirectXShaderCompiler repo. The function
// name has the format "dx.op.<class-name>.<return-type>".

defset list<DXILOpClass> OpClasses = {
  def acceptHitAndEndSearch : DXILOpClass;
  def allocateNodeOutputRecords : DXILOpClass;
  def allocateRayQuery : DXILOpClass;
  def annotateHandle : DXILOpClass;
  def annotateNodeHandle : DXILOpClass;
  def annotateNodeRecordHandle : DXILOpClass;
  def atomicBinOp : DXILOpClass;
  def atomicCompareExchange : DXILOpClass;
  def attributeAtVertex : DXILOpClass;
  def barrier : DXILOpClass;
  def barrierByMemoryHandle : DXILOpClass;
  def barrierByMemoryType : DXILOpClass;
  def barrierByNodeRecordHandle : DXILOpClass;
  def binary : DXILOpClass;
  def binaryWithCarryOrBorrow : DXILOpClass;
  def binaryWithTwoOuts : DXILOpClass;
  def bitcastF16toI16 : DXILOpClass;
  def bitcastF32toI32 : DXILOpClass;
  def bitcastF64toI64 : DXILOpClass;
  def bitcastI16toF16 : DXILOpClass;
  def bitcastI32toF32 : DXILOpClass;
  def bitcastI64toF64 : DXILOpClass;
  def bufferLoad : DXILOpClass;
  def bufferStore : DXILOpClass;
  def bufferUpdateCounter : DXILOpClass;
  def calculateLOD : DXILOpClass;
  def callShader : DXILOpClass;
  def cbufferLoad : DXILOpClass;
  def cbufferLoadLegacy : DXILOpClass;
  def checkAccessFullyMapped : DXILOpClass;
  def coverage : DXILOpClass;
  def createHandle : DXILOpClass;
  def createHandleForLib : DXILOpClass;
  def createHandleFromBinding : DXILOpClass;
  def createHandleFromHeap : DXILOpClass;
  def createNodeInputRecordHandle : DXILOpClass;
  def createNodeOutputHandle : DXILOpClass;
  def cutStream : DXILOpClass;
  def cycleCounterLegacy : DXILOpClass;
  def discard : DXILOpClass;
  def dispatchMesh : DXILOpClass;
  def dispatchRaysDimensions : DXILOpClass;
  def dispatchRaysIndex : DXILOpClass;
  def domainLocation : DXILOpClass;
  def dot2 : DXILOpClass;
  def dot2AddHalf : DXILOpClass;
  def dot3 : DXILOpClass;
  def dot4 : DXILOpClass;
  def dot4AddPacked : DXILOpClass;
  def emitIndices : DXILOpClass;
  def emitStream : DXILOpClass;
  def emitThenCutStream : DXILOpClass;
  def evalCentroid : DXILOpClass;
  def evalSampleIndex : DXILOpClass;
  def evalSnapped : DXILOpClass;
  def finishedCrossGroupSharing : DXILOpClass;
  def flattenedThreadIdInGroup : DXILOpClass;
  def geometryIndex : DXILOpClass;
  def getDimensions : DXILOpClass;
  def getInputRecordCount : DXILOpClass;
  def getMeshPayload : DXILOpClass;
  def getNodeRecordPtr : DXILOpClass;
  def getRemainingRecursionLevels : DXILOpClass;
  def groupId : DXILOpClass;
  def gsInstanceID : DXILOpClass;
  def hitKind : DXILOpClass;
  def ignoreHit : DXILOpClass;
  def incrementOutputCount : DXILOpClass;
  def indexNodeHandle : DXILOpClass;
  def innerCoverage : DXILOpClass;
  def instanceID : DXILOpClass;
  def instanceIndex : DXILOpClass;
  def isHelperLane : DXILOpClass;
  def isSpecialFloat : DXILOpClass;
  def legacyDoubleToFloat : DXILOpClass;
  def legacyDoubleToSInt32 : DXILOpClass;
  def legacyDoubleToUInt32 : DXILOpClass;
  def legacyF16ToF32 : DXILOpClass;
  def legacyF32ToF16 : DXILOpClass;
  def loadInput : DXILOpClass;
  def loadOutputControlPoint : DXILOpClass;
  def loadPatchConstant : DXILOpClass;
  def makeDouble : DXILOpClass;
  def minPrecXRegLoad : DXILOpClass;
  def minPrecXRegStore : DXILOpClass;
  def nodeOutputIsValid : DXILOpClass;
  def objectRayDirection : DXILOpClass;
  def objectRayOrigin : DXILOpClass;
  def objectToWorld : DXILOpClass;
  def outputComplete : DXILOpClass;
  def outputControlPointID : DXILOpClass;
  def pack4x8 : DXILOpClass;
  def primitiveID : DXILOpClass;
  def primitiveIndex : DXILOpClass;
  def quadOp : DXILOpClass;
  def quadReadLaneAt : DXILOpClass;
  def quadVote : DXILOpClass;
  def quaternary : DXILOpClass;
  def rawBufferLoad : DXILOpClass;
  def rawBufferStore : DXILOpClass;
  def rayFlags : DXILOpClass;
  def rayQuery_Abort : DXILOpClass;
  def rayQuery_CommitNonOpaqueTriangleHit : DXILOpClass;
  def rayQuery_CommitProceduralPrimitiveHit : DXILOpClass;
  def rayQuery_Proceed : DXILOpClass;
  def rayQuery_StateMatrix : DXILOpClass;
  def rayQuery_StateScalar : DXILOpClass;
  def rayQuery_StateVector : DXILOpClass;
  def rayQuery_TraceRayInline : DXILOpClass;
  def rayTCurrent : DXILOpClass;
  def rayTMin : DXILOpClass;
  def renderTargetGetSampleCount : DXILOpClass;
  def renderTargetGetSamplePosition : DXILOpClass;
  def reportHit : DXILOpClass;
  def sample : DXILOpClass;
  def sampleBias : DXILOpClass;
  def sampleCmp : DXILOpClass;
  def sampleCmpBias : DXILOpClass;
  def sampleCmpGrad : DXILOpClass;
  def sampleCmpLevel : DXILOpClass;
  def sampleCmpLevelZero : DXILOpClass;
  def sampleGrad : DXILOpClass;
  def sampleIndex : DXILOpClass;
  def sampleLevel : DXILOpClass;
  def setMeshOutputCounts : DXILOpClass;
  def splitDouble : DXILOpClass;
  def startInstanceLocation : DXILOpClass;
  def startVertexLocation : DXILOpClass;
  def storeOutput : DXILOpClass;
  def storePatchConstant : DXILOpClass;
  def storePrimitiveOutput : DXILOpClass;
  def storeVertexOutput : DXILOpClass;
  def tempRegLoad : DXILOpClass;
  def tempRegStore : DXILOpClass;
  def tertiary : DXILOpClass;
  def texture2DMSGetSamplePosition : DXILOpClass;
  def textureGather : DXILOpClass;
  def textureGatherCmp : DXILOpClass;
  def textureGatherRaw : DXILOpClass;
  def textureLoad : DXILOpClass;
  def textureStore : DXILOpClass;
  def textureStoreSample : DXILOpClass;
  def threadId : DXILOpClass;
  def threadIdInGroup : DXILOpClass;
  def traceRay : DXILOpClass;
  def unary : DXILOpClass;
  def unaryBits : DXILOpClass;
  def unpack4x8 : DXILOpClass;
  def viewID : DXILOpClass;
  def waveActiveAllEqual : DXILOpClass;
  def waveActiveBallot : DXILOpClass;
  def waveActiveBit : DXILOpClass;
  def waveActiveOp : DXILOpClass;
  def waveAllOp : DXILOpClass;
  def waveAllTrue : DXILOpClass;
  def waveAnyTrue : DXILOpClass;
  def waveGetLaneCount : DXILOpClass;
  def waveGetLaneIndex : DXILOpClass;
  def waveIsFirstLane : DXILOpClass;
  def waveMatch : DXILOpClass;
  def waveMatrix_Accumulate : DXILOpClass;
  def waveMatrix_Annotate : DXILOpClass;
  def waveMatrix_Depth : DXILOpClass;
  def waveMatrix_Fill : DXILOpClass;
  def waveMatrix_LoadGroupShared : DXILOpClass;
  def waveMatrix_LoadRawBuf : DXILOpClass;
  def waveMatrix_Multiply : DXILOpClass;
  def waveMatrix_ScalarOp : DXILOpClass;
  def waveMatrix_StoreGroupShared : DXILOpClass;
  def waveMatrix_StoreRawBuf : DXILOpClass;
  def waveMultiPrefixBitCount : DXILOpClass;
  def waveMultiPrefixOp : DXILOpClass;
  def wavePrefixOp : DXILOpClass;
  def waveReadLaneAt : DXILOpClass;
  def waveReadLaneFirst : DXILOpClass;
  def worldRayDirection : DXILOpClass;
  def worldRayOrigin : DXILOpClass;
  def worldToObject : DXILOpClass;
  def writeSamplerFeedback : DXILOpClass;
  def writeSamplerFeedbackBias : DXILOpClass;
  def writeSamplerFeedbackGrad : DXILOpClass;
  def writeSamplerFeedbackLevel: DXILOpClass;

  // This is a sentinel definition. Hence placed at the end of the list
  // and not as part of the above alphabetically sorted valid definitions.
  // Additionally it is capitalized unlike all the others.
  def UnknownOpClass: DXILOpClass;
}

// Several of the overloaded DXIL Operations support for data types
// that are a subset of the overloaded LLVM intrinsics that they map to.
// For e.g., llvm.sin.* intrinsic operates on any floating-point type and
// maps for lowering to DXIL Op Sin. However, valid overloads of DXIL Sin
// operation overloads are half (f16) and float (f32) only.
//
// The following abstracts overload types specific to DXIL operations.

class DXILType : LLVMType<OtherVT> {
  let isAny = 1;
  int isI16OrI32 = 0;
  int isHalfOrFloat = 0;
}

// Concrete records for various overload types supported specifically by
// DXIL Operations.
let isI16OrI32 = 1 in
  def llvm_i16ori32_ty : DXILType;

let isHalfOrFloat = 1 in
  def llvm_halforfloat_ty : DXILType;

// Abstraction DXIL Operation to LLVM intrinsic
class DXILOpMappingBase {
  int OpCode = 0;                      // Opcode of DXIL Operation
  DXILOpClass OpClass = UnknownOpClass;// Class of DXIL Operation.
  Intrinsic LLVMIntrinsic = ?;         // LLVM Intrinsic DXIL Operation maps to
  string Doc = "";                     // A short description of the operation
  list<LLVMType> OpTypes = ?;          // Valid types of DXIL Operation in the
                                       // format [returnTy, param1ty, ...]
}

class DXILOpMapping<int opCode, DXILOpClass opClass,
                    Intrinsic intrinsic, string doc,
                    list<LLVMType> opTys = []> : DXILOpMappingBase {
  int OpCode = opCode;                 // Opcode corresponding to DXIL Operation
  DXILOpClass OpClass = opClass;       // Class of DXIL Operation.
  Intrinsic LLVMIntrinsic = intrinsic; // LLVM Intrinsic the DXIL Operation maps
  string Doc = doc;                    // to a short description of the operation
  list<LLVMType> OpTypes = !if(!eq(!size(opTys), 0), LLVMIntrinsic.Types, opTys);
}

// Concrete definition of DXIL Operation mapping to corresponding LLVM intrinsic
def Abs : DXILOpMapping<6, unary, int_fabs,
                         "Returns the absolute value of the input.">;
def IsInf : DXILOpMapping<9, isSpecialFloat, int_dx_isinf,
                         "Determines if the specified value is infinite.",
                         [llvm_i1_ty, llvm_halforfloat_ty]>;
def Cos  : DXILOpMapping<12, unary, int_cos,
                         "Returns cosine(theta) for theta in radians.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Sin  : DXILOpMapping<13, unary, int_sin,
                         "Returns sine(theta) for theta in radians.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Tan  : DXILOpMapping<14, unary, int_tan,
                         "Returns tangent(theta) for theta in radians.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def ACos  : DXILOpMapping<15, unary, int_acos,
                         "Returns the arccosine of each component of input.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def ASin  : DXILOpMapping<16, unary, int_asin,
                         "Returns the arcsine of each component of input.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def ATan  : DXILOpMapping<17, unary, int_atan,
                         "Returns the arctangent of each component of input.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def HCos  : DXILOpMapping<18, unary, int_cosh,
                         "Returns the hyperbolic cosine of the specified value.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def HSin  : DXILOpMapping<19, unary, int_sinh,
                         "Returns the hyperbolic sine of the specified value.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def HTan  : DXILOpMapping<20, unary, int_tanh,
                         "Returns the hyperbolic tan of the specified value.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;

def Exp2 : DXILOpMapping<21, unary, int_exp2,
                         "Returns the base 2 exponential, or 2**x, of the specified value."
                         "exp2(x) = 2**x.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Frac : DXILOpMapping<22, unary, int_dx_frac,
                         "Returns a fraction from 0 to 1 that represents the "
                         "decimal part of the input.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Log2 : DXILOpMapping<23, unary, int_log2,
                         "Returns the base-2 logarithm of the specified value.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Sqrt : DXILOpMapping<24, unary, int_sqrt,
                         "Returns the square root of the specified floating-point"
                         "value, per component.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def RSqrt : DXILOpMapping<25, unary, int_dx_rsqrt,
                         "Returns the reciprocal of the square root of the specified value."
                         "rsqrt(x) = 1 / sqrt(x).",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Round : DXILOpMapping<26, unary, int_roundeven,
                         "Returns the input rounded to the nearest integer"
                         "within a floating-point type.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Floor : DXILOpMapping<27, unary, int_floor,
                         "Returns the largest integer that is less than or equal to the input.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Ceil : DXILOpMapping<28, unary, int_ceil,
                         "Returns the smallest integer that is greater than or equal to the input.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Trunc : DXILOpMapping<29, unary, int_trunc,
                         "Returns the specified value truncated to the integer component.",
                         [llvm_halforfloat_ty, LLVMMatchType<0>]>;
def Rbits : DXILOpMapping<30, unary, int_bitreverse,
                         "Returns the specified value with its bits reversed.",
                         [llvm_anyint_ty, LLVMMatchType<0>]>;
def FMax : DXILOpMapping<35, binary, int_maxnum,
                         "Float maximum. FMax(a,b) = a > b ? a : b">;
def FMin : DXILOpMapping<36, binary, int_minnum,
                         "Float minimum. FMin(a,b) = a < b ? a : b">;
def SMax : DXILOpMapping<37, binary, int_smax,
                         "Signed integer maximum. SMax(a,b) = a > b ? a : b">;
def SMin : DXILOpMapping<38, binary, int_smin,
                         "Signed integer minimum. SMin(a,b) = a < b ? a : b">;
def UMax : DXILOpMapping<39, binary, int_umax,
                         "Unsigned integer maximum. UMax(a,b) = a > b ? a : b">;
def UMin : DXILOpMapping<40, binary, int_umin,
                         "Unsigned integer minimum. UMin(a,b) = a < b ? a : b">;
def FMad : DXILOpMapping<46, tertiary, int_fmuladd,
                         "Floating point arithmetic multiply/add operation. fmad(m,a,b) = m * a + b.">;
def IMad : DXILOpMapping<48, tertiary, int_dx_imad,
                         "Signed integer arithmetic multiply/add operation. imad(m,a,b) = m * a + b.">;
def UMad : DXILOpMapping<49, tertiary, int_dx_umad,
                         "Unsigned integer arithmetic multiply/add operation. umad(m,a,b) = m * a + b.">;
let OpTypes = !listconcat([llvm_halforfloat_ty], !listsplat(llvm_halforfloat_ty, 4)) in
  def Dot2 : DXILOpMapping<54, dot2, int_dx_dot2,
                           "dot product of two float vectors Dot(a,b) = a[0]*b[0] + ... + a[n]*b[n] where n is between 0 and 1">;
let OpTypes = !listconcat([llvm_halforfloat_ty], !listsplat(llvm_halforfloat_ty, 6)) in
  def Dot3 : DXILOpMapping<55, dot3, int_dx_dot3,
                           "dot product of two float vectors Dot(a,b) = a[0]*b[0] + ... + a[n]*b[n] where n is between 0 and 2">;
let OpTypes = !listconcat([llvm_halforfloat_ty], !listsplat(llvm_halforfloat_ty, 8)) in
  def Dot4 : DXILOpMapping<56, dot4, int_dx_dot4,
                           "dot product of two float vectors Dot(a,b) = a[0]*b[0] + ... + a[n]*b[n] where n is between 0 and 3">;
def ThreadId : DXILOpMapping<93, threadId, int_dx_thread_id,
                             "Reads the thread ID">;
def GroupId  : DXILOpMapping<94, groupId, int_dx_group_id,
                             "Reads the group ID (SV_GroupID)">;
def ThreadIdInGroup : DXILOpMapping<95, threadIdInGroup,
                                    int_dx_thread_id_in_group,
                                    "Reads the thread ID within the group "
                                    "(SV_GroupThreadID)">;
def FlattenedThreadIdInGroup : DXILOpMapping<96, flattenedThreadIdInGroup,
                                             int_dx_flattened_thread_id_in_group,
                                             "Provides a flattened index for a "
                                             "given thread within a given "
                                             "group (SV_GroupIndex)">;
