// SPDX-License-Identifier: GPL-2.0
/*
 * llvm C frontend for perf. Support dynamically compile C file
 *
 * Inspired by clang example code:
 * http://llvm.org/svn/llvm-project/cfe/trunk/examples/clang-interpreter/main.cpp
 *
 * Copyright (C) 2016 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2016 Huawei Inc.
 */

#include "clang/Basic/Version.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <memory>

#include "clang.h"
#include "clang-c.h"

namespace perf {

static std::unique_ptr<llvm::LLVMContext> LLVMCtx;

using namespace clang;

static CompilerInvocation *
createCompilerInvocation(llvm::opt::ArgStringList CFlags, StringRef& Path,
			 DiagnosticsEngine& Diags)
{
	llvm::opt::ArgStringList CCArgs {
		"-cc1",
		"-triple", "bpf-pc-linux",
		"-fsyntax-only",
		"-ferror-limit", "19",
		"-fmessage-length", "127",
		"-O2",
		"-nostdsysteminc",
		"-nobuiltininc",
		"-vectorize-loops",
		"-vectorize-slp",
		"-Wno-unused-value",
		"-Wno-pointer-sign",
		"-x", "c"};

	CCArgs.append(CFlags.begin(), CFlags.end());
	CompilerInvocation *CI = tooling::newInvocation(&Diags, CCArgs);

	FrontendOptions& Opts = CI->getFrontendOpts();
	Opts.Inputs.clear();
	Opts.Inputs.emplace_back(Path,
			FrontendOptions::getInputKindForExtension("c"));
	return CI;
}

static std::unique_ptr<llvm::Module>
getModuleFromSource(llvm::opt::ArgStringList CFlags,
		    StringRef Path, IntrusiveRefCntPtr<vfs::FileSystem> VFS)
{
	CompilerInstance Clang;
	Clang.createDiagnostics();

	Clang.setVirtualFileSystem(&*VFS);

#if CLANG_VERSION_MAJOR < 4
	IntrusiveRefCntPtr<CompilerInvocation> CI =
		createCompilerInvocation(std::move(CFlags), Path,
					 Clang.getDiagnostics());
	Clang.setInvocation(&*CI);
#else
	std::shared_ptr<CompilerInvocation> CI(
		createCompilerInvocation(std::move(CFlags), Path,
					 Clang.getDiagnostics()));
	Clang.setInvocation(CI);
#endif

	std::unique_ptr<CodeGenAction> Act(new EmitLLVMOnlyAction(&*LLVMCtx));
	if (!Clang.ExecuteAction(*Act))
		return std::unique_ptr<llvm::Module>(nullptr);

	return Act->takeModule();
}

std::unique_ptr<llvm::Module>
getModuleFromSource(llvm::opt::ArgStringList CFlags,
		    StringRef Name, StringRef Content)
{
	using namespace vfs;

	llvm::IntrusiveRefCntPtr<OverlayFileSystem> OverlayFS(
			new OverlayFileSystem(getRealFileSystem()));
	llvm::IntrusiveRefCntPtr<InMemoryFileSystem> MemFS(
			new InMemoryFileSystem(true));

	/*
	 * pushOverlay helps setting working dir for MemFS. Must call
	 * before addFile.
	 */
	OverlayFS->pushOverlay(MemFS);
	MemFS->addFile(Twine(Name), 0, llvm::MemoryBuffer::getMemBuffer(Content));

	return getModuleFromSource(std::move(CFlags), Name, OverlayFS);
}

std::unique_ptr<llvm::Module>
getModuleFromSource(llvm::opt::ArgStringList CFlags, StringRef Path)
{
	IntrusiveRefCntPtr<vfs::FileSystem> VFS(vfs::getRealFileSystem());
	return getModuleFromSource(std::move(CFlags), Path, VFS);
}

std::unique_ptr<llvm::SmallVectorImpl<char>>
getBPFObjectFromModule(llvm::Module *Module)
{
	using namespace llvm;

	std::string TargetTriple("bpf-pc-linux");
	std::string Error;
	const Target* Target = TargetRegistry::lookupTarget(TargetTriple, Error);
	if (!Target) {
		llvm::errs() << Error;
		return std::unique_ptr<llvm::SmallVectorImpl<char>>(nullptr);
	}

	llvm::TargetOptions Opt;
	TargetMachine *TargetMachine =
		Target->createTargetMachine(TargetTriple,
					    "generic", "",
					    Opt, Reloc::Static);

	Module->setDataLayout(TargetMachine->createDataLayout());
	Module->setTargetTriple(TargetTriple);

	std::unique_ptr<SmallVectorImpl<char>> Buffer(new SmallVector<char, 0>());
	raw_svector_ostream ostream(*Buffer);

	legacy::PassManager PM;
	bool NotAdded;
#if CLANG_VERSION_MAJOR < 7
	NotAdded = TargetMachine->addPassesToEmitFile(PM, ostream,
						      TargetMachine::CGFT_ObjectFile);
#else
	NotAdded = TargetMachine->addPassesToEmitFile(PM, ostream, nullptr,
						      TargetMachine::CGFT_ObjectFile);
#endif
	if (NotAdded) {
		llvm::errs() << "TargetMachine can't emit a file of this type\n";
		return std::unique_ptr<llvm::SmallVectorImpl<char>>(nullptr);;
	}
	PM.run(*Module);

	return std::move(Buffer);
}

}

extern "C" {
void perf_clang__init(void)
{
	perf::LLVMCtx.reset(new llvm::LLVMContext());
	LLVMInitializeBPFTargetInfo();
	LLVMInitializeBPFTarget();
	LLVMInitializeBPFTargetMC();
	LLVMInitializeBPFAsmPrinter();
}

void perf_clang__cleanup(void)
{
	perf::LLVMCtx.reset(nullptr);
	llvm::llvm_shutdown();
}

int perf_clang__compile_bpf(const char *filename,
			    void **p_obj_buf,
			    size_t *p_obj_buf_sz)
{
	using namespace perf;

	if (!p_obj_buf || !p_obj_buf_sz)
		return -EINVAL;

	llvm::opt::ArgStringList CFlags;
	auto M = getModuleFromSource(std::move(CFlags), filename);
	if (!M)
		return  -EINVAL;
	auto O = getBPFObjectFromModule(&*M);
	if (!O)
		return -EINVAL;

	size_t size = O->size_in_bytes();
	void *buffer;

	buffer = malloc(size);
	if (!buffer)
		return -ENOMEM;
	memcpy(buffer, O->data(), size);
	*p_obj_buf = buffer;
	*p_obj_buf_sz = size;
	return 0;
}
}
