# LLVM TableGen

The purpose of TableGen is to generate complex output files based on information
from source files that are significantly easier to code than the output files would be, and also easier to maintain and modify over time.

The information is coded in a declarative style involving classes and records,
which are then processed by TableGen.

```
class Hello <string _msg> {
  string msg = !strconcat("Hello ", _msg);
}

def HelloWorld: Hello<"world!"> {}
```
```
------------- Classes -----------------
class Hello<string Hello:_msg = ?> {
  string msg = !strconcat("Hello ", Hello:_msg);
}
------------- Defs -----------------
def HelloWorld {        // Hello
  string msg = "Hello world!";
}
```
[Try this example on Compiler Explorer.](https://godbolt.org/z/13xo1P5oz)

The internalized records are passed on to various backends, which extract
information from a subset of the records and generate one or more output files.

These output files are typically .inc files for C++, but may be any type of file
that the backend developer needs.

Resources for learning the language:
* [TableGen Overview](https://llvm.org/docs/TableGen/index.html)
* [Programmer's reference guide](https://llvm.org/docs/TableGen/ProgRef.html)
* [Tutorial](jupyter/tablegen_tutorial_part_1.ipynb)
* [Tools for Learning LLVM TableGen](https://blog.llvm.org/posts/2023-12-07-tools-for-learning-llvm-tablegen/)
* [Lessons in TableGen](https://www.youtube.com/watch?v=45gmF77JFBY) (video),
  [slides](https://archive.fosdem.org/2019/schedule/event/llvm_tablegen/attachments/slides/3304/export/events/attachments/llvm_tablegen/slides/3304/tablegen.pdf)
* [Improving Your TableGen Descriptions](https://www.youtube.com/watch?v=dIEVUlsiktQ)
  (video), [slides](https://llvm.org/devmtg/2019-10/slides/Absar-ImprovingYourTableGenDescription.pdf)

Writing TableGen backends:
* [TableGen Backend Developer's Guide](https://llvm.org/docs/TableGen/BackGuide.html)
* [How to write a TableGen backend](https://www.youtube.com/watch?v=UP-LBRbvI_U)
  (video), [slides](https://llvm.org/devmtg/2021-11/slides/2021-how-to-write-a-tablegen-backend.pdf), also available as a
	[notebook](jupyter/sql_query_backend.ipynb).

TableGen in MLIR:
* [Operation Definition Specification](https://mlir.llvm.org/docs/DefiningDialects/Operations/)
* [Defining Dialect Attributes and Types](https://mlir.llvm.org/docs/DefiningDialects/AttributesAndTypes/)

Useful tools:
* [TableGen Jupyter Kernel](jupyter/)
* [TableGen LSP Language Server](https://mlir.llvm.org/docs/Tools/MLIRLSP/#tablegen-lsp-language-server--tblgen-lsp-server)

