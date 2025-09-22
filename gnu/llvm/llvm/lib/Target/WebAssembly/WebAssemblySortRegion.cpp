#include "WebAssemblySortRegion.h"
#include "WebAssemblyExceptionInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"

using namespace llvm;
using namespace WebAssembly;

namespace llvm {
namespace WebAssembly {
template <>
bool ConcreteSortRegion<MachineLoop>::isLoop() const {
  return true;
}
} // end namespace WebAssembly
} // end namespace llvm

const SortRegion *SortRegionInfo::getRegionFor(const MachineBasicBlock *MBB) {
  const auto *ML = MLI.getLoopFor(MBB);
  const auto *WE = WEI.getExceptionFor(MBB);
  if (!ML && !WE)
    return nullptr;
  // We determine subregion relationship by domination of their headers, i.e.,
  // if region A's header dominates region B's header, B is a subregion of A.
  // WebAssemblyException contains BBs in all its subregions (loops or
  // exceptions), but MachineLoop may not, because MachineLoop does not
  // contain BBs that don't have a path to its header even if they are
  // dominated by its header. So here we should use
  // WE->contains(ML->getHeader()), but not ML->contains(WE->getHeader()).
  if ((ML && !WE) || (ML && WE && WE->contains(ML->getHeader()))) {
    // If the smallest region containing MBB is a loop
    if (LoopMap.count(ML))
      return LoopMap[ML].get();
    LoopMap[ML] = std::make_unique<ConcreteSortRegion<MachineLoop>>(ML);
    return LoopMap[ML].get();
  } else {
    // If the smallest region containing MBB is an exception
    if (ExceptionMap.count(WE))
      return ExceptionMap[WE].get();
    ExceptionMap[WE] =
        std::make_unique<ConcreteSortRegion<WebAssemblyException>>(WE);
    return ExceptionMap[WE].get();
  }
}

MachineBasicBlock *SortRegionInfo::getBottom(const SortRegion *R) {
  if (R->isLoop())
    return getBottom(MLI.getLoopFor(R->getHeader()));
  else
    return getBottom(WEI.getExceptionFor(R->getHeader()));
}

MachineBasicBlock *SortRegionInfo::getBottom(const MachineLoop *ML) {
  MachineBasicBlock *Bottom = ML->getHeader();
  for (MachineBasicBlock *MBB : ML->blocks()) {
    if (MBB->getNumber() > Bottom->getNumber())
      Bottom = MBB;
    // MachineLoop does not contain all BBs dominated by its header. BBs that
    // don't have a path back to the loop header aren't included. But for the
    // purpose of CFG sorting and stackification, we need a bottom BB among all
    // BBs that are dominated by the loop header. So we check if there is any
    // WebAssemblyException contained in this loop, and computes the most bottom
    // BB of them all.
    if (MBB->isEHPad()) {
      MachineBasicBlock *ExBottom = getBottom(WEI.getExceptionFor(MBB));
      if (ExBottom->getNumber() > Bottom->getNumber())
        Bottom = ExBottom;
    }
  }
  return Bottom;
}

MachineBasicBlock *SortRegionInfo::getBottom(const WebAssemblyException *WE) {
  MachineBasicBlock *Bottom = WE->getHeader();
  for (MachineBasicBlock *MBB : WE->blocks())
    if (MBB->getNumber() > Bottom->getNumber())
      Bottom = MBB;
  return Bottom;
}
