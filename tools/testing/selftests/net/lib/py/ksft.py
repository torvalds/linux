# SPDX-License-Identifier: GPL-2.0

import builtins
import functools
import inspect
import sys
import time
import traceback
from .consts import KSFT_MAIN_NAME
from .utils import global_defer_queue

KSFT_RESULT = None
KSFT_RESULT_ALL = True
KSFT_DISRUPTIVE = True


class KsftFailEx(Exception):
    pass


class KsftSkipEx(Exception):
    pass


class KsftXfailEx(Exception):
    pass


def ksft_pr(*objs, **kwargs):
    print("#", *objs, **kwargs)


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


def ksft_in(a, b, comment=""):
    if a not in b:
        _fail("Check failed", a, "not in", b, comment)


def ksft_ge(a, b, comment=""):
    if a < b:
        _fail("Check failed", a, "<", b, comment)


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


def ktap_result(ok, cnt=1, case="", comment=""):
    global KSFT_RESULT_ALL
    KSFT_RESULT_ALL = KSFT_RESULT_ALL and ok

    res = ""
    if not ok:
        res += "not "
    res += "ok "
    res += str(cnt) + " "
    res += KSFT_MAIN_NAME
    if case:
        res += "." + str(case.__name__)
    if comment:
        res += " # " + comment
    print(res)


def ksft_flush_defer():
    global KSFT_RESULT

    i = 0
    qlen_start = len(global_defer_queue)
    while global_defer_queue:
        i += 1
        entry = global_defer_queue.pop()
        try:
            entry.exec_only()
        except:
            ksft_pr(f"Exception while handling defer / cleanup (callback {i} of {qlen_start})!")
            tb = traceback.format_exc()
            for line in tb.strip().split('\n'):
                ksft_pr("Defer Exception|", line)
            KSFT_RESULT = False


def ksft_disruptive(func):
    """
    Decorator that marks the test as disruptive (e.g. the test
    that can down the interface). Disruptive tests can be skipped
    by passing DISRUPTIVE=False environment variable.
    """

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        if not KSFT_DISRUPTIVE:
            raise KsftSkipEx(f"marked as disruptive")
        return func(*args, **kwargs)
    return wrapper


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
        except:
            raise Exception(f"failed to parse {name}")

    if "DISRUPTIVE" in env:
        global KSFT_DISRUPTIVE
        KSFT_DISRUPTIVE = get_bool(env, "DISRUPTIVE")

    return env


def ksft_run(cases=None, globs=None, case_pfx=None, args=()):
    cases = cases or []

    if globs and case_pfx:
        for key, value in globs.items():
            if not callable(value):
                continue
            for prefix in case_pfx:
                if key.startswith(prefix):
                    cases.append(value)
                    break

    totals = {"pass": 0, "fail": 0, "skip": 0, "xfail": 0}

    print("KTAP version 1")
    print("1.." + str(len(cases)))

    global KSFT_RESULT
    cnt = 0
    stop = False
    for case in cases:
        KSFT_RESULT = True
        cnt += 1
        comment = ""
        cnt_key = ""

        try:
            case(*args)
        except KsftSkipEx as e:
            comment = "SKIP " + str(e)
            cnt_key = 'skip'
        except KsftXfailEx as e:
            comment = "XFAIL " + str(e)
            cnt_key = 'xfail'
        except BaseException as e:
            stop |= isinstance(e, KeyboardInterrupt)
            tb = traceback.format_exc()
            for line in tb.strip().split('\n'):
                ksft_pr("Exception|", line)
            if stop:
                ksft_pr("Stopping tests due to KeyboardInterrupt.")
            KSFT_RESULT = False
            cnt_key = 'fail'

        ksft_flush_defer()

        if not cnt_key:
            cnt_key = 'pass' if KSFT_RESULT else 'fail'

        ktap_result(KSFT_RESULT, cnt, case, comment=comment)
        totals[cnt_key] += 1

        if stop:
            break

    print(
        f"# Totals: pass:{totals['pass']} fail:{totals['fail']} xfail:{totals['xfail']} xpass:0 skip:{totals['skip']} error:0"
    )


def ksft_exit():
    global KSFT_RESULT_ALL
    sys.exit(0 if KSFT_RESULT_ALL else 1)
