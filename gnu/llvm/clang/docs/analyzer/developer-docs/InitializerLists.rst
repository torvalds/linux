================
Initializer List
================
This discussion took place in https://reviews.llvm.org/D35216
"Escape symbols when creating std::initializer_list".

It touches problems of modelling C++ standard library constructs in general,
including modelling implementation-defined fields within C++ standard library
objects, in particular constructing objects into pointers held by such fields,
and separation of responsibilities between analyzer's core and checkers.

**Artem:**

I've seen a few false positives that appear because we construct
C++11 std::initializer_list objects with brace initializers, and such
construction is not properly modeled. For instance, if a new object is
constructed on the heap only to be put into a brace-initialized STL container,
the object is reported to be leaked.

Approach (0): This can be trivially fixed by this patch, which causes pointers
passed into initializer list expressions to immediately escape.

This fix is overly conservative though. So i did a bit of investigation as to
how model std::initializer_list better.

According to the standard, ``std::initializer_list<T>`` is an object that has
methods ``begin(), end(), and size()``, where ``begin()`` returns a pointer to continuous
array of ``size()`` objects of type T, and end() is equal to begin() plus size().
The standard does hint that it should be possible to implement
``std::initializer_list<T>`` as a pair of pointers, or as a pointer and a size
integer, however specific fields that the object would contain are an
implementation detail.

Ideally, we should be able to model the initializer list's methods precisely.
Or, at least, it should be possible to explain to the analyzer that the list
somehow "takes hold" of the values put into it. Initializer lists can also be
copied, which is a separate story that i'm not trying to address here.

The obvious approach to modeling ``std::initializer_list`` in a checker would be to
construct a SymbolMetadata for the memory region of the initializer list object,
which would be of type ``T*`` and represent ``begin()``, so we'd trivially model ``begin()``
as a function that returns this symbol. The array pointed to by that symbol
would be ``bindLoc()``ed to contain the list's contents (probably as a ``CompoundVal``
to produce less bindings in the store). Extent of this array would represent
``size()`` and would be equal to the length of the list as written.

So this sounds good, however apparently it does nothing to address our false
positives: when the list escapes, our ``RegionStoreManager`` is not magically
guessing that the metadata symbol attached to it, together with its contents,
should also escape. In fact, it's impossible to trigger a pointer escape from
within the checker.

Approach (1): If only we enabled ``ProgramState::bindLoc(..., notifyChanges=true)``
to cause pointer escapes (not only region changes) (which sounds like the right
thing to do anyway) such checker would be able to solve the false positives by
triggering escapes when binding list elements to the list. However, it'd be as
conservative as the current patch's solution. Ideally, we do not want escapes to
happen so early. Instead, we'd prefer them to be delayed until the list itself
escapes.

So i believe that escaping metadata symbols whenever their base regions escape
would be the right thing to do. Currently we didn't think about that because we
had neither pointer-type metadatas nor non-pointer escapes.

Approach (2): We could teach the Store to scan itself for bindings to
metadata-symbolic-based regions during scanReachableSymbols() whenever a region
turns out to be reachable. This requires no work on checker side, but it sounds
performance-heavy.

Approach (3): We could let checkers maintain the set of active metadata symbols
in the program state (ideally somewhere in the Store, which sounds weird but
causes the smallest amount of layering violations), so that the core knew what
to escape. This puts a stress on the checkers, but with a smart data map it
wouldn't be a problem.

Approach (4): We could allow checkers to trigger pointer escapes in arbitrary
moments. If we allow doing this within ``checkPointerEscape`` callback itself, we
would be able to express facts like "when this region escapes, that metadata
symbol attached to it should also escape". This sounds like an ultimate freedom,
with maximum stress on the checkers - still not too much stress when we have
smart data maps.

I'm personally liking the approach (2) - it should be possible to avoid
performance overhead, and clarity seems nice.

**Gabor:**

At this point, I am a bit wondering about two questions.

* When should something belong to a checker and when should something belong to the engine?
  Sometimes we model library aspects in the engine and model language constructs in checkers.

* What is the checker programming model that we are aiming for? Maximum freedom or more easy checker development?

I think if we aim for maximum freedom, we do not need to worry about the
potential stress on checkers, and we can introduce abstractions to mitigate that
later on.
If we want to simplify the API, then maybe it makes more sense to move language
construct modeling to the engine when the checker API is not sufficient instead
of complicating the API.

Right now I have no preference or objections between the alternatives but there
are some random thoughts:

* Maybe it would be great to have a guideline how to evolve the analyzer and
  follow it, so it can help us to decide in similar situations

* I do care about performance in this case. The reason is that we have a
  limited performance budget. And I think we should not expect most of the checker
  writers to add modeling of language constructs. So, in my opinion, it is ok to
  have less nice/more verbose API for language modeling if we can have better
  performance this way, since it only needs to be done once, and is done by the
  framework developers.

**Artem:** These are some great questions, i guess it'd be better to discuss
them more openly. As a quick dump of my current mood:

* To me it seems obvious that we need to aim for a checker API that is both
  simple and powerful. This can probably by keeping the API as powerful as
  necessary while providing a layer of simple ready-made solutions on top of it.
  Probably a few reusable components for assembling checkers. And this layer
  should ideally be pleasant enough to work with, so that people would prefer to
  extend it when something is lacking, instead of falling back to the complex
  omnipotent API. I'm thinking of AST matchers vs. AST visitors as a roughly
  similar situation: matchers are not omnipotent, but they're so nice.

* Separation between core and checkers is usually quite strange. Once we have
  shared state traits, i generally wouldn't mind having region store or range
  constraint manager as checkers (though it's probably not worth it to transform
  them - just a mood). The main thing to avoid here would be the situation when
  the checker overwrites stuff written by the core because it thinks it has a
  better idea what's going on, so the core should provide a good default behavior.

* Yeah, i totally care about performance as well, and if i try to implement
  approach, i'd make sure it's good.

**Artem:**

> Approach (2): We could teach the Store to scan itself for bindings to
> metadata-symbolic-based regions during scanReachableSymbols() whenever
> a region turns out to be reachable. This requires no work on checker side,
> but it sounds performance-heavy.

Nope, this approach is wrong. Metadata symbols may become out-of-date: when the
object changes, metadata symbols attached to it aren't changing (because symbols
simply don't change). The same metadata may have different symbols to denote its
value in different moments of time, but at most one of them represents the
actual metadata value. So we'd be escaping more stuff than necessary.

If only we had "ghost fields"
(https://lists.llvm.org/pipermail/cfe-dev/2016-May/049000.html), it would have
been much easier, because the ghost field would only contain the actual
metadata, and the Store would always know about it. This example adds to my
belief that ghost fields are exactly what we need for most C++ checkers.

**Devin:**

In this case, I would be fine with some sort of
AbstractStorageMemoryRegion that meant "here is a memory region and somewhere
reachable from here exists another region of type T". Or even multiple regions
with different identifiers. This wouldn't specify how the memory is reachable,
but it would allow for transfer functions to get at those regions and it would
allow for invalidation.

For ``std::initializer_list`` this reachable region would the region for the backing
array and the transfer functions for begin() and end() yield the beginning and
end element regions for it.

In my view this differs from ghost variables in that (1) this storage does
actually exist (it is just a library implementation detail where that storage
lives) and (2) it is perfectly valid for a pointer into that storage to be
returned and for another part of the program to read or write from that storage.
(Well, in this case just read since it is allowed to be read-only memory).

What I'm not OK with is modeling abstract analysis state (for example, the count
of a NSMutableArray or the typestate of a file handle) as a value stored in some
ginned up region in the store. This takes an easy problem that the analyzer does
well at (modeling typestate) and turns it into a hard one that the analyzer is
bad at (reasoning about the contents of the heap).

I think the key criterion here is: "is the region accessible from outside the
library". That is, does the library expose the region as a pointer that can be
read to or written from in the client program? If so, then it makes sense for
this to be in the store: we are modeling reachable storage as storage. But if
we're just modeling arbitrary analysis facts that need to be invalidated when a
pointer escapes then we shouldn't try to gin up storage for them just to get
invalidation for free.

**Artem:**

> In this case, I would be fine with some sort of ``AbstractStorageMemoryRegion``
> that meant "here is a memory region and somewhere reachable from here exists
> another region of type T". Or even multiple regions with different
> identifiers. This wouldn't specify how the memory is reachable, but it would
> allow for transfer functions to get at those regions and it would allow for
> invalidation.

Yeah, this is what we can easily implement now as a
symbolic-region-based-on-a-metadata-symbol (though we can make a new region
class for that if we eg. want it typed). The problem is that the relation
between such storage region and its parent object region is essentially
immaterial, similarly to the relation between ``SymbolRegionValue`` and its parent
region. Region contents are mutable: today the abstract storage is reachable
from its parent object, tomorrow it's not, and maybe something else becomes
reachable, something that isn't even abstract. So the parent region for the
abstract storage is most of the time at best a "nice to know" thing - we cannot
rely on it to do any actual work. We'd anyway need to rely on the checker to do
the job.

> For std::initializer_list this reachable region would the region for the
> backing array and the transfer functions for begin() and end() yield the
> beginning and end element regions for it.

So maybe in fact for std::initializer_list it may work fine because you cannot
change the data after the object is constructed - so this region's contents are
essentially immutable. For the future, i feel as if it is a dead end.

I'd like to consider another funny example. Suppose we're trying to model

.. code-block:: cpp

 std::unique_ptr. Consider::

   void bar(const std::unique_ptr<int> &x);

   void foo(std::unique_ptr<int> &x) {
     int *a = x.get();   // (a, 0, direct): &AbstractStorageRegion
     *a = 1;             // (AbstractStorageRegion, 0, direct): 1 S32b
     int *b = new int;
     *b = 2;             // (SymRegion{conj_$0<int *>}, 0 ,direct): 2 S32b
     x.reset(b);         // Checker map: x -> SymRegion{conj_$0<int *>}
     bar(x);             // 'a' doesn't escape (the pointer was unique), 'b' does.
     clang_analyzer_eval(*a == 1); // Making this true is up to the checker.
     clang_analyzer_eval(*b == 2); // Making this unknown is up to the checker.
   }

The checker doesn't totally need to ensure that ``*a == 1`` passes - even though the
pointer was unique, it could theoretically have ``.get()``-ed above and the code
could of course break the uniqueness invariant (though we'd probably want it).
The checker can say that "even if ``*a`` did escape, it was not because it was
stuffed directly into bar()".

The checker's direct responsibility, however, is to solve the ``*b == 2`` thing
(which is in fact the problem we're dealing with in this patch - escaping the
storage region of the object).

So we're talking about one more operation over the program state (scanning
reachable symbols and regions) that cannot work without checker support.

We can probably add a new callback "checkReachableSymbols" to solve this. This
is in fact also related to the dead symbols problem (we're scanning for live
symbols in the store and in the checkers separately, but we need to do so
simultaneously with a single worklist). Hmm, in fact this sounds like a good
idea; we can replace checkLiveSymbols with checkReachableSymbols.

Or we could just have ghost member variables, and no checker support required at
all. For ghost member variables, the relation with their parent region (which
would be their superregion) is actually useful, the mutability of their contents
is expressed naturally, and the store automagically sees reachable symbols, live
symbols, escapes, invalidations, whatever.

> In my view this differs from ghost variables in that (1) this storage does
> actually exist (it is just a library implementation detail where that storage
> lives) and (2) it is perfectly valid for a pointer into that storage to be
> returned and for another part of the program to read or write from that
> storage. (Well, in this case just read since it is allowed to be read-only
> memory).

> What I'm not OK with is modeling abstract analysis state (for example, the
> count of a NSMutableArray or the typestate of a file handle) as a value stored
> in some ginned up region in the store.This takes an easy problem that the
> analyzer does well at (modeling typestate) and turns it into a hard one that
> the analyzer is bad at (reasoning about the contents of the heap).

Yeah, i tend to agree on that. For simple typestates, this is probably an
overkill, so let's definitely put aside the idea of "ghost symbolic regions"
that i had earlier.

But, to summarize a bit, in our current case, however, the typestate we're
looking for is the contents of the heap. And when we try to model such
typestates (complex in this specific manner, i.e. heap-like) in any checker, we
have a choice between re-doing this modeling in every such checker (which is
something analyzer is indeed good at, but at a price of making checkers heavy)
or instead relying on the Store to do exactly what it's designed to do.

> I think the key criterion here is: "is the region accessible from outside
> the library". That is, does the library expose the region as a pointer that
> can be read to or written from in the client program? If so, then it makes
> sense for this to be in the store: we are modeling reachable storage as
> storage. But if we're just modeling arbitrary analysis facts that need to be
> invalidated when a pointer escapes then we shouldn't try to gin up storage
> for them just to get invalidation for free.

As a metaphor, i'd probably compare it to body farms - the difference between
ghost member variables and metadata symbols seems to me like the difference
between body farms and evalCall. Both are nice to have, and body farms are very
pleasant to work with, even if not omnipotent. I think it's fine for a
FunctionDecl's body in a body farm to have a local variable, even if such
variable doesn't actually exist, even if it cannot be seen from outside the
function call. I'm not seeing immediate practical difference between "it does
actually exist" and "it doesn't actually exist, just a handy abstraction".
Similarly, i think it's fine if we have a ``CXXRecordDecl`` with
implementation-defined contents, and try to farm up a member variable as a handy
abstraction (we don't even need to know its name or offset, only that it's there
somewhere).

**Artem:**

We've discussed it in person with Devin, and he provided more points to think
about:

* If the initializer list consists of non-POD data, constructors of list's
  objects need to take the sub-region of the list's region as this-region In the
  current (v2) version of this patch, these objects are constructed elsewhere and
  then trivial-copied into the list's metadata pointer region, which may be
  incorrect. This is our overall problem with C++ constructors, which manifests in
  this case as well. Additionally, objects would need to be constructed in the
  analyzer's core, which would not be able to predict that it needs to take a
  checker-specific region as this-region, which makes it harder, though it might
  be mitigated by sharing the checker state traits.

* Because "ghost variables" are not material to the user, we need to somehow
  make super sure that they don't make it into the diagnostic messages.

So, because this needs further digging into overall C++ support and rises too
many questions, i'm delaying a better approach to this problem and will fall
back to the original trivial patch.
