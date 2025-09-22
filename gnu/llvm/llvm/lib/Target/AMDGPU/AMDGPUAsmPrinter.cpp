//===-- AMDGPUAsmPrinter.cpp - AMDGPU assembly printer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// The AMDGPUAsmPrinter is used to print both assembly string and also binary
/// code.  When passed an MCAsmStreamer it prints assembly and when passed
/// an MCObjectStreamer it outputs binary code.
//
//===----------------------------------------------------------------------===//
//

#include "AMDGPUAsmPrinter.h"
#include "AMDGPU.h"
#include "AMDGPUHSAMetadataStreamer.h"
#include "AMDGPUResourceUsageAnalysis.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUInstPrinter.h"
#include "MCTargetDesc/AMDGPUMCExpr.h"
#include "MCTargetDesc/AMDGPUMCKernelDescriptor.h"
#include "MCTargetDesc/AMDGPUTargetStreamer.h"
#include "R600AsmPrinter.h"
#include "SIMachineFunctionInfo.h"
#include "TargetInfo/AMDGPUTargetInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "Utils/AMDKernelCodeTUtils.h"
#include "Utils/SIDefinesUtils.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/AMDHSAKernelDescriptor.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/TargetParser.h"

using namespace llvm;
using namespace llvm::AMDGPU;

// This should get the default rounding mode from the kernel. We just set the
// default here, but this could change if the OpenCL rounding mode pragmas are
// used.
//
// The denormal mode here should match what is reported by the OpenCL runtime
// for the CL_FP_DENORM bit from CL_DEVICE_{HALF|SINGLE|DOUBLE}_FP_CONFIG, but
// can also be override to flush with the -cl-denorms-are-zero compiler flag.
//
// AMD OpenCL only sets flush none and reports CL_FP_DENORM for double
// precision, and leaves single precision to flush all and does not report
// CL_FP_DENORM for CL_DEVICE_SINGLE_FP_CONFIG. Mesa's OpenCL currently reports
// CL_FP_DENORM for both.
//
// FIXME: It seems some instructions do not support single precision denormals
// regardless of the mode (exp_*_f32, rcp_*_f32, rsq_*_f32, rsq_*f32, sqrt_f32,
// and sin_f32, cos_f32 on most parts).

// We want to use these instructions, and using fp32 denormals also causes
// instructions to run at the double precision rate for the device so it's
// probably best to just report no single precision denormals.
static uint32_t getFPMode(SIModeRegisterDefaults Mode) {
  return FP_ROUND_MODE_SP(FP_ROUND_ROUND_TO_NEAREST) |
         FP_ROUND_MODE_DP(FP_ROUND_ROUND_TO_NEAREST) |
         FP_DENORM_MODE_SP(Mode.fpDenormModeSPValue()) |
         FP_DENORM_MODE_DP(Mode.fpDenormModeDPValue());
}

static AsmPrinter *
createAMDGPUAsmPrinterPass(TargetMachine &tm,
                           std::unique_ptr<MCStreamer> &&Streamer) {
  return new AMDGPUAsmPrinter(tm, std::move(Streamer));
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUAsmPrinter() {
  TargetRegistry::RegisterAsmPrinter(getTheR600Target(),
                                     llvm::createR600AsmPrinterPass);
  TargetRegistry::RegisterAsmPrinter(getTheGCNTarget(),
                                     createAMDGPUAsmPrinterPass);
}

AMDGPUAsmPrinter::AMDGPUAsmPrinter(TargetMachine &TM,
                                   std::unique_ptr<MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)) {
  assert(OutStreamer && "AsmPrinter constructed without streamer");
}

StringRef AMDGPUAsmPrinter::getPassName() const {
  return "AMDGPU Assembly Printer";
}

const MCSubtargetInfo *AMDGPUAsmPrinter::getGlobalSTI() const {
  return TM.getMCSubtargetInfo();
}

AMDGPUTargetStreamer* AMDGPUAsmPrinter::getTargetStreamer() const {
  if (!OutStreamer)
    return nullptr;
  return static_cast<AMDGPUTargetStreamer*>(OutStreamer->getTargetStreamer());
}

void AMDGPUAsmPrinter::emitStartOfAsmFile(Module &M) {
  IsTargetStreamerInitialized = false;
}

void AMDGPUAsmPrinter::initTargetStreamer(Module &M) {
  IsTargetStreamerInitialized = true;

  // TODO: Which one is called first, emitStartOfAsmFile or
  // emitFunctionBodyStart?
  if (getTargetStreamer() && !getTargetStreamer()->getTargetID())
    initializeTargetID(M);

  if (TM.getTargetTriple().getOS() != Triple::AMDHSA &&
      TM.getTargetTriple().getOS() != Triple::AMDPAL)
    return;

  getTargetStreamer()->EmitDirectiveAMDGCNTarget();

  if (TM.getTargetTriple().getOS() == Triple::AMDHSA) {
    getTargetStreamer()->EmitDirectiveAMDHSACodeObjectVersion(
        CodeObjectVersion);
    HSAMetadataStream->begin(M, *getTargetStreamer()->getTargetID());
  }

  if (TM.getTargetTriple().getOS() == Triple::AMDPAL)
    getTargetStreamer()->getPALMetadata()->readFromIR(M);
}

void AMDGPUAsmPrinter::emitEndOfAsmFile(Module &M) {
  // Init target streamer if it has not yet happened
  if (!IsTargetStreamerInitialized)
    initTargetStreamer(M);

  if (TM.getTargetTriple().getOS() != Triple::AMDHSA)
    getTargetStreamer()->EmitISAVersion();

  // Emit HSA Metadata (NT_AMD_AMDGPU_HSA_METADATA).
  // Emit HSA Metadata (NT_AMD_HSA_METADATA).
  if (TM.getTargetTriple().getOS() == Triple::AMDHSA) {
    HSAMetadataStream->end();
    bool Success = HSAMetadataStream->emitTo(*getTargetStreamer());
    (void)Success;
    assert(Success && "Malformed HSA Metadata");
  }
}

void AMDGPUAsmPrinter::emitFunctionBodyStart() {
  const SIMachineFunctionInfo &MFI = *MF->getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &STM = MF->getSubtarget<GCNSubtarget>();
  const Function &F = MF->getFunction();

  // TODO: We're checking this late, would be nice to check it earlier.
  if (STM.requiresCodeObjectV6() && CodeObjectVersion < AMDGPU::AMDHSA_COV6) {
    report_fatal_error(
        STM.getCPU() + " is only available on code object version 6 or better",
        /*gen_crash_diag*/ false);
  }

  // TODO: Which one is called first, emitStartOfAsmFile or
  // emitFunctionBodyStart?
  if (!getTargetStreamer()->getTargetID())
    initializeTargetID(*F.getParent());

  const auto &FunctionTargetID = STM.getTargetID();
  // Make sure function's xnack settings are compatible with module's
  // xnack settings.
  if (FunctionTargetID.isXnackSupported() &&
      FunctionTargetID.getXnackSetting() != IsaInfo::TargetIDSetting::Any &&
      FunctionTargetID.getXnackSetting() != getTargetStreamer()->getTargetID()->getXnackSetting()) {
    OutContext.reportError({}, "xnack setting of '" + Twine(MF->getName()) +
                           "' function does not match module xnack setting");
    return;
  }
  // Make sure function's sramecc settings are compatible with module's
  // sramecc settings.
  if (FunctionTargetID.isSramEccSupported() &&
      FunctionTargetID.getSramEccSetting() != IsaInfo::TargetIDSetting::Any &&
      FunctionTargetID.getSramEccSetting() != getTargetStreamer()->getTargetID()->getSramEccSetting()) {
    OutContext.reportError({}, "sramecc setting of '" + Twine(MF->getName()) +
                           "' function does not match module sramecc setting");
    return;
  }

  if (!MFI.isEntryFunction())
    return;

  if (STM.isMesaKernel(F) &&
      (F.getCallingConv() == CallingConv::AMDGPU_KERNEL ||
       F.getCallingConv() == CallingConv::SPIR_KERNEL)) {
    AMDGPUMCKernelCodeT KernelCode;
    getAmdKernelCode(KernelCode, CurrentProgramInfo, *MF);
    KernelCode.validate(&STM, MF->getContext());
    getTargetStreamer()->EmitAMDKernelCodeT(KernelCode);
  }

  if (STM.isAmdHsaOS())
    HSAMetadataStream->emitKernel(*MF, CurrentProgramInfo);

  if (MFI.getNumKernargPreloadedSGPRs() > 0) {
    assert(AMDGPU::hasKernargPreload(STM));
    getTargetStreamer()->EmitKernargPreloadHeader(*getGlobalSTI(),
                                                  STM.isAmdHsaOS());
  }
}

void AMDGPUAsmPrinter::emitFunctionBodyEnd() {
  const SIMachineFunctionInfo &MFI = *MF->getInfo<SIMachineFunctionInfo>();
  if (!MFI.isEntryFunction())
    return;

  if (TM.getTargetTriple().getOS() != Triple::AMDHSA)
    return;

  auto &Streamer = getTargetStreamer()->getStreamer();
  auto &Context = Streamer.getContext();
  auto &ObjectFileInfo = *Context.getObjectFileInfo();
  auto &ReadOnlySection = *ObjectFileInfo.getReadOnlySection();

  Streamer.pushSection();
  Streamer.switchSection(&ReadOnlySection);

  // CP microcode requires the kernel descriptor to be allocated on 64 byte
  // alignment.
  Streamer.emitValueToAlignment(Align(64), 0, 1, 0);
  ReadOnlySection.ensureMinAlignment(Align(64));

  const GCNSubtarget &STM = MF->getSubtarget<GCNSubtarget>();

  SmallString<128> KernelName;
  getNameWithPrefix(KernelName, &MF->getFunction());
  getTargetStreamer()->EmitAmdhsaKernelDescriptor(
      STM, KernelName, getAmdhsaKernelDescriptor(*MF, CurrentProgramInfo),
      CurrentProgramInfo.NumVGPRsForWavesPerEU,
      MCBinaryExpr::createSub(
          CurrentProgramInfo.NumSGPRsForWavesPerEU,
          AMDGPUMCExpr::createExtraSGPRs(
              CurrentProgramInfo.VCCUsed, CurrentProgramInfo.FlatUsed,
              getTargetStreamer()->getTargetID()->isXnackOnOrAny(), Context),
          Context),
      CurrentProgramInfo.VCCUsed, CurrentProgramInfo.FlatUsed);

  Streamer.popSection();
}

void AMDGPUAsmPrinter::emitImplicitDef(const MachineInstr *MI) const {
  Register RegNo = MI->getOperand(0).getReg();

  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  OS << "implicit-def: "
     << printReg(RegNo, MF->getSubtarget().getRegisterInfo());

  if (MI->getAsmPrinterFlags() & AMDGPU::SGPR_SPILL)
    OS << " : SGPR spill to VGPR lane";

  OutStreamer->AddComment(OS.str());
  OutStreamer->addBlankLine();
}

void AMDGPUAsmPrinter::emitFunctionEntryLabel() {
  if (TM.getTargetTriple().getOS() == Triple::AMDHSA) {
    AsmPrinter::emitFunctionEntryLabel();
    return;
  }

  const SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &STM = MF->getSubtarget<GCNSubtarget>();
  if (MFI->isEntryFunction() && STM.isAmdHsaOrMesa(MF->getFunction())) {
    SmallString<128> SymbolName;
    getNameWithPrefix(SymbolName, &MF->getFunction()),
    getTargetStreamer()->EmitAMDGPUSymbolType(
        SymbolName, ELF::STT_AMDGPU_HSA_KERNEL);
  }
  if (DumpCodeInstEmitter) {
    // Disassemble function name label to text.
    DisasmLines.push_back(MF->getName().str() + ":");
    DisasmLineMaxLen = std::max(DisasmLineMaxLen, DisasmLines.back().size());
    HexLines.emplace_back("");
  }

  AsmPrinter::emitFunctionEntryLabel();
}

void AMDGPUAsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  if (DumpCodeInstEmitter && !isBlockOnlyReachableByFallthrough(&MBB)) {
    // Write a line for the basic block label if it is not only fallthrough.
    DisasmLines.push_back(
        (Twine("BB") + Twine(getFunctionNumber())
         + "_" + Twine(MBB.getNumber()) + ":").str());
    DisasmLineMaxLen = std::max(DisasmLineMaxLen, DisasmLines.back().size());
    HexLines.emplace_back("");
  }
  AsmPrinter::emitBasicBlockStart(MBB);
}

void AMDGPUAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  if (GV->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS) {
    if (GV->hasInitializer() && !isa<UndefValue>(GV->getInitializer())) {
      OutContext.reportError({},
                             Twine(GV->getName()) +
                                 ": unsupported initializer for address space");
      return;
    }

    // LDS variables aren't emitted in HSA or PAL yet.
    const Triple::OSType OS = TM.getTargetTriple().getOS();
    if (OS == Triple::AMDHSA || OS == Triple::AMDPAL)
      return;

    MCSymbol *GVSym = getSymbol(GV);

    GVSym->redefineIfPossible();
    if (GVSym->isDefined() || GVSym->isVariable())
      report_fatal_error("symbol '" + Twine(GVSym->getName()) +
                         "' is already defined");

    const DataLayout &DL = GV->getDataLayout();
    uint64_t Size = DL.getTypeAllocSize(GV->getValueType());
    Align Alignment = GV->getAlign().value_or(Align(4));

    emitVisibility(GVSym, GV->getVisibility(), !GV->isDeclaration());
    emitLinkage(GV, GVSym);
    auto TS = getTargetStreamer();
    TS->emitAMDGPULDS(GVSym, Size, Alignment);
    return;
  }

  AsmPrinter::emitGlobalVariable(GV);
}

bool AMDGPUAsmPrinter::doInitialization(Module &M) {
  CodeObjectVersion = AMDGPU::getAMDHSACodeObjectVersion(M);

  if (TM.getTargetTriple().getOS() == Triple::AMDHSA) {
    switch (CodeObjectVersion) {
    case AMDGPU::AMDHSA_COV4:
      HSAMetadataStream = std::make_unique<HSAMD::MetadataStreamerMsgPackV4>();
      break;
    case AMDGPU::AMDHSA_COV5:
      HSAMetadataStream = std::make_unique<HSAMD::MetadataStreamerMsgPackV5>();
      break;
    case AMDGPU::AMDHSA_COV6:
      HSAMetadataStream = std::make_unique<HSAMD::MetadataStreamerMsgPackV6>();
      break;
    default:
      report_fatal_error("Unexpected code object version");
    }
  }
  return AsmPrinter::doInitialization(M);
}

bool AMDGPUAsmPrinter::doFinalization(Module &M) {
  // Pad with s_code_end to help tools and guard against instruction prefetch
  // causing stale data in caches. Arguably this should be done by the linker,
  // which is why this isn't done for Mesa.
  const MCSubtargetInfo &STI = *getGlobalSTI();
  if ((AMDGPU::isGFX10Plus(STI) || AMDGPU::isGFX90A(STI)) &&
      (STI.getTargetTriple().getOS() == Triple::AMDHSA ||
       STI.getTargetTriple().getOS() == Triple::AMDPAL)) {
    OutStreamer->switchSection(getObjFileLowering().getTextSection());
    getTargetStreamer()->EmitCodeEnd(STI);
  }

  return AsmPrinter::doFinalization(M);
}

// Print comments that apply to both callable functions and entry points.
void AMDGPUAsmPrinter::emitCommonFunctionComments(
    uint32_t NumVGPR, std::optional<uint32_t> NumAGPR, uint32_t TotalNumVGPR,
    uint32_t NumSGPR, uint64_t ScratchSize, uint64_t CodeSize,
    const AMDGPUMachineFunction *MFI) {
  OutStreamer->emitRawComment(" codeLenInByte = " + Twine(CodeSize), false);
  OutStreamer->emitRawComment(" NumSgprs: " + Twine(NumSGPR), false);
  OutStreamer->emitRawComment(" NumVgprs: " + Twine(NumVGPR), false);
  if (NumAGPR) {
    OutStreamer->emitRawComment(" NumAgprs: " + Twine(*NumAGPR), false);
    OutStreamer->emitRawComment(" TotalNumVgprs: " + Twine(TotalNumVGPR),
                                false);
  }
  OutStreamer->emitRawComment(" ScratchSize: " + Twine(ScratchSize), false);
  OutStreamer->emitRawComment(" MemoryBound: " + Twine(MFI->isMemoryBound()),
                              false);
}

SmallString<128> AMDGPUAsmPrinter::getMCExprStr(const MCExpr *Value) {
  SmallString<128> Str;
  raw_svector_ostream OSS(Str);
  int64_t IVal;
  if (Value->evaluateAsAbsolute(IVal)) {
    OSS << static_cast<uint64_t>(IVal);
  } else {
    Value->print(OSS, MAI);
  }
  return Str;
}

void AMDGPUAsmPrinter::emitCommonFunctionComments(
    const MCExpr *NumVGPR, const MCExpr *NumAGPR, const MCExpr *TotalNumVGPR,
    const MCExpr *NumSGPR, const MCExpr *ScratchSize, uint64_t CodeSize,
    const AMDGPUMachineFunction *MFI) {
  OutStreamer->emitRawComment(" codeLenInByte = " + Twine(CodeSize), false);
  OutStreamer->emitRawComment(" NumSgprs: " + getMCExprStr(NumSGPR), false);
  OutStreamer->emitRawComment(" NumVgprs: " + getMCExprStr(NumVGPR), false);
  if (NumAGPR && TotalNumVGPR) {
    OutStreamer->emitRawComment(" NumAgprs: " + getMCExprStr(NumAGPR), false);
    OutStreamer->emitRawComment(" TotalNumVgprs: " + getMCExprStr(TotalNumVGPR),
                                false);
  }
  OutStreamer->emitRawComment(" ScratchSize: " + getMCExprStr(ScratchSize),
                              false);
  OutStreamer->emitRawComment(" MemoryBound: " + Twine(MFI->isMemoryBound()),
                              false);
}

const MCExpr *AMDGPUAsmPrinter::getAmdhsaKernelCodeProperties(
    const MachineFunction &MF) const {
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  MCContext &Ctx = MF.getContext();
  uint16_t KernelCodeProperties = 0;
  const GCNUserSGPRUsageInfo &UserSGPRInfo = MFI.getUserSGPRInfo();

  if (UserSGPRInfo.hasPrivateSegmentBuffer()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER;
  }
  if (UserSGPRInfo.hasDispatchPtr()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR;
  }
  if (UserSGPRInfo.hasQueuePtr() && CodeObjectVersion < AMDGPU::AMDHSA_COV5) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR;
  }
  if (UserSGPRInfo.hasKernargSegmentPtr()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR;
  }
  if (UserSGPRInfo.hasDispatchID()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID;
  }
  if (UserSGPRInfo.hasFlatScratchInit()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT;
  }
  if (UserSGPRInfo.hasPrivateSegmentSize()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE;
  }
  if (MF.getSubtarget<GCNSubtarget>().isWave32()) {
    KernelCodeProperties |=
        amdhsa::KERNEL_CODE_PROPERTY_ENABLE_WAVEFRONT_SIZE32;
  }

  // CurrentProgramInfo.DynamicCallStack is a MCExpr and could be
  // un-evaluatable at this point so it cannot be conditionally checked here.
  // Instead, we'll directly shift the possibly unknown MCExpr into its place
  // and bitwise-or it into KernelCodeProperties.
  const MCExpr *KernelCodePropExpr =
      MCConstantExpr::create(KernelCodeProperties, Ctx);
  const MCExpr *OrValue = MCConstantExpr::create(
      amdhsa::KERNEL_CODE_PROPERTY_USES_DYNAMIC_STACK_SHIFT, Ctx);
  OrValue = MCBinaryExpr::createShl(CurrentProgramInfo.DynamicCallStack,
                                    OrValue, Ctx);
  KernelCodePropExpr = MCBinaryExpr::createOr(KernelCodePropExpr, OrValue, Ctx);

  return KernelCodePropExpr;
}

MCKernelDescriptor
AMDGPUAsmPrinter::getAmdhsaKernelDescriptor(const MachineFunction &MF,
                                            const SIProgramInfo &PI) const {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  const Function &F = MF.getFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  MCContext &Ctx = MF.getContext();

  MCKernelDescriptor KernelDescriptor;

  KernelDescriptor.group_segment_fixed_size =
      MCConstantExpr::create(PI.LDSSize, Ctx);
  KernelDescriptor.private_segment_fixed_size = PI.ScratchSize;

  Align MaxKernArgAlign;
  KernelDescriptor.kernarg_size = MCConstantExpr::create(
      STM.getKernArgSegmentSize(F, MaxKernArgAlign), Ctx);

  KernelDescriptor.compute_pgm_rsrc1 = PI.getComputePGMRSrc1(STM, Ctx);
  KernelDescriptor.compute_pgm_rsrc2 = PI.getComputePGMRSrc2(Ctx);
  KernelDescriptor.kernel_code_properties = getAmdhsaKernelCodeProperties(MF);

  int64_t PGRM_Rsrc3 = 1;
  bool EvaluatableRsrc3 =
      CurrentProgramInfo.ComputePGMRSrc3GFX90A->evaluateAsAbsolute(PGRM_Rsrc3);
  (void)PGRM_Rsrc3;
  (void)EvaluatableRsrc3;
  assert(STM.hasGFX90AInsts() || !EvaluatableRsrc3 ||
         static_cast<uint64_t>(PGRM_Rsrc3) == 0);
  KernelDescriptor.compute_pgm_rsrc3 = CurrentProgramInfo.ComputePGMRSrc3GFX90A;

  KernelDescriptor.kernarg_preload = MCConstantExpr::create(
      AMDGPU::hasKernargPreload(STM) ? Info->getNumKernargPreloadedSGPRs() : 0,
      Ctx);

  return KernelDescriptor;
}

bool AMDGPUAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  // Init target streamer lazily on the first function so that previous passes
  // can set metadata.
  if (!IsTargetStreamerInitialized)
    initTargetStreamer(*MF.getFunction().getParent());

  ResourceUsage = &getAnalysis<AMDGPUResourceUsageAnalysis>();
  CurrentProgramInfo.reset(MF);

  const AMDGPUMachineFunction *MFI = MF.getInfo<AMDGPUMachineFunction>();
  MCContext &Ctx = MF.getContext();

  // The starting address of all shader programs must be 256 bytes aligned.
  // Regular functions just need the basic required instruction alignment.
  MF.setAlignment(MFI->isEntryFunction() ? Align(256) : Align(4));

  SetupMachineFunction(MF);

  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  MCContext &Context = getObjFileLowering().getContext();
  // FIXME: This should be an explicit check for Mesa.
  if (!STM.isAmdHsaOS() && !STM.isAmdPalOS()) {
    MCSectionELF *ConfigSection =
        Context.getELFSection(".AMDGPU.config", ELF::SHT_PROGBITS, 0);
    OutStreamer->switchSection(ConfigSection);
  }

  if (MFI->isModuleEntryFunction()) {
    getSIProgramInfo(CurrentProgramInfo, MF);
  }

  if (STM.isAmdPalOS()) {
    if (MFI->isEntryFunction())
      EmitPALMetadata(MF, CurrentProgramInfo);
    else if (MFI->isModuleEntryFunction())
      emitPALFunctionMetadata(MF);
  } else if (!STM.isAmdHsaOS()) {
    EmitProgramInfoSI(MF, CurrentProgramInfo);
  }

  DumpCodeInstEmitter = nullptr;
  if (STM.dumpCode()) {
    // For -dumpcode, get the assembler out of the streamer. This only works
    // with -filetype=obj.
    MCAssembler *Assembler = OutStreamer->getAssemblerPtr();
    if (Assembler)
      DumpCodeInstEmitter = Assembler->getEmitterPtr();
  }

  DisasmLines.clear();
  HexLines.clear();
  DisasmLineMaxLen = 0;

  emitFunctionBody();

  emitResourceUsageRemarks(MF, CurrentProgramInfo, MFI->isModuleEntryFunction(),
                           STM.hasMAIInsts());

  if (isVerbose()) {
    MCSectionELF *CommentSection =
        Context.getELFSection(".AMDGPU.csdata", ELF::SHT_PROGBITS, 0);
    OutStreamer->switchSection(CommentSection);

    if (!MFI->isEntryFunction()) {
      OutStreamer->emitRawComment(" Function info:", false);
      const AMDGPUResourceUsageAnalysis::SIFunctionResourceInfo &Info =
          ResourceUsage->getResourceInfo(&MF.getFunction());
      emitCommonFunctionComments(
          Info.NumVGPR,
          STM.hasMAIInsts() ? Info.NumAGPR : std::optional<uint32_t>(),
          Info.getTotalNumVGPRs(STM),
          Info.getTotalNumSGPRs(MF.getSubtarget<GCNSubtarget>()),
          Info.PrivateSegmentSize, getFunctionCodeSize(MF), MFI);
      return false;
    }

    OutStreamer->emitRawComment(" Kernel info:", false);
    emitCommonFunctionComments(
        CurrentProgramInfo.NumArchVGPR,
        STM.hasMAIInsts() ? CurrentProgramInfo.NumAccVGPR : nullptr,
        CurrentProgramInfo.NumVGPR, CurrentProgramInfo.NumSGPR,
        CurrentProgramInfo.ScratchSize, getFunctionCodeSize(MF), MFI);

    OutStreamer->emitRawComment(
      " FloatMode: " + Twine(CurrentProgramInfo.FloatMode), false);
    OutStreamer->emitRawComment(
      " IeeeMode: " + Twine(CurrentProgramInfo.IEEEMode), false);
    OutStreamer->emitRawComment(
      " LDSByteSize: " + Twine(CurrentProgramInfo.LDSSize) +
      " bytes/workgroup (compile time only)", false);

    OutStreamer->emitRawComment(
        " SGPRBlocks: " + getMCExprStr(CurrentProgramInfo.SGPRBlocks), false);

    OutStreamer->emitRawComment(
        " VGPRBlocks: " + getMCExprStr(CurrentProgramInfo.VGPRBlocks), false);

    OutStreamer->emitRawComment(
        " NumSGPRsForWavesPerEU: " +
            getMCExprStr(CurrentProgramInfo.NumSGPRsForWavesPerEU),
        false);
    OutStreamer->emitRawComment(
        " NumVGPRsForWavesPerEU: " +
            getMCExprStr(CurrentProgramInfo.NumVGPRsForWavesPerEU),
        false);

    if (STM.hasGFX90AInsts()) {
      const MCExpr *AdjustedAccum = MCBinaryExpr::createAdd(
          CurrentProgramInfo.AccumOffset, MCConstantExpr::create(1, Ctx), Ctx);
      AdjustedAccum = MCBinaryExpr::createMul(
          AdjustedAccum, MCConstantExpr::create(4, Ctx), Ctx);
      OutStreamer->emitRawComment(
          " AccumOffset: " + getMCExprStr(AdjustedAccum), false);
    }

    OutStreamer->emitRawComment(
        " Occupancy: " + getMCExprStr(CurrentProgramInfo.Occupancy), false);

    OutStreamer->emitRawComment(
      " WaveLimiterHint : " + Twine(MFI->needsWaveLimiter()), false);

    OutStreamer->emitRawComment(
        " COMPUTE_PGM_RSRC2:SCRATCH_EN: " +
            getMCExprStr(CurrentProgramInfo.ScratchEnable),
        false);
    OutStreamer->emitRawComment(" COMPUTE_PGM_RSRC2:USER_SGPR: " +
                                    Twine(CurrentProgramInfo.UserSGPR),
                                false);
    OutStreamer->emitRawComment(" COMPUTE_PGM_RSRC2:TRAP_HANDLER: " +
                                    Twine(CurrentProgramInfo.TrapHandlerEnable),
                                false);
    OutStreamer->emitRawComment(" COMPUTE_PGM_RSRC2:TGID_X_EN: " +
                                    Twine(CurrentProgramInfo.TGIdXEnable),
                                false);
    OutStreamer->emitRawComment(" COMPUTE_PGM_RSRC2:TGID_Y_EN: " +
                                    Twine(CurrentProgramInfo.TGIdYEnable),
                                false);
    OutStreamer->emitRawComment(" COMPUTE_PGM_RSRC2:TGID_Z_EN: " +
                                    Twine(CurrentProgramInfo.TGIdZEnable),
                                false);
    OutStreamer->emitRawComment(" COMPUTE_PGM_RSRC2:TIDIG_COMP_CNT: " +
                                    Twine(CurrentProgramInfo.TIdIGCompCount),
                                false);

    [[maybe_unused]] int64_t PGMRSrc3;
    assert(STM.hasGFX90AInsts() ||
           (CurrentProgramInfo.ComputePGMRSrc3GFX90A->evaluateAsAbsolute(
                PGMRSrc3) &&
            static_cast<uint64_t>(PGMRSrc3) == 0));
    if (STM.hasGFX90AInsts()) {
      OutStreamer->emitRawComment(
          " COMPUTE_PGM_RSRC3_GFX90A:ACCUM_OFFSET: " +
              getMCExprStr(MCKernelDescriptor::bits_get(
                  CurrentProgramInfo.ComputePGMRSrc3GFX90A,
                  amdhsa::COMPUTE_PGM_RSRC3_GFX90A_ACCUM_OFFSET_SHIFT,
                  amdhsa::COMPUTE_PGM_RSRC3_GFX90A_ACCUM_OFFSET, Ctx)),
          false);
      OutStreamer->emitRawComment(
          " COMPUTE_PGM_RSRC3_GFX90A:TG_SPLIT: " +
              getMCExprStr(MCKernelDescriptor::bits_get(
                  CurrentProgramInfo.ComputePGMRSrc3GFX90A,
                  amdhsa::COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT_SHIFT,
                  amdhsa::COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT, Ctx)),
          false);
    }
  }

  if (DumpCodeInstEmitter) {

    OutStreamer->switchSection(
        Context.getELFSection(".AMDGPU.disasm", ELF::SHT_PROGBITS, 0));

    for (size_t i = 0; i < DisasmLines.size(); ++i) {
      std::string Comment = "\n";
      if (!HexLines[i].empty()) {
        Comment = std::string(DisasmLineMaxLen - DisasmLines[i].size(), ' ');
        Comment += " ; " + HexLines[i] + "\n";
      }

      OutStreamer->emitBytes(StringRef(DisasmLines[i]));
      OutStreamer->emitBytes(StringRef(Comment));
    }
  }

  return false;
}

// TODO: Fold this into emitFunctionBodyStart.
void AMDGPUAsmPrinter::initializeTargetID(const Module &M) {
  // In the beginning all features are either 'Any' or 'NotSupported',
  // depending on global target features. This will cover empty modules.
  getTargetStreamer()->initializeTargetID(*getGlobalSTI(),
                                          getGlobalSTI()->getFeatureString());

  // If module is empty, we are done.
  if (M.empty())
    return;

  // If module is not empty, need to find first 'Off' or 'On' feature
  // setting per feature from functions in module.
  for (auto &F : M) {
    auto &TSTargetID = getTargetStreamer()->getTargetID();
    if ((!TSTargetID->isXnackSupported() || TSTargetID->isXnackOnOrOff()) &&
        (!TSTargetID->isSramEccSupported() || TSTargetID->isSramEccOnOrOff()))
      break;

    const GCNSubtarget &STM = TM.getSubtarget<GCNSubtarget>(F);
    const IsaInfo::AMDGPUTargetID &STMTargetID = STM.getTargetID();
    if (TSTargetID->isXnackSupported())
      if (TSTargetID->getXnackSetting() == IsaInfo::TargetIDSetting::Any)
        TSTargetID->setXnackSetting(STMTargetID.getXnackSetting());
    if (TSTargetID->isSramEccSupported())
      if (TSTargetID->getSramEccSetting() == IsaInfo::TargetIDSetting::Any)
        TSTargetID->setSramEccSetting(STMTargetID.getSramEccSetting());
  }
}

uint64_t AMDGPUAsmPrinter::getFunctionCodeSize(const MachineFunction &MF) const {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = STM.getInstrInfo();

  uint64_t CodeSize = 0;

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      // TODO: CodeSize should account for multiple functions.

      // TODO: Should we count size of debug info?
      if (MI.isDebugInstr())
        continue;

      CodeSize += TII->getInstSizeInBytes(MI);
    }
  }

  return CodeSize;
}

void AMDGPUAsmPrinter::getSIProgramInfo(SIProgramInfo &ProgInfo,
                                        const MachineFunction &MF) {
  const AMDGPUResourceUsageAnalysis::SIFunctionResourceInfo &Info =
      ResourceUsage->getResourceInfo(&MF.getFunction());
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  MCContext &Ctx = MF.getContext();

  auto CreateExpr = [&Ctx](int64_t Value) {
    return MCConstantExpr::create(Value, Ctx);
  };

  auto TryGetMCExprValue = [](const MCExpr *Value, uint64_t &Res) -> bool {
    int64_t Val;
    if (Value->evaluateAsAbsolute(Val)) {
      Res = Val;
      return true;
    }
    return false;
  };

  ProgInfo.NumArchVGPR = CreateExpr(Info.NumVGPR);
  ProgInfo.NumAccVGPR = CreateExpr(Info.NumAGPR);
  ProgInfo.NumVGPR = CreateExpr(Info.getTotalNumVGPRs(STM));
  ProgInfo.AccumOffset =
      CreateExpr(alignTo(std::max(1, Info.NumVGPR), 4) / 4 - 1);
  ProgInfo.TgSplit = STM.isTgSplitEnabled();
  ProgInfo.NumSGPR = CreateExpr(Info.NumExplicitSGPR);
  ProgInfo.ScratchSize = CreateExpr(Info.PrivateSegmentSize);
  ProgInfo.VCCUsed = CreateExpr(Info.UsesVCC);
  ProgInfo.FlatUsed = CreateExpr(Info.UsesFlatScratch);
  ProgInfo.DynamicCallStack =
      CreateExpr(Info.HasDynamicallySizedStack || Info.HasRecursion);

  const uint64_t MaxScratchPerWorkitem =
      STM.getMaxWaveScratchSize() / STM.getWavefrontSize();
  uint64_t ScratchSize;
  if (TryGetMCExprValue(ProgInfo.ScratchSize, ScratchSize) &&
      ScratchSize > MaxScratchPerWorkitem) {
    DiagnosticInfoStackSize DiagStackSize(MF.getFunction(), ScratchSize,
                                          MaxScratchPerWorkitem, DS_Error);
    MF.getFunction().getContext().diagnose(DiagStackSize);
  }

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // The calculations related to SGPR/VGPR blocks are
  // duplicated in part in AMDGPUAsmParser::calculateGPRBlocks, and could be
  // unified.
  const MCExpr *ExtraSGPRs = AMDGPUMCExpr::createExtraSGPRs(
      ProgInfo.VCCUsed, ProgInfo.FlatUsed,
      getTargetStreamer()->getTargetID()->isXnackOnOrAny(), Ctx);

  // Check the addressable register limit before we add ExtraSGPRs.
  if (STM.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS &&
      !STM.hasSGPRInitBug()) {
    unsigned MaxAddressableNumSGPRs = STM.getAddressableNumSGPRs();
    uint64_t NumSgpr;
    if (TryGetMCExprValue(ProgInfo.NumSGPR, NumSgpr) &&
        NumSgpr > MaxAddressableNumSGPRs) {
      // This can happen due to a compiler bug or when using inline asm.
      LLVMContext &Ctx = MF.getFunction().getContext();
      DiagnosticInfoResourceLimit Diag(
          MF.getFunction(), "addressable scalar registers", NumSgpr,
          MaxAddressableNumSGPRs, DS_Error, DK_ResourceLimit);
      Ctx.diagnose(Diag);
      ProgInfo.NumSGPR = CreateExpr(MaxAddressableNumSGPRs - 1);
    }
  }

  // Account for extra SGPRs and VGPRs reserved for debugger use.
  ProgInfo.NumSGPR = MCBinaryExpr::createAdd(ProgInfo.NumSGPR, ExtraSGPRs, Ctx);

  const Function &F = MF.getFunction();

  // Ensure there are enough SGPRs and VGPRs for wave dispatch, where wave
  // dispatch registers are function args.
  unsigned WaveDispatchNumSGPR = 0, WaveDispatchNumVGPR = 0;

  if (isShader(F.getCallingConv())) {
    bool IsPixelShader =
        F.getCallingConv() == CallingConv::AMDGPU_PS && !STM.isAmdHsaOS();

    // Calculate the number of VGPR registers based on the SPI input registers
    uint32_t InputEna = 0;
    uint32_t InputAddr = 0;
    unsigned LastEna = 0;

    if (IsPixelShader) {
      // Note for IsPixelShader:
      // By this stage, all enabled inputs are tagged in InputAddr as well.
      // We will use InputAddr to determine whether the input counts against the
      // vgpr total and only use the InputEnable to determine the last input
      // that is relevant - if extra arguments are used, then we have to honour
      // the InputAddr for any intermediate non-enabled inputs.
      InputEna = MFI->getPSInputEnable();
      InputAddr = MFI->getPSInputAddr();

      // We only need to consider input args up to the last used arg.
      assert((InputEna || InputAddr) &&
             "PSInputAddr and PSInputEnable should "
             "never both be 0 for AMDGPU_PS shaders");
      // There are some rare circumstances where InputAddr is non-zero and
      // InputEna can be set to 0. In this case we default to setting LastEna
      // to 1.
      LastEna = InputEna ? llvm::Log2_32(InputEna) + 1 : 1;
    }

    // FIXME: We should be using the number of registers determined during
    // calling convention lowering to legalize the types.
    const DataLayout &DL = F.getDataLayout();
    unsigned PSArgCount = 0;
    unsigned IntermediateVGPR = 0;
    for (auto &Arg : F.args()) {
      unsigned NumRegs = (DL.getTypeSizeInBits(Arg.getType()) + 31) / 32;
      if (Arg.hasAttribute(Attribute::InReg)) {
        WaveDispatchNumSGPR += NumRegs;
      } else {
        // If this is a PS shader and we're processing the PS Input args (first
        // 16 VGPR), use the InputEna and InputAddr bits to define how many
        // VGPRs are actually used.
        // Any extra VGPR arguments are handled as normal arguments (and
        // contribute to the VGPR count whether they're used or not).
        if (IsPixelShader && PSArgCount < 16) {
          if ((1 << PSArgCount) & InputAddr) {
            if (PSArgCount < LastEna)
              WaveDispatchNumVGPR += NumRegs;
            else
              IntermediateVGPR += NumRegs;
          }
          PSArgCount++;
        } else {
          // If there are extra arguments we have to include the allocation for
          // the non-used (but enabled with InputAddr) input arguments
          if (IntermediateVGPR) {
            WaveDispatchNumVGPR += IntermediateVGPR;
            IntermediateVGPR = 0;
          }
          WaveDispatchNumVGPR += NumRegs;
        }
      }
    }
    ProgInfo.NumSGPR = AMDGPUMCExpr::createMax(
        {ProgInfo.NumSGPR, CreateExpr(WaveDispatchNumSGPR)}, Ctx);

    ProgInfo.NumArchVGPR = AMDGPUMCExpr::createMax(
        {ProgInfo.NumVGPR, CreateExpr(WaveDispatchNumVGPR)}, Ctx);

    ProgInfo.NumVGPR = AMDGPUMCExpr::createTotalNumVGPR(
        ProgInfo.NumAccVGPR, ProgInfo.NumArchVGPR, Ctx);
  }

  // Adjust number of registers used to meet default/requested minimum/maximum
  // number of waves per execution unit request.
  unsigned MaxWaves = MFI->getMaxWavesPerEU();
  ProgInfo.NumSGPRsForWavesPerEU =
      AMDGPUMCExpr::createMax({ProgInfo.NumSGPR, CreateExpr(1ul),
                               CreateExpr(STM.getMinNumSGPRs(MaxWaves))},
                              Ctx);
  ProgInfo.NumVGPRsForWavesPerEU =
      AMDGPUMCExpr::createMax({ProgInfo.NumVGPR, CreateExpr(1ul),
                               CreateExpr(STM.getMinNumVGPRs(MaxWaves))},
                              Ctx);

  if (STM.getGeneration() <= AMDGPUSubtarget::SEA_ISLANDS ||
      STM.hasSGPRInitBug()) {
    unsigned MaxAddressableNumSGPRs = STM.getAddressableNumSGPRs();
    uint64_t NumSgpr;
    if (TryGetMCExprValue(ProgInfo.NumSGPR, NumSgpr) &&
        NumSgpr > MaxAddressableNumSGPRs) {
      // This can happen due to a compiler bug or when using inline asm to use
      // the registers which are usually reserved for vcc etc.
      LLVMContext &Ctx = MF.getFunction().getContext();
      DiagnosticInfoResourceLimit Diag(MF.getFunction(), "scalar registers",
                                       NumSgpr, MaxAddressableNumSGPRs,
                                       DS_Error, DK_ResourceLimit);
      Ctx.diagnose(Diag);
      ProgInfo.NumSGPR = CreateExpr(MaxAddressableNumSGPRs);
      ProgInfo.NumSGPRsForWavesPerEU = CreateExpr(MaxAddressableNumSGPRs);
    }
  }

  if (STM.hasSGPRInitBug()) {
    ProgInfo.NumSGPR =
        CreateExpr(AMDGPU::IsaInfo::FIXED_NUM_SGPRS_FOR_INIT_BUG);
    ProgInfo.NumSGPRsForWavesPerEU =
        CreateExpr(AMDGPU::IsaInfo::FIXED_NUM_SGPRS_FOR_INIT_BUG);
  }

  if (MFI->getNumUserSGPRs() > STM.getMaxNumUserSGPRs()) {
    LLVMContext &Ctx = MF.getFunction().getContext();
    DiagnosticInfoResourceLimit Diag(MF.getFunction(), "user SGPRs",
                                     MFI->getNumUserSGPRs(),
                                     STM.getMaxNumUserSGPRs(), DS_Error);
    Ctx.diagnose(Diag);
  }

  if (MFI->getLDSSize() >
      static_cast<unsigned>(STM.getAddressableLocalMemorySize())) {
    LLVMContext &Ctx = MF.getFunction().getContext();
    DiagnosticInfoResourceLimit Diag(
        MF.getFunction(), "local memory", MFI->getLDSSize(),
        STM.getAddressableLocalMemorySize(), DS_Error);
    Ctx.diagnose(Diag);
  }
  // The MCExpr equivalent of getNumSGPRBlocks/getNumVGPRBlocks:
  // (alignTo(max(1u, NumGPR), GPREncodingGranule) / GPREncodingGranule) - 1
  auto GetNumGPRBlocks = [&CreateExpr, &Ctx](const MCExpr *NumGPR,
                                             unsigned Granule) {
    const MCExpr *OneConst = CreateExpr(1ul);
    const MCExpr *GranuleConst = CreateExpr(Granule);
    const MCExpr *MaxNumGPR = AMDGPUMCExpr::createMax({NumGPR, OneConst}, Ctx);
    const MCExpr *AlignToGPR =
        AMDGPUMCExpr::createAlignTo(MaxNumGPR, GranuleConst, Ctx);
    const MCExpr *DivGPR =
        MCBinaryExpr::createDiv(AlignToGPR, GranuleConst, Ctx);
    const MCExpr *SubGPR = MCBinaryExpr::createSub(DivGPR, OneConst, Ctx);
    return SubGPR;
  };

  ProgInfo.SGPRBlocks = GetNumGPRBlocks(ProgInfo.NumSGPRsForWavesPerEU,
                                        IsaInfo::getSGPREncodingGranule(&STM));
  ProgInfo.VGPRBlocks = GetNumGPRBlocks(ProgInfo.NumVGPRsForWavesPerEU,
                                        IsaInfo::getVGPREncodingGranule(&STM));

  const SIModeRegisterDefaults Mode = MFI->getMode();

  // Set the value to initialize FP_ROUND and FP_DENORM parts of the mode
  // register.
  ProgInfo.FloatMode = getFPMode(Mode);

  ProgInfo.IEEEMode = Mode.IEEE;

  // Make clamp modifier on NaN input returns 0.
  ProgInfo.DX10Clamp = Mode.DX10Clamp;

  unsigned LDSAlignShift;
  if (STM.getGeneration() < AMDGPUSubtarget::SEA_ISLANDS) {
    // LDS is allocated in 64 dword blocks.
    LDSAlignShift = 8;
  } else {
    // LDS is allocated in 128 dword blocks.
    LDSAlignShift = 9;
  }

  ProgInfo.SGPRSpill = MFI->getNumSpilledSGPRs();
  ProgInfo.VGPRSpill = MFI->getNumSpilledVGPRs();

  ProgInfo.LDSSize = MFI->getLDSSize();
  ProgInfo.LDSBlocks =
      alignTo(ProgInfo.LDSSize, 1ULL << LDSAlignShift) >> LDSAlignShift;

  // The MCExpr equivalent of divideCeil.
  auto DivideCeil = [&Ctx](const MCExpr *Numerator, const MCExpr *Denominator) {
    const MCExpr *Ceil =
        AMDGPUMCExpr::createAlignTo(Numerator, Denominator, Ctx);
    return MCBinaryExpr::createDiv(Ceil, Denominator, Ctx);
  };

  // Scratch is allocated in 64-dword or 256-dword blocks.
  unsigned ScratchAlignShift =
      STM.getGeneration() >= AMDGPUSubtarget::GFX11 ? 8 : 10;
  // We need to program the hardware with the amount of scratch memory that
  // is used by the entire wave.  ProgInfo.ScratchSize is the amount of
  // scratch memory used per thread.
  ProgInfo.ScratchBlocks = DivideCeil(
      MCBinaryExpr::createMul(ProgInfo.ScratchSize,
                              CreateExpr(STM.getWavefrontSize()), Ctx),
      CreateExpr(1ULL << ScratchAlignShift));

  if (getIsaVersion(getGlobalSTI()->getCPU()).Major >= 10) {
    ProgInfo.WgpMode = STM.isCuModeEnabled() ? 0 : 1;
    ProgInfo.MemOrdered = 1;
  }

  // 0 = X, 1 = XY, 2 = XYZ
  unsigned TIDIGCompCnt = 0;
  if (MFI->hasWorkItemIDZ())
    TIDIGCompCnt = 2;
  else if (MFI->hasWorkItemIDY())
    TIDIGCompCnt = 1;

  // The private segment wave byte offset is the last of the system SGPRs. We
  // initially assumed it was allocated, and may have used it. It shouldn't harm
  // anything to disable it if we know the stack isn't used here. We may still
  // have emitted code reading it to initialize scratch, but if that's unused
  // reading garbage should be OK.
  ProgInfo.ScratchEnable = MCBinaryExpr::createLOr(
      MCBinaryExpr::createGT(ProgInfo.ScratchBlocks,
                             MCConstantExpr::create(0, Ctx), Ctx),
      ProgInfo.DynamicCallStack, Ctx);

  ProgInfo.UserSGPR = MFI->getNumUserSGPRs();
  // For AMDHSA, TRAP_HANDLER must be zero, as it is populated by the CP.
  ProgInfo.TrapHandlerEnable =
      STM.isAmdHsaOS() ? 0 : STM.isTrapHandlerEnabled();
  ProgInfo.TGIdXEnable = MFI->hasWorkGroupIDX();
  ProgInfo.TGIdYEnable = MFI->hasWorkGroupIDY();
  ProgInfo.TGIdZEnable = MFI->hasWorkGroupIDZ();
  ProgInfo.TGSizeEnable = MFI->hasWorkGroupInfo();
  ProgInfo.TIdIGCompCount = TIDIGCompCnt;
  ProgInfo.EXCPEnMSB = 0;
  // For AMDHSA, LDS_SIZE must be zero, as it is populated by the CP.
  ProgInfo.LdsSize = STM.isAmdHsaOS() ? 0 : ProgInfo.LDSBlocks;
  ProgInfo.EXCPEnable = 0;

  if (STM.hasGFX90AInsts()) {
    // return ((Dst & ~Mask) | (Value << Shift))
    auto SetBits = [&Ctx](const MCExpr *Dst, const MCExpr *Value, uint32_t Mask,
                          uint32_t Shift) {
      auto Shft = MCConstantExpr::create(Shift, Ctx);
      auto Msk = MCConstantExpr::create(Mask, Ctx);
      Dst = MCBinaryExpr::createAnd(Dst, MCUnaryExpr::createNot(Msk, Ctx), Ctx);
      Dst = MCBinaryExpr::createOr(
          Dst, MCBinaryExpr::createShl(Value, Shft, Ctx), Ctx);
      return Dst;
    };

    ProgInfo.ComputePGMRSrc3GFX90A =
        SetBits(ProgInfo.ComputePGMRSrc3GFX90A, ProgInfo.AccumOffset,
                amdhsa::COMPUTE_PGM_RSRC3_GFX90A_ACCUM_OFFSET,
                amdhsa::COMPUTE_PGM_RSRC3_GFX90A_ACCUM_OFFSET_SHIFT);
    ProgInfo.ComputePGMRSrc3GFX90A =
        SetBits(ProgInfo.ComputePGMRSrc3GFX90A, CreateExpr(ProgInfo.TgSplit),
                amdhsa::COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT,
                amdhsa::COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT_SHIFT);
  }

  ProgInfo.Occupancy = AMDGPUMCExpr::createOccupancy(
      STM.computeOccupancy(F, ProgInfo.LDSSize), ProgInfo.NumSGPRsForWavesPerEU,
      ProgInfo.NumVGPRsForWavesPerEU, STM, Ctx);

  const auto [MinWEU, MaxWEU] =
      AMDGPU::getIntegerPairAttribute(F, "amdgpu-waves-per-eu", {0, 0}, true);
  uint64_t Occupancy;
  if (TryGetMCExprValue(ProgInfo.Occupancy, Occupancy) && Occupancy < MinWEU) {
    DiagnosticInfoOptimizationFailure Diag(
        F, F.getSubprogram(),
        "failed to meet occupancy target given by 'amdgpu-waves-per-eu' in "
        "'" +
            F.getName() + "': desired occupancy was " + Twine(MinWEU) +
            ", final occupancy is " + Twine(Occupancy));
    F.getContext().diagnose(Diag);
  }
}

static unsigned getRsrcReg(CallingConv::ID CallConv) {
  switch (CallConv) {
  default: [[fallthrough]];
  case CallingConv::AMDGPU_CS: return R_00B848_COMPUTE_PGM_RSRC1;
  case CallingConv::AMDGPU_LS: return R_00B528_SPI_SHADER_PGM_RSRC1_LS;
  case CallingConv::AMDGPU_HS: return R_00B428_SPI_SHADER_PGM_RSRC1_HS;
  case CallingConv::AMDGPU_ES: return R_00B328_SPI_SHADER_PGM_RSRC1_ES;
  case CallingConv::AMDGPU_GS: return R_00B228_SPI_SHADER_PGM_RSRC1_GS;
  case CallingConv::AMDGPU_VS: return R_00B128_SPI_SHADER_PGM_RSRC1_VS;
  case CallingConv::AMDGPU_PS: return R_00B028_SPI_SHADER_PGM_RSRC1_PS;
  }
}

void AMDGPUAsmPrinter::EmitProgramInfoSI(const MachineFunction &MF,
                                         const SIProgramInfo &CurrentProgramInfo) {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  unsigned RsrcReg = getRsrcReg(MF.getFunction().getCallingConv());
  MCContext &Ctx = MF.getContext();

  // (((Value) & Mask) << Shift)
  auto SetBits = [&Ctx](const MCExpr *Value, uint32_t Mask, uint32_t Shift) {
    const MCExpr *msk = MCConstantExpr::create(Mask, Ctx);
    const MCExpr *shft = MCConstantExpr::create(Shift, Ctx);
    return MCBinaryExpr::createShl(MCBinaryExpr::createAnd(Value, msk, Ctx),
                                   shft, Ctx);
  };

  auto EmitResolvedOrExpr = [this](const MCExpr *Value, unsigned Size) {
    int64_t Val;
    if (Value->evaluateAsAbsolute(Val))
      OutStreamer->emitIntValue(static_cast<uint64_t>(Val), Size);
    else
      OutStreamer->emitValue(Value, Size);
  };

  if (AMDGPU::isCompute(MF.getFunction().getCallingConv())) {
    OutStreamer->emitInt32(R_00B848_COMPUTE_PGM_RSRC1);

    EmitResolvedOrExpr(CurrentProgramInfo.getComputePGMRSrc1(STM, Ctx),
                       /*Size=*/4);

    OutStreamer->emitInt32(R_00B84C_COMPUTE_PGM_RSRC2);
    EmitResolvedOrExpr(CurrentProgramInfo.getComputePGMRSrc2(Ctx), /*Size=*/4);

    OutStreamer->emitInt32(R_00B860_COMPUTE_TMPRING_SIZE);

    // Sets bits according to S_0286E8_WAVESIZE_* mask and shift values for the
    // appropriate generation.
    if (STM.getGeneration() >= AMDGPUSubtarget::GFX12) {
      EmitResolvedOrExpr(SetBits(CurrentProgramInfo.ScratchBlocks,
                                 /*Mask=*/0x3FFFF, /*Shift=*/12),
                         /*Size=*/4);
    } else if (STM.getGeneration() == AMDGPUSubtarget::GFX11) {
      EmitResolvedOrExpr(SetBits(CurrentProgramInfo.ScratchBlocks,
                                 /*Mask=*/0x7FFF, /*Shift=*/12),
                         /*Size=*/4);
    } else {
      EmitResolvedOrExpr(SetBits(CurrentProgramInfo.ScratchBlocks,
                                 /*Mask=*/0x1FFF, /*Shift=*/12),
                         /*Size=*/4);
    }

    // TODO: Should probably note flat usage somewhere. SC emits a "FlatPtr32 =
    // 0" comment but I don't see a corresponding field in the register spec.
  } else {
    OutStreamer->emitInt32(RsrcReg);

    const MCExpr *GPRBlocks = MCBinaryExpr::createOr(
        SetBits(CurrentProgramInfo.VGPRBlocks, /*Mask=*/0x3F, /*Shift=*/0),
        SetBits(CurrentProgramInfo.SGPRBlocks, /*Mask=*/0x0F, /*Shift=*/6),
        MF.getContext());
    EmitResolvedOrExpr(GPRBlocks, /*Size=*/4);
    OutStreamer->emitInt32(R_0286E8_SPI_TMPRING_SIZE);

    // Sets bits according to S_0286E8_WAVESIZE_* mask and shift values for the
    // appropriate generation.
    if (STM.getGeneration() >= AMDGPUSubtarget::GFX12) {
      EmitResolvedOrExpr(SetBits(CurrentProgramInfo.ScratchBlocks,
                                 /*Mask=*/0x3FFFF, /*Shift=*/12),
                         /*Size=*/4);
    } else if (STM.getGeneration() == AMDGPUSubtarget::GFX11) {
      EmitResolvedOrExpr(SetBits(CurrentProgramInfo.ScratchBlocks,
                                 /*Mask=*/0x7FFF, /*Shift=*/12),
                         /*Size=*/4);
    } else {
      EmitResolvedOrExpr(SetBits(CurrentProgramInfo.ScratchBlocks,
                                 /*Mask=*/0x1FFF, /*Shift=*/12),
                         /*Size=*/4);
    }
  }

  if (MF.getFunction().getCallingConv() == CallingConv::AMDGPU_PS) {
    OutStreamer->emitInt32(R_00B02C_SPI_SHADER_PGM_RSRC2_PS);
    unsigned ExtraLDSSize = STM.getGeneration() >= AMDGPUSubtarget::GFX11
                                ? divideCeil(CurrentProgramInfo.LDSBlocks, 2)
                                : CurrentProgramInfo.LDSBlocks;
    OutStreamer->emitInt32(S_00B02C_EXTRA_LDS_SIZE(ExtraLDSSize));
    OutStreamer->emitInt32(R_0286CC_SPI_PS_INPUT_ENA);
    OutStreamer->emitInt32(MFI->getPSInputEnable());
    OutStreamer->emitInt32(R_0286D0_SPI_PS_INPUT_ADDR);
    OutStreamer->emitInt32(MFI->getPSInputAddr());
  }

  OutStreamer->emitInt32(R_SPILLED_SGPRS);
  OutStreamer->emitInt32(MFI->getNumSpilledSGPRs());
  OutStreamer->emitInt32(R_SPILLED_VGPRS);
  OutStreamer->emitInt32(MFI->getNumSpilledVGPRs());
}

// Helper function to add common PAL Metadata 3.0+
static void EmitPALMetadataCommon(AMDGPUPALMetadata *MD,
                                  const SIProgramInfo &CurrentProgramInfo,
                                  CallingConv::ID CC, const GCNSubtarget &ST) {
  if (ST.hasIEEEMode())
    MD->setHwStage(CC, ".ieee_mode", (bool)CurrentProgramInfo.IEEEMode);

  MD->setHwStage(CC, ".wgp_mode", (bool)CurrentProgramInfo.WgpMode);
  MD->setHwStage(CC, ".mem_ordered", (bool)CurrentProgramInfo.MemOrdered);

  if (AMDGPU::isCompute(CC)) {
    MD->setHwStage(CC, ".trap_present",
                   (bool)CurrentProgramInfo.TrapHandlerEnable);
    MD->setHwStage(CC, ".excp_en", CurrentProgramInfo.EXCPEnable);
  }

  MD->setHwStage(CC, ".lds_size",
                 (unsigned)(CurrentProgramInfo.LdsSize *
                            getLdsDwGranularity(ST) * sizeof(uint32_t)));
}

// This is the equivalent of EmitProgramInfoSI above, but for when the OS type
// is AMDPAL.  It stores each compute/SPI register setting and other PAL
// metadata items into the PALMD::Metadata, combining with any provided by the
// frontend as LLVM metadata. Once all functions are written, the PAL metadata
// is then written as a single block in the .note section.
void AMDGPUAsmPrinter::EmitPALMetadata(const MachineFunction &MF,
       const SIProgramInfo &CurrentProgramInfo) {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  auto CC = MF.getFunction().getCallingConv();
  auto MD = getTargetStreamer()->getPALMetadata();
  auto &Ctx = MF.getContext();

  MD->setEntryPoint(CC, MF.getFunction().getName());
  MD->setNumUsedVgprs(CC, CurrentProgramInfo.NumVGPRsForWavesPerEU, Ctx);

  // Only set AGPRs for supported devices
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  if (STM.hasMAIInsts()) {
    MD->setNumUsedAgprs(CC, CurrentProgramInfo.NumAccVGPR);
  }

  MD->setNumUsedSgprs(CC, CurrentProgramInfo.NumSGPRsForWavesPerEU, Ctx);
  if (MD->getPALMajorVersion() < 3) {
    MD->setRsrc1(CC, CurrentProgramInfo.getPGMRSrc1(CC, STM, Ctx), Ctx);
    if (AMDGPU::isCompute(CC)) {
      MD->setRsrc2(CC, CurrentProgramInfo.getComputePGMRSrc2(Ctx), Ctx);
    } else {
      const MCExpr *HasScratchBlocks =
          MCBinaryExpr::createGT(CurrentProgramInfo.ScratchBlocks,
                                 MCConstantExpr::create(0, Ctx), Ctx);
      auto [Shift, Mask] = getShiftMask(C_00B84C_SCRATCH_EN);
      MD->setRsrc2(CC, maskShiftSet(HasScratchBlocks, Mask, Shift, Ctx), Ctx);
    }
  } else {
    MD->setHwStage(CC, ".debug_mode", (bool)CurrentProgramInfo.DebugMode);
    MD->setHwStage(CC, ".scratch_en", msgpack::Type::Boolean,
                   CurrentProgramInfo.ScratchEnable);
    EmitPALMetadataCommon(MD, CurrentProgramInfo, CC, STM);
  }

  // ScratchSize is in bytes, 16 aligned.
  MD->setScratchSize(
      CC,
      AMDGPUMCExpr::createAlignTo(CurrentProgramInfo.ScratchSize,
                                  MCConstantExpr::create(16, Ctx), Ctx),
      Ctx);

  if (MF.getFunction().getCallingConv() == CallingConv::AMDGPU_PS) {
    unsigned ExtraLDSSize = STM.getGeneration() >= AMDGPUSubtarget::GFX11
                                ? divideCeil(CurrentProgramInfo.LDSBlocks, 2)
                                : CurrentProgramInfo.LDSBlocks;
    if (MD->getPALMajorVersion() < 3) {
      MD->setRsrc2(
          CC,
          MCConstantExpr::create(S_00B02C_EXTRA_LDS_SIZE(ExtraLDSSize), Ctx),
          Ctx);
      MD->setSpiPsInputEna(MFI->getPSInputEnable());
      MD->setSpiPsInputAddr(MFI->getPSInputAddr());
    } else {
      // Graphics registers
      const unsigned ExtraLdsDwGranularity =
          STM.getGeneration() >= AMDGPUSubtarget::GFX11 ? 256 : 128;
      MD->setGraphicsRegisters(
          ".ps_extra_lds_size",
          (unsigned)(ExtraLDSSize * ExtraLdsDwGranularity * sizeof(uint32_t)));

      // Set PsInputEna and PsInputAddr .spi_ps_input_ena and .spi_ps_input_addr
      static StringLiteral const PsInputFields[] = {
          ".persp_sample_ena",    ".persp_center_ena",
          ".persp_centroid_ena",  ".persp_pull_model_ena",
          ".linear_sample_ena",   ".linear_center_ena",
          ".linear_centroid_ena", ".line_stipple_tex_ena",
          ".pos_x_float_ena",     ".pos_y_float_ena",
          ".pos_z_float_ena",     ".pos_w_float_ena",
          ".front_face_ena",      ".ancillary_ena",
          ".sample_coverage_ena", ".pos_fixed_pt_ena"};
      unsigned PSInputEna = MFI->getPSInputEnable();
      unsigned PSInputAddr = MFI->getPSInputAddr();
      for (auto [Idx, Field] : enumerate(PsInputFields)) {
        MD->setGraphicsRegisters(".spi_ps_input_ena", Field,
                                 (bool)((PSInputEna >> Idx) & 1));
        MD->setGraphicsRegisters(".spi_ps_input_addr", Field,
                                 (bool)((PSInputAddr >> Idx) & 1));
      }
    }
  }

  // For version 3 and above the wave front size is already set in the metadata
  if (MD->getPALMajorVersion() < 3 && STM.isWave32())
    MD->setWave32(MF.getFunction().getCallingConv());
}

void AMDGPUAsmPrinter::emitPALFunctionMetadata(const MachineFunction &MF) {
  auto *MD = getTargetStreamer()->getPALMetadata();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  StringRef FnName = MF.getFunction().getName();
  MD->setFunctionScratchSize(FnName, MFI.getStackSize());
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  MCContext &Ctx = MF.getContext();

  if (MD->getPALMajorVersion() < 3) {
    // Set compute registers
    MD->setRsrc1(
        CallingConv::AMDGPU_CS,
        CurrentProgramInfo.getPGMRSrc1(CallingConv::AMDGPU_CS, ST, Ctx), Ctx);
    MD->setRsrc2(CallingConv::AMDGPU_CS,
                 CurrentProgramInfo.getComputePGMRSrc2(Ctx), Ctx);
  } else {
    EmitPALMetadataCommon(MD, CurrentProgramInfo, CallingConv::AMDGPU_CS, ST);
  }

  // Set optional info
  MD->setFunctionLdsSize(FnName, CurrentProgramInfo.LDSSize);
  MD->setFunctionNumUsedVgprs(FnName, CurrentProgramInfo.NumVGPRsForWavesPerEU);
  MD->setFunctionNumUsedSgprs(FnName, CurrentProgramInfo.NumSGPRsForWavesPerEU);
}

// This is supposed to be log2(Size)
static amd_element_byte_size_t getElementByteSizeValue(unsigned Size) {
  switch (Size) {
  case 4:
    return AMD_ELEMENT_4_BYTES;
  case 8:
    return AMD_ELEMENT_8_BYTES;
  case 16:
    return AMD_ELEMENT_16_BYTES;
  default:
    llvm_unreachable("invalid private_element_size");
  }
}

void AMDGPUAsmPrinter::getAmdKernelCode(AMDGPUMCKernelCodeT &Out,
                                        const SIProgramInfo &CurrentProgramInfo,
                                        const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  assert(F.getCallingConv() == CallingConv::AMDGPU_KERNEL ||
         F.getCallingConv() == CallingConv::SPIR_KERNEL);

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  MCContext &Ctx = MF.getContext();

  Out.initDefault(&STM, Ctx, /*InitMCExpr=*/false);

  Out.compute_pgm_resource1_registers =
      CurrentProgramInfo.getComputePGMRSrc1(STM, Ctx);
  Out.compute_pgm_resource2_registers =
      CurrentProgramInfo.getComputePGMRSrc2(Ctx);
  Out.code_properties |= AMD_CODE_PROPERTY_IS_PTR64;

  Out.is_dynamic_callstack = CurrentProgramInfo.DynamicCallStack;

  AMD_HSA_BITS_SET(Out.code_properties, AMD_CODE_PROPERTY_PRIVATE_ELEMENT_SIZE,
                   getElementByteSizeValue(STM.getMaxPrivateElementSize(true)));

  const GCNUserSGPRUsageInfo &UserSGPRInfo = MFI->getUserSGPRInfo();
  if (UserSGPRInfo.hasPrivateSegmentBuffer()) {
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER;
  }

  if (UserSGPRInfo.hasDispatchPtr())
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR;

  if (UserSGPRInfo.hasQueuePtr() && CodeObjectVersion < AMDGPU::AMDHSA_COV5)
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR;

  if (UserSGPRInfo.hasKernargSegmentPtr())
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR;

  if (UserSGPRInfo.hasDispatchID())
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID;

  if (UserSGPRInfo.hasFlatScratchInit())
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT;

  if (UserSGPRInfo.hasPrivateSegmentSize())
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE;

  if (UserSGPRInfo.hasDispatchPtr())
    Out.code_properties |= AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR;

  if (STM.isXNACKEnabled())
    Out.code_properties |= AMD_CODE_PROPERTY_IS_XNACK_SUPPORTED;

  Align MaxKernArgAlign;
  Out.kernarg_segment_byte_size = STM.getKernArgSegmentSize(F, MaxKernArgAlign);
  Out.wavefront_sgpr_count = CurrentProgramInfo.NumSGPR;
  Out.workitem_vgpr_count = CurrentProgramInfo.NumVGPR;
  Out.workitem_private_segment_byte_size = CurrentProgramInfo.ScratchSize;
  Out.workgroup_group_segment_byte_size = CurrentProgramInfo.LDSSize;

  // kernarg_segment_alignment is specified as log of the alignment.
  // The minimum alignment is 16.
  // FIXME: The metadata treats the minimum as 4?
  Out.kernarg_segment_alignment = Log2(std::max(Align(16), MaxKernArgAlign));
}

bool AMDGPUAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                       const char *ExtraCode, raw_ostream &O) {
  // First try the generic code, which knows about modifiers like 'c' and 'n'.
  if (!AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O))
    return false;

  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    case 'r':
      break;
    default:
      return true;
    }
  }

  // TODO: Should be able to support other operand types like globals.
  const MachineOperand &MO = MI->getOperand(OpNo);
  if (MO.isReg()) {
    AMDGPUInstPrinter::printRegOperand(MO.getReg(), O,
                                       *MF->getSubtarget().getRegisterInfo());
    return false;
  }
  if (MO.isImm()) {
    int64_t Val = MO.getImm();
    if (AMDGPU::isInlinableIntLiteral(Val)) {
      O << Val;
    } else if (isUInt<16>(Val)) {
      O << format("0x%" PRIx16, static_cast<uint16_t>(Val));
    } else if (isUInt<32>(Val)) {
      O << format("0x%" PRIx32, static_cast<uint32_t>(Val));
    } else {
      O << format("0x%" PRIx64, static_cast<uint64_t>(Val));
    }
    return false;
  }
  return true;
}

void AMDGPUAsmPrinter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AMDGPUResourceUsageAnalysis>();
  AU.addPreserved<AMDGPUResourceUsageAnalysis>();
  AsmPrinter::getAnalysisUsage(AU);
}

void AMDGPUAsmPrinter::emitResourceUsageRemarks(
    const MachineFunction &MF, const SIProgramInfo &CurrentProgramInfo,
    bool isModuleEntryFunction, bool hasMAIInsts) {
  if (!ORE)
    return;

  const char *Name = "kernel-resource-usage";
  const char *Indent = "    ";

  // If the remark is not specifically enabled, do not output to yaml
  LLVMContext &Ctx = MF.getFunction().getContext();
  if (!Ctx.getDiagHandlerPtr()->isAnalysisRemarkEnabled(Name))
    return;

  // Currently non-kernel functions have no resources to emit.
  if (!isEntryFunctionCC(MF.getFunction().getCallingConv()))
    return;

  auto EmitResourceUsageRemark = [&](StringRef RemarkName,
                                     StringRef RemarkLabel, auto Argument) {
    // Add an indent for every line besides the line with the kernel name. This
    // makes it easier to tell which resource usage go with which kernel since
    // the kernel name will always be displayed first.
    std::string LabelStr = RemarkLabel.str() + ": ";
    if (RemarkName != "FunctionName")
      LabelStr = Indent + LabelStr;

    ORE->emit([&]() {
      return MachineOptimizationRemarkAnalysis(Name, RemarkName,
                                               MF.getFunction().getSubprogram(),
                                               &MF.front())
             << LabelStr << ore::NV(RemarkName, Argument);
    });
  };

  // FIXME: Formatting here is pretty nasty because clang does not accept
  // newlines from diagnostics. This forces us to emit multiple diagnostic
  // remarks to simulate newlines. If and when clang does accept newlines, this
  // formatting should be aggregated into one remark with newlines to avoid
  // printing multiple diagnostic location and diag opts.
  EmitResourceUsageRemark("FunctionName", "Function Name",
                          MF.getFunction().getName());
  EmitResourceUsageRemark("NumSGPR", "SGPRs",
                          getMCExprStr(CurrentProgramInfo.NumSGPR));
  EmitResourceUsageRemark("NumVGPR", "VGPRs",
                          getMCExprStr(CurrentProgramInfo.NumArchVGPR));
  if (hasMAIInsts) {
    EmitResourceUsageRemark("NumAGPR", "AGPRs",
                            getMCExprStr(CurrentProgramInfo.NumAccVGPR));
  }
  EmitResourceUsageRemark("ScratchSize", "ScratchSize [bytes/lane]",
                          getMCExprStr(CurrentProgramInfo.ScratchSize));
  int64_t DynStack;
  bool DynStackEvaluatable =
      CurrentProgramInfo.DynamicCallStack->evaluateAsAbsolute(DynStack);
  StringRef DynamicStackStr =
      DynStackEvaluatable && DynStack ? "True" : "False";
  EmitResourceUsageRemark("DynamicStack", "Dynamic Stack", DynamicStackStr);
  EmitResourceUsageRemark("Occupancy", "Occupancy [waves/SIMD]",
                          getMCExprStr(CurrentProgramInfo.Occupancy));
  EmitResourceUsageRemark("SGPRSpill", "SGPRs Spill",
                          CurrentProgramInfo.SGPRSpill);
  EmitResourceUsageRemark("VGPRSpill", "VGPRs Spill",
                          CurrentProgramInfo.VGPRSpill);
  if (isModuleEntryFunction)
    EmitResourceUsageRemark("BytesLDS", "LDS Size [bytes/block]",
                            CurrentProgramInfo.LDSSize);
}
