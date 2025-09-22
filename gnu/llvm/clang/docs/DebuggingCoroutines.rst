========================
Debugging C++ Coroutines
========================

.. contents::
   :local:

Introduction
============

For performance and other architectural reasons, the C++ Coroutines feature in
the Clang compiler is implemented in two parts of the compiler.  Semantic
analysis is performed in Clang, and Coroutine construction and optimization
takes place in the LLVM middle-end.

However, this design forces us to generate insufficient debugging information.
Typically, the compiler generates debug information in the Clang frontend, as
debug information is highly language specific. However, this is not possible
for Coroutine frames because the frames are constructed in the LLVM middle-end.

To mitigate this problem, the LLVM middle end attempts to generate some debug
information, which is unfortunately incomplete, since much of the language
specific information is missing in the middle end.

This document describes how to use this debug information to better debug
coroutines.

Terminology
===========

Due to the recent nature of C++20 Coroutines, the terminology used to describe
the concepts of Coroutines is not settled.  This section defines a common,
understandable terminology to be used consistently throughout this document.

coroutine type
--------------

A `coroutine function` is any function that contains any of the Coroutine
Keywords `co_await`, `co_yield`, or `co_return`.  A `coroutine type` is a
possible return type of one of these `coroutine functions`.  `Task` and
`Generator` are commonly referred to coroutine types.

coroutine
---------

By technical definition, a `coroutine` is a suspendable function. However,
programmers typically use `coroutine` to refer to an individual instance.
For example:

.. code-block:: c++

  std::vector<Task> Coros; // Task is a coroutine type.
  for (int i = 0; i < 3; i++)
    Coros.push_back(CoroTask()); // CoroTask is a coroutine function, which
                                 // would return a coroutine type 'Task'.

In practice, we typically say "`Coros` contains 3 coroutines" in the above
example, though this is not strictly correct.  More technically, this should
say "`Coros` contains 3 coroutine instances" or "Coros contains 3 coroutine
objects."

In this document, we follow the common practice of using `coroutine` to refer
to an individual `coroutine instance`, since the terms `coroutine instance` and
`coroutine object` aren't sufficiently defined in this case.

coroutine frame
---------------

The C++ Standard uses `coroutine state` to describe the allocated storage. In
the compiler, we use `coroutine frame` to describe the generated data structure
that contains the necessary information.

The structure of coroutine frames
=================================

The structure of coroutine frames is defined as:

.. code-block:: c++

  struct {
    void (*__r)(); // function pointer to the `resume` function
    void (*__d)(); // function pointer to the `destroy` function
    promise_type; // the corresponding `promise_type`
    ... // Any other needed information
  }

In the debugger, the function's name is obtainable from the address of the
function. And the name of `resume` function is equal to the name of the
coroutine function. So the name of the coroutine is obtainable once the
address of the coroutine is known.

Print promise_type
==================

Every coroutine has a `promise_type`, which defines the behavior
for the corresponding coroutine. In other words, if two coroutines have the
same `promise_type`, they should behave in the same way.
To print a `promise_type` in a debugger when stopped at a breakpoint inside a
coroutine, printing the `promise_type` can be done by:

.. parsed-literal::

  print __promise

It is also possible to print the `promise_type` of a coroutine from the address
of the coroutine frame. For example, if the address of a coroutine frame is
0x416eb0, and the type of the `promise_type` is `task::promise_type`, printing
the `promise_type` can be done by:

.. parsed-literal::

  print (task::promise_type)*(0x416eb0+0x10)

This is possible because the `promise_type` is guaranteed by the ABI to be at a
16 bit offset from the coroutine frame.

Note that there is also an ABI independent method:

.. parsed-literal::

  print std::coroutine_handle<task::promise_type>::from_address((void*)0x416eb0).promise()

The functions `from_address(void*)` and `promise()` are often small enough to
be removed during optimization, so this method may not be possible.

Print coroutine frames
======================

LLVM generates the debug information for the coroutine frame in the LLVM middle
end, which permits printing of the coroutine frame in the debugger. Much like
the `promise_type`, when stopped at a breakpoint inside a coroutine we can
print the coroutine frame by:

.. parsed-literal::

  print __coro_frame


Just as printing the `promise_type` is possible from the coroutine address,
printing the details of the coroutine frame from an address is also possible:

::

  (gdb) # Get the address of coroutine frame
  (gdb) print/x *0x418eb0
  $1 = 0x4019e0
  (gdb) # Get the linkage name for the coroutine
  (gdb) x 0x4019e0
  0x4019e0 <_ZL9coro_taski>:  0xe5894855
  (gdb) # Turn off the demangler temporarily to avoid the debugger misunderstanding the name.
  (gdb) set demangle-style none
  (gdb) # The coroutine frame type is 'linkage_name.coro_frame_ty'
  (gdb) print  ('_ZL9coro_taski.coro_frame_ty')*(0x418eb0)
  $2 = {__resume_fn = 0x4019e0 <coro_task(int)>, __destroy_fn = 0x402000 <coro_task(int)>, __promise = {...}, ...}

The above is possible because:

(1) The name of the debug type of the coroutine frame is the `linkage_name`,
plus the `.coro_frame_ty` suffix because each coroutine function shares the
same coroutine type.

(2) The coroutine function name is accessible from the address of the coroutine
frame.

The above commands can be simplified by placing them in debug scripts.

Examples to print coroutine frames
----------------------------------

The print examples below use the following definition:

.. code-block:: c++

  #include <coroutine>
  #include <iostream>

  struct task{
    struct promise_type {
      task get_return_object() { return std::coroutine_handle<promise_type>::from_promise(*this); }
      std::suspend_always initial_suspend() { return {}; }
      std::suspend_always final_suspend() noexcept { return {}; }
      void return_void() noexcept {}
      void unhandled_exception() noexcept {}

      int count = 0;
    };

    void resume() noexcept {
      handle.resume();
    }

    task(std::coroutine_handle<promise_type> hdl) : handle(hdl) {}
    ~task() {
      if (handle)
        handle.destroy();
    }

    std::coroutine_handle<> handle;
  };

  class await_counter : public std::suspend_always {
    public:
      template<class PromiseType>
      void await_suspend(std::coroutine_handle<PromiseType> handle) noexcept {
          handle.promise().count++;
      }
  };

  static task coro_task(int v) {
    int a = v;
    co_await await_counter{};
    a++;
    std::cout << a << "\n";
    a++;
    std::cout << a << "\n";
    a++;
    std::cout << a << "\n";
    co_await await_counter{};
    a++;
    std::cout << a << "\n";
    a++;
    std::cout << a << "\n";
  }

  int main() {
    task t = coro_task(43);
    t.resume();
    t.resume();
    t.resume();
    return 0;
  }

In debug mode (`O0` + `g`), the printing result would be:

.. parsed-literal::

  {__resume_fn = 0x4019e0 <coro_task(int)>, __destroy_fn = 0x402000 <coro_task(int)>, __promise = {count = 1}, v = 43, a = 45, __coro_index = 1 '\001', struct_std__suspend_always_0 = {__int_8 = 0 '\000'},
    class_await_counter_1 = {__int_8 = 0 '\000'}, class_await_counter_2 = {__int_8 = 0 '\000'}, struct_std__suspend_always_3 = {__int_8 = 0 '\000'}}

In the above, the values of `v` and `a` are clearly expressed, as are the
temporary values for `await_counter` (`class_await_counter_1` and
`class_await_counter_2`) and `std::suspend_always` (
`struct_std__suspend_always_0` and `struct_std__suspend_always_3`). The index
of the current suspension point of the coroutine is emitted as `__coro_index`.
In the above example, the `__coro_index` value of `1` means the coroutine
stopped at the second suspend point (Note that `__coro_index` is zero indexed)
which is the first `co_await await_counter{};` in `coro_task`. Note that the
first initial suspend point is the compiler generated
`co_await promise_type::initial_suspend()`.

However, when optimizations are enabled, the printed result changes drastically:

.. parsed-literal::

  {__resume_fn = 0x401280 <coro_task(int)>, __destroy_fn = 0x401390 <coro_task(int)>, __promise = {count = 1}, __int_32_0 = 43, __coro_index = 1 '\001'}

Unused values are optimized out, as well as the name of the local variable `a`.
The only information remained is the value of a 32 bit integer. In this simple
case, it seems to be pretty clear that `__int_32_0` represents `a`. However, it
is not true.

An important note with optimization is that the value of a variable may not
properly express the intended value in the source code.  For example:

.. code-block:: c++

  static task coro_task(int v) {
    int a = v;
    co_await await_counter{};
    a++; // __int_32_0 is 43 here
    std::cout << a << "\n";
    a++; // __int_32_0 is still 43 here
    std::cout << a << "\n";
    a++; // __int_32_0 is still 43 here!
    std::cout << a << "\n";
    co_await await_counter{};
    a++; // __int_32_0 is still 43 here!!
    std::cout << a << "\n";
    a++; // Why is __int_32_0 still 43 here?
    std::cout << a << "\n";
  }

When debugging step-by-step, the value of `__int_32_0` seemingly does not
change, despite being frequently incremented, and instead is always `43`.
While this might be surprising, this is a result of the optimizer recognizing
that it can eliminate most of the load/store operations. The above code gets
optimized to the equivalent of:

.. code-block:: c++

  static task coro_task(int v) {
    store v to __int_32_0 in the frame
    co_await await_counter{};
    a = load __int_32_0
    std::cout << a+1 << "\n";
    std::cout << a+2 << "\n";
    std::cout << a+3 << "\n";
    co_await await_counter{};
    a = load __int_32_0
    std::cout << a+4 << "\n";
    std::cout << a+5 << "\n";
  }

It should now be obvious why the value of `__int_32_0` remains unchanged
throughout the function. It is important to recognize that `__int_32_0`
does not directly correspond to `a`, but is instead a variable generated
to assist the compiler in code generation. The variables in an optimized
coroutine frame should not be thought of as directly representing the
variables in the C++ source.

Get the suspended points
========================

An important requirement for debugging coroutines is to understand suspended
points, which are where the coroutine is currently suspended and awaiting.

For simple cases like the above, inspecting the value of the `__coro_index`
variable in the coroutine frame works well.

However, it is not quite so simple in really complex situations. In these
cases, it is necessary to use the coroutine libraries to insert the
line-number.

For example:

.. code-block:: c++

  // For all the promise_type we want:
  class promise_type {
    ...
  +  unsigned line_number = 0xffffffff;
  };

  #include <source_location>

  // For all the awaiter types we need:
  class awaiter {
    ...
    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle,
                       std::source_location sl = std::source_location::current()) {
          ...
          handle.promise().line_number = sl.line();
    }
  };

In this case, we use `std::source_location` to store the line number of the
await inside the `promise_type`.  Since we can locate the coroutine function
from the address of the coroutine, we can identify suspended points this way
as well.

The downside here is that this comes at the price of additional runtime cost.
This is consistent with the C++ philosophy of "Pay for what you use".

Get the asynchronous stack
==========================

Another important requirement to debug a coroutine is to print the asynchronous
stack to identify the asynchronous caller of the coroutine.  As many
implementations of coroutine types store `std::coroutine_handle<> continuation`
in the promise type, identifying the caller should be trivial.  The
`continuation` is typically the awaiting coroutine for the current coroutine.
That is, the asynchronous parent.

Since the `promise_type` is obtainable from the address of a coroutine and
contains the corresponding continuation (which itself is a coroutine with a
`promise_type`), it should be trivial to print the entire asynchronous stack.

This logic should be quite easily captured in a debugger script.

Examples to print asynchronous stack
------------------------------------

Here is an example to print the asynchronous stack for the normal task implementation.

.. code-block:: c++

  // debugging-example.cpp
  #include <coroutine>
  #include <iostream>
  #include <utility>

  struct task {
    struct promise_type {
      task get_return_object();
      std::suspend_always initial_suspend() { return {}; }

      void unhandled_exception() noexcept {}

      struct FinalSuspend {
        std::coroutine_handle<> continuation;
        auto await_ready() noexcept { return false; }
        auto await_suspend(std::coroutine_handle<> handle) noexcept {
          return continuation;
        }
        void await_resume() noexcept {}
      };
      FinalSuspend final_suspend() noexcept { return {continuation}; }

      void return_value(int res) { result = res; }

      std::coroutine_handle<> continuation = std::noop_coroutine();
      int result = 0;
    };

    task(std::coroutine_handle<promise_type> handle) : handle(handle) {}
    ~task() {
      if (handle)
        handle.destroy();
    }

    auto operator co_await() {
      struct Awaiter {
        std::coroutine_handle<promise_type> handle;
        auto await_ready() { return false; }
        auto await_suspend(std::coroutine_handle<> continuation) {
          handle.promise().continuation = continuation;
          return handle;
        }
        int await_resume() {
          int ret = handle.promise().result;
          handle.destroy();
          return ret;
        }
      };
      return Awaiter{std::exchange(handle, nullptr)};
    }

    int syncStart() {
      handle.resume();
      return handle.promise().result;
    }

  private:
    std::coroutine_handle<promise_type> handle;
  };

  task task::promise_type::get_return_object() {
    return std::coroutine_handle<promise_type>::from_promise(*this);
  }

  namespace detail {
  template <int N>
  task chain_fn() {
    co_return N + co_await chain_fn<N - 1>();
  }

  template <>
  task chain_fn<0>() {
    // This is the default breakpoint.
    __builtin_debugtrap();
    co_return 0;
  }
  }  // namespace detail

  task chain() {
    co_return co_await detail::chain_fn<30>();
  }

  int main() {
    std::cout << chain().syncStart() << "\n";
    return 0;
  }

In the example, the ``task`` coroutine holds a ``continuation`` field,
which would be resumed once the ``task`` completes.
In another word, the ``continuation`` is the asynchronous caller for the ``task``.
Just like the normal function returns to its caller when the function completes.

So we can use the ``continuation`` field to construct the asynchronous stack:

.. code-block:: python

  # debugging-helper.py
  import gdb
  from gdb.FrameDecorator import FrameDecorator

  class SymValueWrapper():
      def __init__(self, symbol, value):
          self.sym = symbol
          self.val = value

      def __str__(self):
          return str(self.sym) + " = " + str(self.val)

  def get_long_pointer_size():
      return gdb.lookup_type('long').pointer().sizeof

  def cast_addr2long_pointer(addr):
      return gdb.Value(addr).cast(gdb.lookup_type('long').pointer())

  def dereference(addr):
      return long(cast_addr2long_pointer(addr).dereference())

  class CoroutineFrame(object):
      def __init__(self, task_addr):
          self.frame_addr = task_addr
          self.resume_addr = task_addr
          self.destroy_addr = task_addr + get_long_pointer_size()
          self.promise_addr = task_addr + get_long_pointer_size() * 2
          # In the example, the continuation is the first field member of the promise_type.
          # So they have the same addresses.
          # If we want to generalize the scripts to other coroutine types, we need to be sure
          # the continuation field is the first member of promise_type.
          self.continuation_addr = self.promise_addr

      def next_task_addr(self):
          return dereference(self.continuation_addr)

  class CoroutineFrameDecorator(FrameDecorator):
      def __init__(self, coro_frame):
          super(CoroutineFrameDecorator, self).__init__(None)
          self.coro_frame = coro_frame
          self.resume_func = dereference(self.coro_frame.resume_addr)
          self.resume_func_block = gdb.block_for_pc(self.resume_func)
          if self.resume_func_block == None:
              raise Exception('Not stackless coroutine.')
          self.line_info = gdb.find_pc_line(self.resume_func)

      def address(self):
          return self.resume_func

      def filename(self):
          return self.line_info.symtab.filename

      def frame_args(self):
          return [SymValueWrapper("frame_addr", cast_addr2long_pointer(self.coro_frame.frame_addr)),
                  SymValueWrapper("promise_addr", cast_addr2long_pointer(self.coro_frame.promise_addr)),
                  SymValueWrapper("continuation_addr", cast_addr2long_pointer(self.coro_frame.continuation_addr))
                  ]

      def function(self):
          return self.resume_func_block.function.print_name

      def line(self):
          return self.line_info.line

  class StripDecorator(FrameDecorator):
      def __init__(self, frame):
          super(StripDecorator, self).__init__(frame)
          self.frame = frame
          f = frame.function()
          self.function_name = f

      def __str__(self, shift = 2):
          addr = "" if self.address() == None else '%#x' % self.address() + " in "
          location = "" if self.filename() == None else " at " + self.filename() + ":" + str(self.line())
          return addr + self.function() + " " + str([str(args) for args in self.frame_args()]) + location

  class CoroutineFilter:
      def create_coroutine_frames(self, task_addr):
          frames = []
          while task_addr != 0:
              coro_frame = CoroutineFrame(task_addr)
              frames.append(CoroutineFrameDecorator(coro_frame))
              task_addr = coro_frame.next_task_addr()
          return frames

  class AsyncStack(gdb.Command):
      def __init__(self):
          super(AsyncStack, self).__init__("async-bt", gdb.COMMAND_USER)

      def invoke(self, arg, from_tty):
          coroutine_filter = CoroutineFilter()
          argv = gdb.string_to_argv(arg)
          if len(argv) == 0:
              try:
                  task = gdb.parse_and_eval('__coro_frame')
                  task = int(str(task.address), 16)
              except Exception:
                  print ("Can't find __coro_frame in current context.\n" +
                        "Please use `async-bt` in stackless coroutine context.")
                  return
          elif len(argv) != 1:
              print("usage: async-bt <pointer to task>")
              return
          else:
              task = int(argv[0], 16)

          frames = coroutine_filter.create_coroutine_frames(task)
          i = 0
          for f in frames:
              print '#'+ str(i), str(StripDecorator(f))
              i += 1
          return

  AsyncStack()

  class ShowCoroFrame(gdb.Command):
      def __init__(self):
          super(ShowCoroFrame, self).__init__("show-coro-frame", gdb.COMMAND_USER)

      def invoke(self, arg, from_tty):
          argv = gdb.string_to_argv(arg)
          if len(argv) != 1:
              print("usage: show-coro-frame <address of coroutine frame>")
              return

          addr = int(argv[0], 16)
          block = gdb.block_for_pc(long(cast_addr2long_pointer(addr).dereference()))
          if block == None:
              print "block " + str(addr) + "  is none."
              return

          # Disable demangling since gdb will treat names starting with `_Z`(The marker for Itanium ABI) specially.
          gdb.execute("set demangle-style none")

          coro_frame_type = gdb.lookup_type(block.function.linkage_name + ".coro_frame_ty")
          coro_frame_ptr_type = coro_frame_type.pointer()
          coro_frame = gdb.Value(addr).cast(coro_frame_ptr_type).dereference()

          gdb.execute("set demangle-style auto")
          gdb.write(coro_frame.format_string(pretty_structs = True))

  ShowCoroFrame()

Then let's run:

.. code-block:: text

  $ clang++ -std=c++20 -g debugging-example.cpp -o debugging-example
  $ gdb ./debugging-example
  (gdb) # We've already set the breakpoint.
  (gdb) r
  Program received signal SIGTRAP, Trace/breakpoint trap.
  detail::chain_fn<0> () at debugging-example2.cpp:73
  73	  co_return 0;
  (gdb) # Executes the debugging scripts
  (gdb) source debugging-helper.py
  (gdb) # Print the asynchronous stack
  (gdb) async-bt
  #0 0x401c40 in detail::chain_fn<0>() ['frame_addr = 0x441860', 'promise_addr = 0x441870', 'continuation_addr = 0x441870'] at debugging-example.cpp:71
  #1 0x4022d0 in detail::chain_fn<1>() ['frame_addr = 0x441810', 'promise_addr = 0x441820', 'continuation_addr = 0x441820'] at debugging-example.cpp:66
  #2 0x403060 in detail::chain_fn<2>() ['frame_addr = 0x4417c0', 'promise_addr = 0x4417d0', 'continuation_addr = 0x4417d0'] at debugging-example.cpp:66
  #3 0x403df0 in detail::chain_fn<3>() ['frame_addr = 0x441770', 'promise_addr = 0x441780', 'continuation_addr = 0x441780'] at debugging-example.cpp:66
  #4 0x404b80 in detail::chain_fn<4>() ['frame_addr = 0x441720', 'promise_addr = 0x441730', 'continuation_addr = 0x441730'] at debugging-example.cpp:66
  #5 0x405910 in detail::chain_fn<5>() ['frame_addr = 0x4416d0', 'promise_addr = 0x4416e0', 'continuation_addr = 0x4416e0'] at debugging-example.cpp:66
  #6 0x4066a0 in detail::chain_fn<6>() ['frame_addr = 0x441680', 'promise_addr = 0x441690', 'continuation_addr = 0x441690'] at debugging-example.cpp:66
  #7 0x407430 in detail::chain_fn<7>() ['frame_addr = 0x441630', 'promise_addr = 0x441640', 'continuation_addr = 0x441640'] at debugging-example.cpp:66
  #8 0x4081c0 in detail::chain_fn<8>() ['frame_addr = 0x4415e0', 'promise_addr = 0x4415f0', 'continuation_addr = 0x4415f0'] at debugging-example.cpp:66
  #9 0x408f50 in detail::chain_fn<9>() ['frame_addr = 0x441590', 'promise_addr = 0x4415a0', 'continuation_addr = 0x4415a0'] at debugging-example.cpp:66
  #10 0x409ce0 in detail::chain_fn<10>() ['frame_addr = 0x441540', 'promise_addr = 0x441550', 'continuation_addr = 0x441550'] at debugging-example.cpp:66
  #11 0x40aa70 in detail::chain_fn<11>() ['frame_addr = 0x4414f0', 'promise_addr = 0x441500', 'continuation_addr = 0x441500'] at debugging-example.cpp:66
  #12 0x40b800 in detail::chain_fn<12>() ['frame_addr = 0x4414a0', 'promise_addr = 0x4414b0', 'continuation_addr = 0x4414b0'] at debugging-example.cpp:66
  #13 0x40c590 in detail::chain_fn<13>() ['frame_addr = 0x441450', 'promise_addr = 0x441460', 'continuation_addr = 0x441460'] at debugging-example.cpp:66
  #14 0x40d320 in detail::chain_fn<14>() ['frame_addr = 0x441400', 'promise_addr = 0x441410', 'continuation_addr = 0x441410'] at debugging-example.cpp:66
  #15 0x40e0b0 in detail::chain_fn<15>() ['frame_addr = 0x4413b0', 'promise_addr = 0x4413c0', 'continuation_addr = 0x4413c0'] at debugging-example.cpp:66
  #16 0x40ee40 in detail::chain_fn<16>() ['frame_addr = 0x441360', 'promise_addr = 0x441370', 'continuation_addr = 0x441370'] at debugging-example.cpp:66
  #17 0x40fbd0 in detail::chain_fn<17>() ['frame_addr = 0x441310', 'promise_addr = 0x441320', 'continuation_addr = 0x441320'] at debugging-example.cpp:66
  #18 0x410960 in detail::chain_fn<18>() ['frame_addr = 0x4412c0', 'promise_addr = 0x4412d0', 'continuation_addr = 0x4412d0'] at debugging-example.cpp:66
  #19 0x4116f0 in detail::chain_fn<19>() ['frame_addr = 0x441270', 'promise_addr = 0x441280', 'continuation_addr = 0x441280'] at debugging-example.cpp:66
  #20 0x412480 in detail::chain_fn<20>() ['frame_addr = 0x441220', 'promise_addr = 0x441230', 'continuation_addr = 0x441230'] at debugging-example.cpp:66
  #21 0x413210 in detail::chain_fn<21>() ['frame_addr = 0x4411d0', 'promise_addr = 0x4411e0', 'continuation_addr = 0x4411e0'] at debugging-example.cpp:66
  #22 0x413fa0 in detail::chain_fn<22>() ['frame_addr = 0x441180', 'promise_addr = 0x441190', 'continuation_addr = 0x441190'] at debugging-example.cpp:66
  #23 0x414d30 in detail::chain_fn<23>() ['frame_addr = 0x441130', 'promise_addr = 0x441140', 'continuation_addr = 0x441140'] at debugging-example.cpp:66
  #24 0x415ac0 in detail::chain_fn<24>() ['frame_addr = 0x4410e0', 'promise_addr = 0x4410f0', 'continuation_addr = 0x4410f0'] at debugging-example.cpp:66
  #25 0x416850 in detail::chain_fn<25>() ['frame_addr = 0x441090', 'promise_addr = 0x4410a0', 'continuation_addr = 0x4410a0'] at debugging-example.cpp:66
  #26 0x4175e0 in detail::chain_fn<26>() ['frame_addr = 0x441040', 'promise_addr = 0x441050', 'continuation_addr = 0x441050'] at debugging-example.cpp:66
  #27 0x418370 in detail::chain_fn<27>() ['frame_addr = 0x440ff0', 'promise_addr = 0x441000', 'continuation_addr = 0x441000'] at debugging-example.cpp:66
  #28 0x419100 in detail::chain_fn<28>() ['frame_addr = 0x440fa0', 'promise_addr = 0x440fb0', 'continuation_addr = 0x440fb0'] at debugging-example.cpp:66
  #29 0x419e90 in detail::chain_fn<29>() ['frame_addr = 0x440f50', 'promise_addr = 0x440f60', 'continuation_addr = 0x440f60'] at debugging-example.cpp:66
  #30 0x41ac20 in detail::chain_fn<30>() ['frame_addr = 0x440f00', 'promise_addr = 0x440f10', 'continuation_addr = 0x440f10'] at debugging-example.cpp:66
  #31 0x41b9b0 in chain() ['frame_addr = 0x440eb0', 'promise_addr = 0x440ec0', 'continuation_addr = 0x440ec0'] at debugging-example.cpp:77

Now we get the complete asynchronous stack!
It is also possible to print other asynchronous stack which doesn't live in the top of the stack.
We can make it by passing the address of the corresponding coroutine frame to ``async-bt`` command.

By the debugging scripts, we can print any coroutine frame too as long as we know the address.
For example, we can print the coroutine frame for ``detail::chain_fn<18>()`` in the above example.
From the log record, we know the address of the coroutine frame is ``0x4412c0`` in the run. Then we can:

.. code-block:: text

  (gdb) show-coro-frame 0x4412c0
  {
    __resume_fn = 0x410960 <detail::chain_fn<18>()>,
    __destroy_fn = 0x410d60 <detail::chain_fn<18>()>,
    __promise = {
      continuation = {
        _M_fr_ptr = 0x441270
      },
      result = 0
    },
    struct_Awaiter_0 = {
      struct_std____n4861__coroutine_handle_0 = {
        struct_std____n4861__coroutine_handle = {
          PointerType = 0x441310
        }
      }
    },
    struct_task_1 = {
      struct_std____n4861__coroutine_handle_0 = {
        struct_std____n4861__coroutine_handle = {
          PointerType = 0x0
        }
      }
    },
    struct_task__promise_type__FinalSuspend_2 = {
      struct_std____n4861__coroutine_handle = {
        PointerType = 0x0
      }
    },
    __coro_index = 1 '\001',
    struct_std____n4861__suspend_always_3 = {
      __int_8 = 0 '\000'
    }


Get the living coroutines
=========================

Another useful task when debugging coroutines is to enumerate the list of
living coroutines, which is often done with threads.  While technically
possible, this task is not recommended in production code as it is costly at
runtime. One such solution is to store the list of currently running coroutines
in a collection:

.. code-block:: c++

  inline std::unordered_set<void*> lived_coroutines;
  // For all promise_type we want to record
  class promise_type {
  public:
      promise_type() {
          // Note to avoid data races
          lived_coroutines.insert(std::coroutine_handle<promise_type>::from_promise(*this).address());
      }
      ~promise_type() {
          // Note to avoid data races
          lived_coroutines.erase(std::coroutine_handle<promise_type>::from_promise(*this).address());
      }
  };

In the above code snippet, we save the address of every lived coroutine in the
`lived_coroutines` `unordered_set`. As before, once we know the address of the
coroutine we can derive the function, `promise_type`, and other members of the
frame. Thus, we could print the list of lived coroutines from that collection.

Please note that the above is expensive from a storage perspective, and requires
some level of locking (not pictured) on the collection to prevent data races.
