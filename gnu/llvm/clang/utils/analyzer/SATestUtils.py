import os
import sys
import time

from subprocess import CalledProcessError, check_call
from typing import List, IO, Optional, Tuple


def which(command: str, paths: Optional[str] = None) -> Optional[str]:
    """which(command, [paths]) - Look up the given command in the paths string
    (or the PATH environment variable, if unspecified)."""

    if paths is None:
        paths = os.environ.get("PATH", "")

    # Check for absolute match first.
    if os.path.exists(command):
        return command

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
            if os.path.exists(p):
                return p

    return None


def has_no_extension(file_name: str) -> bool:
    root, ext = os.path.splitext(file_name)
    return ext == ""


def is_valid_single_input_file(file_name: str) -> bool:
    root, ext = os.path.splitext(file_name)
    return ext in (".i", ".ii", ".c", ".cpp", ".m", "")


def time_to_str(time: float) -> str:
    """
    Convert given time in seconds into a human-readable string.
    """
    return f"{time:.2f}s"


def memory_to_str(memory: int) -> str:
    """
    Convert given number of bytes into a human-readable string.
    """
    if memory:
        try:
            import humanize

            return humanize.naturalsize(memory, gnu=True)
        except ImportError:
            # no formatter installed, let's keep it in bytes
            return f"{memory}B"

    # If memory is 0, we didn't succeed measuring it.
    return "N/A"


def check_and_measure_call(*popenargs, **kwargs) -> Tuple[float, int]:
    """
    Run command with arguments.  Wait for command to complete and measure
    execution time and peak memory consumption.
    If the exit code was zero then return, otherwise raise
    CalledProcessError.  The CalledProcessError object will have the
    return code in the returncode attribute.

    The arguments are the same as for the call and check_call functions.

    Return a tuple of execution time and peak memory.
    """
    peak_mem = 0
    start_time = time.time()

    try:
        import psutil as ps

        def get_memory(process: ps.Process) -> int:
            mem = 0

            # we want to gather memory usage from all of the child processes
            descendants = list(process.children(recursive=True))
            descendants.append(process)

            for subprocess in descendants:
                try:
                    mem += subprocess.memory_info().rss
                except (ps.NoSuchProcess, ps.AccessDenied):
                    continue

            return mem

        with ps.Popen(*popenargs, **kwargs) as process:
            # while the process is running calculate resource utilization.
            while process.is_running() and process.status() != ps.STATUS_ZOMBIE:
                # track the peak utilization of the process
                peak_mem = max(peak_mem, get_memory(process))
                time.sleep(0.5)

            if process.is_running():
                process.kill()

        if process.returncode != 0:
            cmd = kwargs.get("args")
            if cmd is None:
                cmd = popenargs[0]
            raise CalledProcessError(process.returncode, cmd)

    except ImportError:
        # back off to subprocess if we don't have psutil installed
        peak_mem = 0
        check_call(*popenargs, **kwargs)

    return time.time() - start_time, peak_mem


def run_script(
    script_path: str,
    build_log_file: IO,
    cwd: str,
    out=sys.stdout,
    err=sys.stderr,
    verbose: int = 0,
):
    """
    Run the provided script if it exists.
    """
    if os.path.exists(script_path):
        try:
            if verbose == 1:
                out.write(f"  Executing: {script_path}\n")

            check_call(
                f"chmod +x '{script_path}'",
                cwd=cwd,
                stderr=build_log_file,
                stdout=build_log_file,
                shell=True,
            )

            check_call(
                f"'{script_path}'",
                cwd=cwd,
                stderr=build_log_file,
                stdout=build_log_file,
                shell=True,
            )

        except CalledProcessError:
            err.write(
                f"Error: Running {script_path} failed. "
                f"See {build_log_file.name} for details.\n"
            )
            sys.exit(-1)


def is_comment_csv_line(entries: List[str]) -> bool:
    """
    Treat CSV lines starting with a '#' as a comment.
    """
    return len(entries) > 0 and entries[0].startswith("#")
