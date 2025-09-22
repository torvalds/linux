#include "llvm/Support/Locale.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Unicode.h"

namespace llvm {
namespace sys {
namespace locale {

int columnWidth(StringRef Text) {
  return llvm::sys::unicode::columnWidthUTF8(Text);
}

bool isPrint(int UCS) {
  return llvm::sys::unicode::isPrintable(UCS);
}

} // namespace locale
} // namespace sys
} // namespace llvm
