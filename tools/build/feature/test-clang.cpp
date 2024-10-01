// SPDX-License-Identifier: GPL-2.0
#include "clang/Basic/Version.h"
#if CLANG_VERSION_MAJOR < 8
#include "clang/Basic/VirtualFileSystem.h"
#endif
#include "clang/Driver/Driver.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/ManagedStatic.h"
#if CLANG_VERSION_MAJOR >= 8
#include "llvm/Support/VirtualFileSystem.h"
#endif
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;

int main()
{
	IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
	IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();

	DiagnosticsEngine Diags(DiagID, &*DiagOpts);
	Driver TheDriver("test", "bpf-pc-linux", Diags);

	llvm::llvm_shutdown();
	return 0;
}
