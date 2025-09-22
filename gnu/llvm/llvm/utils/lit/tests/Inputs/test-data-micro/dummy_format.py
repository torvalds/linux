import os

try:
    import ConfigParser
except ImportError:
    import configparser as ConfigParser

import lit.formats
import lit.Test


class DummyFormat(lit.formats.FileBasedTest):
    def execute(self, test, lit_config):
        # In this dummy format, expect that each test file is actually just a
        # .ini format dump of the results to report.

        source_path = test.getSourcePath()

        cfg = ConfigParser.ConfigParser()
        cfg.read(source_path)

        # Create the basic test result.
        result_code = cfg.get("global", "result_code")
        result_output = cfg.get("global", "result_output")
        result = lit.Test.Result(getattr(lit.Test, result_code), result_output)

        # Load additional metrics.
        for key, value_str in cfg.items("results"):
            value = eval(value_str)
            if isinstance(value, int):
                metric = lit.Test.IntMetricValue(value)
            elif isinstance(value, float):
                metric = lit.Test.RealMetricValue(value)
            else:
                raise RuntimeError("unsupported result type")
            result.addMetric(key, metric)

        # Create micro test results
        for key, micro_name in cfg.items("micro-tests"):
            micro_result = lit.Test.Result(getattr(lit.Test, result_code, ""))
            # Load micro test additional metrics
            for key, value_str in cfg.items("micro-results"):
                value = eval(value_str)
                if isinstance(value, int):
                    metric = lit.Test.IntMetricValue(value)
                elif isinstance(value, float):
                    metric = lit.Test.RealMetricValue(value)
                else:
                    raise RuntimeError("unsupported result type")
                micro_result.addMetric(key, metric)
            result.addMicroResult(micro_name, micro_result)

        return result
