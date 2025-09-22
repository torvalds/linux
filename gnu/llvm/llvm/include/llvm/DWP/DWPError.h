#ifndef LLVM_DWP_DWPERROR_H
#define LLVM_DWP_DWPERROR_H

#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

namespace llvm {
class DWPError : public ErrorInfo<DWPError> {
public:
  DWPError(std::string Info) : Info(std::move(Info)) {}
  void log(raw_ostream &OS) const override { OS << Info; }
  std::error_code convertToErrorCode() const override {
    llvm_unreachable("Not implemented");
  }
  static char ID;

private:
  std::string Info;
};
} // namespace llvm

#endif // LLVM_DWP_DWPERROR_H
