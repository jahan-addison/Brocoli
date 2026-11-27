# Object Table

The functions, stack frames, vectors, unions, and globals of a program.

* The [`hir::Analyzer`](/credence/language/hir/analyzer.h) defines its functions, vectors, and globals
* The [`ir::Binder`](/credence/ir/binder.h) defines the labels and locals of each frame, and binds the IR onto them
* The backends allocate and address memory from it

See [`table.h`](/credence/object/table.h) for the table, and [`frame.h`](/credence/object/frame.h) for a function's frame.
