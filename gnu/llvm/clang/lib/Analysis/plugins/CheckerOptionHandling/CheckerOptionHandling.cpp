#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

using namespace clang;
using namespace ento;

namespace {
struct MyChecker : public Checker<check::BeginFunction> {
  void checkBeginFunction(CheckerContext &Ctx) const {}
};

void registerMyChecker(CheckerManager &Mgr) {
  MyChecker *Checker = Mgr.registerChecker<MyChecker>();
  llvm::outs() << "Example option is set to "
               << (Mgr.getAnalyzerOptions().getCheckerBooleanOption(
                       Checker, "ExampleOption")
                       ? "true"
                       : "false")
               << '\n';
}

bool shouldRegisterMyChecker(const CheckerManager &mgr) { return true; }

} // end anonymous namespace

// Register plugin!
extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker(registerMyChecker, shouldRegisterMyChecker,
                      "example.MyChecker", "Example Description",
                      "example.mychecker.documentation.nonexistent.html",
                      /*isHidden*/false);

  registry.addCheckerOption(/*OptionType*/ "bool",
                            /*CheckerFullName*/ "example.MyChecker",
                            /*OptionName*/ "ExampleOption",
                            /*DefaultValStr*/ "false",
                            /*Description*/ "This is an example checker opt.",
                            /*DevelopmentStage*/ "released");
}

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;
