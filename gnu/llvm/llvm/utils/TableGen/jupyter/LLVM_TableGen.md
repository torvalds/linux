# LLVM TableGen Kernel

This notebook is running `llvm-tblgen`.


```tablegen
%reset
// This is some tablegen
class Foo {}
```

    ------------- Classes -----------------
    class Foo {
    }
    ------------- Defs -----------------


Errors printed to stderr are shown.


```tablegen
%reset
This is not tablegen.
```

    <stdin>:1:1: error: Unexpected token at top level
    This is not tablegen.
    ^


Add some classes to get some output.


```tablegen
%reset
class Stuff {}
def thing : Stuff {}
```

    ------------- Classes -----------------
    class Stuff {
    }
    ------------- Defs -----------------
    def thing {	// Stuff
    }


By default cells are connected. Meaning that we cache the code and magic directives from the previously run cells.

This means that the next cell still sees the `Stuff` class.


```tablegen
def other_thing : Stuff {}
```

    ------------- Classes -----------------
    class Stuff {
    }
    ------------- Defs -----------------
    def other_thing {	// Stuff
    }
    def thing {	// Stuff
    }


You can use the magic `%reset` to clear this cache and start fresh.


```tablegen
%reset
def other_thing : Stuff {}
```

    <stdin>:1:19: error: Couldn't find class 'Stuff'
    def other_thing : Stuff {}
                      ^


You can also configure the default reset behaviour using the `%config` magic.


```tablegen
%config cellreset on
class Thing {}
```

    ------------- Classes -----------------
    class Thing {
    }
    ------------- Defs -----------------



```tablegen
// The cache is reset here so this is an error.
def AThing: Thing {}
```

    <stdin>:2:13: error: Couldn't find class 'Thing'
    def AThing: Thing {}
                ^


The default value is `off`, meaning cells are connected. If you want to override the default for one cell only, use the `%reset` or `%noreset` magic. These always override the default.


```tablegen
class Thing {}
```

    ------------- Classes -----------------
    class Thing {
    }
    ------------- Defs -----------------



```tablegen
%noreset
// This works because of the noreset above.
def AThing: Thing {}
```

    ------------- Classes -----------------
    class Thing {
    }
    ------------- Defs -----------------
    def AThing {	// Thing
    }



```tablegen
// This does not because we're not changing the default.
def AnotherThing: Thing {}
```

    <stdin>:2:19: error: Couldn't find class 'Thing'
    def AnotherThing: Thing {}
                      ^



```tablegen
%config cellreset off
%reset
// Here we have an empty cache and default reset behaviour.
```

    ------------- Classes -----------------
    ------------- Defs -----------------


It is not valid to have `%reset` and `%noreset` in the same cell.


```tablegen
%reset
%noreset
```

    %reset and %noreset in the same cell is not allowed. Use only one, or neither.

Consider setting `cellreset` to the majority usecase for your notebook. For example a tutorial building a large example across many cells will likely want it `off`. One with many standalone examples, `on`.

There is a "magic" directive `%args` that you can use to send command line arguments to `llvm-tblgen`.

For example, here we have some code that shows a warning.


```tablegen
%reset
class Thing <int A, int B> {
    int num = A;
}
```

    <stdin>:1:25: warning: unused template argument: Thing:B
    class Thing <int A, int B> {
                            ^


We can pass an argument to ignore that warning.


```tablegen
%args --no-warn-on-unused-template-args
```

    ------------- Classes -----------------
    class Thing<int Thing:A = ?, int Thing:B = ?> {
      int num = Thing:A;
    }
    ------------- Defs -----------------


If you have a run of cells without a `%reset`, the most recent `%args` is used.


```tablegen
// This passes --no-warn-on-unused-template-args
```

    ------------- Classes -----------------
    class Thing<int Thing:A = ?, int Thing:B = ?> {
      int num = Thing:A;
    }
    ------------- Defs -----------------



```tablegen
%args
// Now we're not passing the argument so the warning comes back.
```

    <stdin>:1:25: warning: unused template argument: Thing:B
    class Thing <int A, int B> {
                            ^


If there are many `%args` in a cell, the last one is used.


```tablegen
%reset
%args --no-warn-on-unused-template-args
%args
class Thing <int A, int B> {}
```

    <stdin>:1:18: warning: unused template argument: Thing:A
    class Thing <int A, int B> {}
                     ^
    <stdin>:1:25: warning: unused template argument: Thing:B
    class Thing <int A, int B> {}
                            ^

