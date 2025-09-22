====================
Objective-C Literals
====================

Introduction
============

Three new features were introduced into clang at the same time:
*NSNumber Literals* provide a syntax for creating ``NSNumber`` from
scalar literal expressions; *Collection Literals* provide a short-hand
for creating arrays and dictionaries; *Object Subscripting* provides a
way to use subscripting with Objective-C objects. Users of Apple
compiler releases can use these features starting with the Apple LLVM
Compiler 4.0. Users of open-source LLVM.org compiler releases can use
these features starting with clang v3.1.

These language additions simplify common Objective-C programming
patterns, make programs more concise, and improve the safety of
container creation.

This document describes how the features are implemented in clang, and
how to use them in your own programs.

NSNumber Literals
=================

The framework class ``NSNumber`` is used to wrap scalar values inside
objects: signed and unsigned integers (``char``, ``short``, ``int``,
``long``, ``long long``), floating point numbers (``float``,
``double``), and boolean values (``BOOL``, C++ ``bool``). Scalar values
wrapped in objects are also known as *boxed* values.

In Objective-C, any character, numeric or boolean literal prefixed with
the ``'@'`` character will evaluate to a pointer to an ``NSNumber``
object initialized with that value. C's type suffixes may be used to
control the size of numeric literals.

Examples
--------

The following program illustrates the rules for ``NSNumber`` literals:

.. code-block:: objc

    void main(int argc, const char *argv[]) {
      // character literals.
      NSNumber *theLetterZ = @'Z';          // equivalent to [NSNumber numberWithChar:'Z']

      // integral literals.
      NSNumber *fortyTwo = @42;             // equivalent to [NSNumber numberWithInt:42]
      NSNumber *fortyTwoUnsigned = @42U;    // equivalent to [NSNumber numberWithUnsignedInt:42U]
      NSNumber *fortyTwoLong = @42L;        // equivalent to [NSNumber numberWithLong:42L]
      NSNumber *fortyTwoLongLong = @42LL;   // equivalent to [NSNumber numberWithLongLong:42LL]

      // floating point literals.
      NSNumber *piFloat = @3.141592654F;    // equivalent to [NSNumber numberWithFloat:3.141592654F]
      NSNumber *piDouble = @3.1415926535;   // equivalent to [NSNumber numberWithDouble:3.1415926535]

      // BOOL literals.
      NSNumber *yesNumber = @YES;           // equivalent to [NSNumber numberWithBool:YES]
      NSNumber *noNumber = @NO;             // equivalent to [NSNumber numberWithBool:NO]

    #ifdef __cplusplus
      NSNumber *trueNumber = @true;         // equivalent to [NSNumber numberWithBool:(BOOL)true]
      NSNumber *falseNumber = @false;       // equivalent to [NSNumber numberWithBool:(BOOL)false]
    #endif
    }

Discussion
----------

NSNumber literals only support literal scalar values after the ``'@'``.
Consequently, ``@INT_MAX`` works, but ``@INT_MIN`` does not, because
they are defined like this:

.. code-block:: objc

    #define INT_MAX   2147483647  /* max value for an int */
    #define INT_MIN   (-2147483647-1) /* min value for an int */

The definition of ``INT_MIN`` is not a simple literal, but a
parenthesized expression. Parenthesized expressions are supported using
the `boxed expression <#objc_boxed_expressions>`_ syntax, which is
described in the next section.

Because ``NSNumber`` does not currently support wrapping ``long double``
values, the use of a ``long double NSNumber`` literal (e.g.
``@123.23L``) will be rejected by the compiler.

Previously, the ``BOOL`` type was simply a typedef for ``signed char``,
and ``YES`` and ``NO`` were macros that expand to ``(BOOL)1`` and
``(BOOL)0`` respectively. To support ``@YES`` and ``@NO`` expressions,
these macros are now defined using new language keywords in
``<objc/objc.h>``:

.. code-block:: objc

    #if __has_feature(objc_bool)
    #define YES             __objc_yes
    #define NO              __objc_no
    #else
    #define YES             ((BOOL)1)
    #define NO              ((BOOL)0)
    #endif

The compiler implicitly converts ``__objc_yes`` and ``__objc_no`` to
``(BOOL)1`` and ``(BOOL)0``. The keywords are used to disambiguate
``BOOL`` and integer literals.

Objective-C++ also supports ``@true`` and ``@false`` expressions, which
are equivalent to ``@YES`` and ``@NO``.

Boxed Expressions
=================

Objective-C provides a new syntax for boxing C expressions:

.. code-block:: objc

    @( <expression> )

Expressions of scalar (numeric, enumerated, BOOL), C string pointer
and some C structures (via NSValue) are supported:

.. code-block:: objc

    // numbers.
    NSNumber *smallestInt = @(-INT_MAX - 1);  // [NSNumber numberWithInt:(-INT_MAX - 1)]
    NSNumber *piOverTwo = @(M_PI / 2);        // [NSNumber numberWithDouble:(M_PI / 2)]

    // enumerated types.
    typedef enum { Red, Green, Blue } Color;
    NSNumber *favoriteColor = @(Green);       // [NSNumber numberWithInt:((int)Green)]

    // strings.
    NSString *path = @(getenv("PATH"));       // [NSString stringWithUTF8String:(getenv("PATH"))]
    NSArray *pathComponents = [path componentsSeparatedByString:@":"];

    // structs.
    NSValue *center = @(view.center);         // Point p = view.center;
                                              // [NSValue valueWithBytes:&p objCType:@encode(Point)];
    NSValue *frame = @(view.frame);           // Rect r = view.frame;
                                              // [NSValue valueWithBytes:&r objCType:@encode(Rect)];

Boxed Enums
-----------

Cocoa frameworks frequently define constant values using *enums.*
Although enum values are integral, they may not be used directly as
boxed literals (this avoids conflicts with future ``'@'``-prefixed
Objective-C keywords). Instead, an enum value must be placed inside a
boxed expression. The following example demonstrates configuring an
``AVAudioRecorder`` using a dictionary that contains a boxed enumeration
value:

.. code-block:: objc

    enum {
      AVAudioQualityMin = 0,
      AVAudioQualityLow = 0x20,
      AVAudioQualityMedium = 0x40,
      AVAudioQualityHigh = 0x60,
      AVAudioQualityMax = 0x7F
    };

    - (AVAudioRecorder *)recordToFile:(NSURL *)fileURL {
      NSDictionary *settings = @{ AVEncoderAudioQualityKey : @(AVAudioQualityMax) };
      return [[AVAudioRecorder alloc] initWithURL:fileURL settings:settings error:NULL];
    }

The expression ``@(AVAudioQualityMax)`` converts ``AVAudioQualityMax``
to an integer type, and boxes the value accordingly. If the enum has a
:ref:`fixed underlying type <objc-fixed-enum>` as in:

.. code-block:: objc

    typedef enum : unsigned char { Red, Green, Blue } Color;
    NSNumber *red = @(Red), *green = @(Green), *blue = @(Blue); // => [NSNumber numberWithUnsignedChar:]

then the fixed underlying type will be used to select the correct
``NSNumber`` creation method.

Boxing a value of enum type will result in a ``NSNumber`` pointer with a
creation method according to the underlying type of the enum, which can
be a :ref:`fixed underlying type <objc-fixed-enum>`
or a compiler-defined integer type capable of representing the values of
all the members of the enumeration:

.. code-block:: objc

    typedef enum : unsigned char { Red, Green, Blue } Color;
    Color col = Red;
    NSNumber *nsCol = @(col); // => [NSNumber numberWithUnsignedChar:]

Boxed C Strings
---------------

A C string literal prefixed by the ``'@'`` token denotes an ``NSString``
literal in the same way a numeric literal prefixed by the ``'@'`` token
denotes an ``NSNumber`` literal. When the type of the parenthesized
expression is ``(char *)`` or ``(const char *)``, the result of the
boxed expression is a pointer to an ``NSString`` object containing
equivalent character data, which is assumed to be '\\0'-terminated and
UTF-8 encoded. The following example converts C-style command line
arguments into ``NSString`` objects.

.. code-block:: objc

    // Partition command line arguments into positional and option arguments.
    NSMutableArray *args = [NSMutableArray new];
    NSMutableDictionary *options = [NSMutableDictionary new];
    while (--argc) {
        const char *arg = *++argv;
        if (strncmp(arg, "--", 2) == 0) {
            options[@(arg + 2)] = @(*++argv);   // --key value
        } else {
            [args addObject:@(arg)];            // positional argument
        }
    }

As with all C pointers, character pointer expressions can involve
arbitrary pointer arithmetic, therefore programmers must ensure that the
character data is valid. Passing ``NULL`` as the character pointer will
raise an exception at runtime. When possible, the compiler will reject
``NULL`` character pointers used in boxed expressions.

Boxed C Structures
------------------

Boxed expressions support construction of NSValue objects.
It said that C structures can be used, the only requirement is:
structure should be marked with ``objc_boxable`` attribute.
To support older version of frameworks and/or third-party libraries
you may need to add the attribute via ``typedef``.

.. code-block:: objc

    struct __attribute__((objc_boxable)) Point {
        // ...
    };

    typedef struct __attribute__((objc_boxable)) _Size {
        // ...
    } Size;

    typedef struct _Rect {
        // ...
    } Rect;

    struct Point p;
    NSValue *point = @(p);          // ok
    Size s;
    NSValue *size = @(s);           // ok

    Rect r;
    NSValue *bad_rect = @(r);       // error

    typedef struct __attribute__((objc_boxable)) _Rect Rect;

    NSValue *good_rect = @(r);      // ok


Container Literals
==================

Objective-C now supports a new expression syntax for creating immutable
array and dictionary container objects.

Examples
--------

Immutable array expression:

.. code-block:: objc

    NSArray *array = @[ @"Hello", NSApp, [NSNumber numberWithInt:42] ];

This creates an ``NSArray`` with 3 elements. The comma-separated
sub-expressions of an array literal can be any Objective-C object
pointer typed expression.

Immutable dictionary expression:

.. code-block:: objc

    NSDictionary *dictionary = @{
        @"name" : NSUserName(),
        @"date" : [NSDate date],
        @"processInfo" : [NSProcessInfo processInfo]
    };

This creates an ``NSDictionary`` with 3 key/value pairs. Value
sub-expressions of a dictionary literal must be Objective-C object
pointer typed, as in array literals. Key sub-expressions must be of an
Objective-C object pointer type that implements the
``<NSCopying>`` protocol.

Discussion
----------

Neither keys nor values can have the value ``nil`` in containers. If the
compiler can prove that a key or value is ``nil`` at compile time, then
a warning will be emitted. Otherwise, a runtime error will occur.

Using array and dictionary literals is safer than the variadic creation
forms commonly in use today. Array literal expressions expand to calls
to ``+[NSArray arrayWithObjects:count:]``, which validates that all
objects are non-``nil``. The variadic form,
``+[NSArray arrayWithObjects:]`` uses ``nil`` as an argument list
terminator, which can lead to malformed array objects. Dictionary
literals are similarly created with
``+[NSDictionary dictionaryWithObjects:forKeys:count:]`` which validates
all objects and keys, unlike
``+[NSDictionary dictionaryWithObjectsAndKeys:]`` which also uses a
``nil`` parameter as an argument list terminator.

Object Subscripting
===================

Objective-C object pointer values can now be used with C's subscripting
operator.

Examples
--------

The following code demonstrates the use of object subscripting syntax
with ``NSMutableArray`` and ``NSMutableDictionary`` objects:

.. code-block:: objc

    NSMutableArray *array = ...;
    NSUInteger idx = ...;
    id newObject = ...;
    id oldObject = array[idx];
    array[idx] = newObject;         // replace oldObject with newObject

    NSMutableDictionary *dictionary = ...;
    NSString *key = ...;
    oldObject = dictionary[key];
    dictionary[key] = newObject;    // replace oldObject with newObject

The next section explains how subscripting expressions map to accessor
methods.

Subscripting Methods
--------------------

Objective-C supports two kinds of subscript expressions: *array-style*
subscript expressions use integer typed subscripts; *dictionary-style*
subscript expressions use Objective-C object pointer typed subscripts.
Each type of subscript expression is mapped to a message send using a
predefined selector. The advantage of this design is flexibility: class
designers are free to introduce subscripting by declaring methods or by
adopting protocols. Moreover, because the method names are selected by
the type of the subscript, an object can be subscripted using both array
and dictionary styles.

Array-Style Subscripting
^^^^^^^^^^^^^^^^^^^^^^^^

When the subscript operand has an integral type, the expression is
rewritten to use one of two different selectors, depending on whether
the element is being read or written. When an expression reads an
element using an integral index, as in the following example:

.. code-block:: objc

    NSUInteger idx = ...;
    id value = object[idx];

it is translated into a call to ``objectAtIndexedSubscript:``

.. code-block:: objc

    id value = [object objectAtIndexedSubscript:idx];

When an expression writes an element using an integral index:

.. code-block:: objc

    object[idx] = newValue;

it is translated to a call to ``setObject:atIndexedSubscript:``

.. code-block:: objc

    [object setObject:newValue atIndexedSubscript:idx];

These message sends are then type-checked and performed just like
explicit message sends. The method used for objectAtIndexedSubscript:
must be declared with an argument of integral type and a return value of
some Objective-C object pointer type. The method used for
setObject:atIndexedSubscript: must be declared with its first argument
having some Objective-C pointer type and its second argument having
integral type.

The meaning of indexes is left up to the declaring class. The compiler
will coerce the index to the appropriate argument type of the method it
uses for type-checking. For an instance of ``NSArray``, reading an
element using an index outside the range ``[0, array.count)`` will raise
an exception. For an instance of ``NSMutableArray``, assigning to an
element using an index within this range will replace that element, but
assigning to an element using an index outside this range will raise an
exception; no syntax is provided for inserting, appending, or removing
elements for mutable arrays.

A class need not declare both methods in order to take advantage of this
language feature. For example, the class ``NSArray`` declares only
``objectAtIndexedSubscript:``, so that assignments to elements will fail
to type-check; moreover, its subclass ``NSMutableArray`` declares
``setObject:atIndexedSubscript:``.

Dictionary-Style Subscripting
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When the subscript operand has an Objective-C object pointer type, the
expression is rewritten to use one of two different selectors, depending
on whether the element is being read from or written to. When an
expression reads an element using an Objective-C object pointer
subscript operand, as in the following example:

.. code-block:: objc

    id key = ...;
    id value = object[key];

it is translated into a call to the ``objectForKeyedSubscript:`` method:

.. code-block:: objc

    id value = [object objectForKeyedSubscript:key];

When an expression writes an element using an Objective-C object pointer
subscript:

.. code-block:: objc

    object[key] = newValue;

it is translated to a call to ``setObject:forKeyedSubscript:``

.. code-block:: objc

    [object setObject:newValue forKeyedSubscript:key];

The behavior of ``setObject:forKeyedSubscript:`` is class-specific; but
in general it should replace an existing value if one is already
associated with a key, otherwise it should add a new value for the key.
No syntax is provided for removing elements from mutable dictionaries.

Discussion
----------

An Objective-C subscript expression occurs when the base operand of the
C subscript operator has an Objective-C object pointer type. Since this
potentially collides with pointer arithmetic on the value, these
expressions are only supported under the modern Objective-C runtime,
which categorically forbids such arithmetic.

Currently, only subscripts of integral or Objective-C object pointer
type are supported. In C++, a class type can be used if it has a single
conversion function to an integral or Objective-C pointer type, in which
case that conversion is applied and analysis continues as appropriate.
Otherwise, the expression is ill-formed.

An Objective-C object subscript expression is always an l-value. If the
expression appears on the left-hand side of a simple assignment operator
(=), the element is written as described below. If the expression
appears on the left-hand side of a compound assignment operator (e.g.
+=), the program is ill-formed, because the result of reading an element
is always an Objective-C object pointer and no binary operators are
legal on such pointers. If the expression appears in any other position,
the element is read as described below. It is an error to take the
address of a subscript expression, or (in C++) to bind a reference to
it.

Programs can use object subscripting with Objective-C object pointers of
type ``id``. Normal dynamic message send rules apply; the compiler must
see *some* declaration of the subscripting methods, and will pick the
declaration seen first.

Caveats
=======

Objects created using the literal or boxed expression syntax are not
guaranteed to be uniqued by the runtime, but nor are they guaranteed to
be newly-allocated. As such, the result of performing direct comparisons
against the location of an object literal (using ``==``, ``!=``, ``<``,
``<=``, ``>``, or ``>=``) is not well-defined. This is usually a simple
mistake in code that intended to call the ``isEqual:`` method (or the
``compare:`` method).

This caveat applies to compile-time string literals as well.
Historically, string literals (using the ``@"..."`` syntax) have been
uniqued across translation units during linking. This is an
implementation detail of the compiler and should not be relied upon. If
you are using such code, please use global string constants instead
(``NSString * const MyConst = @"..."``) or use ``isEqual:``.

Grammar Additions
=================

To support the new syntax described above, the Objective-C
``@``-expression grammar has the following new productions:

::

    objc-at-expression : '@' (string-literal | encode-literal | selector-literal | protocol-literal | object-literal)
                       ;

    object-literal : ('+' | '-')? numeric-constant
                   | character-constant
                   | boolean-constant
                   | array-literal
                   | dictionary-literal
                   ;

    boolean-constant : '__objc_yes' | '__objc_no' | 'true' | 'false'  /* boolean keywords. */
                     ;

    array-literal : '[' assignment-expression-list ']'
                  ;

    assignment-expression-list : assignment-expression (',' assignment-expression-list)?
                               | /* empty */
                               ;

    dictionary-literal : '{' key-value-list '}'
                       ;

    key-value-list : key-value-pair (',' key-value-list)?
                   | /* empty */
                   ;

    key-value-pair : assignment-expression ':' assignment-expression
                   ;

Note: ``@true`` and ``@false`` are only supported in Objective-C++.

Availability Checks
===================

Programs test for the new features by using clang's \_\_has\_feature
checks. Here are examples of their use:

.. code-block:: objc

    #if __has_feature(objc_array_literals)
        // new way.
        NSArray *elements = @[ @"H", @"He", @"O", @"C" ];
    #else
        // old way (equivalent).
        id objects[] = { @"H", @"He", @"O", @"C" };
        NSArray *elements = [NSArray arrayWithObjects:objects count:4];
    #endif

    #if __has_feature(objc_dictionary_literals)
        // new way.
        NSDictionary *masses = @{ @"H" : @1.0078,  @"He" : @4.0026, @"O" : @15.9990, @"C" : @12.0096 };
    #else
        // old way (equivalent).
        id keys[] = { @"H", @"He", @"O", @"C" };
        id values[] = { [NSNumber numberWithDouble:1.0078], [NSNumber numberWithDouble:4.0026],
                        [NSNumber numberWithDouble:15.9990], [NSNumber numberWithDouble:12.0096] };
        NSDictionary *masses = [NSDictionary dictionaryWithObjects:objects forKeys:keys count:4];
    #endif

    #if __has_feature(objc_subscripting)
        NSUInteger i, count = elements.count;
        for (i = 0; i < count; ++i) {
            NSString *element = elements[i];
            NSNumber *mass = masses[element];
            NSLog(@"the mass of %@ is %@", element, mass);
        }
    #else
        NSUInteger i, count = [elements count];
        for (i = 0; i < count; ++i) {
            NSString *element = [elements objectAtIndex:i];
            NSNumber *mass = [masses objectForKey:element];
            NSLog(@"the mass of %@ is %@", element, mass);
        }
    #endif

    #if __has_attribute(objc_boxable)
        typedef struct __attribute__((objc_boxable)) _Rect Rect;
    #endif

    #if __has_feature(objc_boxed_nsvalue_expressions)
        CABasicAnimation animation = [CABasicAnimation animationWithKeyPath:@"position"];
        animation.fromValue = @(layer.position);
        animation.toValue = @(newPosition);
        [layer addAnimation:animation forKey:@"move"];
    #else
        CABasicAnimation animation = [CABasicAnimation animationWithKeyPath:@"position"];
        animation.fromValue = [NSValue valueWithCGPoint:layer.position];
        animation.toValue = [NSValue valueWithCGPoint:newPosition];
        [layer addAnimation:animation forKey:@"move"];
    #endif

Code can use also ``__has_feature(objc_bool)`` to check for the
availability of numeric literals support. This checks for the new
``__objc_yes / __objc_no`` keywords, which enable the use of
``@YES / @NO`` literals.

To check whether boxed expressions are supported, use
``__has_feature(objc_boxed_expressions)`` feature macro.
