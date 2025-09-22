Data Formatters
===============

This page is an introduction to the design of the LLDB data formatters
subsystem. The intended target audience are people interested in understanding
or modifying the formatters themselves rather than writing a specific data
formatter. For the latter, refer to :doc:`/use/variable/`.

This page also highlights some open areas for improvement to the general
subsystem, and more evolutions not anticipated here are certainly possible.

Overview
--------

The LLDB data formatters subsystem is used to allow the debugger as well as the
end-users to customize the way their variables look upon inspection in the user
interface (be it the command line tool, or one of the several GUIs that are
backed by LLDB).

To this aim, they are hooked into the ``ValueObjects`` model, in order to
provide entry points through which such customization questions can be
answered. For example: What format should this number be printed as? How many
child elements does this ``std::vector`` have?

The architecture of the subsystem is layered, with the highest level layer
being the user visible interaction features (e.g. the ``type ***`` commands,
the SB classes, ...). Other layers of interest that will be analyzed in this
document include:

* Classes implementing individual data formatter types
* Classes implementing formatters navigation, discovery and categorization
* The ``FormatManager`` layer
* The ``DataVisualization`` layer
* The SWIG <> LLDB communication layer

Data Formatter Types
--------------------

As described in the user documentation, there are four types of formatters:

* Formats
* Summaries
* Filters
* Synthetic children

Formatters have descriptor classes, ``Type*Impl``, which contain at least a
"Flags" nested object, which contains both rules to be used by the matching
algorithm (e.g. should the formatter for type Foo apply to a Foo*?) or rules to
be used by the formatter itself (e.g. is this summary a oneliner?).

Individual formatter descriptor classes then also contain data items useful to
them for performing their functionality. For instance ``TypeFormatImpl``
(backing formats) contains an ``lldb::Format`` that is the format to then be
applied were this formatter to be selected. Upon issuing a ``type format add``
a new ``TypeFormatImpl`` is created that wraps the user-specified format, and
matching options:

::

  entry.reset(new TypeFormatImpl(
      format, TypeFormatImpl::Flags()
                  .SetCascades(m_command_options.m_cascade)
                  .SetSkipPointers(m_command_options.m_skip_pointers)
                  .SetSkipReferences(m_command_options.m_skip_references)));


While formats are fairly simple and only implemented by one class, the other
formatter types are backed by a class hierarchy.

Summaries, for instance, can exist in one of three "flavors":

* Summary strings
* Python script
* Native C++

The base class for summaries, ``TypeSummaryImpl``, is a pure virtual class that
wraps, again, the Flags, and exports among others:

::

  virtual bool FormatObject (ValueObject *valobj, std::string& dest) = 0;


This is the core entry point, which allows subclasses to specify their mode of
operation.

``StringSummaryFormat``, which is the class that implements summary strings,
does a check as to whether the summary is a one-liner, and if not, then uses
its stored summary string to call into ``Debugger::FormatPrompt``, and obtain a
string back, which it returns in ``dest`` as the resulting summary.

For a Python summary, implemented in ``ScriptSummaryFormat``,
``FormatObject()`` calls into the ``ScriptInterpreter`` which is supposed to
hold the knowledge on how to bridge back and forth with the scripting language
(Python in the case of LLDB) in order to produce a valid string. Implementors
of new ``ScriptInterpreters`` for other languages are expected to provide a
``GetScriptedSummary()`` entry point for this purpose, if they desire to allow
users to provide formatters in the new language

Lastly, C++ summaries (``CXXFunctionSummaryFormat``), wrap a function pointer
and call into it to execute their duty. It should be noted that there are no
facilities for users to interact with C++ formatters, and as such they are
extremely opaque, effectively being a thin wrapper between plain function
pointers and the LLDB formatters subsystem.

Also, dynamic loading of C++ formatters in LLDB is currently not implemented,
and as such it is safe and reasonable for these formatters to deal with
internal ``ValueObjects`` instances instead of public ``SBValue`` objects.

An interesting data point is that summaries are expected to be stateless. While
at the Python layer they are handed an ``SBValue`` (since nothing else could be
visible for scripts), it is not expected that the ``SBValue`` should be cached
and reused - any and all caching occurs on the LLDB side, completely
transparent to the formatter itself.

The design of synthetic children is somewhat more intricate, due to them being
stateful objects. The core idea of the design is that synthetic children act
like a two-tier model, in which there is a backend dataset (the underlying
unformatted ``ValueObject``), and an higher level view (frontend) which vends
the computed representation.

To implement a new type of synthetic children one would implement a subclass of
``SyntheticChildren``, which akin to the ``TypeFormatImpl``, contains Flags for
matching, and data items to be used for formatting. For instance,
``TypeFilterImpl`` (which implements filters), stores the list of expression
paths of the children to be displayed.

Filters are themselves synthetic children. Since all they do is provide child
values for a ``ValueObject``, it does not truly matter whether these come from the
real set of children or are crafted through some intricate algorithm. As such,
they perfectly fit within the realm of synthetic children and are only shown as
separate entities for user friendliness (to a user, picking a subset of
elements to be shown with relative ease is a valuable task, and they should not
be concerned with writing scripts to do so).

Once the descriptor of the synthetic children has been coded, in order to hook
it up, one has to implement a subclass of ``SyntheticChildrenFrontEnd``. For a
given type of synthetic children, there is a deep coupling with the matching
front-end class, given that the front-end usually needs data stored in the
descriptor (e.g. a filter needs the list of child elements).

The front-end answers the interesting questions that are the true raison d'être
of synthetic children:

::

  virtual size_t CalculateNumChildren () = 0;
  virtual lldb::ValueObjectSP GetChildAtIndex (size_t idx) = 0;
  virtual size_t GetIndexOfChildWithName (const ConstString &name) = 0;
  virtual bool Update () = 0;
  virtual bool MightHaveChildren () = 0;

Synthetic children providers (their front-ends) will be queried by LLDB for a
number of children, and then for each of them as necessary, they should be
prepared to return a ``ValueObject`` describing the child. They might also be
asked to provide a name-to-index mapping (e.g. to allow LLDB to resolve queries
like ``myFoo.myChild``).

``Update()`` and ``MightHaveChildren()`` are described in the user
documentation, and they mostly serve bookkeeping purposes.

LLDB provides three kinds of synthetic children: filters, scripted synthetics,
and the native C++ providers Filters are implemented by
``TypeFilterImpl::FrontEnd``.

Scripted synthetics are implemented by ``ScriptedSyntheticChildren::FrontEnd``,
plus a set of callbacks provided by the ``ScriptInterpteter`` infrastructure to
allow LLDB to pass the front-end queries down to the scripting languages.

As for C++ native synthetics, there is a ``CXXSyntheticChildren``, but no
corresponding ``FrontEnd`` class. The reason for this design is that
``CXXSyntheticChildren`` store a callback to a creator function, which is
responsible for providing a ``FrontEnd``. Each individual formatter (e.g.
``LibstdcppMapIteratorSyntheticFrontEnd``) is a standalone frontend, and once
created retains to relation to its underlying ``SyntheticChildren`` object.

On a ``ValueObject`` level, upon being asked to generate synthetic children for
a ``ValueObject``, LLDB spawns a ValueObjectSynthetic object which is a
subclass of ``ValueObject``. Building upon the ``ValueObject`` infrastructure,
it stores a backend, and a shared pointer to the ``SyntheticChildren``. Upon
being asked queries about children, it will use the ``SyntheticChildren`` to
generate a front-end for itself and will let the front-end answer questions.
The reason for not storing the ``FrontEnd`` itself is that there is no
guarantee that across updates, the same ``FrontEnd`` will be used over and over
(e.g. a ``SyntheticChildren`` object could serve an entire class hierarchy and
vend different frontends for different subclasses).

Formatters Matching
-------------------

The problem of formatters matching is going from "I have a ``ValueObject``" to
"these are the formatters to be used for it."

There is a rather intricate set of user rules that are involved, and a rather
intricate implementation of this model. All of these relate to the type of the
``ValueObject``. It is assumed that types are a strong enough contract that it
is possible to format an object entirely depending on its type. If this turns
out to not be correct, then the existing model will have to be changed fairly
deeply.

The basic building block is that formatters can match by exact type name or by
regular expressions, i.e. one can describe matching by saying things like "this
formatters matches type ``__NSDictionaryI``", or "this formatter matches all
type names like ``^std::__1::vector<.+>(( )?&)?$``."

This match happens in class ``FormattersContainer``. For exact matches, this
goes straight to the ``FormatMap`` (the actual storage area for formatters),
whereas for regular expression matches the regular expression is matched
against the provided candidate type name. If one were to introduce a new type
of matching (say, match against number of ``$`` signs present in the typename,
``FormattersContainer`` is the place where such a change would have to be
introduced).

It should be noted that this code involves template specialization, and as such
is somewhat trickier than other formatters code to update.

On top of the string matching mechanism (exact or regex), there are a set of
more advanced rules implemented by the ``FormattersContainer``, with the aid of the
``FormattersMatchCandidate``. Namely, it is assumed that any formatter class will
have flags to say whether it allows cascading (i.e. seeing through typedefs),
allowing pointers-to-object and reference-to-object to be formatted. Upon
verifying that a formatter would be a textual match, the Flags are checked, and
if they do not allow the formatter to be used (e.g. pointers are not allowed,
and one is looking at a Foo*), then the formatter is rejected and the search
continues. If the flags also match, then the formatter is returned upstream and
the search is over.

One relevant fact to notice is that this entire mechanism is not dependent on
the kind of formatter to be returned, which makes it easier to devise new types
of formatters as the lowest layers of the system. The demands on individual
formatters are that they define a few typedefs, and export a Flags object, and
then they can be freely matched against types as needed.

This mechanism is replicated across a number of categories. A category is a
named bucket where formatters are grouped on some basis. The most common reason
for a category to exist is a library (e.g. ``libcxx`` formatters vs. ``libstdcpp``
formatters). Categories can be enabled or disabled, and they have a priority
number, called position. The priority sets a strong order among enabled
categories. A category named "default" is always the highest priority one and
it's the category where all formatters that do not ask for a category of their
own end up (e.g. ``type summary add ....`` without a ``w somecategory`` flag
passed) The algorithm inquires each category, in the order of their priorities,
for a formatter for a type, and upon receiving a positive answer from a
category, ends the search. Of course, no search occurs in disabled categories.

At the individual category level, there is the first dependence on the type of
formatter to be returned. Since both filters and synthetic children proper are
implemented through the same backing store, the matching code needs to ensure
that, were both a synthetic children provider and a filter to match a type,
only the most recently added one is actually used. The details of the algorithm
used are to be found in ``TypeCategoryImpl::Get()``.

It is quite obvious, even to a casual reader, that there are a number of
complexities involved in this algorithm. For starters, the entire search
process has to be repeated for every variable. Moreover, for each category, one
has to repeat the entire process of crawling the types (go to pointee, ...).
This is exactly the algorithm initially implemented by LLDB. Over the course of
the life of the formatters subsystem, two main evolutions have been made to the
matching mechanism:

* A caching mechanism
* A pregeneration of all possible type matches

The cache is a layer that sits between the ``FormatManager`` and the
``TypeCategoryMap``. Upon being asked to figure out a formatter, the ``FormatManager``
will first query the cache layer, and only if that fails, will the categories
be queried using the full search algorithm. The result of that full search will
then be stored in the cache. Even a negative answer (no formatter) gets stored.
The negative answer is actually the most beneficial to cache as obtaining it
requires traversing all possible formatters in all categories just to get a
no-op back.

Of course, once an answer is cached, getting it will be much quicker than going
to a full category search, as the cached answers are of the form "type foo" -->
"formatter bar". But given how formatters can be edited or removed by the user,
either at the command line or via the API, there needs to be a way to
invalidate the cache.

This happens through the ``FormatManager::Changed()`` method. In general, anything
that changes the formatters causes ``FormatManager::Changed()`` to be called
through the ``IFormatChangeListener`` interface. This call increases the
``FormatManager``'s revision and clears the cache. The revision number is a
monotonically increasing integer counter that essentially corresponds to the
number of changes made to the formatters throughout the current LLDB session.
This counter is used by ``ValueObjects`` to know when their formatters are out of
date. Since a search is a potentially expensive operation, before caching was
introduced, individual ``ValueObjects`` remembered which revision of the
``FormatManager`` they used to search for their formatter, and stored it, so that
they would not repeat the search unless a change in the formatters had
occurred. While caching has made this less critical of an optimization, it is
still sensible and thus is kept.

Lastly, as a side note, it is worth highlighting that any change in the
formatters invalidates the entire cache. It would likely not be impossible to
be smarter and figure out a subset of cache entries to be deleted, letting
others persist, instead of having to rebuild the entire cache from scratch.
However, given that formatters are not that frequently changed during a debug
session, and the algorithmic complexity to "get it right" seems larger than the
potential benefit to be had from doing it, the full cache invalidation is the
chosen policy. The algorithm to selectively invalidate entries is probably one
of the major areas for improvements in formatters performance.

The second major optimization, introduced fairly recently, is the pregeneration
of type matches. The original algorithm was based upon the notion of a
``FormatNavigator`` as a smart object, aware of all the intricacies of the
matching rules. For each category, the ``FormatNavigator`` would generate the
possible matches (e.g. dynamic type, pointee type, ...), and check each one,
one at a time. If that failed for a category, the next one would again generate
the same matches.

This worked well, but was of course inefficient. The
``FormattersMatchCandidate`` is the solution to this performance issue. In
top-of-tree LLDB, the ``FormatManager`` has the centralized notion of the
matching rules, and the former ``FormatNavigators`` are now
``FormattersContainers``, whose only job is to guarantee a centralized storage
of formatters, and thread-safe access to such storage.

``FormatManager::GetPossibleMatches()`` fills a vector of possible matches. The
way it works is by applying each rule, generating the corresponding typename,
and storing the typename, plus the required Flags for that rule to be accepted
as a match candidate (e.g. if the match comes by fetching the pointee type, a
formatter that matches will have to allow pointees as part of its Flags
object). The ``TypeCategoryMap``, when tasked with finding a formatter for a
type, generates all possible matches and passes them down to each category. In
this model, the type system only does its (expensive) job once, and textual or
regex matches are the core of the work.

FormatManager and DataVisualization
-----------------------------------

There are two main entry points in the data formatters: the ``FormatManager`` and
the ``DataVisualization``.

The ``FormatManager`` is the internal such entry point. In this context,
internal refers to data formatters code itself, compared to other parts of
LLDB. For other components of the debugger, the ``DataVisualization`` provides
a more stable entry point. On the other hand, the ``FormatManager`` is an
aggregator of all moving parts, and as such is less stable in the face of
refactoring.

People involved in the data formatters code itself, however, will most likely
have to confront the ``FormatManager`` for significant architecture changes.

The ``FormatManager`` wraps a ``TypeCategoryMap`` (the list of all existing
categories, enabled and not), the ``FormatCache``, and several utility objects.
Plus, it is the repository of named summaries, since these don't logically
belong anywhere else.

It is also responsible for creating all builtin formatters upon the launch of
LLDB. It does so through a bunch of methods ``Load***Formatters()``, invoked as
part of its constructor. The original design of data formatters anticipated
that individual libraries would load their formatters as part of their debug
information. This work however has largely been left unattended in practice,
and as such core system libraries (mostly those for masOS/iOS development as of
today) load their formatters in an hardcoded fashion.

For performance reasons, the ``FormatManager`` is constructed upon being first
required. This happens through the ``DataVisualization`` layer. Upon first
being inquired for anything formatters, ``DataVisualization`` calls its own
local static function ``GetFormatManager()``, which in turns constructs and
returns a local static ``FormatManager``.

Unlike most things in LLDB, the lifetime of the ``FormatManager`` is the same
as the entire session, rather than a specific ``Debugger`` or ``Target``
instance. This is an area to be improved, but as of now it has not caused
enough grief to warrant action. If this work were to be undertaken, one could
conceivably devise a per-architecture-triple model, upon the assumption that an
OS and CPU combination are a good enough key to decide which formatters apply
(e.g. Linux i386 is probably different from masOS x86_64, but two macOS x86_64
targets will probably have the same formatters; of course versioning of the
underlying OS is also to be considered, but experience with OSX has shown that
formatters can take care of that internally in most cases of interest).

The public entry point is the ``DataVisualization`` layer.
``DataVisualization`` is a static class on which questions can be asked in a
relatively refactoring-safe manner.

The main question asked of it is to obtain formatters for ``ValueObjects`` (or
typenames). One can also query ``DataVisualization`` for named summaries or
individual categories, but of course those queries delve deeper in the internal
object model.

As said, the ``FormatManager`` holds a notion of revision number, which changes
every time formatters are edited (added, deleted, categories enabled or
disabled, ...). Through ``DataVisualization::ForceUpdate()`` one can cause the
same effects of a formatters edit to happen without it actually having
happened.

The main reason for this feature is that formatters can be dynamically created
in Python, and one can then enter the ``ScriptInterpreter`` and edit the
formatter function or class. If formatters were not updated, one could find
them to be out of sync with the new definitions of these objects. To avoid the
issue, whenever the user exits the scripting mode, formatters force an update
to make sure new potential definitions are reloaded on demand.

The SWIG Layer
--------------

In order to implement formatters written in Python, LLDB requires that
``ScriptInterpreter`` implementations provide a set of functions that one can call
to ask formatting questions of scripts.

For instance, in order to obtain a scripting summary, LLDB calls:

::

  virtual bool
  GetScriptedSummary(const char *function_name, llldb::ValueObjectSP valobj,
                     lldb::ScriptInterpreterObjectSP &callee_wrapper_sp,
                     std::string &retval)


For Python, this function is implemented by first checking if the
``callee_wrapper_sp`` is valid. If so, LLDB knows that it does not need to
search a function with the passed name, and can directly call the wrapped
Python function object. Either way, the call is routed to a global callback
``g_swig_typescript_callback``.

This callback pointer points to ``LLDBSwigPythonCallTypeScript``. The details
of the implementation require familiarity with the Python C API, plus a few
utility objects defined by LLDB to ease the burden of dealing with the
scripting world. However, as a sketch of what happens, the code tries to find a
Python function object with the given name (i.e. if you say ``type summary add
-F module.function`` LLDB will scan for the ``module`` module, and then for a
function named ``function`` inside the module's namespace). If the function
object is found, it is wrapped in a ``PyCallable``, which is an LLDB utility class
that wraps the callable and allows for easier calling. The callable gets
invoked, and the return value, if any, is cast into a string. Originally, if a
non-string object was returned, LLDB would refuse to use it. This disallowed
such simple construct as:

::

  def getSummary(value,*args):
    return 1

Similar considerations apply to other formatter (and non-formatter related)
scripting callbacks.
