#!/usr/bin/env python3
# A tool to parse ASTMatchers.h and update the documentation in
# ../LibASTMatchersReference.html automatically. Run from the
# directory in which this file is located to update the docs.

import collections
import re

try:
    from urllib.request import urlopen
except ImportError:
    from urllib2 import urlopen

CLASS_INDEX_PAGE_URL = "https://clang.llvm.org/doxygen/classes.html"
try:
    CLASS_INDEX_PAGE = urlopen(CLASS_INDEX_PAGE_URL).read().decode("utf-8")
except Exception as e:
    CLASS_INDEX_PAGE = None
    print("Unable to get %s: %s" % (CLASS_INDEX_PAGE_URL, e))

MATCHERS_FILE = "../../include/clang/ASTMatchers/ASTMatchers.h"

# Each matcher is documented in one row of the form:
#   result | name | argA
# The subsequent row contains the documentation and is hidden by default,
# becoming visible via javascript when the user clicks the matcher name.
TD_TEMPLATE = """
<tr><td>%(result)s</td><td class="name" onclick="toggle('%(id)s')"><a name="%(id)sAnchor">%(name)s</a></td><td>%(args)s</td></tr>
<tr><td colspan="4" class="doc" id="%(id)s"><pre>%(comment)s</pre></td></tr>
"""

# We categorize the matchers into these three categories in the reference:
node_matchers = {}
narrowing_matchers = {}
traversal_matchers = {}

# We output multiple rows per matcher if the matcher can be used on multiple
# node types. Thus, we need a new id per row to control the documentation
# pop-up. ids[name] keeps track of those ids.
ids = collections.defaultdict(int)

# Cache for doxygen urls we have already verified.
doxygen_probes = {}


def esc(text):
    """Escape any html in the given text."""
    text = re.sub(r"&", "&amp;", text)
    text = re.sub(r"<", "&lt;", text)
    text = re.sub(r">", "&gt;", text)

    def link_if_exists(m):
        """Wrap a likely AST node name in a link to its clang docs.

        We want to do this only if the page exists, in which case it will be
        referenced from the class index page.
        """
        name = m.group(1)
        url = "https://clang.llvm.org/doxygen/classclang_1_1%s.html" % name
        if url not in doxygen_probes:
            search_str = 'href="classclang_1_1%s.html"' % name
            if CLASS_INDEX_PAGE is not None:
                doxygen_probes[url] = search_str in CLASS_INDEX_PAGE
            else:
                doxygen_probes[url] = True
            if not doxygen_probes[url]:
                print("Did not find %s in class index page" % name)
        if doxygen_probes[url]:
            return r'Matcher&lt;<a href="%s">%s</a>&gt;' % (url, name)
        else:
            return m.group(0)

    text = re.sub(r"Matcher&lt;([^\*&]+)&gt;", link_if_exists, text)
    return text


def extract_result_types(comment):
    """Extracts a list of result types from the given comment.

    We allow annotations in the comment of the matcher to specify what
    nodes a matcher can match on. Those comments have the form:
      Usable as: Any Matcher | (Matcher<T1>[, Matcher<t2>[, ...]])

    Returns ['*'] in case of 'Any Matcher', or ['T1', 'T2', ...].
    Returns the empty list if no 'Usable as' specification could be
    parsed.
    """
    result_types = []
    m = re.search(r"Usable as: Any Matcher[\s\n]*$", comment, re.S)
    if m:
        return ["*"]
    while True:
        m = re.match(r"^(.*)Matcher<([^>]+)>\s*,?[\s\n]*$", comment, re.S)
        if not m:
            if re.search(r"Usable as:\s*$", comment):
                return result_types
            else:
                return None
        result_types += [m.group(2)]
        comment = m.group(1)


def strip_doxygen(comment):
    """Returns the given comment without \-escaped words."""
    # If there is only a doxygen keyword in the line, delete the whole line.
    comment = re.sub(r"^\\[^\s]+\n", r"", comment, flags=re.M)

    # If there is a doxygen \see command, change the \see prefix into "See also:".
    # FIXME: it would be better to turn this into a link to the target instead.
    comment = re.sub(r"\\see", r"See also:", comment)

    # Delete the doxygen command and the following whitespace.
    comment = re.sub(r"\\[^\s]+\s+", r"", comment)
    return comment


def unify_arguments(args):
    """Gets rid of anything the user doesn't care about in the argument list."""
    args = re.sub(r"clang::ast_matchers::internal::", r"", args)
    args = re.sub(r"ast_matchers::internal::", r"", args)
    args = re.sub(r"internal::", r"", args)
    args = re.sub(r"extern const\s+(.*)&", r"\1 ", args)
    args = re.sub(r"&", r" ", args)
    args = re.sub(r"(^|\s)M\d?(\s)", r"\1Matcher<*>\2", args)
    args = re.sub(r"BindableMatcher", r"Matcher", args)
    args = re.sub(r"const Matcher", r"Matcher", args)
    return args


def unify_type(result_type):
    """Gets rid of anything the user doesn't care about in the type name."""
    result_type = re.sub(
        r"^internal::(Bindable)?Matcher<([a-zA-Z_][a-zA-Z0-9_]*)>$", r"\2", result_type
    )
    return result_type


def add_matcher(result_type, name, args, comment, is_dyncast=False):
    """Adds a matcher to one of our categories."""
    if name == "id":
        # FIXME: Figure out whether we want to support the 'id' matcher.
        return
    matcher_id = "%s%d" % (name, ids[name])
    ids[name] += 1
    args = unify_arguments(args)
    result_type = unify_type(result_type)

    docs_result_type = esc("Matcher<%s>" % result_type)

    if name == "mapAnyOf":
        args = "nodeMatcherFunction..."
        docs_result_type = "<em>unspecified</em>"

    matcher_html = TD_TEMPLATE % {
        "result": docs_result_type,
        "name": name,
        "args": esc(args),
        "comment": esc(strip_doxygen(comment)),
        "id": matcher_id,
    }
    if is_dyncast:
        dict = node_matchers
        lookup = result_type + name
    # Use a heuristic to figure out whether a matcher is a narrowing or
    # traversal matcher. By default, matchers that take other matchers as
    # arguments (and are not node matchers) do traversal. We specifically
    # exclude known narrowing matchers that also take other matchers as
    # arguments.
    elif "Matcher<" not in args or name in [
        "allOf",
        "anyOf",
        "anything",
        "unless",
        "mapAnyOf",
    ]:
        dict = narrowing_matchers
        lookup = result_type + name + esc(args)
    else:
        dict = traversal_matchers
        lookup = result_type + name + esc(args)

    if dict.get(lookup) is None or len(dict.get(lookup)) < len(matcher_html):
        dict[lookup] = matcher_html


def act_on_decl(declaration, comment, allowed_types):
    """Parse the matcher out of the given declaration and comment.

    If 'allowed_types' is set, it contains a list of node types the matcher
    can match on, as extracted from the static type asserts in the matcher
    definition.
    """
    if declaration.strip():

        if re.match(r"^\s?(#|namespace|using|template <typename NodeType> using|})", declaration):
            return

        # Node matchers are defined by writing:
        #   VariadicDynCastAllOfMatcher<ResultType, ArgumentType> name;
        m = re.match(
            r""".*Variadic(?:DynCast)?AllOfMatcher\s*<
                       \s*([^\s,]+)\s*(?:,
                       \s*([^\s>]+)\s*)?>
                       \s*([^\s;]+)\s*;\s*$""",
            declaration,
            flags=re.X,
        )
        if m:
            result, inner, name = m.groups()
            if not inner:
                inner = result
            add_matcher(
                result, name, "Matcher<%s>..." % inner, comment, is_dyncast=True
            )
            return

        # Special case of type matchers:
        #   AstTypeMatcher<ArgumentType> name
        m = re.match(
            r""".*AstTypeMatcher\s*<
                       \s*([^\s>]+)\s*>
                       \s*([^\s;]+)\s*;\s*$""",
            declaration,
            flags=re.X,
        )
        if m:
            inner, name = m.groups()
            add_matcher(
                "Type", name, "Matcher<%s>..." % inner, comment, is_dyncast=True
            )
            # FIXME: re-enable once we have implemented casting on the TypeLoc
            # hierarchy.
            # add_matcher('TypeLoc', '%sLoc' % name, 'Matcher<%sLoc>...' % inner,
            #             comment, is_dyncast=True)
            return

        # Parse the various matcher definition macros.
        m = re.match(
            """.*AST_TYPE(LOC)?_TRAVERSE_MATCHER(?:_DECL)?\(
                       \s*([^\s,]+\s*),
                       \s*(?:[^\s,]+\s*),
                       \s*AST_POLYMORPHIC_SUPPORTED_TYPES\(([^)]*)\)
                     \)\s*;\s*$""",
            declaration,
            flags=re.X,
        )
        if m:
            loc, name, results = m.groups()[0:3]
            result_types = [r.strip() for r in results.split(",")]

            comment_result_types = extract_result_types(comment)
            if comment_result_types and sorted(result_types) != sorted(
                comment_result_types
            ):
                raise Exception("Inconsistent documentation for: %s" % name)
            for result_type in result_types:
                add_matcher(result_type, name, "Matcher<Type>", comment)
                # if loc:
                #   add_matcher('%sLoc' % result_type, '%sLoc' % name, 'Matcher<TypeLoc>',
                #               comment)
            return

        m = re.match(
            r"""^\s*AST_POLYMORPHIC_MATCHER(_P)?(.?)(?:_OVERLOAD)?\(
                          \s*([^\s,]+)\s*,
                          \s*AST_POLYMORPHIC_SUPPORTED_TYPES\(([^)]*)\)
                       (?:,\s*([^\s,]+)\s*
                          ,\s*([^\s,]+)\s*)?
                       (?:,\s*([^\s,]+)\s*
                          ,\s*([^\s,]+)\s*)?
                       (?:,\s*\d+\s*)?
                      \)\s*{\s*$""",
            declaration,
            flags=re.X,
        )

        if m:
            p, n, name, results = m.groups()[0:4]
            args = m.groups()[4:]
            result_types = [r.strip() for r in results.split(",")]
            if allowed_types and allowed_types != result_types:
                raise Exception("Inconsistent documentation for: %s" % name)
            if n not in ["", "2"]:
                raise Exception('Cannot parse "%s"' % declaration)
            args = ", ".join(
                "%s %s" % (args[i], args[i + 1])
                for i in range(0, len(args), 2)
                if args[i]
            )
            for result_type in result_types:
                add_matcher(result_type, name, args, comment)
            return

        m = re.match(
            r"""^\s*AST_POLYMORPHIC_MATCHER_REGEX(?:_OVERLOAD)?\(
                          \s*([^\s,]+)\s*,
                          \s*AST_POLYMORPHIC_SUPPORTED_TYPES\(([^)]*)\),
                          \s*([^\s,]+)\s*
                       (?:,\s*\d+\s*)?
                      \)\s*{\s*$""",
            declaration,
            flags=re.X,
        )

        if m:
            name, results, arg_name = m.groups()[0:3]
            result_types = [r.strip() for r in results.split(",")]
            if allowed_types and allowed_types != result_types:
                raise Exception("Inconsistent documentation for: %s" % name)
            arg = "StringRef %s, Regex::RegexFlags Flags = NoFlags" % arg_name
            comment += """
If the matcher is used in clang-query, RegexFlags parameter
should be passed as a quoted string. e.g: "NoFlags".
Flags can be combined with '|' example \"IgnoreCase | BasicRegex\"
"""
            for result_type in result_types:
                add_matcher(result_type, name, arg, comment)
            return

        m = re.match(
            r"""^\s*AST_MATCHER_FUNCTION(_P)?(.?)(?:_OVERLOAD)?\(
                       (?:\s*([^\s,]+)\s*,)?
                          \s*([^\s,]+)\s*
                       (?:,\s*([^\s,]+)\s*
                          ,\s*([^\s,]+)\s*)?
                       (?:,\s*([^\s,]+)\s*
                          ,\s*([^\s,]+)\s*)?
                       (?:,\s*\d+\s*)?
                      \)\s*{\s*$""",
            declaration,
            flags=re.X,
        )
        if m:
            p, n, result, name = m.groups()[0:4]
            args = m.groups()[4:]
            if n not in ["", "2"]:
                raise Exception('Cannot parse "%s"' % declaration)
            args = ", ".join(
                "%s %s" % (args[i], args[i + 1])
                for i in range(0, len(args), 2)
                if args[i]
            )
            add_matcher(result, name, args, comment)
            return

        m = re.match(
            r"""^\s*AST_MATCHER(_P)?(.?)(?:_OVERLOAD)?\(
                       (?:\s*([^\s,]+)\s*,)?
                          \s*([^\s,]+)\s*
                       (?:,\s*([^,]+)\s*
                          ,\s*([^\s,]+)\s*)?
                       (?:,\s*([^\s,]+)\s*
                          ,\s*([^\s,]+)\s*)?
                       (?:,\s*\d+\s*)?
                      \)\s*{""",
            declaration,
            flags=re.X,
        )
        if m:
            p, n, result, name = m.groups()[0:4]
            args = m.groups()[4:]
            if not result:
                if not allowed_types:
                    raise Exception("Did not find allowed result types for: %s" % name)
                result_types = allowed_types
            else:
                result_types = [result]
            if n not in ["", "2"]:
                raise Exception('Cannot parse "%s"' % declaration)
            args = ", ".join(
                "%s %s" % (args[i], args[i + 1])
                for i in range(0, len(args), 2)
                if args[i]
            )
            for result_type in result_types:
                add_matcher(result_type, name, args, comment)
            return

        m = re.match(
            r"""^\s*AST_MATCHER_REGEX(?:_OVERLOAD)?\(
                       \s*([^\s,]+)\s*,
                       \s*([^\s,]+)\s*,
                       \s*([^\s,]+)\s*
                       (?:,\s*\d+\s*)?
                      \)\s*{""",
            declaration,
            flags=re.X,
        )
        if m:
            result, name, arg_name = m.groups()[0:3]
            if not result:
                if not allowed_types:
                    raise Exception("Did not find allowed result types for: %s" % name)
                result_types = allowed_types
            else:
                result_types = [result]
            arg = "StringRef %s, Regex::RegexFlags Flags = NoFlags" % arg_name
            comment += """
If the matcher is used in clang-query, RegexFlags parameter
should be passed as a quoted string. e.g: "NoFlags".
Flags can be combined with '|' example \"IgnoreCase | BasicRegex\"
"""

            for result_type in result_types:
                add_matcher(result_type, name, arg, comment)
            return

        # Parse ArgumentAdapting matchers.
        m = re.match(
            r"""^.*ArgumentAdaptingMatcherFunc<.*>\s*
              ([a-zA-Z]*);$""",
            declaration,
            flags=re.X,
        )
        if m:
            name = m.groups()[0]
            add_matcher("*", name, "Matcher<*>", comment)
            return

        # Parse Variadic functions.
        m = re.match(
            r"""^.*internal::VariadicFunction\s*<\s*([^,]+),\s*([^,]+),\s*[^>]+>\s*
              ([a-zA-Z]*);$""",
            declaration,
            flags=re.X,
        )
        if m:
            result, arg, name = m.groups()[:3]
            add_matcher(result, name, "%s, ..., %s" % (arg, arg), comment)
            return

        m = re.match(
            r"""^.*internal::VariadicFunction\s*<\s*
              internal::PolymorphicMatcher<[\S\s]+
              AST_POLYMORPHIC_SUPPORTED_TYPES\(([^)]*)\),\s*(.*);$""",
            declaration,
            flags=re.X,
        )

        if m:
            results, trailing = m.groups()
            trailing, name = trailing.rsplit(">", 1)
            name = name.strip()
            trailing, _ = trailing.rsplit(",", 1)
            _, arg = trailing.rsplit(",", 1)
            arg = arg.strip()

            result_types = [r.strip() for r in results.split(",")]
            for result_type in result_types:
                add_matcher(result_type, name, "%s, ..., %s" % (arg, arg), comment)
            return

        # Parse Variadic operator matchers.
        m = re.match(
            r"""^.*VariadicOperatorMatcherFunc\s*<\s*([^,]+),\s*([^\s]+)\s*>\s*
              ([a-zA-Z]*);$""",
            declaration,
            flags=re.X,
        )
        if m:
            min_args, max_args, name = m.groups()[:3]
            if max_args == "1":
                add_matcher("*", name, "Matcher<*>", comment)
                return
            elif max_args == "std::numeric_limits<unsigned>::max()":
                add_matcher("*", name, "Matcher<*>, ..., Matcher<*>", comment)
                return

        m = re.match(
            r"""^.*MapAnyOfMatcher<.*>\s*
              ([a-zA-Z]*);$""",
            declaration,
            flags=re.X,
        )
        if m:
            name = m.groups()[0]
            add_matcher("*", name, "Matcher<*>...Matcher<*>", comment)
            return

        # Parse free standing matcher functions, like:
        #   Matcher<ResultType> Name(Matcher<ArgumentType> InnerMatcher) {
        m = re.match(
            r"""^\s*(?:template\s+<\s*(?:class|typename)\s+(.+)\s*>\s+)?
                     (.*)\s+
                     ([^\s\(]+)\s*\(
                     (.*)
                     \)\s*{""",
            declaration,
            re.X,
        )
        if m:
            template_name, result, name, args = m.groups()
            if template_name:
                matcherTemplateArgs = re.findall(
                    r"Matcher<\s*(%s)\s*>" % template_name, args
                )
                templateArgs = re.findall(
                    r"(?:^|[\s,<])(%s)(?:$|[\s,>])" % template_name, args
                )
                if len(matcherTemplateArgs) < len(templateArgs):
                    # The template name is used naked, so don't replace with `*`` later on
                    template_name = None
                else:
                    args = re.sub(
                        r"(^|[\s,<])%s($|[\s,>])" % template_name, r"\1*\2", args
                    )
            args = ", ".join(p.strip() for p in args.split(","))
            m = re.match(r"(?:^|.*\s+)internal::(?:Bindable)?Matcher<([^>]+)>$", result)
            if m:
                result_types = [m.group(1)]
                if (
                    template_name
                    and len(result_types) == 1
                    and result_types[0] == template_name
                ):
                    result_types = ["*"]
            else:
                result_types = extract_result_types(comment)
            if not result_types:
                if not comment:
                    # Only overloads don't have their own doxygen comments; ignore those.
                    print('Ignoring "%s"' % name)
                else:
                    print('Cannot determine result type for "%s"' % name)
            else:
                for result_type in result_types:
                    add_matcher(result_type, name, args, comment)
        else:
            print('*** Unparsable: "' + declaration + '" ***')


def sort_table(matcher_type, matcher_map):
    """Returns the sorted html table for the given row map."""
    table = ""
    for key in sorted(matcher_map.keys()):
        table += matcher_map[key] + "\n"
    return (
        "<!-- START_%(type)s_MATCHERS -->\n"
        + "%(table)s"
        + "<!--END_%(type)s_MATCHERS -->"
    ) % {
        "type": matcher_type,
        "table": table,
    }


# Parse the ast matchers.
# We alternate between two modes:
# body = True: We parse the definition of a matcher. We need
#   to parse the full definition before adding a matcher, as the
#   definition might contain static asserts that specify the result
#   type.
# body = False: We parse the comments and declaration of the matcher.
comment = ""
declaration = ""
allowed_types = []
body = False
for line in open(MATCHERS_FILE).read().splitlines():
    if body:
        if line.strip() and line[0] == "}":
            if declaration:
                act_on_decl(declaration, comment, allowed_types)
                comment = ""
                declaration = ""
                allowed_types = []
            body = False
        else:
            m = re.search(r"is_base_of<([^,]+), NodeType>", line)
            if m and m.group(1):
                allowed_types += [m.group(1)]
        continue
    if line.strip() and line.lstrip()[0] == "/":
        comment += re.sub(r"^/+\s?", "", line) + "\n"
    else:
        declaration += " " + line
        if (
            (not line.strip())
            or line.rstrip()[-1] == ";"
            or (line.rstrip()[-1] == "{" and line.rstrip()[-3:] != "= {")
        ):
            if line.strip() and line.rstrip()[-1] == "{":
                body = True
            else:
                act_on_decl(declaration, comment, allowed_types)
                comment = ""
                declaration = ""
                allowed_types = []

node_matcher_table = sort_table("DECL", node_matchers)
narrowing_matcher_table = sort_table("NARROWING", narrowing_matchers)
traversal_matcher_table = sort_table("TRAVERSAL", traversal_matchers)

reference = open("../LibASTMatchersReference.html").read()
reference = re.sub(
    r"<!-- START_DECL_MATCHERS.*END_DECL_MATCHERS -->",
    node_matcher_table,
    reference,
    flags=re.S,
)
reference = re.sub(
    r"<!-- START_NARROWING_MATCHERS.*END_NARROWING_MATCHERS -->",
    narrowing_matcher_table,
    reference,
    flags=re.S,
)
reference = re.sub(
    r"<!-- START_TRAVERSAL_MATCHERS.*END_TRAVERSAL_MATCHERS -->",
    traversal_matcher_table,
    reference,
    flags=re.S,
)

with open("../LibASTMatchersReference.html", "w", newline="\n") as output:
    output.write(reference)
