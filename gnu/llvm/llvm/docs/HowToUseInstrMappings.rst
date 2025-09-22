===============================
How To Use Instruction Mappings
===============================

.. contents::
   :local:

Introduction
============

This document contains information about adding instruction mapping support
for a target. The motivation behind this feature comes from the need to switch
between different instruction formats during various optimizations. One approach
could be to use switch cases which list all the instructions along with formats
they can transition to. However, it has large maintenance overhead
because of the hardcoded instruction names. Also, whenever a new instruction is
added in the .td files, all the relevant switch cases should be modified
accordingly. Instead, the same functionality could be achieved with TableGen and
some support from the .td files for a fraction of maintenance cost.

``InstrMapping`` Class Overview
===============================

TableGen uses relationship models to map instructions with each other. These
models are described using ``InstrMapping`` class as a base. Each model sets
various fields of the ``InstrMapping`` class such that they can uniquely
describe all the instructions using that model. TableGen parses all the relation
models and uses the information to construct relation tables which relate
instructions with each other. These tables are emitted in the
``XXXInstrInfo.inc`` file along with the functions to query them. Following
is the definition of ``InstrMapping`` class defined in Target.td file:

.. code-block:: text

  class InstrMapping {
    // Used to reduce search space only to the instructions using this
    // relation model.
    string FilterClass;

    // List of fields/attributes that should be same for all the instructions in
    // a row of the relation table. Think of this as a set of properties shared
    // by all the instructions related by this relationship.
    list<string> RowFields = [];

    // List of fields/attributes that are same for all the instructions
    // in a column of the relation table.
    list<string> ColFields = [];

    // Values for the fields/attributes listed in 'ColFields' corresponding to
    // the key instruction. This is the instruction that will be transformed
    // using this relation model.
    list<string> KeyCol = [];

    // List of values for the fields/attributes listed in 'ColFields', one for
    // each column in the relation table. These are the instructions a key
    // instruction will be transformed into.
    list<list<string> > ValueCols = [];
  }

Sample Example
--------------

Let's say that we want to have a function
``int getPredOpcode(uint16_t Opcode, enum PredSense inPredSense)`` which
takes a non-predicated instruction and returns its predicated true or false form
depending on some input flag, ``inPredSense``. The first step in the process is
to define a relationship model that relates predicated instructions to their
non-predicated form by assigning appropriate values to the ``InstrMapping``
fields. For this relationship, non-predicated instructions are treated as key
instruction since they are the one used to query the interface function.

.. code-block:: text

  def getPredOpcode : InstrMapping {
    // Choose a FilterClass that is used as a base class for all the
    // instructions modeling this relationship. This is done to reduce the
    // search space only to these set of instructions.
    let FilterClass = "PredRel";

    // Instructions with same values for all the fields in RowFields form a
    // row in the resulting relation table.
    // For example, if we want to relate 'ADD' (non-predicated) with 'Add_pt'
    // (predicated true) and 'Add_pf' (predicated false), then all 3
    // instructions need to have same value for BaseOpcode field. It can be any
    // unique value (Ex: XYZ) and should not be shared with any other
    // instruction not related to 'add'.
    let RowFields = ["BaseOpcode"];

    // List of attributes that can be used to define key and column instructions
    // for a relation. Key instruction is passed as an argument
    // to the function used for querying relation tables. Column instructions
    // are the instructions they (key) can transform into.
    //
    // Here, we choose 'PredSense' as ColFields since this is the unique
    // attribute of the key (non-predicated) and column (true/false)
    // instructions involved in this relationship model.
    let ColFields = ["PredSense"];

    // The key column contains non-predicated instructions.
    let KeyCol = ["none"];

    // Two value columns - first column contains instructions with
    // PredSense=true while second column has instructions with PredSense=false.
    let ValueCols = [["true"], ["false"]];
  }

TableGen uses the above relationship model to emit relation table that maps
non-predicated instructions with their predicated forms. It also outputs the
interface function
``int getPredOpcode(uint16_t Opcode, enum PredSense inPredSense)`` to query
the table. Here, Function ``getPredOpcode`` takes two arguments, opcode of the
current instruction and PredSense of the desired instruction, and returns
predicated form of the instruction, if found in the relation table.
In order for an instruction to be added into the relation table, it needs
to include relevant information in its definition. For example, consider
following to be the current definitions of ADD, ADD_pt (true) and ADD_pf (false)
instructions:

.. code-block:: text

  def ADD : ALU32_rr<(outs IntRegs:$dst), (ins IntRegs:$a, IntRegs:$b),
              "$dst = add($a, $b)",
              [(set (i32 IntRegs:$dst), (add (i32 IntRegs:$a),
                                             (i32 IntRegs:$b)))]>;

  def ADD_Pt : ALU32_rr<(outs IntRegs:$dst),
                         (ins PredRegs:$p, IntRegs:$a, IntRegs:$b),
              "if ($p) $dst = add($a, $b)",
              []>;

  def ADD_Pf : ALU32_rr<(outs IntRegs:$dst),
                         (ins PredRegs:$p, IntRegs:$a, IntRegs:$b),
              "if (!$p) $dst = add($a, $b)",
              []>;

In this step, we modify these instructions to include the information
required by the relationship model, <tt>getPredOpcode</tt>, so that they can
be related.

.. code-block:: text

  def ADD : PredRel, ALU32_rr<(outs IntRegs:$dst), (ins IntRegs:$a, IntRegs:$b),
              "$dst = add($a, $b)",
              [(set (i32 IntRegs:$dst), (add (i32 IntRegs:$a),
                                             (i32 IntRegs:$b)))]> {
    let BaseOpcode = "ADD";
    let PredSense = "none";
  }

  def ADD_Pt : PredRel, ALU32_rr<(outs IntRegs:$dst),
                         (ins PredRegs:$p, IntRegs:$a, IntRegs:$b),
              "if ($p) $dst = add($a, $b)",
              []> {
    let BaseOpcode = "ADD";
    let PredSense = "true";
  }

  def ADD_Pf : PredRel, ALU32_rr<(outs IntRegs:$dst),
                         (ins PredRegs:$p, IntRegs:$a, IntRegs:$b),
              "if (!$p) $dst = add($a, $b)",
              []> {
    let BaseOpcode = "ADD";
    let PredSense = "false";
  }

Please note that all the above instructions use ``PredRel`` as a base class.
This is extremely important since TableGen uses it as a filter for selecting
instructions for ``getPredOpcode`` model. Any instruction not derived from
``PredRel`` is excluded from the analysis. ``BaseOpcode`` is another important
field. Since it's selected as a ``RowFields`` of the model, it is required
to have the same value for all 3 instructions in order to be related. Next,
``PredSense`` is used to determine their column positions by comparing its value
with ``KeyCol`` and ``ValueCols``. If an instruction sets its ``PredSense``
value to something not used in the relation model, it will not be assigned
a column in the relation table.
