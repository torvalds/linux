from __future__ import absolute_import
import errno
import io
import itertools
import getopt
import os, signal, subprocess, sys
import re
import stat
import pathlib
import platform
import shlex
import shutil
import tempfile
import threading
import typing
from typing import Optional, Tuple

import io

try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO

from lit.ShCommands import GlobItem, Command
import lit.ShUtil as ShUtil
import lit.Test as Test
import lit.util
from lit.util import to_bytes, to_string, to_unicode
from lit.BooleanExpression import BooleanExpression


class InternalShellError(Exception):
    def __init__(self, command, message):
        self.command = command
        self.message = message


class ScriptFatal(Exception):
    """
    A script had a fatal error such that there's no point in retrying.  The
    message has not been emitted on stdout or stderr but is instead included in
    this exception.
    """

    def __init__(self, message):
        super().__init__(message)


kIsWindows = platform.system() == "Windows"

# Don't use close_fds on Windows.
kUseCloseFDs = not kIsWindows

# Use temporary files to replace /dev/null on Windows.
kAvoidDevNull = kIsWindows
kDevNull = "/dev/null"

# A regex that matches %dbg(ARG), which lit inserts at the beginning of each
# run command pipeline such that ARG specifies the pipeline's source line
# number.  lit later expands each %dbg(ARG) to a command that behaves as a null
# command in the target shell so that the line number is seen in lit's verbose
# mode.
#
# This regex captures ARG.  ARG must not contain a right parenthesis, which
# terminates %dbg.  ARG must not contain quotes, in which ARG might be enclosed
# during expansion.
#
# COMMAND that follows %dbg(ARG) is also captured. COMMAND can be
# empty as a result of conditinal substitution.
kPdbgRegex = "%dbg\\(([^)'\"]*)\\)((?:.|\\n)*)"


def buildPdbgCommand(msg, cmd):
    res = f"%dbg({msg}) {cmd}"
    assert re.fullmatch(
        kPdbgRegex, res
    ), f"kPdbgRegex expected to match actual %dbg usage: {res}"
    return res


class ShellEnvironment(object):

    """Mutable shell environment containing things like CWD and env vars.

    Environment variables are not implemented, but cwd tracking is. In addition,
    we maintain a dir stack for pushd/popd.
    """

    def __init__(self, cwd, env):
        self.cwd = cwd
        self.env = dict(env)
        self.dirStack = []

    def change_dir(self, newdir):
        if os.path.isabs(newdir):
            self.cwd = newdir
        else:
            self.cwd = lit.util.abs_path_preserve_drive(os.path.join(self.cwd, newdir))


class TimeoutHelper(object):
    """
    Object used to helper manage enforcing a timeout in
    _executeShCmd(). It is passed through recursive calls
    to collect processes that have been executed so that when
    the timeout happens they can be killed.
    """

    def __init__(self, timeout):
        self.timeout = timeout
        self._procs = []
        self._timeoutReached = False
        self._doneKillPass = False
        # This lock will be used to protect concurrent access
        # to _procs and _doneKillPass
        self._lock = None
        self._timer = None

    def cancel(self):
        if not self.active():
            return
        self._timer.cancel()

    def active(self):
        return self.timeout > 0

    def addProcess(self, proc):
        if not self.active():
            return
        needToRunKill = False
        with self._lock:
            self._procs.append(proc)
            # Avoid re-entering the lock by finding out if kill needs to be run
            # again here but call it if necessary once we have left the lock.
            # We could use a reentrant lock here instead but this code seems
            # clearer to me.
            needToRunKill = self._doneKillPass

        # The initial call to _kill() from the timer thread already happened so
        # we need to call it again from this thread, otherwise this process
        # will be left to run even though the timeout was already hit
        if needToRunKill:
            assert self.timeoutReached()
            self._kill()

    def startTimer(self):
        if not self.active():
            return

        # Do some late initialisation that's only needed
        # if there is a timeout set
        self._lock = threading.Lock()
        self._timer = threading.Timer(self.timeout, self._handleTimeoutReached)
        self._timer.start()

    def _handleTimeoutReached(self):
        self._timeoutReached = True
        self._kill()

    def timeoutReached(self):
        return self._timeoutReached

    def _kill(self):
        """
        This method may be called multiple times as we might get unlucky
        and be in the middle of creating a new process in _executeShCmd()
        which won't yet be in ``self._procs``. By locking here and in
        addProcess() we should be able to kill processes launched after
        the initial call to _kill()
        """
        with self._lock:
            for p in self._procs:
                lit.util.killProcessAndChildren(p.pid)
            # Empty the list and note that we've done a pass over the list
            self._procs = []  # Python2 doesn't have list.clear()
            self._doneKillPass = True


class ShellCommandResult(object):
    """Captures the result of an individual command."""

    def __init__(
        self, command, stdout, stderr, exitCode, timeoutReached, outputFiles=[]
    ):
        self.command = command
        self.stdout = stdout
        self.stderr = stderr
        self.exitCode = exitCode
        self.timeoutReached = timeoutReached
        self.outputFiles = list(outputFiles)


def executeShCmd(cmd, shenv, results, timeout=0):
    """
    Wrapper around _executeShCmd that handles
    timeout
    """
    # Use the helper even when no timeout is required to make
    # other code simpler (i.e. avoid bunch of ``!= None`` checks)
    timeoutHelper = TimeoutHelper(timeout)
    if timeout > 0:
        timeoutHelper.startTimer()
    finalExitCode = _executeShCmd(cmd, shenv, results, timeoutHelper)
    timeoutHelper.cancel()
    timeoutInfo = None
    if timeoutHelper.timeoutReached():
        timeoutInfo = "Reached timeout of {} seconds".format(timeout)

    return (finalExitCode, timeoutInfo)


def expand_glob(arg, cwd):
    if isinstance(arg, GlobItem):
        return sorted(arg.resolve(cwd))
    return [arg]


def expand_glob_expressions(args, cwd):
    result = [args[0]]
    for arg in args[1:]:
        result.extend(expand_glob(arg, cwd))
    return result


def quote_windows_command(seq):
    r"""
    Reimplement Python's private subprocess.list2cmdline for MSys compatibility

    Based on CPython implementation here:
      https://hg.python.org/cpython/file/849826a900d2/Lib/subprocess.py#l422

    Some core util distributions (MSys) don't tokenize command line arguments
    the same way that MSVC CRT does. Lit rolls its own quoting logic similar to
    the stock CPython logic to paper over these quoting and tokenization rule
    differences.

    We use the same algorithm from MSDN as CPython
    (http://msdn.microsoft.com/en-us/library/17w5ykft.aspx), but we treat more
    characters as needing quoting, such as double quotes themselves, and square
    brackets.

    For MSys based tools, this is very brittle though, because quoting an
    argument makes the MSys based tool unescape backslashes where it shouldn't
    (e.g. "a\b\\c\\\\d" becomes "a\b\c\\d" where it should stay as it was,
    according to regular win32 command line parsing rules).
    """
    result = []
    needquote = False
    for arg in seq:
        bs_buf = []

        # Add a space to separate this argument from the others
        if result:
            result.append(" ")

        # This logic differs from upstream list2cmdline.
        needquote = (
            (" " in arg)
            or ("\t" in arg)
            or ('"' in arg)
            or ("[" in arg)
            or (";" in arg)
            or not arg
        )
        if needquote:
            result.append('"')

        for c in arg:
            if c == "\\":
                # Don't know if we need to double yet.
                bs_buf.append(c)
            elif c == '"':
                # Double backslashes.
                result.append("\\" * len(bs_buf) * 2)
                bs_buf = []
                result.append('\\"')
            else:
                # Normal char
                if bs_buf:
                    result.extend(bs_buf)
                    bs_buf = []
                result.append(c)

        # Add remaining backslashes, if any.
        if bs_buf:
            result.extend(bs_buf)

        if needquote:
            result.extend(bs_buf)
            result.append('"')

    return "".join(result)


# args are from 'export' or 'env' command.
# Skips the command, and parses its arguments.
# Modifies env accordingly.
# Returns copy of args without the command or its arguments.
def updateEnv(env, args):
    arg_idx_next = len(args)
    unset_next_env_var = False
    for arg_idx, arg in enumerate(args[1:]):
        # Support for the -u flag (unsetting) for env command
        # e.g., env -u FOO -u BAR will remove both FOO and BAR
        # from the environment.
        if arg == "-u":
            unset_next_env_var = True
            continue
        if unset_next_env_var:
            unset_next_env_var = False
            if arg in env.env:
                del env.env[arg]
            continue

        # Partition the string into KEY=VALUE.
        key, eq, val = arg.partition("=")
        # Stop if there was no equals.
        if eq == "":
            arg_idx_next = arg_idx + 1
            break
        env.env[key] = val
    return args[arg_idx_next:]


def executeBuiltinCd(cmd, shenv):
    """executeBuiltinCd - Change the current directory."""
    if len(cmd.args) != 2:
        raise InternalShellError(cmd, "'cd' supports only one argument")
    # Update the cwd in the parent environment.
    shenv.change_dir(cmd.args[1])
    # The cd builtin always succeeds. If the directory does not exist, the
    # following Popen calls will fail instead.
    return ShellCommandResult(cmd, "", "", 0, False)


def executeBuiltinPushd(cmd, shenv):
    """executeBuiltinPushd - Change the current dir and save the old."""
    if len(cmd.args) != 2:
        raise InternalShellError(cmd, "'pushd' supports only one argument")
    shenv.dirStack.append(shenv.cwd)
    shenv.change_dir(cmd.args[1])
    return ShellCommandResult(cmd, "", "", 0, False)


def executeBuiltinPopd(cmd, shenv):
    """executeBuiltinPopd - Restore a previously saved working directory."""
    if len(cmd.args) != 1:
        raise InternalShellError(cmd, "'popd' does not support arguments")
    if not shenv.dirStack:
        raise InternalShellError(cmd, "popd: directory stack empty")
    shenv.cwd = shenv.dirStack.pop()
    return ShellCommandResult(cmd, "", "", 0, False)


def executeBuiltinExport(cmd, shenv):
    """executeBuiltinExport - Set an environment variable."""
    if len(cmd.args) != 2:
        raise InternalShellError("'export' supports only one argument")
    updateEnv(shenv, cmd.args)
    return ShellCommandResult(cmd, "", "", 0, False)


def executeBuiltinEcho(cmd, shenv):
    """Interpret a redirected echo or @echo command"""
    opened_files = []
    stdin, stdout, stderr = processRedirects(cmd, subprocess.PIPE, shenv, opened_files)
    if stdin != subprocess.PIPE or stderr != subprocess.PIPE:
        raise InternalShellError(
            cmd, f"stdin and stderr redirects not supported for {cmd.args[0]}"
        )

    # Some tests have un-redirected echo commands to help debug test failures.
    # Buffer our output and return it to the caller.
    is_redirected = True
    encode = lambda x: x
    if stdout == subprocess.PIPE:
        is_redirected = False
        stdout = StringIO()
    elif kIsWindows:
        # Reopen stdout in binary mode to avoid CRLF translation. The versions
        # of echo we are replacing on Windows all emit plain LF, and the LLVM
        # tests now depend on this.
        # When we open as binary, however, this also means that we have to write
        # 'bytes' objects to stdout instead of 'str' objects.
        encode = lit.util.to_bytes
        stdout = open(stdout.name, stdout.mode + "b")
        opened_files.append((None, None, stdout, None))

    # Implement echo flags. We only support -e and -n, and not yet in
    # combination. We have to ignore unknown flags, because `echo "-D FOO"`
    # prints the dash.
    args = cmd.args[1:]
    interpret_escapes = False
    write_newline = True
    while len(args) >= 1 and args[0] in ("-e", "-n"):
        flag = args[0]
        args = args[1:]
        if flag == "-e":
            interpret_escapes = True
        elif flag == "-n":
            write_newline = False

    def maybeUnescape(arg):
        if not interpret_escapes:
            return arg

        arg = lit.util.to_bytes(arg)
        codec = "string_escape" if sys.version_info < (3, 0) else "unicode_escape"
        return arg.decode(codec)

    if args:
        for arg in args[:-1]:
            stdout.write(encode(maybeUnescape(arg)))
            stdout.write(encode(" "))
        stdout.write(encode(maybeUnescape(args[-1])))
    if write_newline:
        stdout.write(encode("\n"))

    for (name, mode, f, path) in opened_files:
        f.close()

    output = "" if is_redirected else stdout.getvalue()
    return ShellCommandResult(cmd, output, "", 0, False)


def executeBuiltinMkdir(cmd, cmd_shenv):
    """executeBuiltinMkdir - Create new directories."""
    args = expand_glob_expressions(cmd.args, cmd_shenv.cwd)[1:]
    try:
        opts, args = getopt.gnu_getopt(args, "p")
    except getopt.GetoptError as err:
        raise InternalShellError(cmd, "Unsupported: 'mkdir':  %s" % str(err))

    parent = False
    for o, a in opts:
        if o == "-p":
            parent = True
        else:
            assert False, "unhandled option"

    if len(args) == 0:
        raise InternalShellError(cmd, "Error: 'mkdir' is missing an operand")

    stderr = StringIO()
    exitCode = 0
    for dir in args:
        cwd = cmd_shenv.cwd
        dir = to_unicode(dir) if kIsWindows else to_bytes(dir)
        cwd = to_unicode(cwd) if kIsWindows else to_bytes(cwd)
        if not os.path.isabs(dir):
            dir = lit.util.abs_path_preserve_drive(os.path.join(cwd, dir))
        if parent:
            lit.util.mkdir_p(dir)
        else:
            try:
                lit.util.mkdir(dir)
            except OSError as err:
                stderr.write("Error: 'mkdir' command failed, %s\n" % str(err))
                exitCode = 1
    return ShellCommandResult(cmd, "", stderr.getvalue(), exitCode, False)


def executeBuiltinRm(cmd, cmd_shenv):
    """executeBuiltinRm - Removes (deletes) files or directories."""
    args = expand_glob_expressions(cmd.args, cmd_shenv.cwd)[1:]
    try:
        opts, args = getopt.gnu_getopt(args, "frR", ["--recursive"])
    except getopt.GetoptError as err:
        raise InternalShellError(cmd, "Unsupported: 'rm':  %s" % str(err))

    force = False
    recursive = False
    for o, a in opts:
        if o == "-f":
            force = True
        elif o in ("-r", "-R", "--recursive"):
            recursive = True
        else:
            assert False, "unhandled option"

    if len(args) == 0:
        raise InternalShellError(cmd, "Error: 'rm' is missing an operand")

    def on_rm_error(func, path, exc_info):
        # path contains the path of the file that couldn't be removed
        # let's just assume that it's read-only and remove it.
        os.chmod(path, stat.S_IMODE(os.stat(path).st_mode) | stat.S_IWRITE)
        os.remove(path)

    stderr = StringIO()
    exitCode = 0
    for path in args:
        cwd = cmd_shenv.cwd
        path = to_unicode(path) if kIsWindows else to_bytes(path)
        cwd = to_unicode(cwd) if kIsWindows else to_bytes(cwd)
        if not os.path.isabs(path):
            path = lit.util.abs_path_preserve_drive(os.path.join(cwd, path))
        if force and not os.path.exists(path):
            continue
        try:
            if os.path.isdir(path):
                if not recursive:
                    stderr.write("Error: %s is a directory\n" % path)
                    exitCode = 1
                if platform.system() == "Windows":
                    # NOTE: use ctypes to access `SHFileOperationsW` on Windows to
                    # use the NT style path to get access to long file paths which
                    # cannot be removed otherwise.
                    from ctypes.wintypes import BOOL, HWND, LPCWSTR, UINT, WORD
                    from ctypes import addressof, byref, c_void_p, create_unicode_buffer
                    from ctypes import Structure
                    from ctypes import windll, WinError, POINTER

                    class SHFILEOPSTRUCTW(Structure):
                        _fields_ = [
                            ("hWnd", HWND),
                            ("wFunc", UINT),
                            ("pFrom", LPCWSTR),
                            ("pTo", LPCWSTR),
                            ("fFlags", WORD),
                            ("fAnyOperationsAborted", BOOL),
                            ("hNameMappings", c_void_p),
                            ("lpszProgressTitle", LPCWSTR),
                        ]

                    FO_MOVE, FO_COPY, FO_DELETE, FO_RENAME = range(1, 5)

                    FOF_SILENT = 4
                    FOF_NOCONFIRMATION = 16
                    FOF_NOCONFIRMMKDIR = 512
                    FOF_NOERRORUI = 1024

                    FOF_NO_UI = (
                        FOF_SILENT
                        | FOF_NOCONFIRMATION
                        | FOF_NOERRORUI
                        | FOF_NOCONFIRMMKDIR
                    )

                    SHFileOperationW = windll.shell32.SHFileOperationW
                    SHFileOperationW.argtypes = [POINTER(SHFILEOPSTRUCTW)]

                    path = os.path.abspath(path)

                    pFrom = create_unicode_buffer(path, len(path) + 2)
                    pFrom[len(path)] = pFrom[len(path) + 1] = "\0"
                    operation = SHFILEOPSTRUCTW(
                        wFunc=UINT(FO_DELETE),
                        pFrom=LPCWSTR(addressof(pFrom)),
                        fFlags=FOF_NO_UI,
                    )
                    result = SHFileOperationW(byref(operation))
                    if result:
                        raise WinError(result)
                else:
                    shutil.rmtree(path, onerror=on_rm_error if force else None)
            else:
                if force and not os.access(path, os.W_OK):
                    os.chmod(path, stat.S_IMODE(os.stat(path).st_mode) | stat.S_IWRITE)
                os.remove(path)
        except OSError as err:
            stderr.write("Error: 'rm' command failed, %s" % str(err))
            exitCode = 1
    return ShellCommandResult(cmd, "", stderr.getvalue(), exitCode, False)


def executeBuiltinColon(cmd, cmd_shenv):
    """executeBuiltinColon - Discard arguments and exit with status 0."""
    return ShellCommandResult(cmd, "", "", 0, False)


def processRedirects(cmd, stdin_source, cmd_shenv, opened_files):
    """Return the standard fds for cmd after applying redirects

    Returns the three standard file descriptors for the new child process.  Each
    fd may be an open, writable file object or a sentinel value from the
    subprocess module.
    """

    # Apply the redirections, we use (N,) as a sentinel to indicate stdin,
    # stdout, stderr for N equal to 0, 1, or 2 respectively. Redirects to or
    # from a file are represented with a list [file, mode, file-object]
    # where file-object is initially None.
    redirects = [(0,), (1,), (2,)]
    for (op, filename) in cmd.redirects:
        if op == (">", 2):
            redirects[2] = [filename, "w", None]
        elif op == (">>", 2):
            redirects[2] = [filename, "a", None]
        elif op == (">&", 2) and filename in "012":
            redirects[2] = redirects[int(filename)]
        elif op == (">&",) or op == ("&>",):
            redirects[1] = redirects[2] = [filename, "w", None]
        elif op == (">",):
            redirects[1] = [filename, "w", None]
        elif op == (">>",):
            redirects[1] = [filename, "a", None]
        elif op == ("<",):
            redirects[0] = [filename, "r", None]
        else:
            raise InternalShellError(
                cmd, "Unsupported redirect: %r" % ((op, filename),)
            )

    # Open file descriptors in a second pass.
    std_fds = [None, None, None]
    for (index, r) in enumerate(redirects):
        # Handle the sentinel values for defaults up front.
        if isinstance(r, tuple):
            if r == (0,):
                fd = stdin_source
            elif r == (1,):
                if index == 0:
                    raise InternalShellError(cmd, "Unsupported redirect for stdin")
                elif index == 1:
                    fd = subprocess.PIPE
                else:
                    fd = subprocess.STDOUT
            elif r == (2,):
                if index != 2:
                    raise InternalShellError(cmd, "Unsupported redirect on stdout")
                fd = subprocess.PIPE
            else:
                raise InternalShellError(cmd, "Bad redirect")
            std_fds[index] = fd
            continue

        (filename, mode, fd) = r

        # Check if we already have an open fd. This can happen if stdout and
        # stderr go to the same place.
        if fd is not None:
            std_fds[index] = fd
            continue

        redir_filename = None
        name = expand_glob(filename, cmd_shenv.cwd)
        if len(name) != 1:
            raise InternalShellError(
                cmd, "Unsupported: glob in " "redirect expanded to multiple files"
            )
        name = name[0]
        if kAvoidDevNull and name == kDevNull:
            fd = tempfile.TemporaryFile(mode=mode)
        elif kIsWindows and name == "/dev/tty":
            # Simulate /dev/tty on Windows.
            # "CON" is a special filename for the console.
            fd = open("CON", mode)
        else:
            # Make sure relative paths are relative to the cwd.
            redir_filename = os.path.join(cmd_shenv.cwd, name)
            redir_filename = (
                to_unicode(redir_filename) if kIsWindows else to_bytes(redir_filename)
            )
            fd = open(redir_filename, mode)
        # Workaround a Win32 and/or subprocess bug when appending.
        #
        # FIXME: Actually, this is probably an instance of PR6753.
        if mode == "a":
            fd.seek(0, 2)
        # Mutate the underlying redirect list so that we can redirect stdout
        # and stderr to the same place without opening the file twice.
        r[2] = fd
        opened_files.append((filename, mode, fd) + (redir_filename,))
        std_fds[index] = fd

    return std_fds


def _executeShCmd(cmd, shenv, results, timeoutHelper):
    if timeoutHelper.timeoutReached():
        # Prevent further recursion if the timeout has been hit
        # as we should try avoid launching more processes.
        return None

    if isinstance(cmd, ShUtil.Seq):
        if cmd.op == ";":
            res = _executeShCmd(cmd.lhs, shenv, results, timeoutHelper)
            return _executeShCmd(cmd.rhs, shenv, results, timeoutHelper)

        if cmd.op == "&":
            raise InternalShellError(cmd, "unsupported shell operator: '&'")

        if cmd.op == "||":
            res = _executeShCmd(cmd.lhs, shenv, results, timeoutHelper)
            if res != 0:
                res = _executeShCmd(cmd.rhs, shenv, results, timeoutHelper)
            return res

        if cmd.op == "&&":
            res = _executeShCmd(cmd.lhs, shenv, results, timeoutHelper)
            if res is None:
                return res

            if res == 0:
                res = _executeShCmd(cmd.rhs, shenv, results, timeoutHelper)
            return res

        raise ValueError("Unknown shell command: %r" % cmd.op)
    assert isinstance(cmd, ShUtil.Pipeline)

    procs = []
    proc_not_counts = []
    default_stdin = subprocess.PIPE
    stderrTempFiles = []
    opened_files = []
    named_temp_files = []
    builtin_commands = set(["cat", "diff"])
    builtin_commands_dir = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "builtin_commands"
    )
    inproc_builtins = {
        "cd": executeBuiltinCd,
        "export": executeBuiltinExport,
        "echo": executeBuiltinEcho,
        "@echo": executeBuiltinEcho,
        "mkdir": executeBuiltinMkdir,
        "popd": executeBuiltinPopd,
        "pushd": executeBuiltinPushd,
        "rm": executeBuiltinRm,
        ":": executeBuiltinColon,
    }
    # To avoid deadlock, we use a single stderr stream for piped
    # output. This is null until we have seen some output using
    # stderr.
    for i, j in enumerate(cmd.commands):
        # Reference the global environment by default.
        cmd_shenv = shenv
        args = list(j.args)
        not_args = []
        not_count = 0
        not_crash = False
        while True:
            if args[0] == "env":
                # Create a copy of the global environment and modify it for
                # this one command. There might be multiple envs in a pipeline,
                # and there might be multiple envs in a command (usually when
                # one comes from a substitution):
                #   env FOO=1 llc < %s | env BAR=2 llvm-mc | FileCheck %s
                #   env FOO=1 %{another_env_plus_cmd} | FileCheck %s
                if cmd_shenv is shenv:
                    cmd_shenv = ShellEnvironment(shenv.cwd, shenv.env)
                args = updateEnv(cmd_shenv, args)
                if not args:
                    raise InternalShellError(j, "Error: 'env' requires a" " subcommand")
            elif args[0] == "not":
                not_args.append(args.pop(0))
                not_count += 1
                if args and args[0] == "--crash":
                    not_args.append(args.pop(0))
                    not_crash = True
                if not args:
                    raise InternalShellError(j, "Error: 'not' requires a" " subcommand")
            elif args[0] == "!":
                not_args.append(args.pop(0))
                not_count += 1
                if not args:
                    raise InternalShellError(j, "Error: '!' requires a" " subcommand")
            else:
                break

        # Handle in-process builtins.
        #
        # Handle "echo" as a builtin if it is not part of a pipeline. This
        # greatly speeds up tests that construct input files by repeatedly
        # echo-appending to a file.
        # FIXME: Standardize on the builtin echo implementation. We can use a
        # temporary file to sidestep blocking pipe write issues.
        inproc_builtin = inproc_builtins.get(args[0], None)
        if inproc_builtin and (args[0] != "echo" or len(cmd.commands) == 1):
            # env calling an in-process builtin is useless, so we take the safe
            # approach of complaining.
            if not cmd_shenv is shenv:
                raise InternalShellError(
                    j, "Error: 'env' cannot call '{}'".format(args[0])
                )
            if not_crash:
                raise InternalShellError(
                    j, "Error: 'not --crash' cannot call" " '{}'".format(args[0])
                )
            if len(cmd.commands) != 1:
                raise InternalShellError(
                    j,
                    "Unsupported: '{}' cannot be part" " of a pipeline".format(args[0]),
                )
            result = inproc_builtin(Command(args, j.redirects), cmd_shenv)
            if not_count % 2:
                result.exitCode = int(not result.exitCode)
            result.command.args = j.args
            results.append(result)
            return result.exitCode

        # Resolve any out-of-process builtin command before adding back 'not'
        # commands.
        if args[0] in builtin_commands:
            args.insert(0, sys.executable)
            cmd_shenv.env["PYTHONPATH"] = os.path.dirname(os.path.abspath(__file__))
            args[1] = os.path.join(builtin_commands_dir, args[1] + ".py")

        # We had to search through the 'not' commands to find all the 'env'
        # commands and any other in-process builtin command.  We don't want to
        # reimplement 'not' and its '--crash' here, so just push all 'not'
        # commands back to be called as external commands.  Because this
        # approach effectively moves all 'env' commands up front, it relies on
        # the assumptions that (1) environment variables are not intended to be
        # relevant to 'not' commands and (2) the 'env' command should always
        # blindly pass along the status it receives from any command it calls.

        # For plain negations, either 'not' without '--crash', or the shell
        # operator '!', leave them out from the command to execute and
        # invert the result code afterwards.
        if not_crash:
            args = not_args + args
            not_count = 0
        else:
            not_args = []

        stdin, stdout, stderr = processRedirects(
            j, default_stdin, cmd_shenv, opened_files
        )

        # If stderr wants to come from stdout, but stdout isn't a pipe, then put
        # stderr on a pipe and treat it as stdout.
        if stderr == subprocess.STDOUT and stdout != subprocess.PIPE:
            stderr = subprocess.PIPE
            stderrIsStdout = True
        else:
            stderrIsStdout = False

            # Don't allow stderr on a PIPE except for the last
            # process, this could deadlock.
            #
            # FIXME: This is slow, but so is deadlock.
            if stderr == subprocess.PIPE and j != cmd.commands[-1]:
                stderr = tempfile.TemporaryFile(mode="w+b")
                stderrTempFiles.append((i, stderr))

        # Resolve the executable path ourselves.
        executable = None
        # For paths relative to cwd, use the cwd of the shell environment.
        if args[0].startswith("."):
            exe_in_cwd = os.path.join(cmd_shenv.cwd, args[0])
            if os.path.isfile(exe_in_cwd):
                executable = exe_in_cwd
        if not executable:
            executable = lit.util.which(args[0], cmd_shenv.env["PATH"])
        if not executable:
            raise InternalShellError(j, "%r: command not found" % args[0])

        # Replace uses of /dev/null with temporary files.
        if kAvoidDevNull:
            # In Python 2.x, basestring is the base class for all string (including unicode)
            # In Python 3.x, basestring no longer exist and str is always unicode
            try:
                str_type = basestring
            except NameError:
                str_type = str
            for i, arg in enumerate(args):
                if isinstance(arg, str_type) and kDevNull in arg:
                    f = tempfile.NamedTemporaryFile(delete=False)
                    f.close()
                    named_temp_files.append(f.name)
                    args[i] = arg.replace(kDevNull, f.name)

        # Expand all glob expressions
        args = expand_glob_expressions(args, cmd_shenv.cwd)

        # On Windows, do our own command line quoting for better compatibility
        # with some core utility distributions.
        if kIsWindows:
            args = quote_windows_command(args)

        try:
            procs.append(
                subprocess.Popen(
                    args,
                    cwd=cmd_shenv.cwd,
                    executable=executable,
                    stdin=stdin,
                    stdout=stdout,
                    stderr=stderr,
                    env=cmd_shenv.env,
                    close_fds=kUseCloseFDs,
                    universal_newlines=True,
                    errors="replace",
                )
            )
            proc_not_counts.append(not_count)
            # Let the helper know about this process
            timeoutHelper.addProcess(procs[-1])
        except OSError as e:
            raise InternalShellError(
                j, "Could not create process ({}) due to {}".format(executable, e)
            )

        # Immediately close stdin for any process taking stdin from us.
        if stdin == subprocess.PIPE:
            procs[-1].stdin.close()
            procs[-1].stdin = None

        # Update the current stdin source.
        if stdout == subprocess.PIPE:
            default_stdin = procs[-1].stdout
        elif stderrIsStdout:
            default_stdin = procs[-1].stderr
        else:
            default_stdin = subprocess.PIPE

    # Explicitly close any redirected files. We need to do this now because we
    # need to release any handles we may have on the temporary files (important
    # on Win32, for example). Since we have already spawned the subprocess, our
    # handles have already been transferred so we do not need them anymore.
    for (name, mode, f, path) in opened_files:
        f.close()

    # FIXME: There is probably still deadlock potential here. Yawn.
    procData = [None] * len(procs)
    procData[-1] = procs[-1].communicate()

    for i in range(len(procs) - 1):
        if procs[i].stdout is not None:
            out = procs[i].stdout.read()
        else:
            out = ""
        if procs[i].stderr is not None:
            err = procs[i].stderr.read()
        else:
            err = ""
        procData[i] = (out, err)

    # Read stderr out of the temp files.
    for i, f in stderrTempFiles:
        f.seek(0, 0)
        procData[i] = (procData[i][0], f.read())
        f.close()

    exitCode = None
    for i, (out, err) in enumerate(procData):
        res = procs[i].wait()
        # Detect Ctrl-C in subprocess.
        if res == -signal.SIGINT:
            raise KeyboardInterrupt
        if proc_not_counts[i] % 2:
            res = 1 if res == 0 else 0
        elif proc_not_counts[i] > 1:
            res = 1 if res != 0 else 0

        # Ensure the resulting output is always of string type.
        try:
            if out is None:
                out = ""
            else:
                out = to_string(out.decode("utf-8", errors="replace"))
        except:
            out = str(out)
        try:
            if err is None:
                err = ""
            else:
                err = to_string(err.decode("utf-8", errors="replace"))
        except:
            err = str(err)

        # Gather the redirected output files for failed commands.
        output_files = []
        if res != 0:
            for (name, mode, f, path) in sorted(opened_files):
                if path is not None and mode in ("w", "a"):
                    try:
                        with open(path, "rb") as f:
                            data = f.read()
                    except:
                        data = None
                    if data is not None:
                        output_files.append((name, path, data))

        results.append(
            ShellCommandResult(
                cmd.commands[i],
                out,
                err,
                res,
                timeoutHelper.timeoutReached(),
                output_files,
            )
        )
        if cmd.pipe_err:
            # Take the last failing exit code from the pipeline.
            if not exitCode or res != 0:
                exitCode = res
        else:
            exitCode = res

    # Remove any named temporary files we created.
    for f in named_temp_files:
        try:
            os.remove(f)
        except OSError:
            pass

    if cmd.negate:
        exitCode = not exitCode

    return exitCode


def formatOutput(title, data, limit=None):
    if not data.strip():
        return ""
    if not limit is None and len(data) > limit:
        data = data[:limit] + "\n...\n"
        msg = "data was truncated"
    else:
        msg = ""
    ndashes = 30
    # fmt: off
    out =  f"# .---{title}{'-' * (ndashes - 4 - len(title))}\n"
    out += f"# | " + "\n# | ".join(data.splitlines()) + "\n"
    out += f"# `---{msg}{'-' * (ndashes - 4 - len(msg))}\n"
    # fmt: on
    return out


# Always either returns the tuple (out, err, exitCode, timeoutInfo) or raises a
# ScriptFatal exception.
#
# If debug is True (the normal lit behavior), err is empty, and out contains an
# execution trace, including stdout and stderr shown per command executed.
#
# If debug is False (set by some custom lit test formats that call this
# function), out contains only stdout from the script, err contains only stderr
# from the script, and there is no execution trace.
def executeScriptInternal(
    test, litConfig, tmpBase, commands, cwd, debug=True
) -> Tuple[str, str, int, Optional[str]]:
    cmds = []
    for i, ln in enumerate(commands):
        # Within lit, we try to always add '%dbg(...)' to command lines in order
        # to maximize debuggability.  However, custom lit test formats might not
        # always add it, so add a generic debug message in that case.
        match = re.fullmatch(kPdbgRegex, ln)
        if match:
            dbg = match.group(1)
            command = match.group(2)
        else:
            dbg = "command line"
            command = ln
        if debug:
            ln = f"@echo '# {dbg}' "
            if command:
                ln += f"&& @echo {shlex.quote(command.lstrip())} && {command}"
            else:
                ln += "has no command after substitutions"
        else:
            ln = command
        try:
            cmds.append(
                ShUtil.ShParser(ln, litConfig.isWindows, test.config.pipefail).parse()
            )
        except:
            raise ScriptFatal(
                f"shell parser error on {dbg}: {command.lstrip()}\n"
            ) from None

    cmd = cmds[0]
    for c in cmds[1:]:
        cmd = ShUtil.Seq(cmd, "&&", c)

    results = []
    timeoutInfo = None
    try:
        shenv = ShellEnvironment(cwd, test.config.environment)
        exitCode, timeoutInfo = executeShCmd(
            cmd, shenv, results, timeout=litConfig.maxIndividualTestTime
        )
    except InternalShellError:
        e = sys.exc_info()[1]
        exitCode = 127
        results.append(ShellCommandResult(e.command, "", e.message, exitCode, False))

    out = err = ""
    for i, result in enumerate(results):
        if not debug:
            out += result.stdout
            err += result.stderr
            continue

        # The purpose of an "@echo" command is merely to add a debugging message
        # directly to lit's output.  It is used internally by lit's internal
        # shell and is not currently documented for use in lit tests.  However,
        # if someone misuses it (e.g., both "echo" and "@echo" complain about
        # stdin redirection), produce the normal execution trace to facilitate
        # debugging.
        if (
            result.command.args[0] == "@echo"
            and result.exitCode == 0
            and not result.stderr
            and not result.outputFiles
            and not result.timeoutReached
        ):
            out += result.stdout
            continue

        # Write the command line that was run.  Properly quote it.  Leading
        # "!" commands should not be quoted as that would indicate they are not
        # the builtins.
        out += "# executed command: "
        nLeadingBangs = next(
            (i for i, cmd in enumerate(result.command.args) if cmd != "!"),
            len(result.command.args),
        )
        out += "! " * nLeadingBangs
        out += " ".join(
            shlex.quote(str(s))
            for i, s in enumerate(result.command.args)
            if i >= nLeadingBangs
        )
        out += "\n"

        # If nothing interesting happened, move on.
        if (
            litConfig.maxIndividualTestTime == 0
            and result.exitCode == 0
            and not result.stdout.strip()
            and not result.stderr.strip()
        ):
            continue

        # Otherwise, something failed or was printed, show it.

        # Add the command output, if redirected.
        for (name, path, data) in result.outputFiles:
            data = to_string(data.decode("utf-8", errors="replace"))
            out += formatOutput(f"redirected output from '{name}'", data, limit=1024)
        if result.stdout.strip():
            out += formatOutput("command stdout", result.stdout)
        if result.stderr.strip():
            out += formatOutput("command stderr", result.stderr)
        if not result.stdout.strip() and not result.stderr.strip():
            out += "# note: command had no output on stdout or stderr\n"

        # Show the error conditions:
        if result.exitCode != 0:
            # On Windows, a negative exit code indicates a signal, and those are
            # easier to recognize or look up if we print them in hex.
            if litConfig.isWindows and (result.exitCode < 0 or result.exitCode > 255):
                codeStr = hex(int(result.exitCode & 0xFFFFFFFF)).rstrip("L")
            else:
                codeStr = str(result.exitCode)
            out += "# error: command failed with exit status: %s\n" % (codeStr,)
        if litConfig.maxIndividualTestTime > 0 and result.timeoutReached:
            out += "# error: command reached timeout: %s\n" % (
                str(result.timeoutReached),
            )

    return out, err, exitCode, timeoutInfo


def executeScript(test, litConfig, tmpBase, commands, cwd):
    bashPath = litConfig.getBashPath()
    isWin32CMDEXE = litConfig.isWindows and not bashPath
    script = tmpBase + ".script"
    if isWin32CMDEXE:
        script += ".bat"

    # Write script file
    mode = "w"
    open_kwargs = {}
    if litConfig.isWindows and not isWin32CMDEXE:
        mode += "b"  # Avoid CRLFs when writing bash scripts.
    elif sys.version_info > (3, 0):
        open_kwargs["encoding"] = "utf-8"
    f = open(script, mode, **open_kwargs)
    if isWin32CMDEXE:
        for i, ln in enumerate(commands):
            match = re.fullmatch(kPdbgRegex, ln)
            if match:
                command = match.group(2)
                commands[i] = match.expand(
                    "echo '\\1' > nul && " if command else "echo '\\1' > nul"
                )
        f.write("@echo on\n")
        f.write("\n@if %ERRORLEVEL% NEQ 0 EXIT\n".join(commands))
    else:
        for i, ln in enumerate(commands):
            match = re.fullmatch(kPdbgRegex, ln)
            if match:
                dbg = match.group(1)
                command = match.group(2)
                # Echo the debugging diagnostic to stderr.
                #
                # For that echo command, use 'set' commands to suppress the
                # shell's execution trace, which would just add noise.  Suppress
                # the shell's execution trace for the 'set' commands by
                # redirecting their stderr to /dev/null.
                if command:
                    msg = f"'{dbg}': {shlex.quote(command.lstrip())}"
                else:
                    msg = f"'{dbg}' has no command after substitutions"
                commands[i] = (
                    f"{{ set +x; }} 2>/dev/null && "
                    f"echo {msg} >&2 && "
                    f"{{ set -x; }} 2>/dev/null"
                )
                # Execute the command, if any.
                #
                # 'command' might be something like:
                #
                #   subcmd & PID=$!
                #
                # In that case, we need something like:
                #
                #   echo_dbg && { subcmd & PID=$!; }
                #
                # Without the '{ ...; }' enclosing the original 'command', '&'
                # would put all of 'echo_dbg && subcmd' in the background.  This
                # would cause 'echo_dbg' to execute at the wrong time, and a
                # later kill of $PID would target the wrong process. We have
                # seen the latter manage to terminate the shell running lit.
                if command:
                    commands[i] += f" && {{ {command}; }}"
        if test.config.pipefail:
            f.write(b"set -o pipefail;" if mode == "wb" else "set -o pipefail;")
        f.write(b"set -x;" if mode == "wb" else "set -x;")
        if sys.version_info > (3, 0) and mode == "wb":
            f.write(bytes("{ " + "; } &&\n{ ".join(commands) + "; }", "utf-8"))
        else:
            f.write("{ " + "; } &&\n{ ".join(commands) + "; }")
    f.write(b"\n" if mode == "wb" else "\n")
    f.close()

    if isWin32CMDEXE:
        command = ["cmd", "/c", script]
    else:
        if bashPath:
            command = [bashPath, script]
        else:
            command = ["/bin/sh", script]
        if litConfig.useValgrind:
            # FIXME: Running valgrind on sh is overkill. We probably could just
            # run on clang with no real loss.
            command = litConfig.valgrindArgs + command

    try:
        out, err, exitCode = lit.util.executeCommand(
            command,
            cwd=cwd,
            env=test.config.environment,
            timeout=litConfig.maxIndividualTestTime,
        )
        return (out, err, exitCode, None)
    except lit.util.ExecuteCommandTimeoutException as e:
        return (e.out, e.err, e.exitCode, e.msg)


def parseIntegratedTestScriptCommands(source_path, keywords):
    """
    parseIntegratedTestScriptCommands(source_path) -> commands

    Parse the commands in an integrated test script file into a list of
    (line_number, command_type, line).
    """

    # This code is carefully written to be dual compatible with Python 2.5+ and
    # Python 3 without requiring input files to always have valid codings. The
    # trick we use is to open the file in binary mode and use the regular
    # expression library to find the commands, with it scanning strings in
    # Python2 and bytes in Python3.
    #
    # Once we find a match, we do require each script line to be decodable to
    # UTF-8, so we convert the outputs to UTF-8 before returning. This way the
    # remaining code can work with "strings" agnostic of the executing Python
    # version.

    keywords_re = re.compile(
        to_bytes("(%s)(.*)\n" % ("|".join(re.escape(k) for k in keywords),))
    )

    f = open(source_path, "rb")
    try:
        # Read the entire file contents.
        data = f.read()

        # Ensure the data ends with a newline.
        if not data.endswith(to_bytes("\n")):
            data = data + to_bytes("\n")

        # Iterate over the matches.
        line_number = 1
        last_match_position = 0
        for match in keywords_re.finditer(data):
            # Compute the updated line number by counting the intervening
            # newlines.
            match_position = match.start()
            line_number += data.count(
                to_bytes("\n"), last_match_position, match_position
            )
            last_match_position = match_position

            # Convert the keyword and line to UTF-8 strings and yield the
            # command. Note that we take care to return regular strings in
            # Python 2, to avoid other code having to differentiate between the
            # str and unicode types.
            #
            # Opening the file in binary mode prevented Windows \r newline
            # characters from being converted to Unix \n newlines, so manually
            # strip those from the yielded lines.
            keyword, ln = match.groups()
            yield (
                line_number,
                to_string(keyword.decode("utf-8")),
                to_string(ln.decode("utf-8").rstrip("\r")),
            )
    finally:
        f.close()


def getTempPaths(test):
    """Get the temporary location, this is always relative to the test suite
    root, not test source root."""
    execpath = test.getExecPath()
    execdir, execbase = os.path.split(execpath)
    tmpDir = os.path.join(execdir, "Output")
    tmpBase = os.path.join(tmpDir, execbase)
    return tmpDir, tmpBase


def colonNormalizePath(path):
    if kIsWindows:
        return re.sub(r"^(.):", r"\1", path.replace("\\", "/"))
    else:
        assert path[0] == "/"
        return path[1:]


def getDefaultSubstitutions(test, tmpDir, tmpBase, normalize_slashes=False):
    sourcepath = test.getSourcePath()
    sourcedir = os.path.dirname(sourcepath)

    # Normalize slashes, if requested.
    if normalize_slashes:
        sourcepath = sourcepath.replace("\\", "/")
        sourcedir = sourcedir.replace("\\", "/")
        tmpDir = tmpDir.replace("\\", "/")
        tmpBase = tmpBase.replace("\\", "/")

    substitutions = []
    substitutions.extend(test.config.substitutions)
    tmpName = tmpBase + ".tmp"
    baseName = os.path.basename(tmpBase)

    substitutions.append(("%{pathsep}", os.pathsep))
    substitutions.append(("%basename_t", baseName))

    substitutions.extend(
        [
            ("%{fs-src-root}", pathlib.Path(sourcedir).anchor),
            ("%{fs-tmp-root}", pathlib.Path(tmpBase).anchor),
            ("%{fs-sep}", os.path.sep),
        ]
    )

    substitutions.append(("%/et", tmpName.replace("\\", "\\\\\\\\\\\\\\\\")))

    def regex_escape(s):
        s = s.replace("@", r"\@")
        s = s.replace("&", r"\&")
        return s

    path_substitutions = [
        ("s", sourcepath), ("S", sourcedir), ("p", sourcedir),
        ("t", tmpName), ("T", tmpDir)
    ]
    for path_substitution in path_substitutions:
        letter = path_substitution[0]
        path = path_substitution[1]

        # Original path variant
        substitutions.append(("%" + letter, path))

        # Normalized path separator variant
        substitutions.append(("%/" + letter, path.replace("\\", "/")))

        # realpath variants
        # Windows paths with substitute drives are not expanded by default
        # as they are used to avoid MAX_PATH issues, but sometimes we do
        # need the fully expanded path.
        real_path = os.path.realpath(path)
        substitutions.append(("%{" + letter + ":real}", real_path))
        substitutions.append(("%{/" + letter + ":real}",
            real_path.replace("\\", "/")))

        # "%{/[STpst]:regex_replacement}" should be normalized like
        # "%/[STpst]" but we're also in a regex replacement context
        # of a s@@@ regex.
        substitutions.append(
            ("%{/" + letter + ":regex_replacement}",
            regex_escape(path.replace("\\", "/"))))

        # "%:[STpst]" are normalized paths without colons and without
        # a leading slash.
        substitutions.append(("%:" + letter, colonNormalizePath(path)))

    return substitutions


def _memoize(f):
    cache = {}  # Intentionally unbounded, see applySubstitutions()

    def memoized(x):
        if x not in cache:
            cache[x] = f(x)
        return cache[x]

    return memoized


@_memoize
def _caching_re_compile(r):
    return re.compile(r)


class ExpandableScriptDirective(object):
    """
    Common interface for lit directives for which any lit substitutions must be
    expanded to produce the shell script.  It includes directives (e.g., 'RUN:')
    specifying shell commands that might have lit substitutions to be expanded.
    It also includes lit directives (e.g., 'DEFINE:') that adjust substitutions.

    start_line_number: The directive's starting line number.
    end_line_number: The directive's ending line number, which is
        start_line_number if the directive has no line continuations.
    keyword: The keyword that specifies the directive.  For example, 'RUN:'.
    """

    def __init__(self, start_line_number, end_line_number, keyword):
        # Input line number where the directive starts.
        self.start_line_number = start_line_number
        # Input line number where the directive ends.
        self.end_line_number = end_line_number
        # The keyword used to indicate the directive.
        self.keyword = keyword

    def add_continuation(self, line_number, keyword, line):
        """
        Add a continuation line to this directive and return True, or do nothing
        and return False if the specified line is not a continuation for this
        directive (e.g., previous line does not end in '\', or keywords do not
        match).

        line_number: The line number for the continuation line.
        keyword: The keyword that specifies the continuation line.  For example,
            'RUN:'.
        line: The content of the continuation line after the keyword.
        """
        assert False, "expected method to be called on derived class"

    def needs_continuation(self):
        """
        Does this directive require a continuation line?

        '\' is documented as indicating a line continuation even if whitespace
        separates it from the newline.  It looks like a line continuation, and
        it would be confusing if it didn't behave as one.
        """
        assert False, "expected method to be called on derived class"

    def get_location(self):
        """
        Get a phrase describing the line or range of lines so far included by
        this directive and any line continuations.
        """
        if self.start_line_number == self.end_line_number:
            return f"at line {self.start_line_number}"
        return f"from line {self.start_line_number} to {self.end_line_number}"


class CommandDirective(ExpandableScriptDirective):
    """
    A lit directive taking a shell command line.  For example,
    'RUN: echo hello world'.

    command: The content accumulated so far from the directive and its
        continuation lines.
    """

    def __init__(self, start_line_number, end_line_number, keyword, line):
        super().__init__(start_line_number, end_line_number, keyword)
        self.command = line.rstrip()

    def add_continuation(self, line_number, keyword, line):
        if keyword != self.keyword or not self.needs_continuation():
            return False
        self.command = self.command[:-1] + line.rstrip()
        self.end_line_number = line_number
        return True

    def needs_continuation(self):
        # Trailing whitespace is stripped immediately when each line is added,
        # so '\' is never hidden here.
        return self.command[-1] == "\\"


class SubstDirective(ExpandableScriptDirective):
    """
    A lit directive taking a substitution definition or redefinition.  For
    example, 'DEFINE: %{name} = value'.

    new_subst: True if this directive defines a new substitution.  False if it
        redefines an existing substitution.
    body: The unparsed content accumulated so far from the directive and its
        continuation lines.
    name: The substitution's name, or None if more continuation lines are still
        required.
    value: The substitution's value, or None if more continuation lines are
        still required.
    """

    def __init__(self, start_line_number, end_line_number, keyword, new_subst, line):
        super().__init__(start_line_number, end_line_number, keyword)
        self.new_subst = new_subst
        self.body = line
        self.name = None
        self.value = None
        self._parse_body()

    def add_continuation(self, line_number, keyword, line):
        if keyword != self.keyword or not self.needs_continuation():
            return False
        if not line.strip():
            raise ValueError("Substitution's continuation is empty")
        # Append line.  Replace the '\' and any adjacent whitespace with a
        # single space.
        self.body = self.body.rstrip()[:-1].rstrip() + " " + line.lstrip()
        self.end_line_number = line_number
        self._parse_body()
        return True

    def needs_continuation(self):
        return self.body.rstrip()[-1:] == "\\"

    def _parse_body(self):
        """
        If no more line continuations are required, parse all the directive's
        accumulated lines in order to identify the substitution's name and full
        value, and raise an exception if invalid.
        """
        if self.needs_continuation():
            return

        # Extract the left-hand side and value, and discard any whitespace
        # enclosing each.
        parts = self.body.split("=", 1)
        if len(parts) == 1:
            raise ValueError("Substitution's definition does not contain '='")
        self.name = parts[0].strip()
        self.value = parts[1].strip()

        # Check the substitution's name.
        #
        # Do not extend this to permit '.' or any sequence that's special in a
        # python pattern.  We could escape that automatically for
        # DEFINE/REDEFINE directives in test files.  However, lit configuration
        # file authors would still have to remember to escape them manually in
        # substitution names but not in values.  Moreover, the manually chosen
        # and automatically chosen escape sequences would have to be consistent
        # (e.g., '\.' vs. '[.]') in order for REDEFINE to successfully redefine
        # a substitution previously defined by a lit configuration file.  All
        # this seems too error prone and confusing to be worthwhile.  If you
        # want your name to express structure, use ':' instead of '.'.
        #
        # Actually, '{' and '}' are special if they contain only digits possibly
        # separated by a comma.  Requiring a leading letter avoids that.
        if not re.fullmatch(r"%{[_a-zA-Z][-_:0-9a-zA-Z]*}", self.name):
            raise ValueError(
                f"Substitution name '{self.name}' is malformed as it must "
                f"start with '%{{', it must end with '}}', and the rest must "
                f"start with a letter or underscore and contain only "
                f"alphanumeric characters, hyphens, underscores, and colons"
            )

    def adjust_substitutions(self, substitutions):
        """
        Modify the specified substitution list as specified by this directive.
        """
        assert (
            not self.needs_continuation()
        ), "expected directive continuations to be parsed before applying"
        value_repl = self.value.replace("\\", "\\\\")
        existing = [i for i, subst in enumerate(substitutions) if self.name in subst[0]]
        existing_res = "".join(
            "\nExisting pattern: " + substitutions[i][0] for i in existing
        )
        if self.new_subst:
            if existing:
                raise ValueError(
                    f"Substitution whose pattern contains '{self.name}' is "
                    f"already defined before '{self.keyword}' directive "
                    f"{self.get_location()}"
                    f"{existing_res}"
                )
            substitutions.insert(0, (self.name, value_repl))
            return
        if len(existing) > 1:
            raise ValueError(
                f"Multiple substitutions whose patterns contain '{self.name}' "
                f"are defined before '{self.keyword}' directive "
                f"{self.get_location()}"
                f"{existing_res}"
            )
        if not existing:
            raise ValueError(
                f"No substitution for '{self.name}' is defined before "
                f"'{self.keyword}' directive {self.get_location()}"
            )
        if substitutions[existing[0]][0] != self.name:
            raise ValueError(
                f"Existing substitution whose pattern contains '{self.name}' "
                f"does not have the pattern specified by '{self.keyword}' "
                f"directive {self.get_location()}\n"
                f"Expected pattern: {self.name}"
                f"{existing_res}"
            )
        substitutions[existing[0]] = (self.name, value_repl)


def applySubstitutions(script, substitutions, conditions={}, recursion_limit=None):
    """
    Apply substitutions to the script.  Allow full regular expression syntax.
    Replace each matching occurrence of regular expression pattern a with
    substitution b in line ln.

    If a substitution expands into another substitution, it is expanded
    recursively until the line has no more expandable substitutions. If
    the line can still can be substituted after being substituted
    `recursion_limit` times, it is an error. If the `recursion_limit` is
    `None` (the default), no recursive substitution is performed at all.
    """

    # We use #_MARKER_# to hide %% while we do the other substitutions.
    def escapePercents(ln):
        return _caching_re_compile("%%").sub("#_MARKER_#", ln)

    def unescapePercents(ln):
        return _caching_re_compile("#_MARKER_#").sub("%", ln)

    def substituteIfElse(ln):
        # early exit to avoid wasting time on lines without
        # conditional substitutions
        if ln.find("%if ") == -1:
            return ln

        def tryParseIfCond(ln):
            # space is important to not conflict with other (possible)
            # substitutions
            if not ln.startswith("%if "):
                return None, ln
            ln = ln[4:]

            # stop at '%{'
            match = _caching_re_compile("%{").search(ln)
            if not match:
                raise ValueError("'%{' is missing for %if substitution")
            cond = ln[: match.start()]

            # eat '%{' as well
            ln = ln[match.end() :]
            return cond, ln

        def tryParseElse(ln):
            match = _caching_re_compile(r"^\s*%else\s*(%{)?").search(ln)
            if not match:
                return False, ln
            if not match.group(1):
                raise ValueError("'%{' is missing for %else substitution")
            return True, ln[match.end() :]

        def tryParseEnd(ln):
            if ln.startswith("%}"):
                return True, ln[2:]
            return False, ln

        def parseText(ln, isNested):
            # parse everything until %if, or %} if we're parsing a
            # nested expression.
            match = _caching_re_compile(
                "(.*?)(?:%if|%})" if isNested else "(.*?)(?:%if)"
            ).search(ln)
            if not match:
                # there is no terminating pattern, so treat the whole
                # line as text
                return ln, ""
            text_end = match.end(1)
            return ln[:text_end], ln[text_end:]

        def parseRecursive(ln, isNested):
            result = ""
            while len(ln):
                if isNested:
                    found_end, _ = tryParseEnd(ln)
                    if found_end:
                        break

                # %if cond %{ branch_if %} %else %{ branch_else %}
                cond, ln = tryParseIfCond(ln)
                if cond:
                    branch_if, ln = parseRecursive(ln, isNested=True)
                    found_end, ln = tryParseEnd(ln)
                    if not found_end:
                        raise ValueError("'%}' is missing for %if substitution")

                    branch_else = ""
                    found_else, ln = tryParseElse(ln)
                    if found_else:
                        branch_else, ln = parseRecursive(ln, isNested=True)
                        found_end, ln = tryParseEnd(ln)
                        if not found_end:
                            raise ValueError("'%}' is missing for %else substitution")

                    if BooleanExpression.evaluate(cond, conditions):
                        result += branch_if
                    else:
                        result += branch_else
                    continue

                # The rest is handled as plain text.
                text, ln = parseText(ln, isNested)
                result += text

            return result, ln

        result, ln = parseRecursive(ln, isNested=False)
        assert len(ln) == 0
        return result

    def processLine(ln):
        # Apply substitutions
        ln = substituteIfElse(escapePercents(ln))
        for a, b in substitutions:
            if kIsWindows:
                b = b.replace("\\", "\\\\")
            # re.compile() has a built-in LRU cache with 512 entries. In some
            # test suites lit ends up thrashing that cache, which made e.g.
            # check-llvm run 50% slower.  Use an explicit, unbounded cache
            # to prevent that from happening.  Since lit is fairly
            # short-lived, since the set of substitutions is fairly small, and
            # since thrashing has such bad consequences, not bounding the cache
            # seems reasonable.
            ln = _caching_re_compile(a).sub(str(b), escapePercents(ln))

        # Strip the trailing newline and any extra whitespace.
        return ln.strip()

    def processLineToFixedPoint(ln):
        assert isinstance(recursion_limit, int) and recursion_limit >= 0
        origLine = ln
        steps = 0
        processed = processLine(ln)
        while processed != ln and steps < recursion_limit:
            ln = processed
            processed = processLine(ln)
            steps += 1

        if processed != ln:
            raise ValueError(
                "Recursive substitution of '%s' did not complete "
                "in the provided recursion limit (%s)" % (origLine, recursion_limit)
            )

        return processed

    process = processLine if recursion_limit is None else processLineToFixedPoint
    output = []
    for directive in script:
        if isinstance(directive, SubstDirective):
            directive.adjust_substitutions(substitutions)
        else:
            if isinstance(directive, CommandDirective):
                line = directive.command
            else:
                # Can come from preamble_commands.
                assert isinstance(directive, str)
                line = directive
            output.append(unescapePercents(process(line)))

    return output


class ParserKind(object):
    """
    An enumeration representing the style of an integrated test keyword or
    command.

    TAG: A keyword taking no value. Ex 'END.'
    COMMAND: A keyword taking a list of shell commands. Ex 'RUN:'
    LIST: A keyword taking a comma-separated list of values.
    SPACE_LIST: A keyword taking a space-separated list of values.
    BOOLEAN_EXPR: A keyword taking a comma-separated list of
        boolean expressions. Ex 'XFAIL:'
    INTEGER: A keyword taking a single integer. Ex 'ALLOW_RETRIES:'
    CUSTOM: A keyword with custom parsing semantics.
    DEFINE: A keyword taking a new lit substitution definition. Ex
        'DEFINE: %{name}=value'
    REDEFINE: A keyword taking a lit substitution redefinition. Ex
        'REDEFINE: %{name}=value'
    """

    TAG = 0
    COMMAND = 1
    LIST = 2
    SPACE_LIST = 3
    BOOLEAN_EXPR = 4
    INTEGER = 5
    CUSTOM = 6
    DEFINE = 7
    REDEFINE = 8

    @staticmethod
    def allowedKeywordSuffixes(value):
        return {
            ParserKind.TAG: ["."],
            ParserKind.COMMAND: [":"],
            ParserKind.LIST: [":"],
            ParserKind.SPACE_LIST: [":"],
            ParserKind.BOOLEAN_EXPR: [":"],
            ParserKind.INTEGER: [":"],
            ParserKind.CUSTOM: [":", "."],
            ParserKind.DEFINE: [":"],
            ParserKind.REDEFINE: [":"],
        }[value]

    @staticmethod
    def str(value):
        return {
            ParserKind.TAG: "TAG",
            ParserKind.COMMAND: "COMMAND",
            ParserKind.LIST: "LIST",
            ParserKind.SPACE_LIST: "SPACE_LIST",
            ParserKind.BOOLEAN_EXPR: "BOOLEAN_EXPR",
            ParserKind.INTEGER: "INTEGER",
            ParserKind.CUSTOM: "CUSTOM",
            ParserKind.DEFINE: "DEFINE",
            ParserKind.REDEFINE: "REDEFINE",
        }[value]


class IntegratedTestKeywordParser(object):
    """A parser for LLVM/Clang style integrated test scripts.

    keyword: The keyword to parse for. It must end in either '.' or ':'.
    kind: An value of ParserKind.
    parser: A custom parser. This value may only be specified with
            ParserKind.CUSTOM.
    """

    def __init__(self, keyword, kind, parser=None, initial_value=None):
        allowedSuffixes = ParserKind.allowedKeywordSuffixes(kind)
        if len(keyword) == 0 or keyword[-1] not in allowedSuffixes:
            if len(allowedSuffixes) == 1:
                raise ValueError(
                    "Keyword '%s' of kind '%s' must end in '%s'"
                    % (keyword, ParserKind.str(kind), allowedSuffixes[0])
                )
            else:
                raise ValueError(
                    "Keyword '%s' of kind '%s' must end in "
                    " one of '%s'"
                    % (keyword, ParserKind.str(kind), " ".join(allowedSuffixes))
                )

        if parser is not None and kind != ParserKind.CUSTOM:
            raise ValueError(
                "custom parsers can only be specified with " "ParserKind.CUSTOM"
            )
        self.keyword = keyword
        self.kind = kind
        self.parsed_lines = []
        self.value = initial_value
        self.parser = parser

        if kind == ParserKind.COMMAND:
            self.parser = lambda line_number, line, output: self._handleCommand(
                line_number, line, output, self.keyword
            )
        elif kind == ParserKind.LIST:
            self.parser = self._handleList
        elif kind == ParserKind.SPACE_LIST:
            self.parser = self._handleSpaceList
        elif kind == ParserKind.BOOLEAN_EXPR:
            self.parser = self._handleBooleanExpr
        elif kind == ParserKind.INTEGER:
            self.parser = self._handleSingleInteger
        elif kind == ParserKind.TAG:
            self.parser = self._handleTag
        elif kind == ParserKind.CUSTOM:
            if parser is None:
                raise ValueError("ParserKind.CUSTOM requires a custom parser")
            self.parser = parser
        elif kind == ParserKind.DEFINE:
            self.parser = lambda line_number, line, output: self._handleSubst(
                line_number, line, output, self.keyword, new_subst=True
            )
        elif kind == ParserKind.REDEFINE:
            self.parser = lambda line_number, line, output: self._handleSubst(
                line_number, line, output, self.keyword, new_subst=False
            )
        else:
            raise ValueError("Unknown kind '%s'" % kind)

    def parseLine(self, line_number, line):
        try:
            self.parsed_lines += [(line_number, line)]
            self.value = self.parser(line_number, line, self.value)
        except ValueError as e:
            raise ValueError(
                str(e)
                + ("\nin %s directive on test line %d" % (self.keyword, line_number))
            )

    def getValue(self):
        return self.value

    @staticmethod
    def _handleTag(line_number, line, output):
        """A helper for parsing TAG type keywords"""
        return not line.strip() or output

    @staticmethod
    def _substituteLineNumbers(line_number, line):
        line = re.sub(r"%\(line\)", str(line_number), line)

        def replace_line_number(match):
            if match.group(1) == "+":
                return str(line_number + int(match.group(2)))
            if match.group(1) == "-":
                return str(line_number - int(match.group(2)))

        return re.sub(r"%\(line *([\+-]) *(\d+)\)", replace_line_number, line)

    @classmethod
    def _handleCommand(cls, line_number, line, output, keyword):
        """A helper for parsing COMMAND type keywords"""
        # Substitute line number expressions.
        line = cls._substituteLineNumbers(line_number, line)

        # Collapse lines with trailing '\\', or add line with line number to
        # start a new pipeline.
        if not output or not output[-1].add_continuation(line_number, keyword, line):
            if output is None:
                output = []
            line = buildPdbgCommand(f"{keyword} at line {line_number}", line)
            output.append(CommandDirective(line_number, line_number, keyword, line))
        return output

    @staticmethod
    def _handleList(line_number, line, output):
        """A parser for LIST type keywords"""
        if output is None:
            output = []
        output.extend([s.strip() for s in line.split(",")])
        return output

    @staticmethod
    def _handleSpaceList(line_number, line, output):
        """A parser for SPACE_LIST type keywords"""
        if output is None:
            output = []
        output.extend([s.strip() for s in line.split(" ") if s.strip() != ""])
        return output

    @staticmethod
    def _handleSingleInteger(line_number, line, output):
        """A parser for INTEGER type keywords"""
        if output is None:
            output = []
        try:
            n = int(line)
        except ValueError:
            raise ValueError(
                "INTEGER parser requires the input to be an integer (got {})".format(
                    line
                )
            )
        output.append(n)
        return output

    @staticmethod
    def _handleBooleanExpr(line_number, line, output):
        """A parser for BOOLEAN_EXPR type keywords"""
        parts = [s.strip() for s in line.split(",") if s.strip() != ""]
        if output and output[-1][-1] == "\\":
            output[-1] = output[-1][:-1] + parts[0]
            del parts[0]
        if output is None:
            output = []
        output.extend(parts)
        # Evaluate each expression to verify syntax.
        # We don't want any results, just the raised ValueError.
        for s in output:
            if s != "*" and not s.endswith("\\"):
                BooleanExpression.evaluate(s, [])
        return output

    @classmethod
    def _handleSubst(cls, line_number, line, output, keyword, new_subst):
        """A parser for DEFINE and REDEFINE type keywords"""
        line = cls._substituteLineNumbers(line_number, line)
        if output and output[-1].add_continuation(line_number, keyword, line):
            return output
        if output is None:
            output = []
        output.append(
            SubstDirective(line_number, line_number, keyword, new_subst, line)
        )
        return output


def _parseKeywords(sourcepath, additional_parsers=[], require_script=True):
    """_parseKeywords

    Scan an LLVM/Clang style integrated test script and extract all the lines
    pertaining to a special parser. This includes 'RUN', 'XFAIL', 'REQUIRES',
    'UNSUPPORTED', 'ALLOW_RETRIES', 'END', 'DEFINE', 'REDEFINE', as well as
    other specified custom parsers.

    Returns a dictionary mapping each custom parser to its value after
    parsing the test.
    """
    # Install the built-in keyword parsers.
    script = []
    builtin_parsers = [
        IntegratedTestKeywordParser("RUN:", ParserKind.COMMAND, initial_value=script),
        IntegratedTestKeywordParser("XFAIL:", ParserKind.BOOLEAN_EXPR),
        IntegratedTestKeywordParser("REQUIRES:", ParserKind.BOOLEAN_EXPR),
        IntegratedTestKeywordParser("UNSUPPORTED:", ParserKind.BOOLEAN_EXPR),
        IntegratedTestKeywordParser("ALLOW_RETRIES:", ParserKind.INTEGER),
        IntegratedTestKeywordParser("END.", ParserKind.TAG),
        IntegratedTestKeywordParser("DEFINE:", ParserKind.DEFINE, initial_value=script),
        IntegratedTestKeywordParser(
            "REDEFINE:", ParserKind.REDEFINE, initial_value=script
        ),
    ]
    keyword_parsers = {p.keyword: p for p in builtin_parsers}

    # Install user-defined additional parsers.
    for parser in additional_parsers:
        if not isinstance(parser, IntegratedTestKeywordParser):
            raise ValueError(
                "Additional parser must be an instance of "
                "IntegratedTestKeywordParser"
            )
        if parser.keyword in keyword_parsers:
            raise ValueError("Parser for keyword '%s' already exists" % parser.keyword)
        keyword_parsers[parser.keyword] = parser

    # Collect the test lines from the script.
    for line_number, command_type, ln in parseIntegratedTestScriptCommands(
        sourcepath, keyword_parsers.keys()
    ):
        parser = keyword_parsers[command_type]
        parser.parseLine(line_number, ln)
        if command_type == "END." and parser.getValue() is True:
            break

    # Verify the script contains a run line.
    if require_script and not any(
        isinstance(directive, CommandDirective) for directive in script
    ):
        raise ValueError("Test has no 'RUN:' line")

    # Check for unterminated run or subst lines.
    #
    # If, after a line continuation for one kind of directive (e.g., 'RUN:',
    # 'DEFINE:', 'REDEFINE:') in script, the next directive in script is a
    # different kind, then the '\\' remains on the former, and we report it
    # here.
    for directive in script:
        if directive.needs_continuation():
            raise ValueError(
                f"Test has unterminated '{directive.keyword}' "
                f"directive (with '\\') "
                f"{directive.get_location()}"
            )

    # Check boolean expressions for unterminated lines.
    for key in keyword_parsers:
        kp = keyword_parsers[key]
        if kp.kind != ParserKind.BOOLEAN_EXPR:
            continue
        value = kp.getValue()
        if value and value[-1][-1] == "\\":
            raise ValueError(
                "Test has unterminated '{key}' lines (with '\\')".format(key=key)
            )

    # Make sure there's at most one ALLOW_RETRIES: line
    allowed_retries = keyword_parsers["ALLOW_RETRIES:"].getValue()
    if allowed_retries and len(allowed_retries) > 1:
        raise ValueError("Test has more than one ALLOW_RETRIES lines")

    return {p.keyword: p.getValue() for p in keyword_parsers.values()}


def parseIntegratedTestScript(test, additional_parsers=[], require_script=True):
    """parseIntegratedTestScript - Scan an LLVM/Clang style integrated test
    script and extract the lines to 'RUN' as well as 'XFAIL', 'REQUIRES',
    'UNSUPPORTED' and 'ALLOW_RETRIES' information into the given test.

    If additional parsers are specified then the test is also scanned for the
    keywords they specify and all matches are passed to the custom parser.

    If 'require_script' is False an empty script
    may be returned. This can be used for test formats where the actual script
    is optional or ignored.
    """
    # Parse the test sources and extract test properties
    try:
        parsed = _parseKeywords(
            test.getSourcePath(), additional_parsers, require_script
        )
    except ValueError as e:
        return lit.Test.Result(Test.UNRESOLVED, str(e))
    script = parsed["RUN:"] or []
    assert parsed["DEFINE:"] == script
    assert parsed["REDEFINE:"] == script
    test.xfails += parsed["XFAIL:"] or []
    test.requires += parsed["REQUIRES:"] or []
    test.unsupported += parsed["UNSUPPORTED:"] or []
    if parsed["ALLOW_RETRIES:"]:
        test.allowed_retries = parsed["ALLOW_RETRIES:"][0]

    # Enforce REQUIRES:
    missing_required_features = test.getMissingRequiredFeatures()
    if missing_required_features:
        msg = ", ".join(missing_required_features)
        return lit.Test.Result(
            Test.UNSUPPORTED,
            "Test requires the following unavailable " "features: %s" % msg,
        )

    # Enforce UNSUPPORTED:
    unsupported_features = test.getUnsupportedFeatures()
    if unsupported_features:
        msg = ", ".join(unsupported_features)
        return lit.Test.Result(
            Test.UNSUPPORTED,
            "Test does not support the following features " "and/or targets: %s" % msg,
        )

    # Enforce limit_to_features.
    if not test.isWithinFeatureLimits():
        msg = ", ".join(test.config.limit_to_features)
        return lit.Test.Result(
            Test.UNSUPPORTED,
            "Test does not require any of the features "
            "specified in limit_to_features: %s" % msg,
        )

    return script


def _runShTest(test, litConfig, useExternalSh, script, tmpBase) -> lit.Test.Result:
    # Always returns the tuple (out, err, exitCode, timeoutInfo, status).
    def runOnce(
        execdir,
    ) -> Tuple[str, str, int, Optional[str], Test.ResultCode]:
        # script is modified below (for litConfig.per_test_coverage, and for
        # %dbg expansions).  runOnce can be called multiple times, but applying
        # the modifications multiple times can corrupt script, so always modify
        # a copy.
        scriptCopy = script[:]
        # Set unique LLVM_PROFILE_FILE for each run command
        if litConfig.per_test_coverage:
            # Extract the test case name from the test object, and remove the
            # file extension.
            test_case_name = test.path_in_suite[-1]
            test_case_name = test_case_name.rsplit(".", 1)[0]
            coverage_index = 0  # Counter for coverage file index
            for i, ln in enumerate(scriptCopy):
                match = re.fullmatch(kPdbgRegex, ln)
                if match:
                    dbg = match.group(1)
                    command = match.group(2)
                else:
                    command = ln
                profile = f"{test_case_name}{coverage_index}.profraw"
                coverage_index += 1
                command = f"export LLVM_PROFILE_FILE={profile}; {command}"
                if match:
                    command = buildPdbgCommand(dbg, command)
                scriptCopy[i] = command

        try:
            if useExternalSh:
                res = executeScript(test, litConfig, tmpBase, scriptCopy, execdir)
            else:
                res = executeScriptInternal(
                    test, litConfig, tmpBase, scriptCopy, execdir
                )
        except ScriptFatal as e:
            out = f"# " + "\n# ".join(str(e).splitlines()) + "\n"
            return out, "", 1, None, Test.UNRESOLVED

        out, err, exitCode, timeoutInfo = res
        if exitCode == 0:
            status = Test.PASS
        else:
            if timeoutInfo is None:
                status = Test.FAIL
            else:
                status = Test.TIMEOUT
        return out, err, exitCode, timeoutInfo, status

    # Create the output directory if it does not already exist.
    lit.util.mkdir_p(os.path.dirname(tmpBase))

    # Re-run failed tests up to test.allowed_retries times.
    execdir = os.path.dirname(test.getExecPath())
    attempts = test.allowed_retries + 1
    for i in range(attempts):
        res = runOnce(execdir)
        out, err, exitCode, timeoutInfo, status = res
        if status != Test.FAIL:
            break

    # If we had to run the test more than once, count it as a flaky pass. These
    # will be printed separately in the test summary.
    if i > 0 and status == Test.PASS:
        status = Test.FLAKYPASS

    # Form the output log.
    output = f"Exit Code: {exitCode}\n"

    if timeoutInfo is not None:
        output += """Timeout: %s\n""" % (timeoutInfo,)
    output += "\n"

    # Append the outputs, if present.
    if out:
        output += """Command Output (stdout):\n--\n%s\n--\n""" % (out,)
    if err:
        output += """Command Output (stderr):\n--\n%s\n--\n""" % (err,)

    return lit.Test.Result(status, output)


def executeShTest(
    test, litConfig, useExternalSh, extra_substitutions=[], preamble_commands=[]
):
    if test.config.unsupported:
        return lit.Test.Result(Test.UNSUPPORTED, "Test is unsupported")

    script = list(preamble_commands)
    script = [buildPdbgCommand(f"preamble command line", ln) for ln in script]

    parsed = parseIntegratedTestScript(test, require_script=not script)
    if isinstance(parsed, lit.Test.Result):
        return parsed
    script += parsed

    if litConfig.noExecute:
        return lit.Test.Result(Test.PASS)

    tmpDir, tmpBase = getTempPaths(test)
    substitutions = list(extra_substitutions)
    substitutions += getDefaultSubstitutions(
        test, tmpDir, tmpBase, normalize_slashes=useExternalSh
    )
    conditions = {feature: True for feature in test.config.available_features}
    script = applySubstitutions(
        script,
        substitutions,
        conditions,
        recursion_limit=test.config.recursiveExpansionLimit,
    )

    return _runShTest(test, litConfig, useExternalSh, script, tmpBase)
