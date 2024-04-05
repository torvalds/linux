# SPDX-License-Identifier: GPL-2.0
import re
import csv
import json
import argparse
from pathlib import Path
import subprocess


class TestError:
    def __init__(self, metric: list[str], wl: str, value: list[float], low: float, up=float('nan'), description=str()):
        self.metric: list = metric  # multiple metrics in relationship type tests
        self.workloads = [wl]  # multiple workloads possible
        self.collectedValue: list = value
        self.valueLowBound = low
        self.valueUpBound = up
        self.description = description

    def __repr__(self) -> str:
        if len(self.metric) > 1:
            return "\nMetric Relationship Error: \tThe collected value of metric {0}\n\
                \tis {1} in workload(s): {2} \n\
                \tbut expected value range is [{3}, {4}]\n\
                \tRelationship rule description: \'{5}\'".format(self.metric, self.collectedValue, self.workloads,
                                                                 self.valueLowBound, self.valueUpBound, self.description)
        elif len(self.collectedValue) == 0:
            return "\nNo Metric Value Error: \tMetric {0} returns with no value \n\
                    \tworkload(s): {1}".format(self.metric, self.workloads)
        else:
            return "\nWrong Metric Value Error: \tThe collected value of metric {0}\n\
                    \tis {1} in workload(s): {2}\n\
                    \tbut expected value range is [{3}, {4}]"\
                        .format(self.metric, self.collectedValue, self.workloads,
                                self.valueLowBound, self.valueUpBound)


class Validator:
    def __init__(self, rulefname, reportfname='', t=5, debug=False, datafname='', fullrulefname='', workload='true', metrics=''):
        self.rulefname = rulefname
        self.reportfname = reportfname
        self.rules = None
        self.collectlist: str = metrics
        self.metrics = self.__set_metrics(metrics)
        self.skiplist = set()
        self.tolerance = t

        self.workloads = [x for x in workload.split(",") if x]
        self.wlidx = 0  # idx of current workloads
        self.allresults = dict()  # metric results of all workload
        self.alltotalcnt = dict()
        self.allpassedcnt = dict()

        self.results = dict()  # metric results of current workload
        # vars for test pass/failure statistics
        # metrics with no results or negative results, neg result counts failed tests
        self.ignoremetrics = set()
        self.totalcnt = 0
        self.passedcnt = 0
        # vars for errors
        self.errlist = list()

        # vars for Rule Generator
        self.pctgmetrics = set()  # Percentage rule

        # vars for debug
        self.datafname = datafname
        self.debug = debug
        self.fullrulefname = fullrulefname

    def __set_metrics(self, metrics=''):
        if metrics != '':
            return set(metrics.split(","))
        else:
            return set()

    def read_json(self, filename: str) -> dict:
        try:
            with open(Path(filename).resolve(), "r") as f:
                data = json.loads(f.read())
        except OSError as e:
            print(f"Error when reading file {e}")
            sys.exit()

        return data

    def json_dump(self, data, output_file):
        parent = Path(output_file).parent
        if not parent.exists():
            parent.mkdir(parents=True)

        with open(output_file, "w+") as output_file:
            json.dump(data,
                      output_file,
                      ensure_ascii=True,
                      indent=4)

    def get_results(self, idx: int = 0):
        return self.results[idx]

    def get_bounds(self, lb, ub, error, alias={}, ridx: int = 0) -> list:
        """
        Get bounds and tolerance from lb, ub, and error.
        If missing lb, use 0.0; missing ub, use float('inf); missing error, use self.tolerance.

        @param lb: str/float, lower bound
        @param ub: str/float, upper bound
        @param error: float/str, error tolerance
        @returns: lower bound, return inf if the lower bound is a metric value and is not collected
                  upper bound, return -1 if the upper bound is a metric value and is not collected
                  tolerance, denormalized base on upper bound value
        """
        # init ubv and lbv to invalid values
        def get_bound_value(bound, initval, ridx):
            val = initval
            if isinstance(bound, int) or isinstance(bound, float):
                val = bound
            elif isinstance(bound, str):
                if bound == '':
                    val = float("inf")
                elif bound in alias:
                    vall = self.get_value(alias[ub], ridx)
                    if vall:
                        val = vall[0]
                elif bound.replace('.', '1').isdigit():
                    val = float(bound)
                else:
                    print("Wrong bound: {0}".format(bound))
            else:
                print("Wrong bound: {0}".format(bound))
            return val

        ubv = get_bound_value(ub, -1, ridx)
        lbv = get_bound_value(lb, float('inf'), ridx)
        t = get_bound_value(error, self.tolerance, ridx)

        # denormalize error threshold
        denormerr = t * ubv / 100 if ubv != 100 and ubv > 0 else t

        return lbv, ubv, denormerr

    def get_value(self, name: str, ridx: int = 0) -> list:
        """
        Get value of the metric from self.results.
        If result of this metric is not provided, the metric name will be added into self.ignoremetics.
        All future test(s) on this metric will fail.

        @param name: name of the metric
        @returns: list with value found in self.results; list is empty when value is not found.
        """
        results = []
        data = self.results[ridx] if ridx in self.results else self.results[0]
        if name not in self.ignoremetrics:
            if name in data:
                results.append(data[name])
            elif name.replace('.', '1').isdigit():
                results.append(float(name))
            else:
                self.ignoremetrics.add(name)
        return results

    def check_bound(self, val, lb, ub, err):
        return True if val <= ub + err and val >= lb - err else False

    # Positive Value Sanity check
    def pos_val_test(self):
        """
        Check if metrics value are non-negative.
        One metric is counted as one test.
        Failure: when metric value is negative or not provided.
        Metrics with negative value will be added into self.ignoremetrics.
        """
        negmetric = dict()
        pcnt = 0
        tcnt = 0
        rerun = list()
        for name, val in self.get_results().items():
            if val < 0:
                negmetric[name] = val
                rerun.append(name)
            else:
                pcnt += 1
            tcnt += 1
        # The first round collect_perf() run these metrics with simple workload
        # "true". We give metrics a second chance with a longer workload if less
        # than 20 metrics failed positive test.
        if len(rerun) > 0 and len(rerun) < 20:
            second_results = dict()
            self.second_test(rerun, second_results)
            for name, val in second_results.items():
                if name not in negmetric:
                    continue
                if val >= 0:
                    del negmetric[name]
                    pcnt += 1

        if len(negmetric.keys()):
            self.ignoremetrics.update(negmetric.keys())
            self.errlist.extend(
                [TestError([m], self.workloads[self.wlidx], negmetric[m], 0) for m in negmetric.keys()])

        return

    def evaluate_formula(self, formula: str, alias: dict, ridx: int = 0):
        """
        Evaluate the value of formula.

        @param formula: the formula to be evaluated
        @param alias: the dict has alias to metric name mapping
        @returns: value of the formula is success; -1 if the one or more metric value not provided
        """
        stack = []
        b = 0
        errs = []
        sign = "+"
        f = str()

        # TODO: support parenthesis?
        for i in range(len(formula)):
            if i+1 == len(formula) or formula[i] in ('+', '-', '*', '/'):
                s = alias[formula[b:i]] if i + \
                    1 < len(formula) else alias[formula[b:]]
                v = self.get_value(s, ridx)
                if not v:
                    errs.append(s)
                else:
                    f = f + "{0}(={1:.4f})".format(s, v[0])
                    if sign == "*":
                        stack[-1] = stack[-1] * v
                    elif sign == "/":
                        stack[-1] = stack[-1] / v
                    elif sign == '-':
                        stack.append(-v[0])
                    else:
                        stack.append(v[0])
                if i + 1 < len(formula):
                    sign = formula[i]
                    f += sign
                    b = i + 1

        if len(errs) > 0:
            return -1, "Metric value missing: "+','.join(errs)

        val = sum(stack)
        return val, f

    # Relationships Tests
    def relationship_test(self, rule: dict):
        """
        Validate if the metrics follow the required relationship in the rule.
        eg. lower_bound <= eval(formula)<= upper_bound
        One rule is counted as ont test.
        Failure: when one or more metric result(s) not provided, or when formula evaluated outside of upper/lower bounds.

        @param rule: dict with metric name(+alias), formula, and required upper and lower bounds.
        """
        alias = dict()
        for m in rule['Metrics']:
            alias[m['Alias']] = m['Name']
        lbv, ubv, t = self.get_bounds(
            rule['RangeLower'], rule['RangeUpper'], rule['ErrorThreshold'], alias, ridx=rule['RuleIndex'])
        val, f = self.evaluate_formula(
            rule['Formula'], alias, ridx=rule['RuleIndex'])

        lb = rule['RangeLower']
        ub = rule['RangeUpper']
        if isinstance(lb, str):
            if lb in alias:
                lb = alias[lb]
        if isinstance(ub, str):
            if ub in alias:
                ub = alias[ub]

        if val == -1:
            self.errlist.append(TestError([m['Name'] for m in rule['Metrics']], self.workloads[self.wlidx], [],
                                lb, ub, rule['Description']))
        elif not self.check_bound(val, lbv, ubv, t):
            self.errlist.append(TestError([m['Name'] for m in rule['Metrics']], self.workloads[self.wlidx], [val],
                                lb, ub, rule['Description']))
        else:
            self.passedcnt += 1
        self.totalcnt += 1

        return

    # Single Metric Test
    def single_test(self, rule: dict):
        """
        Validate if the metrics are in the required value range.
        eg. lower_bound <= metrics_value <= upper_bound
        One metric is counted as one test in this type of test.
        One rule may include one or more metrics.
        Failure: when the metric value not provided or the value is outside the bounds.
        This test updates self.total_cnt.

        @param rule: dict with metrics to validate and the value range requirement
        """
        lbv, ubv, t = self.get_bounds(
            rule['RangeLower'], rule['RangeUpper'], rule['ErrorThreshold'])
        metrics = rule['Metrics']
        passcnt = 0
        totalcnt = 0
        failures = dict()
        rerun = list()
        for m in metrics:
            totalcnt += 1
            result = self.get_value(m['Name'])
            if len(result) > 0 and self.check_bound(result[0], lbv, ubv, t) or m['Name'] in self.skiplist:
                passcnt += 1
            else:
                failures[m['Name']] = result
                rerun.append(m['Name'])

        if len(rerun) > 0 and len(rerun) < 20:
            second_results = dict()
            self.second_test(rerun, second_results)
            for name, val in second_results.items():
                if name not in failures:
                    continue
                if self.check_bound(val, lbv, ubv, t):
                    passcnt += 1
                    del failures[name]
                else:
                    failures[name] = [val]
                    self.results[0][name] = val

        self.totalcnt += totalcnt
        self.passedcnt += passcnt
        if len(failures.keys()) != 0:
            self.errlist.extend([TestError([name], self.workloads[self.wlidx], val,
                                rule['RangeLower'], rule['RangeUpper']) for name, val in failures.items()])

        return

    def create_report(self):
        """
        Create final report and write into a JSON file.
        """
        print(self.errlist)

        if self.debug:
            allres = [{"Workload": self.workloads[i], "Results": self.allresults[i]}
                      for i in range(0, len(self.workloads))]
            self.json_dump(allres, self.datafname)

    def check_rule(self, testtype, metric_list):
        """
        Check if the rule uses metric(s) that not exist in current platform.

        @param metric_list: list of metrics from the rule.
        @return: False when find one metric out in Metric file. (This rule should not skipped.)
                 True when all metrics used in the rule are found in Metric file.
        """
        if testtype == "RelationshipTest":
            for m in metric_list:
                if m['Name'] not in self.metrics:
                    return False
        return True

    # Start of Collector and Converter
    def convert(self, data: list, metricvalues: dict):
        """
        Convert collected metric data from the -j output to dict of {metric_name:value}.
        """
        for json_string in data:
            try:
                result = json.loads(json_string)
                if "metric-unit" in result and result["metric-unit"] != "(null)" and result["metric-unit"] != "":
                    name = result["metric-unit"].split("  ")[1] if len(result["metric-unit"].split("  ")) > 1 \
                        else result["metric-unit"]
                    metricvalues[name.lower()] = float(result["metric-value"])
            except ValueError as error:
                continue
        return

    def _run_perf(self, metric, workload: str):
        tool = 'perf'
        command = [tool, 'stat', '-j', '-M', f"{metric}", "-a"]
        wl = workload.split()
        command.extend(wl)
        print(" ".join(command))
        cmd = subprocess.run(command, stderr=subprocess.PIPE, encoding='utf-8')
        data = [x+'}' for x in cmd.stderr.split('}\n') if x]
        if data[0][0] != '{':
            data[0] = data[0][data[0].find('{'):]
        return data

    def collect_perf(self, workload: str):
        """
        Collect metric data with "perf stat -M" on given workload with -a and -j.
        """
        self.results = dict()
        print(f"Starting perf collection")
        print(f"Long workload: {workload}")
        collectlist = dict()
        if self.collectlist != "":
            collectlist[0] = {x for x in self.collectlist.split(",")}
        else:
            collectlist[0] = set(list(self.metrics))
        # Create metric set for relationship rules
        for rule in self.rules:
            if rule["TestType"] == "RelationshipTest":
                metrics = [m["Name"] for m in rule["Metrics"]]
                if not any(m not in collectlist[0] for m in metrics):
                    collectlist[rule["RuleIndex"]] = [
                        ",".join(list(set(metrics)))]

        for idx, metrics in collectlist.items():
            if idx == 0:
                wl = "true"
            else:
                wl = workload
            for metric in metrics:
                data = self._run_perf(metric, wl)
                if idx not in self.results:
                    self.results[idx] = dict()
                self.convert(data, self.results[idx])
        return

    def second_test(self, collectlist, second_results):
        workload = self.workloads[self.wlidx]
        for metric in collectlist:
            data = self._run_perf(metric, workload)
            self.convert(data, second_results)

    # End of Collector and Converter

    # Start of Rule Generator
    def parse_perf_metrics(self):
        """
        Read and parse perf metric file:
        1) find metrics with '1%' or '100%' as ScaleUnit for Percent check
        2) create metric name list
        """
        command = ['perf', 'list', '-j', '--details', 'metrics']
        cmd = subprocess.run(command, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, encoding='utf-8')
        try:
            data = json.loads(cmd.stdout)
            for m in data:
                if 'MetricName' not in m:
                    print("Warning: no metric name")
                    continue
                name = m['MetricName'].lower()
                self.metrics.add(name)
                if 'ScaleUnit' in m and (m['ScaleUnit'] == '1%' or m['ScaleUnit'] == '100%'):
                    self.pctgmetrics.add(name.lower())
        except ValueError as error:
            print(f"Error when parsing metric data")
            sys.exit()

        return

    def remove_unsupported_rules(self, rules):
        new_rules = []
        for rule in rules:
            add_rule = True
            for m in rule["Metrics"]:
                if m["Name"] in self.skiplist or m["Name"] not in self.metrics:
                    add_rule = False
                    break
            if add_rule:
                new_rules.append(rule)
        return new_rules

    def create_rules(self):
        """
        Create full rules which includes:
        1) All the rules from the "relationshi_rules" file
        2) SingleMetric rule for all the 'percent' metrics

        Reindex all the rules to avoid repeated RuleIndex
        """
        data = self.read_json(self.rulefname)
        rules = data['RelationshipRules']
        self.skiplist = set([name.lower() for name in data['SkipList']])
        self.rules = self.remove_unsupported_rules(rules)
        pctgrule = {'RuleIndex': 0,
                    'TestType': 'SingleMetricTest',
                    'RangeLower': '0',
                    'RangeUpper': '100',
                    'ErrorThreshold': self.tolerance,
                    'Description': 'Metrics in percent unit have value with in [0, 100]',
                    'Metrics': [{'Name': m.lower()} for m in self.pctgmetrics]}
        self.rules.append(pctgrule)

        # Re-index all rules to avoid repeated RuleIndex
        idx = 1
        for r in self.rules:
            r['RuleIndex'] = idx
            idx += 1

        if self.debug:
            # TODO: need to test and generate file name correctly
            data = {'RelationshipRules': self.rules, 'SupportedMetrics': [
                {"MetricName": name} for name in self.metrics]}
            self.json_dump(data, self.fullrulefname)

        return
    # End of Rule Generator

    def _storewldata(self, key):
        '''
        Store all the data of one workload into the corresponding data structure for all workloads.
        @param key: key to the dictionaries (index of self.workloads).
        '''
        self.allresults[key] = self.results
        self.alltotalcnt[key] = self.totalcnt
        self.allpassedcnt[key] = self.passedcnt

    # Initialize data structures before data validation of each workload
    def _init_data(self):

        testtypes = ['PositiveValueTest',
                     'RelationshipTest', 'SingleMetricTest']
        self.results = dict()
        self.ignoremetrics = set()
        self.errlist = list()
        self.totalcnt = 0
        self.passedcnt = 0

    def test(self):
        '''
        The real entry point of the test framework.
        This function loads the validation rule JSON file and Standard Metric file to create rules for
        testing and namemap dictionaries.
        It also reads in result JSON file for testing.

        In the test process, it passes through each rule and launch correct test function bases on the
        'TestType' field of the rule.

        The final report is written into a JSON file.
        '''
        if not self.collectlist:
            self.parse_perf_metrics()
        self.create_rules()
        for i in range(0, len(self.workloads)):
            self.wlidx = i
            self._init_data()
            self.collect_perf(self.workloads[i])
            # Run positive value test
            self.pos_val_test()
            for r in self.rules:
                # skip rules that uses metrics not exist in this platform
                testtype = r['TestType']
                if not self.check_rule(testtype, r['Metrics']):
                    continue
                if testtype == 'RelationshipTest':
                    self.relationship_test(r)
                elif testtype == 'SingleMetricTest':
                    self.single_test(r)
                else:
                    print("Unsupported Test Type: ", testtype)
            print("Workload: ", self.workloads[i])
            print("Total Test Count: ", self.totalcnt)
            print("Passed Test Count: ", self.passedcnt)
            self._storewldata(i)
        self.create_report()
        return len(self.errlist) > 0
# End of Class Validator


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Launch metric value validation")

    parser.add_argument(
        "-rule", help="Base validation rule file", required=True)
    parser.add_argument(
        "-output_dir", help="Path for validator output file, report file", required=True)
    parser.add_argument("-debug", help="Debug run, save intermediate data to files",
                        action="store_true", default=False)
    parser.add_argument(
        "-wl", help="Workload to run while data collection", default="true")
    parser.add_argument("-m", help="Metric list to validate", default="")
    args = parser.parse_args()
    outpath = Path(args.output_dir)
    reportf = Path.joinpath(outpath, 'perf_report.json')
    fullrule = Path.joinpath(outpath, 'full_rule.json')
    datafile = Path.joinpath(outpath, 'perf_data.json')

    validator = Validator(args.rule, reportf, debug=args.debug,
                          datafname=datafile, fullrulefname=fullrule, workload=args.wl,
                          metrics=args.m)
    ret = validator.test()

    return ret


if __name__ == "__main__":
    import sys
    sys.exit(main())
