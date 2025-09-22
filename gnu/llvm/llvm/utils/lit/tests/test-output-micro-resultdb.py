# RUN: %{lit} -j 1 -v %{inputs}/test-data-micro --resultdb-output %t.results.out
# RUN: FileCheck < %t.results.out %s
# RUN: rm %t.results.out


# CHECK: {
# CHECK: "__version__"
# CHECK: "elapsed"
# CHECK-NEXT: "tests": [
# CHECK-NEXT:    {
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
# CHECK-NEXT:      "testId": "test-data-micro :: micro-tests.ini"
# CHECK-NEXT:    },
# CHECK-NEXT:    {
# CHECK-NEXT:      "artifacts": {
# CHECK-NEXT:        "artifact-content-in-request": {
# CHECK-NEXT:          "contents": ""
# CHECK-NEXT:        }
# CHECK-NEXT:      },
# CHECK-NEXT:      "duration"
# CHECK-NEXT:      "expected": true,
# CHECK-NEXT:      "start_time"
# CHECK-NEXT:      "status": "PASS",
# CHECK-NEXT:      "summary_html": "<p><text-artifact artifact-id=\"artifact-content-in-request\"></p>",
# CHECK-NEXT:      "testId": "test-data-micro :: micro-tests.ini:test0microres"
# CHECK-NEXT:    },
# CHECK-NEXT:    {
# CHECK-NEXT:      "artifacts": {
# CHECK-NEXT:        "artifact-content-in-request": {
# CHECK-NEXT:          "contents": ""
# CHECK-NEXT:        }
# CHECK-NEXT:      },
# CHECK-NEXT:      "duration"
# CHECK-NEXT:      "expected": true,
# CHECK-NEXT:      "start_time"
# CHECK-NEXT:      "status": "PASS",
# CHECK-NEXT:      "summary_html": "<p><text-artifact artifact-id=\"artifact-content-in-request\"></p>",
# CHECK-NEXT:      "testId": "test-data-micro :: micro-tests.ini:test1microres"
# CHECK-NEXT:    },
# CHECK-NEXT:    {
# CHECK-NEXT:      "artifacts": {
# CHECK-NEXT:        "artifact-content-in-request": {
# CHECK-NEXT:          "contents": ""
# CHECK-NEXT:        }
# CHECK-NEXT:      },
# CHECK-NEXT:      "duration"
# CHECK-NEXT:      "expected": true,
# CHECK-NEXT:      "start_time"
# CHECK-NEXT:      "status": "PASS",
# CHECK-NEXT:      "summary_html": "<p><text-artifact artifact-id=\"artifact-content-in-request\"></p>",
# CHECK-NEXT:      "testId": "test-data-micro :: micro-tests.ini:test2microres"
# CHECK-NEXT:    }
# CHECK-NEXT: ]
# CHECK-NEXT: }
