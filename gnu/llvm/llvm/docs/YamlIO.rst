=====================
YAML I/O
=====================

.. contents::
   :local:

Introduction to YAML
====================

YAML is a human readable data serialization language.  The full YAML language
spec can be read at `yaml.org
<http://www.yaml.org/spec/1.2/spec.html#Introduction>`_.  The simplest form of
yaml is just "scalars", "mappings", and "sequences".  A scalar is any number
or string.  The pound/hash symbol (#) begins a comment line.   A mapping is
a set of key-value pairs where the key ends with a colon.  For example:

.. code-block:: yaml

     # a mapping
     name:      Tom
     hat-size:  7

A sequence is a list of items where each item starts with a leading dash ('-').
For example:

.. code-block:: yaml

     # a sequence
     - x86
     - x86_64
     - PowerPC

You can combine mappings and sequences by indenting.  For example a sequence
of mappings in which one of the mapping values is itself a sequence:

.. code-block:: yaml

     # a sequence of mappings with one key's value being a sequence
     - name:      Tom
       cpus:
        - x86
        - x86_64
     - name:      Bob
       cpus:
        - x86
     - name:      Dan
       cpus:
        - PowerPC
        - x86

Sometime sequences are known to be short and the one entry per line is too
verbose, so YAML offers an alternate syntax for sequences called a "Flow
Sequence" in which you put comma separated sequence elements into square
brackets.  The above example could then be simplified to :


.. code-block:: yaml

     # a sequence of mappings with one key's value being a flow sequence
     - name:      Tom
       cpus:      [ x86, x86_64 ]
     - name:      Bob
       cpus:      [ x86 ]
     - name:      Dan
       cpus:      [ PowerPC, x86 ]


Introduction to YAML I/O
========================

The use of indenting makes the YAML easy for a human to read and understand,
but having a program read and write YAML involves a lot of tedious details.
The YAML I/O library structures and simplifies reading and writing YAML
documents.

YAML I/O assumes you have some "native" data structures which you want to be
able to dump as YAML and recreate from YAML.  The first step is to try
writing example YAML for your data structures. You may find after looking at
possible YAML representations that a direct mapping of your data structures
to YAML is not very readable.  Often the fields are not in the order that
a human would find readable.  Or the same information is replicated in multiple
locations, making it hard for a human to write such YAML correctly.

In relational database theory there is a design step called normalization in
which you reorganize fields and tables.  The same considerations need to
go into the design of your YAML encoding.  But, you may not want to change
your existing native data structures.  Therefore, when writing out YAML
there may be a normalization step, and when reading YAML there would be a
corresponding denormalization step.

YAML I/O uses a non-invasive, traits based design.  YAML I/O defines some
abstract base templates.  You specialize those templates on your data types.
For instance, if you have an enumerated type FooBar you could specialize
ScalarEnumerationTraits on that type and define the enumeration() method:

.. code-block:: c++

    using llvm::yaml::ScalarEnumerationTraits;
    using llvm::yaml::IO;

    template <>
    struct ScalarEnumerationTraits<FooBar> {
      static void enumeration(IO &io, FooBar &value) {
      ...
      }
    };


As with all YAML I/O template specializations, the ScalarEnumerationTraits is used for
both reading and writing YAML. That is, the mapping between in-memory enum
values and the YAML string representation is only in one place.
This assures that the code for writing and parsing of YAML stays in sync.

To specify a YAML mappings, you define a specialization on
llvm::yaml::MappingTraits.
If your native data structure happens to be a struct that is already normalized,
then the specialization is simple.  For example:

.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    template <>
    struct MappingTraits<Person> {
      static void mapping(IO &io, Person &info) {
        io.mapRequired("name",         info.name);
        io.mapOptional("hat-size",     info.hatSize);
      }
    };


A YAML sequence is automatically inferred if you data type has begin()/end()
iterators and a push_back() method.  Therefore any of the STL containers
(such as std::vector<>) will automatically translate to YAML sequences.

Once you have defined specializations for your data types, you can
programmatically use YAML I/O to write a YAML document:

.. code-block:: c++

    using llvm::yaml::Output;

    Person tom;
    tom.name = "Tom";
    tom.hatSize = 8;
    Person dan;
    dan.name = "Dan";
    dan.hatSize = 7;
    std::vector<Person> persons;
    persons.push_back(tom);
    persons.push_back(dan);

    Output yout(llvm::outs());
    yout << persons;

This would write the following:

.. code-block:: yaml

     - name:      Tom
       hat-size:  8
     - name:      Dan
       hat-size:  7

And you can also read such YAML documents with the following code:

.. code-block:: c++

    using llvm::yaml::Input;

    typedef std::vector<Person> PersonList;
    std::vector<PersonList> docs;

    Input yin(document.getBuffer());
    yin >> docs;

    if ( yin.error() )
      return;

    // Process read document
    for ( PersonList &pl : docs ) {
      for ( Person &person : pl ) {
        cout << "name=" << person.name;
      }
    }

One other feature of YAML is the ability to define multiple documents in a
single file.  That is why reading YAML produces a vector of your document type.



Error Handling
==============

When parsing a YAML document, if the input does not match your schema (as
expressed in your XxxTraits<> specializations).  YAML I/O
will print out an error message and your Input object's error() method will
return true. For instance the following document:

.. code-block:: yaml

     - name:      Tom
       shoe-size: 12
     - name:      Dan
       hat-size:  7

Has a key (shoe-size) that is not defined in the schema.  YAML I/O will
automatically generate this error:

.. code-block:: yaml

    YAML:2:2: error: unknown key 'shoe-size'
      shoe-size:       12
      ^~~~~~~~~

Similar errors are produced for other input not conforming to the schema.


Scalars
=======

YAML scalars are just strings (i.e. not a sequence or mapping).  The YAML I/O
library provides support for translating between YAML scalars and specific
C++ types.


Built-in types
--------------
The following types have built-in support in YAML I/O:

* bool
* float
* double
* StringRef
* std::string
* int64_t
* int32_t
* int16_t
* int8_t
* uint64_t
* uint32_t
* uint16_t
* uint8_t

That is, you can use those types in fields of MappingTraits or as element type
in sequence.  When reading, YAML I/O will validate that the string found
is convertible to that type and error out if not.


Unique types
------------
Given that YAML I/O is trait based, the selection of how to convert your data
to YAML is based on the type of your data.  But in C++ type matching, typedefs
do not generate unique type names.  That means if you have two typedefs of
unsigned int, to YAML I/O both types look exactly like unsigned int.  To
facilitate make unique type names, YAML I/O provides a macro which is used
like a typedef on built-in types, but expands to create a class with conversion
operators to and from the base type.  For example:

.. code-block:: c++

    LLVM_YAML_STRONG_TYPEDEF(uint32_t, MyFooFlags)
    LLVM_YAML_STRONG_TYPEDEF(uint32_t, MyBarFlags)

This generates two classes MyFooFlags and MyBarFlags which you can use in your
native data structures instead of uint32_t. They are implicitly
converted to and from uint32_t.  The point of creating these unique types
is that you can now specify traits on them to get different YAML conversions.

Hex types
---------
An example use of a unique type is that YAML I/O provides fixed sized unsigned
integers that are written with YAML I/O as hexadecimal instead of the decimal
format used by the built-in integer types:

* Hex64
* Hex32
* Hex16
* Hex8

You can use llvm::yaml::Hex32 instead of uint32_t and the only different will
be that when YAML I/O writes out that type it will be formatted in hexadecimal.


ScalarEnumerationTraits
-----------------------
YAML I/O supports translating between in-memory enumerations and a set of string
values in YAML documents. This is done by specializing ScalarEnumerationTraits<>
on your enumeration type and define an enumeration() method.
For instance, suppose you had an enumeration of CPUs and a struct with it as
a field:

.. code-block:: c++

    enum CPUs {
      cpu_x86_64  = 5,
      cpu_x86     = 7,
      cpu_PowerPC = 8
    };

    struct Info {
      CPUs      cpu;
      uint32_t  flags;
    };

To support reading and writing of this enumeration, you can define a
ScalarEnumerationTraits specialization on CPUs, which can then be used
as a field type:

.. code-block:: c++

    using llvm::yaml::ScalarEnumerationTraits;
    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    template <>
    struct ScalarEnumerationTraits<CPUs> {
      static void enumeration(IO &io, CPUs &value) {
        io.enumCase(value, "x86_64",  cpu_x86_64);
        io.enumCase(value, "x86",     cpu_x86);
        io.enumCase(value, "PowerPC", cpu_PowerPC);
      }
    };

    template <>
    struct MappingTraits<Info> {
      static void mapping(IO &io, Info &info) {
        io.mapRequired("cpu",       info.cpu);
        io.mapOptional("flags",     info.flags, 0);
      }
    };

When reading YAML, if the string found does not match any of the strings
specified by enumCase() methods, an error is automatically generated.
When writing YAML, if the value being written does not match any of the values
specified by the enumCase() methods, a runtime assertion is triggered.


BitValue
--------
Another common data structure in C++ is a field where each bit has a unique
meaning.  This is often used in a "flags" field.  YAML I/O has support for
converting such fields to a flow sequence.   For instance suppose you
had the following bit flags defined:

.. code-block:: c++

    enum {
      flagsPointy = 1
      flagsHollow = 2
      flagsFlat   = 4
      flagsRound  = 8
    };

    LLVM_YAML_STRONG_TYPEDEF(uint32_t, MyFlags)

To support reading and writing of MyFlags, you specialize ScalarBitSetTraits<>
on MyFlags and provide the bit values and their names.

.. code-block:: c++

    using llvm::yaml::ScalarBitSetTraits;
    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    template <>
    struct ScalarBitSetTraits<MyFlags> {
      static void bitset(IO &io, MyFlags &value) {
        io.bitSetCase(value, "hollow",  flagHollow);
        io.bitSetCase(value, "flat",    flagFlat);
        io.bitSetCase(value, "round",   flagRound);
        io.bitSetCase(value, "pointy",  flagPointy);
      }
    };

    struct Info {
      StringRef   name;
      MyFlags     flags;
    };

    template <>
    struct MappingTraits<Info> {
      static void mapping(IO &io, Info& info) {
        io.mapRequired("name",  info.name);
        io.mapRequired("flags", info.flags);
       }
    };

With the above, YAML I/O (when writing) will test mask each value in the
bitset trait against the flags field, and each that matches will
cause the corresponding string to be added to the flow sequence.  The opposite
is done when reading and any unknown string values will result in an error. With
the above schema, a same valid YAML document is:

.. code-block:: yaml

    name:    Tom
    flags:   [ pointy, flat ]

Sometimes a "flags" field might contains an enumeration part
defined by a bit-mask.

.. code-block:: c++

    enum {
      flagsFeatureA = 1,
      flagsFeatureB = 2,
      flagsFeatureC = 4,

      flagsCPUMask = 24,

      flagsCPU1 = 8,
      flagsCPU2 = 16
    };

To support reading and writing such fields, you need to use the maskedBitSet()
method and provide the bit values, their names and the enumeration mask.

.. code-block:: c++

    template <>
    struct ScalarBitSetTraits<MyFlags> {
      static void bitset(IO &io, MyFlags &value) {
        io.bitSetCase(value, "featureA",  flagsFeatureA);
        io.bitSetCase(value, "featureB",  flagsFeatureB);
        io.bitSetCase(value, "featureC",  flagsFeatureC);
        io.maskedBitSetCase(value, "CPU1",  flagsCPU1, flagsCPUMask);
        io.maskedBitSetCase(value, "CPU2",  flagsCPU2, flagsCPUMask);
      }
    };

YAML I/O (when writing) will apply the enumeration mask to the flags field,
and compare the result and values from the bitset. As in case of a regular
bitset, each that matches will cause the corresponding string to be added
to the flow sequence.

Custom Scalar
-------------
Sometimes for readability a scalar needs to be formatted in a custom way. For
instance your internal data structure may use an integer for time (seconds since
some epoch), but in YAML it would be much nicer to express that integer in
some time format (e.g. 4-May-2012 10:30pm).  YAML I/O has a way to support
custom formatting and parsing of scalar types by specializing ScalarTraits<> on
your data type.  When writing, YAML I/O will provide the native type and
your specialization must create a temporary llvm::StringRef.  When reading,
YAML I/O will provide an llvm::StringRef of scalar and your specialization
must convert that to your native data type.  An outline of a custom scalar type
looks like:

.. code-block:: c++

    using llvm::yaml::ScalarTraits;
    using llvm::yaml::IO;

    template <>
    struct ScalarTraits<MyCustomType> {
      static void output(const MyCustomType &value, void*,
                         llvm::raw_ostream &out) {
        out << value;  // do custom formatting here
      }
      static StringRef input(StringRef scalar, void*, MyCustomType &value) {
        // do custom parsing here.  Return the empty string on success,
        // or an error message on failure.
        return StringRef();
      }
      // Determine if this scalar needs quotes.
      static QuotingType mustQuote(StringRef) { return QuotingType::Single; }
    };

Block Scalars
-------------

YAML block scalars are string literals that are represented in YAML using the
literal block notation, just like the example shown below:

.. code-block:: yaml

    text: |
      First line
      Second line

The YAML I/O library provides support for translating between YAML block scalars
and specific C++ types by allowing you to specialize BlockScalarTraits<> on
your data type. The library doesn't provide any built-in support for block
scalar I/O for types like std::string and llvm::StringRef as they are already
supported by YAML I/O and use the ordinary scalar notation by default.

BlockScalarTraits specializations are very similar to the
ScalarTraits specialization - YAML I/O will provide the native type and your
specialization must create a temporary llvm::StringRef when writing, and
it will also provide an llvm::StringRef that has the value of that block scalar
and your specialization must convert that to your native data type when reading.
An example of a custom type with an appropriate specialization of
BlockScalarTraits is shown below:

.. code-block:: c++

    using llvm::yaml::BlockScalarTraits;
    using llvm::yaml::IO;

    struct MyStringType {
      std::string Str;
    };

    template <>
    struct BlockScalarTraits<MyStringType> {
      static void output(const MyStringType &Value, void *Ctxt,
                         llvm::raw_ostream &OS) {
        OS << Value.Str;
      }

      static StringRef input(StringRef Scalar, void *Ctxt,
                             MyStringType &Value) {
        Value.Str = Scalar.str();
        return StringRef();
      }
    };



Mappings
========

To be translated to or from a YAML mapping for your type T you must specialize
llvm::yaml::MappingTraits on T and implement the "void mapping(IO &io, T&)"
method. If your native data structures use pointers to a class everywhere,
you can specialize on the class pointer.  Examples:

.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    // Example of struct Foo which is used by value
    template <>
    struct MappingTraits<Foo> {
      static void mapping(IO &io, Foo &foo) {
        io.mapOptional("size",      foo.size);
      ...
      }
    };

    // Example of struct Bar which is natively always a pointer
    template <>
    struct MappingTraits<Bar*> {
      static void mapping(IO &io, Bar *&bar) {
        io.mapOptional("size",    bar->size);
      ...
      }
    };

There are circumstances where we want to allow the entire mapping to be
read as an enumeration.  For example, say some configuration option
started as an enumeration.  Then it got more complex so it is now a
mapping.  But it is necessary to support the old configuration files.
In that case, add a function ``enumInput`` like for
``ScalarEnumerationTraits::enumeration``.  Examples:

.. code-block:: c++

    struct FooBarEnum {
      int Foo;
      int Bar;
      bool operator==(const FooBarEnum &R) const {
        return Foo == R.Foo && Bar == R.Bar;
      }
    };

    template <> struct MappingTraits<FooBarEnum> {
      static void enumInput(IO &io, FooBarEnum &Val) {
        io.enumCase(Val, "OnlyFoo", FooBarEnum({1, 0}));
        io.enumCase(Val, "OnlyBar", FooBarEnum({0, 1}));
      }
      static void mapping(IO &io, FooBarEnum &Val) {
        io.mapOptional("Foo", Val.Foo);
        io.mapOptional("Bar", Val.Bar);
      }
    };


No Normalization
----------------

The ``mapping()`` method is responsible, if needed, for normalizing and
denormalizing. In a simple case where the native data structure requires no
normalization, the mapping method just uses mapOptional() or mapRequired() to
bind the struct's fields to YAML key names.  For example:

.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    template <>
    struct MappingTraits<Person> {
      static void mapping(IO &io, Person &info) {
        io.mapRequired("name",         info.name);
        io.mapOptional("hat-size",     info.hatSize);
      }
    };


Normalization
----------------

When [de]normalization is required, the mapping() method needs a way to access
normalized values as fields. To help with this, there is
a template MappingNormalization<> which you can then use to automatically
do the normalization and denormalization.  The template is used to create
a local variable in your mapping() method which contains the normalized keys.

Suppose you have native data type
Polar which specifies a position in polar coordinates (distance, angle):

.. code-block:: c++

    struct Polar {
      float distance;
      float angle;
    };

but you've decided the normalized YAML for should be in x,y coordinates. That
is, you want the yaml to look like:

.. code-block:: yaml

    x:   10.3
    y:   -4.7

You can support this by defining a MappingTraits that normalizes the polar
coordinates to x,y coordinates when writing YAML and denormalizes x,y
coordinates into polar when reading YAML.

.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    template <>
    struct MappingTraits<Polar> {

      class NormalizedPolar {
      public:
        NormalizedPolar(IO &io)
          : x(0.0), y(0.0) {
        }
        NormalizedPolar(IO &, Polar &polar)
          : x(polar.distance * cos(polar.angle)),
            y(polar.distance * sin(polar.angle)) {
        }
        Polar denormalize(IO &) {
          return Polar(sqrt(x*x+y*y), arctan(x,y));
        }

        float        x;
        float        y;
      };

      static void mapping(IO &io, Polar &polar) {
        MappingNormalization<NormalizedPolar, Polar> keys(io, polar);

        io.mapRequired("x",    keys->x);
        io.mapRequired("y",    keys->y);
      }
    };

When writing YAML, the local variable "keys" will be a stack allocated
instance of NormalizedPolar, constructed from the supplied polar object which
initializes it x and y fields.  The mapRequired() methods then write out the x
and y values as key/value pairs.

When reading YAML, the local variable "keys" will be a stack allocated instance
of NormalizedPolar, constructed by the empty constructor.  The mapRequired
methods will find the matching key in the YAML document and fill in the x and y
fields of the NormalizedPolar object keys. At the end of the mapping() method
when the local keys variable goes out of scope, the denormalize() method will
automatically be called to convert the read values back to polar coordinates,
and then assigned back to the second parameter to mapping().

In some cases, the normalized class may be a subclass of the native type and
could be returned by the denormalize() method, except that the temporary
normalized instance is stack allocated.  In these cases, the utility template
MappingNormalizationHeap<> can be used instead.  It just like
MappingNormalization<> except that it heap allocates the normalized object
when reading YAML.  It never destroys the normalized object.  The denormalize()
method can this return "this".


Default values
--------------
Within a mapping() method, calls to io.mapRequired() mean that that key is
required to exist when parsing YAML documents, otherwise YAML I/O will issue an
error.

On the other hand, keys registered with io.mapOptional() are allowed to not
exist in the YAML document being read.  So what value is put in the field
for those optional keys?
There are two steps to how those optional fields are filled in. First, the
second parameter to the mapping() method is a reference to a native class.  That
native class must have a default constructor.  Whatever value the default
constructor initially sets for an optional field will be that field's value.
Second, the mapOptional() method has an optional third parameter.  If provided
it is the value that mapOptional() should set that field to if the YAML document
does not have that key.

There is one important difference between those two ways (default constructor
and third parameter to mapOptional). When YAML I/O generates a YAML document,
if the mapOptional() third parameter is used, if the actual value being written
is the same as (using ==) the default value, then that key/value is not written.


Order of Keys
--------------

When writing out a YAML document, the keys are written in the order that the
calls to mapRequired()/mapOptional() are made in the mapping() method. This
gives you a chance to write the fields in an order that a human reader of
the YAML document would find natural.  This may be different that the order
of the fields in the native class.

When reading in a YAML document, the keys in the document can be in any order,
but they are processed in the order that the calls to mapRequired()/mapOptional()
are made in the mapping() method.  That enables some interesting
functionality.  For instance, if the first field bound is the cpu and the second
field bound is flags, and the flags are cpu specific, you can programmatically
switch how the flags are converted to and from YAML based on the cpu.
This works for both reading and writing. For example:

.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    struct Info {
      CPUs        cpu;
      uint32_t    flags;
    };

    template <>
    struct MappingTraits<Info> {
      static void mapping(IO &io, Info &info) {
        io.mapRequired("cpu",       info.cpu);
        // flags must come after cpu for this to work when reading yaml
        if ( info.cpu == cpu_x86_64 )
          io.mapRequired("flags",  *(My86_64Flags*)info.flags);
        else
          io.mapRequired("flags",  *(My86Flags*)info.flags);
     }
    };


Tags
----

The YAML syntax supports tags as a way to specify the type of a node before
it is parsed. This allows dynamic types of nodes.  But the YAML I/O model uses
static typing, so there are limits to how you can use tags with the YAML I/O
model. Recently, we added support to YAML I/O for checking/setting the optional
tag on a map. Using this functionality it is even possible to support different
mappings, as long as they are convertible.

To check a tag, inside your mapping() method you can use io.mapTag() to specify
what the tag should be.  This will also add that tag when writing yaml.

Validation
----------

Sometimes in a YAML map, each key/value pair is valid, but the combination is
not.  This is similar to something having no syntax errors, but still having
semantic errors.  To support semantic level checking, YAML I/O allows
an optional ``validate()`` method in a MappingTraits template specialization.

When parsing YAML, the ``validate()`` method is call *after* all key/values in
the map have been processed. Any error message returned by the ``validate()``
method during input will be printed just a like a syntax error would be printed.
When writing YAML, the ``validate()`` method is called *before* the YAML
key/values  are written.  Any error during output will trigger an ``assert()``
because it is a programming error to have invalid struct values.


.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    struct Stuff {
      ...
    };

    template <>
    struct MappingTraits<Stuff> {
      static void mapping(IO &io, Stuff &stuff) {
      ...
      }
      static std::string validate(IO &io, Stuff &stuff) {
        // Look at all fields in 'stuff' and if there
        // are any bad values return a string describing
        // the error.  Otherwise return an empty string.
        return std::string{};
      }
    };

Flow Mapping
------------
A YAML "flow mapping" is a mapping that uses the inline notation
(e.g { x: 1, y: 0 } ) when written to YAML. To specify that a type should be
written in YAML using flow mapping, your MappingTraits specialization should
add "static const bool flow = true;". For instance:

.. code-block:: c++

    using llvm::yaml::MappingTraits;
    using llvm::yaml::IO;

    struct Stuff {
      ...
    };

    template <>
    struct MappingTraits<Stuff> {
      static void mapping(IO &io, Stuff &stuff) {
        ...
      }

      static const bool flow = true;
    }

Flow mappings are subject to line wrapping according to the Output object
configuration.

Sequence
========

To be translated to or from a YAML sequence for your type T you must specialize
llvm::yaml::SequenceTraits on T and implement two methods:
``size_t size(IO &io, T&)`` and
``T::value_type& element(IO &io, T&, size_t indx)``.  For example:

.. code-block:: c++

  template <>
  struct SequenceTraits<MySeq> {
    static size_t size(IO &io, MySeq &list) { ... }
    static MySeqEl &element(IO &io, MySeq &list, size_t index) { ... }
  };

The size() method returns how many elements are currently in your sequence.
The element() method returns a reference to the i'th element in the sequence.
When parsing YAML, the element() method may be called with an index one bigger
than the current size.  Your element() method should allocate space for one
more element (using default constructor if element is a C++ object) and returns
a reference to that new allocated space.


Flow Sequence
-------------
A YAML "flow sequence" is a sequence that when written to YAML it uses the
inline notation (e.g [ foo, bar ] ).  To specify that a sequence type should
be written in YAML as a flow sequence, your SequenceTraits specialization should
add "static const bool flow = true;".  For instance:

.. code-block:: c++

  template <>
  struct SequenceTraits<MyList> {
    static size_t size(IO &io, MyList &list) { ... }
    static MyListEl &element(IO &io, MyList &list, size_t index) { ... }

    // The existence of this member causes YAML I/O to use a flow sequence
    static const bool flow = true;
  };

With the above, if you used MyList as the data type in your native data
structures, then when converted to YAML, a flow sequence of integers
will be used (e.g. [ 10, -3, 4 ]).

Flow sequences are subject to line wrapping according to the Output object
configuration.

Utility Macros
--------------
Since a common source of sequences is std::vector<>, YAML I/O provides macros:
LLVM_YAML_IS_SEQUENCE_VECTOR() and LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR() which
can be used to easily specify SequenceTraits<> on a std::vector type.  YAML
I/O does not partial specialize SequenceTraits on std::vector<> because that
would force all vectors to be sequences.  An example use of the macros:

.. code-block:: c++

  std::vector<MyType1>;
  std::vector<MyType2>;
  LLVM_YAML_IS_SEQUENCE_VECTOR(MyType1)
  LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(MyType2)



Document List
=============

YAML allows you to define multiple "documents" in a single YAML file.  Each
new document starts with a left aligned "---" token.  The end of all documents
is denoted with a left aligned "..." token.  Many users of YAML will never
have need for multiple documents.  The top level node in their YAML schema
will be a mapping or sequence. For those cases, the following is not needed.
But for cases where you do want multiple documents, you can specify a
trait for you document list type.  The trait has the same methods as
SequenceTraits but is named DocumentListTraits.  For example:

.. code-block:: c++

  template <>
  struct DocumentListTraits<MyDocList> {
    static size_t size(IO &io, MyDocList &list) { ... }
    static MyDocType element(IO &io, MyDocList &list, size_t index) { ... }
  };


User Context Data
=================
When an llvm::yaml::Input or llvm::yaml::Output object is created their
constructors take an optional "context" parameter.  This is a pointer to
whatever state information you might need.

For instance, in a previous example we showed how the conversion type for a
flags field could be determined at runtime based on the value of another field
in the mapping. But what if an inner mapping needs to know some field value
of an outer mapping?  That is where the "context" parameter comes in. You
can set values in the context in the outer map's mapping() method and
retrieve those values in the inner map's mapping() method.

The context value is just a void*.  All your traits which use the context
and operate on your native data types, need to agree what the context value
actually is.  It could be a pointer to an object or struct which your various
traits use to shared context sensitive information.


Output
======

The llvm::yaml::Output class is used to generate a YAML document from your
in-memory data structures, using traits defined on your data types.
To instantiate an Output object you need an llvm::raw_ostream, an optional
context pointer and an optional wrapping column:

.. code-block:: c++

      class Output : public IO {
      public:
        Output(llvm::raw_ostream &, void *context = NULL, int WrapColumn = 70);

Once you have an Output object, you can use the C++ stream operator on it
to write your native data as YAML. One thing to recall is that a YAML file
can contain multiple "documents".  If the top level data structure you are
streaming as YAML is a mapping, scalar, or sequence, then Output assumes you
are generating one document and wraps the mapping output
with  "``---``" and trailing "``...``".

The WrapColumn parameter will cause the flow mappings and sequences to
line-wrap when they go over the supplied column. Pass 0 to completely
suppress the wrapping.

.. code-block:: c++

    using llvm::yaml::Output;

    void dumpMyMapDoc(const MyMapType &info) {
      Output yout(llvm::outs());
      yout << info;
    }

The above could produce output like:

.. code-block:: yaml

     ---
     name:      Tom
     hat-size:  7
     ...

On the other hand, if the top level data structure you are streaming as YAML
has a DocumentListTraits specialization, then Output walks through each element
of your DocumentList and generates a "---" before the start of each element
and ends with a "...".

.. code-block:: c++

    using llvm::yaml::Output;

    void dumpMyMapDoc(const MyDocListType &docList) {
      Output yout(llvm::outs());
      yout << docList;
    }

The above could produce output like:

.. code-block:: yaml

     ---
     name:      Tom
     hat-size:  7
     ---
     name:      Tom
     shoe-size:  11
     ...

Input
=====

The llvm::yaml::Input class is used to parse YAML document(s) into your native
data structures. To instantiate an Input
object you need a StringRef to the entire YAML file, and optionally a context
pointer:

.. code-block:: c++

      class Input : public IO {
      public:
        Input(StringRef inputContent, void *context=NULL);

Once you have an Input object, you can use the C++ stream operator to read
the document(s).  If you expect there might be multiple YAML documents in
one file, you'll need to specialize DocumentListTraits on a list of your
document type and stream in that document list type.  Otherwise you can
just stream in the document type.  Also, you can check if there was
any syntax errors in the YAML be calling the error() method on the Input
object.  For example:

.. code-block:: c++

     // Reading a single document
     using llvm::yaml::Input;

     Input yin(mb.getBuffer());

     // Parse the YAML file
     MyDocType theDoc;
     yin >> theDoc;

     // Check for error
     if ( yin.error() )
       return;


.. code-block:: c++

     // Reading multiple documents in one file
     using llvm::yaml::Input;

     LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(MyDocType)

     Input yin(mb.getBuffer());

     // Parse the YAML file
     std::vector<MyDocType> theDocList;
     yin >> theDocList;

     // Check for error
     if ( yin.error() )
       return;
