=============================================
Enable std::unique_ptr [[clang::trivial_abi]]
=============================================

Background
==========

Consider the follow snippets


.. code-block:: cpp

    void raw_func(Foo* raw_arg) { ... }
    void smart_func(std::unique_ptr<Foo> smart_arg) { ... }

    Foo* raw_ptr_retval() { ... }
    std::unique_ptr<Foo*> smart_ptr_retval() { ... }



The argument ``raw_arg`` could be passed in a register but ``smart_arg`` could not, due to current
implementation.

Specifically, in the ``smart_arg`` case, the caller secretly constructs a temporary ``std::unique_ptr``
in its stack-frame, and then passes a pointer to it to the callee in a hidden parameter.
Similarly, the return value from ``smart_ptr_retval`` is secretly allocated in the caller and
passed as a secret reference to the callee.


Goal
===================

``std::unique_ptr`` is passed directly in a register.

Design
======

* Annotate the two definitions of ``std::unique_ptr``  with ``clang::trivial_abi`` attribute.
* Put the attribute behind a flag because this change has potential compilation and runtime breakages.


This comes with some side effects:

* ``std::unique_ptr`` parameters will now be destroyed by callees, rather than callers.
  It is worth noting that destruction by callee is not unique to the use of trivial_abi attribute.
  In most Microsoft's ABIs, arguments are always destroyed by the callee.

  Consequently, this may change the destruction order for function parameters to an order that is non-conforming to the standard.
  For example:


  .. code-block:: cpp

    struct A { ~A(); };
    struct B { ~B(); };
    struct C { C(A, unique_ptr<B>, A) {} };
    C c{{}, make_unique<B>, {}};


  In a conforming implementation, the destruction order for C::C's parameters is required to be ``~A(), ~B(), ~A()`` but with this mode enabled, we'll instead see ``~B(), ~A(), ~A()``.

* Reduced code-size.


Performance impact
------------------

Google has measured performance improvements of up to 1.6% on some large server macrobenchmarks, and a small reduction in binary sizes.

This also affects null pointer optimization

Clang's optimizer can now figure out when a `std::unique_ptr` is known to contain *non*-null.
(Actually, this has been a *missed* optimization all along.)


.. code-block:: cpp

    struct Foo {
      ~Foo();
    };
    std::unique_ptr<Foo> make_foo();
    void do_nothing(const Foo&)

    void bar() {
      auto x = make_foo();
      do_nothing(*x);
    }


With this change, ``~Foo()`` will be called even if ``make_foo`` returns ``unique_ptr<Foo>(nullptr)``.
The compiler can now assume that ``x.get()`` cannot be null by the end of ``bar()``, because
the deference of ``x`` would be UB if it were ``nullptr``. (This dereference would not have caused
a segfault, because no load is generated for dereferencing a pointer to a reference. This can be detected with ``-fsanitize=null``).


Potential breakages
-------------------

The following breakages were discovered by enabling this change and fixing the resulting issues in a large code base.

- Compilation failures

 - Function definitions now require complete type ``T`` for parameters with type ``std::unique_ptr<T>``. The following code will no longer compile.

   .. code-block:: cpp

       class Foo;
       void func(std::unique_ptr<Foo> arg) { /* never use `arg` directly */ }

 - Fix: Remove forward-declaration of ``Foo`` and include its proper header.

- Runtime Failures

 - Lifetime of ``std::unique_ptr<>`` arguments end earlier (at the end of the callee's body, rather than at the end of the full expression containing the call).

   .. code-block:: cpp

     util::Status run_worker(std::unique_ptr<Foo>);
     void func() {
        std::unique_ptr<Foo> smart_foo = ...;
        Foo* owned_foo = smart_foo.get();
        // Currently, the following would "work" because the argument to run_worker() is deleted at the end of func()
        // With the new calling convention, it will be deleted at the end of run_worker(),
        // making this an access to freed memory.
        owned_foo->Bar(run_worker(std::move(smart_foo)));
                  ^
                 // <<<Crash expected here
     }

 - Lifetime of local *returned* ``std::unique_ptr<>`` ends earlier.

   Spot the bug:

    .. code-block:: cpp

     std::unique_ptr<Foo> create_and_subscribe(Bar* subscriber) {
       auto foo = std::make_unique<Foo>();
       subscriber->sub([&foo] { foo->do_thing();} );
       return foo;
     }

   One could point out this is an obvious stack-use-after return bug.
   With the current calling convention, running this code with ASAN enabled, however, would not yield any "issue".
   So is this a bug in ASAN? (Spoiler: No)

   This currently would "work" only because the storage for ``foo`` is in the caller's stackframe.
   In other words, ``&foo`` in callee and ``&foo`` in the caller are the same address.

ASAN can be used to detect both of these.
