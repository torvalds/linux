""" Copies the build output of a custom python interpreter to a directory
    structure that mirrors that of an official Python distribution.

    --------------------------------------------------------------------------
    File:           install_custom_python.py

    Overview:       Most users build LLDB by linking against the standard
                    Python distribution installed on their system.  Occasionally
                    a user may want to build their own version of Python, and on
                    platforms such as Windows this is a hard requirement.  This
                    script will take the build output of a custom interpreter and
                    install it into a canonical structure that mirrors that of an
                    official Python distribution, thus allowing PYTHONHOME to be
                    set appropriately.

    Gotchas:        None.

    Copyright:      None.
    --------------------------------------------------------------------------

"""

import argparse
import itertools
import os
import shutil
import sys


def copy_one_file(dest_dir, source_dir, filename):
    source_path = os.path.join(source_dir, filename)
    dest_path = os.path.join(dest_dir, filename)
    print("Copying file %s ==> %s..." % (source_path, dest_path))
    shutil.copyfile(source_path, dest_path)


def copy_named_files(dest_dir, source_dir, files, extensions, copy_debug_suffix_also):
    for file, ext in itertools.product(files, extensions):
        copy_one_file(dest_dir, source_dir, file + "." + ext)
        if copy_debug_suffix_also:
            copy_one_file(dest_dir, source_dir, file + "_d." + ext)


def copy_subdirectory(dest_dir, source_dir, subdir):
    dest_dir = os.path.join(dest_dir, subdir)
    source_dir = os.path.join(source_dir, subdir)
    print("Copying directory %s ==> %s..." % (source_dir, dest_dir))
    shutil.copytree(source_dir, dest_dir)


def copy_distro(dest_dir, dest_subdir, source_dir, source_prefix):
    dest_dir = os.path.join(dest_dir, dest_subdir)

    print("Copying distribution %s ==> %s" % (source_dir, dest_dir))

    os.mkdir(dest_dir)
    PCbuild_dir = os.path.join(source_dir, "PCbuild")
    if source_prefix:
        PCbuild_dir = os.path.join(PCbuild_dir, source_prefix)
    # First copy the files that go into the root of the new distribution. This
    # includes the Python executables, python27(_d).dll, and relevant PDB
    # files.
    print("Copying Python executables...")
    copy_named_files(dest_dir, PCbuild_dir, ["w9xpopen"], ["exe", "pdb"], False)
    copy_named_files(dest_dir, PCbuild_dir, ["python_d", "pythonw_d"], ["exe"], False)
    copy_named_files(
        dest_dir, PCbuild_dir, ["python", "pythonw"], ["exe", "pdb"], False
    )
    copy_named_files(dest_dir, PCbuild_dir, ["python27"], ["dll", "pdb"], True)

    # Next copy everything in the Include directory.
    print("Copying Python include directory")
    copy_subdirectory(dest_dir, source_dir, "Include")

    # Copy Lib folder (builtin Python modules)
    print("Copying Python Lib directory")
    copy_subdirectory(dest_dir, source_dir, "Lib")

    # Copy tools folder.  These are probably not necessary, but we copy them anyway to
    # match an official distribution as closely as possible.  Note that we don't just copy
    # the subdirectory recursively.  The source distribution ships with many more tools
    # than what you get by installing python regularly.  We only copy the tools that appear
    # in an installed distribution.
    tools_dest_dir = os.path.join(dest_dir, "Tools")
    tools_source_dir = os.path.join(source_dir, "Tools")
    os.mkdir(tools_dest_dir)
    copy_subdirectory(tools_dest_dir, tools_source_dir, "i18n")
    copy_subdirectory(tools_dest_dir, tools_source_dir, "pynche")
    copy_subdirectory(tools_dest_dir, tools_source_dir, "scripts")
    copy_subdirectory(tools_dest_dir, tools_source_dir, "versioncheck")
    copy_subdirectory(tools_dest_dir, tools_source_dir, "webchecker")

    pyd_names = [
        "_ctypes",
        "_ctypes_test",
        "_elementtree",
        "_multiprocessing",
        "_socket",
        "_testcapi",
        "pyexpat",
        "select",
        "unicodedata",
        "winsound",
    ]

    # Copy builtin extension modules (pyd files)
    dlls_dir = os.path.join(dest_dir, "DLLs")
    os.mkdir(dlls_dir)
    print("Copying DLLs directory")
    copy_named_files(dlls_dir, PCbuild_dir, pyd_names, ["pyd", "pdb"], True)

    # Copy libs folder (implibs for the pyd files)
    libs_dir = os.path.join(dest_dir, "libs")
    os.mkdir(libs_dir)
    print("Copying libs directory")
    copy_named_files(libs_dir, PCbuild_dir, pyd_names, ["lib"], False)
    copy_named_files(libs_dir, PCbuild_dir, ["python27"], ["lib"], True)


parser = argparse.ArgumentParser(description="Install a custom Python distribution")
parser.add_argument(
    "--source", required=True, help="The root of the source tree where Python is built."
)
parser.add_argument(
    "--dest", required=True, help="The location to install the Python distributions."
)
parser.add_argument(
    "--overwrite",
    default=False,
    action="store_true",
    help="If the destination directory already exists, destroys its contents first.",
)
parser.add_argument(
    "--silent",
    default=False,
    action="store_true",
    help="If --overwite was specified, suppress confirmation before deleting a directory tree.",
)

args = parser.parse_args()

args.source = os.path.normpath(args.source)
args.dest = os.path.normpath(args.dest)

if not os.path.exists(args.source):
    print("The source directory %s does not exist.  Exiting...")
    sys.exit(1)

if os.path.exists(args.dest):
    if not args.overwrite:
        print(
            "The destination directory '%s' already exists and --overwrite was not specified.  Exiting..."
            % args.dest
        )
        sys.exit(1)
    while not args.silent:
        print(
            "Ok to recursively delete '%s' and all contents (Y/N)?  Choosing Y will permanently delete the contents."
            % args.dest
        )
        result = str.upper(sys.stdin.read(1))
        if result == "N":
            print(
                "Unable to copy files to the destination.  The destination already exists."
            )
            sys.exit(1)
        elif result == "Y":
            break
    shutil.rmtree(args.dest)

os.mkdir(args.dest)
copy_distro(args.dest, "x86", args.source, None)
copy_distro(args.dest, "x64", args.source, "amd64")
