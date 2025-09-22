import sys

if sys.version_info[0] < 3:
    import __builtin__ as builtins
else:
    import builtins
import code
import lldb
import traceback

try:
    import readline
    import rlcompleter
except ImportError:
    have_readline = False
except AttributeError:
    # This exception gets hit by the rlcompleter when Linux is using
    # the readline suppression import.
    have_readline = False
else:
    have_readline = True
    if "libedit" in readline.__doc__:
        readline.parse_and_bind("bind ^I rl_complete")
    else:
        readline.parse_and_bind("tab: complete")

# When running one line, we might place the string to run in this string
# in case it would be hard to correctly escape a string's contents

g_run_one_line_str = None


def get_terminal_size(fd):
    try:
        import fcntl
        import termios
        import struct

        hw = struct.unpack("hh", fcntl.ioctl(fd, termios.TIOCGWINSZ, "1234"))
    except:
        hw = (0, 0)
    return hw


class LLDBExit(SystemExit):
    pass


def strip_and_check_exit(line):
    line = line.rstrip()
    if line in ("exit", "quit"):
        raise LLDBExit
    return line


def readfunc(prompt):
    line = input(prompt)
    return strip_and_check_exit(line)


def readfunc_stdio(prompt):
    sys.stdout.write(prompt)
    sys.stdout.flush()
    line = sys.stdin.readline()
    # Readline always includes a trailing newline character unless the file
    # ends with an incomplete line. An empty line indicates EOF.
    if not line:
        raise EOFError
    return strip_and_check_exit(line)


def run_python_interpreter(local_dict):
    # Pass in the dictionary, for continuity from one session to the next.
    try:
        fd = sys.stdin.fileno()
        interacted = False
        if get_terminal_size(fd)[1] == 0:
            try:
                import termios

                old = termios.tcgetattr(fd)
                if old[3] & termios.ECHO:
                    # Need to turn off echoing and restore
                    new = termios.tcgetattr(fd)
                    new[3] = new[3] & ~termios.ECHO
                    try:
                        termios.tcsetattr(fd, termios.TCSADRAIN, new)
                        interacted = True
                        code.interact(
                            banner="Python Interactive Interpreter. To exit, type 'quit()', 'exit()'.",
                            readfunc=readfunc_stdio,
                            local=local_dict,
                        )
                    finally:
                        termios.tcsetattr(fd, termios.TCSADRAIN, old)
            except:
                pass
            # Don't need to turn off echoing
            if not interacted:
                code.interact(
                    banner="Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.",
                    readfunc=readfunc_stdio,
                    local=local_dict,
                )
        else:
            # We have a real interactive terminal
            code.interact(
                banner="Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.",
                readfunc=readfunc,
                local=local_dict,
            )
    except LLDBExit:
        pass
    except SystemExit as e:
        if e.code:
            print("Script exited with code %s" % e.code)


def run_one_line(local_dict, input_string):
    global g_run_one_line_str
    try:
        input_string = strip_and_check_exit(input_string)
        repl = code.InteractiveConsole(local_dict)
        if input_string:
            # A newline is appended to support one-line statements containing
            # control flow. For example "if True: print(1)" silently does
            # nothing, but works with a newline: "if True: print(1)\n".
            input_string += "\n"
            repl.runsource(input_string)
        elif g_run_one_line_str:
            repl.runsource(g_run_one_line_str)
    except LLDBExit:
        pass
    except SystemExit as e:
        if e.code:
            print("Script exited with code %s" % e.code)
