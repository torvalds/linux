
====================
``<atomic>`` Design
====================

There were originally 3 designs under consideration. They differ in where most
of the implementation work is done. The functionality exposed to the customer
should be identical (and conforming) for all three designs.


Design A: Minimal work for the library
======================================
The compiler supplies all of the intrinsics as described below. This list of
intrinsics roughly parallels the requirements of the C and C++ atomics proposals.
The C and C++ library implementations simply drop through to these intrinsics.
Anything the platform does not support in hardware, the compiler
arranges for a (compiler-rt) library call to be made which will do the job with
a mutex, and in this case ignoring the memory ordering parameter (effectively
implementing ``memory_order_seq_cst``).

Ultimate efficiency is preferred over run time error checking. Undefined
behavior is acceptable when the inputs do not conform as defined below.

.. code-block:: cpp

    // In every intrinsic signature below, type* atomic_obj may be a pointer to a
    // volatile-qualified type. Memory ordering values map to the following meanings:
    //  memory_order_relaxed == 0
    //  memory_order_consume == 1
    //  memory_order_acquire == 2
    //  memory_order_release == 3
    //  memory_order_acq_rel == 4
    //  memory_order_seq_cst == 5

    // type must be trivially copyable
    // type represents a "type argument"
    bool __atomic_is_lock_free(type);

    // type must be trivially copyable
    // Behavior is defined for mem_ord = 0, 1, 2, 5
    type __atomic_load(const type* atomic_obj, int mem_ord);

    // type must be trivially copyable
    // Behavior is defined for mem_ord = 0, 3, 5
    void __atomic_store(type* atomic_obj, type desired, int mem_ord);

    // type must be trivially copyable
    // Behavior is defined for mem_ord = [0 ... 5]
    type __atomic_exchange(type* atomic_obj, type desired, int mem_ord);

    // type must be trivially copyable
    // Behavior is defined for mem_success = [0 ... 5],
    //   mem_failure <= mem_success
    //   mem_failure != 3
    //   mem_failure != 4
    bool __atomic_compare_exchange_strong(type* atomic_obj,
                                        type* expected, type desired,
                                        int mem_success, int mem_failure);

    // type must be trivially copyable
    // Behavior is defined for mem_success = [0 ... 5],
    //   mem_failure <= mem_success
    //   mem_failure != 3
    //   mem_failure != 4
    bool __atomic_compare_exchange_weak(type* atomic_obj,
                                        type* expected, type desired,
                                        int mem_success, int mem_failure);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    // Behavior is defined for mem_ord = [0 ... 5]
    type __atomic_fetch_add(type* atomic_obj, type operand, int mem_ord);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    // Behavior is defined for mem_ord = [0 ... 5]
    type __atomic_fetch_sub(type* atomic_obj, type operand, int mem_ord);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    // Behavior is defined for mem_ord = [0 ... 5]
    type __atomic_fetch_and(type* atomic_obj, type operand, int mem_ord);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    // Behavior is defined for mem_ord = [0 ... 5]
    type __atomic_fetch_or(type* atomic_obj, type operand, int mem_ord);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    // Behavior is defined for mem_ord = [0 ... 5]
    type __atomic_fetch_xor(type* atomic_obj, type operand, int mem_ord);

    // Behavior is defined for mem_ord = [0 ... 5]
    void* __atomic_fetch_add(void** atomic_obj, ptrdiff_t operand, int mem_ord);
    void* __atomic_fetch_sub(void** atomic_obj, ptrdiff_t operand, int mem_ord);

    // Behavior is defined for mem_ord = [0 ... 5]
    void __atomic_thread_fence(int mem_ord);
    void __atomic_signal_fence(int mem_ord);

If desired the intrinsics taking a single ``mem_ord`` parameter can default
this argument to 5.

If desired the intrinsics taking two ordering parameters can default ``mem_success``
to 5, and ``mem_failure`` to ``translate_memory_order(mem_success)`` where
``translate_memory_order(mem_success)`` is defined as:

.. code-block:: cpp

    int translate_memory_order(int o) {
        switch (o) {
        case 4:
            return 2;
        case 3:
            return 0;
        }
        return o;
    }

Below are representative C++ implementations of all of the operations. Their
purpose is to document the desired semantics of each operation, assuming
``memory_order_seq_cst``. This is essentially the code that will be called
if the front end calls out to compiler-rt.

.. code-block:: cpp

    template <class T>
    T __atomic_load(T const volatile* obj) {
        unique_lock<mutex> _(some_mutex);
        return *obj;
    }

    template <class T>
    void __atomic_store(T volatile* obj, T desr) {
        unique_lock<mutex> _(some_mutex);
        *obj = desr;
    }

    template <class T>
    T __atomic_exchange(T volatile* obj, T desr) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj = desr;
        return r;
    }

    template <class T>
    bool __atomic_compare_exchange_strong(T volatile* obj, T* exp, T desr) {
        unique_lock<mutex> _(some_mutex);
        if (std::memcmp(const_cast<T*>(obj), exp, sizeof(T)) == 0) // if (*obj == *exp)
        {
            std::memcpy(const_cast<T*>(obj), &desr, sizeof(T)); // *obj = desr;
            return true;
        }
        std::memcpy(exp, const_cast<T*>(obj), sizeof(T)); // *exp = *obj;
        return false;
    }

    // May spuriously return false (even if *obj == *exp)
    template <class T>
    bool __atomic_compare_exchange_weak(T volatile* obj, T* exp, T desr) {
        unique_lock<mutex> _(some_mutex);
        if (std::memcmp(const_cast<T*>(obj), exp, sizeof(T)) == 0) // if (*obj == *exp)
        {
            std::memcpy(const_cast<T*>(obj), &desr, sizeof(T)); // *obj = desr;
            return true;
        }
        std::memcpy(exp, const_cast<T*>(obj), sizeof(T)); // *exp = *obj;
        return false;
    }

    template <class T>
    T __atomic_fetch_add(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj += operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_sub(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj -= operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_and(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj &= operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_or(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj |= operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_xor(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj ^= operand;
        return r;
    }

    void* __atomic_fetch_add(void* volatile* obj, ptrdiff_t operand) {
        unique_lock<mutex> _(some_mutex);
        void* r = *obj;
        (char*&)(*obj) += operand;
        return r;
    }

    void* __atomic_fetch_sub(void* volatile* obj, ptrdiff_t operand) {
        unique_lock<mutex> _(some_mutex);
        void* r = *obj;
        (char*&)(*obj) -= operand;
        return r;
    }

    void __atomic_thread_fence() {
        unique_lock<mutex> _(some_mutex);
    }

    void __atomic_signal_fence() {
        unique_lock<mutex> _(some_mutex);
    }


Design B: Something in between
==============================
This is a variation of design A which puts the burden on the library to arrange
for the correct manipulation of the run time memory ordering arguments, and only
calls the compiler for well-defined memory orderings. I think of this design as
the worst of A and C, instead of the best of A and C. But I offer it as an
option in the spirit of completeness.

.. code-block:: cpp

    // type must be trivially copyable
    bool __atomic_is_lock_free(const type* atomic_obj);

    // type must be trivially copyable
    type __atomic_load_relaxed(const volatile type* atomic_obj);
    type __atomic_load_consume(const volatile type* atomic_obj);
    type __atomic_load_acquire(const volatile type* atomic_obj);
    type __atomic_load_seq_cst(const volatile type* atomic_obj);

    // type must be trivially copyable
    type __atomic_store_relaxed(volatile type* atomic_obj, type desired);
    type __atomic_store_release(volatile type* atomic_obj, type desired);
    type __atomic_store_seq_cst(volatile type* atomic_obj, type desired);

    // type must be trivially copyable
    type __atomic_exchange_relaxed(volatile type* atomic_obj, type desired);
    type __atomic_exchange_consume(volatile type* atomic_obj, type desired);
    type __atomic_exchange_acquire(volatile type* atomic_obj, type desired);
    type __atomic_exchange_release(volatile type* atomic_obj, type desired);
    type __atomic_exchange_acq_rel(volatile type* atomic_obj, type desired);
    type __atomic_exchange_seq_cst(volatile type* atomic_obj, type desired);

    // type must be trivially copyable
    bool __atomic_compare_exchange_strong_relaxed_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_consume_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_consume_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_acquire_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_acquire_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_acquire_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_release_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_release_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_release_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_acq_rel_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_acq_rel_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_acq_rel_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_seq_cst_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_seq_cst_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_seq_cst_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_strong_seq_cst_seq_cst(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);

    // type must be trivially copyable
    bool __atomic_compare_exchange_weak_relaxed_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_consume_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_consume_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_acquire_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_acquire_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_acquire_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_release_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_release_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_release_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_acq_rel_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_acq_rel_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_acq_rel_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_seq_cst_relaxed(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_seq_cst_consume(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_seq_cst_acquire(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);
    bool __atomic_compare_exchange_weak_seq_cst_seq_cst(volatile type* atomic_obj,
                                                        type* expected,
                                                        type desired);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    type __atomic_fetch_add_relaxed(volatile type* atomic_obj, type operand);
    type __atomic_fetch_add_consume(volatile type* atomic_obj, type operand);
    type __atomic_fetch_add_acquire(volatile type* atomic_obj, type operand);
    type __atomic_fetch_add_release(volatile type* atomic_obj, type operand);
    type __atomic_fetch_add_acq_rel(volatile type* atomic_obj, type operand);
    type __atomic_fetch_add_seq_cst(volatile type* atomic_obj, type operand);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    type __atomic_fetch_sub_relaxed(volatile type* atomic_obj, type operand);
    type __atomic_fetch_sub_consume(volatile type* atomic_obj, type operand);
    type __atomic_fetch_sub_acquire(volatile type* atomic_obj, type operand);
    type __atomic_fetch_sub_release(volatile type* atomic_obj, type operand);
    type __atomic_fetch_sub_acq_rel(volatile type* atomic_obj, type operand);
    type __atomic_fetch_sub_seq_cst(volatile type* atomic_obj, type operand);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    type __atomic_fetch_and_relaxed(volatile type* atomic_obj, type operand);
    type __atomic_fetch_and_consume(volatile type* atomic_obj, type operand);
    type __atomic_fetch_and_acquire(volatile type* atomic_obj, type operand);
    type __atomic_fetch_and_release(volatile type* atomic_obj, type operand);
    type __atomic_fetch_and_acq_rel(volatile type* atomic_obj, type operand);
    type __atomic_fetch_and_seq_cst(volatile type* atomic_obj, type operand);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    type __atomic_fetch_or_relaxed(volatile type* atomic_obj, type operand);
    type __atomic_fetch_or_consume(volatile type* atomic_obj, type operand);
    type __atomic_fetch_or_acquire(volatile type* atomic_obj, type operand);
    type __atomic_fetch_or_release(volatile type* atomic_obj, type operand);
    type __atomic_fetch_or_acq_rel(volatile type* atomic_obj, type operand);
    type __atomic_fetch_or_seq_cst(volatile type* atomic_obj, type operand);

    // type is one of: char, signed char, unsigned char, short, unsigned short, int,
    //      unsigned int, long, unsigned long, long long, unsigned long long,
    //      char16_t, char32_t, wchar_t
    type __atomic_fetch_xor_relaxed(volatile type* atomic_obj, type operand);
    type __atomic_fetch_xor_consume(volatile type* atomic_obj, type operand);
    type __atomic_fetch_xor_acquire(volatile type* atomic_obj, type operand);
    type __atomic_fetch_xor_release(volatile type* atomic_obj, type operand);
    type __atomic_fetch_xor_acq_rel(volatile type* atomic_obj, type operand);
    type __atomic_fetch_xor_seq_cst(volatile type* atomic_obj, type operand);

    void* __atomic_fetch_add_relaxed(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_add_consume(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_add_acquire(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_add_release(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_add_acq_rel(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_add_seq_cst(void* volatile* atomic_obj, ptrdiff_t operand);

    void* __atomic_fetch_sub_relaxed(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_sub_consume(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_sub_acquire(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_sub_release(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_sub_acq_rel(void* volatile* atomic_obj, ptrdiff_t operand);
    void* __atomic_fetch_sub_seq_cst(void* volatile* atomic_obj, ptrdiff_t operand);

    void __atomic_thread_fence_relaxed();
    void __atomic_thread_fence_consume();
    void __atomic_thread_fence_acquire();
    void __atomic_thread_fence_release();
    void __atomic_thread_fence_acq_rel();
    void __atomic_thread_fence_seq_cst();

    void __atomic_signal_fence_relaxed();
    void __atomic_signal_fence_consume();
    void __atomic_signal_fence_acquire();
    void __atomic_signal_fence_release();
    void __atomic_signal_fence_acq_rel();
    void __atomic_signal_fence_seq_cst();

Design C: Minimal work for the front end
========================================
The ``<atomic>`` header is one of the most closely coupled headers to the compiler.
Ideally when you invoke any function from ``<atomic>``, it should result in highly
optimized assembly being inserted directly into your application -- assembly that
is not otherwise representable by higher level C or C++ expressions. The design of
the libc++ ``<atomic>`` header started with this goal in mind. A secondary, but
still very important goal is that the compiler should have to do minimal work to
facilitate the implementation of ``<atomic>``.  Without this second goal, then
practically speaking, the libc++ ``<atomic>`` header would be doomed to be a
barely supported, second class citizen on almost every platform.

Goals:

- Optimal code generation for atomic operations
- Minimal effort for the compiler to achieve goal 1 on any given platform
- Conformance to the C++0X draft standard

The purpose of this document is to inform compiler writers what they need to do
to enable a high performance libc++ ``<atomic>`` with minimal effort.

The minimal work that must be done for a conforming ``<atomic>``
----------------------------------------------------------------
The only "atomic" operations that must actually be lock free in
``<atomic>`` are represented by the following compiler intrinsics:

.. code-block:: cpp

    __atomic_flag__ __atomic_exchange_seq_cst(__atomic_flag__ volatile* obj, __atomic_flag__ desr) {
        unique_lock<mutex> _(some_mutex);
        __atomic_flag__ result = *obj;
        *obj = desr;
        return result;
    }

    void __atomic_store_seq_cst(__atomic_flag__ volatile* obj, __atomic_flag__ desr) {
        unique_lock<mutex> _(some_mutex);
        *obj = desr;
    }

Where:

- If ``__has_feature(__atomic_flag)`` evaluates to 1 in the preprocessor then
  the compiler must define ``__atomic_flag__`` (e.g. as a typedef to ``int``).
- If ``__has_feature(__atomic_flag)`` evaluates to 0 in the preprocessor then
  the library defines ``__atomic_flag__`` as a typedef to ``bool``.
- To communicate that the above intrinsics are available, the compiler must
  arrange for ``__has_feature`` to return 1 when fed the intrinsic name
  appended with an '_' and the mangled type name of ``__atomic_flag__``.

For example if ``__atomic_flag__`` is ``unsigned int``:

.. code-block:: cpp

    // __has_feature(__atomic_flag) == 1
    // __has_feature(__atomic_exchange_seq_cst_j) == 1
    // __has_feature(__atomic_store_seq_cst_j) == 1

    typedef unsigned int __atomic_flag__;

    unsigned int __atomic_exchange_seq_cst(unsigned int volatile*, unsigned int) {
        // ...
    }

    void __atomic_store_seq_cst(unsigned int volatile*, unsigned int) {
        // ...
    }

That's it! Compiler writers do the above and you've got a fully conforming
(though sub-par performance) ``<atomic>`` header!


Recommended work for a higher performance ``<atomic>``
------------------------------------------------------
It would be good if the above intrinsics worked with all integral types plus
``void*``. Because this may not be possible to do in a lock-free manner for
all integral types on all platforms, a compiler must communicate each type that
an intrinsic works with. For example, if ``__atomic_exchange_seq_cst`` works
for all types except for ``long long`` and ``unsigned long long`` then:

.. code-block:: cpp

    __has_feature(__atomic_exchange_seq_cst_b) == 1  // bool
    __has_feature(__atomic_exchange_seq_cst_c) == 1  // char
    __has_feature(__atomic_exchange_seq_cst_a) == 1  // signed char
    __has_feature(__atomic_exchange_seq_cst_h) == 1  // unsigned char
    __has_feature(__atomic_exchange_seq_cst_Ds) == 1 // char16_t
    __has_feature(__atomic_exchange_seq_cst_Di) == 1 // char32_t
    __has_feature(__atomic_exchange_seq_cst_w) == 1  // wchar_t
    __has_feature(__atomic_exchange_seq_cst_s) == 1  // short
    __has_feature(__atomic_exchange_seq_cst_t) == 1  // unsigned short
    __has_feature(__atomic_exchange_seq_cst_i) == 1  // int
    __has_feature(__atomic_exchange_seq_cst_j) == 1  // unsigned int
    __has_feature(__atomic_exchange_seq_cst_l) == 1  // long
    __has_feature(__atomic_exchange_seq_cst_m) == 1  // unsigned long
    __has_feature(__atomic_exchange_seq_cst_Pv) == 1 // void*

Note that only the ``__has_feature`` flag is decorated with the argument
type. The name of the compiler intrinsic is not decorated, but instead works
like a C++ overloaded function.

Additionally, there are other intrinsics besides ``__atomic_exchange_seq_cst``
and ``__atomic_store_seq_cst``. They are optional. But if the compiler can
generate faster code than provided by the library, then clients will benefit
from the compiler writer's expertise and knowledge of the targeted platform.

Below is the complete list of *sequentially consistent* intrinsics, and
their library implementations. Template syntax is used to indicate the desired
overloading for integral and ``void*`` types. The template does not represent a
requirement that the intrinsic operate on **any** type!

.. code-block:: cpp

    // T is one of:
    // bool, char, signed char, unsigned char, short, unsigned short,
    // int, unsigned int, long, unsigned long,
    // long long, unsigned long long, char16_t, char32_t, wchar_t, void*

    template <class T>
    T __atomic_load_seq_cst(T const volatile* obj) {
        unique_lock<mutex> _(some_mutex);
        return *obj;
    }

    template <class T>
    void __atomic_store_seq_cst(T volatile* obj, T desr) {
        unique_lock<mutex> _(some_mutex);
        *obj = desr;
    }

    template <class T>
    T __atomic_exchange_seq_cst(T volatile* obj, T desr) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj = desr;
        return r;
    }

    template <class T>
    bool __atomic_compare_exchange_strong_seq_cst_seq_cst(T volatile* obj, T* exp, T desr) {
        unique_lock<mutex> _(some_mutex);
        if (std::memcmp(const_cast<T*>(obj), exp, sizeof(T)) == 0) {
            std::memcpy(const_cast<T*>(obj), &desr, sizeof(T));
            return true;
        }
        std::memcpy(exp, const_cast<T*>(obj), sizeof(T));
        return false;
    }

    template <class T>
    bool __atomic_compare_exchange_weak_seq_cst_seq_cst(T volatile* obj, T* exp, T desr) {
        unique_lock<mutex> _(some_mutex);
        if (std::memcmp(const_cast<T*>(obj), exp, sizeof(T)) == 0)
        {
            std::memcpy(const_cast<T*>(obj), &desr, sizeof(T));
            return true;
        }
        std::memcpy(exp, const_cast<T*>(obj), sizeof(T));
        return false;
    }

    // T is one of:
    // char, signed char, unsigned char, short, unsigned short,
    // int, unsigned int, long, unsigned long,
    // long long, unsigned long long, char16_t, char32_t, wchar_t

    template <class T>
    T __atomic_fetch_add_seq_cst(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj += operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_sub_seq_cst(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj -= operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_and_seq_cst(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj &= operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_or_seq_cst(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj |= operand;
        return r;
    }

    template <class T>
    T __atomic_fetch_xor_seq_cst(T volatile* obj, T operand) {
        unique_lock<mutex> _(some_mutex);
        T r = *obj;
        *obj ^= operand;
        return r;
    }

    void* __atomic_fetch_add_seq_cst(void* volatile* obj, ptrdiff_t operand) {
        unique_lock<mutex> _(some_mutex);
        void* r = *obj;
        (char*&)(*obj) += operand;
        return r;
    }

    void* __atomic_fetch_sub_seq_cst(void* volatile* obj, ptrdiff_t operand) {
        unique_lock<mutex> _(some_mutex);
        void* r = *obj;
        (char*&)(*obj) -= operand;
        return r;
    }

    void __atomic_thread_fence_seq_cst() {
        unique_lock<mutex> _(some_mutex);
    }

    void __atomic_signal_fence_seq_cst() {
        unique_lock<mutex> _(some_mutex);
    }

One should consult the (currently draft) `C++ Standard <https://wg21.link/n3126>`_
for the details of the definitions for these operations. For example,
``__atomic_compare_exchange_weak_seq_cst_seq_cst`` is allowed to fail
spuriously while ``__atomic_compare_exchange_strong_seq_cst_seq_cst`` is not.

If on your platform the lock-free definition of ``__atomic_compare_exchange_weak_seq_cst_seq_cst``
would be the same as ``__atomic_compare_exchange_strong_seq_cst_seq_cst``, you may omit the
``__atomic_compare_exchange_weak_seq_cst_seq_cst`` intrinsic without a performance cost. The
library will prefer your implementation of ``__atomic_compare_exchange_strong_seq_cst_seq_cst``
over its own definition for implementing ``__atomic_compare_exchange_weak_seq_cst_seq_cst``.
That is, the library will arrange for ``__atomic_compare_exchange_weak_seq_cst_seq_cst`` to call
``__atomic_compare_exchange_strong_seq_cst_seq_cst`` if you supply an intrinsic for the strong
version but not the weak.

Taking advantage of weaker memory synchronization
-------------------------------------------------
So far, all of the intrinsics presented require a **sequentially consistent** memory ordering.
That is, no loads or stores can move across the operation (just as if the library had locked
that internal mutex). But ``<atomic>`` supports weaker memory ordering operations. In all,
there are six memory orderings (listed here from strongest to weakest):

.. code-block:: cpp

    memory_order_seq_cst
    memory_order_acq_rel
    memory_order_release
    memory_order_acquire
    memory_order_consume
    memory_order_relaxed

(See the `C++ Standard <https://wg21.link/n3126>`_ for the detailed definitions of each of these orderings).

On some platforms, the compiler vendor can offer some or even all of the above
intrinsics at one or more weaker levels of memory synchronization. This might
lead for example to not issuing an ``mfence`` instruction on the x86.

If the compiler does not offer any given operation, at any given memory ordering
level, the library will automatically attempt to call the next highest memory
ordering operation. This continues up to ``seq_cst``, and if that doesn't
exist, then the library takes over and does the job with a ``mutex``. This
is a compile-time search and selection operation. At run time, the application
will only see the few inlined assembly instructions for the selected intrinsic.

Each intrinsic is appended with the 7-letter name of the memory ordering it
addresses. For example a ``load`` with ``relaxed`` ordering is defined by:

.. code-block:: cpp

    T __atomic_load_relaxed(const volatile T* obj);

And announced with:

.. code-block:: cpp

    __has_feature(__atomic_load_relaxed_b) == 1  // bool
    __has_feature(__atomic_load_relaxed_c) == 1  // char
    __has_feature(__atomic_load_relaxed_a) == 1  // signed char
    ...

The ``__atomic_compare_exchange_strong(weak)`` intrinsics are parameterized
on two memory orderings. The first ordering applies when the operation returns
``true`` and the second ordering applies when the operation returns ``false``.

Not every memory ordering is appropriate for every operation. ``exchange``
and the ``fetch_XXX`` operations support all 6. But ``load`` only supports
``relaxed``, ``consume``, ``acquire`` and ``seq_cst``. ``store`` only supports
``relaxed``, ``release``, and ``seq_cst``. The ``compare_exchange`` operations
support the following 16 combinations out of the possible 36:

.. code-block:: cpp

    relaxed_relaxed
    consume_relaxed
    consume_consume
    acquire_relaxed
    acquire_consume
    acquire_acquire
    release_relaxed
    release_consume
    release_acquire
    acq_rel_relaxed
    acq_rel_consume
    acq_rel_acquire
    seq_cst_relaxed
    seq_cst_consume
    seq_cst_acquire
    seq_cst_seq_cst

Again, the compiler supplies intrinsics only for the strongest orderings where
it can make a difference. The library takes care of calling the weakest
supplied intrinsic that is as strong or stronger than the customer asked for.

Note about ABI
==============
With any design, the (back end) compiler writer should note that the decision to
implement lock-free operations on any given type (or not) is an ABI-binding decision.
One can not change from treating a type as not lock free, to lock free (or vice-versa)
without breaking your ABI.

For example:

**TU1.cpp**:

.. code-block:: cpp

    extern atomic<long long> A;
    int foo() { return A.compare_exchange_strong(w, x); }


**TU2.cpp**:

.. code-block:: cpp

    extern atomic<long long> A;
    void bar() { return A.compare_exchange_strong(y, z); }

If only **one** of these calls to ``compare_exchange_strong`` is implemented with
mutex-locked code, then that mutex-locked code will not be executed mutually
exclusively of the one implemented in a lock-free manner.
