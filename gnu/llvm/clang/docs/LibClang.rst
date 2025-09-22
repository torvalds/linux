.. role:: raw-html(raw)
    :format: html

Libclang tutorial
=================
The C Interface to Clang provides a relatively small API that exposes facilities for parsing source code into an abstract syntax tree (AST), loading already-parsed ASTs, traversing the AST, associating physical source locations with elements within the AST, and other facilities that support Clang-based development tools.
This C interface to Clang will never provide all of the information representation stored in Clang's C++ AST, nor should it: the intent is to maintain an API that is relatively stable from one release to the next, providing only the basic functionality needed to support development tools.
The entire C interface of libclang is available in the file `Index.h`_

Essential types overview
-------------------------

All types of libclang are prefixed with ``CX``

CXIndex
~~~~~~~
An Index that consists of a set of translation units that would typically be linked together into an executable or library.

CXTranslationUnit
~~~~~~~~~~~~~~~~~
A single translation unit, which resides in an index.

CXCursor
~~~~~~~~
A cursor representing a pointer to some element in the abstract syntax tree of a translation unit.


Code example
""""""""""""

.. code-block:: cpp

  // file.cpp
  struct foo{
    int bar;
    int* bar_pointer;
  };

.. code-block:: cpp

  #include <clang-c/Index.h>
  #include <iostream>

  int main(){
    CXIndex index = clang_createIndex(0, 0); //Create index
    CXTranslationUnit unit = clang_parseTranslationUnit(
      index,
      "file.cpp", nullptr, 0,
      nullptr, 0,
      CXTranslationUnit_None); //Parse "file.cpp"


    if (unit == nullptr){
      std::cerr << "Unable to parse translation unit. Quitting.\n";
      return 0;
    }
    CXCursor cursor = clang_getTranslationUnitCursor(unit); //Obtain a cursor at the root of the translation unit
  }

Visiting elements of an AST
~~~~~~~~~~~~~~~~~~~~~~~~~~~
The elements of an AST can be recursively visited with pre-order traversal with ``clang_visitChildren``.

.. code-block:: cpp

  clang_visitChildren(
    cursor, //Root cursor
    [](CXCursor current_cursor, CXCursor parent, CXClientData client_data){

      CXString current_display_name = clang_getCursorDisplayName(current_cursor);
      //Allocate a CXString representing the name of the current cursor

      std::cout << "Visiting element " << clang_getCString(current_display_name) << "\n";
      //Print the char* value of current_display_name

      clang_disposeString(current_display_name);
      //Since clang_getCursorDisplayName allocates a new CXString, it must be freed. This applies
      //to all functions returning a CXString

      return CXChildVisit_Recurse;


    }, //CXCursorVisitor: a function pointer
    nullptr //client_data
    );

The return value of ``CXCursorVisitor``, the callable argument of ``clang_visitChildren``, can return one of the three:

#. ``CXChildVisit_Break``: Terminates the cursor traversal

#. ``CXChildVisit_Continue``: Continues the cursor traversal with the next sibling of the cursor just visited, without visiting its children.

#. ``CXChildVisit_Recurse``: Recursively traverse the children of this cursor, using the same visitor and client data

The expected output of that program is

.. code-block::

  Visiting element foo
  Visiting element bar
  Visiting element bar_pointer


Extracting information from a Cursor
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
.. The following functions take a ``CXCursor`` as an argument and return associated information.



Extracting the Cursor kind
""""""""""""""""""""""""""

``CXCursorKind clang_getCursorKind(CXCursor)`` Describes the kind of entity that a cursor refers to. Example values:

- ``CXCursor_StructDecl``: A C or C++ struct.
- ``CXCursor_FieldDecl``: A field in a struct, union, or C++ class.
- ``CXCursor_CallExpr``: An expression that calls a function.


Extracting the Cursor type
""""""""""""""""""""""""""
``CXType clang_getCursorType(CXCursor)``: Retrieve the type of a CXCursor (if any).

A ``CXType`` represents a complete C++ type, including qualifiers and pointers. It has a member field ``CXTypeKind kind`` and additional opaque data.

Example values for ``CXTypeKind kind``

- ``CXType_Invalid``: Represents an invalid type (e.g., where no type is available)
- ``CXType_Pointer``: A pointer to another type
- ``CXType_Int``: Regular ``int``
- ``CXType_Elaborated``: Represents a type that was referred to using an elaborated type keyword e.g. struct S, or via a qualified name, e.g., N::M::type, or both.

Any ``CXTypeKind`` can be converted to a ``CXString`` using ``clang_getTypeKindSpelling(CXTypeKind)``.

A ``CXType`` holds additional necessary opaque type info, such as:

- Which struct was referred to?
- What type is the pointer pointing to?
- Qualifiers (e.g. ``const``, ``volatile``)?

Qualifiers of a ``CXType`` can be queried with:

- ``clang_isConstQualifiedType(CXType)`` to check for ``const``
- ``clang_isRestrictQualifiedType(CXType)`` to check for ``restrict``
- ``clang_isVolatileQualifiedType(CXType)`` to check for ``volatile``

Code example
""""""""""""
.. code-block:: cpp

  //structs.cpp
  struct A{
    int value;
  };
  struct B{
    int value;
    A struct_value;
  };

.. code-block:: cpp

  #include <clang-c/Index.h>
  #include <iostream>

  int main(){
    CXIndex index = clang_createIndex(0, 0); //Create index
    CXTranslationUnit unit = clang_parseTranslationUnit(
      index,
      "structs.cpp", nullptr, 0,
      nullptr, 0,
      CXTranslationUnit_None); //Parse "structs.cpp"

    if (unit == nullptr){
      std::cerr << "Unable to parse translation unit. Quitting.\n";
      return 0;
    }
    CXCursor cursor = clang_getTranslationUnitCursor(unit); //Obtain a cursor at the root of the translation unit

    clang_visitChildren(
    cursor,
    [](CXCursor current_cursor, CXCursor parent, CXClientData client_data){
      CXType cursor_type = clang_getCursorType(current_cursor);

      CXString type_kind_spelling = clang_getTypeKindSpelling(cursor_type.kind);
      std::cout << "Type Kind: " << clang_getCString(type_kind_spelling);
      clang_disposeString(type_kind_spelling);

      if(cursor_type.kind == CXType_Pointer ||                     // If cursor_type is a pointer
        cursor_type.kind == CXType_LValueReference ||              // or an LValue Reference (&)
        cursor_type.kind == CXType_RValueReference){               // or an RValue Reference (&&),
        CXType pointed_to_type = clang_getPointeeType(cursor_type);// retrieve the pointed-to type

        CXString pointed_to_type_spelling = clang_getTypeSpelling(pointed_to_type);     // Spell out the entire
        std::cout << "pointing to type: " << clang_getCString(pointed_to_type_spelling);// pointed-to type
        clang_disposeString(pointed_to_type_spelling);
      }
      else if(cursor_type.kind == CXType_Record){
        CXString type_spelling = clang_getTypeSpelling(cursor_type);
        std::cout <<  ", namely " << clang_getCString(type_spelling);
        clang_disposeString(type_spelling);
      }
      std::cout << "\n";
      return CXChildVisit_Recurse;
    },
    nullptr
    );

The expected output of program is:

.. code-block::

  Type Kind: Record, namely A
  Type Kind: Int
  Type Kind: Record, namely B
  Type Kind: Int
  Type Kind: Record, namely A
  Type Kind: Record, namely A


Reiterating the difference between ``CXType`` and ``CXTypeKind``: For an example

.. code-block:: cpp

   const char* __restrict__ variable;

- Type Kind will be: ``CXType_Pointer`` spelled ``"Pointer"``
- Type will be a complex ``CXType`` structure, spelled ``"const char* __restrict__``

Retrieving source locations
"""""""""""""""""""""""""""

``CXSourceRange clang_getCursorExtent(CXCursor)`` returns a ``CXSourceRange``, representing a half-open range in the source code.

Use ``clang_getRangeStart(CXSourceRange)`` and ``clang_getRangeEnd(CXSourceRange)`` to retrieve the starting and end ``CXSourceLocation`` from a source range, respectively.

Given a ``CXSourceLocation``, use ``clang_getExpansionLocation`` to retrieve file, line and column of a source location.

Code example
""""""""""""
.. code-block:: cpp

  // Again, file.cpp
  struct foo{
    int bar;
    int* bar_pointer;
  };
.. code-block:: cpp

  clang_visitChildren(
    cursor,
    [](CXCursor current_cursor, CXCursor parent, CXClientData client_data){

      CXType cursor_type = clang_getCursorType(current_cursor);
      CXString cursor_spelling = clang_getCursorSpelling(current_cursor);
      CXSourceRange cursor_range = clang_getCursorExtent(current_cursor);
      std::cout << "Cursor " << clang_getCString(cursor_spelling);

      CXFile file;
      unsigned start_line, start_column, start_offset;
      unsigned end_line, end_column, end_offset;

      clang_getExpansionLocation(clang_getRangeStart(cursor_range), &file, &start_line, &start_column, &start_offset);
      clang_getExpansionLocation(clang_getRangeEnd  (cursor_range), &file, &end_line  , &end_column  , &end_offset);
      std::cout << " spanning lines " << start_line << " to " << end_line;
      clang_disposeString(cursor_spelling);

      std::cout << "\n";
      return CXChildVisit_Recurse;
    },
    nullptr
  );

The expected output of this program is:

.. code-block::

  Cursor foo spanning lines 2 to 5
  Cursor bar spanning lines 3 to 3
  Cursor bar_pointer spanning lines 4 to 4

Complete example code
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

  #include <clang-c/Index.h>
  #include <iostream>

  int main(){
    CXIndex index = clang_createIndex(0, 0); //Create index
    CXTranslationUnit unit = clang_parseTranslationUnit(
      index,
      "file.cpp", nullptr, 0,
      nullptr, 0,
      CXTranslationUnit_None); //Parse "file.cpp"

    if (unit == nullptr){
      std::cerr << "Unable to parse translation unit. Quitting.\n";
      return 0;
    }
    CXCursor cursor = clang_getTranslationUnitCursor(unit); //Obtain a cursor at the root of the translation unit


    clang_visitChildren(
    cursor,
    [](CXCursor current_cursor, CXCursor parent, CXClientData client_data){
      CXType cursor_type = clang_getCursorType(current_cursor);

      CXString type_kind_spelling = clang_getTypeKindSpelling(cursor_type.kind);
      std::cout << "TypeKind: " << clang_getCString(type_kind_spelling);
      clang_disposeString(type_kind_spelling);

      if(cursor_type.kind == CXType_Pointer ||                     // If cursor_type is a pointer
        cursor_type.kind == CXType_LValueReference ||              // or an LValue Reference (&)
        cursor_type.kind == CXType_RValueReference){               // or an RValue Reference (&&),
        CXType pointed_to_type = clang_getPointeeType(cursor_type);// retrieve the pointed-to type

        CXString pointed_to_type_spelling = clang_getTypeSpelling(pointed_to_type);     // Spell out the entire
        std::cout << "pointing to type: " << clang_getCString(pointed_to_type_spelling);// pointed-to type
        clang_disposeString(pointed_to_type_spelling);
      }
      else if(cursor_type.kind == CXType_Record){
        CXString type_spelling = clang_getTypeSpelling(cursor_type);
        std::cout <<  ", namely " << clang_getCString(type_spelling);
        clang_disposeString(type_spelling);
      }
      std::cout << "\n";
      return CXChildVisit_Recurse;
    },
    nullptr
    );


    clang_visitChildren(
    cursor,
    [](CXCursor current_cursor, CXCursor parent, CXClientData client_data){

      CXType cursor_type = clang_getCursorType(current_cursor);
      CXString cursor_spelling = clang_getCursorSpelling(current_cursor);
      CXSourceRange cursor_range = clang_getCursorExtent(current_cursor);
      std::cout << "Cursor " << clang_getCString(cursor_spelling);

      CXFile file;
      unsigned start_line, start_column, start_offset;
      unsigned end_line, end_column, end_offset;

      clang_getExpansionLocation(clang_getRangeStart(cursor_range), &file, &start_line, &start_column, &start_offset);
      clang_getExpansionLocation(clang_getRangeEnd  (cursor_range), &file, &end_line  , &end_column  , &end_offset);
      std::cout << " spanning lines " << start_line << " to " << end_line;
      clang_disposeString(cursor_spelling);

      std::cout << "\n";
      return CXChildVisit_Recurse;
    },
    nullptr
    );
  }


.. _Index.h: https://github.com/llvm/llvm-project/blob/main/clang/include/clang-c/Index.h
