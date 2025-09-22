from __future__ import print_function

import errno
import itertools
import math
import numbers
import os
import platform
import re
import signal
import subprocess
import sys
import threading


def is_string(value):
    try:
        # Python 2 and Python 3 are different here.
        return isinstance(value, basestring)
    except NameError:
        return isinstance(value, str)


def pythonize_bool(value):
    if value is None:
        return False
    if type(value) is bool:
        return value
    if isinstance(value, numbers.Number):
        return value != 0
    if is_string(value):
        if value.lower() in ("1", "true", "on", "yes"):
            return True
        if value.lower() in ("", "0", "false", "off", "no"):
            return False
    raise ValueError('"{}" is not a valid boolean'.format(value))


def make_word_regex(word):
    return r"\b" + word + r"\b"


def to_bytes(s):
    """Return the parameter as type 'bytes', possibly encoding it.

    In Python2, the 'bytes' type is the same as 'str'. In Python3, they
    are distinct.

    """
    if isinstance(s, bytes):
        # In Python2, this branch is taken for both 'str' and 'bytes'.
        # In Python3, this branch is taken only for 'bytes'.
        return s
    # In Python2, 's' is a 'unicode' object.
    # In Python3, 's' is a 'str' object.
    # Encode to UTF-8 to get 'bytes' data.
    return s.encode("utf-8")


def to_string(b):
    """Return the parameter as type 'str', possibly encoding it.

    In Python2, the 'str' type is the same as 'bytes'. In Python3, the
    'str' type is (essentially) Python2's 'unicode' type, and 'bytes' is
    distinct.

    """
    if isinstance(b, str):
        # In Python2, this branch is taken for types 'str' and 'bytes'.
        # In Python3, this branch is taken only for 'str'.
        return b
    if isinstance(b, bytes):
        # In Python2, this branch is never taken ('bytes' is handled as 'str').
        # In Python3, this is true only for 'bytes'.
        try:
            return b.decode("utf-8")
        except UnicodeDecodeError:
            # If the value is not valid Unicode, return the default
            # repr-line encoding.
            return str(b)

    # By this point, here's what we *don't* have:
    #
    #  - In Python2:
    #    - 'str' or 'bytes' (1st branch above)
    #  - In Python3:
    #    - 'str' (1st branch above)
    #    - 'bytes' (2nd branch above)
    #
    # The last type we might expect is the Python2 'unicode' type. There is no
    # 'unicode' type in Python3 (all the Python3 cases were already handled). In
    # order to get a 'str' object, we need to encode the 'unicode' object.
    try:
        return b.encode("utf-8")
    except AttributeError:
        raise TypeError("not sure how to convert %s to %s" % (type(b), str))


def to_unicode(s):
    """Return the parameter as type which supports unicode, possibly decoding
    it.

    In Python2, this is the unicode type. In Python3 it's the str type.

    """
    if isinstance(s, bytes):
        # In Python2, this branch is taken for both 'str' and 'bytes'.
        # In Python3, this branch is taken only for 'bytes'.
        return s.decode("utf-8")
    return s


def usable_core_count():
    """Return the number of cores the current process can use, if supported.
    Otherwise, return the total number of cores (like `os.cpu_count()`).
    Default to 1 if undetermined.

    """
    try:
        n = len(os.sched_getaffinity(0))
    except AttributeError:
        n = os.cpu_count() or 1

    # On Windows with more than 60 processes, multiprocessing's call to
    # _winapi.WaitForMultipleObjects() prints an error and lit hangs.
    if platform.system() == "Windows":
        return min(n, 60)

    return n

def abs_path_preserve_drive(path):
    """Return the absolute path without resolving drive mappings on Windows.

    """
    if platform.system() == "Windows":
        # Windows has limitations on path length (MAX_PATH) that
        # can be worked around using substitute drives, which map
        # a drive letter to a longer path on another drive.
        # Since Python 3.8, os.path.realpath resolves sustitute drives,
        # so we should not use it. In Python 3.7, os.path.realpath
        # was implemented as os.path.abspath.
        return os.path.abspath(path)
    else:
        # On UNIX, the current directory always has symbolic links resolved,
        # so any program accepting relative paths cannot preserve symbolic
        # links in paths and we should always use os.path.realpath.
        return os.path.realpath(path)

def mkdir(path):
    try:
        if platform.system() == "Windows":
            from ctypes import windll
            from ctypes import GetLastError, WinError

            path = os.path.abspath(path)
            # Make sure that the path uses backslashes here, in case
            # python would have happened to use forward slashes, as the
            # NT path format only supports backslashes.
            path = path.replace("/", "\\")
            NTPath = to_unicode(r"\\?\%s" % path)
            if not windll.kernel32.CreateDirectoryW(NTPath, None):
                raise WinError(GetLastError())
        else:
            os.mkdir(path)
    except OSError:
        e = sys.exc_info()[1]
        # ignore EEXIST, which may occur during a race condition
        if e.errno != errno.EEXIST:
            raise


def mkdir_p(path):
    """mkdir_p(path) - Make the "path" directory, if it does not exist; this
    will also make directories for any missing parent directories."""
    if not path or os.path.exists(path):
        return

    parent = os.path.dirname(path)
    if parent != path:
        mkdir_p(parent)

    mkdir(path)


def listdir_files(dirname, suffixes=None, exclude_filenames=None):
    """Yields files in a directory.

    Filenames that are not excluded by rules below are yielded one at a time, as
    basenames (i.e., without dirname).

    Files starting with '.' are always skipped.

    If 'suffixes' is not None, then only filenames ending with one of its
    members will be yielded. These can be extensions, like '.exe', or strings,
    like 'Test'. (It is a lexicographic check; so an empty sequence will yield
    nothing, but a single empty string will yield all filenames.)

    If 'exclude_filenames' is not None, then none of the file basenames in it
    will be yielded.

    If specified, the containers for 'suffixes' and 'exclude_filenames' must
    support membership checking for strs.

    Args:
        dirname: a directory path.
        suffixes: (optional) a sequence of strings (set, list, etc.).
        exclude_filenames: (optional) a sequence of strings.

    Yields:
        Filenames as returned by os.listdir (generally, str).

    """
    if exclude_filenames is None:
        exclude_filenames = set()
    if suffixes is None:
        suffixes = {""}
    for filename in os.listdir(dirname):
        if (
            os.path.isdir(os.path.join(dirname, filename))
            or filename.startswith(".")
            or filename in exclude_filenames
            or not any(filename.endswith(sfx) for sfx in suffixes)
        ):
            continue
        yield filename


def which(command, paths=None):
    """which(command, [paths]) - Look up the given command in the paths string
    (or the PATH environment variable, if unspecified)."""

    if paths is None:
        paths = os.environ.get("PATH", "")

    # Check for absolute match first.
    if os.path.isabs(command) and os.path.isfile(command):
        return os.path.normcase(os.path.normpath(command))

    # Would be nice if Python had a lib function for this.
    if not paths:
        paths = os.defpath

    # Get suffixes to search.
    # On Cygwin, 'PATHEXT' may exist but it should not be used.
    if os.pathsep == ";":
        pathext = os.environ.get("PATHEXT", "").split(";")
    else:
        pathext = [""]

    # Search the paths...
    for path in paths.split(os.pathsep):
        for ext in pathext:
            p = os.path.join(path, command + ext)
            if os.path.exists(p) and not os.path.isdir(p):
                return os.path.normcase(os.path.abspath(p))

    return None


def checkToolsPath(dir, tools):
    for tool in tools:
        if not os.path.exists(os.path.join(dir, tool)):
            return False
    return True


def whichTools(tools, paths):
    for path in paths.split(os.pathsep):
        if checkToolsPath(path, tools):
            return path
    return None


def printHistogram(items, title="Items"):
    items.sort(key=lambda item: item[1])

    maxValue = max([v for _, v in items])

    # Select first "nice" bar height that produces more than 10 bars.
    power = int(math.ceil(math.log(maxValue, 10)))
    for inc in itertools.cycle((5, 2, 2.5, 1)):
        barH = inc * 10**power
        N = int(math.ceil(maxValue / barH))
        if N > 10:
            break
        elif inc == 1:
            power -= 1

    histo = [set() for i in range(N)]
    for name, v in items:
        bin = min(int(N * v / maxValue), N - 1)
        histo[bin].add(name)

    barW = 40
    hr = "-" * (barW + 34)
    print("Slowest %s:" % title)
    print(hr)
    for name, value in reversed(items[-20:]):
        print("%.2fs: %s" % (value, name))
    print("\n%s Times:" % title)
    print(hr)
    pDigits = int(math.ceil(math.log(maxValue, 10)))
    pfDigits = max(0, 3 - pDigits)
    if pfDigits:
        pDigits += pfDigits + 1
    cDigits = int(math.ceil(math.log(len(items), 10)))
    print(
        "[%s] :: [%s] :: [%s]"
        % (
            "Range".center((pDigits + 1) * 2 + 3),
            "Percentage".center(barW),
            "Count".center(cDigits * 2 + 1),
        )
    )
    print(hr)
    for i, row in reversed(list(enumerate(histo))):
        pct = float(len(row)) / len(items)
        w = int(barW * pct)
        print(
            "[%*.*fs,%*.*fs) :: [%s%s] :: [%*d/%*d]"
            % (
                pDigits,
                pfDigits,
                i * barH,
                pDigits,
                pfDigits,
                (i + 1) * barH,
                "*" * w,
                " " * (barW - w),
                cDigits,
                len(row),
                cDigits,
                len(items),
            )
        )
    print(hr)


class ExecuteCommandTimeoutException(Exception):
    def __init__(self, msg, out, err, exitCode):
        assert isinstance(msg, str)
        assert isinstance(out, str)
        assert isinstance(err, str)
        assert isinstance(exitCode, int)
        self.msg = msg
        self.out = out
        self.err = err
        self.exitCode = exitCode


# Close extra file handles on UNIX (on Windows this cannot be done while
# also redirecting input).
kUseCloseFDs = not (platform.system() == "Windows")


def executeCommand(
    command, cwd=None, env=None, input=None, timeout=0, redirect_stderr=False
):
    """Execute command ``command`` (list of arguments or string) with.

    * working directory ``cwd`` (str), use None to use the current
      working directory
    * environment ``env`` (dict), use None for none
    * Input to the command ``input`` (str), use string to pass
      no input.
    * Max execution time ``timeout`` (int) seconds. Use 0 for no timeout.
    * ``redirect_stderr`` (bool), use True if redirect stderr to stdout

    Returns a tuple (out, err, exitCode) where
    * ``out`` (str) is the standard output of running the command
    * ``err`` (str) is the standard error of running the command
    * ``exitCode`` (int) is the exitCode of running the command

    If the timeout is hit an ``ExecuteCommandTimeoutException``
    is raised.

    """
    if input is not None:
        input = to_bytes(input)
    err_out = subprocess.STDOUT if redirect_stderr else subprocess.PIPE
    p = subprocess.Popen(
        command,
        cwd=cwd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=err_out,
        env=env,
        close_fds=kUseCloseFDs,
    )
    timerObject = None
    # FIXME: Because of the way nested function scopes work in Python 2.x we
    # need to use a reference to a mutable object rather than a plain
    # bool. In Python 3 we could use the "nonlocal" keyword but we need
    # to support Python 2 as well.
    hitTimeOut = [False]
    try:
        if timeout > 0:

            def killProcess():
                # We may be invoking a shell so we need to kill the
                # process and all its children.
                hitTimeOut[0] = True
                killProcessAndChildren(p.pid)

            timerObject = threading.Timer(timeout, killProcess)
            timerObject.start()

        out, err = p.communicate(input=input)
        exitCode = p.wait()
    finally:
        if timerObject != None:
            timerObject.cancel()

    # Ensure the resulting output is always of string type.
    out = to_string(out)
    err = "" if redirect_stderr else to_string(err)

    if hitTimeOut[0]:
        raise ExecuteCommandTimeoutException(
            msg="Reached timeout of {} seconds".format(timeout),
            out=out,
            err=err,
            exitCode=exitCode,
        )

    # Detect Ctrl-C in subprocess.
    if exitCode == -signal.SIGINT:
        raise KeyboardInterrupt

    return out, err, exitCode


def isAIXTriple(target_triple):
    """Whether the given target triple is for AIX,
    e.g. powerpc64-ibm-aix
    """
    return "aix" in target_triple


def addAIXVersion(target_triple):
    """Add the AIX version to the given target triple,
    e.g. powerpc64-ibm-aix7.2.5.6
    """
    os_cmd = "oslevel -s | awk -F\'-\' \'{printf \"%.1f.%d.%d\", $1/1000, $2, $3}\'"
    os_version = subprocess.run(os_cmd, capture_output=True, shell=True).stdout.decode()
    return re.sub("aix", "aix" + os_version, target_triple)


def isMacOSTriple(target_triple):
    """Whether the given target triple is for macOS,
    e.g. x86_64-apple-darwin, arm64-apple-macos
    """
    return "darwin" in target_triple or "macos" in target_triple


def usePlatformSdkOnDarwin(config, lit_config):
    # On Darwin, support relocatable SDKs by providing Clang with a
    # default system root path.
    if isMacOSTriple(config.target_triple):
        try:
            cmd = subprocess.Popen(
                ["xcrun", "--show-sdk-path", "--sdk", "macosx"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = cmd.communicate()
            out = out.strip()
            res = cmd.wait()
        except OSError:
            res = -1
        if res == 0 and out:
            sdk_path = out.decode()
            lit_config.note("using SDKROOT: %r" % sdk_path)
            config.environment["SDKROOT"] = sdk_path


def findPlatformSdkVersionOnMacOS(config, lit_config):
    if isMacOSTriple(config.target_triple):
        try:
            cmd = subprocess.Popen(
                ["xcrun", "--show-sdk-version", "--sdk", "macosx"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = cmd.communicate()
            out = out.strip()
            res = cmd.wait()
        except OSError:
            res = -1
        if res == 0 and out:
            return out.decode()
    return None


def killProcessAndChildrenIsSupported():
    """
    Returns a tuple (<supported> , <error message>)
    where
    `<supported>` is True if `killProcessAndChildren()` is supported on
        the current host, returns False otherwise.
    `<error message>` is an empty string if `<supported>` is True,
        otherwise is contains a string describing why the function is
        not supported.
    """
    if platform.system() == "AIX":
        return (True, "")
    try:
        import psutil  # noqa: F401

        return (True, "")
    except ImportError:
        return (
            False,
            "Requires the Python psutil module but it could"
            " not be found. Try installing it via pip or via"
            " your operating system's package manager.",
        )


def killProcessAndChildren(pid):
    """This function kills a process with ``pid`` and all its running children
    (recursively). It is currently implemented using the psutil module on some
    platforms which provides a simple platform neutral implementation.

    TODO: Reimplement this without using psutil on all platforms so we can
    remove our dependency on it.

    """
    if platform.system() == "AIX":
        subprocess.call("kill -kill $(ps -o pid= -L{})".format(pid), shell=True)
    else:
        import psutil

        try:
            psutilProc = psutil.Process(pid)
            # Handle the different psutil API versions
            try:
                # psutil >= 2.x
                children_iterator = psutilProc.children(recursive=True)
            except AttributeError:
                # psutil 1.x
                children_iterator = psutilProc.get_children(recursive=True)
            for child in children_iterator:
                try:
                    child.kill()
                except psutil.NoSuchProcess:
                    pass
            psutilProc.kill()
        except psutil.NoSuchProcess:
            pass
