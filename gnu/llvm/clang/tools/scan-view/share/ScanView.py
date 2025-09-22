from __future__ import print_function

try:
    from http.server import HTTPServer, SimpleHTTPRequestHandler
except ImportError:
    from BaseHTTPServer import HTTPServer
    from SimpleHTTPServer import SimpleHTTPRequestHandler
import os
import sys

try:
    from urlparse import urlparse
    from urllib import unquote
except ImportError:
    from urllib.parse import urlparse, unquote

import posixpath

if sys.version_info.major >= 3:
    from io import StringIO, BytesIO
else:
    from io import BytesIO, BytesIO as StringIO

import re
import shutil
import threading
import time
import socket
import itertools

import Reporter

try:
    import configparser
except ImportError:
    import ConfigParser as configparser

###
# Various patterns matched or replaced by server.

kReportFileRE = re.compile("(.*/)?report-(.*)\\.html")

kBugKeyValueRE = re.compile("<!-- BUG([^ ]*) (.*) -->")

#  <!-- REPORTPROBLEM file="crashes/clang_crash_ndSGF9.mi" stderr="crashes/clang_crash_ndSGF9.mi.stderr.txt" info="crashes/clang_crash_ndSGF9.mi.info" -->

kReportCrashEntryRE = re.compile("<!-- REPORTPROBLEM (.*?)-->")
kReportCrashEntryKeyValueRE = re.compile(' ?([^=]+)="(.*?)"')

kReportReplacements = []

# Add custom javascript.
kReportReplacements.append(
    (
        re.compile("<!-- SUMMARYENDHEAD -->"),
        """\
<script language="javascript" type="text/javascript">
function load(url) {
  if (window.XMLHttpRequest) {
    req = new XMLHttpRequest();
  } else if (window.ActiveXObject) {
    req = new ActiveXObject("Microsoft.XMLHTTP");
  }
  if (req != undefined) {
    req.open("GET", url, true);
    req.send("");
  }
}
</script>""",
    )
)

# Insert additional columns.
kReportReplacements.append((re.compile("<!-- REPORTBUGCOL -->"), "<td></td><td></td>"))

# Insert report bug and open file links.
kReportReplacements.append(
    (
        re.compile('<!-- REPORTBUG id="report-(.*)\\.html" -->'),
        (
            '<td class="Button"><a href="report/\\1">Report Bug</a></td>'
            + '<td class="Button"><a href="javascript:load(\'open/\\1\')">Open File</a></td>'
        ),
    )
)

kReportReplacements.append(
    (
        re.compile("<!-- REPORTHEADER -->"),
        '<h3><a href="/">Summary</a> > Report %(report)s</h3>',
    )
)

kReportReplacements.append(
    (
        re.compile("<!-- REPORTSUMMARYEXTRA -->"),
        '<td class="Button"><a href="report/%(report)s">Report Bug</a></td>',
    )
)

# Insert report crashes link.

# Disabled for the time being until we decide exactly when this should
# be enabled. Also the radar reporter needs to be fixed to report
# multiple files.

# kReportReplacements.append((re.compile('<!-- REPORTCRASHES -->'),
#                            '<br>These files will automatically be attached to ' +
#                            'reports filed here: <a href="report_crashes">Report Crashes</a>.'))

###
# Other simple parameters

kShare = posixpath.join(posixpath.dirname(__file__), "../share/scan-view")
kConfigPath = os.path.expanduser("~/.scanview.cfg")

###

__version__ = "0.1"

__all__ = ["create_server"]


class ReporterThread(threading.Thread):
    def __init__(self, report, reporter, parameters, server):
        threading.Thread.__init__(self)
        self.report = report
        self.server = server
        self.reporter = reporter
        self.parameters = parameters
        self.success = False
        self.status = None

    def run(self):
        result = None
        try:
            if self.server.options.debug:
                print("%s: SERVER: submitting bug." % (sys.argv[0],), file=sys.stderr)
            self.status = self.reporter.fileReport(self.report, self.parameters)
            self.success = True
            time.sleep(3)
            if self.server.options.debug:
                print(
                    "%s: SERVER: submission complete." % (sys.argv[0],), file=sys.stderr
                )
        except Reporter.ReportFailure as e:
            self.status = e.value
        except Exception as e:
            s = StringIO()
            import traceback

            print("<b>Unhandled Exception</b><br><pre>", file=s)
            traceback.print_exc(file=s)
            print("</pre>", file=s)
            self.status = s.getvalue()


class ScanViewServer(HTTPServer):
    def __init__(self, address, handler, root, reporters, options):
        HTTPServer.__init__(self, address, handler)
        self.root = root
        self.reporters = reporters
        self.options = options
        self.halted = False
        self.config = None
        self.load_config()

    def load_config(self):
        self.config = configparser.RawConfigParser()

        # Add defaults
        self.config.add_section("ScanView")
        for r in self.reporters:
            self.config.add_section(r.getName())
            for p in r.getParameters():
                if p.saveConfigValue():
                    self.config.set(r.getName(), p.getName(), "")

        # Ignore parse errors
        try:
            self.config.read([kConfigPath])
        except:
            pass

        # Save on exit
        import atexit

        atexit.register(lambda: self.save_config())

    def save_config(self):
        # Ignore errors (only called on exit).
        try:
            f = open(kConfigPath, "w")
            self.config.write(f)
            f.close()
        except:
            pass

    def halt(self):
        self.halted = True
        if self.options.debug:
            print("%s: SERVER: halting." % (sys.argv[0],), file=sys.stderr)

    def serve_forever(self):
        while not self.halted:
            if self.options.debug > 1:
                print("%s: SERVER: waiting..." % (sys.argv[0],), file=sys.stderr)
            try:
                self.handle_request()
            except OSError as e:
                print("OSError", e.errno)

    def finish_request(self, request, client_address):
        if self.options.autoReload:
            import ScanView

            self.RequestHandlerClass = reload(ScanView).ScanViewRequestHandler
        HTTPServer.finish_request(self, request, client_address)

    def handle_error(self, request, client_address):
        # Ignore socket errors
        info = sys.exc_info()
        if info and isinstance(info[1], socket.error):
            if self.options.debug > 1:
                print(
                    "%s: SERVER: ignored socket error." % (sys.argv[0],),
                    file=sys.stderr,
                )
            return
        HTTPServer.handle_error(self, request, client_address)


# Borrowed from Quixote, with simplifications.
def parse_query(qs, fields=None):
    if fields is None:
        fields = {}
    for chunk in (_f for _f in qs.split("&") if _f):
        if "=" not in chunk:
            name = chunk
            value = ""
        else:
            name, value = chunk.split("=", 1)
        name = unquote(name.replace("+", " "))
        value = unquote(value.replace("+", " "))
        item = fields.get(name)
        if item is None:
            fields[name] = [value]
        else:
            item.append(value)
    return fields


class ScanViewRequestHandler(SimpleHTTPRequestHandler):
    server_version = "ScanViewServer/" + __version__
    dynamic_mtime = time.time()

    def do_HEAD(self):
        try:
            SimpleHTTPRequestHandler.do_HEAD(self)
        except Exception as e:
            self.handle_exception(e)

    def do_GET(self):
        try:
            SimpleHTTPRequestHandler.do_GET(self)
        except Exception as e:
            self.handle_exception(e)

    def do_POST(self):
        """Serve a POST request."""
        try:
            length = self.headers.getheader("content-length") or "0"
            try:
                length = int(length)
            except:
                length = 0
            content = self.rfile.read(length)
            fields = parse_query(content)
            f = self.send_head(fields)
            if f:
                self.copyfile(f, self.wfile)
                f.close()
        except Exception as e:
            self.handle_exception(e)

    def log_message(self, format, *args):
        if self.server.options.debug:
            sys.stderr.write(
                "%s: SERVER: %s - - [%s] %s\n"
                % (
                    sys.argv[0],
                    self.address_string(),
                    self.log_date_time_string(),
                    format % args,
                )
            )

    def load_report(self, report):
        path = os.path.join(self.server.root, "report-%s.html" % report)
        data = open(path).read()
        keys = {}
        for item in kBugKeyValueRE.finditer(data):
            k, v = item.groups()
            keys[k] = v
        return keys

    def load_crashes(self):
        path = posixpath.join(self.server.root, "index.html")
        data = open(path).read()
        problems = []
        for item in kReportCrashEntryRE.finditer(data):
            fieldData = item.group(1)
            fields = dict(
                [i.groups() for i in kReportCrashEntryKeyValueRE.finditer(fieldData)]
            )
            problems.append(fields)
        return problems

    def handle_exception(self, exc):
        import traceback

        s = StringIO()
        print("INTERNAL ERROR\n", file=s)
        traceback.print_exc(file=s)
        f = self.send_string(s.getvalue(), "text/plain")
        if f:
            self.copyfile(f, self.wfile)
            f.close()

    def get_scalar_field(self, name):
        if name in self.fields:
            return self.fields[name][0]
        else:
            return None

    def submit_bug(self, c):
        title = self.get_scalar_field("title")
        description = self.get_scalar_field("description")
        report = self.get_scalar_field("report")
        reporterIndex = self.get_scalar_field("reporter")
        files = []
        for fileID in self.fields.get("files", []):
            try:
                i = int(fileID)
            except:
                i = None
            if i is None or i < 0 or i >= len(c.files):
                return (False, "Invalid file ID")
            files.append(c.files[i])

        if not title:
            return (False, "Missing title.")
        if not description:
            return (False, "Missing description.")
        try:
            reporterIndex = int(reporterIndex)
        except:
            return (False, "Invalid report method.")

        # Get the reporter and parameters.
        reporter = self.server.reporters[reporterIndex]
        parameters = {}
        for o in reporter.getParameters():
            name = "%s_%s" % (reporter.getName(), o.getName())
            if name not in self.fields:
                return (
                    False,
                    'Missing field "%s" for %s report method.'
                    % (name, reporter.getName()),
                )
            parameters[o.getName()] = self.get_scalar_field(name)

        # Update config defaults.
        if report != "None":
            self.server.config.set("ScanView", "reporter", reporterIndex)
            for o in reporter.getParameters():
                if o.saveConfigValue():
                    name = o.getName()
                    self.server.config.set(reporter.getName(), name, parameters[name])

        # Create the report.
        bug = Reporter.BugReport(title, description, files)

        # Kick off a reporting thread.
        t = ReporterThread(bug, reporter, parameters, self.server)
        t.start()

        # Wait for thread to die...
        while t.isAlive():
            time.sleep(0.25)
        submitStatus = t.status

        return (t.success, t.status)

    def send_report_submit(self):
        report = self.get_scalar_field("report")
        c = self.get_report_context(report)
        if c.reportSource is None:
            reportingFor = "Report Crashes > "
            fileBug = (
                """\
<a href="/report_crashes">File Bug</a> > """
                % locals()
            )
        else:
            reportingFor = '<a href="/%s">Report %s</a> > ' % (c.reportSource, report)
            fileBug = '<a href="/report/%s">File Bug</a> > ' % report
        title = self.get_scalar_field("title")
        description = self.get_scalar_field("description")

        res, message = self.submit_bug(c)

        if res:
            statusClass = "SubmitOk"
            statusName = "Succeeded"
        else:
            statusClass = "SubmitFail"
            statusName = "Failed"

        result = (
            """
<head>
  <title>Bug Submission</title>
  <link rel="stylesheet" type="text/css" href="/scanview.css" />
</head>
<body>
<h3>
<a href="/">Summary</a> > 
%(reportingFor)s
%(fileBug)s
Submit</h3>
<form name="form" action="">
<table class="form">
<tr><td>
<table class="form_group">
<tr>
  <td class="form_clabel">Title:</td>
  <td class="form_value">
    <input type="text" name="title" size="50" value="%(title)s" disabled>
  </td>
</tr>
<tr>
  <td class="form_label">Description:</td>
  <td class="form_value">
<textarea rows="10" cols="80" name="description" disabled>
%(description)s
</textarea>
  </td>
</table>
</td></tr>
</table>
</form>
<h1 class="%(statusClass)s">Submission %(statusName)s</h1>
%(message)s
<p>
<hr>
<a href="/">Return to Summary</a>
</body>
</html>"""
            % locals()
        )
        return self.send_string(result)

    def send_open_report(self, report):
        try:
            keys = self.load_report(report)
        except IOError:
            return self.send_error(400, "Invalid report.")

        file = keys.get("FILE")
        if not file or not posixpath.exists(file):
            return self.send_error(400, 'File does not exist: "%s"' % file)

        import startfile

        if self.server.options.debug:
            print('%s: SERVER: opening "%s"' % (sys.argv[0], file), file=sys.stderr)

        status = startfile.open(file)
        if status:
            res = 'Opened: "%s"' % file
        else:
            res = 'Open failed: "%s"' % file

        return self.send_string(res, "text/plain")

    def get_report_context(self, report):
        class Context(object):
            pass

        if report is None or report == "None":
            data = self.load_crashes()
            # Don't allow empty reports.
            if not data:
                raise ValueError("No crashes detected!")
            c = Context()
            c.title = "clang static analyzer failures"

            stderrSummary = ""
            for item in data:
                if "stderr" in item:
                    path = posixpath.join(self.server.root, item["stderr"])
                    if os.path.exists(path):
                        lns = itertools.islice(open(path), 0, 10)
                        stderrSummary += "%s\n--\n%s" % (
                            item.get("src", "<unknown>"),
                            "".join(lns),
                        )

            c.description = """\
The clang static analyzer failed on these inputs:
%s

STDERR Summary
--------------
%s
""" % (
                "\n".join([item.get("src", "<unknown>") for item in data]),
                stderrSummary,
            )
            c.reportSource = None
            c.navMarkup = "Report Crashes > "
            c.files = []
            for item in data:
                c.files.append(item.get("src", ""))
                c.files.append(posixpath.join(self.server.root, item.get("file", "")))
                c.files.append(
                    posixpath.join(self.server.root, item.get("clangfile", ""))
                )
                c.files.append(posixpath.join(self.server.root, item.get("stderr", "")))
                c.files.append(posixpath.join(self.server.root, item.get("info", "")))
            # Just in case something failed, ignore files which don't
            # exist.
            c.files = [f for f in c.files if os.path.exists(f) and os.path.isfile(f)]
        else:
            # Check that this is a valid report.
            path = posixpath.join(self.server.root, "report-%s.html" % report)
            if not posixpath.exists(path):
                raise ValueError("Invalid report ID")
            keys = self.load_report(report)
            c = Context()
            c.title = keys.get("DESC", "clang error (unrecognized")
            c.description = """\
Bug reported by the clang static analyzer.

Description: %s
File: %s
Line: %s
""" % (
                c.title,
                keys.get("FILE", "<unknown>"),
                keys.get("LINE", "<unknown>"),
            )
            c.reportSource = "report-%s.html" % report
            c.navMarkup = """<a href="/%s">Report %s</a> > """ % (
                c.reportSource,
                report,
            )

            c.files = [path]
        return c

    def send_report(self, report, configOverrides=None):
        def getConfigOption(section, field):
            if (
                configOverrides is not None
                and section in configOverrides
                and field in configOverrides[section]
            ):
                return configOverrides[section][field]
            return self.server.config.get(section, field)

        # report is None is used for crashes
        try:
            c = self.get_report_context(report)
        except ValueError as e:
            return self.send_error(400, e.message)

        title = c.title
        description = c.description
        reportingFor = c.navMarkup
        if c.reportSource is None:
            extraIFrame = ""
        else:
            extraIFrame = """\
<iframe src="/%s" width="100%%" height="40%%"
        scrolling="auto" frameborder="1">
  <a href="/%s">View Bug Report</a>
</iframe>""" % (
                c.reportSource,
                c.reportSource,
            )

        reporterSelections = []
        reporterOptions = []

        try:
            active = int(getConfigOption("ScanView", "reporter"))
        except:
            active = 0
        for i, r in enumerate(self.server.reporters):
            selected = i == active
            if selected:
                selectedStr = " selected"
            else:
                selectedStr = ""
            reporterSelections.append(
                '<option value="%d"%s>%s</option>' % (i, selectedStr, r.getName())
            )
            options = "\n".join(
                [o.getHTML(r, title, getConfigOption) for o in r.getParameters()]
            )
            display = ("none", "")[selected]
            reporterOptions.append(
                """\
<tr id="%sReporterOptions" style="display:%s">
  <td class="form_label">%s Options</td>
  <td class="form_value">
    <table class="form_inner_group">
%s
    </table>
  </td>
</tr>
"""
                % (r.getName(), display, r.getName(), options)
            )
        reporterSelections = "\n".join(reporterSelections)
        reporterOptionsDivs = "\n".join(reporterOptions)
        reportersArray = "[%s]" % (
            ",".join([repr(r.getName()) for r in self.server.reporters])
        )

        if c.files:
            fieldSize = min(5, len(c.files))
            attachFileOptions = "\n".join(
                [
                    """\
<option value="%d" selected>%s</option>"""
                    % (i, v)
                    for i, v in enumerate(c.files)
                ]
            )
            attachFileRow = """\
<tr>
  <td class="form_label">Attach:</td>
  <td class="form_value">
<select style="width:100%%" name="files" multiple size=%d>
%s
</select>
  </td>
</tr>
""" % (
                min(5, len(c.files)),
                attachFileOptions,
            )
        else:
            attachFileRow = ""

        result = (
            """<html>
<head>
  <title>File Bug</title>
  <link rel="stylesheet" type="text/css" href="/scanview.css" />
</head>
<script language="javascript" type="text/javascript">
var reporters = %(reportersArray)s;
function updateReporterOptions() {
  index = document.getElementById('reporter').selectedIndex;
  for (var i=0; i < reporters.length; ++i) {
    o = document.getElementById(reporters[i] + "ReporterOptions");
    if (i == index) {
      o.style.display = "";
    } else {
      o.style.display = "none";
    }
  }
}
</script>
<body onLoad="updateReporterOptions()">
<h3>
<a href="/">Summary</a> > 
%(reportingFor)s
File Bug</h3>
<form name="form" action="/report_submit" method="post">
<input type="hidden" name="report" value="%(report)s">

<table class="form">
<tr><td>
<table class="form_group">
<tr>
  <td class="form_clabel">Title:</td>
  <td class="form_value">
    <input type="text" name="title" size="50" value="%(title)s">
  </td>
</tr>
<tr>
  <td class="form_label">Description:</td>
  <td class="form_value">
<textarea rows="10" cols="80" name="description">
%(description)s
</textarea>
  </td>
</tr>

%(attachFileRow)s

</table>
<br>
<table class="form_group">
<tr>
  <td class="form_clabel">Method:</td>
  <td class="form_value">
    <select id="reporter" name="reporter" onChange="updateReporterOptions()">
    %(reporterSelections)s
    </select>
  </td>
</tr>
%(reporterOptionsDivs)s
</table>
<br>
</td></tr>
<tr><td class="form_submit">
  <input align="right" type="submit" name="Submit" value="Submit">
</td></tr>
</table>
</form>

%(extraIFrame)s

</body>
</html>"""
            % locals()
        )

        return self.send_string(result)

    def send_head(self, fields=None):
        if self.server.options.onlyServeLocal and self.client_address[0] != "127.0.0.1":
            return self.send_error(401, "Unauthorized host.")

        if fields is None:
            fields = {}
        self.fields = fields

        o = urlparse(self.path)
        self.fields = parse_query(o.query, fields)
        path = posixpath.normpath(unquote(o.path))

        # Split the components and strip the root prefix.
        components = path.split("/")[1:]

        # Special case some top-level entries.
        if components:
            name = components[0]
            if len(components) == 2:
                if name == "report":
                    return self.send_report(components[1])
                elif name == "open":
                    return self.send_open_report(components[1])
            elif len(components) == 1:
                if name == "quit":
                    self.server.halt()
                    return self.send_string("Goodbye.", "text/plain")
                elif name == "report_submit":
                    return self.send_report_submit()
                elif name == "report_crashes":
                    overrides = {"ScanView": {}, "Radar": {}, "Email": {}}
                    for i, r in enumerate(self.server.reporters):
                        if r.getName() == "Radar":
                            overrides["ScanView"]["reporter"] = i
                            break
                    overrides["Radar"]["Component"] = "llvm - checker"
                    overrides["Radar"]["Component Version"] = "X"
                    return self.send_report(None, overrides)
                elif name == "favicon.ico":
                    return self.send_path(posixpath.join(kShare, "bugcatcher.ico"))

        # Match directory entries.
        if components[-1] == "":
            components[-1] = "index.html"

        relpath = "/".join(components)
        path = posixpath.join(self.server.root, relpath)

        if self.server.options.debug > 1:
            print(
                '%s: SERVER: sending path "%s"' % (sys.argv[0], path), file=sys.stderr
            )
        return self.send_path(path)

    def send_404(self):
        self.send_error(404, "File not found")
        return None

    def send_path(self, path):
        # If the requested path is outside the root directory, do not open it
        rel = os.path.abspath(path)
        if not rel.startswith(os.path.abspath(self.server.root)):
            return self.send_404()

        ctype = self.guess_type(path)
        if ctype.startswith("text/"):
            # Patch file instead
            return self.send_patched_file(path, ctype)
        else:
            mode = "rb"
        try:
            f = open(path, mode)
        except IOError:
            return self.send_404()
        return self.send_file(f, ctype)

    def send_file(self, f, ctype):
        # Patch files to add links, but skip binary files.
        self.send_response(200)
        self.send_header("Content-type", ctype)
        fs = os.fstat(f.fileno())
        self.send_header("Content-Length", str(fs[6]))
        self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
        self.end_headers()
        return f

    def send_string(self, s, ctype="text/html", headers=True, mtime=None):
        encoded_s = s.encode("utf-8")
        if headers:
            self.send_response(200)
            self.send_header("Content-type", ctype)
            self.send_header("Content-Length", str(len(encoded_s)))
            if mtime is None:
                mtime = self.dynamic_mtime
            self.send_header("Last-Modified", self.date_time_string(mtime))
            self.end_headers()
        return BytesIO(encoded_s)

    def send_patched_file(self, path, ctype):
        # Allow a very limited set of variables. This is pretty gross.
        variables = {}
        variables["report"] = ""
        m = kReportFileRE.match(path)
        if m:
            variables["report"] = m.group(2)

        try:
            f = open(path, "rb")
        except IOError:
            return self.send_404()
        fs = os.fstat(f.fileno())
        data = f.read().decode("utf-8")
        for a, b in kReportReplacements:
            data = a.sub(b % variables, data)
        return self.send_string(data, ctype, mtime=fs.st_mtime)


def create_server(address, options, root):
    import Reporter

    reporters = Reporter.getReporters()

    return ScanViewServer(address, ScanViewRequestHandler, root, reporters, options)
