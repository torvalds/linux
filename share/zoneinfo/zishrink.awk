# Convert tzdata source into a smaller version of itself.

# Contributed by Paul Eggert.  This file is in the public domain.

# This is not a general-purpose converter; it is designed for current tzdata.
# 'zic' should treat this script's output as if it were identical to
# this script's input.

# Record a hash N for the new name NAME, checking for collisions.

function record_hash(n, name)
{
  if (used_hashes[n]) {
    printf "# ! collision: %s %s\n", used_hashes[n], name
    exit 1
  }
  used_hashes[n] = name
}

# Return a shortened rule name representing NAME,
# and record this relationship to the hash table.

function gen_rule_name(name, \
		       n)
{
  # Use a simple mnemonic: the first two letters.
  n = substr(name, 1, 2)
  record_hash(n, name)
  # printf "# %s = %s\n", n, name
  return n
}

function prehash_rule_names( \
			    name)
{
  # Rule names are not part of the tzdb API, so substitute shorter
  # ones.  Shortening them consistently from one release to the next
  # simplifies comparison of the output.  That being said, the
  # 1-letter names below are not standardized in any way, and can
  # change arbitrarily from one release to the next, as the main goal
  # here is compression not comparison.

  # Abbreviating these rules names to one letter saved the most space
  # circa 2018e.
  rule["Arg"] = "A"
  rule["Brazil"] = "B"
  rule["Canada"] = "C"
  rule["Denmark"] = "D"
  rule["EU"] = "E"
  rule["France"] = "F"
  rule["GB-Eire"] = "G"
  rule["Halifax"] = "H"
  rule["Italy"] = "I"
  rule["Jordan"] = "J"
  rule["Egypt"] = "K" # "Kemet" in ancient Egyptian
  rule["Libya"] = "L"
  rule["Morocco"] = "M"
  rule["Neth"] = "N"
  rule["Poland"] = "O" # arbitrary
  rule["Palestine"] = "P"
  rule["Cuba"] = "Q" # Its start sounds like "Q".
  rule["Russia"] = "R"
  rule["Syria"] = "S"
  rule["Turkey"] = "T"
  rule["Uruguay"] = "U"
  rule["Vincennes"] = "V"
  rule["Winn"] = "W"
  rule["Mongol"] = "X" # arbitrary
  rule["NT_YK"] = "Y"
  rule["Zion"] = "Z"
  rule["Austria"] = "a"
  rule["Belgium"] = "b"
  rule["C-Eur"] = "c"
  rule["Algeria"] = "d" # country code DZ
  rule["E-Eur"] = "e"
  rule["Taiwan"] = "f" # Formosa
  rule["Greece"] = "g"
  rule["Hungary"] = "h"
  rule["Iran"] = "i"
  rule["StJohns"] = "j"
  rule["Chatham"] = "k" # arbitrary
  rule["Lebanon"] = "l"
  rule["Mexico"] = "m"
  rule["Tunisia"] = "n" # country code TN
  rule["Moncton"] = "o" # arbitrary
  rule["Port"] = "p"
  rule["Albania"] = "q" # arbitrary
  rule["Regina"] = "r"
  rule["Spain"] = "s"
  rule["Toronto"] = "t"
  rule["US"] = "u"
  rule["Louisville"] = "v" # ville
  rule["Iceland"] = "w" # arbitrary
  rule["Chile"] = "x" # arbitrary
  rule["Para"] = "y" # country code PY
  rule["Romania"] = "z" # arbitrary
  rule["Macau"] = "_" # arbitrary

  # Use ISO 3166 alpha-2 country codes for remaining names that are countries.
  # This is more systematic, and avoids collisions (e.g., Malta and Moldova).
  rule["Armenia"] = "AM"
  rule["Aus"] = "AU"
  rule["Azer"] = "AZ"
  rule["Barb"] = "BB"
  rule["Dhaka"] = "BD"
  rule["Bulg"] = "BG"
  rule["Bahamas"] = "BS"
  rule["Belize"] = "BZ"
  rule["Swiss"] = "CH"
  rule["Cook"] = "CK"
  rule["PRC"] = "CN"
  rule["Cyprus"] = "CY"
  rule["Czech"] = "CZ"
  rule["Germany"] = "DE"
  rule["DR"] = "DO"
  rule["Ecuador"] = "EC"
  rule["Finland"] = "FI"
  rule["Fiji"] = "FJ"
  rule["Falk"] = "FK"
  rule["Ghana"] = "GH"
  rule["Guat"] = "GT"
  rule["Hond"] = "HN"
  rule["Haiti"] = "HT"
  rule["Eire"] = "IE"
  rule["Iraq"] = "IQ"
  rule["Japan"] = "JP"
  rule["Kyrgyz"] = "KG"
  rule["ROK"] = "KR"
  rule["Latvia"] = "LV"
  rule["Lux"] = "LX"
  rule["Moldova"] = "MD"
  rule["Malta"] = "MT"
  rule["Mauritius"] = "MU"
  rule["Namibia"] = "NA"
  rule["Nic"] = "NI"
  rule["Norway"] = "NO"
  rule["Peru"] = "PE"
  rule["Phil"] = "PH"
  rule["Pakistan"] = "PK"
  rule["Sudan"] = "SD"
  rule["Salv"] = "SV"
  rule["Tonga"] = "TO"
  rule["Vanuatu"] = "VU"

  # Avoid collisions.
  rule["Detroit"] = "Dt" # De = Denver

  for (name in rule) {
    record_hash(rule[name], name)
  }
}

function make_line(n, field, \
		   f, r)
{
  r = field[1]
  for (f = 2; f <= n; f++)
    r = r " " field[f]
  return r
}

# Process the input line LINE and save it for later output.

function process_input_line(line, \
			    f, field, end, n, outline, r, \
			    linkline, ruleline, zoneline)
{
  # Remove comments, normalize spaces, and append a space to each line.
  sub(/#.*/, "", line)
  line = line " "
  gsub(/[\t ]+/, " ", line)

  # Abbreviate keywords and determine line type.
  linkline = sub(/^Link /, "L ", line)
  ruleline = sub(/^Rule /, "R ", line)
  zoneline = sub(/^Zone /, "Z ", line)

  # Replace FooAsia rules with the same rules without "Asia", as they
  # are duplicates.
  if (match(line, /[^ ]Asia /)) {
    if (ruleline) return
    line = substr(line, 1, RSTART) substr(line, RSTART + 5)
  }

  # Abbreviate times.
  while (match(line, /[: ]0+[0-9]/))
    line = substr(line, 1, RSTART) substr(line, RSTART + RLENGTH - 1)
  while (match(line, /:0[^:]/))
    line = substr(line, 1, RSTART - 1) substr(line, RSTART + 2)

  # Abbreviate weekday names.
  while (match(line, / (last)?(Mon|Wed|Fri)[ <>]/)) {
    end = RSTART + RLENGTH
    line = substr(line, 1, end - 4) substr(line, end - 1)
  }
  while (match(line, / (last)?(Sun|Tue|Thu|Sat)[ <>]/)) {
    end = RSTART + RLENGTH
    line = substr(line, 1, end - 3) substr(line, end - 1)
  }

  # Abbreviate "max", "min", "only" and month names.
  # Although "max" and "min" can both be abbreviated to just "m",
  # the longer forms "ma" and "mi" are needed with zic 2023d and earlier.
  gsub(/ max /, dataform == "vanguard" ? " m " : " ma ", line)
  gsub(/ min /, dataform == "vanguard" ? " m " : " mi ", line)
  gsub(/ only /, " o ", line)
  gsub(/ Jan /, " Ja ", line)
  gsub(/ Feb /, " F ", line)
  gsub(/ Apr /, " Ap ", line)
  gsub(/ Aug /, " Au ", line)
  gsub(/ Sep /, " S ", line)
  gsub(/ Oct /, " O ", line)
  gsub(/ Nov /, " N ", line)
  gsub(/ Dec /, " D ", line)

  # Strip leading and trailing space.
  sub(/^ /, "", line)
  sub(/ $/, "", line)

  # Remove unnecessary trailing zero fields.
  sub(/ 0+$/, "", line)

  # Remove unnecessary trailing days-of-month "1".
  if (match(line, /[A-Za-z] 1$/))
    line = substr(line, 1, RSTART)

  # Remove unnecessary trailing " Ja" (for January).
  sub(/ Ja$/, "", line)

  n = split(line, field)

  # Record which rule names are used, and generate their abbreviations.
  f = zoneline ? 4 : linkline || ruleline ? 0 : 2
  r = field[f]
  if (r ~ /^[^-+0-9]/) {
    rule_used[r] = 1
  }

  if (zoneline)
    zonename = startdef = field[2]
  else if (linkline)
    zonename = startdef = field[3]
  else if (ruleline)
    zonename = ""

  # Save the information for later output.
  outline = make_line(n, field)
  if (ruleline)
    rule_output_line[nrule_out++] = outline
  else if (linkline) {
    # In vanguard format with Gawk, links are output sorted by destination.
    if (dataform == "vanguard" && PROCINFO["version"])
      linkdef[zonename] = field[2]
    else
      link_output_line[nlink_out++] = outline
  }else
    zonedef[zonename] = (zoneline ? "" : zonedef[zonename] "\n") outline
}

function omit_unused_rules( \
			   i, field)
{
  for (i = 0; i < nrule_out; i++) {
    split(rule_output_line[i], field)
    if (!rule_used[field[2]])
      rule_output_line[i] = ""
  }
}

function abbreviate_rule_names( \
			       abbr, f, field, i, n, newdef, newline, r, \
			       zoneline, zonelines, zonename)
{
  for (i = 0; i < nrule_out; i++) {
    n = split(rule_output_line[i], field)
    if (n) {
      r = field[2]
      if (r ~ /^[^-+0-9]/) {
	abbr = rule[r]
	if (!abbr) {
	  rule[r] = abbr = gen_rule_name(r)
	}
	field[2] = abbr
	rule_output_line[i] = make_line(n, field)
      }
    }
  }
  for (zonename in zonedef) {
    zonelines = split(zonedef[zonename], zoneline, /\n/)
    newdef = ""
    for (i = 1; i <= zonelines; i++) {
      newline = zoneline[i]
      n = split(newline, field)
      f = i == 1 ? 4 : 2
      r = rule[field[f]]
      if (r) {
	field[f] = r
	newline = make_line(n, field)
      }
      newdef = (newdef ? newdef "\n" : "") newline
    }
    zonedef[zonename] = newdef
  }
}

function output_saved_lines( \
			    i, zonename)
{
  for (i = 0; i < nrule_out; i++)
    if (rule_output_line[i])
      print rule_output_line[i]

  # When using gawk, output zones sorted by name.
  # This makes the output a bit more compressible.
  PROCINFO["sorted_in"] = "@ind_str_asc"
  for (zonename in zonedef)
    print zonedef[zonename]

  if (nlink_out)
    for (i = 0; i < nlink_out; i++)
      print link_output_line[i]
  else {
    # When using gawk, output links sorted by destination.
    # This also helps compressibility a bit.
    PROCINFO["sorted_in"] = "@val_type_asc"
    for (zonename in linkdef)
      printf "L %s %s\n", linkdef[zonename], zonename
  }
}

BEGIN {
  # Files that the output normally depends on.
  default_dep["africa"] = 1
  default_dep["antarctica"] = 1
  default_dep["asia"] = 1
  default_dep["australasia"] = 1
  default_dep["backward"] = 1
  default_dep["etcetera"] = 1
  default_dep["europe"] = 1
  default_dep["factory"] = 1
  default_dep["northamerica"] = 1
  default_dep["southamerica"] = 1
  default_dep["ziguard.awk"] = 1
  default_dep["zishrink.awk"] = 1

  # Output a version string from 'version' and related configuration variables
  # supported by tzdb's Makefile.  If you change the makefile or any other files
  # that affect the output of this script, you should append '-SOMETHING'
  # to the contents of 'version', where SOMETHING identifies what was changed.

  ndeps = split(deps, dep)
  ddeps = ""
  for (i = 1; i <= ndeps; i++) {
    if (default_dep[dep[i]]) {
      default_dep[dep[i]]++
    } else {
      ddeps = ddeps " " dep[i]
    }
  }
  for (d in default_dep) {
    if (default_dep[d] == 1) {
      ddeps = ddeps " !" d
    }
  }
  print "# version", version
  if (dataform != "main") {
    print "# dataform", dataform
  }
  if (redo != "posix_right") {
    print "# redo " redo
  }
  if (ddeps) {
    print "# ddeps" ddeps
  }
  print "# This zic input file is in the public domain."

  prehash_rule_names()
}

/^[\t ]*[^#\t ]/ {
  process_input_line($0)
}

END {
  omit_unused_rules()
  abbreviate_rule_names()
  output_saved_lines()
}
