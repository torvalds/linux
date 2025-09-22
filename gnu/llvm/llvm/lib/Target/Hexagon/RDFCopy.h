//===- RDFCopy.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_RDFCOPY_H
#define LLVM_LIB_TARGET_HEXAGON_RDFCOPY_H

#include "llvm/CodeGen/RDFGraph.h"
#include "llvm/CodeGen/RDFLiveness.h"
#include "llvm/CodeGen/RDFRegisters.h"
#include "llvm/CodeGen/MachineFunction.h"
#include <map>
#include <vector>

namespace llvm {

class MachineBasicBlock;
class MachineDominatorTree;
class MachineInstr;

namespace rdf {

  struct CopyPropagation {
    CopyPropagation(DataFlowGraph &dfg) : MDT(dfg.getDT()), DFG(dfg),
        RDefMap(std::less<RegisterRef>(DFG.getPRI())) {}

    virtual ~CopyPropagation() = default;

    bool run();
    void trace(bool On) { Trace = On; }
    bool trace() const { return Trace; }
    DataFlowGraph &getDFG() { return DFG; }

    using EqualityMap = std::map<RegisterRef, RegisterRef>;
    virtual bool interpretAsCopy(const MachineInstr *MI, EqualityMap &EM);

  private:
    const MachineDominatorTree &MDT;
    DataFlowGraph &DFG;
    DataFlowGraph::DefStackMap DefM;
    bool Trace = false;

    // map: register -> (map: stmt -> reaching def)
    std::map<RegisterRef,std::map<NodeId,NodeId>> RDefMap;
    // map: statement -> (map: dst reg -> src reg)
    std::map<NodeId, EqualityMap> CopyMap;
    std::vector<NodeId> Copies;

    void recordCopy(NodeAddr<StmtNode*> SA, EqualityMap &EM);
    void updateMap(NodeAddr<InstrNode*> IA);
    bool scanBlock(MachineBasicBlock *B);
  };

} // end namespace rdf

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_RDFCOPY_H
