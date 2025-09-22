============
Region Store
============
The analyzer "Store" represents the contents of memory regions. It is an opaque
functional data structure stored in each ``ProgramState``; the only class that
can modify the store is its associated StoreManager.

Currently (Feb. 2013), the only StoreManager implementation being used is
``RegionStoreManager``. This store records bindings to memory regions using a
"base region + offset" key. (This allows ``*p`` and ``p[0]`` to map to the same
location, among other benefits.)

Regions are grouped into "clusters", which roughly correspond to "regions with
the same base region". This allows certain operations to be more efficient,
such as invalidation.

Regions that do not have a known offset use a special "symbolic" offset. These
keys store both the original region, and the "concrete offset region" -- the
last region whose offset is entirely concrete. (For example, in the expression
``foo.bar[1][i].baz``, the concrete offset region is the array ``foo.bar[1]``,
since that has a known offset from the start of the top-level ``foo`` struct.)


Binding Invalidation
--------------------

Supporting both concrete and symbolic offsets makes things a bit tricky. Here's
an example:

.. code-block:: cpp

  foo[0] = 0;
  foo[1] = 1;
  foo[i] = i;

After the third assignment, nothing can be said about the value of ``foo[0]``,
because ``foo[i]`` may have overwritten it! Thus, *binding to a region with a
symbolic offset invalidates the entire concrete offset region.* We know
``foo[i]`` is somewhere within ``foo``, so we don't have to invalidate
anything else, but we do have to be conservative about all other bindings within
``foo``.

Continuing the example:

.. code-block:: cpp

  foo[i] = i;
  foo[0] = 0;

After this latest assignment, nothing can be said about the value of ``foo[i]``,
because ``foo[0]`` may have overwritten it! *Binding to a region R with a
concrete offset invalidates any symbolic offset bindings whose concrete offset
region is a super-region **or** sub-region of R.* All we know about ``foo[i]``
is that it is somewhere within ``foo``, so changing *anything* within ``foo``
might change ``foo[i]``, and changing *all* of ``foo`` (or its base region) will
*definitely* change ``foo[i]``.

This logic could be improved by using the current constraints on ``i``, at the
cost of speed. The latter case could also be improved by matching region kinds,
i.e. changing ``foo[0].a`` is unlikely to affect ``foo[i].b``, no matter what
``i`` is.

For more detail, read through ``RegionStoreManager::removeSubRegionBindings`` in
RegionStore.cpp.


ObjCIvarRegions
---------------

Objective-C instance variables require a bit of special handling. Like struct
fields, they are not base regions, and when their parent object region is
invalidated, all the instance variables must be invalidated as well. However,
they have no concrete compile-time offsets (in the modern, "non-fragile"
runtime), and so cannot easily be represented as an offset from the start of
the object in the analyzer. Moreover, this means that invalidating a single
instance variable should *not* invalidate the rest of the object, since unlike
struct fields or array elements there is no way to perform pointer arithmetic
to access another instance variable.

Consequently, although the base region of an ObjCIvarRegion is the entire
object, RegionStore offsets are computed from the start of the instance
variable. Thus it is not valid to assume that all bindings with non-symbolic
offsets start from the base region!


Region Invalidation
-------------------

Unlike binding invalidation, region invalidation occurs when the entire
contents of a region may have changed---say, because it has been passed to a
function the analyzer can model, like memcpy, or because its address has
escaped, usually as an argument to an opaque function call. In these cases we
need to throw away not just all bindings within the region itself, but within
its entire cluster, since neighboring regions may be accessed via pointer
arithmetic.

Region invalidation typically does even more than this, however. Because it
usually represents the complete escape of a region from the analyzer's model,
its *contents* must also be transitively invalidated. (For example, if a region
``p`` of type ``int **`` is invalidated, the contents of ``*p`` and ``**p`` may
have changed as well.) The algorithm that traverses this transitive closure of
accessible regions is known as ClusterAnalysis, and is also used for finding
all live bindings in the store (in order to throw away the dead ones). The name
"ClusterAnalysis" predates the cluster-based organization of bindings, but
refers to the same concept: during invalidation and liveness analysis, all
bindings within a cluster must be treated in the same way for a conservative
model of program behavior.


Default Bindings
----------------

Most bindings in RegionStore are simple scalar values -- integers and pointers.
These are known as "Direct" bindings. However, RegionStore supports a second
type of binding called a "Default" binding. These are used to provide values to
all the elements of an aggregate type (struct or array) without having to
explicitly specify a binding for each individual element.

When there is no Direct binding for a particular region, the store manager
looks at each super-region in turn to see if there is a Default binding. If so,
this value is used as the value of the original region. The search ends when
the base region is reached, at which point the RegionStore will pick an
appropriate default value for the region (usually a symbolic value, but
sometimes zero, for static data, or "uninitialized", for stack variables).

.. code-block:: cpp

  int manyInts[10];
  manyInts[1] = 42;   // Creates a Direct binding for manyInts[1].
  print(manyInts[1]); // Retrieves the Direct binding for manyInts[1];
  print(manyInts[0]); // There is no Direct binding for manyInts[0].
                      // Is there a Default binding for the entire array?
                      // There is not, but it is a stack variable, so we use
                      // "uninitialized" as the default value (and emit a
                      // diagnostic!).

NOTE: The fact that bindings are stored as a base region plus an offset limits
the Default Binding strategy, because in C aggregates can contain other
aggregates. In the current implementation of RegionStore, there is no way to
distinguish a Default binding for an entire aggregate from a Default binding
for the sub-aggregate at offset 0.


Lazy Bindings (LazyCompoundVal)
-------------------------------

RegionStore implements an optimization for copying aggregates (structs and
arrays) called "lazy bindings", implemented using a special SVal called
LazyCompoundVal. When the store is asked for the "binding" for an entire
aggregate (i.e. for an lvalue-to-rvalue conversion), it returns a
LazyCompoundVal instead. When this value is then stored into a variable, it is
bound as a Default value. This makes copying arrays and structs much cheaper
than if they had required memberwise access.

Under the hood, a LazyCompoundVal is implemented as a uniqued pair of (region,
store), representing "the value of the region during this 'snapshot' of the
store". This has important implications for any sort of liveness or
reachability analysis, which must take the bindings in the old store into
account.

Retrieving a value from a lazy binding happens in the same way as any other
Default binding: since there is no direct binding, the store manager falls back
to super-regions to look for an appropriate default binding. LazyCompoundVal
differs from a normal default binding, however, in that it contains several
different values, instead of one value that will appear several times. Because
of this, the store manager has to reconstruct the subregion chain on top of the
LazyCompoundVal region, and look up *that* region in the previous store.

Here's a concrete example:

.. code-block:: cpp

  CGPoint p;
  p.x = 42;       // A Direct binding is made to the FieldRegion 'p.x'.
  CGPoint p2 = p; // A LazyCompoundVal is created for 'p', along with a
                  // snapshot of the current store state. This value is then
                  // used as a Default binding for the VarRegion 'p2'.
  return p2.x;    // The binding for FieldRegion 'p2.x' is requested.
                  // There is no Direct binding, so we look for a Default
                  // binding to 'p2' and find the LCV.
                  // Because it's a LCV, we look at our requested region
                  // and see that it's the '.x' field. We ask for the value
                  // of 'p.x' within the snapshot, and get back 42.
