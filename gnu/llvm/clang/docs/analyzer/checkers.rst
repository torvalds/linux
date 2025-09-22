==================
Available Checkers
==================

The analyzer performs checks that are categorized into families or "checkers".

The default set of checkers covers a variety of checks targeted at finding security and API usage bugs,
dead code, and other logic errors. See the :ref:`default-checkers` checkers list below.

In addition to these, the analyzer contains a number of :ref:`alpha-checkers` (aka *alpha* checkers).
These checkers are under development and are switched off by default. They may crash or emit a higher number of false positives.

The :ref:`debug-checkers` package contains checkers for analyzer developers for debugging purposes.

.. contents:: Table of Contents
   :depth: 4


.. _default-checkers:

Default Checkers
----------------

.. _core-checkers:

core
^^^^
Models core language features and contains general-purpose checkers such as division by zero,
null pointer dereference, usage of uninitialized values, etc.
*These checkers must be always switched on as other checker rely on them.*

.. _core-BitwiseShift:

core.BitwiseShift (C, C++)
""""""""""""""""""""""""""

Finds undefined behavior caused by the bitwise left- and right-shift operator
operating on integer types.

By default, this checker only reports situations when the right operand is
either negative or larger than the bit width of the type of the left operand;
these are logically unsound.

Moreover, if the pedantic mode is activated by
``-analyzer-config core.BitwiseShift:Pedantic=true``, then this checker also
reports situations where the _left_ operand of a shift operator is negative or
overflow occurs during the right shift of a signed value. (Most compilers
handle these predictably, but the C standard and the C++ standards before C++20
say that they're undefined behavior. In the C++20 standard these constructs are
well-defined, so activating pedantic mode in C++20 has no effect.)

**Examples**

.. code-block:: cpp

 static_assert(sizeof(int) == 4, "assuming 32-bit int")

 void basic_examples(int a, int b) {
   if (b < 0) {
     b = a << b; // warn: right operand is negative in left shift
   } else if (b >= 32) {
     b = a >> b; // warn: right shift overflows the capacity of 'int'
   }
 }

 int pedantic_examples(int a, int b) {
   if (a < 0) {
     return a >> b; // warn: left operand is negative in right shift
   }
   a = 1000u << 31; // OK, overflow of unsigned value is well-defined, a == 0
   if (b > 10) {
     a = b << 31; // this is undefined before C++20, but the checker doesn't
                  // warn because it doesn't know the exact value of b
   }
   return 1000 << 31; // warn: this overflows the capacity of 'int'
 }

**Solution**

Ensure the shift operands are in proper range before shifting.

.. _core-CallAndMessage:

core.CallAndMessage (C, C++, ObjC)
""""""""""""""""""""""""""""""""""
 Check for logical errors for function calls and Objective-C message expressions (e.g., uninitialized arguments, null function pointers).

.. literalinclude:: checkers/callandmessage_example.c
    :language: objc

.. _core-DivideZero:

core.DivideZero (C, C++, ObjC)
""""""""""""""""""""""""""""""
 Check for division by zero.

.. literalinclude:: checkers/dividezero_example.c
    :language: c

.. _core-NonNullParamChecker:

core.NonNullParamChecker (C, C++, ObjC)
"""""""""""""""""""""""""""""""""""""""
Check for null pointers passed as arguments to a function whose arguments are references or marked with the 'nonnull' attribute.

.. code-block:: cpp

 int f(int *p) __attribute__((nonnull));

 void test(int *p) {
   if (!p)
     f(p); // warn
 }

.. _core-NullDereference:

core.NullDereference (C, C++, ObjC)
"""""""""""""""""""""""""""""""""""
Check for dereferences of null pointers.

This checker specifically does
not report null pointer dereferences for x86 and x86-64 targets when the
address space is 256 (x86 GS Segment), 257 (x86 FS Segment), or 258 (x86 SS
segment). See `X86/X86-64 Language Extensions
<https://clang.llvm.org/docs/LanguageExtensions.html#memory-references-to-specified-segments>`__
for reference.

The ``SuppressAddressSpaces`` option suppresses
warnings for null dereferences of all pointers with address spaces. You can
disable this behavior with the option
``-analyzer-config core.NullDereference:SuppressAddressSpaces=false``.
*Defaults to true*.

.. code-block:: objc

 // C
 void test(int *p) {
   if (p)
     return;

   int x = p[0]; // warn
 }

 // C
 void test(int *p) {
   if (!p)
     *p = 0; // warn
 }

 // C++
 class C {
 public:
   int x;
 };

 void test() {
   C *pc = 0;
   int k = pc->x; // warn
 }

 // Objective-C
 @interface MyClass {
 @public
   int x;
 }
 @end

 void test() {
   MyClass *obj = 0;
   obj->x = 1; // warn
 }

.. _core-StackAddressEscape:

core.StackAddressEscape (C)
"""""""""""""""""""""""""""
Check that addresses to stack memory do not escape the function.

.. code-block:: c

 char const *p;

 void test() {
   char const str[] = "string";
   p = str; // warn
 }

 void* test() {
    return __builtin_alloca(12); // warn
 }

 void test() {
   static int *x;
   int y;
   x = &y; // warn
 }


.. _core-UndefinedBinaryOperatorResult:

core.UndefinedBinaryOperatorResult (C)
""""""""""""""""""""""""""""""""""""""
Check for undefined results of binary operators.

.. code-block:: c

 void test() {
   int x;
   int y = x + 1; // warn: left operand is garbage
 }

.. _core-VLASize:

core.VLASize (C)
""""""""""""""""
Check for declarations of Variable Length Arrays (VLA) of undefined, zero or negative
size.

.. code-block:: c

 void test() {
   int x;
   int vla1[x]; // warn: garbage as size
 }

 void test() {
   int x = 0;
   int vla2[x]; // warn: zero size
 }


The checker also gives warning if the `TaintPropagation` checker is switched on
and an unbound, attacker controlled (tainted) value is used to define
the size of the VLA.

.. code-block:: c

 void taintedVLA(void) {
   int x;
   scanf("%d", &x);
   int vla[x]; // Declared variable-length array (VLA) has tainted (attacker controlled) size, that can be 0 or negative
 }

 void taintedVerfieidVLA(void) {
   int x;
   scanf("%d", &x);
   if (x<1)
     return;
   int vla[x]; // no-warning. The analyzer can prove that x must be positive.
 }


.. _core-uninitialized-ArraySubscript:

core.uninitialized.ArraySubscript (C)
"""""""""""""""""""""""""""""""""""""
Check for uninitialized values used as array subscripts.

.. code-block:: c

 void test() {
   int i, a[10];
   int x = a[i]; // warn: array subscript is undefined
 }

.. _core-uninitialized-Assign:

core.uninitialized.Assign (C)
"""""""""""""""""""""""""""""
Check for assigning uninitialized values.

.. code-block:: c

 void test() {
   int x;
   x |= 1; // warn: left expression is uninitialized
 }

.. _core-uninitialized-Branch:

core.uninitialized.Branch (C)
"""""""""""""""""""""""""""""
Check for uninitialized values used as branch conditions.

.. code-block:: c

 void test() {
   int x;
   if (x) // warn
     return;
 }

.. _core-uninitialized-CapturedBlockVariable:

core.uninitialized.CapturedBlockVariable (C)
""""""""""""""""""""""""""""""""""""""""""""
Check for blocks that capture uninitialized values.

.. code-block:: c

 void test() {
   int x;
   ^{ int y = x; }(); // warn
 }

.. _core-uninitialized-UndefReturn:

core.uninitialized.UndefReturn (C)
""""""""""""""""""""""""""""""""""
Check for uninitialized values being returned to the caller.

.. code-block:: c

 int test() {
   int x;
   return x; // warn
 }

.. _core-uninitialized-NewArraySize:

core.uninitialized.NewArraySize (C++)
"""""""""""""""""""""""""""""""""""""

Check if the element count in new[] is garbage or undefined.

.. code-block:: cpp

  void test() {
    int n;
    int *arr = new int[n]; // warn: Element count in new[] is a garbage value
    delete[] arr;
  }


.. _cplusplus-checkers:


cplusplus
^^^^^^^^^

C++ Checkers.

.. _cplusplus-ArrayDelete:

cplusplus.ArrayDelete (C++)
"""""""""""""""""""""""""""

Reports destructions of arrays of polymorphic objects that are destructed as
their base class. If the dynamic type of the array is different from its static
type, calling `delete[]` is undefined.

This checker corresponds to the SEI CERT rule `EXP51-CPP: Do not delete an array through a pointer of the incorrect type <https://wiki.sei.cmu.edu/confluence/display/cplusplus/EXP51-CPP.+Do+not+delete+an+array+through+a+pointer+of+the+incorrect+type>`_.

.. code-block:: cpp

 class Base {
 public:
   virtual ~Base() {}
 };
 class Derived : public Base {};

 Base *create() {
   Base *x = new Derived[10]; // note: Casting from 'Derived' to 'Base' here
   return x;
 }

 void foo() {
   Base *x = create();
   delete[] x; // warn: Deleting an array of 'Derived' objects as their base class 'Base' is undefined
 }

**Limitations**

The checker does not emit note tags when casting to and from reference types,
even though the pointer values are tracked across references.

.. code-block:: cpp

 void foo() {
   Derived *d = new Derived[10];
   Derived &dref = *d;

   Base &bref = static_cast<Base&>(dref); // no note
   Base *b = &bref;
   delete[] b; // warn: Deleting an array of 'Derived' objects as their base class 'Base' is undefined
 }

.. _cplusplus-InnerPointer:

cplusplus.InnerPointer (C++)
""""""""""""""""""""""""""""
Check for inner pointers of C++ containers used after re/deallocation.

Many container methods in the C++ standard library are known to invalidate
"references" (including actual references, iterators and raw pointers) to
elements of the container. Using such references after they are invalidated
causes undefined behavior, which is a common source of memory errors in C++ that
this checker is capable of finding.

The checker is currently limited to ``std::string`` objects and doesn't
recognize some of the more sophisticated approaches to passing unowned pointers
around, such as ``std::string_view``.

.. code-block:: cpp

 void deref_after_assignment() {
   std::string s = "llvm";
   const char *c = s.data(); // note: pointer to inner buffer of 'std::string' obtained here
   s = "clang"; // note: inner buffer of 'std::string' reallocated by call to 'operator='
   consume(c); // warn: inner pointer of container used after re/deallocation
 }

 const char *return_temp(int x) {
   return std::to_string(x).c_str(); // warn: inner pointer of container used after re/deallocation
   // note: pointer to inner buffer of 'std::string' obtained here
   // note: inner buffer of 'std::string' deallocated by call to destructor
 }

.. _cplusplus-Move:

cplusplus.Move (C++)
""""""""""""""""""""
Find use-after-move bugs in C++. This includes method calls on moved-from
objects, assignment of a moved-from object, and repeated move of a moved-from
object.

.. code-block:: cpp

 struct A {
   void foo() {}
 };

 void f1() {
   A a;
   A b = std::move(a); // note: 'a' became 'moved-from' here
   a.foo();            // warn: method call on a 'moved-from' object 'a'
 }

 void f2() {
   A a;
   A b = std::move(a);
   A c(std::move(a)); // warn: move of an already moved-from object
 }

 void f3() {
   A a;
   A b = std::move(a);
   b = a; // warn: copy of moved-from object
 }

The checker option ``WarnOn`` controls on what objects the use-after-move is
checked:

* The most strict value is ``KnownsOnly``, in this mode only objects are
  checked whose type is known to be move-unsafe. These include most STL objects
  (but excluding move-safe ones) and smart pointers.
* With option value ``KnownsAndLocals`` local variables (of any type) are
  additionally checked. The idea behind this is that local variables are
  usually not tempting to be re-used so an use after move is more likely a bug
  than with member variables.
* With option value ``All`` any use-after move condition is checked on all
  kinds of variables, excluding global variables and known move-safe cases.

Default value is ``KnownsAndLocals``.

Calls of methods named ``empty()`` or ``isEmpty()`` are allowed on moved-from
objects because these methods are considered as move-safe. Functions called
``reset()``, ``destroy()``, ``clear()``, ``assign``, ``resize``,  ``shrink`` are
treated as state-reset functions and are allowed on moved-from objects, these
make the object valid again. This applies to any type of object (not only STL
ones).

.. _cplusplus-NewDelete:

cplusplus.NewDelete (C++)
"""""""""""""""""""""""""
Check for double-free and use-after-free problems. Traces memory managed by new/delete.

.. literalinclude:: checkers/newdelete_example.cpp
    :language: cpp

.. _cplusplus-NewDeleteLeaks:

cplusplus.NewDeleteLeaks (C++)
""""""""""""""""""""""""""""""
Check for memory leaks. Traces memory managed by new/delete.

.. code-block:: cpp

 void test() {
   int *p = new int;
 } // warn

.. _cplusplus-PlacementNew:

cplusplus.PlacementNew (C++)
""""""""""""""""""""""""""""
Check if default placement new is provided with pointers to sufficient storage capacity.

.. code-block:: cpp

 #include <new>

 void f() {
   short s;
   long *lp = ::new (&s) long; // warn
 }

.. _cplusplus-SelfAssignment:

cplusplus.SelfAssignment (C++)
""""""""""""""""""""""""""""""
Checks C++ copy and move assignment operators for self assignment.

.. _cplusplus-StringChecker:

cplusplus.StringChecker (C++)
"""""""""""""""""""""""""""""
Checks std::string operations.

Checks if the cstring pointer from which the ``std::string`` object is
constructed is ``NULL`` or not.
If the checker cannot reason about the nullness of the pointer it will assume
that it was non-null to satisfy the precondition of the constructor.

This checker is capable of checking the `SEI CERT C++ coding rule STR51-CPP.
Do not attempt to create a std::string from a null pointer
<https://wiki.sei.cmu.edu/confluence/x/E3s-BQ>`__.

.. code-block:: cpp

 #include <string>

 void f(const char *p) {
   if (!p) {
     std::string msg(p); // warn: The parameter must not be null
   }
 }

.. _deadcode-checkers:

deadcode
^^^^^^^^

Dead Code Checkers.

.. _deadcode-DeadStores:

deadcode.DeadStores (C)
"""""""""""""""""""""""
Check for values stored to variables that are never read afterwards.

.. code-block:: c

 void test() {
   int x;
   x = 1; // warn
 }

The ``WarnForDeadNestedAssignments`` option enables the checker to emit
warnings for nested dead assignments. You can disable with the
``-analyzer-config deadcode.DeadStores:WarnForDeadNestedAssignments=false``.
*Defaults to true*.

Would warn for this e.g.:
if ((y = make_int())) {
}

.. _nullability-checkers:

nullability
^^^^^^^^^^^

Objective C checkers that warn for null pointer passing and dereferencing errors.

.. _nullability-NullPassedToNonnull:

nullability.NullPassedToNonnull (ObjC)
""""""""""""""""""""""""""""""""""""""
Warns when a null pointer is passed to a pointer which has a _Nonnull type.

.. code-block:: objc

 if (name != nil)
   return;
 // Warning: nil passed to a callee that requires a non-null 1st parameter
 NSString *greeting = [@"Hello " stringByAppendingString:name];

.. _nullability-NullReturnedFromNonnull:

nullability.NullReturnedFromNonnull (ObjC)
""""""""""""""""""""""""""""""""""""""""""
Warns when a null pointer is returned from a function that has _Nonnull return type.

.. code-block:: objc

 - (nonnull id)firstChild {
   id result = nil;
   if ([_children count] > 0)
     result = _children[0];

   // Warning: nil returned from a method that is expected
   // to return a non-null value
   return result;
 }

.. _nullability-NullableDereferenced:

nullability.NullableDereferenced (ObjC)
"""""""""""""""""""""""""""""""""""""""
Warns when a nullable pointer is dereferenced.

.. code-block:: objc

 struct LinkedList {
   int data;
   struct LinkedList *next;
 };

 struct LinkedList * _Nullable getNext(struct LinkedList *l);

 void updateNextData(struct LinkedList *list, int newData) {
   struct LinkedList *next = getNext(list);
   // Warning: Nullable pointer is dereferenced
   next->data = 7;
 }

.. _nullability-NullablePassedToNonnull:

nullability.NullablePassedToNonnull (ObjC)
""""""""""""""""""""""""""""""""""""""""""
Warns when a nullable pointer is passed to a pointer which has a _Nonnull type.

.. code-block:: objc

 typedef struct Dummy { int val; } Dummy;
 Dummy *_Nullable returnsNullable();
 void takesNonnull(Dummy *_Nonnull);

 void test() {
   Dummy *p = returnsNullable();
   takesNonnull(p); // warn
 }

.. _nullability-NullableReturnedFromNonnull:

nullability.NullableReturnedFromNonnull (ObjC)
""""""""""""""""""""""""""""""""""""""""""""""
Warns when a nullable pointer is returned from a function that has _Nonnull return type.

.. _optin-checkers:

optin
^^^^^

Checkers for portability, performance, optional security and coding style specific rules.

.. _optin-core-EnumCastOutOfRange:

optin.core.EnumCastOutOfRange (C, C++)
""""""""""""""""""""""""""""""""""""""
Check for integer to enumeration casts that would produce a value with no
corresponding enumerator. This is not necessarily undefined behavior, but can
lead to nasty surprises, so projects may decide to use a coding standard that
disallows these "unusual" conversions.

Note that no warnings are produced when the enum type (e.g. `std::byte`) has no
enumerators at all.

.. code-block:: cpp

 enum WidgetKind { A=1, B, C, X=99 };

 void foo() {
   WidgetKind c = static_cast<WidgetKind>(3);  // OK
   WidgetKind x = static_cast<WidgetKind>(99); // OK
   WidgetKind d = static_cast<WidgetKind>(4);  // warn
 }

**Limitations**

This checker does not accept the coding pattern where an enum type is used to
store combinations of flag values:

.. code-block:: cpp

 enum AnimalFlags
 {
     HasClaws   = 1,
     CanFly     = 2,
     EatsFish   = 4,
     Endangered = 8
 };

 AnimalFlags operator|(AnimalFlags a, AnimalFlags b)
 {
     return static_cast<AnimalFlags>(static_cast<int>(a) | static_cast<int>(b));
 }

 auto flags = HasClaws | CanFly;

Projects that use this pattern should not enable this optin checker.

.. _optin-cplusplus-UninitializedObject:

optin.cplusplus.UninitializedObject (C++)
"""""""""""""""""""""""""""""""""""""""""

This checker reports uninitialized fields in objects created after a constructor
call. It doesn't only find direct uninitialized fields, but rather makes a deep
inspection of the object, analyzing all of its fields' subfields.
The checker regards inherited fields as direct fields, so one will receive
warnings for uninitialized inherited data members as well.

.. code-block:: cpp

 // With Pedantic and CheckPointeeInitialization set to true

 struct A {
   struct B {
     int x; // note: uninitialized field 'this->b.x'
     // note: uninitialized field 'this->bptr->x'
     int y; // note: uninitialized field 'this->b.y'
     // note: uninitialized field 'this->bptr->y'
   };
   int *iptr; // note: uninitialized pointer 'this->iptr'
   B b;
   B *bptr;
   char *cptr; // note: uninitialized pointee 'this->cptr'

   A (B *bptr, char *cptr) : bptr(bptr), cptr(cptr) {}
 };

 void f() {
   A::B b;
   char c;
   A a(&b, &c); // warning: 6 uninitialized fields
  //          after the constructor call
 }

 // With Pedantic set to false and
 // CheckPointeeInitialization set to true
 // (every field is uninitialized)

 struct A {
   struct B {
     int x;
     int y;
   };
   int *iptr;
   B b;
   B *bptr;
   char *cptr;

   A (B *bptr, char *cptr) : bptr(bptr), cptr(cptr) {}
 };

 void f() {
   A::B b;
   char c;
   A a(&b, &c); // no warning
 }

 // With Pedantic set to true and
 // CheckPointeeInitialization set to false
 // (pointees are regarded as initialized)

 struct A {
   struct B {
     int x; // note: uninitialized field 'this->b.x'
     int y; // note: uninitialized field 'this->b.y'
   };
   int *iptr; // note: uninitialized pointer 'this->iptr'
   B b;
   B *bptr;
   char *cptr;

   A (B *bptr, char *cptr) : bptr(bptr), cptr(cptr) {}
 };

 void f() {
   A::B b;
   char c;
   A a(&b, &c); // warning: 3 uninitialized fields
  //          after the constructor call
 }


**Options**

This checker has several options which can be set from command line (e.g.
``-analyzer-config optin.cplusplus.UninitializedObject:Pedantic=true``):

* ``Pedantic`` (boolean). If to false, the checker won't emit warnings for
  objects that don't have at least one initialized field. Defaults to false.

* ``NotesAsWarnings``  (boolean). If set to true, the checker will emit a
  warning for each uninitialized field, as opposed to emitting one warning per
  constructor call, and listing the uninitialized fields that belongs to it in
  notes. *Defaults to false*.

* ``CheckPointeeInitialization`` (boolean). If set to false, the checker will
  not analyze the pointee of pointer/reference fields, and will only check
  whether the object itself is initialized. *Defaults to false*.

* ``IgnoreRecordsWithField`` (string). If supplied, the checker will not analyze
  structures that have a field with a name or type name that matches  the given
  pattern. *Defaults to ""*.

.. _optin-cplusplus-VirtualCall:

optin.cplusplus.VirtualCall (C++)
"""""""""""""""""""""""""""""""""
Check virtual function calls during construction or destruction.

.. code-block:: cpp

 class A {
 public:
   A() {
     f(); // warn
   }
   virtual void f();
 };

 class A {
 public:
   ~A() {
     this->f(); // warn
   }
   virtual void f();
 };

.. _optin-mpi-MPI-Checker:

optin.mpi.MPI-Checker (C)
"""""""""""""""""""""""""
Checks MPI code.

.. code-block:: c

 void test() {
   double buf = 0;
   MPI_Request sendReq1;
   MPI_Ireduce(MPI_IN_PLACE, &buf, 1, MPI_DOUBLE, MPI_SUM,
       0, MPI_COMM_WORLD, &sendReq1);
 } // warn: request 'sendReq1' has no matching wait.

 void test() {
   double buf = 0;
   MPI_Request sendReq;
   MPI_Isend(&buf, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, &sendReq);
   MPI_Irecv(&buf, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, &sendReq); // warn
   MPI_Isend(&buf, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, &sendReq); // warn
   MPI_Wait(&sendReq, MPI_STATUS_IGNORE);
 }

 void missingNonBlocking() {
   int rank = 0;
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
   MPI_Request sendReq1[10][10][10];
   MPI_Wait(&sendReq1[1][7][9], MPI_STATUS_IGNORE); // warn
 }

.. _optin-osx-cocoa-localizability-EmptyLocalizationContextChecker:

optin.osx.cocoa.localizability.EmptyLocalizationContextChecker (ObjC)
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
Check that NSLocalizedString macros include a comment for context.

.. code-block:: objc

 - (void)test {
   NSString *string = NSLocalizedString(@"LocalizedString", nil); // warn
   NSString *string2 = NSLocalizedString(@"LocalizedString", @" "); // warn
   NSString *string3 = NSLocalizedStringWithDefaultValue(
     @"LocalizedString", nil, [[NSBundle alloc] init], nil,@""); // warn
 }

.. _optin-osx-cocoa-localizability-NonLocalizedStringChecker:

optin.osx.cocoa.localizability.NonLocalizedStringChecker (ObjC)
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
Warns about uses of non-localized NSStrings passed to UI methods expecting localized NSStrings.

.. code-block:: objc

 NSString *alarmText =
   NSLocalizedString(@"Enabled", @"Indicates alarm is turned on");
 if (!isEnabled) {
   alarmText = @"Disabled";
 }
 UILabel *alarmStateLabel = [[UILabel alloc] init];

 // Warning: User-facing text should use localized string macro
 [alarmStateLabel setText:alarmText];

.. _optin-performance-GCDAntipattern:

optin.performance.GCDAntipattern
""""""""""""""""""""""""""""""""
Check for performance anti-patterns when using Grand Central Dispatch.

.. _optin-performance-Padding:

optin.performance.Padding (C, C++, ObjC)
""""""""""""""""""""""""""""""""""""""""
Check for excessively padded structs.

This checker detects structs with excessive padding, which can lead to wasted
memory thus decreased performance by reducing the effectiveness of the
processor cache. Padding bytes are added by compilers to align data accesses
as some processors require data to be aligned to certain boundaries. On others,
unaligned data access are possible, but impose significantly larger latencies.

To avoid padding bytes, the fields of a struct should be ordered by decreasing
by alignment. Usually, its easier to think of the ``sizeof`` of the fields, and
ordering the fields by ``sizeof`` would usually also lead to the same optimal
layout.

In rare cases, one can use the ``#pragma pack(1)`` directive to enforce a packed
layout too, but it can significantly increase the access times, so reordering the
fields is usually a better solution.


.. code-block:: cpp

 // warn: Excessive padding in 'struct NonOptimal' (35 padding bytes, where 3 is optimal)
 struct NonOptimal {
   char c1;
   // 7 bytes of padding
   std::int64_t big1; // 8 bytes
   char c2;
   // 7 bytes of padding
   std::int64_t big2; // 8 bytes
   char c3;
   // 7 bytes of padding
   std::int64_t big3; // 8 bytes
   char c4;
   // 7 bytes of padding
   std::int64_t big4; // 8 bytes
   char c5;
   // 7 bytes of padding
 };
 static_assert(sizeof(NonOptimal) == 4*8+5+5*7);

 // no-warning: The fields are nicely aligned to have the minimal amount of padding bytes.
 struct Optimal {
   std::int64_t big1; // 8 bytes
   std::int64_t big2; // 8 bytes
   std::int64_t big3; // 8 bytes
   std::int64_t big4; // 8 bytes
   char c1;
   char c2;
   char c3;
   char c4;
   char c5;
   // 3 bytes of padding
 };
 static_assert(sizeof(Optimal) == 4*8+5+3);

 // no-warning: Bit packing representation is also accepted by this checker, but
 // it can significantly increase access times, so prefer reordering the fields.
 #pragma pack(1)
 struct BitPacked {
   char c1;
   std::int64_t big1; // 8 bytes
   char c2;
   std::int64_t big2; // 8 bytes
   char c3;
   std::int64_t big3; // 8 bytes
   char c4;
   std::int64_t big4; // 8 bytes
   char c5;
 };
 static_assert(sizeof(BitPacked) == 4*8+5);

The ``AllowedPad`` option can be used to specify a threshold for the number
padding bytes raising the warning. If the number of padding bytes of the struct
and the optimal number of padding bytes differ by more than the threshold value,
a warning will be raised.

By default, the ``AllowedPad`` threshold is 24 bytes.

To override this threshold to e.g. 4 bytes, use the
``-analyzer-config optin.performance.Padding:AllowedPad=4`` option.


.. _optin-portability-UnixAPI:

optin.portability.UnixAPI
"""""""""""""""""""""""""
Finds implementation-defined behavior in UNIX/Posix functions.

.. _optin-taint-TaintedAlloc:

optin.taint.TaintedAlloc (C, C++)
"""""""""""""""""""""""""""""""""

This checker warns for cases when the ``size`` parameter of the ``malloc`` ,
``calloc``, ``realloc``, ``alloca`` or the size parameter of the
array new C++ operator is tainted (potentially attacker controlled).
If an attacker can inject a large value as the size parameter, memory exhaustion
denial of service attack can be carried out.

The analyzer emits warning only if it cannot prove that the size parameter is
within reasonable bounds (``<= SIZE_MAX/4``). This functionality partially
covers the SEI Cert coding standard rule `INT04-C
<https://wiki.sei.cmu.edu/confluence/display/c/INT04-C.+Enforce+limits+on+integer+values+originating+from+tainted+sources>`_.

You can silence this warning either by bound checking the ``size`` parameter, or
by explicitly marking the ``size`` parameter as sanitized. See the
:ref:`alpha-security-taint-GenericTaint` checker for an example.

.. code-block:: c

  void vulnerable(void) {
    size_t size = 0;
    scanf("%zu", &size);
    int *p = malloc(size); // warn: malloc is called with a tainted (potentially attacker controlled) value
    free(p);
  }

  void not_vulnerable(void) {
    size_t size = 0;
    scanf("%zu", &size);
    if (1024 < size)
      return;
    int *p = malloc(size); // No warning expected as the the user input is bound
    free(p);
  }

  void vulnerable_cpp(void) {
    size_t size = 0;
    scanf("%zu", &size);
    int *ptr = new int[size];// warn: Memory allocation function is called with a tainted (potentially attacker controlled) value
    delete[] ptr;
  }

.. _security-checkers:

security
^^^^^^^^

Security related checkers.

.. _security-cert-env-InvalidPtr:

security.cert.env.InvalidPtr
""""""""""""""""""""""""""""""""""

Corresponds to SEI CERT Rules `ENV31-C <https://wiki.sei.cmu.edu/confluence/display/c/ENV31-C.+Do+not+rely+on+an+environment+pointer+following+an+operation+that+may+invalidate+it>`_ and `ENV34-C <https://wiki.sei.cmu.edu/confluence/display/c/ENV34-C.+Do+not+store+pointers+returned+by+certain+functions>`_.

* **ENV31-C**:
  Rule is about the possible problem with ``main`` function's third argument, environment pointer,
  "envp". When environment array is modified using some modification function
  such as ``putenv``, ``setenv`` or others, It may happen that memory is reallocated,
  however "envp" is not updated to reflect the changes and points to old memory
  region.

* **ENV34-C**:
  Some functions return a pointer to a statically allocated buffer.
  Consequently, subsequent call of these functions will invalidate previous
  pointer. These functions include: ``getenv``, ``localeconv``, ``asctime``, ``setlocale``, ``strerror``

.. code-block:: c

  int main(int argc, const char *argv[], const char *envp[]) {
    if (setenv("MY_NEW_VAR", "new_value", 1) != 0) {
      // setenv call may invalidate 'envp'
      /* Handle error */
    }
    if (envp != NULL) {
      for (size_t i = 0; envp[i] != NULL; ++i) {
        puts(envp[i]);
        // envp may no longer point to the current environment
        // this program has unanticipated behavior, since envp
        // does not reflect changes made by setenv function.
      }
    }
    return 0;
  }

  void previous_call_invalidation() {
    char *p, *pp;

    p = getenv("VAR");
    setenv("SOMEVAR", "VALUE", /*overwrite = */1);
    // call to 'setenv' may invalidate p

    *p;
    // dereferencing invalid pointer
  }


The ``InvalidatingGetEnv`` option is available for treating ``getenv`` calls as
invalidating. When enabled, the checker issues a warning if ``getenv`` is called
multiple times and their results are used without first creating a copy.
This level of strictness might be considered overly pedantic for the commonly
used ``getenv`` implementations.

To enable this option, use:
``-analyzer-config security.cert.env.InvalidPtr:InvalidatingGetEnv=true``.

By default, this option is set to *false*.

When this option is enabled, warnings will be generated for scenarios like the
following:

.. code-block:: c

  char* p = getenv("VAR");
  char* pp = getenv("VAR2"); // assumes this call can invalidate `env`
  strlen(p); // warns about accessing invalid ptr

.. _security-FloatLoopCounter:

security.FloatLoopCounter (C)
"""""""""""""""""""""""""""""
Warn on using a floating point value as a loop counter (CERT: FLP30-C, FLP30-CPP).

.. code-block:: c

 void test() {
   for (float x = 0.1f; x <= 1.0f; x += 0.1f) {} // warn
 }

.. _security-insecureAPI-UncheckedReturn:

security.insecureAPI.UncheckedReturn (C)
""""""""""""""""""""""""""""""""""""""""
Warn on uses of functions whose return values must be always checked.

.. code-block:: c

 void test() {
   setuid(1); // warn
 }

.. _security-insecureAPI-bcmp:

security.insecureAPI.bcmp (C)
"""""""""""""""""""""""""""""
Warn on uses of the 'bcmp' function.

.. code-block:: c

 void test() {
   bcmp(ptr0, ptr1, n); // warn
 }

.. _security-insecureAPI-bcopy:

security.insecureAPI.bcopy (C)
""""""""""""""""""""""""""""""
Warn on uses of the 'bcopy' function.

.. code-block:: c

 void test() {
   bcopy(src, dst, n); // warn
 }

.. _security-insecureAPI-bzero:

security.insecureAPI.bzero (C)
""""""""""""""""""""""""""""""
Warn on uses of the 'bzero' function.

.. code-block:: c

 void test() {
   bzero(ptr, n); // warn
 }

.. _security-insecureAPI-getpw:

security.insecureAPI.getpw (C)
""""""""""""""""""""""""""""""
Warn on uses of the 'getpw' function.

.. code-block:: c

 void test() {
   char buff[1024];
   getpw(2, buff); // warn
 }

.. _security-insecureAPI-gets:

security.insecureAPI.gets (C)
"""""""""""""""""""""""""""""
Warn on uses of the 'gets' function.

.. code-block:: c

 void test() {
   char buff[1024];
   gets(buff); // warn
 }

.. _security-insecureAPI-mkstemp:

security.insecureAPI.mkstemp (C)
""""""""""""""""""""""""""""""""
Warn when 'mkstemp' is passed fewer than 6 X's in the format string.

.. code-block:: c

 void test() {
   mkstemp("XX"); // warn
 }

.. _security-insecureAPI-mktemp:

security.insecureAPI.mktemp (C)
"""""""""""""""""""""""""""""""
Warn on uses of the ``mktemp`` function.

.. code-block:: c

 void test() {
   char *x = mktemp("/tmp/zxcv"); // warn: insecure, use mkstemp
 }

.. _security-insecureAPI-rand:

security.insecureAPI.rand (C)
"""""""""""""""""""""""""""""
Warn on uses of inferior random number generating functions (only if arc4random function is available):
``drand48, erand48, jrand48, lcong48, lrand48, mrand48, nrand48, random, rand_r``.

.. code-block:: c

 void test() {
   random(); // warn
 }

.. _security-insecureAPI-strcpy:

security.insecureAPI.strcpy (C)
"""""""""""""""""""""""""""""""
Warn on uses of the ``strcpy`` and ``strcat`` functions.

.. code-block:: c

 void test() {
   char x[4];
   char *y = "abcd";

   strcpy(x, y); // warn
 }


.. _security-insecureAPI-vfork:

security.insecureAPI.vfork (C)
""""""""""""""""""""""""""""""
 Warn on uses of the 'vfork' function.

.. code-block:: c

 void test() {
   vfork(); // warn
 }

.. _security-insecureAPI-DeprecatedOrUnsafeBufferHandling:

security.insecureAPI.DeprecatedOrUnsafeBufferHandling (C)
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""
 Warn on occurrences of unsafe or deprecated buffer handling functions, which now have a secure variant: ``sprintf, fprintf, vsprintf, scanf, wscanf, fscanf, fwscanf, vscanf, vwscanf, vfscanf, vfwscanf, sscanf, swscanf, vsscanf, vswscanf, swprintf, snprintf, vswprintf, vsnprintf, memcpy, memmove, strncpy, strncat, memset``

.. code-block:: c

 void test() {
   char buf [5];
   strncpy(buf, "a", 1); // warn
 }

.. _security-putenv-stack-array:

security.PutenvStackArray (C)
"""""""""""""""""""""""""""""
Finds calls to the ``putenv`` function which pass a pointer to a stack-allocated
(automatic) array as the argument. Function ``putenv`` does not copy the passed
string, only a pointer to the data is stored and this data can be read even by
other threads. Content of a stack-allocated array is likely to be overwritten
after exiting from the function.

The problem can be solved by using a static array variable or dynamically
allocated memory. Even better is to avoid using ``putenv`` (it has other
problems related to memory leaks) and use ``setenv`` instead.

The check corresponds to CERT rule
`POS34-C. Do not call putenv() with a pointer to an automatic variable as the argument
<https://wiki.sei.cmu.edu/confluence/display/c/POS34-C.+Do+not+call+putenv%28%29+with+a+pointer+to+an+automatic+variable+as+the+argument>`_.

.. code-block:: c

  int f() {
    char env[] = "NAME=value";
    return putenv(env); // putenv function should not be called with stack-allocated string
  }

There is one case where the checker can report a false positive. This is when
the stack-allocated array is used at `putenv` in a function or code branch that
does not return (process is terminated on all execution paths).

Another special case is if the `putenv` is called from function `main`. Here
the stack is deallocated at the end of the program and it should be no problem
to use the stack-allocated string (a multi-threaded program may require more
attention). The checker does not warn for cases when stack space of `main` is
used at the `putenv` call.

security.SetgidSetuidOrder (C)
""""""""""""""""""""""""""""""
When dropping user-level and group-level privileges in a program by using
``setuid`` and ``setgid`` calls, it is important to reset the group-level
privileges (with ``setgid``) first. Function ``setgid`` will likely fail if
the superuser privileges are already dropped.

The checker checks for sequences of ``setuid(getuid())`` and
``setgid(getgid())`` calls (in this order). If such a sequence is found and
there is no other privilege-changing function call (``seteuid``, ``setreuid``,
``setresuid`` and the GID versions of these) in between, a warning is
generated. The checker finds only exactly ``setuid(getuid())`` calls (and the
GID versions), not for example if the result of ``getuid()`` is stored in a
variable.

.. code-block:: c

 void test1() {
   // ...
   // end of section with elevated privileges
   // reset privileges (user and group) to normal user
   if (setuid(getuid()) != 0) {
     handle_error();
     return;
   }
   if (setgid(getgid()) != 0) { // warning: A 'setgid(getgid())' call following a 'setuid(getuid())' call is likely to fail
     handle_error();
     return;
   }
   // user-ID and group-ID are reset to normal user now
   // ...
 }

In the code above the problem is that ``setuid(getuid())`` removes superuser
privileges before ``setgid(getgid())`` is called. To fix the problem the
``setgid(getgid())`` should be called first. Further attention is needed to
avoid code like ``setgid(getuid())`` (this checker does not detect bugs like
this) and always check the return value of these calls.

This check corresponds to SEI CERT Rule `POS36-C <https://wiki.sei.cmu.edu/confluence/display/c/POS36-C.+Observe+correct+revocation+order+while+relinquishing+privileges>`_.

.. _unix-checkers:

unix
^^^^
POSIX/Unix checkers.

.. _unix-API:

unix.API (C)
""""""""""""
Check calls to various UNIX/Posix functions: ``open, pthread_once, calloc, malloc, realloc, alloca``.

.. literalinclude:: checkers/unix_api_example.c
    :language: c

.. _unix-BlockInCriticalSection:

unix.BlockInCriticalSection (C, C++)
""""""""""""""""""""""""""""""""""""
Check for calls to blocking functions inside a critical section.
Blocking functions detected by this checker: ``sleep, getc, fgets, read, recv``.
Critical section handling functions modeled by this checker:
``lock, unlock, pthread_mutex_lock, pthread_mutex_trylock, pthread_mutex_unlock, mtx_lock, mtx_timedlock, mtx_trylock, mtx_unlock, lock_guard, unique_lock``.

.. code-block:: c

 void pthread_lock_example(pthread_mutex_t *m) {
   pthread_mutex_lock(m); // note: entering critical section here
   sleep(10); // warn: Call to blocking function 'sleep' inside of critical section
   pthread_mutex_unlock(m);
 }

.. code-block:: cpp

 void overlapping_critical_sections(mtx_t *m1, std::mutex &m2) {
   std::lock_guard lg{m2}; // note: entering critical section here
   mtx_lock(m1); // note: entering critical section here
   sleep(10); // warn: Call to blocking function 'sleep' inside of critical section
   mtx_unlock(m1);
   sleep(10); // warn: Call to blocking function 'sleep' inside of critical section
              // still inside of the critical section of the std::lock_guard
 }

**Limitations**

* The ``trylock`` and ``timedlock`` versions of acquiring locks are currently assumed to always succeed.
  This can lead to false positives.

.. code-block:: c

 void trylock_example(pthread_mutex_t *m) {
   if (pthread_mutex_trylock(m) == 0) { // assume trylock always succeeds
     sleep(10); // warn: Call to blocking function 'sleep' inside of critical section
     pthread_mutex_unlock(m);
   } else {
     sleep(10); // false positive: Incorrect warning about blocking function inside critical section.
   }
 }

.. _unix-Errno:

unix.Errno (C)
""""""""""""""

Check for improper use of ``errno``.
This checker implements partially CERT rule
`ERR30-C. Set errno to zero before calling a library function known to set errno,
and check errno only after the function returns a value indicating failure
<https://wiki.sei.cmu.edu/confluence/pages/viewpage.action?pageId=87152351>`_.
The checker can find the first read of ``errno`` after successful standard
function calls.

The C and POSIX standards often do not define if a standard library function
may change value of ``errno`` if the call does not fail.
Therefore, ``errno`` should only be used if it is known from the return value
of a function that the call has failed.
There are exceptions to this rule (for example ``strtol``) but the affected
functions are not yet supported by the checker.
The return values for the failure cases are documented in the standard Linux man
pages of the functions and in the `POSIX standard <https://pubs.opengroup.org/onlinepubs/9699919799/>`_.

.. code-block:: c

 int unsafe_errno_read(int sock, void *data, int data_size) {
   if (send(sock, data, data_size, 0) != data_size) {
     // 'send' can be successful even if not all data was sent
     if (errno == 1) { // An undefined value may be read from 'errno'
       return 0;
     }
   }
   return 1;
 }

The checker :ref:`unix-StdCLibraryFunctions` must be turned on to get the
warnings from this checker. The supported functions are the same as by
:ref:`unix-StdCLibraryFunctions`. The ``ModelPOSIX`` option of that
checker affects the set of checked functions.

**Parameters**

The ``AllowErrnoReadOutsideConditionExpressions`` option allows read of the
errno value if the value is not used in a condition (in ``if`` statements,
loops, conditional expressions, ``switch`` statements). For example ``errno``
can be stored into a variable without getting a warning by the checker.

.. code-block:: c

 int unsafe_errno_read(int sock, void *data, int data_size) {
   if (send(sock, data, data_size, 0) != data_size) {
     int err = errno;
     // warning if 'AllowErrnoReadOutsideConditionExpressions' is false
     // no warning if 'AllowErrnoReadOutsideConditionExpressions' is true
   }
   return 1;
 }

Default value of this option is ``true``. This allows save of the errno value
for possible later error handling.

**Limitations**

 - Only the very first usage of ``errno`` is checked after an affected function
   call. Value of ``errno`` is not followed when it is stored into a variable
   or returned from a function.
 - Documentation of function ``lseek`` is not clear about what happens if the
   function returns different value than the expected file position but not -1.
   To avoid possible false-positives ``errno`` is allowed to be used in this
   case.

.. _unix-Malloc:

unix.Malloc (C)
"""""""""""""""
Check for memory leaks, double free, and use-after-free problems. Traces memory managed by malloc()/free().

.. literalinclude:: checkers/unix_malloc_example.c
    :language: c

.. _unix-MallocSizeof:

unix.MallocSizeof (C)
"""""""""""""""""""""
Check for dubious ``malloc`` arguments involving ``sizeof``.

.. code-block:: c

 void test() {
   long *p = malloc(sizeof(short));
     // warn: result is converted to 'long *', which is
     // incompatible with operand type 'short'
   free(p);
 }

.. _unix-MismatchedDeallocator:

unix.MismatchedDeallocator (C, C++)
"""""""""""""""""""""""""""""""""""
Check for mismatched deallocators.

.. literalinclude:: checkers/mismatched_deallocator_example.cpp
    :language: c

.. _unix-Vfork:

unix.Vfork (C)
""""""""""""""
Check for proper usage of ``vfork``.

.. code-block:: c

 int test(int x) {
   pid_t pid = vfork(); // warn
   if (pid != 0)
     return 0;

   switch (x) {
   case 0:
     pid = 1;
     execl("", "", 0);
     _exit(1);
     break;
   case 1:
     x = 0; // warn: this assignment is prohibited
     break;
   case 2:
     foo(); // warn: this function call is prohibited
     break;
   default:
     return 0; // warn: return is prohibited
   }

   while(1);
 }

.. _unix-cstring-BadSizeArg:

unix.cstring.BadSizeArg (C)
"""""""""""""""""""""""""""
Check the size argument passed into C string functions for common erroneous patterns. Use ``-Wno-strncat-size`` compiler option to mute other ``strncat``-related compiler warnings.

.. code-block:: c

 void test() {
   char dest[3];
   strncat(dest, """""""""""""""""""""""""*", sizeof(dest));
     // warn: potential buffer overflow
 }

.. _unix-cstring-NullArg:

unix.cstring.NullArg (C)
""""""""""""""""""""""""
Check for null pointers being passed as arguments to C string functions:
``strlen, strnlen, strcpy, strncpy, strcat, strncat, strcmp, strncmp, strcasecmp, strncasecmp, wcslen, wcsnlen``.

.. code-block:: c

 int test() {
   return strlen(0); // warn
 }

.. _unix-StdCLibraryFunctions:

unix.StdCLibraryFunctions (C)
"""""""""""""""""""""""""""""
Check for calls of standard library functions that violate predefined argument
constraints. For example, according to the C standard the behavior of function
``int isalnum(int ch)`` is undefined if the value of ``ch`` is not representable
as ``unsigned char`` and is not equal to ``EOF``.

You can think of this checker as defining restrictions (pre- and postconditions)
on standard library functions. Preconditions are checked, and when they are
violated, a warning is emitted. Postconditions are added to the analysis, e.g.
that the return value of a function is not greater than 255. Preconditions are
added to the analysis too, in the case when the affected values are not known
before the call.

For example, if an argument to a function must be in between 0 and 255, but the
value of the argument is unknown, the analyzer will assume that it is in this
interval. Similarly, if a function mustn't be called with a null pointer and the
analyzer cannot prove that it is null, then it will assume that it is non-null.

These are the possible checks on the values passed as function arguments:
 - The argument has an allowed range (or multiple ranges) of values. The checker
   can detect if a passed value is outside of the allowed range and show the
   actual and allowed values.
 - The argument has pointer type and is not allowed to be null pointer. Many
   (but not all) standard functions can produce undefined behavior if a null
   pointer is passed, these cases can be detected by the checker.
 - The argument is a pointer to a memory block and the minimal size of this
   buffer is determined by another argument to the function, or by
   multiplication of two arguments (like at function ``fread``), or is a fixed
   value (for example ``asctime_r`` requires at least a buffer of size 26). The
   checker can detect if the buffer size is too small and in optimal case show
   the size of the buffer and the values of the corresponding arguments.

.. code-block:: c

  #define EOF -1
  void test_alnum_concrete(int v) {
    int ret = isalnum(256); // \
    // warning: Function argument outside of allowed range
    (void)ret;
  }

  void buffer_size_violation(FILE *file) {
    enum { BUFFER_SIZE = 1024 };
    wchar_t wbuf[BUFFER_SIZE];

    const size_t size = sizeof(*wbuf);   // 4
    const size_t nitems = sizeof(wbuf);  // 4096

    // Below we receive a warning because the 3rd parameter should be the
    // number of elements to read, not the size in bytes. This case is a known
    // vulnerability described by the ARR38-C SEI-CERT rule.
    fread(wbuf, size, nitems, file);
  }

  int test_alnum_symbolic(int x) {
    int ret = isalnum(x);
    // after the call, ret is assumed to be in the range [-1, 255]

    if (ret > 255)      // impossible (infeasible branch)
      if (x == 0)
        return ret / x; // division by zero is not reported
    return ret;
  }

Additionally to the argument and return value conditions, this checker also adds
state of the value ``errno`` if applicable to the analysis. Many system
functions set the ``errno`` value only if an error occurs (together with a
specific return value of the function), otherwise it becomes undefined. This
checker changes the analysis state to contain such information. This data is
used by other checkers, for example :ref:`unix-Errno`.

**Limitations**

The checker can not always provide notes about the values of the arguments.
Without this information it is hard to confirm if the constraint is indeed
violated. The argument values are shown if they are known constants or the value
is determined by previous (not too complicated) assumptions.

The checker can produce false positives in cases such as if the program has
invariants not known to the analyzer engine or the bug report path contains
calls to unknown functions. In these cases the analyzer fails to detect the real
range of the argument.

**Parameters**

The ``ModelPOSIX`` option controls if functions from the POSIX standard are
recognized by the checker.

With ``ModelPOSIX=true``, many POSIX functions are modeled according to the
`POSIX standard`_. This includes ranges of parameters and possible return
values. Furthermore the behavior related to ``errno`` in the POSIX case is
often that ``errno`` is set only if a function call fails, and it becomes
undefined after a successful function call.

With ``ModelPOSIX=false``, this checker follows the C99 language standard and
only models the functions that are described there. It is possible that the
same functions are modeled differently in the two cases because differences in
the standards. The C standard specifies less aspects of the functions, for
example exact ``errno`` behavior is often unspecified (and not modeled by the
checker).

Default value of the option is ``true``.

.. _unix-Stream:

unix.Stream (C)
"""""""""""""""
Check C stream handling functions:
``fopen, fdopen, freopen, tmpfile, fclose, fread, fwrite, fgetc, fgets, fputc, fputs, fprintf, fscanf, ungetc, getdelim, getline, fseek, fseeko, ftell, ftello, fflush, rewind, fgetpos, fsetpos, clearerr, feof, ferror, fileno``.

The checker maintains information about the C stream objects (``FILE *``) and
can detect error conditions related to use of streams. The following conditions
are detected:

* The ``FILE *`` pointer passed to the function is NULL (the single exception is
  ``fflush`` where NULL is allowed).
* Use of stream after close.
* Opened stream is not closed.
* Read from a stream after end-of-file. (This is not a fatal error but reported
  by the checker. Stream remains in EOF state and the read operation fails.)
* Use of stream when the file position is indeterminate after a previous failed
  operation. Some functions (like ``ferror``, ``clearerr``, ``fseek``) are
  allowed in this state.
* Invalid 3rd ("``whence``") argument to ``fseek``.

The stream operations are by this checker usually split into two cases, a success
and a failure case. However, in the case of write operations (like ``fwrite``,
``fprintf`` and even ``fsetpos``) this behavior could produce a large amount of
unwanted reports on projects that don't have error checks around the write
operations, so by default the checker assumes that write operations always succeed.
This behavior can be controlled by the ``Pedantic`` flag: With
``-analyzer-config unix.Stream:Pedantic=true`` the checker will model the
cases where a write operation fails and report situations where this leads to
erroneous behavior. (The default is ``Pedantic=false``, where write operations
are assumed to succeed.)

.. code-block:: c

 void test1() {
   FILE *p = fopen("foo", "r");
 } // warn: opened file is never closed

 void test2() {
   FILE *p = fopen("foo", "r");
   fseek(p, 1, SEEK_SET); // warn: stream pointer might be NULL
   fclose(p);
 }

 void test3() {
   FILE *p = fopen("foo", "r");
   if (p) {
     fseek(p, 1, 3); // warn: third arg should be SEEK_SET, SEEK_END, or SEEK_CUR
     fclose(p);
   }
 }

 void test4() {
   FILE *p = fopen("foo", "r");
   if (!p)
     return;

   fclose(p);
   fclose(p); // warn: stream already closed
 }

 void test5() {
   FILE *p = fopen("foo", "r");
   if (!p)
     return;

   fgetc(p);
   if (!ferror(p))
     fgetc(p); // warn: possible read after end-of-file

   fclose(p);
 }

 void test6() {
   FILE *p = fopen("foo", "r");
   if (!p)
     return;

   fgetc(p);
   if (!feof(p))
     fgetc(p); // warn: file position may be indeterminate after I/O error

   fclose(p);
 }

**Limitations**

The checker does not track the correspondence between integer file descriptors
and ``FILE *`` pointers. Operations on standard streams like ``stdin`` are not
treated specially and are therefore often not recognized (because these streams
are usually not opened explicitly by the program, and are global variables).

.. _osx-checkers:

osx
^^^
macOS checkers.

.. _osx-API:

osx.API (C)
"""""""""""
Check for proper uses of various Apple APIs.

.. code-block:: objc

 void test() {
   dispatch_once_t pred = 0;
   dispatch_once(&pred, ^(){}); // warn: dispatch_once uses local
 }

.. _osx-NumberObjectConversion:

osx.NumberObjectConversion (C, C++, ObjC)
"""""""""""""""""""""""""""""""""""""""""
Check for erroneous conversions of objects representing numbers into numbers.

.. code-block:: objc

 NSNumber *photoCount = [albumDescriptor objectForKey:@"PhotoCount"];
 // Warning: Comparing a pointer value of type 'NSNumber *'
 // to a scalar integer value
 if (photoCount > 0) {
   [self displayPhotos];
 }

.. _osx-ObjCProperty:

osx.ObjCProperty (ObjC)
"""""""""""""""""""""""
Check for proper uses of Objective-C properties.

.. code-block:: objc

 NSNumber *photoCount = [albumDescriptor objectForKey:@"PhotoCount"];
 // Warning: Comparing a pointer value of type 'NSNumber *'
 // to a scalar integer value
 if (photoCount > 0) {
   [self displayPhotos];
 }


.. _osx-SecKeychainAPI:

osx.SecKeychainAPI (C)
""""""""""""""""""""""
Check for proper uses of Secure Keychain APIs.

.. literalinclude:: checkers/seckeychainapi_example.m
    :language: objc

.. _osx-cocoa-AtSync:

osx.cocoa.AtSync (ObjC)
"""""""""""""""""""""""
Check for nil pointers used as mutexes for @synchronized.

.. code-block:: objc

 void test(id x) {
   if (!x)
     @synchronized(x) {} // warn: nil value used as mutex
 }

 void test() {
   id y;
   @synchronized(y) {} // warn: uninitialized value used as mutex
 }

.. _osx-cocoa-AutoreleaseWrite:

osx.cocoa.AutoreleaseWrite
""""""""""""""""""""""""""
Warn about potentially crashing writes to autoreleasing objects from different autoreleasing pools in Objective-C.

.. _osx-cocoa-ClassRelease:

osx.cocoa.ClassRelease (ObjC)
"""""""""""""""""""""""""""""
Check for sending 'retain', 'release', or 'autorelease' directly to a Class.

.. code-block:: objc

 @interface MyClass : NSObject
 @end

 void test(void) {
   [MyClass release]; // warn
 }

.. _osx-cocoa-Dealloc:

osx.cocoa.Dealloc (ObjC)
""""""""""""""""""""""""
Warn about Objective-C classes that lack a correct implementation of -dealloc

.. literalinclude:: checkers/dealloc_example.m
    :language: objc

.. _osx-cocoa-IncompatibleMethodTypes:

osx.cocoa.IncompatibleMethodTypes (ObjC)
""""""""""""""""""""""""""""""""""""""""
Warn about Objective-C method signatures with type incompatibilities.

.. code-block:: objc

 @interface MyClass1 : NSObject
 - (int)foo;
 @end

 @implementation MyClass1
 - (int)foo { return 1; }
 @end

 @interface MyClass2 : MyClass1
 - (float)foo;
 @end

 @implementation MyClass2
 - (float)foo { return 1.0; } // warn
 @end

.. _osx-cocoa-Loops:

osx.cocoa.Loops
"""""""""""""""
Improved modeling of loops using Cocoa collection types.

.. _osx-cocoa-MissingSuperCall:

osx.cocoa.MissingSuperCall (ObjC)
"""""""""""""""""""""""""""""""""
Warn about Objective-C methods that lack a necessary call to super.

.. code-block:: objc

 @interface Test : UIViewController
 @end
 @implementation test
 - (void)viewDidLoad {} // warn
 @end


.. _osx-cocoa-NSAutoreleasePool:

osx.cocoa.NSAutoreleasePool (ObjC)
""""""""""""""""""""""""""""""""""
Warn for suboptimal uses of NSAutoreleasePool in Objective-C GC mode.

.. code-block:: objc

 void test() {
   NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
   [pool release]; // warn
 }

.. _osx-cocoa-NSError:

osx.cocoa.NSError (ObjC)
""""""""""""""""""""""""
Check usage of NSError parameters.

.. code-block:: objc

 @interface A : NSObject
 - (void)foo:(NSError """""""""""""""""""""""")error;
 @end

 @implementation A
 - (void)foo:(NSError """""""""""""""""""""""")error {
   // warn: method accepting NSError"""""""""""""""""""""""" should have a non-void
   // return value
 }
 @end

 @interface A : NSObject
 - (BOOL)foo:(NSError """""""""""""""""""""""")error;
 @end

 @implementation A
 - (BOOL)foo:(NSError """""""""""""""""""""""")error {
   *error = 0; // warn: potential null dereference
   return 0;
 }
 @end

.. _osx-cocoa-NilArg:

osx.cocoa.NilArg (ObjC)
"""""""""""""""""""""""
Check for prohibited nil arguments to ObjC method calls.

 - caseInsensitiveCompare:
 - compare:
 - compare:options:
 - compare:options:range:
 - compare:options:range:locale:
 - componentsSeparatedByCharactersInSet:
 - initWithFormat:

.. code-block:: objc

 NSComparisonResult test(NSString *s) {
   NSString *aString = nil;
   return [s caseInsensitiveCompare:aString];
     // warn: argument to 'NSString' method
     // 'caseInsensitiveCompare:' cannot be nil
 }


.. _osx-cocoa-NonNilReturnValue:

osx.cocoa.NonNilReturnValue
"""""""""""""""""""""""""""
Models the APIs that are guaranteed to return a non-nil value.

.. _osx-cocoa-ObjCGenerics:

osx.cocoa.ObjCGenerics (ObjC)
"""""""""""""""""""""""""""""
Check for type errors when using Objective-C generics.

.. code-block:: objc

 NSMutableArray *names = [NSMutableArray array];
 NSMutableArray *birthDates = names;

 // Warning: Conversion from value of type 'NSDate *'
 // to incompatible type 'NSString *'
 [birthDates addObject: [NSDate date]];

.. _osx-cocoa-RetainCount:

osx.cocoa.RetainCount (ObjC)
""""""""""""""""""""""""""""
Check for leaks and improper reference count management

.. code-block:: objc

 void test() {
   NSString *s = [[NSString alloc] init]; // warn
 }

 CFStringRef test(char *bytes) {
   return CFStringCreateWithCStringNoCopy(
            0, bytes, NSNEXTSTEPStringEncoding, 0); // warn
 }


.. _osx-cocoa-RunLoopAutoreleaseLeak:

osx.cocoa.RunLoopAutoreleaseLeak
""""""""""""""""""""""""""""""""
Check for leaked memory in autorelease pools that will never be drained.

.. _osx-cocoa-SelfInit:

osx.cocoa.SelfInit (ObjC)
"""""""""""""""""""""""""
Check that 'self' is properly initialized inside an initializer method.

.. code-block:: objc

 @interface MyObj : NSObject {
   id x;
 }
 - (id)init;
 @end

 @implementation MyObj
 - (id)init {
   [super init];
   x = 0; // warn: instance variable used while 'self' is not
          // initialized
   return 0;
 }
 @end

 @interface MyObj : NSObject
 - (id)init;
 @end

 @implementation MyObj
 - (id)init {
   [super init];
   return self; // warn: returning uninitialized 'self'
 }
 @end

.. _osx-cocoa-SuperDealloc:

osx.cocoa.SuperDealloc (ObjC)
"""""""""""""""""""""""""""""
Warn about improper use of '[super dealloc]' in Objective-C.

.. code-block:: objc

 @interface SuperDeallocThenReleaseIvarClass : NSObject {
   NSObject *_ivar;
 }
 @end

 @implementation SuperDeallocThenReleaseIvarClass
 - (void)dealloc {
   [super dealloc];
   [_ivar release]; // warn
 }
 @end

.. _osx-cocoa-UnusedIvars:

osx.cocoa.UnusedIvars (ObjC)
""""""""""""""""""""""""""""
Warn about private ivars that are never used.

.. code-block:: objc

 @interface MyObj : NSObject {
 @private
   id x; // warn
 }
 @end

 @implementation MyObj
 @end

.. _osx-cocoa-VariadicMethodTypes:

osx.cocoa.VariadicMethodTypes (ObjC)
""""""""""""""""""""""""""""""""""""
Check for passing non-Objective-C types to variadic collection
initialization methods that expect only Objective-C types.

.. code-block:: objc

 void test() {
   [NSSet setWithObjects:@"Foo", "Bar", nil];
     // warn: argument should be an ObjC pointer type, not 'char *'
 }

.. _osx-coreFoundation-CFError:

osx.coreFoundation.CFError (C)
""""""""""""""""""""""""""""""
Check usage of CFErrorRef* parameters

.. code-block:: c

 void test(CFErrorRef *error) {
   // warn: function accepting CFErrorRef* should have a
   // non-void return
 }

 int foo(CFErrorRef *error) {
   *error = 0; // warn: potential null dereference
   return 0;
 }

.. _osx-coreFoundation-CFNumber:

osx.coreFoundation.CFNumber (C)
"""""""""""""""""""""""""""""""
Check for proper uses of CFNumber APIs.

.. code-block:: c

 CFNumberRef test(unsigned char x) {
   return CFNumberCreate(0, kCFNumberSInt16Type, &x);
    // warn: 8 bit integer is used to initialize a 16 bit integer
 }

.. _osx-coreFoundation-CFRetainRelease:

osx.coreFoundation.CFRetainRelease (C)
""""""""""""""""""""""""""""""""""""""
Check for null arguments to CFRetain/CFRelease/CFMakeCollectable.

.. code-block:: c

 void test(CFTypeRef p) {
   if (!p)
     CFRetain(p); // warn
 }

 void test(int x, CFTypeRef p) {
   if (p)
     return;

   CFRelease(p); // warn
 }

.. _osx-coreFoundation-containers-OutOfBounds:

osx.coreFoundation.containers.OutOfBounds (C)
"""""""""""""""""""""""""""""""""""""""""""""
Checks for index out-of-bounds when using 'CFArray' API.

.. code-block:: c

 void test() {
   CFArrayRef A = CFArrayCreate(0, 0, 0, &kCFTypeArrayCallBacks);
   CFArrayGetValueAtIndex(A, 0); // warn
 }

.. _osx-coreFoundation-containers-PointerSizedValues:

osx.coreFoundation.containers.PointerSizedValues (C)
""""""""""""""""""""""""""""""""""""""""""""""""""""
Warns if 'CFArray', 'CFDictionary', 'CFSet' are created with non-pointer-size values.

.. code-block:: c

 void test() {
   int x[] = { 1 };
   CFArrayRef A = CFArrayCreate(0, (const void """""""""""""""""""""""")x, 1,
                                &kCFTypeArrayCallBacks); // warn
 }

Fuchsia
^^^^^^^

Fuchsia is an open source capability-based operating system currently being
developed by Google. This section describes checkers that can find various
misuses of Fuchsia APIs.

.. _fuchsia-HandleChecker:

fuchsia.HandleChecker
""""""""""""""""""""""""""""
Handles identify resources. Similar to pointers they can be leaked,
double freed, or use after freed. This check attempts to find such problems.

.. code-block:: cpp

 void checkLeak08(int tag) {
   zx_handle_t sa, sb;
   zx_channel_create(0, &sa, &sb);
   if (tag)
     zx_handle_close(sa);
   use(sb); // Warn: Potential leak of handle
   zx_handle_close(sb);
 }

WebKit
^^^^^^

WebKit is an open-source web browser engine available for macOS, iOS and Linux.
This section describes checkers that can find issues in WebKit codebase.

Most of the checkers focus on memory management for which WebKit uses custom implementation of reference counted smartpointers.

Checkers are formulated in terms related to ref-counting:
 - *Ref-counted type* is either ``Ref<T>`` or ``RefPtr<T>``.
 - *Ref-countable type* is any type that implements ``ref()`` and ``deref()`` methods as ``RefPtr<>`` is a template (i. e. relies on duck typing).
 - *Uncounted type* is ref-countable but not ref-counted type.

.. _webkit-RefCntblBaseVirtualDtor:

webkit.RefCntblBaseVirtualDtor
""""""""""""""""""""""""""""""""""""
All uncounted types used as base classes must have a virtual destructor.

Ref-counted types hold their ref-countable data by a raw pointer and allow implicit upcasting from ref-counted pointer to derived type to ref-counted pointer to base type. This might lead to an object of (dynamic) derived type being deleted via pointer to the base class type which C++ standard defines as UB in case the base class doesn't have virtual destructor ``[expr.delete]``.

.. code-block:: cpp

 struct RefCntblBase {
   void ref() {}
   void deref() {}
 };

 struct Derived : RefCntblBase { }; // warn

.. _webkit-NoUncountedMemberChecker:

webkit.NoUncountedMemberChecker
"""""""""""""""""""""""""""""""""""""
Raw pointers and references to uncounted types can't be used as class members. Only ref-counted types are allowed.

.. code-block:: cpp

 struct RefCntbl {
   void ref() {}
   void deref() {}
 };

 struct Foo {
   RefCntbl * ptr; // warn
   RefCntbl & ptr; // warn
   // ...
 };

.. _webkit-UncountedLambdaCapturesChecker:

webkit.UncountedLambdaCapturesChecker
"""""""""""""""""""""""""""""""""""""
Raw pointers and references to uncounted types can't be captured in lambdas. Only ref-counted types are allowed.

.. code-block:: cpp

 struct RefCntbl {
   void ref() {}
   void deref() {}
 };

 void foo(RefCntbl* a, RefCntbl& b) {
   [&, a](){ // warn about 'a'
     do_something(b); // warn about 'b'
   };
 };

.. _alpha-checkers:

Experimental Checkers
---------------------

*These are checkers with known issues or limitations that keep them from being on by default. They are likely to have false positives. Bug reports and especially patches are welcome.*

alpha.clone
^^^^^^^^^^^

.. _alpha-clone-CloneChecker:

alpha.clone.CloneChecker (C, C++, ObjC)
"""""""""""""""""""""""""""""""""""""""
Reports similar pieces of code.

.. code-block:: c

 void log();

 int max(int a, int b) { // warn
   log();
   if (a > b)
     return a;
   return b;
 }

 int maxClone(int x, int y) { // similar code here
   log();
   if (x > y)
     return x;
   return y;
 }

alpha.core
^^^^^^^^^^

.. _alpha-core-BoolAssignment:

alpha.core.BoolAssignment (ObjC)
""""""""""""""""""""""""""""""""
Warn about assigning non-{0,1} values to boolean variables.

.. code-block:: objc

 void test() {
   BOOL b = -1; // warn
 }

.. _alpha-core-C11Lock:

alpha.core.C11Lock
""""""""""""""""""
Similarly to :ref:`alpha.unix.PthreadLock <alpha-unix-PthreadLock>`, checks for
the locking/unlocking of ``mtx_t`` mutexes.

.. code-block:: cpp

 mtx_t mtx1;

 void bad1(void)
 {
   mtx_lock(&mtx1);
   mtx_lock(&mtx1); // warn: This lock has already been acquired
 }

.. _alpha-core-CastSize:

alpha.core.CastSize (C)
"""""""""""""""""""""""
Check when casting a malloc'ed type ``T``, whether the size is a multiple of the size of ``T``.

.. code-block:: c

 void test() {
   int *x = (int *) malloc(11); // warn
 }

.. _alpha-core-CastToStruct:

alpha.core.CastToStruct (C, C++)
""""""""""""""""""""""""""""""""
Check for cast from non-struct pointer to struct pointer.

.. code-block:: cpp

 // C
 struct s {};

 void test(int *p) {
   struct s *ps = (struct s *) p; // warn
 }

 // C++
 class c {};

 void test(int *p) {
   c *pc = (c *) p; // warn
 }

.. _alpha-core-Conversion:

alpha.core.Conversion (C, C++, ObjC)
""""""""""""""""""""""""""""""""""""
Loss of sign/precision in implicit conversions.

.. code-block:: c

 void test(unsigned U, signed S) {
   if (S > 10) {
     if (U < S) {
     }
   }
   if (S < -10) {
     if (U < S) { // warn (loss of sign)
     }
   }
 }

 void test() {
   long long A = 1LL << 60;
   short X = A; // warn (loss of precision)
 }

.. _alpha-core-DynamicTypeChecker:

alpha.core.DynamicTypeChecker (ObjC)
""""""""""""""""""""""""""""""""""""
Check for cases where the dynamic and the static type of an object are unrelated.


.. code-block:: objc

 id date = [NSDate date];

 // Warning: Object has a dynamic type 'NSDate *' which is
 // incompatible with static type 'NSNumber *'"
 NSNumber *number = date;
 [number doubleValue];

.. _alpha-core-FixedAddr:

alpha.core.FixedAddr (C)
""""""""""""""""""""""""
Check for assignment of a fixed address to a pointer.

.. code-block:: c

 void test() {
   int *p;
   p = (int *) 0x10000; // warn
 }

.. _alpha-core-IdenticalExpr:

alpha.core.IdenticalExpr (C, C++)
"""""""""""""""""""""""""""""""""
Warn about unintended use of identical expressions in operators.

.. code-block:: cpp

 // C
 void test() {
   int a = 5;
   int b = a | 4 | a; // warn: identical expr on both sides
 }

 // C++
 bool f(void);

 void test(bool b) {
   int i = 10;
   if (f()) { // warn: true and false branches are identical
     do {
       i--;
     } while (f());
   } else {
     do {
       i--;
     } while (f());
   }
 }

.. _alpha-core-PointerArithm:

alpha.core.PointerArithm (C)
""""""""""""""""""""""""""""
Check for pointer arithmetic on locations other than array elements.

.. code-block:: c

 void test() {
   int x;
   int *p;
   p = &x + 1; // warn
 }

.. _alpha-core-PointerSub:

alpha.core.PointerSub (C)
"""""""""""""""""""""""""
Check for pointer subtractions on two pointers pointing to different memory chunks.

.. code-block:: c

 void test() {
   int x, y;
   int d = &y - &x; // warn
 }

.. _alpha-core-StackAddressAsyncEscape:

alpha.core.StackAddressAsyncEscape (C)
""""""""""""""""""""""""""""""""""""""
Check that addresses to stack memory do not escape the function that involves dispatch_after or dispatch_async.
This checker is a part of ``core.StackAddressEscape``, but is temporarily disabled until some false positives are fixed.

.. code-block:: c

 dispatch_block_t test_block_inside_block_async_leak() {
   int x = 123;
   void (^inner)(void) = ^void(void) {
     int y = x;
     ++y;
   };
   void (^outer)(void) = ^void(void) {
     int z = x;
     ++z;
     inner();
   };
   return outer; // warn: address of stack-allocated block is captured by a
                 //       returned block
 }

.. _alpha-core-StdVariant:

alpha.core.StdVariant (C++)
"""""""""""""""""""""""""""
Check if a value of active type is retrieved from an ``std::variant`` instance with ``std::get``.
In case of bad variant type access (the accessed type differs from the active type)
a warning is emitted. Currently, this checker does not take exception handling into account.

.. code-block:: cpp

 void test() {
   std::variant<int, char> v = 25;
   char c = stg::get<char>(v); // warn: "int" is the active alternative
 }

.. _alpha-core-TestAfterDivZero:

alpha.core.TestAfterDivZero (C)
"""""""""""""""""""""""""""""""
Check for division by variable that is later compared against 0.
Either the comparison is useless or there is division by zero.

.. code-block:: c

 void test(int x) {
   var = 77 / x;
   if (x == 0) { } // warn
 }

alpha.cplusplus
^^^^^^^^^^^^^^^

.. _alpha-cplusplus-DeleteWithNonVirtualDtor:

alpha.cplusplus.DeleteWithNonVirtualDtor (C++)
""""""""""""""""""""""""""""""""""""""""""""""
Reports destructions of polymorphic objects with a non-virtual destructor in their base class.

.. code-block:: cpp

 class NonVirtual {};
 class NVDerived : public NonVirtual {};

 NonVirtual *create() {
   NonVirtual *x = new NVDerived(); // note: Casting from 'NVDerived' to
                                    //       'NonVirtual' here
   return x;
 }

 void foo() {
   NonVirtual *x = create();
   delete x; // warn: destruction of a polymorphic object with no virtual
             //       destructor
 }

.. _alpha-cplusplus-InvalidatedIterator:

alpha.cplusplus.InvalidatedIterator (C++)
"""""""""""""""""""""""""""""""""""""""""
Check for use of invalidated iterators.

.. code-block:: cpp

 void bad_copy_assign_operator_list1(std::list &L1,
                                     const std::list &L2) {
   auto i0 = L1.cbegin();
   L1 = L2;
   *i0; // warn: invalidated iterator accessed
 }


.. _alpha-cplusplus-IteratorRange:

alpha.cplusplus.IteratorRange (C++)
"""""""""""""""""""""""""""""""""""
Check for iterators used outside their valid ranges.

.. code-block:: cpp

 void simple_bad_end(const std::vector &v) {
   auto i = v.end();
   *i; // warn: iterator accessed outside of its range
 }

.. _alpha-cplusplus-MismatchedIterator:

alpha.cplusplus.MismatchedIterator (C++)
""""""""""""""""""""""""""""""""""""""""
Check for use of iterators of different containers where iterators of the same container are expected.

.. code-block:: cpp

 void bad_insert3(std::vector &v1, std::vector &v2) {
   v2.insert(v1.cbegin(), v2.cbegin(), v2.cend()); // warn: container accessed
                                                   //       using foreign
                                                   //       iterator argument
   v1.insert(v1.cbegin(), v1.cbegin(), v2.cend()); // warn: iterators of
                                                   //       different containers
                                                   //       used where the same
                                                   //       container is
                                                   //       expected
   v1.insert(v1.cbegin(), v2.cbegin(), v1.cend()); // warn: iterators of
                                                   //       different containers
                                                   //       used where the same
                                                   //       container is
                                                   //       expected
 }

.. _alpha-cplusplus-SmartPtr:

alpha.cplusplus.SmartPtr (C++)
""""""""""""""""""""""""""""""
Check for dereference of null smart pointers.

.. code-block:: cpp

 void deref_smart_ptr() {
   std::unique_ptr<int> P;
   *P; // warn: dereference of a default constructed smart unique_ptr
 }


alpha.deadcode
^^^^^^^^^^^^^^
.. _alpha-deadcode-UnreachableCode:

alpha.deadcode.UnreachableCode (C, C++)
"""""""""""""""""""""""""""""""""""""""
Check unreachable code.

.. code-block:: cpp

 // C
 int test() {
   int x = 1;
   while(x);
   return x; // warn
 }

 // C++
 void test() {
   int a = 2;

   while (a > 1)
     a--;

   if (a > 1)
     a++; // warn
 }

 // Objective-C
 void test(id x) {
   return;
   [x retain]; // warn
 }

alpha.fuchsia
^^^^^^^^^^^^^

.. _alpha-fuchsia-lock:

alpha.fuchsia.Lock
""""""""""""""""""
Similarly to :ref:`alpha.unix.PthreadLock <alpha-unix-PthreadLock>`, checks for
the locking/unlocking of fuchsia mutexes.

.. code-block:: cpp

 spin_lock_t mtx1;

 void bad1(void)
 {
   spin_lock(&mtx1);
   spin_lock(&mtx1);	// warn: This lock has already been acquired
 }

alpha.llvm
^^^^^^^^^^

.. _alpha-llvm-Conventions:

alpha.llvm.Conventions
""""""""""""""""""""""

Check code for LLVM codebase conventions:

* A StringRef should not be bound to a temporary std::string whose lifetime is shorter than the StringRef's.
* Clang AST nodes should not have fields that can allocate memory.


alpha.osx
^^^^^^^^^

.. _alpha-osx-cocoa-DirectIvarAssignment:

alpha.osx.cocoa.DirectIvarAssignment (ObjC)
"""""""""""""""""""""""""""""""""""""""""""
Check for direct assignments to instance variables.


.. code-block:: objc

 @interface MyClass : NSObject {}
 @property (readonly) id A;
 - (void) foo;
 @end

 @implementation MyClass
 - (void) foo {
   _A = 0; // warn
 }
 @end

.. _alpha-osx-cocoa-DirectIvarAssignmentForAnnotatedFunctions:

alpha.osx.cocoa.DirectIvarAssignmentForAnnotatedFunctions (ObjC)
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
Check for direct assignments to instance variables in
the methods annotated with ``objc_no_direct_instance_variable_assignment``.

.. code-block:: objc

 @interface MyClass : NSObject {}
 @property (readonly) id A;
 - (void) fAnnotated __attribute__((
     annotate("objc_no_direct_instance_variable_assignment")));
 - (void) fNotAnnotated;
 @end

 @implementation MyClass
 - (void) fAnnotated {
   _A = 0; // warn
 }
 - (void) fNotAnnotated {
   _A = 0; // no warn
 }
 @end


.. _alpha-osx-cocoa-InstanceVariableInvalidation:

alpha.osx.cocoa.InstanceVariableInvalidation (ObjC)
"""""""""""""""""""""""""""""""""""""""""""""""""""
Check that the invalidatable instance variables are
invalidated in the methods annotated with objc_instance_variable_invalidator.

.. code-block:: objc

 @protocol Invalidation <NSObject>
 - (void) invalidate
   __attribute__((annotate("objc_instance_variable_invalidator")));
 @end

 @interface InvalidationImpObj : NSObject <Invalidation>
 @end

 @interface SubclassInvalidationImpObj : InvalidationImpObj {
   InvalidationImpObj *var;
 }
 - (void)invalidate;
 @end

 @implementation SubclassInvalidationImpObj
 - (void) invalidate {}
 @end
 // warn: var needs to be invalidated or set to nil

.. _alpha-osx-cocoa-MissingInvalidationMethod:

alpha.osx.cocoa.MissingInvalidationMethod (ObjC)
""""""""""""""""""""""""""""""""""""""""""""""""
Check that the invalidation methods are present in classes that contain invalidatable instance variables.

.. code-block:: objc

 @protocol Invalidation <NSObject>
 - (void)invalidate
   __attribute__((annotate("objc_instance_variable_invalidator")));
 @end

 @interface NeedInvalidation : NSObject <Invalidation>
 @end

 @interface MissingInvalidationMethodDecl : NSObject {
   NeedInvalidation *Var; // warn
 }
 @end

 @implementation MissingInvalidationMethodDecl
 @end

.. _alpha-osx-cocoa-localizability-PluralMisuseChecker:

alpha.osx.cocoa.localizability.PluralMisuseChecker (ObjC)
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""
Warns against using one vs. many plural pattern in code when generating localized strings.

.. code-block:: objc

 NSString *reminderText =
   NSLocalizedString(@"None", @"Indicates no reminders");
 if (reminderCount == 1) {
   // Warning: Plural cases are not supported across all languages.
   // Use a .stringsdict file instead
   reminderText =
     NSLocalizedString(@"1 Reminder", @"Indicates single reminder");
 } else if (reminderCount >= 2) {
   // Warning: Plural cases are not supported across all languages.
   // Use a .stringsdict file instead
   reminderText =
     [NSString stringWithFormat:
       NSLocalizedString(@"%@ Reminders", @"Indicates multiple reminders"),
         reminderCount];
 }

alpha.security
^^^^^^^^^^^^^^

.. _alpha-security-ArrayBound:

alpha.security.ArrayBound (C)
"""""""""""""""""""""""""""""
Warn about buffer overflows (older checker).

.. code-block:: c

 void test() {
   char *s = "";
   char c = s[1]; // warn
 }

 struct seven_words {
   int c[7];
 };

 void test() {
   struct seven_words a, *p;
   p = &a;
   p[0] = a;
   p[1] = a;
   p[2] = a; // warn
 }

 // note: requires unix.Malloc or
 // alpha.unix.MallocWithAnnotations checks enabled.
 void test() {
   int *p = malloc(12);
   p[3] = 4; // warn
 }

 void test() {
   char a[2];
   int *b = (int*)a;
   b[1] = 3; // warn
 }

.. _alpha-security-ArrayBoundV2:

alpha.security.ArrayBoundV2 (C)
"""""""""""""""""""""""""""""""
Warn about buffer overflows (newer checker).

.. code-block:: c

 void test() {
   char *s = "";
   char c = s[1]; // warn
 }

 void test() {
   int buf[100];
   int *p = buf;
   p = p + 99;
   p[1] = 1; // warn
 }

 // note: compiler has internal check for this.
 // Use -Wno-array-bounds to suppress compiler warning.
 void test() {
   int buf[100][100];
   buf[0][-1] = 1; // warn
 }

 // note: requires alpha.security.taint check turned on.
 void test() {
   char s[] = "abc";
   int x = getchar();
   char c = s[x]; // warn: index is tainted
 }

.. _alpha-security-MallocOverflow:

alpha.security.MallocOverflow (C)
"""""""""""""""""""""""""""""""""
Check for overflows in the arguments to ``malloc()``.
It tries to catch ``malloc(n * c)`` patterns, where:

 - ``n``: a variable or member access of an object
 - ``c``: a constant foldable integral

This checker was designed for code audits, so expect false-positive reports.
One is supposed to silence this checker by ensuring proper bounds checking on
the variable in question using e.g. an ``assert()`` or a branch.

.. code-block:: c

 void test(int n) {
   void *p = malloc(n * sizeof(int)); // warn
 }

 void test2(int n) {
   if (n > 100) // gives an upper-bound
     return;
   void *p = malloc(n * sizeof(int)); // no warning
 }

 void test3(int n) {
   assert(n <= 100 && "Contract violated.");
   void *p = malloc(n * sizeof(int)); // no warning
 }

Limitations:

 - The checker won't warn for variables involved in explicit casts,
   since that might limit the variable's domain.
   E.g.: ``(unsigned char)int x`` would limit the domain to ``[0,255]``.
   The checker will miss the true-positive cases when the explicit cast would
   not tighten the domain to prevent the overflow in the subsequent
   multiplication operation.

 - It is an AST-based checker, thus it does not make use of the
   path-sensitive taint-analysis.

.. _alpha-security-MmapWriteExec:

alpha.security.MmapWriteExec (C)
""""""""""""""""""""""""""""""""
Warn on mmap() calls that are both writable and executable.

.. code-block:: c

 void test(int n) {
   void *c = mmap(NULL, 32, PROT_READ | PROT_WRITE | PROT_EXEC,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
   // warn: Both PROT_WRITE and PROT_EXEC flags are set. This can lead to
   //       exploitable memory regions, which could be overwritten with malicious
   //       code
 }

.. _alpha-security-ReturnPtrRange:

alpha.security.ReturnPtrRange (C)
"""""""""""""""""""""""""""""""""
Check for an out-of-bound pointer being returned to callers.

.. code-block:: c

 static int A[10];

 int *test() {
   int *p = A + 10;
   return p; // warn
 }

 int test(void) {
   int x;
   return x; // warn: undefined or garbage returned
 }


alpha.security.cert
^^^^^^^^^^^^^^^^^^^

SEI CERT checkers which tries to find errors based on their `C coding rules <https://wiki.sei.cmu.edu/confluence/display/c/2+Rules>`_.

alpha.security.taint
^^^^^^^^^^^^^^^^^^^^

Checkers implementing
`taint analysis <https://en.wikipedia.org/wiki/Taint_checking>`_.

.. _alpha-security-taint-GenericTaint:

alpha.security.taint.GenericTaint (C, C++)
""""""""""""""""""""""""""""""""""""""""""

Taint analysis identifies potential security vulnerabilities where the
attacker can inject malicious data to the program to execute an attack
(privilege escalation, command injection, SQL injection etc.).

The malicious data is injected at the taint source (e.g. ``getenv()`` call)
which is then propagated through function calls and being used as arguments of
sensitive operations, also called as taint sinks (e.g. ``system()`` call).

One can defend against this type of vulnerability by always checking and
sanitizing the potentially malicious, untrusted user input.

The goal of the checker is to discover and show to the user these potential
taint source-sink pairs and the propagation call chain.

The most notable examples of taint sources are:

  - data from network
  - files or standard input
  - environment variables
  - data from databases

Let us examine a practical example of a Command Injection attack.

.. code-block:: c

  // Command Injection Vulnerability Example
  int main(int argc, char** argv) {
    char cmd[2048] = "/bin/cat ";
    char filename[1024];
    printf("Filename:");
    scanf (" %1023[^\n]", filename); // The attacker can inject a shell escape here
    strcat(cmd, filename);
    system(cmd); // Warning: Untrusted data is passed to a system call
  }

The program prints the content of any user specified file.
Unfortunately the attacker can execute arbitrary commands
with shell escapes. For example with the following input the `ls` command is also
executed after the contents of `/etc/shadow` is printed.
`Input: /etc/shadow ; ls /`

The analysis implemented in this checker points out this problem.

One can protect against such attack by for example checking if the provided
input refers to a valid file and removing any invalid user input.

.. code-block:: c

  // No vulnerability anymore, but we still get the warning
  void sanitizeFileName(char* filename){
    if (access(filename,F_OK)){// Verifying user input
      printf("File does not exist\n");
      filename[0]='\0';
      }
  }
  int main(int argc, char** argv) {
    char cmd[2048] = "/bin/cat ";
    char filename[1024];
    printf("Filename:");
    scanf (" %1023[^\n]", filename); // The attacker can inject a shell escape here
    sanitizeFileName(filename);// filename is safe after this point
    if (!filename[0])
      return -1;
    strcat(cmd, filename);
    system(cmd); // Superfluous Warning: Untrusted data is passed to a system call
  }

Unfortunately, the checker cannot discover automatically that the programmer
have performed data sanitation, so it still emits the warning.

One can get rid of this superfluous warning by telling by specifying the
sanitation functions in the taint configuration file (see
:doc:`user-docs/TaintAnalysisConfiguration`).

.. code-block:: YAML

  Filters:
  - Name: sanitizeFileName
    Args: [0]

The clang invocation to pass the configuration file location:

.. code-block:: bash

  clang  --analyze -Xclang -analyzer-config  -Xclang alpha.security.taint.TaintPropagation:Config=`pwd`/taint_config.yml ...

If you are validating your inputs instead of sanitizing them, or don't want to
mention each sanitizing function in our configuration,
you can use a more generic approach.

Introduce a generic no-op `csa_mark_sanitized(..)` function to
tell the Clang Static Analyzer
that the variable is safe to be used on that analysis path.

.. code-block:: c

  // Marking sanitized variables safe.
  // No vulnerability anymore, no warning.

  // User csa_mark_sanitize function is for the analyzer only
  #ifdef __clang_analyzer__
    void csa_mark_sanitized(const void *);
  #endif

  int main(int argc, char** argv) {
    char cmd[2048] = "/bin/cat ";
    char filename[1024];
    printf("Filename:");
    scanf (" %1023[^\n]", filename);
    if (access(filename,F_OK)){// Verifying user input
      printf("File does not exist\n");
      return -1;
    }
    #ifdef __clang_analyzer__
      csa_mark_sanitized(filename); // Indicating to CSA that filename variable is safe to be used after this point
    #endif
    strcat(cmd, filename);
    system(cmd); // No warning
  }

Similarly to the previous example, you need to
define a `Filter` function in a `YAML` configuration file
and add the `csa_mark_sanitized` function.

.. code-block:: YAML

  Filters:
  - Name: csa_mark_sanitized
    Args: [0]

Then calling `csa_mark_sanitized(X)` will tell the analyzer that `X` is safe to
be used after this point, because its contents are verified. It is the
responsibility of the programmer to ensure that this verification was indeed
correct. Please note that `csa_mark_sanitized` function is only declared and
used during Clang Static Analysis and skipped in (production) builds.

Further examples of injection vulnerabilities this checker can find.

.. code-block:: c

  void test() {
    char x = getchar(); // 'x' marked as tainted
    system(&x); // warn: untrusted data is passed to a system call
  }

  // note: compiler internally checks if the second param to
  // sprintf is a string literal or not.
  // Use -Wno-format-security to suppress compiler warning.
  void test() {
    char s[10], buf[10];
    fscanf(stdin, "%s", s); // 's' marked as tainted

    sprintf(buf, s); // warn: untrusted data used as a format string
  }

There are built-in sources, propagations and sinks even if no external taint
configuration is provided.

Default sources:
 ``_IO_getc``, ``fdopen``, ``fopen``, ``freopen``, ``get_current_dir_name``,
 ``getch``, ``getchar``, ``getchar_unlocked``, ``getwd``, ``getcwd``,
 ``getgroups``, ``gethostname``, ``getlogin``, ``getlogin_r``, ``getnameinfo``,
 ``gets``, ``gets_s``, ``getseuserbyname``, ``readlink``, ``readlinkat``,
 ``scanf``, ``scanf_s``, ``socket``, ``wgetch``

Default propagations rules:
 ``atoi``, ``atol``, ``atoll``, ``basename``, ``dirname``, ``fgetc``,
 ``fgetln``, ``fgets``, ``fnmatch``, ``fread``, ``fscanf``, ``fscanf_s``,
 ``index``, ``inflate``, ``isalnum``, ``isalpha``, ``isascii``, ``isblank``,
 ``iscntrl``, ``isdigit``, ``isgraph``, ``islower``, ``isprint``, ``ispunct``,
 ``isspace``, ``isupper``, ``isxdigit``, ``memchr``, ``memrchr``, ``sscanf``,
 ``getc``, ``getc_unlocked``, ``getdelim``, ``getline``, ``getw``, ``memcmp``,
 ``memcpy``, ``memmem``, ``memmove``, ``mbtowc``, ``pread``, ``qsort``,
 ``qsort_r``, ``rawmemchr``, ``read``, ``recv``, ``recvfrom``, ``rindex``,
 ``strcasestr``, ``strchr``, ``strchrnul``, ``strcasecmp``, ``strcmp``,
 ``strcspn``, ``strncasecmp``, ``strncmp``, ``strndup``,
 ``strndupa``, ``strpbrk``, ``strrchr``, ``strsep``, ``strspn``,
 ``strstr``, ``strtol``, ``strtoll``, ``strtoul``, ``strtoull``, ``tolower``,
 ``toupper``, ``ttyname``, ``ttyname_r``, ``wctomb``, ``wcwidth``

Default sinks:
 ``printf``, ``setproctitle``, ``system``, ``popen``, ``execl``, ``execle``,
 ``execlp``, ``execv``, ``execvp``, ``execvP``, ``execve``, ``dlopen``

Please note that there are no built-in filter functions.

One can configure their own taint sources, sinks, and propagation rules by
providing a configuration file via checker option
``alpha.security.taint.TaintPropagation:Config``. The configuration file is in
`YAML <http://llvm.org/docs/YamlIO.html#introduction-to-yaml>`_ format. The
taint-related options defined in the config file extend but do not override the
built-in sources, rules, sinks. The format of the external taint configuration
file is not stable, and could change without any notice even in a non-backward
compatible way.

For a more detailed description of configuration options, please see the
:doc:`user-docs/TaintAnalysisConfiguration`. For an example see
:ref:`clangsa-taint-configuration-example`.

**Configuration**

* `Config`  Specifies the name of the YAML configuration file. The user can
  define their own taint sources and sinks.

**Related Guidelines**

* `CWE Data Neutralization Issues
  <https://cwe.mitre.org/data/definitions/137.html>`_
* `SEI Cert STR02-C. Sanitize data passed to complex subsystems
  <https://wiki.sei.cmu.edu/confluence/display/c/STR02-C.+Sanitize+data+passed+to+complex+subsystems>`_
* `SEI Cert ENV33-C. Do not call system()
  <https://wiki.sei.cmu.edu/confluence/pages/viewpage.action?pageId=87152177>`_
* `ENV03-C. Sanitize the environment when invoking external programs
  <https://wiki.sei.cmu.edu/confluence/display/c/ENV03-C.+Sanitize+the+environment+when+invoking+external+programs>`_

**Limitations**

* The taintedness property is not propagated through function calls which are
  unknown (or too complex) to the analyzer, unless there is a specific
  propagation rule built-in to the checker or given in the YAML configuration
  file. This causes potential true positive findings to be lost.

alpha.unix
^^^^^^^^^^

.. _alpha-unix-Chroot:

alpha.unix.Chroot (C)
"""""""""""""""""""""
Check improper use of chroot.

.. code-block:: c

 void f();

 void test() {
   chroot("/usr/local");
   f(); // warn: no call of chdir("/") immediately after chroot
 }

.. _alpha-unix-PthreadLock:

alpha.unix.PthreadLock (C)
""""""""""""""""""""""""""
Simple lock -> unlock checker.
Applies to: ``pthread_mutex_lock, pthread_rwlock_rdlock, pthread_rwlock_wrlock, lck_mtx_lock, lck_rw_lock_exclusive``
``lck_rw_lock_shared, pthread_mutex_trylock, pthread_rwlock_tryrdlock, pthread_rwlock_tryrwlock, lck_mtx_try_lock,
lck_rw_try_lock_exclusive, lck_rw_try_lock_shared, pthread_mutex_unlock, pthread_rwlock_unlock, lck_mtx_unlock, lck_rw_done``.


.. code-block:: c

 pthread_mutex_t mtx;

 void test() {
   pthread_mutex_lock(&mtx);
   pthread_mutex_lock(&mtx);
     // warn: this lock has already been acquired
 }

 lck_mtx_t lck1, lck2;

 void test() {
   lck_mtx_lock(&lck1);
   lck_mtx_lock(&lck2);
   lck_mtx_unlock(&lck1);
     // warn: this was not the most recently acquired lock
 }

 lck_mtx_t lck1, lck2;

 void test() {
   if (lck_mtx_try_lock(&lck1) == 0)
     return;

   lck_mtx_lock(&lck2);
   lck_mtx_unlock(&lck1);
     // warn: this was not the most recently acquired lock
 }

.. _alpha-unix-SimpleStream:

alpha.unix.SimpleStream (C)
"""""""""""""""""""""""""""
Check for misuses of stream APIs. Check for misuses of stream APIs: ``fopen, fclose``
(demo checker, the subject of the demo (`Slides <https://llvm.org/devmtg/2012-11/Zaks-Rose-Checker24Hours.pdf>`_ ,
`Video <https://youtu.be/kdxlsP5QVPw>`_) by Anna Zaks and Jordan Rose presented at the
`2012 LLVM Developers' Meeting <https://llvm.org/devmtg/2012-11/>`_).

.. code-block:: c

 void test() {
   FILE *F = fopen("myfile.txt", "w");
 } // warn: opened file is never closed

 void test() {
   FILE *F = fopen("myfile.txt", "w");

   if (F)
     fclose(F);

   fclose(F); // warn: closing a previously closed file stream
 }

.. _alpha-unix-cstring-BufferOverlap:

alpha.unix.cstring.BufferOverlap (C)
""""""""""""""""""""""""""""""""""""
Checks for overlap in two buffer arguments. Applies to:  ``memcpy, mempcpy, wmemcpy, wmempcpy``.

.. code-block:: c

 void test() {
   int a[4] = {0};
   memcpy(a + 2, a + 1, 8); // warn
 }

.. _alpha-unix-cstring-NotNullTerminated:

alpha.unix.cstring.NotNullTerminated (C)
""""""""""""""""""""""""""""""""""""""""
Check for arguments which are not null-terminated strings; applies to: ``strlen, strnlen, strcpy, strncpy, strcat, strncat, wcslen, wcsnlen``.

.. code-block:: c

 void test() {
   int y = strlen((char *)&test); // warn
 }

.. _alpha-unix-cstring-OutOfBounds:

alpha.unix.cstring.OutOfBounds (C)
""""""""""""""""""""""""""""""""""
Check for out-of-bounds access in string functions, such as:
``memcpy, bcopy, strcpy, strncpy, strcat, strncat, memmove, memcmp, memset`` and more.

This check also works with string literals, except there is a known bug in that
the analyzer cannot detect embedded NULL characters when determining the string length.

.. code-block:: c

 void test1() {
   const char str[] = "Hello world";
   char buffer[] = "Hello world";
   memcpy(buffer, str, sizeof(str) + 1); // warn
 }

 void test2() {
   const char str[] = "Hello world";
   char buffer[] = "Helloworld";
   memcpy(buffer, str, sizeof(str)); // warn
 }

.. _alpha-unix-cstring-UninitializedRead:

alpha.unix.cstring.UninitializedRead (C)
""""""""""""""""""""""""""""""""""""""""
Check for uninitialized reads from common memory copy/manipulation functions such as:
 ``memcpy, mempcpy, memmove, memcmp, strcmp, strncmp, strcpy, strlen, strsep`` and many more.

.. code-block:: c

 void test() {
  char src[10];
  char dst[5];
  memcpy(dst,src,sizeof(dst)); // warn: Bytes string function accesses uninitialized/garbage values
 }

Limitations:

   - Due to limitations of the memory modeling in the analyzer, one can likely
     observe a lot of false-positive reports like this:

      .. code-block:: c

        void false_positive() {
          int src[] = {1, 2, 3, 4};
          int dst[5] = {0};
          memcpy(dst, src, 4 * sizeof(int)); // false-positive:
          // The 'src' buffer was correctly initialized, yet we cannot conclude
          // that since the analyzer could not see a direct initialization of the
          // very last byte of the source buffer.
        }

     More details at the corresponding `GitHub issue <https://github.com/llvm/llvm-project/issues/43459>`_.

.. _alpha-nondeterminism-PointerIteration:

alpha.nondeterminism.PointerIteration (C++)
"""""""""""""""""""""""""""""""""""""""""""
Check for non-determinism caused by iterating unordered containers of pointers.

.. code-block:: c

 void test() {
  int a = 1, b = 2;
  std::unordered_set<int *> UnorderedPtrSet = {&a, &b};

  for (auto i : UnorderedPtrSet) // warn
    f(i);
 }

.. _alpha-nondeterminism-PointerSorting:

alpha.nondeterminism.PointerSorting (C++)
"""""""""""""""""""""""""""""""""""""""""
Check for non-determinism caused by sorting of pointers.

.. code-block:: c

 void test() {
  int a = 1, b = 2;
  std::vector<int *> V = {&a, &b};
  std::sort(V.begin(), V.end()); // warn
 }


alpha.WebKit
^^^^^^^^^^^^

.. _alpha-webkit-UncountedCallArgsChecker:

alpha.webkit.UncountedCallArgsChecker
"""""""""""""""""""""""""""""""""""""
The goal of this rule is to make sure that lifetime of any dynamically allocated ref-countable object passed as a call argument spans past the end of the call. This applies to call to any function, method, lambda, function pointer or functor. Ref-countable types aren't supposed to be allocated on stack so we check arguments for parameters of raw pointers and references to uncounted types.

Here are some examples of situations that we warn about as they *might* be potentially unsafe. The logic is that either we're able to guarantee that an argument is safe or it's considered if not a bug then bug-prone.

  .. code-block:: cpp

    RefCountable* provide_uncounted();
    void consume(RefCountable*);

    // In these cases we can't make sure callee won't directly or indirectly call `deref()` on the argument which could make it unsafe from such point until the end of the call.

    void foo1() {
      consume(provide_uncounted()); // warn
    }

    void foo2() {
      RefCountable* uncounted = provide_uncounted();
      consume(uncounted); // warn
    }

Although we are enforcing member variables to be ref-counted by `webkit.NoUncountedMemberChecker` any method of the same class still has unrestricted access to these. Since from a caller's perspective we can't guarantee a particular member won't get modified by callee (directly or indirectly) we don't consider values obtained from members safe.

Note: It's likely this heuristic could be made more precise with fewer false positives - for example calls to free functions that don't have any parameter other than the pointer should be safe as the callee won't be able to tamper with the member unless it's a global variable.

  .. code-block:: cpp

    struct Foo {
      RefPtr<RefCountable> member;
      void consume(RefCountable*) { /* ... */ }
      void bugprone() {
        consume(member.get()); // warn
      }
    };

The implementation of this rule is a heuristic - we define a whitelist of kinds of values that are considered safe to be passed as arguments. If we can't prove an argument is safe it's considered an error.

Allowed kinds of arguments:

- values obtained from ref-counted objects (including temporaries as those survive the call too)

  .. code-block:: cpp

    RefCountable* provide_uncounted();
    void consume(RefCountable*);

    void foo() {
      RefPtr<RefCountable> rc = makeRef(provide_uncounted());
      consume(rc.get()); // ok
      consume(makeRef(provide_uncounted()).get()); // ok
    }

- forwarding uncounted arguments from caller to callee

  .. code-block:: cpp

    void foo(RefCountable& a) {
      bar(a); // ok
    }

  Caller of ``foo()`` is responsible for  ``a``'s lifetime.

- ``this`` pointer

  .. code-block:: cpp

    void Foo::foo() {
      baz(this);  // ok
    }

  Caller of ``foo()`` is responsible for keeping the memory pointed to by ``this`` pointer safe.

- constants

  .. code-block:: cpp

    foo(nullptr, NULL, 0); // ok

We also define a set of safe transformations which if passed a safe value as an input provide (usually it's the return value) a safe value (or an object that provides safe values). This is also a heuristic.

- constructors of ref-counted types (including factory methods)
- getters of ref-counted types
- member overloaded operators
- casts
- unary operators like ``&`` or ``*``

alpha.webkit.UncountedLocalVarsChecker
""""""""""""""""""""""""""""""""""""""
The goal of this rule is to make sure that any uncounted local variable is backed by a ref-counted object with lifetime that is strictly larger than the scope of the uncounted local variable. To be on the safe side we require the scope of an uncounted variable to be embedded in the scope of ref-counted object that backs it.

These are examples of cases that we consider safe:

  .. code-block:: cpp

    void foo1() {
      RefPtr<RefCountable> counted;
      // The scope of uncounted is EMBEDDED in the scope of counted.
      {
        RefCountable* uncounted = counted.get(); // ok
      }
    }

    void foo2(RefPtr<RefCountable> counted_param) {
      RefCountable* uncounted = counted_param.get(); // ok
    }

    void FooClass::foo_method() {
      RefCountable* uncounted = this; // ok
    }

Here are some examples of situations that we warn about as they *might* be potentially unsafe. The logic is that either we're able to guarantee that an argument is safe or it's considered if not a bug then bug-prone.

  .. code-block:: cpp

    void foo1() {
      RefCountable* uncounted = new RefCountable; // warn
    }

    RefCountable* global_uncounted;
    void foo2() {
      RefCountable* uncounted = global_uncounted; // warn
    }

    void foo3() {
      RefPtr<RefCountable> counted;
      // The scope of uncounted is not EMBEDDED in the scope of counted.
      RefCountable* uncounted = counted.get(); // warn
    }

We don't warn about these cases - we don't consider them necessarily safe but since they are very common and usually safe we'd introduce a lot of false positives otherwise:
- variable defined in condition part of an ```if``` statement
- variable defined in init statement condition of a ```for``` statement

For the time being we also don't warn about uninitialized uncounted local variables.

Debug Checkers
---------------

.. _debug-checkers:


debug
^^^^^

Checkers used for debugging the analyzer.
:doc:`developer-docs/DebugChecks` page contains a detailed description.

.. _debug-AnalysisOrder:

debug.AnalysisOrder
"""""""""""""""""""
Print callbacks that are called during analysis in order.

.. _debug-ConfigDumper:

debug.ConfigDumper
""""""""""""""""""
Dump config table.

.. _debug-DumpCFG Display:

debug.DumpCFG Display
"""""""""""""""""""""
Control-Flow Graphs.

.. _debug-DumpCallGraph:

debug.DumpCallGraph
"""""""""""""""""""
Display Call Graph.

.. _debug-DumpCalls:

debug.DumpCalls
"""""""""""""""
Print calls as they are traversed by the engine.

.. _debug-DumpDominators:

debug.DumpDominators
""""""""""""""""""""
Print the dominance tree for a given CFG.

.. _debug-DumpLiveVars:

debug.DumpLiveVars
""""""""""""""""""
Print results of live variable analysis.

.. _debug-DumpTraversal:

debug.DumpTraversal
"""""""""""""""""""
Print branch conditions as they are traversed by the engine.

.. _debug-ExprInspection:

debug.ExprInspection
""""""""""""""""""""
Check the analyzer's understanding of expressions.

.. _debug-Stats:

debug.Stats
"""""""""""
Emit warnings with analyzer statistics.

.. _debug-TaintTest:

debug.TaintTest
"""""""""""""""
Mark tainted symbols as such.

.. _debug-ViewCFG:

debug.ViewCFG
"""""""""""""
View Control-Flow Graphs using GraphViz.

.. _debug-ViewCallGraph:

debug.ViewCallGraph
"""""""""""""""""""
View Call Graph using GraphViz.

.. _debug-ViewExplodedGraph:

debug.ViewExplodedGraph
"""""""""""""""""""""""
View Exploded Graphs using GraphViz.
