#!/usr/bin/env python3

import argparse
import os
import re
import sys


# FIXME: This should get outputs from gn.
OUTPUT = """struct AvailableComponent {
    /// The name of the component.
    const char *Name;

    /// The name of the library for this component (or NULL).
    const char *Library;

    /// Whether the component is installed.
    bool IsInstalled;

    /// The list of libraries required when linking this component.
    const char *RequiredLibraries[84];
  } AvailableComponents[84] = {
  { "aggressiveinstcombine", "LLVMAggressiveInstCombine", true, {"analysis", "core", "support", "transformutils"} },
{ "all", nullptr, true, {"demangle", "support", "tablegen", "core", "fuzzmutate", "filecheck", "interfacestub", "irreader", "codegen", "selectiondag", "asmprinter", "mirparser", "globalisel", "binaryformat", "bitreader", "bitwriter", "bitstreamreader", "dwarflinker", "extensions", "frontendopenmp", "transformutils", "instrumentation", "aggressiveinstcombine", "instcombine", "scalaropts", "ipo", "vectorize", "objcarcopts", "coroutines", "cfguard", "linker", "analysis", "lto", "mc", "mcparser", "mcdisassembler", "mca", "object", "objectyaml", "option", "remarks", "debuginfodwarf", "debuginfogsym", "debuginfomsf", "debuginfocodeview", "debuginfopdb", "symbolize", "executionengine", "interpreter", "jitlink", "mcjit", "orcjit", "orcshared", "orctargetprocess", "runtimedyld", "target", "asmparser", "lineeditor", "profiledata", "coverage", "passes", "textapi", "dlltooldriver", "libdriver", "xray", "windowsmanifest"} },
{ "all-targets", nullptr, true, {} },
{ "analysis", "LLVMAnalysis", true, {"binaryformat", "core", "object", "profiledata", "support"} },
{ "asmparser", "LLVMAsmParser", true, {"binaryformat", "core", "support"} },
{ "asmprinter", "LLVMAsmPrinter", true, {"analysis", "binaryformat", "codegen", "core", "debuginfocodeview", "debuginfodwarf", "debuginfomsf", "mc", "mcparser", "remarks", "support", "target"} },
{ "binaryformat", "LLVMBinaryFormat", true, {"support"} },
{ "bitreader", "LLVMBitReader", true, {"bitstreamreader", "core", "support"} },
{ "bitstreamreader", "LLVMBitstreamReader", true, {"support"} },
{ "bitwriter", "LLVMBitWriter", true, {"analysis", "core", "mc", "object", "support"} },
{ "cfguard", "LLVMCFGuard", true, {"core", "support"} },
{ "codegen", "LLVMCodeGen", true, {"analysis", "bitreader", "bitwriter", "core", "mc", "profiledata", "scalaropts", "support", "target", "transformutils"} },
{ "core", "LLVMCore", true, {"binaryformat", "remarks", "support"} },
{ "coroutines", "LLVMCoroutines", true, {"analysis", "core", "ipo", "scalaropts", "support", "transformutils"} },
{ "coverage", "LLVMCoverage", true, {"core", "object", "profiledata", "support"} },
{ "debuginfocodeview", "LLVMDebugInfoCodeView", true, {"support", "debuginfomsf"} },
{ "debuginfodwarf", "LLVMDebugInfoDWARF", true, {"binaryformat", "object", "mc", "support"} },
{ "debuginfogsym", "LLVMDebugInfoGSYM", true, {"mc", "object", "support", "debuginfodwarf"} },
{ "debuginfomsf", "LLVMDebugInfoMSF", true, {"support"} },
{ "debuginfopdb", "LLVMDebugInfoPDB", true, {"binaryformat", "object", "support", "debuginfocodeview", "debuginfomsf"} },
{ "demangle", "LLVMDemangle", true, {} },
{ "dlltooldriver", "LLVMDlltoolDriver", true, {"object", "option", "support"} },
{ "dwarflinker", "LLVMDWARFLinker", true, {"debuginfodwarf", "asmprinter", "codegen", "mc", "object", "support"} },
{ "engine", nullptr, true, {"interpreter"} },
{ "executionengine", "LLVMExecutionEngine", true, {"core", "mc", "object", "runtimedyld", "support", "target"} },
{ "extensions", "LLVMExtensions", true, {"support"} },
{ "filecheck", "LLVMFileCheck", true, {} },
{ "frontendopenmp", "LLVMFrontendOpenMP", true, {"core", "support", "transformutils"} },
{ "fuzzmutate", "LLVMFuzzMutate", true, {"analysis", "bitreader", "bitwriter", "core", "scalaropts", "support", "target"} },
{ "globalisel", "LLVMGlobalISel", true, {"analysis", "codegen", "core", "mc", "selectiondag", "support", "target", "transformutils"} },
{ "instcombine", "LLVMInstCombine", true, {"analysis", "core", "support", "transformutils"} },
{ "instrumentation", "LLVMInstrumentation", true, {"analysis", "core", "mc", "support", "transformutils", "profiledata"} },
{ "interfacestub", "LLVMInterfaceStub", true, {"object", "support"} },
{ "interpreter", "LLVMInterpreter", true, {"codegen", "core", "executionengine", "support"} },
{ "ipo", "LLVMipo", true, {"aggressiveinstcombine", "analysis", "bitreader", "bitwriter", "core", "frontendopenmp", "instcombine", "irreader", "linker", "object", "profiledata", "scalaropts", "support", "transformutils", "vectorize", "instrumentation"} },
{ "irreader", "LLVMIRReader", true, {"asmparser", "bitreader", "core", "support"} },
{ "jitlink", "LLVMJITLink", true, {"binaryformat", "object", "orctargetprocess", "support"} },
{ "libdriver", "LLVMLibDriver", true, {"binaryformat", "bitreader", "object", "option", "support", "binaryformat", "bitreader", "object", "option", "support"} },
{ "lineeditor", "LLVMLineEditor", true, {"support"} },
{ "linker", "LLVMLinker", true, {"core", "support", "transformutils"} },
{ "lto", "LLVMLTO", true, {"aggressiveinstcombine", "analysis", "binaryformat", "bitreader", "bitwriter", "codegen", "core", "extensions", "ipo", "instcombine", "linker", "mc", "objcarcopts", "object", "passes", "remarks", "scalaropts", "support", "target", "transformutils"} },
{ "mc", "LLVMMC", true, {"support", "binaryformat", "debuginfocodeview"} },
{ "mca", "LLVMMCA", true, {"mc", "support"} },
{ "mcdisassembler", "LLVMMCDisassembler", true, {"mc", "support"} },
{ "mcjit", "LLVMMCJIT", true, {"core", "executionengine", "object", "runtimedyld", "support", "target"} },
{ "mcparser", "LLVMMCParser", true, {"mc", "support"} },
{ "mirparser", "LLVMMIRParser", true, {"asmparser", "binaryformat", "codegen", "core", "mc", "support", "target"} },
{ "native", nullptr, true, {} },
{ "nativecodegen", nullptr, true, {} },
{ "objcarcopts", "LLVMObjCARCOpts", true, {"analysis", "core", "support", "transformutils"} },
{ "object", "LLVMObject", true, {"bitreader", "core", "mc", "binaryformat", "mcparser", "support", "textapi"} },
{ "objectyaml", "LLVMObjectYAML", true, {"binaryformat", "object", "support", "debuginfocodeview", "mc"} },
{ "option", "LLVMOption", true, {"support"} },
{ "orcjit", "LLVMOrcJIT", true, {"core", "executionengine", "jitlink", "object", "orcshared", "orctargetprocess", "mc", "passes", "runtimedyld", "support", "target", "transformutils"} },
{ "orcshared", "LLVMOrcShared", true, {"support"} },
{ "orctargetprocess", "LLVMOrcTargetProcess", true, {"orcshared", "support"} },
{ "passes", "LLVMPasses", true, {"aggressiveinstcombine", "analysis", "core", "coroutines", "ipo", "instcombine", "objcarcopts", "scalaropts", "support", "target", "transformutils", "vectorize", "instrumentation"} },
{ "profiledata", "LLVMProfileData", true, {"core", "support", "demangle"} },
{ "remarks", "LLVMRemarks", true, {"bitstreamreader", "support"} },
{ "runtimedyld", "LLVMRuntimeDyld", true, {"core", "mc", "object", "support"} },
{ "scalaropts", "LLVMScalarOpts", true, {"aggressiveinstcombine", "analysis", "core", "instcombine", "support", "transformutils"} },
{ "selectiondag", "LLVMSelectionDAG", true, {"analysis", "codegen", "core", "mc", "support", "target", "transformutils"} },
{ "support", "LLVMSupport", true, {"demangle"} },
{ "symbolize", "LLVMSymbolize", true, {"debuginfodwarf", "debuginfopdb", "object", "support", "demangle"} },
{ "tablegen", "LLVMTableGen", true, {"support"} },
{ "target", "LLVMTarget", true, {"analysis", "core", "mc", "support"} },
{ "textapi", "LLVMTextAPI", true, {"support", "binaryformat"} },
{ "transformutils", "LLVMTransformUtils", true, {"analysis", "core", "support"} },
{ "vectorize", "LLVMVectorize", true, {"analysis", "core", "support", "transformutils"} },
{ "windowsmanifest", "LLVMWindowsManifest", true, {"support"} },
{ "xray", "LLVMXRay", true, {"support", "object"} },
};
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output", required=True, help="output file")
    args = parser.parse_args()

    with open(args.output, "w") as f:
        f.write(OUTPUT)


if __name__ == "__main__":
    sys.exit(main())
