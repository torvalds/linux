//==--- DiagnosticRefactoringKinds.td - refactoring diagnostics -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Refactoring Diagnostics
//===----------------------------------------------------------------------===//

let Component = "Refactoring" in {

let CategoryName = "Refactoring Invocation Issue" in {

def err_refactor_no_selection : Error<"refactoring action can't be initiated "
  "without a selection">;
def err_refactor_selection_no_symbol : Error<"there is no symbol at the given "
  "location">;
def err_refactor_selection_invalid_ast : Error<"the provided selection does "
  "not overlap with the AST nodes of interest">;

def err_refactor_code_outside_of_function : Error<"the selected code is not a "
  "part of a function's / method's body">;
def err_refactor_extract_simple_expression : Error<"the selected expression "
  "is too simple to extract">;
def err_refactor_extract_prohibited_expression : Error<"the selected "
  "expression can't be extracted">;

}

} // end of Refactoring diagnostics
