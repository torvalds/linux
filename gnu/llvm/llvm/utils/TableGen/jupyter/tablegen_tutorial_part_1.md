## Introduction to TableGen Part 1: Classes, Defs, Basic Types and Let

**Note:** The content in this notebook is adapted from [this document](https://llvm.org/docs/TableGen/index.html). Refer to it if you want more details.

This tutorial will cover:
* Classes
* Defs
* Basic types
* `let` in various forms
* Class template arguments

## What is TableGen?

TableGen is a language used in LLVM to automate the generation of certain types of code. Usually repetitive code that has a common structure. TableGen is used to generate "records" that are then processed by a "backend" into domain specific code.

The compiler for TableGen is the binary `llvm-tblgen`. This contains the logic to convert TableGen source into records that can then be passed to a TableGen backend.

TableGen allows you to define Classes and Defs (which are instances of classes) but it doesn't encode what to do with that structure. That's what the backend does. The backend converts this structure into something useful, for example C++ code.

These backends are included in the `llvm-tblgen` binary and you can choose which one to run using a command line option. If you don't choose a backend you get a dump of the structure, and that is what this notebook will be showing.

This tutorial will focus on the language itself only. The only thing you need to know now is that in addition to `llvm-tblgen` you will see other `*-tblgen` like `clang-tblgen`. The difference between them is the backends they include.

The default output from `llvm-tblgen` looks like this:


```tablegen
%config cellreset on

// Empty source file
```

    ------------- Classes -----------------
    ------------- Defs -----------------


**Note:** `%config` is not a TableGen command but a "magic" command to the Jupyter kernel for this notebook. By default new cells include the content of previously run cells, but for this notebook we mostly want each to be isolated. On occasion we will use the `%noreset` magic to override this.

No source means no classes and no defs. Let's add a class.

## Classes


```tablegen
class C {}
```

    ------------- Classes -----------------
    class C {
    }
    ------------- Defs -----------------


Followed by a def (definition).


```tablegen
%noreset

def X: C;
```

    ------------- Classes -----------------
    class C {
    }
    ------------- Defs -----------------
    def X {	// C
    }


`def` creates an instance of a class. Typically, the main loop of a TableGen backend will look for all defs that are instances of a certain class.

For example if I am generating register information I would look for all defs that are instances of `RegisterInfo` in the example below.


```tablegen
class RegisterInfo {}
def X0: RegisterInfo {}
def X1: RegisterInfo {}
```

    ------------- Classes -----------------
    class RegisterInfo {
    }
    ------------- Defs -----------------
    def X0 {	// RegisterInfo
    }
    def X1 {	// RegisterInfo
    }


## Inheritance

Like many other languages with classes, a class in TableGen can inherit properties of another class.


```tablegen
class C {}
class D : C {}
```

    ------------- Classes -----------------
    class C {
    }
    class D {	// C
    }
    ------------- Defs -----------------


Inheritance is done by putting the class you want to inherit from after `:`, before the opening `{`.

You'll know that `D` inherits from `C` by the `// C` comment on the `class D {` line in the output.

Not very interesting though, what are we actually inheriting? The members of the parent class.


```tablegen
class C {
    int a;
}
class D : C {}
```

    ------------- Classes -----------------
    class C {
      int a = ?;
    }
    class D {	// C
      int a = ?;
    }
    ------------- Defs -----------------


Note that `D` now has the `a` member which was defined in the class `C`.

You can inherit from multiple classes. In that case the order that that happens in matches the order you write the class names after the `:`.


```tablegen
class C {
    int a = 1;
}
class D {
    int a = 2;
}
class E : C, D {}
```

    ------------- Classes -----------------
    class C {
      int a = 1;
    }
    class D {
      int a = 2;
    }
    class E {	// C D
      int a = 2;
    }
    ------------- Defs -----------------


Class `E` first inherits from class `C`. This gives `E` a member `a` with value `1`. Then it inherits from class `D` which also has a member `a` but with a value of `2`. Meaning the final value of `E`'s `a` is `2`.

When a member has the same name this is handled on a "last one in wins" basis. Assuming the types match.


```tablegen
class C {
    string a = "";
}
class D {
    int a = 2;
}
class E : C, D {}
```

    <stdin>:7:14: error: New definition of 'a' of type 'int' is incompatible with previous definition of type 'string'
    class E : C, D {}
                 ^


When they don't match, we get an error. Luckily for us, we're about to learn all about types.

## Types

TableGen is statically typed with error checking to prevent you from assigning things with mismatched types.


```tablegen
class C {
    int a;
    bit b = 0;
    string s = "Hello";
}
```

    ------------- Classes -----------------
    class C {
      int a = ?;
      bit b = 0;
      string s = "Hello";
    }
    ------------- Defs -----------------


Here we've created a class C with integer, bit (1 or 0) and string members. See [here](https://llvm.org/docs/TableGen/ProgRef.html#types) for a full list of types.

Note that you do not have to give a member a default value, it can be left uninitialised.


```tablegen
%noreset

def X: C {}
```

    ------------- Classes -----------------
    class C {
      int a = ?;
      bit b = 0;
      string s = "Hello";
    }
    ------------- Defs -----------------
    def X {	// C
      int a = ?;
      bit b = 0;
      string s = "Hello";
    }


When you make an instance of a class using `def`, that instance gets all the members of the class. Their values will be as set in the class, unless otherwise overridden.

In the case of `a` it also keeps the undefined value. Any backend using that definition would have to check for that case.


```tablegen
%noreset

def Y {
    int a = "abc"
}
```

    <stdin>:10:13: error: Field 'a' of type 'int' is incompatible with value '"abc"' of type 'string'
        int a = "abc"
                ^
    <stdin>:11:1: error: expected ';' after declaration
    }
    ^


Here we see the type checking in action. Member `a` has type `int` so we cannot assign a `string` to it.

## Let

If we want to override those member values we can use `let` ([documented here](https://llvm.org/docs/TableGen/ProgRef.html#let-override-fields-in-classes-or-records)). This can be done in a couple of ways. The first is where you mark the scope of the `let` using `in {}`.

`let <name>=<value> in {`

The code below says that within the `{}` after the `let`, all `a` should have the value 5.


```tablegen
class C {
    int a = 9;
}
let a=5 in {
    def X: C {}
}
```

    ------------- Classes -----------------
    class C {
      int a = 9;
    }
    ------------- Defs -----------------
    def X {	// C
      int a = 5;
    }


For multiple names, separate them with a comma.


```tablegen
class C {
    int a;
    int b;
}
let a=5, b=6 in {
    def X: C {}
}
```

    ------------- Classes -----------------
    class C {
      int a = ?;
      int b = ?;
    }
    ------------- Defs -----------------
    def X {	// C
      int a = 5;
      int b = 6;
    }


You can also use `let` within a `def`. This means the scope of the `let` is the same as the scope of the `def` (the def's `{...}`).


```tablegen
class C {
    int a = 9;
}
def X: C {
    let a=5;
}
def Y: C {}
```

    ------------- Classes -----------------
    class C {
      int a = 9;
    }
    ------------- Defs -----------------
    def X {	// C
      int a = 5;
    }
    def Y {	// C
      int a = 9;
    }


Note that `Y` has `a` as `9` because the `let` was only applied to `X`.

It is an error to try to `let` a name that hasn't been defined or to give it a value of the incorrect type.


```tablegen
class C {
    int a = 9;
}
def X: C {
    let a="Hello";
}
```

    <stdin>:5:9: error: Field 'a' of type 'int' is incompatible with value '"Hello"' of type 'string'
        let a="Hello";
            ^


Above, the member `a` was defined but with a type of `int`. We therefore cannot `let` it have a value of type `string`.


```tablegen
class C {
    int a = 9;
}
def X: C {
    let b=5;
}
```

    <stdin>:5:11: error: Value 'b' unknown!
        let b=5;
              ^


Above, class `C` only has one member, `a`. Therefore we get an error trying to override the value of `b` which doesn't exist.

If you have multiple let, the outer scope is applied first then on down to the narrowest scope.


```tablegen
class Base {
    int var=4;
}
let var=5 in {
    def X: Base {}
    let var=6 in {
        def Y: Base {}
    }
    def Z: Base { let var=7; }
}
```

    ------------- Classes -----------------
    class Base {
      int var = 4;
    }
    ------------- Defs -----------------
    def X {	// Base
      int var = 5;
    }
    def Y {	// Base
      int var = 6;
    }
    def Z {	// Base
      int var = 7;
    }


The first `let` is at what we call the "top level". That means the outer most scope in terms of the source code. A bit like a global variable in a C file.

This is applied first and changes `var` from `4` to `5` for all classes within that `let` (`4` came from the definition of `Base`).

def `X` is within the global `let`, therefore `var` is `5` within `X`.

Then we have a `let` inside the global `let`. This one changes `var` from `5` to `6`. The scope of the `let` only contains the def `Y` therefore within `Y`, `var` is `6`.

Finally def `Z` is within the global `let`, so `var` starts as `5`. `Z` has an inner `let` that changes `var` to `7`.

That example is quite complex just to demonstrate the feature. Let's look at something more practical.


```tablegen
class Register {
    int size=4;
}
let size=8 in {
    def X0: Register {}
    // Repeats 30 times for X1...X31
}
def W0: Register {}
// Repeats 30 times for W1...W31
```

    ------------- Classes -----------------
    class Register {
      int size = 4;
    }
    ------------- Defs -----------------
    def W0 {	// Register
      int size = 4;
    }
    def X0 {	// Register
      int size = 8;
    }


(for anyone curious that's AArch64's register naming)

The use case here is that we are describing registers. Some are 32 bits wide and some are 64 bits wide.

We start by setting a default value of `size` which is 4 (4x8=32 bits) in the class `Register`. Then using a top level `let` we override that value and set it to 8 for all the 64 bit registers at once. So we don't need to do `size=8` over and over again.

## Classes As Class Members

In addition to the built in types, class members can be user defined classes.


```tablegen
class Inner {}
class Outer {
    Inner i;
}
```

    ------------- Classes -----------------
    class Inner {
    }
    class Outer {
      Inner i = ?;
    }
    ------------- Defs -----------------


Of course that raises the question, how do we construct an instance of `Inner` to use as the value?

We simply use a `def` like we have done before.


```tablegen
class Inner {}
def AnInner: Inner {}
class Outer {
    Inner i = AnInner;
}
def AnOuter: Outer {}
```

    ------------- Classes -----------------
    class Inner {
    }
    class Outer {
      Inner i = AnInner;
    }
    ------------- Defs -----------------
    def AnInner {	// Inner
    }
    def AnOuter {	// Outer
      Inner i = AnInner;
    }


## Class Template Arguments

Class template arguments are used to pass parameters to classes when you `def` them.


```tablegen
class C <int a, int b> {
    int c = a;
    int d = b;
}
def X: C<0, 1> {}
```

    ------------- Classes -----------------
    class C<int C:a = ?, int C:b = ?> {
      int c = C:a;
      int d = C:b;
    }
    ------------- Defs -----------------
    def X {	// C
      int c = 0;
      int d = 1;
    }


This means that to `def` a `C` we must now provide 2 arguments that have type `int` (type checking applies here as it does elsewhere).

This is going to look familiar if you have written C++. In C++ it might look like:
```
template<int a, int b>
class C {
    int c = a;
    int d = b;
};
C<0, 1> X;
```

If templates aren't your thing, another way to think of them is as parameters to the constructor of a class. 

For instance Python code might look like this:
```
class C(object):
    def __init__(self, a, b):
        self.c = a
        self.d = b

print(C(0, 1).c)
# prints "0"
```


```tablegen
class C <int a, int b> {
    int c = a;
    int d = b;
}
def X: C<0> {}
```

    <stdin>:5:8: error: value not specified for template argument 'C:b'
    def X: C<0> {}
           ^
    <stdin>:1:21: note: declared in 'C'
    class C <int a, int b> {
                        ^


When not enough arguments are provided, you get an error.

Below is what happens when one of those arguments is of the wrong type.


```tablegen
class C <int a, int b> {
    int c = a;
    int d = b;
}
def X: C<0, "hello"> {}
```

    <stdin>:5:8: error: Value specified for template argument 'C:b' is of type string; expected type int: "hello"
    def X: C<0, "hello"> {}
           ^


You can also provide default values for template arguments.


```tablegen
class C <int a=10> {
    int b = a;
}
def X: C<> {}
```

    ------------- Classes -----------------
    class C<int C:a = 10> {
      int b = C:a;
    }
    ------------- Defs -----------------
    def X {	// C
      int b = 10;
    }


Using class template arguments you can enforce a structure on the user of the classes. In our previous register example I could use this to require the the user pass a value for the size.

The code below makes the size argument mandatory but the alias optional.


```tablegen
class Register<int _size, string _alias=""> {
    int size = _size;
    string alias = _alias;
}
def X0: Register<8> {}
def X29: Register<8, "frame pointer"> {}
```

    ------------- Classes -----------------
    class Register<int Register:_size = ?, string Register:_alias = ""> {
      int size = Register:_size;
      string alias = Register:_alias;
    }
    ------------- Defs -----------------
    def X0 {	// Register
      int size = 8;
      string alias = "";
    }
    def X29 {	// Register
      int size = 8;
      string alias = "frame pointer";
    }


**Note:** You can't reuse the name between the template argument and the class member.
Here I have added `_` to the template argument but there's no required style.

For `X0` we don't pass an alias so we get the default of `""`, which would mean there is no alias.

For `X29` we've passed a value for the alias, which overrides the default value.

In C++, the equivalent would be:
```
// Constructor for class Register
Register(int size, const char* alias=nullptr) :
```

Or in Python:
```
def __init__(self, size, alias=""):
```
