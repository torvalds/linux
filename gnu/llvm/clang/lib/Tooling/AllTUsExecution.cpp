//===- lib/Tooling/AllTUsExecution.cpp - Execute actions on all TUs. ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/AllTUsExecution.h"
#include "clang/Tooling/ToolExecutorPluginRegistry.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"

namespace clang {
namespace tooling {

const char *AllTUsToolExecutor::ExecutorName = "AllTUsToolExecutor";

namespace {
llvm::Error make_string_error(const llvm::Twine &Message) {
  return llvm::make_error<llvm::StringError>(Message,
                                             llvm::inconvertibleErrorCode());
}

ArgumentsAdjuster getDefaultArgumentsAdjusters() {
  return combineAdjusters(
      getClangStripOutputAdjuster(),
      combineAdjusters(getClangSyntaxOnlyAdjuster(),
                       getClangStripDependencyFileAdjuster()));
}

class ThreadSafeToolResults : public ToolResults {
public:
  void addResult(StringRef Key, StringRef Value) override {
    std::unique_lock<std::mutex> LockGuard(Mutex);
    Results.addResult(Key, Value);
  }

  std::vector<std::pair<llvm::StringRef, llvm::StringRef>>
  AllKVResults() override {
    return Results.AllKVResults();
  }

  void forEachResult(llvm::function_ref<void(StringRef Key, StringRef Value)>
                         Callback) override {
    Results.forEachResult(Callback);
  }

private:
  InMemoryToolResults Results;
  std::mutex Mutex;
};

} // namespace

llvm::cl::opt<std::string>
    Filter("filter",
           llvm::cl::desc("Only process files that match this filter. "
                          "This flag only applies to all-TUs."),
           llvm::cl::init(".*"));

AllTUsToolExecutor::AllTUsToolExecutor(
    const CompilationDatabase &Compilations, unsigned ThreadCount,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : Compilations(Compilations), Results(new ThreadSafeToolResults),
      Context(Results.get()), ThreadCount(ThreadCount) {}

AllTUsToolExecutor::AllTUsToolExecutor(
    CommonOptionsParser Options, unsigned ThreadCount,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : OptionsParser(std::move(Options)),
      Compilations(OptionsParser->getCompilations()),
      Results(new ThreadSafeToolResults), Context(Results.get()),
      ThreadCount(ThreadCount) {}

llvm::Error AllTUsToolExecutor::execute(
    llvm::ArrayRef<
        std::pair<std::unique_ptr<FrontendActionFactory>, ArgumentsAdjuster>>
        Actions) {
  if (Actions.empty())
    return make_string_error("No action to execute.");

  if (Actions.size() != 1)
    return make_string_error(
        "Only support executing exactly 1 action at this point.");

  std::string ErrorMsg;
  std::mutex TUMutex;
  auto AppendError = [&](llvm::Twine Err) {
    std::unique_lock<std::mutex> LockGuard(TUMutex);
    ErrorMsg += Err.str();
  };

  auto Log = [&](llvm::Twine Msg) {
    std::unique_lock<std::mutex> LockGuard(TUMutex);
    llvm::errs() << Msg.str() << "\n";
  };

  std::vector<std::string> Files;
  llvm::Regex RegexFilter(Filter);
  for (const auto& File : Compilations.getAllFiles()) {
    if (RegexFilter.match(File))
      Files.push_back(File);
  }
  // Add a counter to track the progress.
  const std::string TotalNumStr = std::to_string(Files.size());
  unsigned Counter = 0;
  auto Count = [&]() {
    std::unique_lock<std::mutex> LockGuard(TUMutex);
    return ++Counter;
  };

  auto &Action = Actions.front();

  {
    llvm::DefaultThreadPool Pool(llvm::hardware_concurrency(ThreadCount));
    for (std::string File : Files) {
      Pool.async(
          [&](std::string Path) {
            Log("[" + std::to_string(Count()) + "/" + TotalNumStr +
                "] Processing file " + Path);
            // Each thread gets an independent copy of a VFS to allow different
            // concurrent working directories.
            IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
                llvm::vfs::createPhysicalFileSystem();
            ClangTool Tool(Compilations, {Path},
                           std::make_shared<PCHContainerOperations>(), FS);
            Tool.appendArgumentsAdjuster(Action.second);
            Tool.appendArgumentsAdjuster(getDefaultArgumentsAdjusters());
            for (const auto &FileAndContent : OverlayFiles)
              Tool.mapVirtualFile(FileAndContent.first(),
                                  FileAndContent.second);
            if (Tool.run(Action.first.get()))
              AppendError(llvm::Twine("Failed to run action on ") + Path +
                          "\n");
          },
          File);
    }
    // Make sure all tasks have finished before resetting the working directory.
    Pool.wait();
  }

  if (!ErrorMsg.empty())
    return make_string_error(ErrorMsg);

  return llvm::Error::success();
}

llvm::cl::opt<unsigned> ExecutorConcurrency(
    "execute-concurrency",
    llvm::cl::desc("The number of threads used to process all files in "
                   "parallel. Set to 0 for hardware concurrency. "
                   "This flag only applies to all-TUs."),
    llvm::cl::init(0));

class AllTUsToolExecutorPlugin : public ToolExecutorPlugin {
public:
  llvm::Expected<std::unique_ptr<ToolExecutor>>
  create(CommonOptionsParser &OptionsParser) override {
    if (OptionsParser.getSourcePathList().empty())
      return make_string_error(
          "[AllTUsToolExecutorPlugin] Please provide a directory/file path in "
          "the compilation database.");
    return std::make_unique<AllTUsToolExecutor>(std::move(OptionsParser),
                                                 ExecutorConcurrency);
  }
};

static ToolExecutorPluginRegistry::Add<AllTUsToolExecutorPlugin>
    X("all-TUs", "Runs FrontendActions on all TUs in the compilation database. "
                 "Tool results are stored in memory.");

// This anchor is used to force the linker to link in the generated object file
// and thus register the plugin.
volatile int AllTUsToolExecutorAnchorSource = 0;

} // end namespace tooling
} // end namespace clang
