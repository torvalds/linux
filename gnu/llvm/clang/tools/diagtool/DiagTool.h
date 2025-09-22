//===- DiagTool.h - Classes for defining diagtool tools -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the boilerplate for defining diagtool tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_DIAGTOOL_DIAGTOOL_H
#define LLVM_CLANG_TOOLS_DIAGTOOL_DIAGTOOL_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include <string>


namespace diagtool {

class DiagTool {
  const std::string cmd;
  const std::string description;
public:
  DiagTool(llvm::StringRef toolCmd, llvm::StringRef toolDesc);
  virtual ~DiagTool();
  
  llvm::StringRef getName() const { return cmd; }  
  llvm::StringRef getDescription() const { return description; }  

  virtual int run(unsigned argc, char *argv[], llvm::raw_ostream &out) = 0;
};
  
class DiagTools {
  void *tools;
public:
  DiagTools();
  ~DiagTools();
  
  DiagTool *getTool(llvm::StringRef toolCmd);
  void registerTool(DiagTool *tool);  
  void printCommands(llvm::raw_ostream &out);  
};

extern llvm::ManagedStatic<DiagTools> diagTools;

template <typename DIAGTOOL>
class RegisterDiagTool {
public:
  RegisterDiagTool() { diagTools->registerTool(new DIAGTOOL()); }
};

} // end diagtool namespace

#define DEF_DIAGTOOL(NAME, DESC, CLSNAME)\
namespace {\
class CLSNAME : public diagtool::DiagTool {\
public:\
  CLSNAME() : DiagTool(NAME, DESC) {}\
  virtual ~CLSNAME() {}\
  int run(unsigned argc, char *argv[], llvm::raw_ostream &out) override;\
};\
diagtool::RegisterDiagTool<CLSNAME> Register##CLSNAME;\
}

#endif
