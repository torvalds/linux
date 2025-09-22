#include "llvm/Support/ScopedPrinter.h"

#include "llvm/Support/Format.h"

using namespace llvm::support;

namespace llvm {

raw_ostream &operator<<(raw_ostream &OS, const HexNumber &Value) {
  OS << "0x" << utohexstr(Value.Value);
  return OS;
}

void ScopedPrinter::printBinaryImpl(StringRef Label, StringRef Str,
                                    ArrayRef<uint8_t> Data, bool Block,
                                    uint32_t StartOffset) {
  if (Data.size() > 16)
    Block = true;

  if (Block) {
    startLine() << Label;
    if (!Str.empty())
      OS << ": " << Str;
    OS << " (\n";
    if (!Data.empty())
      OS << format_bytes_with_ascii(Data, StartOffset, 16, 4,
                                    (IndentLevel + 1) * 2, true)
         << "\n";
    startLine() << ")\n";
  } else {
    startLine() << Label << ":";
    if (!Str.empty())
      OS << " " << Str;
    OS << " (" << format_bytes(Data, std::nullopt, Data.size(), 1, 0, true)
       << ")\n";
  }
}

JSONScopedPrinter::JSONScopedPrinter(
    raw_ostream &OS, bool PrettyPrint,
    std::unique_ptr<DelimitedScope> &&OuterScope)
    : ScopedPrinter(OS, ScopedPrinter::ScopedPrinterKind::JSON),
      JOS(OS, /*Indent=*/PrettyPrint ? 2 : 0),
      OuterScope(std::move(OuterScope)) {
  if (this->OuterScope)
    this->OuterScope->setPrinter(*this);
}

} // namespace llvm
