#!/usr/bin/env python

"""Test internal functions within check_cfc.py."""

import check_cfc
import os
import platform
import unittest


class TestCheckCFC(unittest.TestCase):
    def test_flip_dash_g(self):
        self.assertIn("-g", check_cfc.flip_dash_g(["clang", "-c"]))
        self.assertNotIn("-g", check_cfc.flip_dash_g(["clang", "-c", "-g"]))
        self.assertNotIn("-g", check_cfc.flip_dash_g(["clang", "-g", "-c", "-g"]))

    def test_remove_dir_from_path(self):
        bin_path = r"/usr/bin"
        space_path = r"/home/user/space in path"
        superstring_path = r"/usr/bin/local"

        # Test removing last thing in path
        self.assertNotIn(bin_path, check_cfc.remove_dir_from_path(bin_path, bin_path))

        # Test removing one entry and leaving others
        # Also tests removing repeated path
        path_var = os.pathsep.join([superstring_path, bin_path, space_path, bin_path])
        stripped_path_var = check_cfc.remove_dir_from_path(path_var, bin_path)
        self.assertIn(superstring_path, stripped_path_var)
        self.assertNotIn(bin_path, stripped_path_var.split(os.pathsep))
        self.assertIn(space_path, stripped_path_var)

        # Test removing non-canonical path
        self.assertNotIn(
            r"/usr//bin", check_cfc.remove_dir_from_path(r"/usr//bin", bin_path)
        )

        if platform == "Windows":
            # Windows is case insensitive so should remove a different case
            # path
            self.assertNotIn(
                bin_path, check_cfc.remove_dir_from_path(path_var, r"/USR/BIN")
            )
        else:
            # Case sensitive so will not remove different case path
            self.assertIn(
                bin_path, check_cfc.remove_dir_from_path(path_var, r"/USR/BIN")
            )

    def test_is_output_specified(self):
        self.assertTrue(check_cfc.is_output_specified(["clang", "-o", "test.o"]))
        self.assertTrue(check_cfc.is_output_specified(["clang", "-otest.o"]))
        self.assertFalse(check_cfc.is_output_specified(["clang", "-gline-tables-only"]))
        # Not specified for implied output file name
        self.assertFalse(check_cfc.is_output_specified(["clang", "test.c"]))

    def test_get_output_file(self):
        self.assertEqual(check_cfc.get_output_file(["clang", "-o", "test.o"]), "test.o")
        self.assertEqual(check_cfc.get_output_file(["clang", "-otest.o"]), "test.o")
        self.assertIsNone(check_cfc.get_output_file(["clang", "-gline-tables-only"]))
        # Can't get output file if more than one input file
        self.assertIsNone(
            check_cfc.get_output_file(["clang", "-c", "test.cpp", "test2.cpp"])
        )
        # No output file specified
        self.assertIsNone(check_cfc.get_output_file(["clang", "-c", "test.c"]))

    def test_derive_output_file(self):
        # Test getting implicit output file
        self.assertEqual(
            check_cfc.derive_output_file(["clang", "-c", "test.c"]), "test.o"
        )
        self.assertEqual(
            check_cfc.derive_output_file(["clang", "-c", "test.cpp"]), "test.o"
        )
        self.assertIsNone(check_cfc.derive_output_file(["clang", "--version"]))

    def test_is_normal_compile(self):
        self.assertTrue(
            check_cfc.is_normal_compile(["clang", "-c", "test.cpp", "-o", "test2.o"])
        )
        self.assertTrue(check_cfc.is_normal_compile(["clang", "-c", "test.cpp"]))
        # Outputting bitcode is not a normal compile
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "test.cpp", "-flto"])
        )
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "test.cpp", "-emit-llvm"])
        )
        # Outputting preprocessed output or assembly is not a normal compile
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-E", "test.cpp", "-o", "test.ii"])
        )
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-S", "test.cpp", "-o", "test.s"])
        )
        # Input of preprocessed or assembly is not a "normal compile"
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "test.s", "-o", "test.o"])
        )
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "test.ii", "-o", "test.o"])
        )
        # Specifying --version and -c is not a normal compile
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "test.cpp", "--version"])
        )
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "test.cpp", "--help"])
        )
        # Outputting dependency files is not a normal compile
        self.assertFalse(check_cfc.is_normal_compile(["clang", "-c", "-M", "test.cpp"]))
        self.assertFalse(
            check_cfc.is_normal_compile(["clang", "-c", "-MM", "test.cpp"])
        )
        # Creating a dependency file as a side effect still outputs an object file
        self.assertTrue(check_cfc.is_normal_compile(["clang", "-c", "-MD", "test.cpp"]))
        self.assertTrue(
            check_cfc.is_normal_compile(["clang", "-c", "-MMD", "test.cpp"])
        )

    def test_replace_output_file(self):
        self.assertEqual(
            check_cfc.replace_output_file(["clang", "-o", "test.o"], "testg.o"),
            ["clang", "-o", "testg.o"],
        )
        self.assertEqual(
            check_cfc.replace_output_file(["clang", "-otest.o"], "testg.o"),
            ["clang", "-otestg.o"],
        )
        with self.assertRaises(Exception):
            check_cfc.replace_output_file(["clang"], "testg.o")

    def test_add_output_file(self):
        self.assertEqual(
            check_cfc.add_output_file(["clang"], "testg.o"), ["clang", "-o", "testg.o"]
        )

    def test_set_output_file(self):
        # Test output not specified
        self.assertEqual(
            check_cfc.set_output_file(["clang"], "test.o"), ["clang", "-o", "test.o"]
        )
        # Test output is specified
        self.assertEqual(
            check_cfc.set_output_file(["clang", "-o", "test.o"], "testb.o"),
            ["clang", "-o", "testb.o"],
        )

    def test_get_input_file(self):
        # No input file
        self.assertIsNone(check_cfc.get_input_file(["clang"]))
        # Input C file
        self.assertEqual(check_cfc.get_input_file(["clang", "test.c"]), "test.c")
        # Input C++ file
        self.assertEqual(check_cfc.get_input_file(["clang", "test.cpp"]), "test.cpp")
        # Multiple input files
        self.assertIsNone(check_cfc.get_input_file(["clang", "test.c", "test2.cpp"]))
        self.assertIsNone(check_cfc.get_input_file(["clang", "test.c", "test2.c"]))
        # Don't handle preprocessed files
        self.assertIsNone(check_cfc.get_input_file(["clang", "test.i"]))
        self.assertIsNone(check_cfc.get_input_file(["clang", "test.ii"]))
        # Test identifying input file with quotes
        self.assertEqual(check_cfc.get_input_file(["clang", '"test.c"']), '"test.c"')
        self.assertEqual(check_cfc.get_input_file(["clang", "'test.c'"]), "'test.c'")
        # Test multiple quotes
        self.assertEqual(
            check_cfc.get_input_file(["clang", "\"'test.c'\""]), "\"'test.c'\""
        )

    def test_set_input_file(self):
        self.assertEqual(
            check_cfc.set_input_file(["clang", "test.c"], "test.s"), ["clang", "test.s"]
        )


if __name__ == "__main__":
    unittest.main()
