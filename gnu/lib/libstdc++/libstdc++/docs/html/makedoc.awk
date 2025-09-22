# Take apart bits of HTML and puts them back together again in new and
# fascinating ways.  Copyright (C) 2002 Free Software Foundation, Inc.
# Contributed by Phil Edwards <pme@gcc.gnu.org>.  Simple two-state automaton
# inspired by Richard Henderson's gcc/mkmap-symver.awk.

# 'file' is the name of the file on stdin
# 'title' is the text to print at the start of the list

BEGIN {
  state = "looking";
  entries = 0;
  printf ("   <li>%s\n", title);
  printf ("   <ul>\n");
}

# Searching for the little table of contents at the top.
state == "looking" && /^<h1>Contents/ {
  state = "entries";
  next;
}

# Ignore everything else up to that point.
state == "looking" {
  next;
}

# An entry in the table of contents.  Pull that line apart.
state == "entries" && /<li>/ {
  extract_info($0);
  next;
}

# End of the list.  Don't bother reading the rest of the file.  (It could
# also contain more <li>'s, so that would be incorrect as well as wasteful.)
state == "entries" && /^<\/ul>/ {
  exit;
}

END {
  for (i = 0; i < entries; i++)
    printf ("     %s\n", entry[i]);
  printf ("   </ul>\n   </li>\n\n");
}

function extract_info(line) {
  # thistarget will be things like "#5" or "elsewhere.html"
  match(line,"href=\".*\"");
  thistarget = substr(line,RSTART+6,RLENGTH-7);

  # take apart the filename
  split(file,X,"/");
  if (thistarget ~ /^#/) {
    # local name, use directory and filename
    target = file thistarget
  } else {
    # different file, only use directory
    target = X[1] "/" thistarget
  }

  # visible text
  gsub("</a></li>","",line);
  start = index(line,"\">") + 2;
  thistext = substr(line,start);

  # Assemble and store the HTML for later output.
  entry[entries++] = "<li><a href=\"" target "\">" thistext "</a></li>"
}

# vim:sw=2
