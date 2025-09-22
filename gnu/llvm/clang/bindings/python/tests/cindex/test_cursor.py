import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

import ctypes
import gc
import unittest

from clang.cindex import AvailabilityKind
from clang.cindex import CursorKind
from clang.cindex import TemplateArgumentKind
from clang.cindex import TranslationUnit
from clang.cindex import TypeKind
from clang.cindex import BinaryOperator
from .util import get_cursor
from .util import get_cursors
from .util import get_tu


kInput = """\
struct s0 {
  int a;
  int b;
};

struct s1;

void f0(int a0, int a1) {
  int l0, l1;

  if (a0)
    return;

  for (;;) {
    break;
  }
}
"""

kParentTest = """\
        class C {
            void f();
        }

        void C::f() { }
    """

kTemplateArgTest = """\
        template <int kInt, typename T, bool kBool>
        void foo();

        template<>
        void foo<-7, float, true>();
    """

kBinops = """\
struct C {
   int m;
 };

 void func(void){
   int a, b;
   int C::* p = &C::

   C c;
   c.*p;

   C* pc;
   pc->*p;

   a * b;
   a / b;
   a % b;
   a + b;
   a - b;

   a << b;
   a >> b;

   a < b;
   a > b;

   a <= b;
   a >= b;
   a == b;
   a != b;

   a & b;
   a ^ b;
   a | b;

   a && b;
   a || b;

   a = b;

   a *= b;
   a /= b;
   a %= b;
   a += b;
   a -= b;

   a <<= b;
   a >>= b;

   a &= b;
   a ^= b;
   a |= b;
   a , b;

 }
 """


class TestCursor(unittest.TestCase):
    def test_get_children(self):
        tu = get_tu(kInput)

        it = tu.cursor.get_children()
        tu_nodes = list(it)

        self.assertEqual(len(tu_nodes), 3)
        for cursor in tu_nodes:
            self.assertIsNotNone(cursor.translation_unit)

        self.assertNotEqual(tu_nodes[0], tu_nodes[1])
        self.assertEqual(tu_nodes[0].kind, CursorKind.STRUCT_DECL)
        self.assertEqual(tu_nodes[0].spelling, "s0")
        self.assertEqual(tu_nodes[0].is_definition(), True)
        self.assertEqual(tu_nodes[0].location.file.name, "t.c")
        self.assertEqual(tu_nodes[0].location.line, 1)
        self.assertEqual(tu_nodes[0].location.column, 8)
        self.assertGreater(tu_nodes[0].hash, 0)
        self.assertIsNotNone(tu_nodes[0].translation_unit)

        s0_nodes = list(tu_nodes[0].get_children())
        self.assertEqual(len(s0_nodes), 2)
        self.assertEqual(s0_nodes[0].kind, CursorKind.FIELD_DECL)
        self.assertEqual(s0_nodes[0].spelling, "a")
        self.assertEqual(s0_nodes[0].type.kind, TypeKind.INT)
        self.assertEqual(s0_nodes[1].kind, CursorKind.FIELD_DECL)
        self.assertEqual(s0_nodes[1].spelling, "b")
        self.assertEqual(s0_nodes[1].type.kind, TypeKind.INT)

        self.assertEqual(tu_nodes[1].kind, CursorKind.STRUCT_DECL)
        self.assertEqual(tu_nodes[1].spelling, "s1")
        self.assertEqual(tu_nodes[1].displayname, "s1")
        self.assertEqual(tu_nodes[1].is_definition(), False)

        self.assertEqual(tu_nodes[2].kind, CursorKind.FUNCTION_DECL)
        self.assertEqual(tu_nodes[2].spelling, "f0")
        self.assertEqual(tu_nodes[2].displayname, "f0(int, int)")
        self.assertEqual(tu_nodes[2].is_definition(), True)

    def test_references(self):
        """Ensure that references to TranslationUnit are kept."""
        tu = get_tu("int x;")
        cursors = list(tu.cursor.get_children())
        self.assertGreater(len(cursors), 0)

        cursor = cursors[0]
        self.assertIsInstance(cursor.translation_unit, TranslationUnit)

        # Delete reference to TU and perform a full GC.
        del tu
        gc.collect()
        self.assertIsInstance(cursor.translation_unit, TranslationUnit)

        # If the TU was destroyed, this should cause a segfault.
        parent = cursor.semantic_parent

    def test_canonical(self):
        source = "struct X; struct X; struct X { int member; };"
        tu = get_tu(source)

        cursors = []
        for cursor in tu.cursor.get_children():
            if cursor.spelling == "X":
                cursors.append(cursor)

        self.assertEqual(len(cursors), 3)
        self.assertEqual(cursors[1].canonical, cursors[2].canonical)

    def test_is_const_method(self):
        """Ensure Cursor.is_const_method works."""
        source = "class X { void foo() const; void bar(); };"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        foo = get_cursor(tu, "foo")
        bar = get_cursor(tu, "bar")
        self.assertIsNotNone(cls)
        self.assertIsNotNone(foo)
        self.assertIsNotNone(bar)

        self.assertTrue(foo.is_const_method())
        self.assertFalse(bar.is_const_method())

    def test_is_converting_constructor(self):
        """Ensure Cursor.is_converting_constructor works."""
        source = "class X { explicit X(int); X(double); X(); };"
        tu = get_tu(source, lang="cpp")

        xs = get_cursors(tu, "X")

        self.assertEqual(len(xs), 4)
        self.assertEqual(xs[0].kind, CursorKind.CLASS_DECL)
        cs = xs[1:]
        self.assertEqual(cs[0].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[1].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[2].kind, CursorKind.CONSTRUCTOR)

        self.assertFalse(cs[0].is_converting_constructor())
        self.assertTrue(cs[1].is_converting_constructor())
        self.assertFalse(cs[2].is_converting_constructor())

    def test_is_copy_constructor(self):
        """Ensure Cursor.is_copy_constructor works."""
        source = "class X { X(); X(const X&); X(X&&); };"
        tu = get_tu(source, lang="cpp")

        xs = get_cursors(tu, "X")
        self.assertEqual(xs[0].kind, CursorKind.CLASS_DECL)
        cs = xs[1:]
        self.assertEqual(cs[0].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[1].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[2].kind, CursorKind.CONSTRUCTOR)

        self.assertFalse(cs[0].is_copy_constructor())
        self.assertTrue(cs[1].is_copy_constructor())
        self.assertFalse(cs[2].is_copy_constructor())

    def test_is_default_constructor(self):
        """Ensure Cursor.is_default_constructor works."""
        source = "class X { X(); X(int); };"
        tu = get_tu(source, lang="cpp")

        xs = get_cursors(tu, "X")
        self.assertEqual(xs[0].kind, CursorKind.CLASS_DECL)
        cs = xs[1:]
        self.assertEqual(cs[0].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[1].kind, CursorKind.CONSTRUCTOR)

        self.assertTrue(cs[0].is_default_constructor())
        self.assertFalse(cs[1].is_default_constructor())

    def test_is_move_constructor(self):
        """Ensure Cursor.is_move_constructor works."""
        source = "class X { X(); X(const X&); X(X&&); };"
        tu = get_tu(source, lang="cpp")

        xs = get_cursors(tu, "X")
        self.assertEqual(xs[0].kind, CursorKind.CLASS_DECL)
        cs = xs[1:]
        self.assertEqual(cs[0].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[1].kind, CursorKind.CONSTRUCTOR)
        self.assertEqual(cs[2].kind, CursorKind.CONSTRUCTOR)

        self.assertFalse(cs[0].is_move_constructor())
        self.assertFalse(cs[1].is_move_constructor())
        self.assertTrue(cs[2].is_move_constructor())

    def test_is_default_method(self):
        """Ensure Cursor.is_default_method works."""
        source = "class X { X() = default; }; class Y { Y(); };"
        tu = get_tu(source, lang="cpp")

        xs = get_cursors(tu, "X")
        ys = get_cursors(tu, "Y")

        self.assertEqual(len(xs), 2)
        self.assertEqual(len(ys), 2)

        xc = xs[1]
        yc = ys[1]

        self.assertTrue(xc.is_default_method())
        self.assertFalse(yc.is_default_method())

    def test_is_move_assignment_operator_method(self):
        """Ensure Cursor.is_move_assignment_operator_method works."""
        source_with_move_assignment_operators = """
        struct Foo {
           // Those are move-assignment operators
           bool operator=(const Foo&&);
           bool operator=(Foo&&);
           bool operator=(volatile Foo&&);
           bool operator=(const volatile Foo&&);

        // Positive-check that the recognition works for templated classes too
        template <typename T>
        class Bar {
            bool operator=(const Bar&&);
            bool operator=(Bar<T>&&);
            bool operator=(volatile Bar&&);
            bool operator=(const volatile Bar<T>&&);
        };
        """
        source_without_move_assignment_operators = """
        struct Foo {
            // Those are not move-assignment operators
            template<typename T>
            bool operator=(const T&&);
            bool operator=(const bool&&);
            bool operator=(char&&);
            bool operator=(volatile unsigned int&&);
            bool operator=(const volatile unsigned char&&);
            bool operator=(int);
            bool operator=(Foo);
        };
        """
        tu_with_move_assignment_operators = get_tu(
            source_with_move_assignment_operators, lang="cpp"
        )
        tu_without_move_assignment_operators = get_tu(
            source_without_move_assignment_operators, lang="cpp"
        )

        move_assignment_operators_cursors = get_cursors(
            tu_with_move_assignment_operators, "operator="
        )
        non_move_assignment_operators_cursors = get_cursors(
            tu_without_move_assignment_operators, "operator="
        )

        self.assertEqual(len(move_assignment_operators_cursors), 8)
        self.assertTrue(len(non_move_assignment_operators_cursors), 7)

        self.assertTrue(
            all(
                [
                    cursor.is_move_assignment_operator_method()
                    for cursor in move_assignment_operators_cursors
                ]
            )
        )
        self.assertFalse(
            any(
                [
                    cursor.is_move_assignment_operator_method()
                    for cursor in non_move_assignment_operators_cursors
                ]
            )
        )

    def test_is_explicit_method(self):
        """Ensure Cursor.is_explicit_method works."""
        source_with_explicit_methods = """
        struct Foo {
           // Those are explicit
           explicit Foo(double);
           explicit(true) Foo(char);
           explicit operator double();
           explicit(true) operator char();
        };
        """
        source_without_explicit_methods = """
        struct Foo {
            // Those are not explicit
            Foo(int);
            explicit(false) Foo(float);
            operator int();
            explicit(false) operator float();
        };
        """
        tu_with_explicit_methods = get_tu(source_with_explicit_methods, lang="cpp")
        tu_without_explicit_methods = get_tu(
            source_without_explicit_methods, lang="cpp"
        )

        explicit_methods_cursors = [
            *get_cursors(tu_with_explicit_methods, "Foo")[1:],
            get_cursor(tu_with_explicit_methods, "operator double"),
            get_cursor(tu_with_explicit_methods, "operator char"),
        ]

        non_explicit_methods_cursors = [
            *get_cursors(tu_without_explicit_methods, "Foo")[1:],
            get_cursor(tu_without_explicit_methods, "operator int"),
            get_cursor(tu_without_explicit_methods, "operator float"),
        ]

        self.assertEqual(len(explicit_methods_cursors), 4)
        self.assertTrue(len(non_explicit_methods_cursors), 4)

        self.assertTrue(
            all([cursor.is_explicit_method() for cursor in explicit_methods_cursors])
        )
        self.assertFalse(
            any(
                [cursor.is_explicit_method() for cursor in non_explicit_methods_cursors]
            )
        )

    def test_is_mutable_field(self):
        """Ensure Cursor.is_mutable_field works."""
        source = "class X { int x_; mutable int y_; };"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        x_ = get_cursor(tu, "x_")
        y_ = get_cursor(tu, "y_")
        self.assertIsNotNone(cls)
        self.assertIsNotNone(x_)
        self.assertIsNotNone(y_)

        self.assertFalse(x_.is_mutable_field())
        self.assertTrue(y_.is_mutable_field())

    def test_is_static_method(self):
        """Ensure Cursor.is_static_method works."""

        source = "class X { static void foo(); void bar(); };"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        foo = get_cursor(tu, "foo")
        bar = get_cursor(tu, "bar")
        self.assertIsNotNone(cls)
        self.assertIsNotNone(foo)
        self.assertIsNotNone(bar)

        self.assertTrue(foo.is_static_method())
        self.assertFalse(bar.is_static_method())

    def test_is_pure_virtual_method(self):
        """Ensure Cursor.is_pure_virtual_method works."""
        source = "class X { virtual void foo() = 0; virtual void bar(); };"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        foo = get_cursor(tu, "foo")
        bar = get_cursor(tu, "bar")
        self.assertIsNotNone(cls)
        self.assertIsNotNone(foo)
        self.assertIsNotNone(bar)

        self.assertTrue(foo.is_pure_virtual_method())
        self.assertFalse(bar.is_pure_virtual_method())

    def test_is_virtual_method(self):
        """Ensure Cursor.is_virtual_method works."""
        source = "class X { virtual void foo(); void bar(); };"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        foo = get_cursor(tu, "foo")
        bar = get_cursor(tu, "bar")
        self.assertIsNotNone(cls)
        self.assertIsNotNone(foo)
        self.assertIsNotNone(bar)

        self.assertTrue(foo.is_virtual_method())
        self.assertFalse(bar.is_virtual_method())

    def test_is_abstract_record(self):
        """Ensure Cursor.is_abstract_record works."""
        source = "struct X { virtual void x() = 0; }; struct Y : X { void x(); };"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        self.assertTrue(cls.is_abstract_record())

        cls = get_cursor(tu, "Y")
        self.assertFalse(cls.is_abstract_record())

    def test_is_scoped_enum(self):
        """Ensure Cursor.is_scoped_enum works."""
        source = "class X {}; enum RegularEnum {}; enum class ScopedEnum {};"
        tu = get_tu(source, lang="cpp")

        cls = get_cursor(tu, "X")
        regular_enum = get_cursor(tu, "RegularEnum")
        scoped_enum = get_cursor(tu, "ScopedEnum")
        self.assertIsNotNone(cls)
        self.assertIsNotNone(regular_enum)
        self.assertIsNotNone(scoped_enum)

        self.assertFalse(cls.is_scoped_enum())
        self.assertFalse(regular_enum.is_scoped_enum())
        self.assertTrue(scoped_enum.is_scoped_enum())

    def test_underlying_type(self):
        tu = get_tu("typedef int foo;")
        typedef = get_cursor(tu, "foo")
        self.assertIsNotNone(typedef)

        self.assertTrue(typedef.kind.is_declaration())
        underlying = typedef.underlying_typedef_type
        self.assertEqual(underlying.kind, TypeKind.INT)

    def test_semantic_parent(self):
        tu = get_tu(kParentTest, "cpp")
        curs = get_cursors(tu, "f")
        decl = get_cursor(tu, "C")
        self.assertEqual(len(curs), 2)
        self.assertEqual(curs[0].semantic_parent, curs[1].semantic_parent)
        self.assertEqual(curs[0].semantic_parent, decl)

    def test_lexical_parent(self):
        tu = get_tu(kParentTest, "cpp")
        curs = get_cursors(tu, "f")
        decl = get_cursor(tu, "C")
        self.assertEqual(len(curs), 2)
        self.assertNotEqual(curs[0].lexical_parent, curs[1].lexical_parent)
        self.assertEqual(curs[0].lexical_parent, decl)
        self.assertEqual(curs[1].lexical_parent, tu.cursor)

    def test_enum_type(self):
        tu = get_tu("enum TEST { FOO=1, BAR=2 };")
        enum = get_cursor(tu, "TEST")
        self.assertIsNotNone(enum)

        self.assertEqual(enum.kind, CursorKind.ENUM_DECL)
        enum_type = enum.enum_type
        self.assertIn(enum_type.kind, (TypeKind.UINT, TypeKind.INT))

    def test_enum_type_cpp(self):
        tu = get_tu("enum TEST : long long { FOO=1, BAR=2 };", lang="cpp")
        enum = get_cursor(tu, "TEST")
        self.assertIsNotNone(enum)

        self.assertEqual(enum.kind, CursorKind.ENUM_DECL)
        self.assertEqual(enum.enum_type.kind, TypeKind.LONGLONG)

    def test_objc_type_encoding(self):
        tu = get_tu("int i;", lang="objc")
        i = get_cursor(tu, "i")

        self.assertIsNotNone(i)
        self.assertEqual(i.objc_type_encoding, "i")

    def test_enum_values(self):
        tu = get_tu("enum TEST { SPAM=1, EGG, HAM = EGG * 20};")
        enum = get_cursor(tu, "TEST")
        self.assertIsNotNone(enum)

        self.assertEqual(enum.kind, CursorKind.ENUM_DECL)

        enum_constants = list(enum.get_children())
        self.assertEqual(len(enum_constants), 3)

        spam, egg, ham = enum_constants

        self.assertEqual(spam.kind, CursorKind.ENUM_CONSTANT_DECL)
        self.assertEqual(spam.enum_value, 1)
        self.assertEqual(egg.kind, CursorKind.ENUM_CONSTANT_DECL)
        self.assertEqual(egg.enum_value, 2)
        self.assertEqual(ham.kind, CursorKind.ENUM_CONSTANT_DECL)
        self.assertEqual(ham.enum_value, 40)

    def test_enum_values_cpp(self):
        tu = get_tu(
            "enum TEST : long long { SPAM = -1, HAM = 0x10000000000};", lang="cpp"
        )
        enum = get_cursor(tu, "TEST")
        self.assertIsNotNone(enum)

        self.assertEqual(enum.kind, CursorKind.ENUM_DECL)

        enum_constants = list(enum.get_children())
        self.assertEqual(len(enum_constants), 2)

        spam, ham = enum_constants

        self.assertEqual(spam.kind, CursorKind.ENUM_CONSTANT_DECL)
        self.assertEqual(spam.enum_value, -1)
        self.assertEqual(ham.kind, CursorKind.ENUM_CONSTANT_DECL)
        self.assertEqual(ham.enum_value, 0x10000000000)

    def test_annotation_attribute(self):
        tu = get_tu(
            'int foo (void) __attribute__ ((annotate("here be annotation attribute")));'
        )

        foo = get_cursor(tu, "foo")
        self.assertIsNotNone(foo)

        for c in foo.get_children():
            if c.kind == CursorKind.ANNOTATE_ATTR:
                self.assertEqual(c.displayname, "here be annotation attribute")
                break
        else:
            self.fail("Couldn't find annotation")

    def test_annotation_template(self):
        annotation = '__attribute__ ((annotate("annotation")))'
        for source, kind in [
            ("int foo (T value) %s;", CursorKind.FUNCTION_TEMPLATE),
            ("class %s foo {};", CursorKind.CLASS_TEMPLATE),
        ]:
            source = "template<typename T> " + (source % annotation)
            tu = get_tu(source, lang="cpp")

            foo = get_cursor(tu, "foo")
            self.assertIsNotNone(foo)
            self.assertEqual(foo.kind, kind)

            for c in foo.get_children():
                if c.kind == CursorKind.ANNOTATE_ATTR:
                    self.assertEqual(c.displayname, "annotation")
                    break
            else:
                self.fail("Couldn't find annotation for {}".format(kind))

    def test_result_type(self):
        tu = get_tu("int foo();")
        foo = get_cursor(tu, "foo")

        self.assertIsNotNone(foo)
        t = foo.result_type
        self.assertEqual(t.kind, TypeKind.INT)

    def test_result_type_objc_method_decl(self):
        code = """\
        @interface Interface : NSObject
        -(void)voidMethod;
        @end
        """
        tu = get_tu(code, lang="objc")
        cursor = get_cursor(tu, "voidMethod")
        result_type = cursor.result_type
        self.assertEqual(cursor.kind, CursorKind.OBJC_INSTANCE_METHOD_DECL)
        self.assertEqual(result_type.kind, TypeKind.VOID)

    def test_availability(self):
        tu = get_tu("class A { A(A const&) = delete; };", lang="cpp")

        # AvailabilityKind.AVAILABLE
        cursor = get_cursor(tu, "A")
        self.assertEqual(cursor.kind, CursorKind.CLASS_DECL)
        self.assertEqual(cursor.availability, AvailabilityKind.AVAILABLE)

        # AvailabilityKind.NOT_AVAILABLE
        cursors = get_cursors(tu, "A")
        for c in cursors:
            if c.kind == CursorKind.CONSTRUCTOR:
                self.assertEqual(c.availability, AvailabilityKind.NOT_AVAILABLE)
                break
        else:
            self.fail("Could not find cursor for deleted constructor")

        # AvailabilityKind.DEPRECATED
        tu = get_tu("void test() __attribute__((deprecated));", lang="cpp")
        cursor = get_cursor(tu, "test")
        self.assertEqual(cursor.availability, AvailabilityKind.DEPRECATED)

        # AvailabilityKind.NOT_ACCESSIBLE is only used in the code completion results

    def test_get_tokens(self):
        """Ensure we can map cursors back to tokens."""
        tu = get_tu("int foo(int i);")
        foo = get_cursor(tu, "foo")

        tokens = list(foo.get_tokens())
        self.assertEqual(len(tokens), 6)
        self.assertEqual(tokens[0].spelling, "int")
        self.assertEqual(tokens[1].spelling, "foo")

    def test_get_token_cursor(self):
        """Ensure we can map tokens to cursors."""
        tu = get_tu("class A {}; int foo(A var = A());", lang="cpp")
        foo = get_cursor(tu, "foo")

        for cursor in foo.walk_preorder():
            if cursor.kind.is_expression() and not cursor.kind.is_statement():
                break
        else:
            self.fail("Could not find default value expression")

        tokens = list(cursor.get_tokens())
        self.assertEqual(len(tokens), 4, [t.spelling for t in tokens])
        self.assertEqual(tokens[0].spelling, "=")
        self.assertEqual(tokens[1].spelling, "A")
        self.assertEqual(tokens[2].spelling, "(")
        self.assertEqual(tokens[3].spelling, ")")
        t_cursor = tokens[1].cursor
        self.assertEqual(t_cursor.kind, CursorKind.TYPE_REF)
        r_cursor = t_cursor.referenced  # should not raise an exception
        self.assertEqual(r_cursor.kind, CursorKind.CLASS_DECL)

    def test_get_arguments(self):
        tu = get_tu("void foo(int i, int j);")
        foo = get_cursor(tu, "foo")
        arguments = list(foo.get_arguments())

        self.assertEqual(len(arguments), 2)
        self.assertEqual(arguments[0].spelling, "i")
        self.assertEqual(arguments[1].spelling, "j")

    def test_get_num_template_arguments(self):
        tu = get_tu(kTemplateArgTest, lang="cpp")
        foos = get_cursors(tu, "foo")

        self.assertEqual(foos[1].get_num_template_arguments(), 3)

    def test_get_template_argument_kind(self):
        tu = get_tu(kTemplateArgTest, lang="cpp")
        foos = get_cursors(tu, "foo")

        self.assertEqual(
            foos[1].get_template_argument_kind(0), TemplateArgumentKind.INTEGRAL
        )
        self.assertEqual(
            foos[1].get_template_argument_kind(1), TemplateArgumentKind.TYPE
        )
        self.assertEqual(
            foos[1].get_template_argument_kind(2), TemplateArgumentKind.INTEGRAL
        )

    def test_get_template_argument_type(self):
        tu = get_tu(kTemplateArgTest, lang="cpp")
        foos = get_cursors(tu, "foo")

        self.assertEqual(foos[1].get_template_argument_type(1).kind, TypeKind.FLOAT)

    def test_get_template_argument_value(self):
        tu = get_tu(kTemplateArgTest, lang="cpp")
        foos = get_cursors(tu, "foo")

        self.assertEqual(foos[1].get_template_argument_value(0), -7)
        self.assertEqual(foos[1].get_template_argument_value(2), True)

    def test_get_template_argument_unsigned_value(self):
        tu = get_tu(kTemplateArgTest, lang="cpp")
        foos = get_cursors(tu, "foo")

        self.assertEqual(foos[1].get_template_argument_unsigned_value(0), 2**32 - 7)
        self.assertEqual(foos[1].get_template_argument_unsigned_value(2), True)

    def test_referenced(self):
        tu = get_tu("void foo(); void bar() { foo(); }")
        foo = get_cursor(tu, "foo")
        bar = get_cursor(tu, "bar")
        for c in bar.get_children():
            if c.kind == CursorKind.CALL_EXPR:
                self.assertEqual(c.referenced.spelling, foo.spelling)
                break

    def test_mangled_name(self):
        kInputForMangling = """\
        int foo(int, int);
        """
        tu = get_tu(kInputForMangling, lang="cpp")
        foo = get_cursor(tu, "foo")

        # Since libclang does not link in targets, we cannot pass a triple to it
        # and force the target. To enable this test to pass on all platforms, accept
        # all valid manglings.
        # [c-index-test handles this by running the source through clang, emitting
        #  an AST file and running libclang on that AST file]
        self.assertIn(
            foo.mangled_name, ("_Z3fooii", "__Z3fooii", "?foo@@YAHHH", "?foo@@YAHHH@Z")
        )

    def test_binop(self):
        tu = get_tu(kBinops, lang="cpp")

        operators = {
            # not exposed yet
            # ".*" : BinaryOperator.PtrMemD,
            "->*": BinaryOperator.PtrMemI,
            "*": BinaryOperator.Mul,
            "/": BinaryOperator.Div,
            "%": BinaryOperator.Rem,
            "+": BinaryOperator.Add,
            "-": BinaryOperator.Sub,
            "<<": BinaryOperator.Shl,
            ">>": BinaryOperator.Shr,
            # tests do not run in C++2a mode so this operator is not available
            # "<=>" : BinaryOperator.Cmp,
            "<": BinaryOperator.LT,
            ">": BinaryOperator.GT,
            "<=": BinaryOperator.LE,
            ">=": BinaryOperator.GE,
            "==": BinaryOperator.EQ,
            "!=": BinaryOperator.NE,
            "&": BinaryOperator.And,
            "^": BinaryOperator.Xor,
            "|": BinaryOperator.Or,
            "&&": BinaryOperator.LAnd,
            "||": BinaryOperator.LOr,
            "=": BinaryOperator.Assign,
            "*=": BinaryOperator.MulAssign,
            "/=": BinaryOperator.DivAssign,
            "%=": BinaryOperator.RemAssign,
            "+=": BinaryOperator.AddAssign,
            "-=": BinaryOperator.SubAssign,
            "<<=": BinaryOperator.ShlAssign,
            ">>=": BinaryOperator.ShrAssign,
            "&=": BinaryOperator.AndAssign,
            "^=": BinaryOperator.XorAssign,
            "|=": BinaryOperator.OrAssign,
            ",": BinaryOperator.Comma,
        }

        for op, typ in operators.items():
            c = get_cursor(tu, op)
            assert c.binary_operator == typ
