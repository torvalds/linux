import re
from . import common
import sys

if sys.version_info[0] > 2:

    class string:
        expandtabs = str.expandtabs

else:
    import string

# Support of isel debug checks
# RegEx: this is where the magic happens.

##### iSel parser

# TODO: add function prefix
ISEL_FUNCTION_DEFAULT_RE = re.compile(
    r"Selected[\s]*selection[\s]*DAG:[\s]*%bb.0[\s]*\'(?P<func>.*?):[^\']*\'*\n"
    r"(?P<body>.*?)\n"
    r"Total[\s]*amount[\s]*of[\s]*phi[\s]*nodes[\s]*to[\s]*update:[\s]*[0-9]+",
    flags=(re.M | re.S),
)


def scrub_isel_default(isel, args):
    # Scrub runs of whitespace out of the iSel debug output, but leave the leading
    # whitespace in place.
    isel = common.SCRUB_WHITESPACE_RE.sub(r" ", isel)
    # Expand the tabs used for indentation.
    isel = string.expandtabs(isel, 2)
    # Strip trailing whitespace.
    isel = common.SCRUB_TRAILING_WHITESPACE_RE.sub(r"", isel)
    return isel


def get_run_handler(triple):
    target_handlers = {}
    handler = None
    best_prefix = ""
    for prefix, s in target_handlers.items():
        if triple.startswith(prefix) and len(prefix) > len(best_prefix):
            handler = s
            best_prefix = prefix

    if handler is None:
        common.debug("Using default handler.")
        handler = (scrub_isel_default, ISEL_FUNCTION_DEFAULT_RE)

    return handler


##### Generator of iSel CHECK lines


def add_checks(
    output_lines,
    comment_marker,
    prefix_list,
    func_dict,
    func_name,
    ginfo: common.GeneralizerInfo,
    global_vars_seen_dict,
    is_filtered,
):
    # Label format is based on iSel string.
    check_label_format = "{} %s-LABEL: %s%s%s%s".format(comment_marker)
    return common.add_checks(
        output_lines,
        comment_marker,
        prefix_list,
        func_dict,
        func_name,
        check_label_format,
        ginfo,
        global_vars_seen_dict,
        is_filtered=is_filtered,
    )
