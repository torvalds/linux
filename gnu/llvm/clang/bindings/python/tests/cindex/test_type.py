import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

import gc
import unittest

from clang.cindex import CursorKind
from clang.cindex import TranslationUnit
from clang.cindex import TypeKind
from .util import get_cursor
from .util import get_tu


kInput = """\

typedef int I;

struct teststruct {
  int a;
  I b;
  long c;
  unsigned long d;
  signed long e;
  const int f;
  int *g;
  int ***h;
};

"""


constarrayInput = """
struct teststruct {
  void *A[2];
};
"""


class TestType(unittest.TestCase):
    def test_a_struct(self):
        tu = get_tu(kInput)

        teststruct = get_cursor(tu, "teststruct")
        self.assertIsNotNone(teststruct, "Could not find teststruct.")
        fields = list(teststruct.get_children())

        self.assertEqual(fields[0].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[0].translation_unit)
        self.assertEqual(fields[0].spelling, "a")
        self.assertFalse(fields[0].type.is_const_qualified())
        self.assertEqual(fields[0].type.kind, TypeKind.INT)
        self.assertEqual(fields[0].type.get_canonical().kind, TypeKind.INT)
        self.assertEqual(fields[0].type.get_typedef_name(), "")

        self.assertEqual(fields[1].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[1].translation_unit)
        self.assertEqual(fields[1].spelling, "b")
        self.assertFalse(fields[1].type.is_const_qualified())
        self.assertEqual(fields[1].type.kind, TypeKind.ELABORATED)
        self.assertEqual(fields[1].type.get_canonical().kind, TypeKind.INT)
        self.assertEqual(fields[1].type.get_declaration().spelling, "I")
        self.assertEqual(fields[1].type.get_typedef_name(), "I")

        self.assertEqual(fields[2].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[2].translation_unit)
        self.assertEqual(fields[2].spelling, "c")
        self.assertFalse(fields[2].type.is_const_qualified())
        self.assertEqual(fields[2].type.kind, TypeKind.LONG)
        self.assertEqual(fields[2].type.get_canonical().kind, TypeKind.LONG)
        self.assertEqual(fields[2].type.get_typedef_name(), "")

        self.assertEqual(fields[3].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[3].translation_unit)
        self.assertEqual(fields[3].spelling, "d")
        self.assertFalse(fields[3].type.is_const_qualified())
        self.assertEqual(fields[3].type.kind, TypeKind.ULONG)
        self.assertEqual(fields[3].type.get_canonical().kind, TypeKind.ULONG)
        self.assertEqual(fields[3].type.get_typedef_name(), "")

        self.assertEqual(fields[4].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[4].translation_unit)
        self.assertEqual(fields[4].spelling, "e")
        self.assertFalse(fields[4].type.is_const_qualified())
        self.assertEqual(fields[4].type.kind, TypeKind.LONG)
        self.assertEqual(fields[4].type.get_canonical().kind, TypeKind.LONG)
        self.assertEqual(fields[4].type.get_typedef_name(), "")

        self.assertEqual(fields[5].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[5].translation_unit)
        self.assertEqual(fields[5].spelling, "f")
        self.assertTrue(fields[5].type.is_const_qualified())
        self.assertEqual(fields[5].type.kind, TypeKind.INT)
        self.assertEqual(fields[5].type.get_canonical().kind, TypeKind.INT)
        self.assertEqual(fields[5].type.get_typedef_name(), "")

        self.assertEqual(fields[6].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[6].translation_unit)
        self.assertEqual(fields[6].spelling, "g")
        self.assertFalse(fields[6].type.is_const_qualified())
        self.assertEqual(fields[6].type.kind, TypeKind.POINTER)
        self.assertEqual(fields[6].type.get_pointee().kind, TypeKind.INT)
        self.assertEqual(fields[6].type.get_typedef_name(), "")

        self.assertEqual(fields[7].kind, CursorKind.FIELD_DECL)
        self.assertIsNotNone(fields[7].translation_unit)
        self.assertEqual(fields[7].spelling, "h")
        self.assertFalse(fields[7].type.is_const_qualified())
        self.assertEqual(fields[7].type.kind, TypeKind.POINTER)
        self.assertEqual(fields[7].type.get_pointee().kind, TypeKind.POINTER)
        self.assertEqual(
            fields[7].type.get_pointee().get_pointee().kind, TypeKind.POINTER
        )
        self.assertEqual(
            fields[7].type.get_pointee().get_pointee().get_pointee().kind, TypeKind.INT
        )
        self.assertEqual(fields[7].type.get_typedef_name(), "")

    def test_references(self):
        """Ensure that a Type maintains a reference to a TranslationUnit."""

        tu = get_tu("int x;")
        children = list(tu.cursor.get_children())
        self.assertGreater(len(children), 0)

        cursor = children[0]
        t = cursor.type

        self.assertIsInstance(t.translation_unit, TranslationUnit)

        # Delete main TranslationUnit reference and force a GC.
        del tu
        gc.collect()
        self.assertIsInstance(t.translation_unit, TranslationUnit)

        # If the TU was destroyed, this should cause a segfault.
        decl = t.get_declaration()

    def testConstantArray(self):
        tu = get_tu(constarrayInput)

        teststruct = get_cursor(tu, "teststruct")
        self.assertIsNotNone(teststruct, "Didn't find teststruct??")
        fields = list(teststruct.get_children())
        self.assertEqual(fields[0].spelling, "A")
        self.assertEqual(fields[0].type.kind, TypeKind.CONSTANTARRAY)
        self.assertIsNotNone(fields[0].type.get_array_element_type())
        self.assertEqual(fields[0].type.get_array_element_type().kind, TypeKind.POINTER)
        self.assertEqual(fields[0].type.get_array_size(), 2)

    def test_equal(self):
        """Ensure equivalence operators work on Type."""
        source = "int a; int b; void *v;"
        tu = get_tu(source)

        a = get_cursor(tu, "a")
        b = get_cursor(tu, "b")
        v = get_cursor(tu, "v")

        self.assertIsNotNone(a)
        self.assertIsNotNone(b)
        self.assertIsNotNone(v)

        self.assertEqual(a.type, b.type)
        self.assertNotEqual(a.type, v.type)

        self.assertNotEqual(a.type, None)
        self.assertNotEqual(a.type, "foo")

    def test_type_spelling(self):
        """Ensure Type.spelling works."""
        tu = get_tu("int c[5]; void f(int i[]); int x; int v[x];")
        c = get_cursor(tu, "c")
        i = get_cursor(tu, "i")
        x = get_cursor(tu, "x")
        v = get_cursor(tu, "v")
        self.assertIsNotNone(c)
        self.assertIsNotNone(i)
        self.assertIsNotNone(x)
        self.assertIsNotNone(v)
        self.assertEqual(c.type.spelling, "int[5]")
        self.assertEqual(i.type.spelling, "int[]")
        self.assertEqual(x.type.spelling, "int")
        self.assertEqual(v.type.spelling, "int[x]")

    def test_typekind_spelling(self):
        """Ensure TypeKind.spelling works."""
        tu = get_tu("int a;")
        a = get_cursor(tu, "a")

        self.assertIsNotNone(a)
        self.assertEqual(a.type.kind.spelling, "Int")

    def test_function_argument_types(self):
        """Ensure that Type.argument_types() works as expected."""
        tu = get_tu("void f(int, int);")
        f = get_cursor(tu, "f")
        self.assertIsNotNone(f)

        args = f.type.argument_types()
        self.assertIsNotNone(args)
        self.assertEqual(len(args), 2)

        t0 = args[0]
        self.assertIsNotNone(t0)
        self.assertEqual(t0.kind, TypeKind.INT)

        t1 = args[1]
        self.assertIsNotNone(t1)
        self.assertEqual(t1.kind, TypeKind.INT)

        args2 = list(args)
        self.assertEqual(len(args2), 2)
        self.assertEqual(t0, args2[0])
        self.assertEqual(t1, args2[1])

    def test_argument_types_string_key(self):
        """Ensure that non-int keys raise a TypeError."""
        tu = get_tu("void f(int, int);")
        f = get_cursor(tu, "f")
        self.assertIsNotNone(f)

        args = f.type.argument_types()
        self.assertEqual(len(args), 2)

        with self.assertRaises(TypeError):
            args["foo"]

    def test_argument_types_negative_index(self):
        """Ensure that negative indexes on argument_types Raises an IndexError."""
        tu = get_tu("void f(int, int);")
        f = get_cursor(tu, "f")
        args = f.type.argument_types()

        with self.assertRaises(IndexError):
            args[-1]

    def test_argument_types_overflow_index(self):
        """Ensure that indexes beyond the length of Type.argument_types() raise."""
        tu = get_tu("void f(int, int);")
        f = get_cursor(tu, "f")
        args = f.type.argument_types()

        with self.assertRaises(IndexError):
            args[2]

    def test_argument_types_invalid_type(self):
        """Ensure that obtaining argument_types on a Type without them raises."""
        tu = get_tu("int i;")
        i = get_cursor(tu, "i")
        self.assertIsNotNone(i)

        with self.assertRaises(Exception):
            i.type.argument_types()

    def test_is_pod(self):
        """Ensure Type.is_pod() works."""
        tu = get_tu("int i; void f();")
        i = get_cursor(tu, "i")
        f = get_cursor(tu, "f")

        self.assertIsNotNone(i)
        self.assertIsNotNone(f)

        self.assertTrue(i.type.is_pod())
        self.assertFalse(f.type.is_pod())

    def test_function_variadic(self):
        """Ensure Type.is_function_variadic works."""

        source = """
#include <stdarg.h>

    void foo(int a, ...);
    void bar(int a, int b);
    """

        tu = get_tu(source)
        foo = get_cursor(tu, "foo")
        bar = get_cursor(tu, "bar")

        self.assertIsNotNone(foo)
        self.assertIsNotNone(bar)

        self.assertIsInstance(foo.type.is_function_variadic(), bool)
        self.assertTrue(foo.type.is_function_variadic())
        self.assertFalse(bar.type.is_function_variadic())

    def test_element_type(self):
        """Ensure Type.element_type works."""
        tu = get_tu("int c[5]; void f(int i[]); int x; int v[x];")
        c = get_cursor(tu, "c")
        i = get_cursor(tu, "i")
        v = get_cursor(tu, "v")
        self.assertIsNotNone(c)
        self.assertIsNotNone(i)
        self.assertIsNotNone(v)

        self.assertEqual(c.type.kind, TypeKind.CONSTANTARRAY)
        self.assertEqual(c.type.element_type.kind, TypeKind.INT)
        self.assertEqual(i.type.kind, TypeKind.INCOMPLETEARRAY)
        self.assertEqual(i.type.element_type.kind, TypeKind.INT)
        self.assertEqual(v.type.kind, TypeKind.VARIABLEARRAY)
        self.assertEqual(v.type.element_type.kind, TypeKind.INT)

    def test_invalid_element_type(self):
        """Ensure Type.element_type raises if type doesn't have elements."""
        tu = get_tu("int i;")
        i = get_cursor(tu, "i")
        self.assertIsNotNone(i)
        with self.assertRaises(Exception):
            i.element_type

    def test_element_count(self):
        """Ensure Type.element_count works."""
        tu = get_tu("int i[5]; int j;")
        i = get_cursor(tu, "i")
        j = get_cursor(tu, "j")

        self.assertIsNotNone(i)
        self.assertIsNotNone(j)

        self.assertEqual(i.type.element_count, 5)

        with self.assertRaises(Exception):
            j.type.element_count

    def test_is_volatile_qualified(self):
        """Ensure Type.is_volatile_qualified works."""

        tu = get_tu("volatile int i = 4; int j = 2;")

        i = get_cursor(tu, "i")
        j = get_cursor(tu, "j")

        self.assertIsNotNone(i)
        self.assertIsNotNone(j)

        self.assertIsInstance(i.type.is_volatile_qualified(), bool)
        self.assertTrue(i.type.is_volatile_qualified())
        self.assertFalse(j.type.is_volatile_qualified())

    def test_is_restrict_qualified(self):
        """Ensure Type.is_restrict_qualified works."""

        tu = get_tu("struct s { void * restrict i; void * j; };")

        i = get_cursor(tu, "i")
        j = get_cursor(tu, "j")

        self.assertIsNotNone(i)
        self.assertIsNotNone(j)

        self.assertIsInstance(i.type.is_restrict_qualified(), bool)
        self.assertTrue(i.type.is_restrict_qualified())
        self.assertFalse(j.type.is_restrict_qualified())

    def test_record_layout(self):
        """Ensure Cursor.type.get_size, Cursor.type.get_align and
        Cursor.type.get_offset works."""

        source = """
    struct a {
        long a1;
        long a2:3;
        long a3:4;
        long long a4;
    };
    """
        tries = [
            (["-target", "i386-linux-gnu"], (4, 16, 0, 32, 35, 64)),
            (["-target", "nvptx64-unknown-unknown"], (8, 24, 0, 64, 67, 128)),
            (["-target", "i386-pc-win32"], (8, 16, 0, 32, 35, 64)),
            (["-target", "msp430-none-none"], (2, 14, 0, 32, 35, 48)),
        ]
        for flags, values in tries:
            align, total, a1, a2, a3, a4 = values

            tu = get_tu(source, flags=flags)
            teststruct = get_cursor(tu, "a")
            fields = list(teststruct.get_children())

            self.assertEqual(teststruct.type.get_align(), align)
            self.assertEqual(teststruct.type.get_size(), total)
            self.assertEqual(teststruct.type.get_offset(fields[0].spelling), a1)
            self.assertEqual(teststruct.type.get_offset(fields[1].spelling), a2)
            self.assertEqual(teststruct.type.get_offset(fields[2].spelling), a3)
            self.assertEqual(teststruct.type.get_offset(fields[3].spelling), a4)
            self.assertEqual(fields[0].is_bitfield(), False)
            self.assertEqual(fields[1].is_bitfield(), True)
            self.assertEqual(fields[1].get_bitfield_width(), 3)
            self.assertEqual(fields[2].is_bitfield(), True)
            self.assertEqual(fields[2].get_bitfield_width(), 4)
            self.assertEqual(fields[3].is_bitfield(), False)

    def test_offset(self):
        """Ensure Cursor.get_record_field_offset works in anonymous records"""
        source = """
    struct Test {
      struct {int a;} typeanon;
      struct {
        int bariton;
        union {
          int foo;
        };
      };
      int bar;
    };"""
        tries = [
            (["-target", "i386-linux-gnu"], (4, 16, 0, 32, 64, 96)),
            (["-target", "nvptx64-unknown-unknown"], (8, 24, 0, 32, 64, 96)),
            (["-target", "i386-pc-win32"], (8, 16, 0, 32, 64, 96)),
            (["-target", "msp430-none-none"], (2, 14, 0, 32, 64, 96)),
        ]
        for flags, values in tries:
            align, total, f1, bariton, foo, bar = values
            tu = get_tu(source)
            teststruct = get_cursor(tu, "Test")
            children = list(teststruct.get_children())
            fields = list(teststruct.type.get_fields())
            self.assertEqual(children[0].kind, CursorKind.STRUCT_DECL)
            self.assertNotEqual(children[0].spelling, "typeanon")
            self.assertEqual(children[1].spelling, "typeanon")
            self.assertEqual(fields[0].kind, CursorKind.FIELD_DECL)
            self.assertEqual(fields[1].kind, CursorKind.FIELD_DECL)
            self.assertTrue(fields[1].is_anonymous())
            self.assertEqual(teststruct.type.get_offset("typeanon"), f1)
            self.assertEqual(teststruct.type.get_offset("bariton"), bariton)
            self.assertEqual(teststruct.type.get_offset("foo"), foo)
            self.assertEqual(teststruct.type.get_offset("bar"), bar)

    def test_decay(self):
        """Ensure decayed types are handled as the original type"""

        tu = get_tu("void foo(int a[]);")
        foo = get_cursor(tu, "foo")
        a = foo.type.argument_types()[0]

        self.assertEqual(a.kind, TypeKind.INCOMPLETEARRAY)
        self.assertEqual(a.element_type.kind, TypeKind.INT)
        self.assertEqual(a.get_canonical().kind, TypeKind.INCOMPLETEARRAY)

    def test_addrspace(self):
        """Ensure the address space can be queried"""
        tu = get_tu("__attribute__((address_space(2))) int testInteger = 3;", "c")

        testInteger = get_cursor(tu, "testInteger")

        self.assertIsNotNone(testInteger, "Could not find testInteger.")
        self.assertEqual(testInteger.type.get_address_space(), 2)

    def test_template_arguments(self):
        source = """
        class Foo {
        };
        template <typename T>
        class Template {
        };
        Template<Foo> instance;
        int bar;
        """
        tu = get_tu(source, lang="cpp")

        # Varible with a template argument.
        cursor = get_cursor(tu, "instance")
        cursor_type = cursor.type
        self.assertEqual(cursor.kind, CursorKind.VAR_DECL)
        self.assertEqual(cursor_type.spelling, "Template<Foo>")
        self.assertEqual(cursor_type.get_num_template_arguments(), 1)
        template_type = cursor_type.get_template_argument_type(0)
        self.assertEqual(template_type.spelling, "Foo")

        # Variable without a template argument.
        cursor = get_cursor(tu, "bar")
        self.assertEqual(cursor.get_num_template_arguments(), -1)
