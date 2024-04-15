# SPDX-License-Identifier: GPL-2.0

import builtins
import inspect
import time
import traceback
from .consts import KSFT_MAIN_NAME

KSFT_RESULT = None


class KsftSkipEx(Exception):
    pass


class KsftXfailEx(Exception):
    pass


def ksft_pr(*objs, **kwargs):
    print("#", *objs, **kwargs)


def _fail(*args):
    global KSFT_RESULT
    KSFT_RESULT = False

    frame = inspect.stack()[2]
    ksft_pr("At " + frame.filename + " line " + str(frame.lineno) + ":")
    ksft_pr(*args)


def ksft_eq(a, b, comment=""):
    global KSFT_RESULT
    if a != b:
        _fail("Check failed", a, "!=", b, comment)


def ksft_true(a, comment=""):
    if not a:
        _fail("Check failed", a, "does not eval to True", comment)


def ksft_in(a, b, comment=""):
    if a not in b:
        _fail("Check failed", a, "not in", b, comment)


def ksft_ge(a, b, comment=""):
    if a < b:
        _fail("Check failed", a, "<", b, comment)


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


def ksft_run(cases, args=()):
    totals = {"pass": 0, "fail": 0, "skip": 0, "xfail": 0}

    print("KTAP version 1")
    print("1.." + str(len(cases)))

    global KSFT_RESULT
    cnt = 0
    for case in cases:
        KSFT_RESULT = True
        cnt += 1
        try:
            case(*args)
        except KsftSkipEx as e:
            ktap_result(True, cnt, case, comment="SKIP " + str(e))
            totals['skip'] += 1
            continue
        except KsftXfailEx as e:
            ktap_result(True, cnt, case, comment="XFAIL " + str(e))
            totals['xfail'] += 1
            continue
        except Exception as e:
            tb = traceback.format_exc()
            for line in tb.strip().split('\n'):
                ksft_pr("Exception|", line)
            ktap_result(False, cnt, case)
            totals['fail'] += 1
            continue

        ktap_result(KSFT_RESULT, cnt, case)
        totals['pass'] += 1

    print(
        f"# Totals: pass:{totals['pass']} fail:{totals['fail']} xfail:{totals['xfail']} xpass:0 skip:{totals['skip']} error:0"
    )
