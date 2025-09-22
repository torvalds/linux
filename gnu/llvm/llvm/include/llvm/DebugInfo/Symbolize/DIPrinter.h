//===- llvm/DebugInfo/Symbolize/DIPrinter.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DIPrinter class, which is responsible for printing
// structures defined in DebugInfo/DIContext.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_DIPRINTER_H
#define LLVM_DEBUGINFO_SYMBOLIZE_DIPRINTER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <memory>
#include <vector>

namespace llvm {
struct DILineInfo;
class DIInliningInfo;
struct DIGlobal;
struct DILocal;
class ErrorInfoBase;
class raw_ostream;

namespace symbolize {

class SourceCode;

struct Request {
  StringRef ModuleName;
  std::optional<uint64_t> Address;
  StringRef Symbol;
};

class DIPrinter {
public:
  DIPrinter() = default;
  virtual ~DIPrinter() = default;

  virtual void print(const Request &Request, const DILineInfo &Info) = 0;
  virtual void print(const Request &Request, const DIInliningInfo &Info) = 0;
  virtual void print(const Request &Request, const DIGlobal &Global) = 0;
  virtual void print(const Request &Request,
                     const std::vector<DILocal> &Locals) = 0;
  virtual void print(const Request &Request,
                     const std::vector<DILineInfo> &Locations) = 0;

  virtual bool printError(const Request &Request,
                          const ErrorInfoBase &ErrorInfo) = 0;

  virtual void listBegin() = 0;
  virtual void listEnd() = 0;
};

struct PrinterConfig {
  bool PrintAddress;
  bool PrintFunctions;
  bool Pretty;
  bool Verbose;
  int SourceContextLines;
};

using ErrorHandler = function_ref<void(const ErrorInfoBase &, StringRef)>;

class PlainPrinterBase : public DIPrinter {
protected:
  raw_ostream &OS;
  ErrorHandler ErrHandler;
  PrinterConfig Config;

  void print(const DILineInfo &Info, bool Inlined);
  void printFunctionName(StringRef FunctionName, bool Inlined);
  virtual void printSimpleLocation(StringRef Filename,
                                   const DILineInfo &Info) = 0;
  void printContext(SourceCode SourceCode);
  void printVerbose(StringRef Filename, const DILineInfo &Info);
  virtual void printStartAddress(const DILineInfo &Info) {}
  virtual void printFooter() {}

private:
  void printHeader(std::optional<uint64_t> Address);

public:
  PlainPrinterBase(raw_ostream &OS, ErrorHandler EH, PrinterConfig &Config)
      : OS(OS), ErrHandler(EH), Config(Config) {}

  void print(const Request &Request, const DILineInfo &Info) override;
  void print(const Request &Request, const DIInliningInfo &Info) override;
  void print(const Request &Request, const DIGlobal &Global) override;
  void print(const Request &Request,
             const std::vector<DILocal> &Locals) override;
  void print(const Request &Request,
             const std::vector<DILineInfo> &Locations) override;

  bool printError(const Request &Request,
                  const ErrorInfoBase &ErrorInfo) override;

  void listBegin() override {}
  void listEnd() override {}
};

class LLVMPrinter : public PlainPrinterBase {
private:
  void printSimpleLocation(StringRef Filename, const DILineInfo &Info) override;
  void printStartAddress(const DILineInfo &Info) override;
  void printFooter() override;

public:
  LLVMPrinter(raw_ostream &OS, ErrorHandler EH, PrinterConfig &Config)
      : PlainPrinterBase(OS, EH, Config) {}
};

class GNUPrinter : public PlainPrinterBase {
private:
  void printSimpleLocation(StringRef Filename, const DILineInfo &Info) override;

public:
  GNUPrinter(raw_ostream &OS, ErrorHandler EH, PrinterConfig &Config)
      : PlainPrinterBase(OS, EH, Config) {}

};

class JSONPrinter : public DIPrinter {
private:
  raw_ostream &OS;
  PrinterConfig Config;
  std::unique_ptr<json::Array> ObjectList;

  void printJSON(const json::Value &V) {
    json::OStream JOS(OS, Config.Pretty ? 2 : 0);
    JOS.value(V);
    OS << '\n';
  }

public:
  JSONPrinter(raw_ostream &OS, PrinterConfig &Config)
      : OS(OS), Config(Config) {}

  void print(const Request &Request, const DILineInfo &Info) override;
  void print(const Request &Request, const DIInliningInfo &Info) override;
  void print(const Request &Request, const DIGlobal &Global) override;
  void print(const Request &Request,
             const std::vector<DILocal> &Locals) override;
  void print(const Request &Request,
             const std::vector<DILineInfo> &Locations) override;

  bool printError(const Request &Request,
                  const ErrorInfoBase &ErrorInfo) override;

  void listBegin() override;
  void listEnd() override;
};
} // namespace symbolize
} // namespace llvm

#endif
