/*
 * Copyright (c) 2016-present, Przemyslaw Skibinski, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
using namespace std;


/* trim string at the beginning and at the end */
void trim(string& s, string characters)
{
    size_t p = s.find_first_not_of(characters);
    s.erase(0, p);

    p = s.find_last_not_of(characters);
    if (string::npos != p)
       s.erase(p+1);
}


/* trim C++ style comments */
void trim_comments(string &s)
{
    size_t spos, epos;

    spos = s.find("/*");
    epos = s.find("*/");
    s = s.substr(spos+3, epos-(spos+3));
}


/* get lines until a given terminator */
vector<string> get_lines(vector<string>& input, int& linenum, string terminator)
{
    vector<string> out;
    string line;
    size_t epos;

    while ((size_t)linenum < input.size()) {
        line = input[linenum];

        if (terminator.empty() && line.empty()) { linenum--; break; }

        epos = line.find(terminator);
        if (!terminator.empty() && epos!=string::npos) {
            out.push_back(line);
            break;
        }
        out.push_back(line);
        linenum++;
    }
    return out;
}


/* print line with ZSTDLIB_API removed and C++ comments not bold */
void print_line(stringstream &sout, string line)
{
    size_t spos;

    if (line.substr(0,12) == "ZSTDLIB_API ") line = line.substr(12);
    spos = line.find("/*");
    if (spos!=string::npos) {
        sout << line.substr(0, spos);
        sout << "</b>" << line.substr(spos) << "<b>" << endl;
    } else {
      //  fprintf(stderr, "lines=%s\n", line.c_str());
        sout << line << endl;
    }
}


int main(int argc, char *argv[]) {
    char exclam;
    int linenum, chapter = 1;
    vector<string> input, lines, comments, chapters;
    string line, version;
    size_t spos, l;
    stringstream sout;
    ifstream istream;
    ofstream ostream;

    if (argc < 4) {
        cout << "usage: " << argv[0] << " [zstd_version] [input_file] [output_html]" << endl;
        return 1;
    }

    version = "zstd " + string(argv[1]) + " Manual";

    istream.open(argv[2], ifstream::in);
    if (!istream.is_open()) {
        cout << "Error opening file " << argv[2] << endl;
        return 1;
    }

    ostream.open(argv[3], ifstream::out);
    if (!ostream.is_open()) {
        cout << "Error opening file " << argv[3] << endl;
        return 1;
   }

    while (getline(istream, line)) {
        input.push_back(line);
    }

    for (linenum=0; (size_t)linenum < input.size(); linenum++) {
        line = input[linenum];

        /* typedefs are detected and included even if uncommented */
        if (line.substr(0,7) == "typedef" && line.find("{")!=string::npos) {
            lines = get_lines(input, linenum, "}");
            sout << "<pre><b>";
            for (l=0; l<lines.size(); l++) {
                print_line(sout, lines[l]);
            }
            sout << "</b></pre><BR>" << endl;
            continue;
        }

        /* comments of type /**< and /*!< are detected and only function declaration is highlighted (bold) */
        if ((line.find("/**<")!=string::npos || line.find("/*!<")!=string::npos) && line.find("*/")!=string::npos) {
            sout << "<pre><b>";
            print_line(sout, line);
            sout << "</b></pre><BR>" << endl;
            continue;
        }

        spos = line.find("/**=");
        if (spos==string::npos) {
            spos = line.find("/*!");
            if (spos==string::npos)
                spos = line.find("/**");
            if (spos==string::npos)
                spos = line.find("/*-");
            if (spos==string::npos)
                spos = line.find("/*=");
            if (spos==string::npos)
                continue;
            exclam = line[spos+2];
        }
        else exclam = '=';

        comments = get_lines(input, linenum, "*/");
        if (!comments.empty()) comments[0] = line.substr(spos+3);
        if (!comments.empty()) comments[comments.size()-1] = comments[comments.size()-1].substr(0, comments[comments.size()-1].find("*/"));
        for (l=0; l<comments.size(); l++) {
            if (comments[l].find(" *")==0) comments[l] = comments[l].substr(2);
            else if (comments[l].find("  *")==0) comments[l] = comments[l].substr(3);
            trim(comments[l], "*-=");
        }
        while (!comments.empty() && comments[comments.size()-1].empty()) comments.pop_back(); // remove empty line at the end
        while (!comments.empty() && comments[0].empty()) comments.erase(comments.begin()); // remove empty line at the start

        /* comments of type /*! mean: this is a function declaration; switch comments with declarations */
        if (exclam == '!') {
            if (!comments.empty()) comments.erase(comments.begin()); /* remove first line like "ZSTD_XXX() :" */
            linenum++;
            lines = get_lines(input, linenum, "");

            sout << "<pre><b>";
            for (l=0; l<lines.size(); l++) {
              //  fprintf(stderr, "line[%d]=%s\n", l, lines[l].c_str());
                string fline = lines[l];
                if (fline.substr(0, 12) == "ZSTDLIB_API " ||
                    fline.substr(0, 12) == string(12, ' '))
                  fline = fline.substr(12);
                print_line(sout, fline);
            }
            sout << "</b><p>";
            for (l=0; l<comments.size(); l++) {
                print_line(sout, comments[l]);
            }
            sout << "</p></pre><BR>" << endl << endl;
        } else if (exclam == '=') { /* comments of type /*= and /**= mean: use a <H3> header and show also all functions until first empty line */
            trim(comments[0], " ");
            sout << "<h3>" << comments[0] << "</h3><pre>";
            for (l=1; l<comments.size(); l++) {
                print_line(sout, comments[l]);
            }
            sout << "</pre><b><pre>";
            lines = get_lines(input, ++linenum, "");
            for (l=0; l<lines.size(); l++) {
                print_line(sout, lines[l]);
            }
            sout << "</pre></b><BR>" << endl;
        } else { /* comments of type /** and /*- mean: this is a comment; use a <H2> header for the first line */
            if (comments.empty()) continue;

            trim(comments[0], " ");
            sout << "<a name=\"Chapter" << chapter << "\"></a><h2>" << comments[0] << "</h2><pre>";
            chapters.push_back(comments[0]);
            chapter++;

            for (l=1; l<comments.size(); l++) {
                print_line(sout, comments[l]);
            }
            if (comments.size() > 1)
                sout << "<BR></pre>" << endl << endl;
            else
                sout << "</pre>" << endl << endl;
        }
    }

    ostream << "<html>\n<head>\n<meta http-equiv=\"Content-Type\" content=\"text/html; charset=ISO-8859-1\">\n<title>" << version << "</title>\n</head>\n<body>" << endl;
    ostream << "<h1>" << version << "</h1>\n";

    ostream << "<hr>\n<a name=\"Contents\"></a><h2>Contents</h2>\n<ol>\n";
    for (size_t i=0; i<chapters.size(); i++)
        ostream << "<li><a href=\"#Chapter" << i+1 << "\">" << chapters[i].c_str() << "</a></li>\n";
    ostream << "</ol>\n<hr>\n";

    ostream << sout.str();
    ostream << "</html>" << endl << "</body>" << endl;

    return 0;
}
