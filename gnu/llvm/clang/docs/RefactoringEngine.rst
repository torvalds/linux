==========================
Clang's refactoring engine
==========================

This document describes the design of Clang's refactoring engine and provides
a couple of examples that show how various primitives in the refactoring API
can be used to implement different refactoring actions. The :doc:`LibTooling`
library provides several other APIs that are used when developing a
refactoring action.

Refactoring engine can be used to implement local refactorings that are
initiated using a selection in an editor or an IDE. You can combine
:doc:`AST matchers<LibASTMatchers>` and the refactoring engine to implement
refactorings that don't lend themselves well to source selection and/or have to
query ASTs for some particular nodes.

We assume basic knowledge about the Clang AST. See the :doc:`Introduction
to the Clang AST <IntroductionToTheClangAST>` if you want to learn more
about how the AST is structured.

..  FIXME: create new refactoring action tutorial and link to the tutorial

Introduction
------------

Clang's refactoring engine defines a set refactoring actions that implement
a number of different source transformations. The ``clang-refactor``
command-line tool can be used to perform these refactorings. Certain
refactorings are also available in other clients like text editors and IDEs.

A refactoring action is a class that defines a list of related refactoring
operations (rules). These rules are grouped under a common umbrella - a single
``clang-refactor`` command. In addition to rules, the refactoring action
provides the action's command name and description to ``clang-refactor``.
Each action must implement the ``RefactoringAction`` interface. Here's an
outline of a ``local-rename`` action:

.. code-block:: c++

  class LocalRename final : public RefactoringAction {
  public:
    StringRef getCommand() const override { return "local-rename"; }

    StringRef getDescription() const override {
      return "Finds and renames symbols in code with no indexer support";
    }

    RefactoringActionRules createActionRules() const override {
      ...
    }
  };

Refactoring Action Rules
------------------------

An individual refactoring action is responsible for creating the set of
grouped refactoring action rules that represent one refactoring operation.
Although the rules in one action may have a number of different implementations,
they should strive to produce a similar result. It should be easy for users to
identify which refactoring action produced the result regardless of which
refactoring action rule was used.

The distinction between actions and rules enables the creation of actions
that define a set of different rules that produce similar results. For example,
the "add missing switch cases" refactoring operation typically adds missing
cases to one switch at a time. However, it could be useful to have a
refactoring that works on all switches that operate on a particular enum, as
one could then automatically update all of them after adding a new enum
constant. To achieve that, we can create two different rules that will use one
``clang-refactor`` subcommand. The first rule will describe a local operation
that's initiated when the user selects a single switch. The second rule will
describe a global operation that works across translation units and is initiated
when the user provides the name of the enum to clang-refactor (or the user could
select the enum declaration instead). The clang-refactor tool will then analyze
the selection and other options passed to the refactoring action, and will pick
the most appropriate rule for the given selection and other options.

Rule Types
^^^^^^^^^^

Clang's refactoring engine supports several different refactoring rules:

- ``SourceChangeRefactoringRule`` produces source replacements that are applied
  to the source files. Subclasses that choose to implement this rule have to
  implement the ``createSourceReplacements`` member function. This type of
  rule is typically used to implement local refactorings that transform the
  source in one translation unit only.

- ``FindSymbolOccurrencesRefactoringRule`` produces a "partial" refactoring
  result: a set of occurrences that refer to a particular symbol. This type
  of rule is typically used to implement an interactive renaming action that
  allows users to specify which occurrences should be renamed during the
  refactoring. Subclasses that choose to implement this rule have to implement
  the ``findSymbolOccurrences`` member function.

The following set of quick checks might help if you are unsure about the type
of rule you should use:

#. If you would like to transform the source in one translation unit and if
   you don't need any cross-TU information, then the
   ``SourceChangeRefactoringRule`` should work for you.

#. If you would like to implement a rename-like operation with potential
   interactive components, then ``FindSymbolOccurrencesRefactoringRule`` might
   work for you.

How to Create a Rule
^^^^^^^^^^^^^^^^^^^^

Once you determine which type of rule is suitable for your needs you can
implement the refactoring by subclassing the rule and implementing its
interface. The subclass should have a constructor that takes the inputs that
are needed to perform the refactoring. For example, if you want to implement a
rule that simply deletes a selection, you should create a subclass of
``SourceChangeRefactoringRule`` with a constructor that accepts the selection
range:

.. code-block:: c++

  class DeleteSelectedRange final : public SourceChangeRefactoringRule {
  public:
    DeleteSelection(SourceRange Selection) : Selection(Selection) {}

    Expected<AtomicChanges>
    createSourceReplacements(RefactoringRuleContext &Context) override {
      AtomicChange Replacement(Context.getSources(), Selection.getBegin());
      Replacement.replace(Context.getSource,
                          CharSourceRange::getCharRange(Selection), "");
      return { Replacement };
    }
  private:
    SourceRange Selection;
  };

The rule's subclass can then be added to the list of refactoring action's
rules for a particular action using the ``createRefactoringActionRule``
function. For example, the class that's shown above can be added to the
list of action rules using the following code:

.. code-block:: c++

  RefactoringActionRules Rules;
  Rules.push_back(
    createRefactoringActionRule<DeleteSelectedRange>(
          SourceRangeSelectionRequirement())
  );

The ``createRefactoringActionRule`` function takes in a list of refactoring
action rule requirement values. These values describe the initiation
requirements that have to be satisfied by the refactoring engine before the
provided action rule can be constructed and invoked. The next section
describes how these requirements are evaluated and lists all the possible
requirements that can be used to construct a refactoring action rule.

Refactoring Action Rule Requirements
------------------------------------

A refactoring action rule requirement is a value whose type derives from the
``RefactoringActionRuleRequirement`` class. The type must define an
``evaluate`` member function that returns a value of type ``Expected<...>``.
When a requirement value is used as an argument to
``createRefactoringActionRule``, that value is evaluated during the initiation
of the action rule. The evaluated result is then passed to the rule's
constructor unless the evaluation produced an error. For example, the
``DeleteSelectedRange`` sample rule that's defined in the previous section
will be evaluated using the following steps:

#. ``SourceRangeSelectionRequirement``'s ``evaluate`` member function will be
   called first. It will return an ``Expected<SourceRange>``.

#. If the return value is an error the initiation will fail and the error
   will be reported to the client. Note that the client may not report the
   error to the user.

#. Otherwise the source range return value will be used to construct the
   ``DeleteSelectedRange`` rule. The rule will then be invoked as the initiation
   succeeded (all requirements were evaluated successfully).

The same series of steps applies to any refactoring rule. Firstly, the engine
will evaluate all of the requirements. Then it will check if these requirements
are satisfied (they should not produce an error). Then it will construct the
rule and invoke it.

The separation of requirements, their evaluation and the invocation of the
refactoring action rule allows the refactoring clients to:

- Disable refactoring action rules whose requirements are not supported.

- Gather the set of options and define a command-line / visual interface
  that allows users to input these options without ever invoking the
  action.

Selection Requirements
^^^^^^^^^^^^^^^^^^^^^^

The refactoring rule requirements that require some form of source selection
are listed below:

- ``SourceRangeSelectionRequirement`` evaluates to a source range when the
  action is invoked with some sort of selection. This requirement should be
  satisfied when a refactoring is initiated in an editor, even when the user
  has not selected anything (the range will contain the cursor's location in
  that case).

..  FIXME: Future selection requirements

..  FIXME: Maybe mention custom selection requirements?

Other Requirements
^^^^^^^^^^^^^^^^^^

There are several other requirements types that can be used when creating
a refactoring rule:

- The ``RefactoringOptionsRequirement`` requirement is an abstract class that
  should be subclassed by requirements working with options. The more
  concrete ``OptionRequirement`` requirement is a simple implementation of the
  aforementioned class that returns the value of the specified option when
  it's evaluated. The next section talks more about refactoring options and
  how they can be used when creating a rule.

Refactoring Options
-------------------

Refactoring options are values that affect a refactoring operation and are
specified either using command-line options or another client-specific
mechanism. Options should be created using a class that derives either from
the ``OptionalRequiredOption`` or ``RequiredRefactoringOption``. The following
example shows how one can created a required string option that corresponds to
the ``-new-name`` command-line option in clang-refactor:

.. code-block:: c++

  class NewNameOption : public RequiredRefactoringOption<std::string> {
  public:
    StringRef getName() const override { return "new-name"; }
    StringRef getDescription() const override {
      return "The new name to change the symbol to";
    }
  };

The option that's shown in the example above can then be used to create
a requirement for a refactoring rule using a requirement like
``OptionRequirement``:

.. code-block:: c++

  createRefactoringActionRule<RenameOccurrences>(
    ...,
    OptionRequirement<NewNameOption>())
  );

..  FIXME: Editor Bindings section
