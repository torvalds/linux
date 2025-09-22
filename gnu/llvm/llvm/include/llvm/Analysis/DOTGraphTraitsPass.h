//===-- DOTGraphTraitsPass.h - Print/View dotty graphs-----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Templates to create dotty viewer and printer passes for GraphTraits graphs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOTGRAPHTRAITSPASS_H
#define LLVM_ANALYSIS_DOTGRAPHTRAITSPASS_H

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GraphWriter.h"
#include <unordered_set>

static std::unordered_set<std::string> nameObj;

namespace llvm {

/// Default traits class for extracting a graph from an analysis pass.
///
/// This assumes that 'GraphT' is 'AnalysisT::Result *', and pass it through
template <typename Result, typename GraphT = Result *>
struct DefaultAnalysisGraphTraits {
  static GraphT getGraph(Result R) { return &R; }
};

template <typename GraphT>
void viewGraphForFunction(Function &F, GraphT Graph, StringRef Name,
                          bool IsSimple) {
  std::string GraphName = DOTGraphTraits<GraphT *>::getGraphName(&Graph);

  ViewGraph(Graph, Name, IsSimple,
            GraphName + " for '" + F.getName() + "' function");
}

template <typename AnalysisT, bool IsSimple,
          typename GraphT = typename AnalysisT::Result *,
          typename AnalysisGraphTraitsT =
              DefaultAnalysisGraphTraits<typename AnalysisT::Result &, GraphT>>
struct DOTGraphTraitsViewer
    : PassInfoMixin<DOTGraphTraitsViewer<AnalysisT, IsSimple, GraphT,
                                         AnalysisGraphTraitsT>> {
  DOTGraphTraitsViewer(StringRef GraphName) : Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be viewed.
  ///
  /// @param Result The current analysis result for this function.
  virtual bool processFunction(Function &F,
                               const typename AnalysisT::Result &Result) {
    return true;
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &Result = FAM.getResult<AnalysisT>(F);
    if (!processFunction(F, Result))
      return PreservedAnalyses::all();

    GraphT Graph = AnalysisGraphTraitsT::getGraph(Result);
    viewGraphForFunction(F, Graph, Name, IsSimple);

    return PreservedAnalyses::all();
  };

protected:
  /// Avoid compiler warning "has virtual functions but non-virtual destructor
  /// [-Wnon-virtual-dtor]" in derived classes.
  ///
  /// DOTGraphTraitsViewer is also used as a mixin for avoiding repeated
  /// implementation of viewer passes, ie there should be no
  /// runtime-polymorphisms/downcasting involving this class and hence no
  /// virtual destructor needed. Making this dtor protected stops accidental
  /// invocation when the derived class destructor should have been called.
  /// Those derived classes sould be marked final to avoid the warning.
  ~DOTGraphTraitsViewer() {}

private:
  StringRef Name;
};

static inline void shortenFileName(std::string &FN, unsigned char len = 250) {
  if (FN.length() > len)
    FN.resize(len);
  auto strLen = FN.length();
  while (strLen > 0) {
    if (nameObj.find(FN) != nameObj.end()) {
      FN.resize(--len);
    } else {
      nameObj.insert(FN);
      break;
    }
    strLen--;
  }
}

template <typename GraphT>
void printGraphForFunction(Function &F, GraphT Graph, StringRef Name,
                           bool IsSimple) {
  std::string Filename = Name.str() + "." + F.getName().str();
  shortenFileName(Filename);
  Filename = Filename + ".dot";
  std::error_code EC;

  errs() << "Writing '" << Filename << "'...";

  raw_fd_ostream File(Filename, EC, sys::fs::OF_TextWithCRLF);
  std::string GraphName = DOTGraphTraits<GraphT>::getGraphName(Graph);

  if (!EC)
    WriteGraph(File, Graph, IsSimple,
               GraphName + " for '" + F.getName() + "' function");
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

template <typename AnalysisT, bool IsSimple,
          typename GraphT = typename AnalysisT::Result *,
          typename AnalysisGraphTraitsT =
              DefaultAnalysisGraphTraits<typename AnalysisT::Result &, GraphT>>
struct DOTGraphTraitsPrinter
    : PassInfoMixin<DOTGraphTraitsPrinter<AnalysisT, IsSimple, GraphT,
                                          AnalysisGraphTraitsT>> {
  DOTGraphTraitsPrinter(StringRef GraphName) : Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be viewed.
  ///
  /// @param Result The current analysis result for this function.
  virtual bool processFunction(Function &F,
                               const typename AnalysisT::Result &Result) {
    return true;
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &Result = FAM.getResult<AnalysisT>(F);
    if (!processFunction(F, Result))
      return PreservedAnalyses::all();

    GraphT Graph = AnalysisGraphTraitsT::getGraph(Result);

    printGraphForFunction(F, Graph, Name, IsSimple);

    return PreservedAnalyses::all();
  };

protected:
  /// Avoid compiler warning "has virtual functions but non-virtual destructor
  /// [-Wnon-virtual-dtor]" in derived classes.
  ///
  /// DOTGraphTraitsPrinter is also used as a mixin for avoiding repeated
  /// implementation of printer passes, ie there should be no
  /// runtime-polymorphisms/downcasting involving this class and hence no
  /// virtual destructor needed. Making this dtor protected stops accidental
  /// invocation when the derived class destructor should have been called.
  /// Those derived classes sould be marked final to avoid the warning.
  ~DOTGraphTraitsPrinter() {}

private:
  StringRef Name;
};

/// Default traits class for extracting a graph from an analysis pass.
///
/// This assumes that 'GraphT' is 'AnalysisT *' and so just passes it through.
template <typename AnalysisT, typename GraphT = AnalysisT *>
struct LegacyDefaultAnalysisGraphTraits {
  static GraphT getGraph(AnalysisT *A) { return A; }
};

template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsViewerWrapperPass : public FunctionPass {
public:
  DOTGraphTraitsViewerWrapperPass(StringRef GraphName, char &ID)
      : FunctionPass(ID), Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be viewed.
  ///
  /// @param Analysis The current analysis result for this function.
  virtual bool processFunction(Function &F, AnalysisT &Analysis) {
    return true;
  }

  bool runOnFunction(Function &F) override {
    auto &Analysis = getAnalysis<AnalysisT>();

    if (!processFunction(F, Analysis))
      return false;

    GraphT Graph = AnalysisGraphTraitsT::getGraph(&Analysis);
    viewGraphForFunction(F, Graph, Name, IsSimple);

    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsPrinterWrapperPass : public FunctionPass {
public:
  DOTGraphTraitsPrinterWrapperPass(StringRef GraphName, char &ID)
      : FunctionPass(ID), Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be printed.
  ///
  /// @param Analysis The current analysis result for this function.
  virtual bool processFunction(Function &F, AnalysisT &Analysis) {
    return true;
  }

  bool runOnFunction(Function &F) override {
    auto &Analysis = getAnalysis<AnalysisT>();

    if (!processFunction(F, Analysis))
      return false;

    GraphT Graph = AnalysisGraphTraitsT::getGraph(&Analysis);
    printGraphForFunction(F, Graph, Name, IsSimple);

    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsModuleViewerWrapperPass : public ModulePass {
public:
  DOTGraphTraitsModuleViewerWrapperPass(StringRef GraphName, char &ID)
      : ModulePass(ID), Name(GraphName) {}

  bool runOnModule(Module &M) override {
    GraphT Graph = AnalysisGraphTraitsT::getGraph(&getAnalysis<AnalysisT>());
    std::string Title = DOTGraphTraits<GraphT>::getGraphName(Graph);

    ViewGraph(Graph, Name, IsSimple, Title);

    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsModulePrinterWrapperPass : public ModulePass {
public:
  DOTGraphTraitsModulePrinterWrapperPass(StringRef GraphName, char &ID)
      : ModulePass(ID), Name(GraphName) {}

  bool runOnModule(Module &M) override {
    GraphT Graph = AnalysisGraphTraitsT::getGraph(&getAnalysis<AnalysisT>());
    shortenFileName(Name);
    std::string Filename = Name + ".dot";
    std::error_code EC;

    errs() << "Writing '" << Filename << "'...";

    raw_fd_ostream File(Filename, EC, sys::fs::OF_TextWithCRLF);
    std::string Title = DOTGraphTraits<GraphT>::getGraphName(Graph);

    if (!EC)
      WriteGraph(File, Graph, IsSimple, Title);
    else
      errs() << "  error opening file for writing!";
    errs() << "\n";

    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

template <typename GraphT>
void WriteDOTGraphToFile(Function &F, GraphT &&Graph,
                         std::string FileNamePrefix, bool IsSimple) {
  std::string Filename = FileNamePrefix + "." + F.getName().str();
  shortenFileName(Filename);
  Filename = Filename + ".dot";
  std::error_code EC;

  errs() << "Writing '" << Filename << "'...";

  raw_fd_ostream File(Filename, EC, sys::fs::OF_TextWithCRLF);
  std::string GraphName = DOTGraphTraits<GraphT>::getGraphName(Graph);
  std::string Title = GraphName + " for '" + F.getName().str() + "' function";

  if (!EC)
    WriteGraph(File, Graph, IsSimple, Title);
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

} // end namespace llvm

#endif
