import sys
import os
import lldb


def check_has_dir_in_path(dirname):
    return sys.path.__contains__(dirname)


def ensure_has_dir_in_path(dirname):
    dirname = os.path.abspath(dirname)
    if not (check_has_dir_in_path(dirname)):
        sys.path.append(dirname)


def do_import(debugger, modname):
    if len(modname) > 4 and modname[-4:] == ".pyc":
        modname = modname[:-4]
    if len(modname) > 3 and modname[-3:] == ".py":
        modname = modname[:-3]
    debugger.HandleCommand("script import " + modname)


def pyimport_cmd(debugger, args, result, dict):
    """Import a Python module given its full path"""
    print('WARNING: obsolete feature - use native command "command script import"')
    if args == "":
        return "no module path given"
    if not (os.sep in args):
        modname = args
        ensure_has_dir_in_path(".")
    else:
        endofdir = args.rfind(os.sep)
        modname = args[endofdir + 1 :]
        args = args[0:endofdir]
        ensure_has_dir_in_path(args)
    do_import(debugger, modname)
    return None
