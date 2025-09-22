# RUN: %{lit} -v %{inputs}/test-data-micro --output %t.results.out
# RUN: FileCheck < %t.results.out %s
# RUN: rm %t.results.out


# CHECK: {
# CHECK: "__version__"
# CHECK: "elapsed"
# CHECK-NEXT: "tests": [
# CHECK-NEXT:   {
# CHECK-NEXT:     "code": "PASS",
# CHECK-NEXT:     "elapsed": null,
# CHECK-NEXT:     "metrics": {
# CHECK-NEXT:       "micro_value0": 4,
# CHECK-NEXT:       "micro_value1": 1.3
# CHECK-NEXT:     },
# CHECK-NEXT:     "name": "test-data-micro :: micro-tests.ini:test{{[0-2]}}",
# CHECK-NEXT:     "output": ""
# CHECK-NEXT:   },
# CHECK-NEXT:   {
# CHECK-NEXT:     "code": "PASS",
# CHECK-NEXT:     "elapsed": null,
# CHECK-NEXT:     "metrics": {
# CHECK-NEXT:       "micro_value0": 4,
# CHECK-NEXT:       "micro_value1": 1.3
# CHECK-NEXT:     },
# CHECK-NEXT:     "name": "test-data-micro :: micro-tests.ini:test{{[0-2]}}",
# CHECK-NEXT:     "output": ""
# CHECK-NEXT:   },
# CHECK-NEXT:   {
# CHECK-NEXT:     "code": "PASS",
# CHECK-NEXT:     "elapsed": null,
# CHECK-NEXT:     "metrics": {
# CHECK-NEXT:       "micro_value0": 4,
# CHECK-NEXT:       "micro_value1": 1.3
# CHECK-NEXT:     },
# CHECK-NEXT:     "name": "test-data-micro :: micro-tests.ini:test{{[0-2]}}",
# CHECK-NEXT:     "output": ""
# CHECK-NEXT:   },
# CHECK-NEXT:   {
# CHECK-NEXT:     "code": "PASS",
# CHECK-NEXT:     "elapsed": {{[-+0-9.eE]+}},
# CHECK-NEXT:     "metrics": {
# CHECK-NEXT:       "value0": 1,
# CHECK-NEXT:       "value1": 2.3456
# CHECK-NEXT:     },
# CHECK-NEXT:     "name": "test-data-micro :: micro-tests.ini",
# CHECK-NEXT:     "output": "Test passed."
# CHECK-NEXT:   }
# CHECK-NEXT: ]
# CHECK-NEXT: }
