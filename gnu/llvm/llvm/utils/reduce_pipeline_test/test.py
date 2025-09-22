#!/usr/bin/env python3

# Automatically formatted with yapf (https://github.com/google/yapf)

import subprocess
import unittest


def getFinalPasses(run):
    stdout = run.stdout.decode()
    stdout = stdout[: stdout.rfind("\n")]
    stdout = stdout[stdout.rfind("\n") + 1 :]
    return stdout


class Test(unittest.TestCase):
    def test_0(self):
        """Test all passes are removed except those required to crash. Verify
        that PM structure is intact."""
        run_args = [
            "./utils/reduce_pipeline.py",
            "--opt-binary=./utils/reduce_pipeline_test/fake_opt.py",
            "--input=/dev/null",
            "--passes=a,b,c,A(d,B(e,f),g),h,i",
            "-crash-seq=b,d,f",
        ]
        run = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(getFinalPasses(run), '-passes="b,A(d,B(f))"')

    def test_1(self):
        """Test all passes are removed except those required to crash. The
        required passes in this case are the first and last in that order
        (a bit of a corner-case for the reduction algorithm)."""
        run_args = [
            "./utils/reduce_pipeline.py",
            "--opt-binary=./utils/reduce_pipeline_test/fake_opt.py",
            "--input=/dev/null",
            "--passes=a,b,c,A(d,B(e,f),g),h,i",
            "-crash-seq=a,i",
        ]
        run = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(getFinalPasses(run), '-passes="a,i"')

    def test_2_0(self):
        """Test expansion of EXPAND_a_to_f (expands into 'a,b,c,d,e,f')."""
        run_args = [
            "./utils/reduce_pipeline.py",
            "--opt-binary=./utils/reduce_pipeline_test/fake_opt.py",
            "--input=/dev/null",
            "--passes=EXPAND_a_to_f",
            "-crash-seq=b,e",
        ]
        run = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(getFinalPasses(run), '-passes="b,e"')

    def test_2_1(self):
        """Test EXPAND_a_to_f and the '--dont-expand-passes' option."""
        run_args = [
            "./utils/reduce_pipeline.py",
            "--opt-binary=./utils/reduce_pipeline_test/fake_opt.py",
            "--input=/dev/null",
            "--passes=EXPAND_a_to_f",
            "-crash-seq=EXPAND_a_to_f",
            "--dont-expand-passes",
        ]
        run = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(getFinalPasses(run), '-passes="EXPAND_a_to_f"')

    def test_3(self):
        """Test that empty pass-managers get removed by default."""
        run_args = [
            "./utils/reduce_pipeline.py",
            "--opt-binary=./utils/reduce_pipeline_test/fake_opt.py",
            "--input=/dev/null",
            "--passes=a,b,c,A(d,B(e,f),g),h,i",
            "-crash-seq=b,d,h",
        ]
        run = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(getFinalPasses(run), '-passes="b,A(d),h"')

    def test_4(self):
        """Test the '--dont-remove-empty-pm' option."""
        run_args = [
            "./utils/reduce_pipeline.py",
            "--opt-binary=./utils/reduce_pipeline_test/fake_opt.py",
            "--input=/dev/null",
            "--passes=a,b,c,A(d,B(e,f),g),h,i",
            "-crash-seq=b,d,h",
            "--dont-remove-empty-pm",
        ]
        run = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(getFinalPasses(run), '-passes="b,A(d,B()),h"')


unittest.main()
exit(0)
