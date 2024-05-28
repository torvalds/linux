#!/usr/bin/env python3

from enum import Enum

class ResultState(Enum):
    noresult = -1
    skip = 0
    success = 1
    fail = 2

class TestResult:
    def __init__(self, test_id="", test_name=""):
       self.test_id = test_id
       self.test_name = test_name
       self.result = ResultState.noresult
       self.failmsg = ""
       self.errormsg = ""
       self.steps = []

    def set_result(self, result):
        if (isinstance(result, ResultState)):
            self.result = result
            return True
        else:
            raise TypeError('Unknown result type, must be type ResultState')

    def get_result(self):
        return self.result

    def set_errormsg(self, errormsg):
        self.errormsg = errormsg
        return True

    def append_errormsg(self, errormsg):
        self.errormsg = '{}\n{}'.format(self.errormsg, errormsg)

    def get_errormsg(self):
        return self.errormsg

    def set_failmsg(self, failmsg):
        self.failmsg = failmsg
        return True

    def append_failmsg(self, failmsg):
        self.failmsg = '{}\n{}'.format(self.failmsg, failmsg)

    def get_failmsg(self):
        return self.failmsg

    def add_steps(self, newstep):
        if type(newstep) == list:
            self.steps.extend(newstep)
        elif type(newstep) == str:
            self.steps.append(step)
        else:
            raise TypeError('TdcResults.add_steps() requires a list or str')

    def get_executed_steps(self):
        return self.steps

class TestSuiteReport():
    def __init__(self):
        self._testsuite = []

    def add_resultdata(self, result_data):
        if isinstance(result_data, TestResult):
            self._testsuite.append(result_data)
            return True

    def count_tests(self):
        return len(self._testsuite)

    def count_failures(self):
        return sum(1 for t in self._testsuite if t.result == ResultState.fail)

    def count_skips(self):
        return sum(1 for t in self._testsuite if t.result == ResultState.skip)

    def find_result(self, test_id):
        return next((tr for tr in self._testsuite if tr.test_id == test_id), None)

    def update_result(self, result_data):
        orig = self.find_result(result_data.test_id)
        if orig != None:
            idx = self._testsuite.index(orig)
            self._testsuite[idx] = result_data
        else:
            self.add_resultdata(result_data)

    def format_tap(self):
        ftap = ""
        ftap += '1..{}\n'.format(self.count_tests())
        index = 1
        for t in self._testsuite:
            if t.result == ResultState.fail:
                ftap += 'not '
            ftap += 'ok {} {} - {}'.format(str(index), t.test_id, t.test_name)
            if t.result == ResultState.skip or t.result == ResultState.noresult:
                ftap += ' # skipped - {}\n'.format(t.errormsg)
            elif t.result == ResultState.fail:
                if len(t.steps) > 0:
                    ftap += '\tCommands executed in this test case:'
                    for step in t.steps:
                        ftap += '\n\t\t{}'.format(step)
                ftap += '\n\t{}'.format(t.failmsg)
            ftap += '\n'
            index += 1
        return ftap

    def format_xunit(self):
        from xml.sax.saxutils import escape
        xunit = "<testsuites>\n"
        xunit += '\t<testsuite tests=\"{}\" skips=\"{}\">\n'.format(self.count_tests(), self.count_skips())
        for t in self._testsuite:
            xunit += '\t\t<testcase classname=\"{}\" '.format(escape(t.test_id))
            xunit += 'name=\"{}\">\n'.format(escape(t.test_name))
            if t.failmsg:
                xunit += '\t\t\t<failure>\n'
                if len(t.steps) > 0:
                    xunit += 'Commands executed in this test case:\n'
                    for step in t.steps:
                        xunit += '\t{}\n'.format(escape(step))
                xunit += 'FAILURE: {}\n'.format(escape(t.failmsg))
                xunit += '\t\t\t</failure>\n'
            if t.errormsg:
                xunit += '\t\t\t<error>\n{}\n'.format(escape(t.errormsg))
                xunit += '\t\t\t</error>\n'
            if t.result == ResultState.skip:
                xunit += '\t\t\t<skipped/>\n'
            xunit += '\t\t</testcase>\n'
        xunit += '\t</testsuite>\n'
        xunit += '</testsuites>\n'
        return xunit
