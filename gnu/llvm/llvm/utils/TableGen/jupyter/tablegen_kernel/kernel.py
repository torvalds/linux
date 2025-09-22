# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import os
import shutil
import subprocess
import tempfile
from ipykernel.kernelbase import Kernel

__version__ = "0.0.1"


class TableGenKernelException(Exception):
    pass


class TableGenKernel(Kernel):
    """Kernel using llvm-tblgen inside jupyter.

    All input is treated as TableGen unless the first non whitespace character
    is "%" in which case it is a "magic" line.

    The supported cell magic is:
    * %args    - to set the arguments passed to llvm-tblgen.
    * %reset   - to reset the cached code and magic state.
    * %noreset - to not reset the cached code and magic state
                 (useful when you have changed the default to always
                  reset the cache).

    These are "cell magic" meaning it applies to the whole cell. Therefore
    it must be the first line, or part of a run of magic lines starting
    from the first line.

    The following are global magic (that applies to all cells going
    forward):
    * %config  - to change the behaviour of the kernel overall, including
                 changing defaults for things like resets.

    Global magic must be written in the same way as cell magic.

    ```tablgen
    %args
    %reset
    %args --print-records --print-detailed-records
    class Stuff {
      string Name;
    }

    def a_thing : Stuff {}
    ```

    """

    implementation = "tablegen"
    implementation_version = __version__

    language_version = __version__
    language = "tablegen"
    language_info = {
        "name": "tablegen",
        "mimetype": "text/x-tablegen",
        "file_extension": ".td",
        "pygments_lexer": "text",
    }

    def __init__(self, **kwargs):
        Kernel.__init__(self, **kwargs)
        self._executable = None
        # A list of (code, magic) tuples.
        # All the previous cell's code we have run since the last reset.
        # This emulates a persistent state like a Python interpreter would have.
        self._previous_code = ""
        # The most recent set of magic since the last reset.
        self._previous_magic = {}
        # The default cache reset behaviour. True means do not cache anything
        # between cells.
        self._cell_reset = False

    @property
    def banner(self):
        return "llvm-tblgen kernel %s" % __version__

    def get_executable(self):
        """If this is the first run, search for llvm-tblgen.
        Otherwise return the cached path to it."""
        if self._executable is None:
            path = os.environ.get("LLVM_TBLGEN_EXECUTABLE")
            if path is not None and os.path.isfile(path) and os.access(path, os.X_OK):
                self._executable = path
            else:
                path = shutil.which("llvm-tblgen")
                if path is None:
                    raise OSError(
                        "llvm-tblgen not found. Put it on your PATH or set the"
                        " environment variable LLVM_TBLGEN_EXECUTABLE to point to it."
                    )
                self._executable = path

        return self._executable

    def parse_config_magic(self, config):
        """Config should be a list of parameters given to the %config command.
        We allow only one setting per %config line and that setting can only
        have one value.

        Assuming the parameters are valid, update the kernel's setting with
        the new value.

        If there is an error, raise a TableGenKernelException.

        >>> k.parse_config_magic([])
        Traceback (most recent call last):
         ...
        TableGenKernelException: Incorrect number of parameters to %config. Expected %config <setting> <value>.
        >>> k._cell_reset
        False
        >>> k.parse_config_magic(["a", "b", "c"])
        Traceback (most recent call last):
         ...
        TableGenKernelException: Incorrect number of parameters to %config. Expected %config <setting> <value>.
        >>> k.parse_config_magic(["notasetting", "..."])
        Traceback (most recent call last):
         ...
        TableGenKernelException: Unknown kernel setting "notasetting". Possible settings are: "cellreset".
        >>> k.parse_config_magic(["cellreset", "food"])
        Traceback (most recent call last):
         ...
        TableGenKernelException: Invalid value for setting "cellreset", expected "on" or "off".
        >>> k.parse_config_magic(["cellreset", "on"])
        >>> k._cell_reset
        True
        >>> k.parse_config_magic(["cellreset", "off"])
        >>> k._cell_reset
        False
        """
        if len(config) != 2:
            raise TableGenKernelException(
                "Incorrect number of parameters to %config. Expected %config <setting> <value>."
            )

        name, value = config
        if name != "cellreset":
            raise TableGenKernelException(
                'Unknown kernel setting "{}". '
                'Possible settings are: "cellreset".'.format(name)
            )

        try:
            self._cell_reset = {"on": True, "off": False}[value.lower()]
        except KeyError:
            raise TableGenKernelException(
                'Invalid value for setting "{}", '
                'expected "on" or "off".'.format(name)
            )

    def get_magic(self, code):
        """Given a block of code remove the magic lines from it.
        Returns a tuple of newline joined code lines and a dictionary of magic.
        Where the key is the magic name (minus the %) and the values are lists
        of the arguments to the magic.

        Currently we only look for "cell magic" which must be at the start of
        the cell. Meaning the first line, or a set of lines beginning with %
        that come before the first non-magic line.

        >>> k.get_magic("")
        ('', {})
        >>> k.get_magic("Hello World.\\nHello again.")
        ('Hello World.\\nHello again.', {})
        >>> k.get_magic("   %foo a b c")
        ('', {'foo': ['a', 'b', 'c']})
        >>> k.get_magic("%foo\\n   %foo a b c\\nFoo")
        ('Foo', {'foo': ['a', 'b', 'c']})
        >>> k.get_magic("%foo\\n%bar\\nFoo")
        ('Foo', {'foo': [], 'bar': []})
        >>> k.get_magic("Foo\\n%foo\\nFoo")
        ('Foo\\n%foo\\nFoo', {})
        >>> k.get_magic("%bar\\n\\n%foo")
        ('\\n%foo', {'bar': []})
        >>> k.get_magic("%foo a b\\n   Foo\\n%foo c d")
        ('   Foo\\n%foo c d', {'foo': ['a', 'b']})
        >>> k.get_magic("%foo a b\\n \\n%foo c d")
        (' \\n%foo c d', {'foo': ['a', 'b']})
        """
        magic = {}
        code_lines = []

        lines = code.splitlines()
        while lines:
            line = lines.pop(0)
            possible_magic = line.lstrip()
            if possible_magic.startswith("%"):
                magic_parts = possible_magic.split()
                # Key has the % removed
                magic[magic_parts[0][1:]] = magic_parts[1:]
            else:
                code_lines = [line, *lines]
                break

        return "\n".join(code_lines), magic

    def should_reset(self, magic):
        """Return true if we should reset the cache, based on the default
        setting and the current cell's magic %reset and/or %noreset.

        >>> k._cell_reset = False
        >>> k.should_reset({})
        False
        >>> k.should_reset({'reset': [], 'noreset': []})
        Traceback (most recent call last):
        ...
        TableGenKernelException: %reset and %noreset in the same cell is not allowed. Use only one, or neither.
        >>> k.should_reset({'reset': []})
        True
        >>> k.should_reset({'noreset': []})
        False
        >>> k._cell_reset = True
        >>> k.should_reset({})
        True
        >>> k.should_reset({'reset': [], 'noreset': []})
        Traceback (most recent call last):
        ...
        TableGenKernelException: %reset and %noreset in the same cell is not allowed. Use only one, or neither.
        >>> k.should_reset({'reset': []})
        True
        >>> k.should_reset({'noreset': []})
        False
        """
        # Cell reset is the default unless told otherwise.
        should_reset = self._cell_reset
        # Magic reset commands always win if present.
        reset = magic.get("reset") is not None
        noreset = magic.get("noreset") is not None

        if reset and not noreset:
            should_reset = True
        elif noreset and not reset:
            should_reset = False
        elif noreset and reset:
            raise TableGenKernelException(
                "%reset and %noreset in the same cell is not allowed. Use only one, or neither."
            )
        # else neither are set so use the default.

        return should_reset

    def get_code_and_args(self, new_code):
        """Get the code that do_execute should use, taking into account
        the code from any cached cells.

        Returns the code to compile and the arguments to use to do so.

        >>> k._previous_code = ""
        >>> k._previous_magic = {}
        >>> k.get_code_and_args("")
        ('', [])
        >>> k.get_code_and_args("%args 1\\nSome code")
        ('Some code', ['1'])
        >>> k.get_code_and_args("%args 2\\nSome more code")
        ('Some code\\nSome more code', ['2'])
        >>> k.get_code_and_args("%reset\\n%args 3 4\\nSome new code")
        ('Some new code', ['3', '4'])
        >>> k.get_code_and_args("%reset\\nSome new code")
        ('Some new code', [])
        """
        new_code, new_magic = self.get_magic(new_code)

        # Update kernel configuration first, if needed.
        config_magic = new_magic.get("config")
        if config_magic is not None:
            self.parse_config_magic(config_magic)

        if self.should_reset(new_magic):
            self._previous_code = new_code
            self._previous_magic = new_magic
        else:
            self._previous_code += ("\n" if self._previous_code else "") + new_code
            self._previous_magic.update(new_magic)

        return self._previous_code, self._previous_magic.get("args", [])

    def make_status(self):
        return {
            "status": "ok",
            "execution_count": self.execution_count,
            "payload": [],
            "user_expressions": {},
        }

    def send_stream(self, name, content):
        self.send_response(self.iopub_socket, "stream", {"name": name, "text": content})

        return self.make_status()

    def send_stderr(self, stderr):
        return self.send_stream("stderr", stderr)

    def send_stdout(self, stdout):
        return self.send_stream("stdout", stdout)

    def do_execute(
        self, code, silent, store_history=True, user_expressions=None, allow_stdin=False
    ):
        """Execute user code using llvm-tblgen binary."""
        try:
            all_code, args = self.get_code_and_args(code)
        except TableGenKernelException as e:
            return self.send_stderr(str(e))

        # If we cannot find llvm-tblgen, propogate the error to the notebook.
        # (in case the user is not able to see the output from the Jupyter server)
        try:
            executable = self.get_executable()
        except Exception as e:
            return self.send_stderr(str(e))

        with tempfile.TemporaryFile("w+") as f:
            f.write(all_code)
            f.seek(0)
            got = subprocess.run(
                [executable, *args],
                stdin=f,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                universal_newlines=True,
            )

        if not silent:
            if got.stderr:
                return self.send_stderr(got.stderr)
            else:
                return self.send_stdout(got.stdout)
        else:
            return self.make_status()


if __name__ == "__main__":
    import doctest

    doctest.testmod(extraglobs={"k": TableGenKernel()})
