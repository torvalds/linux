
.. _instructionselect:

InstructionSelect
-----------------

This pass transforms generic machine instructions into equivalent
target-specific instructions.  It traverses the ``MachineFunction`` bottom-up,
selecting uses before definitions, enabling trivial dead code elimination.

.. _api-instructionselector:

API: InstructionSelector
^^^^^^^^^^^^^^^^^^^^^^^^

The target implements the ``InstructionSelector`` class, containing the
target-specific selection logic proper.

The instance is provided by the subtarget, so that it can specialize the
selector by subtarget feature (with, e.g., a vector selector overriding parts
of a general-purpose common selector).
We might also want to parameterize it by MachineFunction, to enable selector
variants based on function attributes like optsize.

The simple API consists of:

  .. code-block:: c++

    virtual bool select(MachineInstr &MI)

This target-provided method is responsible for mutating (or replacing) a
possibly-generic MI into a fully target-specific equivalent.
It is also responsible for doing the necessary constraining of gvregs into the
appropriate register classes as well as passing through COPY instructions to
the register allocator.

The ``InstructionSelector`` can fold other instructions into the selected MI,
by walking the use-def chain of the vreg operands.
As GlobalISel is Global, this folding can occur across basic blocks.

SelectionDAG Rule Imports
^^^^^^^^^^^^^^^^^^^^^^^^^

TableGen will import SelectionDAG rules and provide the following function to
execute them:

  .. code-block:: c++

    bool selectImpl(MachineInstr &MI)

The ``--stats`` option can be used to determine what proportion of rules were
successfully imported. The easiest way to use this is to copy the
``-gen-globalisel`` tablegen command from ``ninja -v`` and modify it.

Similarly, the ``--warn-on-skipped-patterns`` option can be used to obtain the
reasons that rules weren't imported. This can be used to focus on the most
important rejection reasons.

PatLeaf Predicates
^^^^^^^^^^^^^^^^^^

PatLeafs cannot be imported because their C++ is implemented in terms of
``SDNode`` objects. PatLeafs that handle immediate predicates should be
replaced by ``ImmLeaf``, ``IntImmLeaf``, or ``FPImmLeaf`` as appropriate.

There's no standard answer for other PatLeafs. Some standard predicates have
been baked into TableGen but this should not generally be done.

Custom SDNodes
^^^^^^^^^^^^^^

Custom SDNodes should be mapped to Target Pseudos using ``GINodeEquiv``. This
will cause the instruction selector to import them but you will also need to
ensure the target pseudo is introduced to the MIR before the instruction
selector. Any preceding pass is suitable but the legalizer will be a
particularly common choice.

ComplexPatterns
^^^^^^^^^^^^^^^

ComplexPatterns cannot be imported because their C++ is implemented in terms of
``SDNode`` objects. GlobalISel versions should be defined with
``GIComplexOperandMatcher`` and mapped to ComplexPattern with
``GIComplexPatternEquiv``.

The following predicates are useful for porting ComplexPattern:

* isBaseWithConstantOffset() - Check for base+offset structures
* isOperandImmEqual() - Check for a particular constant
* isObviouslySafeToFold() - Check for reasons an instruction can't be sunk and folded into another.

There are some important points for the C++ implementation:

* Don't modify MIR in the predicate
* Renderer lambdas should capture by value to avoid use-after-free. They will be used after the predicate returns.
* Only create instructions in a renderer lambda. GlobalISel won't clean up things you create but don't use.


