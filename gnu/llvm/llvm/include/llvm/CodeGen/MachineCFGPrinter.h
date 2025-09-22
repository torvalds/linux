//===-- MachineCFGPrinter.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/DOTGraphTraits.h"

namespace llvm {

template <class GraphType> struct GraphTraits;
class DOTMachineFuncInfo {
private:
  const MachineFunction *F;

public:
  DOTMachineFuncInfo(const MachineFunction *F) : F(F) {}

  const MachineFunction *getFunction() const { return this->F; }
};

template <>
struct GraphTraits<DOTMachineFuncInfo *>
    : public GraphTraits<const MachineBasicBlock *> {
  static NodeRef getEntryNode(DOTMachineFuncInfo *CFGInfo) {
    return &(CFGInfo->getFunction()->front());
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<MachineFunction::const_iterator>;

  static nodes_iterator nodes_begin(DOTMachineFuncInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->begin());
  }

  static nodes_iterator nodes_end(DOTMachineFuncInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->end());
  }

  static size_t size(DOTMachineFuncInfo *CFGInfo) {
    return CFGInfo->getFunction()->size();
  }
};

template <>
struct DOTGraphTraits<DOTMachineFuncInfo *> : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static void eraseComment(std::string &OutStr, unsigned &I, unsigned Idx) {
    OutStr.erase(OutStr.begin() + I, OutStr.begin() + Idx);
    --I;
  }

  static std::string getSimpleNodeLabel(const MachineBasicBlock *Node,
                                        DOTMachineFuncInfo *) {
    return SimpleNodeLabelString(Node);
  }

  static std::string getCompleteNodeLabel(
      const MachineBasicBlock *Node, DOTMachineFuncInfo *,
      function_ref<void(raw_string_ostream &, const MachineBasicBlock &)>
          HandleBasicBlock =
              [](raw_string_ostream &OS,
                 const MachineBasicBlock &Node) -> void { OS << Node; },
      function_ref<void(std::string &, unsigned &, unsigned)>
          HandleComment = eraseComment) {
    return CompleteNodeLabelString(Node, HandleBasicBlock, HandleComment);
  }

  std::string getNodeLabel(const MachineBasicBlock *Node,
                           DOTMachineFuncInfo *CFGInfo) {
    if (isSimple())
      return getSimpleNodeLabel(Node, CFGInfo);

    return getCompleteNodeLabel(Node, CFGInfo);
  }

  static std::string getGraphName(DOTMachineFuncInfo *CFGInfo) {
    return "Machine CFG for '" + CFGInfo->getFunction()->getName().str() +
           "' function";
  }
};
} // namespace llvm
