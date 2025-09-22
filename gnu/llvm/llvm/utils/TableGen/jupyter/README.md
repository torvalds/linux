# Jupyter Tools for TableGen

This folder contains notebooks relating to TableGen and a Jupyter kernel for
TableGen.

## Notebooks

[LLVM_TableGen.ipynb](LLVM_TableGen.ipynb) - A demo of the kernel's capabilities.

[tablegen_tutorial_part_1.ipynb](tablegen_tutorial_part_1.ipynb) - A tutorial on the TableGen language.

[sql_query_backend.ipynb](sql_query_backend.ipynb) - How to write a backend using
JSON output and Python.

Notebooks can be viewed in browser on Github or downloaded and run locally. If
that is not possible, there are Markdown versions next to the notebook files.

## TableGen Kernel

To use the kernel, first install it into jupyter.

If you have installed Jupyter into a virtual environment, adjust `python3` to
be the interpreter for that environment. This will ensure that tools run the
kernel in the correct context.

```shell
    python3 -m tablegen_kernel.install
```

If you are going to open the notebook in an IDE like Visual Studio Code,
you should restart it now so that it will find the newly installed kernel.

Then run one of:

```shell
    jupyter notebook
    # Then in the notebook interface, select 'LLVM TableGen' from the 'New' menu.

    # To run the example notebook in this folder.
    jupyter notebook LLVM_TableGen.ipynb

    # To use the kernel from the command line.
    jupyter console --kernel tablegen
```

Or open the notebook in a tool with built in Jupyter support.

`llvm-tblgen` is expected to be either in the `PATH` or you can set
the environment variable `LLVM_TBLGEN_EXECUTABLE` to point to it directly.

If you see an error like this:
```shell
  Cell In[8], line 2
    // This is some tablegen
    ^
SyntaxError: invalid syntax
```

You are probably running the notebook using the iPython kernel. Make sure you
 have selected the tablegen kernel.

To run the kernel's doctests do:

```shell
    python3 tablegen_kernel/kernel.py
```
