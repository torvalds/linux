"""Utility for changing directories and execution of commands in a subshell."""

import os
import shlex
import subprocess

# Store the previous working directory for the 'cd -' command.


class Holder:
    """Holds the _prev_dir_ class attribute for chdir() function."""

    _prev_dir_ = None

    @classmethod
    def prev_dir(cls):
        return cls._prev_dir_

    @classmethod
    def swap(cls, dir):
        cls._prev_dir_ = dir


def chdir(debugger, args, result, dict):
    """Change the working directory, or cd to ${HOME}.
    You can also issue 'cd -' to change to the previous working directory."""
    new_dir = args.strip()
    if not new_dir:
        new_dir = os.path.expanduser("~")
    elif new_dir == "-":
        if not Holder.prev_dir():
            # Bad directory, not changing.
            print("bad directory, not changing")
            return
        else:
            new_dir = Holder.prev_dir()

    Holder.swap(os.getcwd())
    os.chdir(new_dir)
    print("Current working directory: %s" % os.getcwd())


def system(debugger, command_line, result, dict):
    """Execute the command (a string) in a subshell."""
    args = shlex.split(command_line)
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, error = process.communicate()
    retcode = process.poll()
    if output and error:
        print("stdout=>\n", output)
        print("stderr=>\n", error)
    elif output:
        print(output)
    elif error:
        print(error)
    print("retcode:", retcode)
