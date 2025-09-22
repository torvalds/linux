Variable Formatting
===================

LLDB has a data formatters subsystem that allows users to define custom display
options for their variables.

Usually, when you type ``frame variable`` or run some expression LLDB will
automatically choose the way to display your results on a per-type basis, as in
the following example:

::

   (lldb) frame variable
   (uint8_t) x = 'a'
   (intptr_t) y = 124752287

Note: ``frame variable`` without additional arguments prints the list of
variables of the current frame.

However, in certain cases, you may want to associate a different style to the
display for certain datatypes. To do so, you need to give hints to the debugger
as to how variables should be displayed. The LLDB type command allows you to do
just that.

Using it you can change your visualization to look like this:

::

   (lldb) frame variable
   (uint8_t) x = chr='a' dec=65 hex=0x41
   (intptr_t) y = 0x76f919f

In addition, some data structures can encode their data in a way that is not
easily readable to the user, in which case a data formatter can be used to
show the data in a human readable way. For example, without a formatter,
printing a ``std::deque<int>`` with the elements ``{2, 3, 4, 5, 6}`` would
result in something like:

::

   (lldb) frame variable a_deque
   (std::deque<Foo, std::allocator<int> >) $0 = {
      std::_Deque_base<Foo, std::allocator<int> > = {
         _M_impl = {
            _M_map = 0x000000000062ceb0
            _M_map_size = 8
            _M_start = {
               _M_cur = 0x000000000062cf00
               _M_first = 0x000000000062cf00
               _M_last = 0x000000000062d2f4
               _M_node = 0x000000000062cec8
            }
            _M_finish = {
               _M_cur = 0x000000000062d300
               _M_first = 0x000000000062d300
               _M_last = 0x000000000062d6f4
               _M_node = 0x000000000062ced0
            }
         }
      }
   }

which is very hard to make sense of.

Note: ``frame variable <var>`` prints out the variable ``<var>`` in the current
frame.

On the other hand, a proper formatter is able to produce the following output:

::

   (lldb) frame variable a_deque
   (std::deque<Foo, std::allocator<int> >) $0 = size=5 {
      [0] = 2
      [1] = 3
      [2] = 4
      [3] = 5
      [4] = 6
   }

which is what the user would expect from a good debugger.

Note: you can also use ``v <var>`` instead of ``frame variable <var>``.

It's worth mentioning that the ``size=5`` string is produced by a summary
provider and the list of children is produced by a synthetic child provider.
More information about these providers is available later in this document.


There are several features related to data visualization: formats, summaries,
filters, synthetic children.

To reflect this, the type command has five subcommands:

::

   type format
   type summary
   type filter
   type synthetic
   type category

These commands are meant to bind printing options to types. When variables are
printed, LLDB will first check if custom printing options have been associated
to a variable's type and, if so, use them instead of picking the default
choices.

Each of the commands (except ``type category``) has four subcommands available:

- ``add``: associates a new printing option to one or more types
- ``delete``: deletes an existing association
- ``list``: provides a listing of all associations
- ``clear``: deletes all associations

Type Format
-----------

Type formats enable you to quickly override the default format for displaying
primitive types (the usual basic C/C++/ObjC types: int, float, char, ...).

If for some reason you want all int variables in your program to print out as
hex, you can add a format to the int type.

This is done by typing

::

   (lldb) type format add --format hex int

at the LLDB command line.

The ``--format`` (which you can shorten to -f) option accepts a `format
name`_. Then, you provide one or more types to which you want the
new format applied.

A frequent scenario is that your program has a typedef for a numeric type that
you know represents something that must be printed in a certain way. Again, you
can add a format just to that typedef by using type format add with the name
alias.

But things can quickly get hierarchical. Let's say you have a situation like
the following:

::

   typedef int A;
   typedef A B;
   typedef B C;
   typedef C D;

and you want to show all A's as hex, all C's as byte arrays and leave the
defaults untouched for other types (albeit its contrived look, the example is
far from unrealistic in large software systems).

If you simply type

::

   (lldb) type format add -f hex A
   (lldb) type format add -f uint8_t[] C

values of type B will be shown as hex and values of type D as byte arrays, as in:

::

   (lldb) frame variable -T
   (A) a = 0x00000001
   (B) b = 0x00000002
   (C) c = {0x03 0x00 0x00 0x00}
   (D) d = {0x04 0x00 0x00 0x00}

This is because by default LLDB cascades formats through typedef chains. In
order to avoid that you can use the option -C no to prevent cascading, thus
making the two commands required to achieve your goal:

::

   (lldb) type format add -C no -f hex A
   (lldb) type format add -C no -f uint8_t[] C


which provides the desired output:

::

   (lldb) frame variable -T
   (A) a = 0x00000001
   (B) b = 2
   (C) c = {0x03 0x00 0x00 0x00}
   (D) d = 4

Note, that qualifiers such as const and volatile will be stripped when matching types for example:

::

   (lldb) frame var x y z
   (int) x = 1
   (const int) y = 2
   (volatile int) z = 4
   (lldb) type format add -f hex int
   (lldb) frame var x y z
   (int) x = 0x00000001
   (const int) y = 0x00000002
   (volatile int) z = 0x00000004

Two additional options that you will want to look at are --skip-pointers (-p)
and --skip-references (-r). These two options prevent LLDB from applying a
format for type T to values of type T* and T& respectively.

::

   (lldb) type format add -f float32[] int
   (lldb) frame variable pointer *pointer -T
   (int *) pointer = {1.46991e-39 1.4013e-45}
   (int) *pointer = {1.53302e-42}
   (lldb) type format add -f float32[] int -p
   (lldb) frame variable pointer *pointer -T
   (int *) pointer = 0x0000000100100180
   (int) *pointer = {1.53302e-42}

While they can be applied to pointers and references, formats will make no
attempt to dereference the pointer and extract the value before applying the
format, which means you are effectively formatting the address stored in the
pointer rather than the pointee value. For this reason, you may want to use the
-p option when defining formats.

If you need to delete a custom format simply type type format delete followed
by the name of the type to which the format applies.Even if you defined the
same format for multiple types on the same command, type format delete will
only remove the format for the type name passed as argument.

To delete ALL formats, use ``type format clear``. To see all the formats
defined, use type format list.

If all you need to do, however, is display one variable in a custom format,
while leaving the others of the same type untouched, you can simply type:

::

   (lldb) frame variable counter -f hex

This has the effect of displaying the value of counter as an hexadecimal
number, and will keep showing it this way until you either pick a different
format or till you let your program run again.

Finally, this is a list of formatting options available out of which you can
pick:

.. _`format name`:

+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| **Format name**                               | **Abbreviation** | **Description**                                                          |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``default``                                   |                  | the default LLDB algorithm is used to pick a format                      |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``boolean``                                   | B                | show this as a true/false boolean, using the customary rule that 0 is    |
|                                               |                  | false and everything else is true                                        |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``binary``                                    | b                | show this as a sequence of bits                                          |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``bytes``                                     | y                | show the bytes one after the other                                       |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``bytes with ASCII``                          | Y                | show the bytes, but try to display them as ASCII characters as well      |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``character``                                 | c                | show the bytes as ASCII characters                                       |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``printable character``                       | C                | show the bytes as printable ASCII characters                             |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``complex float``                             | F                | interpret this value as the real and imaginary part of a complex         |
|                                               |                  | floating-point number                                                    |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``c-string``                                  | s                | show this as a 0-terminated C string                                     |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``decimal``                                   | d                | show this as a signed integer number (this does not perform a cast, it   |
|                                               |                  | simply shows the bytes as  an integer with sign)                         |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``enumeration``                               | E                | show this as an enumeration, printing the                                |
|                                               |                  | value's name if available or the integer value otherwise                 |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``hex``                                       | x                | show this as in hexadecimal notation (this does                          |
|                                               |                  | not perform a cast, it simply shows the bytes as hex)                    |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``float``                                     | f                | show this as a floating-point number (this does not perform a cast, it   |
|                                               |                  | simply interprets the bytes as an IEEE754 floating-point value)          |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``octal``                                     | o                | show this in octal notation                                              |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``OSType``                                    | O                | show this as a MacOS OSType                                              |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``unicode16``                                 | U                | show this as UTF-16 characters                                           |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``unicode32``                                 |                  | show this as UTF-32 characters                                           |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``unsigned decimal``                          | u                | show this as an unsigned integer number (this does not perform a cast,   |
|                                               |                  | it simply shows the bytes as unsigned integer)                           |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``pointer``                                   | p                | show this as a native pointer (unless this is really a pointer, the      |
|                                               |                  | resulting address will probably be invalid)                              |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``char[]``                                    |                  | show this as an array of characters                                      |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``int8_t[], uint8_t[]``                       |                  | show this as an array of the corresponding integer type                  |
| ``int16_t[], uint16_t[]``                     |                  |                                                                          |
| ``int32_t[], uint32_t[]``                     |                  |                                                                          |
| ``int64_t[], uint64_t[]``                     |                  |                                                                          |
| ``uint128_t[]``                               |                  |                                                                          |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``float32[], float64[]``                      |                  | show this as an array of the corresponding                               |
|                                               |                  |                       floating-point type                                |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``complex integer``                           | I                | interpret this value as the real and imaginary part of a complex integer |
|                                               |                  | number                                                                   |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``character array``                           | a                | show this as a character array                                           |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``address``                                   | A                | show this as an address target (symbol/file/line + offset), possibly     |
|                                               |                  | also the string this address is pointing to                              |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``hex float``                                 |                  | show this as hexadecimal floating point                                  |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``instruction``                               | i                | show this as an disassembled opcode                                      |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+
| ``void``                                      | v                | don't show anything                                                      |
+-----------------------------------------------+------------------+--------------------------------------------------------------------------+

Type Summary
------------

Type formats work by showing a different kind of display for the value of a
variable. However, they only work for basic types. When you want to display a
class or struct in a custom format, you cannot do that using formats.

A different feature, type summaries, works by extracting information from
classes, structures, ... (aggregate types) and arranging it in a user-defined
format, as in the following example:

before adding a summary...

::

   (lldb) frame variable -T one
   (i_am_cool) one = {
      (int) x = 3
      (float) y = 3.14159
      (char) z = 'E'
   }

after adding a summary...

::

   (lldb) frame variable one
   (i_am_cool) one = int = 3, float = 3.14159, char = 69

There are two ways to use type summaries: the first one is to bind a summary
string to the type; the second is to write a Python script that returns the
string to be used as summary. Both options are enabled by the type summary add
command.

The command to obtain the output shown in the example is:

::

(lldb) type summary add --summary-string "int = ${var.x}, float = ${var.y}, char = ${var.z%u}" i_am_cool

Initially, we will focus on summary strings, and then describe the Python
binding mechanism.

Summary Strings
---------------

Summary strings are written using a simple control language, exemplified by the
snippet above. A summary string contains a sequence of tokens that are
processed by LLDB to generate the summary.

Summary strings can contain plain text, control characters and special
variables that have access to information about the current object and the
overall program state.

Plain text is any sequence of characters that doesn't contain a ``{``, ``}``, ``$``,
or ``\`` character, which are the syntax control characters.

The special variables are found in between a "${" prefix, and end with a "}"
suffix. Variables can be a simple name or they can refer to complex objects
that have subitems themselves. In other words, a variable looks like
``${object}`` or ``${object.child.otherchild}``. A variable can also be
prefixed or suffixed with other symbols meant to change the way its value is
handled. An example is ``${*var.int_pointer[0-3]}``.

Basically, the syntax is the same one described Frame and Thread Formatting
plus additional symbols specific for summary strings. The main of them is
${var, which is used refer to the variable that a summary is being created for.

The simplest thing you can do is grab a member variable of a class or structure
by typing its expression path. In the previous example, the expression path for
the field float y is simply .y. Thus, to ask the summary string to display y
you would type ${var.y}.

If you have code like the following:

::

   struct A {
      int x;
      int y;
   };
   struct B {
      A x;
      A y;
      int *z;
   };

the expression path for the y member of the x member of an object of type B
would be .x.y and you would type ``${var.x.y}`` to display it in a summary
string for type B.

By default, a summary defined for type T, also works for types T* and T& (you
can disable this behavior if desired). For this reason, expression paths do not
differentiate between . and ->, and the above expression path .x.y would be
just as good if you were displaying a B*, or even if the actual definition of B
were:

::

   struct B {
      A *x;
      A y;
      int *z;
   };

This is unlike the behavior of frame variable which, on the contrary, will
enforce the distinction. As hinted above, the rationale for this choice is that
waiving this distinction enables you to write a summary string once for type T
and use it for both T and T* instances. As a summary string is mostly about
extracting nested members' information, a pointer to an object is just as good
as the object itself for the purpose.

If you need to access the value of the integer pointed to by B::z, you cannot
simply say ${var.z} because that symbol refers to the pointer z. In order to
dereference it and get the pointed value, you should say ``${*var.z}``. The
``${*var`` tells LLDB to get the object that the expression paths leads to, and
then dereference it. In this example is it equivalent to ``*(bObject.z)`` in
C/C++ syntax. Because ``.`` and ``->`` operators can both be used, there is no
need to have dereferences in the middle of an expression path (e.g. you do not
need to type ``${*(var.x).x}``) to read A::x as contained in ``*(B::x)``. To
achieve that effect you can simply write ``${var.x->x}``, or even
``${var.x.x}``. The ``*`` operator only binds to the result of the whole
expression path, rather than piecewise, and there is no way to use parentheses
to change that behavior.

Of course, a summary string can contain more than one ${var specifier, and can
use ``${var`` and ``${*var`` specifiers together.

Formatting Summary Elements
---------------------------

An expression path can include formatting codes. Much like the type formats
discussed previously, you can also customize the way variables are displayed in
summary strings, regardless of the format they have applied to their types. To
do that, you can use %format inside an expression path, as in ${var.x->x%u},
which would display the value of x as an unsigned integer.

Additionally, custom output can be achieved by using an LLVM format string,
commencing with the ``:`` marker. To illustrate, compare ``${var.byte%x}`` and
``${var.byte:x-}``. The former uses lldb's builtin hex formatting (``x``),
which unconditionally inserts a ``0x`` prefix, and also zero pads the value to
match the size of the type. The latter uses ``llvm::formatv`` formatting
(``:x-``), and will print only the hex value, with no ``0x`` prefix, and no
padding. This raw control is useful when composing multiple pieces into a
larger whole.

You can also use some other special format markers, not available for formats
themselves, but which carry a special meaning when used in this context:

+------------+--------------------------------------------------------------------------+
| **Symbol** | **Description**                                                          |
+------------+--------------------------------------------------------------------------+
| ``Symbol`` | ``Description``                                                          |
+------------+--------------------------------------------------------------------------+
| ``%S``     | Use this object's summary (the default for aggregate types)              |
+------------+--------------------------------------------------------------------------+
| ``%V``     | Use this object's value (the default for non-aggregate types)            |
+------------+--------------------------------------------------------------------------+
| ``%@``     | Use a language-runtime specific description (for C++ this does nothing,  |
|            |                     for Objective-C it calls the NSPrintForDebugger API) |
+------------+--------------------------------------------------------------------------+
| ``%L``     | Use this object's location (memory address, register name, ...)          |
+------------+--------------------------------------------------------------------------+
| ``%#``     | Use the count of the children of this object                             |
+------------+--------------------------------------------------------------------------+
| ``%T``     | Use this object's datatype name                                          |
+------------+--------------------------------------------------------------------------+
| ``%N``     | Print the variable's basename                                            |
+------------+--------------------------------------------------------------------------+
| ``%>``     | Print the expression path for this item                                  |
+------------+--------------------------------------------------------------------------+

Since lldb 3.7.0, you can also specify ``${script.var:pythonFuncName}``.

It is expected that the function name you use specifies a function whose
signature is the same as a Python summary function. The return string from the
function will be placed verbatim in the output.

You cannot use element access, or formatting symbols, in combination with this
syntax. For example the following:

::

   ${script.var.element[0]:myFunctionName%@}

is not valid and will cause the summary to fail to evaluate.


Element Inlining
----------------

Option --inline-children (-c) to type summary add tells LLDB not to look for a summary string, but instead to just print a listing of all the object's children on one line.

As an example, given a type pair:

::

   (lldb) frame variable --show-types a_pair
   (pair) a_pair = {
      (int) first = 1;
      (int) second = 2;
   }

If one types the following commands:

::

   (lldb) type summary add --inline-children pair

the output becomes:

::

   (lldb) frame variable a_pair
   (pair) a_pair = (first=1, second=2)


Of course, one can obtain the same effect by typing

::

   (lldb) type summary add pair --summary-string "(first=${var.first}, second=${var.second})"

While the final result is the same, using --inline-children can often save
time. If one does not need to see the names of the variables, but just their
values, the option --omit-names (-O, uppercase letter o), can be combined with
--inline-children to obtain:

::

   (lldb) frame variable a_pair
   (pair) a_pair = (1, 2)

which is of course the same as typing

::

   (lldb) type summary add pair --summary-string "(${var.first}, ${var.second})"

Bitfields And Array Syntax
--------------------------

Sometimes, a basic type's value actually represents several different values
packed together in a bitfield.

With the classical view, there is no way to look at them. Hexadecimal display
can help, but if the bits actually span nibble boundaries, the help is limited.

Binary view would show it all without ambiguity, but is often too detailed and
hard to read for real-life scenarios.

To cope with the issue, LLDB supports native bitfield formatting in summary
strings. If your expression paths leads to a so-called scalar type (the usual
int, float, char, double, short, long, long long, double, long double and
unsigned variants), you can ask LLDB to only grab some bits out of the value
and display them in any format you like. If you only need one bit you can use
the [n], just like indexing an array. To extract multiple bits, you can use a
slice-like syntax: [n-m], e.g.

::

   (lldb) frame variable float_point
   (float) float_point = -3.14159

::

   (lldb) type summary add --summary-string "Sign: ${var[31]%B} Exponent: ${var[30-23]%x} Mantissa: ${var[0-22]%u}" float
   (lldb) frame variable float_point
   (float) float_point = -3.14159 Sign: true Exponent: 0x00000080 Mantissa: 4788184

In this example, LLDB shows the internal representation of a float variable by
extracting bitfields out of a float object.

When typing a range, the extremes n and m are always included, and the order of
the indices is irrelevant.

LLDB also allows to use a similar syntax to display array members inside a summary string. For instance, you may want to display all arrays of a given type using a more compact notation than the default, and then just delve into individual array members that prove interesting to your debugging task. You can tell LLDB to format arrays in special ways, possibly independent of the way the array members' datatype is formatted.
e.g.

::

   (lldb) frame variable sarray
   (Simple [3]) sarray = {
      [0] = {
         x = 1
         y = 2
         z = '\x03'
      }
      [1] = {
         x = 4
         y = 5
         z = '\x06'
      }
      [2] = {
         x = 7
         y = 8
         z = '\t'
      }
   }

   (lldb) type summary add --summary-string "${var[].x}" "Simple [3]"

   (lldb) frame variable sarray
   (Simple [3]) sarray = [1,4,7]

The [] symbol amounts to: if var is an array and I know its size, apply this summary string to every element of the array. Here, we are asking LLDB to display .x for every element of the array, and in fact this is what happens. If you find some of those integers anomalous, you can then inspect that one item in greater detail, without the array format getting in the way:

::

   (lldb) frame variable sarray[1]
   (Simple) sarray[1] = {
      x = 4
      y = 5
      z = '\x06'
   }

You can also ask LLDB to only print a subset of the array range by using the
same syntax used to extract bit for bitfields:

::

   (lldb) type summary add --summary-string "${var[1-2].x}" "Simple [3]"

   (lldb) frame variable sarray
   (Simple [3]) sarray = [4,7]

If you are dealing with a pointer that you know is an array, you can use this
syntax to display the elements contained in the pointed array instead of just
the pointer value. However, because pointers have no notion of their size, the
empty brackets [] operator does not work, and you must explicitly provide
higher and lower bounds.

In general, LLDB needs the square brackets ``operator []`` in order to handle
arrays and pointers correctly, and for pointers it also needs a range. However,
a few special cases are defined to make your life easier:

you can print a 0-terminated string (C-string) using the %s format, omitting
square brackets, as in:

::

   (lldb) type summary add --summary-string "${var%s}" "char *"

This syntax works for char* as well as for char[] because LLDB can rely on the
final \0 terminator to know when the string has ended.

LLDB has default summary strings for char* and char[] that use this special
case. On debugger startup, the following are defined automatically:

::

   (lldb) type summary add --summary-string "${var%s}" "char *"
   (lldb) type summary add --summary-string "${var%s}" -x "char \[[0-9]+]"

any of the array formats (int8_t[], float32{}, ...), and the y, Y and a formats
work to print an array of a non-aggregate type, even if square brackets are
omitted.

::

   (lldb) type summary add --summary-string "${var%int32_t[]}" "int [10]"

This feature, however, is not enabled for pointers because there is no way for
LLDB to detect the end of the pointed data.

This also does not work for other formats (e.g. boolean), and you must specify
the square brackets operator to get the expected output.

Python Scripting
----------------

Most of the times, summary strings prove good enough for the job of summarizing
the contents of a variable. However, as soon as you need to do more than
picking some values and rearranging them for display, summary strings stop
being an effective tool. This is because summary strings lack the power to
actually perform any kind of computation on the value of variables.

To solve this issue, you can bind some Python scripting code as a summary for
your datatype, and that script has the ability to both extract children
variables as the summary strings do and to perform active computation on the
extracted values. As a small example, let's say we have a Rectangle class:

::


   class Rectangle
   {
   private:
      int height;
      int width;
   public:
      Rectangle() : height(3), width(5) {}
      Rectangle(int H) : height(H), width(H*2-1) {}
      Rectangle(int H, int W) : height(H), width(W) {}
      int GetHeight() { return height; }
      int GetWidth() { return width; }
   };

Summary strings are effective to reduce the screen real estate used by the
default viewing mode, but are not effective if we want to display the area and
perimeter of Rectangle objects

To obtain this, we can simply attach a small Python script to the Rectangle
class, as shown in this example:

::

   (lldb) type summary add -P Rectangle
   Enter your Python command(s). Type 'DONE' to end.
   def function (valobj,internal_dict,options):
      height_val = valobj.GetChildMemberWithName('height')
      width_val = valobj.GetChildMemberWithName('width')
      height = height_val.GetValueAsUnsigned(0)
      width = width_val.GetValueAsUnsigned(0)
      area = height*width
      perimeter = 2*(height + width)
      return 'Area: ' + str(area) + ', Perimeter: ' + str(perimeter)
      DONE
   (lldb) frame variable
   (Rectangle) r1 = Area: 20, Perimeter: 18
   (Rectangle) r2 = Area: 72, Perimeter: 36
   (Rectangle) r3 = Area: 16, Perimeter: 16

In order to write effective summary scripts, you need to know the LLDB public
API, which is the way Python code can access the LLDB object model. For further
details on the API you should look at the LLDB API reference documentation.


As a brief introduction, your script is encapsulated into a function that is
passed two parameters: ``valobj`` and ``internal_dict``.

``internal_dict`` is an internal support parameter used by LLDB and you should
not touch it.

``valobj`` is the object encapsulating the actual variable being displayed, and
its type is `SBValue`. Out of the many possible operations on an `SBValue`, the
basic one is retrieve the children objects it contains (essentially, the fields
of the object wrapped by it), by calling ``GetChildMemberWithName()``, passing
it the child's name as a string.

If the variable has a value, you can ask for it, and return it as a string
using ``GetValue()``, or as a signed/unsigned number using
``GetValueAsSigned()``, ``GetValueAsUnsigned()``. It is also possible to
retrieve an `SBData` object by calling ``GetData()`` and then read the object's
contents out of the `SBData`.

If you need to delve into several levels of hierarchy, as you can do with
summary strings, you can use the method ``GetValueForExpressionPath()``,
passing it an expression path just like those you could use for summary strings
(one of the differences is that dereferencing a pointer does not occur by
prefixing the path with a ``*```, but by calling the ``Dereference()`` method
on the returned `SBValue`). If you need to access array slices, you cannot do
that (yet) via this method call, and you must use ``GetChildAtIndex()``
querying it for the array items one by one. Also, handling custom formats is
something you have to deal with on your own.

``options`` Python summary formatters can optionally define this
third argument, which is an object of type ``lldb.SBTypeSummaryOptions``,
allowing for a few customizations of the result. The decision to
adopt or not this third argument - and the meaning of options
thereof - is up to the individual formatter's writer.

Other than interactively typing a Python script there are two other ways for
you to input a Python script as a summary:

- using the --python-script option to type summary add and typing the script
  code as an option argument; as in:

::

   (lldb) type summary add --python-script "height = valobj.GetChildMemberWithName('height').GetValueAsUnsigned(0);width = valobj.GetChildMemberWithName('width').GetValueAsUnsigned(0); return 'Area: %d' % (height*width)" Rectangle


- using the --python-function (-F) option to type summary add and giving the
  name of a Python function with the correct prototype. Most probably, you will
  define (or have already defined) the function in the interactive interpreter,
  or somehow loaded it from a file, using the command script import command.
  LLDB will emit a warning if it is unable to find the function you passed, but
  will still register the binding.

Regular Expression Typenames
----------------------------

As you noticed, in order to associate the custom summary string to the array
types, one must give the array size as part of the typename. This can long
become tiresome when using arrays of different sizes, Simple [3], Simple [9],
Simple [12], ...

If you use the -x option, type names are treated as regular expressions instead
of type names. This would let you rephrase the above example for arrays of type
Simple [3] as:

::

   (lldb) type summary add --summary-string "${var[].x}" -x "Simple \[[0-9]+\]"
   (lldb) frame variable
   (Simple [3]) sarray = [1,4,7]
   (Simple [2]) sother = [3,6]

The above scenario works for Simple [3] as well as for any other array of
Simple objects.

While this feature is mostly useful for arrays, you could also use regular
expressions to catch other type sets grouped by name. However, as regular
expression matching is slower than normal name matching, LLDB will first try to
match by name in any way it can, and only when this fails, will it resort to
regular expression matching.

One of the ways LLDB uses this feature internally, is to match the names of STL
container classes, regardless of the template arguments provided. The details
for this are found at FormatManager.cpp

The regular expression language used by LLDB is the POSIX extended language, as
defined by the Single UNIX Specification, of which macOS is a compliant
implementation.

Names Summaries
---------------

For a given type, there may be different meaningful summary representations.
However, currently, only one summary can be associated to a type at each
moment. If you need to temporarily override the association for a variable,
without changing the summary string for to its type, you can use named
summaries.

Named summaries work by attaching a name to a summary when creating it. Then,
when there is a need to attach the summary to a variable, the frame variable
command, supports a --summary option that tells LLDB to use the named summary
given instead of the default one.

::

   (lldb) type summary add --summary-string "x=${var.integer}" --name NamedSummary
   (lldb) frame variable one
   (i_am_cool) one = int = 3, float = 3.14159, char = 69
   (lldb) frame variable one --summary NamedSummary
   (i_am_cool) one = x=3

When defining a named summary, binding it to one or more types becomes
optional. Even if you bind the named summary to a type, and later change the
summary string for that type, the named summary will not be changed by that.
You can delete named summaries by using the type summary delete command, as if
the summary name was the datatype that the summary is applied to

A summary attached to a variable using the --summary option, has the same
semantics that a custom format attached using the -f option has: it stays
attached till you attach a new one, or till you let your program run again.

Synthetic Children
------------------

Summaries work well when one is able to navigate through an expression path. In
order for LLDB to do so, appropriate debugging information must be available.

Some types are opaque, i.e. no knowledge of their internals is provided. When
that's the case, expression paths do not work correctly.

In other cases, the internals are available to use in expression paths, but
they do not provide a user-friendly representation of the object's value.

For instance, consider an STL vector, as implemented by the GNU C++ Library:

::

   (lldb) frame variable numbers -T
   (std::vector<int>) numbers = {
      (std::_Vector_base<int, std::allocator<int> >) std::_Vector_base<int, std::allocator<int> > = {
         (std::_Vector_base<int, std::allocator&tl;int> >::_Vector_impl) _M_impl = {
               (int *) _M_start = 0x00000001001008a0
               (int *) _M_finish = 0x00000001001008a8
               (int *) _M_end_of_storage = 0x00000001001008a8
         }
      }
   }

Here, you can see how the type is implemented, and you can write a summary for
that implementation but that is not going to help you infer what items are
actually stored in the vector.

What you would like to see is probably something like:

::

   (lldb) frame variable numbers -T
   (std::vector<int>) numbers = {
      (int) [0] = 1
      (int) [1] = 12
      (int) [2] = 123
      (int) [3] = 1234
   }

Synthetic children are a way to get that result.

The feature is based upon the idea of providing a new set of children for a
variable that replaces the ones available by default through the debug
information. In the example, we can use synthetic children to provide the
vector items as children for the std::vector object.

In order to create synthetic children, you need to provide a Python class that
adheres to a given interface (the word is italicized because Python has no
explicit notion of interface, by that word we mean a given set of methods must
be implemented by the Python class):

.. code-block:: python

   class SyntheticChildrenProvider:
      def __init__(self, valobj, internal_dict):
         this call should initialize the Python object using valobj as the
         variable to provide synthetic children for
      def num_children(self, max_children):
         this call should return the number of children that you want your
         object to have[1]
      def get_child_index(self,name):
         this call should return the index of the synthetic child whose name is
         given as argument
      def get_child_at_index(self,index):
         this call should return a new LLDB SBValue object representing the
         child at the index given as argument
      def update(self):
         this call should be used to update the internal state of this Python
         object whenever the state of the variables in LLDB changes.[2]
         Also, this method is invoked before any other method in the interface.
      def has_children(self):
         this call should return True if this object might have children, and
         False if this object can be guaranteed not to have children.[3]
      def get_value(self):
         this call can return an SBValue to be presented as the value of the
         synthetic value under consideration.[4]

As a warning, exceptions that are thrown by python formatters are caught
silently by LLDB and should be handled appropriately by the formatter itself.
Being more specific, in case of exceptions, LLDB might assume that the given
object has no children or it might skip printing some children, as they are
printed one by one.

[1] The `max_children` argument is optional (since lldb 3.8.0) and indicates the
maximum number of children that lldb is interested in (at this moment). If the
computation of the number of children is expensive (for example, requires
travesing a linked list to determine its size) your implementation may return
`max_children` rather than the actual number. If the computation is cheap (e.g., the
number is stored as a field of the object), then you can always return the true
number of children (that is, ignore the `max_children` argument).

[2] This method is optional. Also, a boolean value must be returned (since lldb
3.1.0). If ``False`` is returned, then whenever the process reaches a new stop,
this method will be invoked again to generate an updated list of the children
for a given variable. Otherwise, if ``True`` is returned, then the value is
cached and this method won't be called again, effectively freezing the state of
the value in subsequent stops. Beware that returning ``True`` incorrectly could
show misleading information to the user.

[3] This method is optional (since lldb 3.2.0). While implementing it in terms
of num_children is acceptable, implementors are encouraged to look for
optimized coding alternatives whenever reasonable.

[4] This method is optional (since lldb 3.5.2). The `SBValue` you return here
will most likely be a numeric type (int, float, ...) as its value bytes will be
used as-if they were the value of the root `SBValue` proper.  As a shortcut for
this, you can inherit from lldb.SBSyntheticValueProvider, and just define
get_value as other methods are defaulted in the superclass as returning default
no-children responses.

If a synthetic child provider supplies a special child named
``$$dereference$$`` then it will be used when evaluating ``operator *`` and
``operator ->`` in the frame variable command and related SB API
functions. It is possible to declare this synthetic child without
including it in the range of children displayed by LLDB. For example,
this subset of a synthetic children provider class would allow the
synthetic value to be dereferenced without actually showing any
synthetic children in the UI:

.. code-block:: python

      class SyntheticChildrenProvider:
          [...]
          def num_children(self):
              return 0
          def get_child_index(self, name):
              if name == '$$dereference$$':
                  return 0
              return -1
          def get_child_at_index(self, index):
              if index == 0:
                  return <valobj resulting from dereference>
              return None


For examples of how synthetic children are created, you are encouraged to look
at examples/synthetic in the LLDB trunk. Please, be aware that the code in
those files (except bitfield/) is legacy code and is not maintained. You may
especially want to begin looking at this example to get a feel for this
feature, as it is a very easy and well commented example.

The design pattern consistently used in synthetic providers shipping with LLDB
is to use the __init__ to store the `SBValue` instance as a part of self. The
update function is then used to perform the actual initialization. Once a
synthetic children provider is written, one must load it into LLDB before it
can be used. Currently, one can use the LLDB script command to type Python code
interactively, or use the command script import fileName command to load Python
code from a Python module (ordinary rules apply to importing modules this way).
A third option is to type the code for the provider class interactively while
adding it.

For example, let's pretend we have a class Foo for which a synthetic children
provider class Foo_Provider is available, in a Python module contained in file
~/Foo_Tools.py. The following interaction sets Foo_Provider as a synthetic
children provider in LLDB:

::

   (lldb) command script import ~/Foo_Tools.py
   (lldb) type synthetic add Foo --python-class Foo_Tools.Foo_Provider
   (lldb) frame variable a_foo
   (Foo) a_foo = {
      x = 1
      y = "Hello world"
   }

LLDB has synthetic children providers for a core subset of STL classes, both in
the version provided by libstdcpp and by libcxx, as well as for several
Foundation classes.

Synthetic children extend summary strings by enabling a new special variable:
``${svar``.

This symbol tells LLDB to refer expression paths to the synthetic children
instead of the real ones. For instance,

::

   (lldb) type summary add --expand -x "std::vector<" --summary-string "${svar%#} items"
   (lldb) frame variable numbers
   (std::vector<int>) numbers = 4 items {
      (int) [0] = 1
      (int) [1] = 12
      (int) [2] = 123
      (int) [3] = 1234
   }

It's important to mention that LLDB invokes the synthetic child provider before
invoking the summary string provider, which allows the latter to have access to
the actual displayable children. This applies to both inlined summary strings
and python-based summary providers.


As a warning, when programmatically accessing the children or children count of
a variable that has a synthetic child provider, notice that LLDB hides the
actual raw children. For example, suppose we have a ``std::vector``, which has
an actual in-memory property ``__begin`` marking the beginning of its data.
After the synthetic child provider is executed, the ``std::vector`` variable
won't show ``__begin`` as child anymore, even through the SB API. It will have
instead the children calculated by the provider. In case the actual raw
children are needed, a call to ``value.GetNonSyntheticValue()`` is enough to
get a raw version of the value. It is import to remember this when implementing
summary string providers, as they run after the synthetic child provider.


In some cases, if LLDB is unable to use the real object to get a child
specified in an expression path, it will automatically refer to the synthetic
children. While in summaries it is best to always use ${svar to make your
intentions clearer, interactive debugging can benefit from this behavior, as
in:

::

   (lldb) frame variable numbers[0] numbers[1]
   (int) numbers[0] = 1
   (int) numbers[1] = 12

Unlike many other visualization features, however, the access to synthetic
children only works when using frame variable, and is not supported in
expression:

::

   (lldb) expression numbers[0]
   Error [IRForTarget]: Call to a function '_ZNSt33vector<int, std::allocator<int> >ixEm' that is not present in the target
   error: Couldn't convert the expression to DWARF

The reason for this is that classes might have an overloaded ``operator []``,
or other special provisions and the expression command chooses to ignore
synthetic children in the interest of equivalency with code you asked to have
compiled from source.

Filters
-------

Filters are a solution to the display of complex classes. At times, classes
have many member variables but not all of these are actually necessary for the
user to see.

A filter will solve this issue by only letting the user see those member
variables they care about. Of course, the equivalent of a filter can be
implemented easily using synthetic children, but a filter lets you get the job
done without having to write Python code.

For instance, if your class Foobar has member variables named A thru Z, but you
only need to see the ones named B, H and Q, you can define a filter:

::

   (lldb) type filter add Foobar --child B --child H --child Q
   (lldb) frame variable a_foobar
   (Foobar) a_foobar = {
      (int) B = 1
      (char) H = 'H'
      (std::string) Q = "Hello world"
   }

Callback-based type matching
----------------------------

Even though regular expression matching works well for the vast majority of data
formatters (you normally know the name of the type you're writing a formatter
for), there are some cases where it's useful to look at the type before deciding
what formatter to apply.

As an example scenario, imagine we have a code generator that produces some
classes that inherit from a common ``GeneratedObject`` class, and we have a
summary function and a synthetic child provider that work for all
``GeneratedObject`` instances (they all follow the same pattern). However, there
is no common pattern in the name of these classes, so we can't register the
formatter neither by name nor by regular expression.

In that case, you can write a recognizer function like this:

::

   def is_generated_object(sbtype, internal_dict):
     for base in sbtype.get_bases_array():
       if base.GetName() == "GeneratedObject"
         return True
     return False

And pass this function to ``type summary add`` and ``type synthetic add`` using
the flag ``--recognizer-function``.

::

   (lldb) type summary add --expand --python-function my_summary_function --recognizer-function is_generated_object
   (lldb) type synthetic add --python-class my_child_provider --recognizer-function is_generated_object

Objective-C Dynamic Type Discovery
----------------------------------

When doing Objective-C development, you may notice that some of your variables
come out as of type id (for instance, items extracted from NSArray). By
default, LLDB will not show you the real type of the object. it can actually
dynamically discover the type of an Objective-C variable, much like the runtime
itself does when invoking a selector. In order to be shown the result of that
discovery that, however, a special option to frame variable or expression is
required: ``--dynamic-type``.


``--dynamic-type`` can have one of three values:

- ``no-dynamic-values``: the default, prevents dynamic type discovery
- ``no-run-target``: enables dynamic type discovery as long as running code on
  the target is not required
- ``run-target``: enables code execution on the target in order to perform
  dynamic type discovery

If you specify a value of either no-run-target or run-target, LLDB will detect
the dynamic type of your variables and show the appropriate formatters for
them. As an example:

::

   (lldb) expr @"Hello"
   (NSString *) $0 = 0x00000001048000b0 @"Hello"
   (lldb) expr -d no-run @"Hello"
   (__NSCFString *) $1 = 0x00000001048000b0 @"Hello"

Because LLDB uses a detection algorithm that does not need to invoke any
functions on the target process, no-run-target is enough for this to work.

As a side note, the summary for NSString shown in the example is built right
into LLDB. It was initially implemented through Python (the code is still
available for reference at CFString.py). However, this is out of sync with the
current implementation of the NSString formatter (which is a C++ function
compiled into the LLDB core).

Categories
----------

Categories are a way to group related formatters. For instance, LLDB itself
groups the formatters for the libstdc++ types in a category named
gnu-libstdc++. Basically, categories act like containers in which to store
formatters for a same library or OS release.

By default, several categories are created in LLDB:

- default: this is the category where every formatter ends up, unless another category is specified
- objc: formatters for basic and common Objective-C types that do not specifically depend on macOS
- gnu-libstdc++: formatters for std::string, std::vector, std::list and std::map as implemented by libstdcpp
- libcxx: formatters for std::string, std::vector, std::list and std::map as implemented by libcxx
- system: truly basic types for which a formatter is required
- AppKit: Cocoa classes
- CoreFoundation: CF classes
- CoreGraphics: CG classes
- CoreServices: CS classes
- VectorTypes: compact display for several vector types

If you want to use a custom category for your formatters, all the type ... add
provide a --category (-w) option, that names the category to add the formatter
to. To delete the formatter, you then have to specify the correct category.

Categories can be in one of two states: enabled and disabled. A category is
initially disabled, and can be enabled using the type category enable command.
To disable an enabled category, the command to use is type category disable.

The order in which categories are enabled or disabled is significant, in that
LLDB uses that order when looking for formatters. Therefore, when you enable a
category, it becomes the second one to be searched (after default, which always
stays on top of the list). The default categories are enabled in such a way
that the search order is:

- default
- objc
- CoreFoundation
- AppKit
- CoreServices
- CoreGraphics
- gnu-libstdc++
- libcxx
- VectorTypes
- system

As said, gnu-libstdc++ and libcxx contain formatters for C++ STL data types.
system contains formatters for char* and char[], which reflect the behavior of
older versions of LLDB which had built-in formatters for these types. Because
now these are formatters, you can even replace them with your own if so you
wish.

There is no special command to create a category. When you place a formatter in
a category, if that category does not exist, it is automatically created. For
instance,

::

   (lldb) type summary add Foobar --summary-string "a foobar" --category newcategory

automatically creates a (disabled) category named newcategory.

Another way to create a new (empty) category, is to enable it, as in:

::

   (lldb) type category enable newcategory

However, in this case LLDB warns you that enabling an empty category has no
effect. If you add formatters to the category after enabling it, they will be
honored. But an empty category per se does not change the way any type is
displayed. The reason the debugger warns you is that enabling an empty category
might be a typo, and you effectively wanted to enable a similarly-named but
not-empty category.

Finding Formatters 101
----------------------

Searching for a formatter (including formats, since lldb 3.4.0) given a
variable goes through a rather intricate set of rules. Namely, what happens is
that LLDB starts looking in each enabled category, according to the order in
which they were enabled (latest enabled first). In each category, LLDB does the
following:

- If there is a formatter for the type of the variable, use it
- If this object is a pointer, and there is a formatter for the pointee type
  that does not skip pointers, use it
- If this object is a reference, and there is a formatter for the referred type
  that does not skip references, use it
- If this object is an Objective-C class and dynamic types are enabled, look
  for a formatter for the dynamic type of the object. If dynamic types are
  disabled, or the search failed, look for a formatter for the declared type of
  the object
- If this object's type is a typedef, go through typedef hierarchy (LLDB might
  not be able to do this if the compiler has not emitted enough information. If
  the required information to traverse typedef hierarchies is missing, type
  cascading will not work. The clang compiler, part of the LLVM project, emits
  the correct debugging information for LLDB to cascade). If at any level of
  the hierarchy there is a valid formatter that can cascade, use it.
- If everything has failed, repeat the above search, looking for regular
  expressions instead of exact matches

If any of those attempts returned a valid formatter to be used, that one is
used, and the search is terminated (without going to look in other categories).
If nothing was found in the current category, the next enabled category is
scanned according to the same algorithm. If there are no more enabled
categories, the search has failed.

**Warning**: previous versions of LLDB defined cascading to mean not only going
through typedef chains, but also through inheritance chains. This feature has
been removed since it significantly degrades performance. You need to set up
your formatters for every type in inheritance chains to which you want the
formatter to apply.
