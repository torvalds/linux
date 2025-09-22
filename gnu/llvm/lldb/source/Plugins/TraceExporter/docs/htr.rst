Hierarchical Trace Representation (HTR)
======================================
The humongous amount of data processor traces like the ones obtained with Intel PT contain is not digestible to humans in its raw form. Given this, it is useful to summarize these massive traces by extracting useful information. Hierarchical Trace Representation (HTR) is the way lldb represents a summarized trace internally. HTR efficiently stores trace data and allows the trace data to be transformed in a way akin to compiler passes.

Concepts
--------
**Block:** One or more contiguous units of the trace. At minimum, the unit of a trace is the load address of an instruction.

**Block Metadata:** Metadata associated with each *block*. For processor traces, some metadata examples are the number of instructions in the block or information on what functions are called in the block.

**Layer:** The representation of trace data between passes. For Intel PT there are two types of layers:

 **Instruction Layer:** Composed of the load addresses of the instructions in the trace. In an effort to save space,
 metadata is only stored for instructions that are of interest, not every instruction in the trace. HTR contains a
 single instruction layer.

 **Block Layer:** Composed of blocks - a block in *layer n* refers to a sequence of blocks in *layer n - 1*. A block in
 *layer 1* refers to a sequence of instructions in *layer 0* (the instruction layer). Metadata is stored for each block in
 a block layer. HTR contains one or more block layers.

**Pass:** A transformation applied to a *layer* that generates a new *layer* that is a more summarized, consolidated representation of the trace data.
A pass merges instructions/blocks based on its specific purpose - for example, a pass designed to summarize a processor trace by function calls would merge all the blocks of a function into a single block representing the entire function.

The image below illustrates the transformation of a trace's representation (HTR)

.. image:: media/htr-example.png


Passes
------
A *pass* is applied to a *layer* to extract useful information (summarization) and compress the trace representation into a new *layer*. The idea is to have a series of passes where each pass specializes in extracting certain information about the trace. Some examples of potential passes include: identifying functions, identifying loops, or a more general purpose such as identifying long sequences of instructions that are repeated (i.e. Basic Super Block). Below you will find a description of each pass currently implemented in lldb.

**Basic Super Block Reduction**

A “basic super block” is the longest sequence of blocks that always occur in the same order. (The concept is akin to “Basic Block'' in compiler theory, but refers to dynamic occurrences rather than CFG nodes).

The image below shows the "basic super blocks" of the sequence. Each unique "basic super block" is marked with a different color

.. image:: media/basic_super_block_pass.png

*Procedure to find all super blocks:*

- For each block, compute the number of distinct predecessor and successor blocks.

 - **Predecessor** - the block that occurs directly before (to the left of) the current block
 - **Successor** - the block that occurs directly after (to the right of) the current block

- A block with more than one distinct successor is always the start of a super block, the super block will continue until the next block with more than one distinct predecessor or successor.
