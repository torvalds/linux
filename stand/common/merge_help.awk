#!/usr/bin/awk -f
#
# $FreeBSD$
#
# Merge two boot loader help files for FreeBSD 3.0
# Joe Abley <jabley@patho.gen.nz>

BEGIN \
{
  state = 0;
  first = -1;
  ind = 0;
}

# beginning of first command
/^###/ && (state == 0) \
{
  state = 1;
  next;
}

# entry header
/^# T[[:graph:]]+ (S[[:graph:]]+ )*D[[:graph:]][[:print:]]*$/ && (state == 1) \
{
  match($0, " T[[:graph:]]+");
  T = substr($0, RSTART + 2, RLENGTH - 2);
  match($0, " S[[:graph:]]+");
  SSTART = RSTART
  S = (RLENGTH == -1) ? "" : substr($0, RSTART + 2, RLENGTH - 2);
  match($0, " D[[:graph:]][[:print:]]*$");
  D = substr($0, RSTART + 2);
  if (SSTART > RSTART)
    S = "";

  # find a suitable place to store this one...
  ind++;
  if (ind == 1)
  {
    first = ind;
    help[ind, "T"] = T;
    help[ind, "S"] = S;
    help[ind, "link"] = -1;
  } else {
    i = first; j = -1;
    while (help[i, "T"] help[i, "S"] < T S)
    {
      j = i;
      i = help[i, "link"];
      if (i == -1) break;
    }

    if (i == -1)
    {
      help[j, "link"] = ind;
      help[ind, "link"] = -1;
    } else {
      help[ind, "link"] = i;
      if (j == -1)
        first = ind;
      else
        help[j, "link"] = ind;
    }
  }
  help[ind, "T"] = T;
  help[ind, "S"] = S;
  help[ind, "D"] = D;

  # set our state
  state = 2;
  help[ind, "text"] = 0;
  next;
}

# end of last command, beginning of next one
/^###/ && (state == 2) \
{
  state = 1;
}

(state == 2) \
{
  sub("[[:blank:]]+$", "");
  if (help[ind, "text"] == 0 && $0 ~ /^[[:blank:]]*$/) next;
  help[ind, "text", help[ind, "text"]] = $0;
  help[ind, "text"]++;
  next;
}

# show them what we have (it's already sorted in help[])
END \
{
  node = first;
  while (node != -1)
  {
    printf "################################################################################\n";
    printf "# T%s ", help[node, "T"];
    if (help[node, "S"] != "") printf "S%s ", help[node, "S"];
    printf "D%s\n\n", help[node, "D"];
    for (i = 0; i < help[node, "text"]; i++)
      printf "%s\n", help[node, "text", i];
    node = help[node, "link"];
  }
  printf "################################################################################\n";
}
