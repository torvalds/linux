# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import json
import libear
import libscanbuild.report as sut
import unittest
import os
import os.path


def run_bug_parse(content):
    with libear.TemporaryDirectory() as tmpdir:
        file_name = os.path.join(tmpdir, "test.html")
        with open(file_name, "w") as handle:
            handle.writelines(content)
        for bug in sut.parse_bug_html(file_name):
            return bug


def run_crash_parse(content, preproc):
    with libear.TemporaryDirectory() as tmpdir:
        file_name = os.path.join(tmpdir, preproc + ".info.txt")
        with open(file_name, "w") as handle:
            handle.writelines(content)
        return sut.parse_crash(file_name)


class ParseFileTest(unittest.TestCase):
    def test_parse_bug(self):
        content = [
            "some header\n",
            "<!-- BUGDESC Division by zero -->\n",
            "<!-- BUGTYPE Division by zero -->\n",
            "<!-- BUGCATEGORY Logic error -->\n",
            "<!-- BUGFILE xx -->\n",
            "<!-- BUGLINE 5 -->\n",
            "<!-- BUGCOLUMN 22 -->\n",
            "<!-- BUGPATHLENGTH 4 -->\n",
            "<!-- BUGMETAEND -->\n",
            "<!-- REPORTHEADER -->\n",
            "some tails\n",
        ]
        result = run_bug_parse(content)
        self.assertEqual(result["bug_category"], "Logic error")
        self.assertEqual(result["bug_path_length"], 4)
        self.assertEqual(result["bug_line"], 5)
        self.assertEqual(result["bug_description"], "Division by zero")
        self.assertEqual(result["bug_type"], "Division by zero")
        self.assertEqual(result["bug_file"], "xx")

    def test_parse_bug_empty(self):
        content = []
        result = run_bug_parse(content)
        self.assertEqual(result["bug_category"], "Other")
        self.assertEqual(result["bug_path_length"], 1)
        self.assertEqual(result["bug_line"], 0)

    def test_parse_crash(self):
        content = [
            "/some/path/file.c\n",
            "Some very serious Error\n",
            "bla\n",
            "bla-bla\n",
        ]
        result = run_crash_parse(content, "file.i")
        self.assertEqual(result["source"], content[0].rstrip())
        self.assertEqual(result["problem"], content[1].rstrip())
        self.assertEqual(os.path.basename(result["file"]), "file.i")
        self.assertEqual(os.path.basename(result["info"]), "file.i.info.txt")
        self.assertEqual(os.path.basename(result["stderr"]), "file.i.stderr.txt")

    def test_parse_real_crash(self):
        import libscanbuild.analyze as sut2
        import re

        with libear.TemporaryDirectory() as tmpdir:
            filename = os.path.join(tmpdir, "test.c")
            with open(filename, "w") as handle:
                handle.write("int main() { return 0")
            # produce failure report
            opts = {
                "clang": "clang",
                "directory": os.getcwd(),
                "flags": [],
                "file": filename,
                "output_dir": tmpdir,
                "language": "c",
                "error_type": "other_error",
                "error_output": "some output",
                "exit_code": 13,
            }
            sut2.report_failure(opts)
            # find the info file
            pp_file = None
            for root, _, files in os.walk(tmpdir):
                keys = [os.path.join(root, name) for name in files]
                for key in keys:
                    if re.match(r"^(.*/)+clang(.*)\.i$", key):
                        pp_file = key
            self.assertIsNot(pp_file, None)
            # read the failure report back
            result = sut.parse_crash(pp_file + ".info.txt")
            self.assertEqual(result["source"], filename)
            self.assertEqual(result["problem"], "Other Error")
            self.assertEqual(result["file"], pp_file)
            self.assertEqual(result["info"], pp_file + ".info.txt")
            self.assertEqual(result["stderr"], pp_file + ".stderr.txt")


class ReportMethodTest(unittest.TestCase):
    def test_chop(self):
        self.assertEqual("file", sut.chop("/prefix", "/prefix/file"))
        self.assertEqual("file", sut.chop("/prefix/", "/prefix/file"))
        self.assertEqual("lib/file", sut.chop("/prefix/", "/prefix/lib/file"))
        self.assertEqual("/prefix/file", sut.chop("", "/prefix/file"))

    def test_chop_when_cwd(self):
        self.assertEqual("../src/file", sut.chop("/cwd", "/src/file"))
        self.assertEqual("../src/file", sut.chop("/prefix/cwd", "/prefix/src/file"))


class GetPrefixFromCompilationDatabaseTest(unittest.TestCase):
    def test_with_different_filenames(self):
        self.assertEqual(sut.commonprefix(["/tmp/a.c", "/tmp/b.c"]), "/tmp")

    def test_with_different_dirnames(self):
        self.assertEqual(sut.commonprefix(["/tmp/abs/a.c", "/tmp/ack/b.c"]), "/tmp")

    def test_no_common_prefix(self):
        self.assertEqual(sut.commonprefix(["/tmp/abs/a.c", "/usr/ack/b.c"]), "/")

    def test_with_single_file(self):
        self.assertEqual(sut.commonprefix(["/tmp/a.c"]), "/tmp")

    def test_empty(self):
        self.assertEqual(sut.commonprefix([]), "")


class MergeSarifTest(unittest.TestCase):
    def test_merging_sarif(self):
        sarif1 = {
            "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json",
            "runs": [
                {
                    "artifacts": [
                        {
                            "length": 100,
                            "location": {
                                "uri": "//clang/tools/scan-build-py/tests/unit/test_report.py"
                            },
                            "mimeType": "text/plain",
                            "roles": ["resultFile"],
                        }
                    ],
                    "columnKind": "unicodeCodePoints",
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "threadFlows": [
                                        {
                                            "locations": [
                                                {
                                                    "importance": "important",
                                                    "location": {
                                                        "message": {
                                                            "text": "test message 1"
                                                        },
                                                        "physicalLocation": {
                                                            "artifactLocation": {
                                                                "index": 0,
                                                                "uri": "//clang/tools/scan-build-py/tests/unit/test_report.py",
                                                            },
                                                            "region": {
                                                                "endColumn": 5,
                                                                "startColumn": 1,
                                                                "startLine": 2,
                                                            },
                                                        },
                                                    },
                                                }
                                            ]
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            "codeFlows": [
                                {
                                    "threadFlows": [
                                        {
                                            "locations": [
                                                {
                                                    "importance": "important",
                                                    "location": {
                                                        "message": {
                                                            "text": "test message 2"
                                                        },
                                                        "physicalLocation": {
                                                            "artifactLocation": {
                                                                "index": 0,
                                                                "uri": "//clang/tools/scan-build-py/tests/unit/test_report.py",
                                                            },
                                                            "region": {
                                                                "endColumn": 23,
                                                                "startColumn": 9,
                                                                "startLine": 10,
                                                            },
                                                        },
                                                    },
                                                }
                                            ]
                                        }
                                    ]
                                }
                            ]
                        },
                    ],
                    "tool": {
                        "driver": {
                            "fullName": "clang static analyzer",
                            "language": "en-US",
                            "name": "clang",
                            "rules": [
                                {
                                    "fullDescription": {
                                        "text": "test rule for merge sarif test"
                                    },
                                    "helpUrl": "//clang/tools/scan-build-py/tests/unit/test_report.py",
                                    "id": "testId",
                                    "name": "testName",
                                }
                            ],
                            "version": "test clang",
                        }
                    },
                }
            ],
            "version": "2.1.0",
        }
        sarif2 = {
            "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json",
            "runs": [
                {
                    "artifacts": [
                        {
                            "length": 1523,
                            "location": {
                                "uri": "//clang/tools/scan-build-py/tests/unit/test_report.py"
                            },
                            "mimeType": "text/plain",
                            "roles": ["resultFile"],
                        }
                    ],
                    "columnKind": "unicodeCodePoints",
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "threadFlows": [
                                        {
                                            "locations": [
                                                {
                                                    "importance": "important",
                                                    "location": {
                                                        "message": {
                                                            "text": "test message 3"
                                                        },
                                                        "physicalLocation": {
                                                            "artifactLocation": {
                                                                "index": 0,
                                                                "uri": "//clang/tools/scan-build-py/tests/unit/test_report.py",
                                                            },
                                                            "region": {
                                                                "endColumn": 99,
                                                                "startColumn": 99,
                                                                "startLine": 17,
                                                            },
                                                        },
                                                    },
                                                }
                                            ]
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            "codeFlows": [
                                {
                                    "threadFlows": [
                                        {
                                            "locations": [
                                                {
                                                    "importance": "important",
                                                    "location": {
                                                        "message": {
                                                            "text": "test message 4"
                                                        },
                                                        "physicalLocation": {
                                                            "artifactLocation": {
                                                                "index": 0,
                                                                "uri": "//clang/tools/scan-build-py/tests/unit/test_report.py",
                                                            },
                                                            "region": {
                                                                "endColumn": 305,
                                                                "startColumn": 304,
                                                                "startLine": 1,
                                                            },
                                                        },
                                                    },
                                                }
                                            ]
                                        }
                                    ]
                                }
                            ]
                        },
                    ],
                    "tool": {
                        "driver": {
                            "fullName": "clang static analyzer",
                            "language": "en-US",
                            "name": "clang",
                            "rules": [
                                {
                                    "fullDescription": {
                                        "text": "test rule for merge sarif test"
                                    },
                                    "helpUrl": "//clang/tools/scan-build-py/tests/unit/test_report.py",
                                    "id": "testId",
                                    "name": "testName",
                                }
                            ],
                            "version": "test clang",
                        }
                    },
                }
            ],
            "version": "2.1.0",
        }

        contents = [sarif1, sarif2]
        with libear.TemporaryDirectory() as tmpdir:
            for idx, content in enumerate(contents):
                file_name = os.path.join(tmpdir, "results-{}.sarif".format(idx))
                with open(file_name, "w") as handle:
                    json.dump(content, handle)

            sut.merge_sarif_files(tmpdir, sort_files=True)

            self.assertIn("results-merged.sarif", os.listdir(tmpdir))
            with open(os.path.join(tmpdir, "results-merged.sarif")) as f:
                merged = json.load(f)
                self.assertEqual(len(merged["runs"]), 2)
                self.assertEqual(len(merged["runs"][0]["results"]), 2)
                self.assertEqual(len(merged["runs"][1]["results"]), 2)

                expected = sarif1
                for run in sarif2["runs"]:
                    expected["runs"].append(run)

                self.assertEqual(merged, expected)

    def test_merge_updates_embedded_link(self):
        sarif1 = {
            "runs": [
                {
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "message": {
                                        "text": "test message 1-1 [link](sarif:/runs/1/results/0) [link2](sarif:/runs/1/results/0)"
                                    },
                                    "threadFlows": [
                                        {
                                            "message": {
                                                "text": "test message 1-2 [link](sarif:/runs/1/results/0)"
                                            }
                                        }
                                    ],
                                }
                            ]
                        }
                    ]
                },
                {
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "message": {
                                        "text": "test message 2-1 [link](sarif:/runs/0/results/0)"
                                    },
                                    "threadFlows": [
                                        {
                                            "message": {
                                                "text": "test message 2-2 [link](sarif:/runs/0/results/0)"
                                            }
                                        }
                                    ],
                                }
                            ]
                        }
                    ]
                },
            ]
        }
        sarif2 = {
            "runs": [
                {
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "message": {
                                        "text": "test message 3-1 [link](sarif:/runs/1/results/0) [link2](sarif:/runs/1/results/0)"
                                    },
                                    "threadFlows": [
                                        {
                                            "message": {
                                                "text": "test message 3-2 [link](sarif:/runs/1/results/0)"
                                            }
                                        }
                                    ],
                                }
                            ]
                        }
                    ],
                },
                {
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "message": {
                                        "text": "test message 4-1 [link](sarif:/runs/0/results/0)"
                                    },
                                    "threadFlows": [
                                        {
                                            "message": {
                                                "text": "test message 4-2 [link](sarif:/runs/0/results/0)"
                                            }
                                        }
                                    ],
                                }
                            ]
                        }
                    ]
                },
            ]
        }
        sarif3 = {
            "runs": [
                {
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "message": {
                                        "text": "test message 5-1 [link](sarif:/runs/1/results/0) [link2](sarif:/runs/1/results/0)"
                                    },
                                    "threadFlows": [
                                        {
                                            "message": {
                                                "text": "test message 5-2 [link](sarif:/runs/1/results/0)"
                                            }
                                        }
                                    ],
                                }
                            ]
                        }
                    ],
                },
                {
                    "results": [
                        {
                            "codeFlows": [
                                {
                                    "message": {
                                        "text": "test message 6-1 [link](sarif:/runs/0/results/0)"
                                    },
                                    "threadFlows": [
                                        {
                                            "message": {
                                                "text": "test message 6-2 [link](sarif:/runs/0/results/0)"
                                            }
                                        }
                                    ],
                                }
                            ]
                        }
                    ]
                },
            ]
        }

        contents = [sarif1, sarif2, sarif3]

        with libear.TemporaryDirectory() as tmpdir:
            for idx, content in enumerate(contents):
                file_name = os.path.join(tmpdir, "results-{}.sarif".format(idx))
                with open(file_name, "w") as handle:
                    json.dump(content, handle)

            sut.merge_sarif_files(tmpdir, sort_files=True)

            self.assertIn("results-merged.sarif", os.listdir(tmpdir))
            with open(os.path.join(tmpdir, "results-merged.sarif")) as f:
                merged = json.load(f)
                self.assertEqual(len(merged["runs"]), 6)

                code_flows = [
                    merged["runs"][x]["results"][0]["codeFlows"][0]["message"]["text"]
                    for x in range(6)
                ]
                thread_flows = [
                    merged["runs"][x]["results"][0]["codeFlows"][0]["threadFlows"][0][
                        "message"
                    ]["text"]
                    for x in range(6)
                ]

                # The run index should be updated for the second and third sets of runs
                self.assertEqual(
                    code_flows,
                    [
                        "test message 1-1 [link](sarif:/runs/1/results/0) [link2](sarif:/runs/1/results/0)",
                        "test message 2-1 [link](sarif:/runs/0/results/0)",
                        "test message 3-1 [link](sarif:/runs/3/results/0) [link2](sarif:/runs/3/results/0)",
                        "test message 4-1 [link](sarif:/runs/2/results/0)",
                        "test message 5-1 [link](sarif:/runs/5/results/0) [link2](sarif:/runs/5/results/0)",
                        "test message 6-1 [link](sarif:/runs/4/results/0)",
                    ],
                )
                self.assertEquals(
                    thread_flows,
                    [
                        "test message 1-2 [link](sarif:/runs/1/results/0)",
                        "test message 2-2 [link](sarif:/runs/0/results/0)",
                        "test message 3-2 [link](sarif:/runs/3/results/0)",
                        "test message 4-2 [link](sarif:/runs/2/results/0)",
                        "test message 5-2 [link](sarif:/runs/5/results/0)",
                        "test message 6-2 [link](sarif:/runs/4/results/0)",
                    ],
                )

    def test_overflow_run_count(self):
        sarif1 = {
            "runs": [
                {
                    "results": [
                        {"message": {"text": "run 1-0 [link](sarif:/runs/1/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-1 [link](sarif:/runs/2/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-2 [link](sarif:/runs/3/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-3 [link](sarif:/runs/4/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-4 [link](sarif:/runs/5/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-5 [link](sarif:/runs/6/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-6 [link](sarif:/runs/7/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-7 [link](sarif:/runs/8/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-8 [link](sarif:/runs/9/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 1-9 [link](sarif:/runs/0/results/0)"}}
                    ]
                },
            ]
        }
        sarif2 = {
            "runs": [
                {
                    "results": [
                        {
                            "message": {
                                "text": "run 2-0 [link](sarif:/runs/1/results/0) [link2](sarif:/runs/2/results/0)"
                            }
                        }
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-1 [link](sarif:/runs/2/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-2 [link](sarif:/runs/3/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-3 [link](sarif:/runs/4/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-4 [link](sarif:/runs/5/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-5 [link](sarif:/runs/6/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-6 [link](sarif:/runs/7/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-7 [link](sarif:/runs/8/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-8 [link](sarif:/runs/9/results/0)"}}
                    ]
                },
                {
                    "results": [
                        {"message": {"text": "run 2-9 [link](sarif:/runs/0/results/0)"}}
                    ]
                },
            ]
        }

        contents = [sarif1, sarif2]
        with libear.TemporaryDirectory() as tmpdir:
            for idx, content in enumerate(contents):
                file_name = os.path.join(tmpdir, "results-{}.sarif".format(idx))
                with open(file_name, "w") as handle:
                    json.dump(content, handle)

            sut.merge_sarif_files(tmpdir, sort_files=True)

            self.assertIn("results-merged.sarif", os.listdir(tmpdir))
            with open(os.path.join(tmpdir, "results-merged.sarif")) as f:
                merged = json.load(f)
                self.assertEqual(len(merged["runs"]), 20)

                messages = [
                    merged["runs"][x]["results"][0]["message"]["text"]
                    for x in range(20)
                ]
                self.assertEqual(
                    messages,
                    [
                        "run 1-0 [link](sarif:/runs/1/results/0)",
                        "run 1-1 [link](sarif:/runs/2/results/0)",
                        "run 1-2 [link](sarif:/runs/3/results/0)",
                        "run 1-3 [link](sarif:/runs/4/results/0)",
                        "run 1-4 [link](sarif:/runs/5/results/0)",
                        "run 1-5 [link](sarif:/runs/6/results/0)",
                        "run 1-6 [link](sarif:/runs/7/results/0)",
                        "run 1-7 [link](sarif:/runs/8/results/0)",
                        "run 1-8 [link](sarif:/runs/9/results/0)",
                        "run 1-9 [link](sarif:/runs/0/results/0)",
                        "run 2-0 [link](sarif:/runs/11/results/0) [link2](sarif:/runs/12/results/0)",
                        "run 2-1 [link](sarif:/runs/12/results/0)",
                        "run 2-2 [link](sarif:/runs/13/results/0)",
                        "run 2-3 [link](sarif:/runs/14/results/0)",
                        "run 2-4 [link](sarif:/runs/15/results/0)",
                        "run 2-5 [link](sarif:/runs/16/results/0)",
                        "run 2-6 [link](sarif:/runs/17/results/0)",
                        "run 2-7 [link](sarif:/runs/18/results/0)",
                        "run 2-8 [link](sarif:/runs/19/results/0)",
                        "run 2-9 [link](sarif:/runs/10/results/0)",
                    ],
                )
