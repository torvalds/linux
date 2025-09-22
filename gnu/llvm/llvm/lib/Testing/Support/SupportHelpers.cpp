
#include "llvm/Testing/Support/SupportHelpers.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::unittest;

static std::pair<bool, SmallString<128>> findSrcDirMap(StringRef Argv0) {
  SmallString<128> BaseDir = llvm::sys::path::parent_path(Argv0);

  llvm::sys::fs::make_absolute(BaseDir);

  SmallString<128> PathInSameDir = BaseDir;
  llvm::sys::path::append(PathInSameDir, "llvm.srcdir.txt");

  if (llvm::sys::fs::is_regular_file(PathInSameDir))
    return std::make_pair(true, std::move(PathInSameDir));

  SmallString<128> PathInParentDir = llvm::sys::path::parent_path(BaseDir);

  llvm::sys::path::append(PathInParentDir, "llvm.srcdir.txt");
  if (llvm::sys::fs::is_regular_file(PathInParentDir))
    return std::make_pair(true, std::move(PathInParentDir));

  return std::pair<bool, SmallString<128>>(false, {});
}

SmallString<128> llvm::unittest::getInputFileDirectory(const char *Argv0) {
  bool Found = false;
  SmallString<128> InputFilePath;
  std::tie(Found, InputFilePath) = findSrcDirMap(Argv0);

  EXPECT_TRUE(Found) << "Unit test source directory file does not exist.";

  auto File = MemoryBuffer::getFile(InputFilePath, /*IsText=*/true);

  EXPECT_TRUE(static_cast<bool>(File))
      << "Could not open unit test source directory file.";

  InputFilePath.clear();
  InputFilePath.append((*File)->getBuffer().trim());
  llvm::sys::path::append(InputFilePath, "Inputs");
  llvm::sys::path::native(InputFilePath);
  return InputFilePath;
}
