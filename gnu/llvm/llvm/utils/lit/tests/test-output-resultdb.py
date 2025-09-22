# RUN: %{lit} -j 1 -v %{inputs}/test-data --resultdb-output %t.results.out > %t.out
# RUN: FileCheck < %t.results.out %s
# RUN: %{lit} -j 1 -v %{inputs}/googletest-cmd-wrapper --resultdb-output %t.results-unit.out > %t.out
# RUN: FileCheck < %t.results-unit.out --check-prefix=UNIT %s

# CHECK: {
# CHECK: "__version__"
# CHECK: "elapsed"
# CHECK-NEXT: "tests": [
# CHECK-NEXT:   {
# CHECK-NEXT:      "artifacts": {
# CHECK-NEXT:        "artifact-content-in-request": {
# CHECK-NEXT:          "contents": "VGVzdCBwYXNzZWQu"
# CHECK-NEXT:        }
# CHECK-NEXT:      },
# CHECK-NEXT:      "duration"
# CHECK-NEXT:      "expected": true,
# CHECK-NEXT:      "start_time"
# CHECK-NEXT:      "status": "PASS",
# CHECK-NEXT:      "summary_html": "<p><text-artifact artifact-id=\"artifact-content-in-request\"></p>",
# CHECK-NEXT:      "testId": "test-data :: metrics.ini"
# CHECK-NEXT:    }
# CHECK-NEXT: ]
# CHECK-NEXT: }

# UNIT: {
# UNIT: "__version__"
# UNIT: "elapsed"
# UNIT-NEXT: "tests": [
# UNIT-NEXT:   {
# UNIT-NEXT:     "artifacts": {
# UNIT-NEXT:       "artifact-content-in-request": {
# UNIT-NEXT:         "contents": ""
# UNIT-NEXT:       }
# UNIT-NEXT:     },
# UNIT-NEXT:     "duration"
# UNIT-NEXT:     "expected": true,
# UNIT-NEXT:     "start_time"
# UNIT-NEXT:     "status": "PASS",
# UNIT-NEXT:     "summary_html": "<p><text-artifact artifact-id=\"artifact-content-in-request\"></p>",
# UNIT-NEXT:     "testId": "googletest-cmd-wrapper :: DummySubDir/OneTest.exe/FirstTest/subTestA"
# UNIT-NEXT:   }
# UNIT-NEXT: ]
# UNIT-NEXT: }
