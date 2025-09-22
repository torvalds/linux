# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
""" This module is a collection of methods commonly used in this project. """
import collections
import functools
import json
import logging
import os
import os.path
import re
import shlex
import subprocess
import sys

ENVIRONMENT_KEY = "INTERCEPT_BUILD"

Execution = collections.namedtuple("Execution", ["pid", "cwd", "cmd"])

CtuConfig = collections.namedtuple(
    "CtuConfig", ["collect", "analyze", "dir", "extdef_map_cmd"]
)


def duplicate_check(method):
    """Predicate to detect duplicated entries.

    Unique hash method can be use to detect duplicates. Entries are
    represented as dictionaries, which has no default hash method.
    This implementation uses a set datatype to store the unique hash values.

    This method returns a method which can detect the duplicate values."""

    def predicate(entry):
        entry_hash = predicate.unique(entry)
        if entry_hash not in predicate.state:
            predicate.state.add(entry_hash)
            return False
        return True

    predicate.unique = method
    predicate.state = set()
    return predicate


def run_build(command, *args, **kwargs):
    """Run and report build command execution

    :param command: array of tokens
    :return: exit code of the process
    """
    environment = kwargs.get("env", os.environ)
    logging.debug("run build %s, in environment: %s", command, environment)
    exit_code = subprocess.call(command, *args, **kwargs)
    logging.debug("build finished with exit code: %d", exit_code)
    return exit_code


def run_command(command, cwd=None):
    """Run a given command and report the execution.

    :param command: array of tokens
    :param cwd: the working directory where the command will be executed
    :return: output of the command
    """

    def decode_when_needed(result):
        """check_output returns bytes or string depend on python version"""
        return result.decode("utf-8") if isinstance(result, bytes) else result

    try:
        directory = os.path.abspath(cwd) if cwd else os.getcwd()
        logging.debug("exec command %s in %s", command, directory)
        output = subprocess.check_output(
            command, cwd=directory, stderr=subprocess.STDOUT
        )
        return decode_when_needed(output).splitlines()
    except subprocess.CalledProcessError as ex:
        ex.output = decode_when_needed(ex.output).splitlines()
        raise ex


def reconfigure_logging(verbose_level):
    """Reconfigure logging level and format based on the verbose flag.

    :param verbose_level: number of `-v` flags received by the command
    :return: no return value
    """
    # Exit when nothing to do.
    if verbose_level == 0:
        return

    root = logging.getLogger()
    # Tune logging level.
    level = logging.WARNING - min(logging.WARNING, (10 * verbose_level))
    root.setLevel(level)
    # Be verbose with messages.
    if verbose_level <= 3:
        fmt_string = "%(name)s: %(levelname)s: %(message)s"
    else:
        fmt_string = "%(name)s: %(levelname)s: %(funcName)s: %(message)s"
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter(fmt=fmt_string))
    root.handlers = [handler]


def command_entry_point(function):
    """Decorator for command entry methods.

    The decorator initialize/shutdown logging and guard on programming
    errors (catch exceptions).

    The decorated method can have arbitrary parameters, the return value will
    be the exit code of the process."""

    @functools.wraps(function)
    def wrapper(*args, **kwargs):
        """Do housekeeping tasks and execute the wrapped method."""

        try:
            logging.basicConfig(
                format="%(name)s: %(message)s", level=logging.WARNING, stream=sys.stdout
            )
            # This hack to get the executable name as %(name).
            logging.getLogger().name = os.path.basename(sys.argv[0])
            return function(*args, **kwargs)
        except KeyboardInterrupt:
            logging.warning("Keyboard interrupt")
            return 130  # Signal received exit code for bash.
        except Exception:
            logging.exception("Internal error.")
            if logging.getLogger().isEnabledFor(logging.DEBUG):
                logging.error(
                    "Please report this bug and attach the output " "to the bug report"
                )
            else:
                logging.error(
                    "Please run this command again and turn on "
                    "verbose mode (add '-vvvv' as argument)."
                )
            return 64  # Some non used exit code for internal errors.
        finally:
            logging.shutdown()

    return wrapper


def compiler_wrapper(function):
    """Implements compiler wrapper base functionality.

    A compiler wrapper executes the real compiler, then implement some
    functionality, then returns with the real compiler exit code.

    :param function: the extra functionality what the wrapper want to
    do on top of the compiler call. If it throws exception, it will be
    caught and logged.
    :return: the exit code of the real compiler.

    The :param function: will receive the following arguments:

    :param result:       the exit code of the compilation.
    :param execution:    the command executed by the wrapper."""

    def is_cxx_compiler():
        """Find out was it a C++ compiler call. Compiler wrapper names
        contain the compiler type. C++ compiler wrappers ends with `c++`,
        but might have `.exe` extension on windows."""

        wrapper_command = os.path.basename(sys.argv[0])
        return re.match(r"(.+)c\+\+(.*)", wrapper_command)

    def run_compiler(executable):
        """Execute compilation with the real compiler."""

        command = executable + sys.argv[1:]
        logging.debug("compilation: %s", command)
        result = subprocess.call(command)
        logging.debug("compilation exit code: %d", result)
        return result

    # Get relevant parameters from environment.
    parameters = json.loads(os.environ[ENVIRONMENT_KEY])
    reconfigure_logging(parameters["verbose"])
    # Execute the requested compilation. Do crash if anything goes wrong.
    cxx = is_cxx_compiler()
    compiler = parameters["cxx"] if cxx else parameters["cc"]
    result = run_compiler(compiler)
    # Call the wrapped method and ignore it's return value.
    try:
        call = Execution(
            pid=os.getpid(),
            cwd=os.getcwd(),
            cmd=["c++" if cxx else "cc"] + sys.argv[1:],
        )
        function(result, call)
    except:
        logging.exception("Compiler wrapper failed complete.")
    finally:
        # Always return the real compiler exit code.
        return result


def wrapper_environment(args):
    """Set up environment for interpose compiler wrapper."""

    return {
        ENVIRONMENT_KEY: json.dumps(
            {
                "verbose": args.verbose,
                "cc": shlex.split(args.cc),
                "cxx": shlex.split(args.cxx),
            }
        )
    }
