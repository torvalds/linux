Missing Key Function
====================

If your build failed with a linker error something like this::

  foo.cc:28: error: undefined reference to 'vtable for C'
  the vtable symbol may be undefined because the class is missing its key function
  (see https://lld.llvm.org/missingkeyfunction)

it's likely that your class C has a key function (defined by the ABI as the first
non-pure, non-inline, virtual function), but you haven't actually defined it.

When a class has a key function, the compiler emits the vtable (and some other
things as well) only in the translation unit that defines that key function. Thus,
if you're missing the key function, you'll also be missing the vtable. If no other
function calls your missing function, you won't see any undefined reference errors
for it, but you will see undefined references to the vtable symbol.

When a class has no non-pure, non-inline, virtual functions, there is no key
function, and the compiler is forced to emit the vtable in every translation unit
that references the class. In this case, it is emitted in a COMDAT section,
which allows the linker to eliminate all duplicate copies. This is still
wasteful in terms of object file size and link time, so it's always advisable to
ensure there is at least one eligible function that can serve as the key function.

Here are the most common mistakes that lead to this error:

Failing to define a virtual destructor
--------------------------------------

Say you have a base class declared in a header file::

  class B {
  public:
    B();
    virtual ~B();
    ...
  };

Here, ``~B`` is the first non-pure, non-inline, virtual function, so it is the key
function. If you forget to define ``B::~B`` in your source file, the compiler will
not emit the vtable for ``B``, and you'll get an undefined reference to "vtable
for B".

This is just an example of the more general mistake of forgetting to define the
key function, but it's quite common because virtual destructors are likely to be
the first eligible key function and it's easy to forget to implement them. It's
also more likely that you won't have any direct references to the destructor, so
you won't see any undefined reference errors that point directly to the problem.

The solution in this case is to implement the missing function.

Forgetting to declare a virtual function in an abstract class as pure
---------------------------------------------------------------------

Say you have an abstract base class declared in a header file::

  class A {
  public:
    A();
    virtual ~A() {}
    virtual int foo() = 0;
    ...
    virtual int bar();
    ...
  };

This base class is intended to be abstract, but you forgot to mark one of the
functions pure. Here, ``A::bar``, being non-pure, is nominated as the key function,
and as a result, the vtable for ``A`` is not emitted, because the compiler is
waiting for a translation unit that defines ``A::bar``.

The solution in this case is to add the missing ``= 0`` to the declaration of
``A::bar``.

Key function is defined, but the linker doesn't see it
------------------------------------------------------

It's also possible that you have defined the key function somewhere, but the
object file containing the definition of that function isn't being linked into
your application.

The solution in this case is to check your dependencies to make sure that
the object file or the library file containing the key function is given to
the linker.
