"""
Minimal clang-rename integration with Vim.

Before installing make sure one of the following is satisfied:

* clang-rename is in your PATH
* `g:clang_rename_path` in ~/.vimrc points to valid clang-rename executable
* `binary` in clang-rename.py points to valid to clang-rename executable

To install, simply put this into your ~/.vimrc for python2 support

    noremap <leader>cr :pyf <path-to>/clang-rename.py<cr>

For python3 use the following command (note the change from :pyf to :py3f)

    noremap <leader>cr :py3f <path-to>/clang-rename.py<cr>

IMPORTANT NOTE: Before running the tool, make sure you saved the file.

All you have to do now is to place a cursor on a variable/function/class which
you would like to rename and press '<leader>cr'. You will be prompted for a new
name if the cursor points to a valid symbol.
"""

from __future__ import absolute_import, division, print_function
import vim
import subprocess
import sys


def main():
    binary = "clang-rename"
    if vim.eval('exists("g:clang_rename_path")') == "1":
        binary = vim.eval("g:clang_rename_path")

    # Get arguments for clang-rename binary.
    offset = int(vim.eval('line2byte(line("."))+col(".")')) - 2
    if offset < 0:
        print(
            "Couldn't determine cursor position. Is your file empty?", file=sys.stderr
        )
        return
    filename = vim.current.buffer.name

    new_name_request_message = "type new name:"
    new_name = vim.eval("input('{}\n')".format(new_name_request_message))

    # Call clang-rename.
    command = [
        binary,
        filename,
        "-i",
        "-offset",
        str(offset),
        "-new-name",
        str(new_name),
    ]
    # FIXME: make it possible to run the tool on unsaved file.
    p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()

    if stderr:
        print(stderr)

    # Reload all buffers in Vim.
    vim.command("checktime")


if __name__ == "__main__":
    main()
