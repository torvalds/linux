# SPDX-License-Identifier: GPL-2.0

import functools
import inspect
import signal
import sys
import time
import traceback
from collections import namedtuple
from .consts import KSFT_MAIN_NAME
from . import utils

KSFT_RESULT = None
KSFT_RESULT_ALL = True
KSFT_DISRUPTIVE = True


class KsftFailEx(Exception):
    pass


class KsftSkipEx(Exception):
    pass


class KsftXfailEx(Exception):
    pass


class KsftTerminate(KeyboardInterrupt):
    pass


def ksft_pr(*objs, **kwargs):
    """
    Print logs to stdout.

    Behaves like print() but log lines will be prefixed
    with # to prevent breaking the TAP output formatting.

    Extra arguments (on top of what print() supports):
      line_pfx - add extra string before each line
    """
    sep = kwargs.pop("sep", " ")
    pfx = kwargs.pop("line_pfx", "")
    pfx = "#" + (" " + pfx if pfx else "")
    kwargs["flush"] = True

    text = sep.join(str(obj) for obj in objs)
    prefixed = f"\n{pfx} ".join(text.split('\n'))
    print(pfx, prefixed, **kwargs)


def _fail(*args):
    global KSFT_RESULT
    KSFT_RESULT = False

    stack = inspect.stack()
    started = False
    for frame in reversed(stack[2:]):
        # Start printing from the test case function
        if not started:
            if frame.function == 'ksft_run':
                started = True
            continue

        ksft_pr("Check| At " + frame.filename + ", line " + str(frame.lineno) +
                ", in " + frame.function + ":")
        ksft_pr("Check|     " + frame.code_context[0].strip())
    ksft_pr(*args)


def ksft_eq(a, b, comment=""):
    global KSFT_RESULT
    if a != b:
        _fail("Check failed", a, "!=", b, comment)


def ksft_ne(a, b, comment=""):
    global KSFT_RESULT
    if a == b:
        _fail("Check failed", a, "==", b, comment)


def ksft_true(a, comment=""):
    if not a:
        _fail("Check failed", a, "does not eval to True", comment)


def ksft_not_none(a, comment=""):
    if a is None:
        _fail("Check failed", a, "is None", comment)


def ksft_in(a, b, comment=""):
    if a not in b:
        _fail("Check failed", a, "not in", b, comment)


def ksft_not_in(a, b, comment=""):
    if a in b:
        _fail("Check failed", a, "in", b, comment)


def ksft_is(a, b, comment=""):
    if a is not b:
        _fail("Check failed", a, "is not", b, comment)


def ksft_ge(a, b, comment=""):
    if a < b:
        _fail("Check failed", a, "<", b, comment)


def ksft_gt(a, b, comment=""):
    if a <= b:
        _fail("Check failed", a, "<=", b, comment)


def ksft_lt(a, b, comment=""):
    if a >= b:
        _fail("Check failed", a, ">=", b, comment)


class ksft_raises:
    def __init__(self, expected_type):
        self.exception = None
        self.expected_type = expected_type

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            _fail(f"Expected exception {str(self.expected_type.__name__)}, none raised")
        elif self.expected_type != exc_type:
            _fail(f"Expected exception {str(self.expected_type.__name__)}, raised {str(exc_type.__name__)}")
        self.exception = exc_val
        # Suppress the exception if its the expected one
        return self.expected_type == exc_type


def ksft_busy_wait(cond, sleep=0.005, deadline=1, comment=""):
    end = time.monotonic() + deadline
    while True:
        if cond():
            return
        if time.monotonic() > end:
            _fail("Waiting for condition timed out", comment)
            return
        time.sleep(sleep)


def ktap_result(ok, cnt=1, case_name="", comment=""):
    global KSFT_RESULT_ALL
    KSFT_RESULT_ALL = KSFT_RESULT_ALL and ok

    res = ""
    if not ok:
        res += "not "
    res += "ok "
    res += str(cnt) + " "
    res += KSFT_MAIN_NAME
    if case_name:
        res += "." + case_name
    if comment:
        res += " # " + comment
    print(res, flush=True)


def _ksft_defer_arm(state):
    """ Allow or disallow the use of defer() """
    utils.GLOBAL_DEFER_ARMED = state


def ksft_flush_defer():
    global KSFT_RESULT

    i = 0
    qlen_start = len(utils.GLOBAL_DEFER_QUEUE)
    while utils.GLOBAL_DEFER_QUEUE:
        i += 1
        entry = utils.GLOBAL_DEFER_QUEUE.pop()
        try:
            entry.exec_only()
        except Exception:
            ksft_pr(f"Exception while handling defer / cleanup (callback {i} of {qlen_start})!")
            ksft_pr(traceback.format_exc(), line_pfx="Defer Exception|")
            KSFT_RESULT = False


KsftCaseFunction = namedtuple("KsftCaseFunction",
                              ['name', 'original_func', 'variants'])


def ksft_disruptive(func):
    """
    Decorator that marks the test as disruptive (e.g. the test
    that can down the interface). Disruptive tests can be skipped
    by passing DISRUPTIVE=False environment variable.
    """

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        if not KSFT_DISRUPTIVE:
            raise KsftSkipEx("marked as disruptive")
        return func(*args, **kwargs)
    return wrapper


class KsftNamedVariant:
    """ Named string name + argument list tuple for @ksft_variants """

    def __init__(self, name, *params):
        self.params = params
        self.name = name or "_".join([str(x) for x in self.params])


def ksft_variants(params):
    """
    Decorator defining the sets of inputs for a test.
    The parameters will be included in the name of the resulting sub-case.
    Parameters can be either single object, tuple or a KsftNamedVariant.
    The argument can be a list or a generator.

    Example:

    @ksft_variants([
        (1, "a"),
        (2, "b"),
        KsftNamedVariant("three", 3, "c"),
    ])
    def my_case(cfg, a, b):
        pass # ...

    ksft_run(cases=[my_case], args=(cfg, ))

    Will generate cases:
        my_case.1_a
        my_case.2_b
        my_case.three
    """

    return lambda func: KsftCaseFunction(func.__name__, func, params)


def ksft_setup(env):
    """
    Setup test framework global state from the environment.
    """

    def get_bool(env, name):
        value = env.get(name, "").lower()
        if value in ["yes", "true"]:
            return True
        if value in ["no", "false"]:
            return False
        try:
            return bool(int(value))
        except Exception:
            raise Exception(f"failed to parse {name}")

    if "DISRUPTIVE" in env:
        global KSFT_DISRUPTIVE
        KSFT_DISRUPTIVE = get_bool(env, "DISRUPTIVE")

    return env


def _ksft_intr(signum, frame):
    # ksft runner.sh sends 2 SIGTERMs in a row on a timeout
    # if we don't ignore the second one it will stop us from handling cleanup
    global term_cnt
    term_cnt += 1
    if term_cnt == 1:
        raise KsftTerminate()
    else:
        ksft_pr(f"Ignoring SIGTERM (cnt: {term_cnt}), already exiting...")


def _ksft_generate_test_cases(cases, globs, case_pfx, args):
    """Generate a flat list of (func, args, name) tuples"""

    cases = cases or []
    test_cases = []

    # If using the globs method find all relevant functions
    if globs and case_pfx:
        for key, value in globs.items():
            if not callable(value):
                continue
            for prefix in case_pfx:
                if key.startswith(prefix):
                    cases.append(value)
                    break

    for func in cases:
        if isinstance(func, KsftCaseFunction):
            # Parametrized test - create case for each param
            for param in func.variants:
                if not isinstance(param, KsftNamedVariant):
                    if not isinstance(param, tuple):
                        param = (param, )
                    param = KsftNamedVariant(None, *param)

                test_cases.append((func.original_func,
                                   (*args, *param.params),
                                   func.name + "." + param.name))
        else:
            test_cases.append((func, args, func.__name__))

    return test_cases


def ksft_run(cases=None, globs=None, case_pfx=None, args=()):
    test_cases = _ksft_generate_test_cases(cases, globs, case_pfx, args)

    global term_cnt
    term_cnt = 0
    prev_sigterm = signal.signal(signal.SIGTERM, _ksft_intr)

    totals = {"pass": 0, "fail": 0, "skip": 0, "xfail": 0}

    print("TAP version 13", flush=True)
    print("1.." + str(len(test_cases)), flush=True)

    global KSFT_RESULT
    cnt = 0
    stop = False
    for func, args, name in test_cases:
        KSFT_RESULT = True
        cnt += 1
        comment = ""
        cnt_key = ""

        _ksft_defer_arm(True)
        try:
            func(*args)
        except KsftSkipEx as e:
            comment = "SKIP " + str(e)
            cnt_key = 'skip'
        except KsftXfailEx as e:
            comment = "XFAIL " + str(e)
            cnt_key = 'xfail'
        except BaseException as e:
            stop |= isinstance(e, KeyboardInterrupt)
            ksft_pr(traceback.format_exc(), line_pfx="Exception|")
            if stop:
                ksft_pr(f"Stopping tests due to {type(e).__name__}.")
            KSFT_RESULT = False
            cnt_key = 'fail'
        _ksft_defer_arm(False)

        try:
            ksft_flush_defer()
        except BaseException as e:
            ksft_pr(traceback.format_exc(), line_pfx="Exception|")
            if isinstance(e, KeyboardInterrupt):
                ksft_pr()
                ksft_pr("WARN: defer() interrupted, cleanup may be incomplete.")
                ksft_pr("      Attempting to finish cleanup before exiting.")
                ksft_pr("      Interrupt again to exit immediately.")
                ksft_pr()
                stop = True
            # Flush was interrupted, try to finish the job best we can
            ksft_flush_defer()

        if not cnt_key:
            cnt_key = 'pass' if KSFT_RESULT else 'fail'

        ktap_result(KSFT_RESULT, cnt, name, comment=comment)
        totals[cnt_key] += 1

        if stop:
            break

    signal.signal(signal.SIGTERM, prev_sigterm)

    print(
        f"# Totals: pass:{totals['pass']} fail:{totals['fail']} xfail:{totals['xfail']} xpass:0 skip:{totals['skip']} error:0"
    )


def ksft_exit():
    global KSFT_RESULT_ALL
    sys.exit(0 if KSFT_RESULT_ALL else 1)
