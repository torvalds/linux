import itertools
import os
import platform
import re
import subprocess
import sys
import errno

import lit.util
from lit.llvm.subst import FindTool
from lit.llvm.subst import ToolSubst

lit_path_displayed = False


def user_is_root():
    # os.getuid() is not available on all platforms
    try:
        if os.getuid() == 0:
            return True
    except:
        pass

    return False


class LLVMConfig(object):
    def __init__(self, lit_config, config):
        self.lit_config = lit_config
        self.config = config

        features = config.available_features

        self.use_lit_shell = False
        # Tweak PATH for Win32 to decide to use bash.exe or not.
        if sys.platform == "win32":
            # Seek necessary tools in directories and set to $PATH.
            path = None
            lit_tools_dir = getattr(config, "lit_tools_dir", None)
            required_tools = ["cmp.exe", "grep.exe", "sed.exe", "diff.exe", "echo.exe"]
            path = self.lit_config.getToolsPath(
                lit_tools_dir, config.environment["PATH"], required_tools
            )
            if path is None:
                path = self._find_git_windows_unix_tools(required_tools)
            if path is not None:
                self.with_environment("PATH", path, append_path=True)
            # Many tools behave strangely if these environment variables aren't
            # set.
            self.with_system_environment(
                ["SystemDrive", "SystemRoot", "TEMP", "TMP", "PLATFORM"]
            )
            self.use_lit_shell = True

            global lit_path_displayed
            if not self.lit_config.quiet and lit_path_displayed is False:
                self.lit_config.note("using lit tools: {}".format(path))
                lit_path_displayed = True

        if platform.system() == "OS/390":
            self.with_environment("_BPXK_AUTOCVT", "ON")
            self.with_environment("_TAG_REDIR_IN", "TXT")
            self.with_environment("_TAG_REDIR_OUT", "TXT")
            self.with_environment("_TAG_REDIR_ERR", "TXT")
            self.with_environment("_CEE_RUNOPTS", "FILETAG(AUTOCVT,AUTOTAG) POSIX(ON)")

        # Choose between lit's internal shell pipeline runner and a real shell.
        # If LIT_USE_INTERNAL_SHELL is in the environment, we use that as an
        # override.
        lit_shell_env = os.environ.get("LIT_USE_INTERNAL_SHELL")
        if lit_shell_env:
            self.use_lit_shell = lit.util.pythonize_bool(lit_shell_env)

        if not self.use_lit_shell:
            features.add("shell")

        self.with_system_environment(
            [
                "ASAN_SYMBOLIZER_PATH",
                "HWASAN_SYMBOLIZER_PATH",
                "MSAN_SYMBOLIZER_PATH",
                "TSAN_SYMBOLIZER_PATH",
                "UBSAN_SYMBOLIZER_PATH" "ASAN_OPTIONS",
                "HWASAN_OPTIONS",
                "MSAN_OPTIONS",
                "TSAN_OPTIONS",
                "UBSAN_OPTIONS",
            ]
        )

        # Running on Darwin OS
        if platform.system() == "Darwin":
            features.add("system-darwin")
        elif platform.system() == "Windows":
            # For tests that require Windows to run.
            features.add("system-windows")
        elif platform.system() == "Linux":
            features.add("system-linux")
        elif platform.system() in ["FreeBSD"]:
            features.add("system-freebsd")
        elif platform.system() == "NetBSD":
            features.add("system-netbsd")
        elif platform.system() == "AIX":
            features.add("system-aix")
        elif platform.system() == "SunOS":
            features.add("system-solaris")
        elif platform.system() == "OS/390":
            features.add("system-zos")

        # Native compilation: host arch == default triple arch
        # Both of these values should probably be in every site config (e.g. as
        # part of the standard header.  But currently they aren't)
        host_triple = getattr(config, "host_triple", None)
        target_triple = getattr(config, "target_triple", None)
        features.add("host=%s" % host_triple)
        features.add("target=%s" % target_triple)
        if host_triple and host_triple == target_triple:
            features.add("native")

        # Sanitizers.
        sanitizers = getattr(config, "llvm_use_sanitizer", "")
        sanitizers = frozenset(x.lower() for x in sanitizers.split(";"))
        if "address" in sanitizers:
            features.add("asan")
        if "hwaddress" in sanitizers:
            features.add("hwasan")
        if "memory" in sanitizers or "memorywithorigins" in sanitizers:
            features.add("msan")
        if "undefined" in sanitizers:
            features.add("ubsan")
        if "thread" in sanitizers:
            features.add("tsan")

        have_zlib = getattr(config, "have_zlib", None)
        if have_zlib:
            features.add("zlib")
        have_zstd = getattr(config, "have_zstd", None)
        if have_zstd:
            features.add("zstd")

        if getattr(config, "reverse_iteration", None):
            features.add("reverse_iteration")

        # Check if we should run long running tests.
        long_tests = lit_config.params.get("run_long_tests", None)
        if lit.util.pythonize_bool(long_tests):
            features.add("long_tests")

        if target_triple:
            if re.match(r"^x86_64.*-apple", target_triple):
                features.add("x86_64-apple")
                host_cxx = getattr(config, "host_cxx", None)
                if "address" in sanitizers and self.get_clang_has_lsan(
                    host_cxx, target_triple
                ):
                    self.with_environment(
                        "ASAN_OPTIONS", "detect_leaks=1", append_path=True
                    )
            if re.match(r"^x86_64.*-linux", target_triple):
                features.add("x86_64-linux")
            if re.match(r"^i.86.*", target_triple):
                features.add("target-x86")
            elif re.match(r"^x86_64.*", target_triple):
                features.add("target-x86_64")
            elif re.match(r"^aarch64.*", target_triple):
                features.add("target-aarch64")
            elif re.match(r"^arm64.*", target_triple):
                features.add("target-aarch64")
            elif re.match(r"^arm.*", target_triple):
                features.add("target-arm")
            if re.match(r'^ppc64le.*-linux', target_triple):
                features.add('target=powerpc64le-linux')

        if not user_is_root():
            features.add("non-root-user")

        use_gmalloc = lit_config.params.get("use_gmalloc", None)
        if lit.util.pythonize_bool(use_gmalloc):
            # Allow use of an explicit path for gmalloc library.
            # Will default to '/usr/lib/libgmalloc.dylib' if not set.
            gmalloc_path_str = lit_config.params.get(
                "gmalloc_path", "/usr/lib/libgmalloc.dylib"
            )
            if gmalloc_path_str is not None:
                self.with_environment("DYLD_INSERT_LIBRARIES", gmalloc_path_str)

    def _find_git_windows_unix_tools(self, tools_needed):
        assert sys.platform == "win32"
        if sys.version_info.major >= 3:
            import winreg
        else:
            import _winreg as winreg

        # Search both the 64 and 32-bit hives, as well as HKLM + HKCU
        masks = [0, winreg.KEY_WOW64_64KEY]
        hives = [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]
        for mask, hive in itertools.product(masks, hives):
            try:
                with winreg.OpenKey(
                    hive, r"SOFTWARE\GitForWindows", 0, winreg.KEY_READ | mask
                ) as key:
                    install_root, _ = winreg.QueryValueEx(key, "InstallPath")

                    if not install_root:
                        continue
                    candidate_path = os.path.join(install_root, "usr", "bin")
                    if not lit.util.checkToolsPath(candidate_path, tools_needed):
                        continue

                    # We found it, stop enumerating.
                    return lit.util.to_string(candidate_path)
            except:
                continue

        return None

    def with_environment(self, variable, value, append_path=False):
        if append_path:
            # For paths, we should be able to take a list of them and process
            # all of them.
            paths_to_add = value
            if lit.util.is_string(paths_to_add):
                paths_to_add = [paths_to_add]

            def norm(x):
                return os.path.normcase(os.path.normpath(x))

            current_paths = self.config.environment.get(variable, None)
            if current_paths:
                current_paths = current_paths.split(os.path.pathsep)
                paths = [norm(p) for p in current_paths]
            else:
                paths = []

            # If we are passed a list [a b c], then iterating this list forwards
            # and adding each to the beginning would result in c b a.  So we
            # need to iterate in reverse to end up with the original ordering.
            for p in reversed(paths_to_add):
                # Move it to the front if it already exists, otherwise insert
                # it at the beginning.
                p = norm(p)
                try:
                    paths.remove(p)
                except ValueError:
                    pass
                paths = [p] + paths
            value = os.pathsep.join(paths)
        self.config.environment[variable] = value

    def with_system_environment(self, variables, append_path=False):
        if lit.util.is_string(variables):
            variables = [variables]
        for v in variables:
            value = os.environ.get(v)
            if value:
                self.with_environment(v, value, append_path)

    def clear_environment(self, variables):
        for name in variables:
            if name in self.config.environment:
                del self.config.environment[name]

    def get_process_output(self, command):
        try:
            cmd = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=self.config.environment,
            )
            stdout, stderr = cmd.communicate()
            stdout = lit.util.to_string(stdout)
            stderr = lit.util.to_string(stderr)
            return (stdout, stderr)
        except OSError:
            self.lit_config.fatal("Could not run process %s" % command)

    def feature_config(self, features):
        # Ask llvm-config about the specified feature.
        arguments = [x for (x, _) in features]
        config_path = os.path.join(self.config.llvm_tools_dir, "llvm-config")

        output, _ = self.get_process_output([config_path] + arguments)
        lines = output.split("\n")

        for (feature_line, (_, patterns)) in zip(lines, features):
            # We should have either a callable or a dictionary.  If it's a
            # dictionary, grep each key against the output and use the value if
            # it matches.  If it's a callable, it does the entire translation.
            if callable(patterns):
                features_to_add = patterns(feature_line)
                self.config.available_features.update(features_to_add)
            else:
                for (re_pattern, feature) in patterns.items():
                    if re.search(re_pattern, feature_line):
                        self.config.available_features.add(feature)

    # Note that when substituting %clang_cc1 also fill in the include directory
    # of the builtin headers. Those are part of even a freestanding
    # environment, but Clang relies on the driver to locate them.
    def get_clang_builtin_include_dir(self, clang):
        # FIXME: Rather than just getting the version, we should have clang
        # print out its resource dir here in an easy to scrape form.
        clang_dir, _ = self.get_process_output([clang, "-print-file-name=include"])

        if not clang_dir:
            print(clang)
            self.lit_config.fatal(
                "Couldn't find the include dir for Clang ('%s')" % clang
            )

        clang_dir = clang_dir.strip()
        if sys.platform in ["win32"] and not self.use_lit_shell:
            # Don't pass dosish path separator to msys bash.exe.
            clang_dir = clang_dir.replace("\\", "/")
        # Ensure the result is an ascii string, across Python2.5+ - Python3.
        return clang_dir

    # On macOS, LSan is only supported on clang versions 5 and higher
    def get_clang_has_lsan(self, clang, triple):
        if not clang:
            self.lit_config.warning(
                "config.host_cxx is unset but test suite is configured "
                "to use sanitizers."
            )
            return False

        clang_binary = clang.split()[0]
        version_string, _ = self.get_process_output([clang_binary, "--version"])
        if not "clang" in version_string:
            self.lit_config.warning(
                "compiler '%s' does not appear to be clang, " % clang_binary
                + "but test suite is configured to use sanitizers."
            )
            return False

        if re.match(r".*-linux", triple):
            return True

        if re.match(r"^x86_64.*-apple", triple):
            version_regex = re.search(
                r"version ([0-9]+)\.([0-9]+).([0-9]+)", version_string
            )
            major_version_number = int(version_regex.group(1))
            minor_version_number = int(version_regex.group(2))
            patch_version_number = int(version_regex.group(3))
            if "Apple LLVM" in version_string or "Apple clang" in version_string:
                # Apple clang doesn't yet support LSan
                return False
            return major_version_number >= 5

        return False

    def make_itanium_abi_triple(self, triple):
        m = re.match(r"(\w+)-(\w+)-(\w+)", triple)
        if not m:
            self.lit_config.fatal(
                "Could not turn '%s' into Itanium ABI triple" % triple
            )
        if m.group(3).lower() != "windows":
            # All non-windows triples use the Itanium ABI.
            return triple
        return m.group(1) + "-" + m.group(2) + "-" + m.group(3) + "-gnu"

    def make_msabi_triple(self, triple):
        m = re.match(r"(\w+)-(\w+)-(\w+)", triple)
        if not m:
            self.lit_config.fatal("Could not turn '%s' into MS ABI triple" % triple)
        isa = m.group(1).lower()
        vendor = m.group(2).lower()
        os = m.group(3).lower()
        if os == "windows" and re.match(r".*-msvc$", triple):
            # If the OS is windows and environment is msvc, we're done.
            return triple
        if isa.startswith("x86") or isa == "amd64" or re.match(r"i\d86", isa):
            # For x86 ISAs, adjust the OS.
            return isa + "-" + vendor + "-windows-msvc"
        # -msvc is not supported for non-x86 targets; use a default.
        return "i686-pc-windows-msvc"

    def add_tool_substitutions(self, tools, search_dirs=None):
        if not search_dirs:
            search_dirs = [self.config.llvm_tools_dir]

        if lit.util.is_string(search_dirs):
            search_dirs = [search_dirs]

        tools = [x if isinstance(x, ToolSubst) else ToolSubst(x) for x in tools]

        search_dirs = os.pathsep.join(search_dirs)
        substitutions = []

        for tool in tools:
            match = tool.resolve(self, search_dirs)

            # Either no match occurred, or there was an unresolved match that
            # is ignored.
            if not match:
                continue

            subst_key, tool_pipe, command = match

            # An unresolved match occurred that can't be ignored.  Fail without
            # adding any of the previously-discovered substitutions.
            if not command:
                return False

            substitutions.append((subst_key, tool_pipe + command))

        self.config.substitutions.extend(substitutions)
        return True

    def add_err_msg_substitutions(self):
        # Python's strerror may not supply the same message
        # as C++ std::error_code. One example of such a platform is
        # Visual Studio. errc_messages may be supplied which contains the error
        # messages for ENOENT, EISDIR, EINVAL and EACCES as a semi colon
        # separated string. LLVM testsuites can use get_errc_messages in cmake
        # to automatically get the messages and pass them into lit.
        errc_messages = getattr(self.config, "errc_messages", "")
        if len(errc_messages) != 0:
            (errc_enoent, errc_eisdir, errc_einval, errc_eacces) = errc_messages.split(
                ";"
            )
            self.config.substitutions.append(("%errc_ENOENT", "'" + errc_enoent + "'"))
            self.config.substitutions.append(("%errc_EISDIR", "'" + errc_eisdir + "'"))
            self.config.substitutions.append(("%errc_EINVAL", "'" + errc_einval + "'"))
            self.config.substitutions.append(("%errc_EACCES", "'" + errc_eacces + "'"))
        else:
            self.config.substitutions.append(
                ("%errc_ENOENT", "'" + os.strerror(errno.ENOENT) + "'")
            )
            self.config.substitutions.append(
                ("%errc_EISDIR", "'" + os.strerror(errno.EISDIR) + "'")
            )
            self.config.substitutions.append(
                ("%errc_EINVAL", "'" + os.strerror(errno.EINVAL) + "'")
            )
            self.config.substitutions.append(
                ("%errc_EACCES", "'" + os.strerror(errno.EACCES) + "'")
            )

    def use_default_substitutions(self):
        tool_patterns = [
            ToolSubst("FileCheck", unresolved="fatal"),
            # Handle these specially as they are strings searched for during
            # testing.
            ToolSubst(
                r"\| \bcount\b",
                command=FindTool("count"),
                verbatim=True,
                unresolved="fatal",
            ),
            ToolSubst(
                r"\| \bnot\b",
                command=FindTool("not"),
                verbatim=True,
                unresolved="fatal",
            ),
        ]

        self.config.substitutions.append(("%python", '"%s"' % (sys.executable)))

        self.add_tool_substitutions(tool_patterns, [self.config.llvm_tools_dir])

        self.add_err_msg_substitutions()

    def use_llvm_tool(
        self,
        name,
        search_env=None,
        required=False,
        quiet=False,
        search_paths=None,
        use_installed=False,
    ):
        """Find the executable program 'name', optionally using the specified
        environment variable as an override before searching the build directory
        and then optionally the configuration's PATH."""
        # If the override is specified in the environment, use it without
        # validation.
        tool = None
        if search_env:
            tool = self.config.environment.get(search_env)

        if not tool:
            if search_paths is None:
                search_paths = [self.config.llvm_tools_dir]
            # Use the specified search paths.
            path = os.pathsep.join(search_paths)
            tool = lit.util.which(name, path)

        if not tool and use_installed:
            # Otherwise look in the path, if enabled.
            tool = lit.util.which(name, self.config.environment["PATH"])

        if required and not tool:
            message = "couldn't find '{}' program".format(name)
            if search_env:
                message = message + ", try setting {} in your environment".format(
                    search_env
                )
            self.lit_config.fatal(message)

        if tool:
            tool = os.path.normpath(tool)
            if not self.lit_config.quiet and not quiet:
                self.lit_config.note("using {}: {}".format(name, tool))
        return tool

    def use_clang(
        self,
        additional_tool_dirs=[],
        additional_flags=[],
        required=True,
        use_installed=False,
    ):
        """Configure the test suite to be able to invoke clang.

        Sets up some environment variables important to clang, locates a
        just-built or optionally an installed clang, and add a set of standard
        substitutions useful to any test suite that makes use of clang.

        """
        # Clear some environment variables that might affect Clang.
        #
        # This first set of vars are read by Clang, but shouldn't affect tests
        # that aren't specifically looking for these features, or are required
        # simply to run the tests at all.
        #
        # FIXME: Should we have a tool that enforces this?

        # safe_env_vars = (
        #     'TMPDIR', 'TEMP', 'TMP', 'USERPROFILE', 'PWD',
        #     'MACOSX_DEPLOYMENT_TARGET', 'IPHONEOS_DEPLOYMENT_TARGET',
        #     'VCINSTALLDIR', 'VC100COMNTOOLS', 'VC90COMNTOOLS',
        #     'VC80COMNTOOLS')
        possibly_dangerous_env_vars = [
            "COMPILER_PATH",
            "RC_DEBUG_OPTIONS",
            "CINDEXTEST_PREAMBLE_FILE",
            "LIBRARY_PATH",
            "CPATH",
            "C_INCLUDE_PATH",
            "CPLUS_INCLUDE_PATH",
            "OBJC_INCLUDE_PATH",
            "OBJCPLUS_INCLUDE_PATH",
            "LIBCLANG_TIMING",
            "LIBCLANG_OBJTRACKING",
            "LIBCLANG_LOGGING",
            "LIBCLANG_BGPRIO_INDEX",
            "LIBCLANG_BGPRIO_EDIT",
            "LIBCLANG_NOTHREADS",
            "LIBCLANG_RESOURCE_USAGE",
            "LIBCLANG_CODE_COMPLETION_LOGGING",
        ]
        # Clang/Win32 may refer to %INCLUDE%. vsvarsall.bat sets it.
        if platform.system() != "Windows":
            possibly_dangerous_env_vars.append("INCLUDE")

        self.clear_environment(possibly_dangerous_env_vars)

        # Tweak the PATH to include the tools dir and the scripts dir.
        # Put Clang first to avoid LLVM from overriding out-of-tree clang
        # builds.
        exe_dir_props = [
            self.config.name.lower() + "_tools_dir",
            "clang_tools_dir",
            "llvm_tools_dir",
        ]
        paths = [
            getattr(self.config, pp)
            for pp in exe_dir_props
            if getattr(self.config, pp, None)
        ]
        paths = additional_tool_dirs + paths
        self.with_environment("PATH", paths, append_path=True)

        lib_dir_props = [
            self.config.name.lower() + "_libs_dir",
            "llvm_shlib_dir",
            "llvm_libs_dir",
        ]
        lib_paths = [
            getattr(self.config, pp)
            for pp in lib_dir_props
            if getattr(self.config, pp, None)
        ]

        if platform.system() == "AIX":
            self.with_environment("LIBPATH", lib_paths, append_path=True)
        else:
            self.with_environment("LD_LIBRARY_PATH", lib_paths, append_path=True)

        shl = getattr(self.config, "llvm_shlib_dir", None)
        pext = getattr(self.config, "llvm_plugin_ext", None)
        if shl:
            self.config.substitutions.append(("%llvmshlibdir", shl))
        if pext:
            self.config.substitutions.append(("%pluginext", pext))

        # Discover the 'clang' and 'clangcc' to use.
        self.config.clang = self.use_llvm_tool(
            "clang",
            search_env="CLANG",
            required=required,
            search_paths=paths,
            use_installed=use_installed,
        )
        if self.config.clang:
            self.config.available_features.add("clang")
            builtin_include_dir = self.get_clang_builtin_include_dir(self.config.clang)
            tool_substitutions = [
                ToolSubst(
                    "%clang", command=self.config.clang, extra_args=additional_flags
                ),
                ToolSubst(
                    "%clang_analyze_cc1",
                    command="%clang_cc1",
                    extra_args=["-analyze", "%analyze", "-setup-static-analyzer"]
                    + additional_flags,
                ),
                ToolSubst(
                    "%clang_cc1",
                    command=self.config.clang,
                    extra_args=[
                        "-cc1",
                        "-internal-isystem",
                        builtin_include_dir,
                        "-nostdsysteminc",
                    ]
                    + additional_flags,
                ),
                ToolSubst(
                    "%clang_cpp",
                    command=self.config.clang,
                    extra_args=["--driver-mode=cpp"] + additional_flags,
                ),
                ToolSubst(
                    "%clang_cl",
                    command=self.config.clang,
                    extra_args=["--driver-mode=cl"] + additional_flags,
                ),
                ToolSubst(
                    "%clang_dxc",
                    command=self.config.clang,
                    extra_args=["--driver-mode=dxc"] + additional_flags,
                ),
                ToolSubst(
                    "%clangxx",
                    command=self.config.clang,
                    extra_args=["--driver-mode=g++"] + additional_flags,
                ),
            ]
            self.add_tool_substitutions(tool_substitutions)
            self.config.substitutions.append(("%resource_dir", builtin_include_dir))

        # There will be no default target triple if one was not specifically
        # set, and the host's architecture is not an enabled target.
        if self.config.target_triple:
            self.config.substitutions.append(
                (
                    "%itanium_abi_triple",
                    self.make_itanium_abi_triple(self.config.target_triple),
                )
            )
            self.config.substitutions.append(
                ("%ms_abi_triple", self.make_msabi_triple(self.config.target_triple))
            )
        else:
            if not self.lit_config.quiet:
                self.lit_config.note(
                    "No default target triple was found, some tests may fail as a result."
                )
            self.config.substitutions.append(("%itanium_abi_triple", ""))
            self.config.substitutions.append(("%ms_abi_triple", ""))

        # The host triple might not be set, at least if we're compiling clang
        # from an already installed llvm.
        if self.config.host_triple and self.config.host_triple != "@LLVM_HOST_TRIPLE@":
            self.config.substitutions.append(
                (
                    "%target_itanium_abi_host_triple",
                    "--target=" + self.make_itanium_abi_triple(self.config.host_triple),
                )
            )
        else:
            self.config.substitutions.append(("%target_itanium_abi_host_triple", ""))

        # TODO: Many tests work across many language standards. Before
        # https://discourse.llvm.org/t/lit-run-a-run-line-multiple-times-with-different-replacements/64932
        # has a solution, provide substitutions to conveniently try every standard with LIT_CLANG_STD_GROUP.
        clang_std_group = int(os.environ.get("LIT_CLANG_STD_GROUP", "0"))
        clang_std_values = ("98", "11", "14", "17", "20", "2b")

        def add_std_cxx(s):
            t = s[8:]
            if t.endswith("-"):
                t += clang_std_values[-1]
            l = clang_std_values.index(t[0:2] if t[0:2] != "23" else "2b")
            h = clang_std_values.index(t[3:5])
            # Let LIT_CLANG_STD_GROUP=0 pick the highest value (likely the most relevant
            # standard).
            l = h - clang_std_group % (h - l + 1)
            self.config.substitutions.append((s, "-std=c++" + clang_std_values[l]))

        add_std_cxx("%std_cxx98-14")
        add_std_cxx("%std_cxx98-")
        add_std_cxx("%std_cxx11-14")
        add_std_cxx("%std_cxx11-")
        add_std_cxx("%std_cxx14-")
        add_std_cxx("%std_cxx17-20")
        add_std_cxx("%std_cxx17-")
        add_std_cxx("%std_cxx20-")
        add_std_cxx("%std_cxx23-")

        # FIXME: Find nicer way to prohibit this.
        def prefer(this, to):
            return '''\"*** Do not use '%s' in tests, use '%s'. ***\"''' % (to, this)

        self.config.substitutions.append((" clang ", prefer("%clang", "clang")))
        self.config.substitutions.append(
            (r" clang\+\+ ", prefer("%clangxx", "clang++"))
        )
        self.config.substitutions.append(
            (" clang-cc ", prefer("%clang_cc1", "clang-cc"))
        )
        self.config.substitutions.append(
            (" clang-cl ", prefer("%clang_cl", "clang-cl"))
        )
        self.config.substitutions.append(
            (
                " clang -cc1 -analyze ",
                prefer("%clang_analyze_cc1", "clang -cc1 -analyze"),
            )
        )
        self.config.substitutions.append(
            (" clang -cc1 ", prefer("%clang_cc1", "clang -cc1"))
        )
        self.config.substitutions.append(
            (" %clang-cc1 ", '''\"*** invalid substitution, use '%clang_cc1'. ***\"''')
        )
        self.config.substitutions.append(
            (" %clang-cpp ", '''\"*** invalid substitution, use '%clang_cpp'. ***\"''')
        )
        self.config.substitutions.append(
            (" %clang-cl ", '''\"*** invalid substitution, use '%clang_cl'. ***\"''')
        )

    def use_lld(self, additional_tool_dirs=[], required=True, use_installed=False):
        """Configure the test suite to be able to invoke lld.

        Sets up some environment variables important to lld, locates a
        just-built or optionally an installed lld, and add a set of standard
        substitutions useful to any test suite that makes use of lld.

        """

        # Tweak the PATH to include the tools dir and the scripts dir.
        exe_dir_props = [
            self.config.name.lower() + "_tools_dir",
            "lld_tools_dir",
            "llvm_tools_dir",
        ]
        paths = [
            getattr(self.config, pp)
            for pp in exe_dir_props
            if getattr(self.config, pp, None)
        ]
        paths = additional_tool_dirs + paths
        self.with_environment("PATH", paths, append_path=True)

        lib_dir_props = [
            self.config.name.lower() + "_libs_dir",
            "lld_libs_dir",
            "llvm_shlib_dir",
            "llvm_libs_dir",
        ]
        lib_paths = [
            getattr(self.config, pp)
            for pp in lib_dir_props
            if getattr(self.config, pp, None)
        ]

        self.with_environment("LD_LIBRARY_PATH", lib_paths, append_path=True)

        # Discover the LLD executables to use.

        ld_lld = self.use_llvm_tool(
            "ld.lld", required=required, search_paths=paths, use_installed=use_installed
        )
        lld_link = self.use_llvm_tool(
            "lld-link",
            required=required,
            search_paths=paths,
            use_installed=use_installed,
        )
        ld64_lld = self.use_llvm_tool(
            "ld64.lld",
            required=required,
            search_paths=paths,
            use_installed=use_installed,
        )
        wasm_ld = self.use_llvm_tool(
            "wasm-ld",
            required=required,
            search_paths=paths,
            use_installed=use_installed,
        )

        was_found = ld_lld and lld_link and ld64_lld and wasm_ld
        tool_substitutions = []
        if ld_lld:
            tool_substitutions.append(ToolSubst(r"ld\.lld", command=ld_lld))
            self.config.available_features.add("ld.lld")
        if lld_link:
            tool_substitutions.append(ToolSubst("lld-link", command=lld_link))
            self.config.available_features.add("lld-link")
        if ld64_lld:
            tool_substitutions.append(ToolSubst(r"ld64\.lld", command=ld64_lld))
            self.config.available_features.add("ld64.lld")
        if wasm_ld:
            tool_substitutions.append(ToolSubst("wasm-ld", command=wasm_ld))
            self.config.available_features.add("wasm-ld")
        self.add_tool_substitutions(tool_substitutions)

        return was_found
