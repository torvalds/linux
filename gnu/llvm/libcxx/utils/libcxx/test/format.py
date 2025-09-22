# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

import lit
import libcxx.test.config as config
import lit.formats
import os
import re


def _getTempPaths(test):
    """
    Return the values to use for the %T and %t substitutions, respectively.

    The difference between this and Lit's default behavior is that we guarantee
    that %T is a path unique to the test being run.
    """
    tmpDir, _ = lit.TestRunner.getTempPaths(test)
    _, testName = os.path.split(test.getExecPath())
    tmpDir = os.path.join(tmpDir, testName + ".dir")
    tmpBase = os.path.join(tmpDir, "t")
    return tmpDir, tmpBase


def _checkBaseSubstitutions(substitutions):
    substitutions = [s for (s, _) in substitutions]
    for s in ["%{cxx}", "%{compile_flags}", "%{link_flags}", "%{flags}", "%{exec}"]:
        assert s in substitutions, "Required substitution {} was not provided".format(s)

def _executeScriptInternal(test, litConfig, commands):
    """
    Returns (stdout, stderr, exitCode, timeoutInfo, parsedCommands)

    TODO: This really should be easier to access from Lit itself
    """
    parsedCommands = parseScript(test, preamble=commands)

    _, tmpBase = _getTempPaths(test)
    execDir = os.path.dirname(test.getExecPath())
    try:
        res = lit.TestRunner.executeScriptInternal(
            test, litConfig, tmpBase, parsedCommands, execDir, debug=False
        )
    except lit.TestRunner.ScriptFatal as e:
        res = ("", str(e), 127, None)
    (out, err, exitCode, timeoutInfo) = res

    return (out, err, exitCode, timeoutInfo, parsedCommands)


def _validateModuleDependencies(modules):
    for m in modules:
        if m not in ("std", "std.compat"):
            raise RuntimeError(
                f"Invalid module dependency '{m}', only 'std' and 'std.compat' are valid"
            )


def parseScript(test, preamble):
    """
    Extract the script from a test, with substitutions applied.

    Returns a list of commands ready to be executed.

    - test
        The lit.Test to parse.

    - preamble
        A list of commands to perform before any command in the test.
        These commands can contain unexpanded substitutions, but they
        must not be of the form 'RUN:' -- they must be proper commands
        once substituted.
    """
    # Get the default substitutions
    tmpDir, tmpBase = _getTempPaths(test)
    substitutions = lit.TestRunner.getDefaultSubstitutions(test, tmpDir, tmpBase)

    # Check base substitutions and add the %{build}, %{verify} and %{run} convenience substitutions
    #
    # Note: We use -Wno-error with %{verify} to make sure that we don't treat all diagnostics as
    #       errors, which doesn't make sense for clang-verify tests because we may want to check
    #       for specific warning diagnostics.
    _checkBaseSubstitutions(substitutions)
    substitutions.append(
        ("%{build}", "%{cxx} %s %{flags} %{compile_flags} %{link_flags} -o %t.exe")
    )
    substitutions.append(
        (
            "%{verify}",
            "%{cxx} %s %{flags} %{compile_flags} -fsyntax-only -Wno-error -Xclang -verify -Xclang -verify-ignore-unexpected=note -ferror-limit=0",
        )
    )
    substitutions.append(("%{run}", "%{exec} %t.exe"))

    # Parse the test file, including custom directives
    additionalCompileFlags = []
    fileDependencies = []
    modules = []  # The enabled modules
    moduleCompileFlags = []  # The compilation flags to use modules
    parsers = [
        lit.TestRunner.IntegratedTestKeywordParser(
            "FILE_DEPENDENCIES:",
            lit.TestRunner.ParserKind.LIST,
            initial_value=fileDependencies,
        ),
        lit.TestRunner.IntegratedTestKeywordParser(
            "ADDITIONAL_COMPILE_FLAGS:",
            lit.TestRunner.ParserKind.SPACE_LIST,
            initial_value=additionalCompileFlags,
        ),
        lit.TestRunner.IntegratedTestKeywordParser(
            "MODULE_DEPENDENCIES:",
            lit.TestRunner.ParserKind.SPACE_LIST,
            initial_value=modules,
        ),
    ]

    # Add conditional parsers for ADDITIONAL_COMPILE_FLAGS. This should be replaced by first
    # class support for conditional keywords in Lit, which would allow evaluating arbitrary
    # Lit boolean expressions instead.
    for feature in test.config.available_features:
        parser = lit.TestRunner.IntegratedTestKeywordParser(
            "ADDITIONAL_COMPILE_FLAGS({}):".format(feature),
            lit.TestRunner.ParserKind.SPACE_LIST,
            initial_value=additionalCompileFlags,
        )
        parsers.append(parser)

    scriptInTest = lit.TestRunner.parseIntegratedTestScript(
        test, additional_parsers=parsers, require_script=not preamble
    )
    if isinstance(scriptInTest, lit.Test.Result):
        return scriptInTest

    script = []

    # For each file dependency in FILE_DEPENDENCIES, inject a command to copy
    # that file to the execution directory. Execute the copy from %S to allow
    # relative paths from the test directory.
    for dep in fileDependencies:
        script += ["%dbg(SETUP) cd %S && cp {} %T".format(dep)]
    script += preamble
    script += scriptInTest

    # Add compile flags specified with ADDITIONAL_COMPILE_FLAGS.
    # Modules need to be built with the same compilation flags as the
    # test. So add these flags before adding the modules.
    substitutions = config._appendToSubstitution(
        substitutions, "%{compile_flags}", " ".join(additionalCompileFlags)
    )

    if modules:
        _validateModuleDependencies(modules)

        # The moduleCompileFlags are added to the %{compile_flags}, but
        # the modules need to be built without these flags. So expand the
        # %{compile_flags} eagerly and hardcode them in the build script.
        compileFlags = config._getSubstitution("%{compile_flags}", test.config)

        # Building the modules needs to happen before the other script
        # commands are executed. Therefore the commands are added to the
        # front of the list.
        if "std.compat" in modules:
            script.insert(
                0,
                "%dbg(MODULE std.compat) %{cxx} %{flags} "
                f"{compileFlags} "
                "-Wno-reserved-module-identifier -Wno-reserved-user-defined-literal "
                "-fmodule-file=std=%T/std.pcm " # The std.compat module imports std.
                "--precompile -o %T/std.compat.pcm -c %{module-dir}/std.compat.cppm",
            )
            moduleCompileFlags.extend(
                ["-fmodule-file=std.compat=%T/std.compat.pcm", "%T/std.compat.pcm"]
            )

        # Make sure the std module is built before std.compat. Libc++'s
        # std.compat module depends on the std module. It is not
        # known whether the compiler expects the modules in the order of
        # their dependencies. However it's trivial to provide them in
        # that order.
        script.insert(
            0,
            "%dbg(MODULE std) %{cxx} %{flags} "
            f"{compileFlags} "
            "-Wno-reserved-module-identifier -Wno-reserved-user-defined-literal "
            "--precompile -o %T/std.pcm -c %{module-dir}/std.cppm",
        )
        moduleCompileFlags.extend(["-fmodule-file=std=%T/std.pcm", "%T/std.pcm"])

        # Add compile flags required for the modules.
        substitutions = config._appendToSubstitution(
            substitutions, "%{compile_flags}", " ".join(moduleCompileFlags)
        )

    # Perform substitutions in the script itself.
    script = lit.TestRunner.applySubstitutions(
        script, substitutions, recursion_limit=test.config.recursiveExpansionLimit
    )

    return script


class CxxStandardLibraryTest(lit.formats.FileBasedTest):
    """
    Lit test format for the C++ Standard Library conformance test suite.

    Lit tests are contained in files that follow a certain pattern, which determines the semantics of the test.
    Under the hood, we basically generate a builtin Lit shell test that follows the ShTest format, and perform
    the appropriate operations (compile/link/run). See
    https://libcxx.llvm.org/TestingLibcxx.html#test-names
    for a complete description of those semantics.

    Substitution requirements
    ===============================
    The test format operates by assuming that each test's configuration provides
    the following substitutions, which it will reuse in the shell scripts it
    constructs:
        %{cxx}           - A command that can be used to invoke the compiler
        %{compile_flags} - Flags to use when compiling a test case
        %{link_flags}    - Flags to use when linking a test case
        %{flags}         - Flags to use either when compiling or linking a test case
        %{exec}          - A command to prefix the execution of executables

    Note that when building an executable (as opposed to only compiling a source
    file), all three of %{flags}, %{compile_flags} and %{link_flags} will be used
    in the same command line. In other words, the test format doesn't perform
    separate compilation and linking steps in this case.

    Additional provided substitutions and features
    ==============================================
    The test format will define the following substitutions for use inside tests:

        %{build}
            Expands to a command-line that builds the current source
            file with the %{flags}, %{compile_flags} and %{link_flags}
            substitutions, and that produces an executable named %t.exe.

        %{verify}
            Expands to a command-line that builds the current source
            file with the %{flags} and %{compile_flags} substitutions
            and enables clang-verify. This can be used to write .sh.cpp
            tests that use clang-verify. Note that this substitution can
            only be used when the 'verify-support' feature is available.

        %{run}
            Equivalent to `%{exec} %t.exe`. This is intended to be used
            in conjunction with the %{build} substitution.
    """

    def getTestsForPath(self, testSuite, pathInSuite, litConfig, localConfig):
        SUPPORTED_SUFFIXES = [
            "[.]pass[.]cpp$",
            "[.]pass[.]mm$",
            "[.]compile[.]pass[.]cpp$",
            "[.]compile[.]pass[.]mm$",
            "[.]compile[.]fail[.]cpp$",
            "[.]link[.]pass[.]cpp$",
            "[.]link[.]pass[.]mm$",
            "[.]link[.]fail[.]cpp$",
            "[.]sh[.][^.]+$",
            "[.]gen[.][^.]+$",
            "[.]verify[.]cpp$",
        ]

        sourcePath = testSuite.getSourcePath(pathInSuite)
        filename = os.path.basename(sourcePath)

        # Ignore dot files, excluded tests and tests with an unsupported suffix
        hasSupportedSuffix = lambda f: any([re.search(ext, f) for ext in SUPPORTED_SUFFIXES])
        if filename.startswith(".") or filename in localConfig.excludes or not hasSupportedSuffix(filename):
            return

        # If this is a generated test, run the generation step and add
        # as many Lit tests as necessary.
        if re.search('[.]gen[.][^.]+$', filename):
            for test in self._generateGenTest(testSuite, pathInSuite, litConfig, localConfig):
                yield test
        else:
            yield lit.Test.Test(testSuite, pathInSuite, localConfig)

    def execute(self, test, litConfig):
        supportsVerify = "verify-support" in test.config.available_features
        filename = test.path_in_suite[-1]

        if re.search("[.]sh[.][^.]+$", filename):
            steps = []  # The steps are already in the script
            return self._executeShTest(test, litConfig, steps)
        elif filename.endswith(".compile.pass.cpp") or filename.endswith(
            ".compile.pass.mm"
        ):
            steps = [
                "%dbg(COMPILED WITH) %{cxx} %s %{flags} %{compile_flags} -fsyntax-only"
            ]
            return self._executeShTest(test, litConfig, steps)
        elif filename.endswith(".compile.fail.cpp"):
            steps = [
                "%dbg(COMPILED WITH) ! %{cxx} %s %{flags} %{compile_flags} -fsyntax-only"
            ]
            return self._executeShTest(test, litConfig, steps)
        elif filename.endswith(".link.pass.cpp") or filename.endswith(".link.pass.mm"):
            steps = [
                "%dbg(COMPILED WITH) %{cxx} %s %{flags} %{compile_flags} %{link_flags} -o %t.exe"
            ]
            return self._executeShTest(test, litConfig, steps)
        elif filename.endswith(".link.fail.cpp"):
            steps = [
                "%dbg(COMPILED WITH) %{cxx} %s %{flags} %{compile_flags} -c -o %t.o",
                "%dbg(LINKED WITH) ! %{cxx} %t.o %{flags} %{link_flags} -o %t.exe",
            ]
            return self._executeShTest(test, litConfig, steps)
        elif filename.endswith(".verify.cpp"):
            if not supportsVerify:
                return lit.Test.Result(
                    lit.Test.UNSUPPORTED,
                    "Test {} requires support for Clang-verify, which isn't supported by the compiler".format(
                        test.getFullName()
                    ),
                )
            steps = ["%dbg(COMPILED WITH) %{verify}"]
            return self._executeShTest(test, litConfig, steps)
        # Make sure to check these ones last, since they will match other
        # suffixes above too.
        elif filename.endswith(".pass.cpp") or filename.endswith(".pass.mm"):
            steps = [
                "%dbg(COMPILED WITH) %{cxx} %s %{flags} %{compile_flags} %{link_flags} -o %t.exe",
                "%dbg(EXECUTED AS) %{exec} %t.exe",
            ]
            return self._executeShTest(test, litConfig, steps)
        else:
            return lit.Test.Result(
                lit.Test.UNRESOLVED, "Unknown test suffix for '{}'".format(filename)
            )

    def _executeShTest(self, test, litConfig, steps):
        if test.config.unsupported:
            return lit.Test.Result(lit.Test.UNSUPPORTED, "Test is unsupported")

        script = parseScript(test, steps)
        if isinstance(script, lit.Test.Result):
            return script

        if litConfig.noExecute:
            return lit.Test.Result(
                lit.Test.XFAIL if test.isExpectedToFail() else lit.Test.PASS
            )
        else:
            _, tmpBase = _getTempPaths(test)
            useExternalSh = False
            return lit.TestRunner._runShTest(
                test, litConfig, useExternalSh, script, tmpBase
            )

    def _generateGenTest(self, testSuite, pathInSuite, litConfig, localConfig):
        generator = lit.Test.Test(testSuite, pathInSuite, localConfig)

        # Make sure we have a directory to execute the generator test in
        generatorExecDir = os.path.dirname(testSuite.getExecPath(pathInSuite))
        os.makedirs(generatorExecDir, exist_ok=True)

        # Run the generator test
        steps = [] # Steps must already be in the script
        (out, err, exitCode, _, _) = _executeScriptInternal(generator, litConfig, steps)
        if exitCode != 0:
            raise RuntimeError(f"Error while trying to generate gen test\nstdout:\n{out}\n\nstderr:\n{err}")

        # Split the generated output into multiple files and generate one test for each file
        for subfile, content in self._splitFile(out):
            generatedFile = testSuite.getExecPath(pathInSuite + (subfile,))
            os.makedirs(os.path.dirname(generatedFile), exist_ok=True)
            with open(generatedFile, 'w') as f:
                f.write(content)
            yield lit.Test.Test(testSuite, (generatedFile,), localConfig)

    def _splitFile(self, input):
        DELIM = r'^(//|#)---(.+)'
        lines = input.splitlines()
        currentFile = None
        thisFileContent = []
        for line in lines:
            match = re.match(DELIM, line)
            if match:
                if currentFile is not None:
                    yield (currentFile, '\n'.join(thisFileContent))
                currentFile = match.group(2).strip()
                thisFileContent = []
            assert currentFile is not None, f"Some input to split-file doesn't belong to any file, input was:\n{input}"
            thisFileContent.append(line)
        if currentFile is not None:
            yield (currentFile, '\n'.join(thisFileContent))
