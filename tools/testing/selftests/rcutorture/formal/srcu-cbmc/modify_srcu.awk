#!/usr/bin/awk -f
# SPDX-License-Identifier: GPL-2.0

# Modify SRCU for formal verification. The first argument should be srcu.h and
# the second should be srcu.c. Outputs modified srcu.h and srcu.c into the
# current directory.

BEGIN {
	if (ARGC != 5) {
		print "Usange: input.h input.c output.h output.c" > "/dev/stderr";
		exit 1;
	}
	h_output = ARGV[3];
	c_output = ARGV[4];
	ARGC = 3;

	# Tokenize using FS and not RS as FS supports regular expressions. Each
	# record is one line of source, except that backslashed lines are
	# combined. Comments are treated as field separators, as are quotes.
	quote_regexp="\"([^\\\\\"]|\\\\.)*\"";
	comment_regexp="\\/\\*([^*]|\\*+[^*/])*\\*\\/|\\/\\/.*(\n|$)";
	FS="([ \\\\\t\n\v\f;,.=(){}+*/<>&|^-]|\\[|\\]|" comment_regexp "|" quote_regexp ")+";

	inside_srcu_struct = 0;
	inside_srcu_init_def = 0;
	srcu_init_param_name = "";
	in_macro = 0;
	brace_nesting = 0;
	paren_nesting = 0;

	# Allow the manipulation of the last field separator after has been
	# seen.
	last_fs = "";
	# Whether the last field separator was intended to be output.
	last_fs_print = 0;

	# rcu_batches stores the initialization for each instance of struct
	# rcu_batch

	in_comment = 0;

	outputfile = "";
}

{
	prev_outputfile = outputfile;
	if (FILENAME ~ /\.h$/) {
		outputfile = h_output;
		if (FNR != NR) {
			print "Incorrect file order" > "/dev/stderr";
			exit 1;
		}
	}
	else
		outputfile = c_output;

	if (prev_outputfile && outputfile != prev_outputfile) {
		new_outputfile = outputfile;
		outputfile = prev_outputfile;
		update_fieldsep("", 0);
		outputfile = new_outputfile;
	}
}

# Combine the next line into $0.
function combine_line() {
	ret = getline next_line;
	if (ret == 0) {
		# Don't allow two consecutive getlines at the end of the file
		if (eof_found) {
			print "Error: expected more input." > "/dev/stderr";
			exit 1;
		} else {
			eof_found = 1;
		}
	} else if (ret == -1) {
		print "Error reading next line of file" FILENAME > "/dev/stderr";
		exit 1;
	}
	$0 = $0 "\n" next_line;
}

# Combine backslashed lines and multiline comments.
function combine_backslashes() {
	while (/\\$|\/\*([^*]|\*+[^*\/])*\**$/) {
		combine_line();
	}
}

function read_line() {
	combine_line();
	combine_backslashes();
}

# Print out field separators and update variables that depend on them. Only
# print if p is true. Call with sep="" and p=0 to print out the last field
# separator.
function update_fieldsep(sep, p) {
	# Count braces
	sep_tmp = sep;
	gsub(quote_regexp "|" comment_regexp, "", sep_tmp);
	while (1)
	{
		if (sub("[^{}()]*\\{", "", sep_tmp)) {
			brace_nesting++;
			continue;
		}
		if (sub("[^{}()]*\\}", "", sep_tmp)) {
			brace_nesting--;
			if (brace_nesting < 0) {
				print "Unbalanced braces!" > "/dev/stderr";
				exit 1;
			}
			continue;
		}
		if (sub("[^{}()]*\\(", "", sep_tmp)) {
			paren_nesting++;
			continue;
		}
		if (sub("[^{}()]*\\)", "", sep_tmp)) {
			paren_nesting--;
			if (paren_nesting < 0) {
				print "Unbalanced parenthesis!" > "/dev/stderr";
				exit 1;
			}
			continue;
		}

		break;
	}

	if (last_fs_print)
		printf("%s", last_fs) > outputfile;
	last_fs = sep;
	last_fs_print = p;
}

# Shifts the fields down by n positions. Calls next if there are no more. If p
# is true then print out field separators.
function shift_fields(n, p) {
	do {
		if (match($0, FS) > 0) {
			update_fieldsep(substr($0, RSTART, RLENGTH), p);
			if (RSTART + RLENGTH <= length())
				$0 = substr($0, RSTART + RLENGTH);
			else
				$0 = "";
		} else {
			update_fieldsep("", 0);
			print "" > outputfile;
			next;
		}
	} while (--n > 0);
}

# Shifts and prints the first n fields.
function print_fields(n) {
	do {
		update_fieldsep("", 0);
		printf("%s", $1) > outputfile;
		shift_fields(1, 1);
	} while (--n > 0);
}

{
	combine_backslashes();
}

# Print leading FS
{
	if (match($0, "^(" FS ")+") > 0) {
		update_fieldsep(substr($0, RSTART, RLENGTH), 1);
		if (RSTART + RLENGTH <= length())
			$0 = substr($0, RSTART + RLENGTH);
		else
			$0 = "";
	}
}

# Parse the line.
{
	while (NF > 0) {
		if ($1 == "struct" && NF < 3) {
			read_line();
			continue;
		}

		if (FILENAME ~ /\.h$/ && !inside_srcu_struct &&
		    brace_nesting == 0 && paren_nesting == 0 &&
		    $1 == "struct" && $2 == "srcu_struct" &&
		    $0 ~ "^struct(" FS ")+srcu_struct(" FS ")+\\{") {
			inside_srcu_struct = 1;
			print_fields(2);
			continue;
		}
		if (inside_srcu_struct && brace_nesting == 0 &&
		    paren_nesting == 0) {
			inside_srcu_struct = 0;
			update_fieldsep("", 0);
			for (name in rcu_batches)
				print "extern struct rcu_batch " name ";" > outputfile;
		}

		if (inside_srcu_struct && $1 == "struct" && $2 == "rcu_batch") {
			# Move rcu_batches outside of the struct.
			rcu_batches[$3] = "";
			shift_fields(3, 1);
			sub(/;[[:space:]]*$/, "", last_fs);
			continue;
		}

		if (FILENAME ~ /\.h$/ && !inside_srcu_init_def &&
		    $1 == "#define" && $2 == "__SRCU_STRUCT_INIT") {
			inside_srcu_init_def = 1;
			srcu_init_param_name = $3;
			in_macro = 1;
			print_fields(3);
			continue;
		}
		if (inside_srcu_init_def && brace_nesting == 0 &&
		    paren_nesting == 0) {
			inside_srcu_init_def = 0;
			in_macro = 0;
			continue;
		}

		if (inside_srcu_init_def && brace_nesting == 1 &&
		    paren_nesting == 0 && last_fs ~ /\.[[:space:]]*$/ &&
		    $1 ~ /^[[:alnum:]_]+$/) {
			name = $1;
			if (name in rcu_batches) {
				# Remove the dot.
				sub(/\.[[:space:]]*$/, "", last_fs);

				old_record = $0;
				do
					shift_fields(1, 0);
				while (last_fs !~ /,/ || paren_nesting > 0);
				end_loc = length(old_record) - length($0);
				end_loc += index(last_fs, ",") - length(last_fs);

				last_fs = substr(last_fs, index(last_fs, ",") + 1);
				last_fs_print = 1;

				match(old_record, "^"name"("FS")+=");
				start_loc = RSTART + RLENGTH;

				len = end_loc - start_loc;
				initializer = substr(old_record, start_loc, len);
				gsub(srcu_init_param_name "\\.", "", initializer);
				rcu_batches[name] = initializer;
				continue;
			}
		}

		# Don't include a nonexistent file
		if (!in_macro && $1 == "#include" && /^#include[[:space:]]+"rcu\.h"/) {
			update_fieldsep("", 0);
			next;
		}

		# Ignore most preprocessor stuff.
		if (!in_macro && $1 ~ /#/) {
			break;
		}

		if (brace_nesting > 0 && $1 ~ "^[[:alnum:]_]+$" && NF < 2) {
			read_line();
			continue;
		}
		if (brace_nesting > 0 &&
		    $0 ~ "^[[:alnum:]_]+[[:space:]]*(\\.|->)[[:space:]]*[[:alnum:]_]+" &&
		    $2 in rcu_batches) {
			# Make uses of rcu_batches global. Somewhat unreliable.
			shift_fields(1, 0);
			print_fields(1);
			continue;
		}

		if ($1 == "static" && NF < 3) {
			read_line();
			continue;
		}
		if ($1 == "static" && ($2 == "bool" && $3 == "try_check_zero" ||
		                       $2 == "void" && $3 == "srcu_flip")) {
			shift_fields(1, 1);
			print_fields(2);
			continue;
		}

		# Distinguish between read-side and write-side memory barriers.
		if ($1 == "smp_mb" && NF < 2) {
			read_line();
			continue;
		}
		if (match($0, /^smp_mb[[:space:]();\/*]*[[:alnum:]]/)) {
			barrier_letter = substr($0, RLENGTH, 1);
			if (barrier_letter ~ /A|D/)
				new_barrier_name = "sync_smp_mb";
			else if (barrier_letter ~ /B|C/)
				new_barrier_name = "rs_smp_mb";
			else {
				print "Unrecognized memory barrier." > "/dev/null";
				exit 1;
			}

			shift_fields(1, 1);
			printf("%s", new_barrier_name) > outputfile;
			continue;
		}

		# Skip definition of rcu_synchronize, since it is already
		# defined in misc.h. Only present in old versions of srcu.
		if (brace_nesting == 0 && paren_nesting == 0 &&
		    $1 == "struct" && $2 == "rcu_synchronize" &&
		    $0 ~ "^struct(" FS ")+rcu_synchronize(" FS ")+\\{") {
			shift_fields(2, 0);
			while (brace_nesting) {
				if (NF < 2)
					read_line();
				shift_fields(1, 0);
			}
		}

		# Skip definition of wakeme_after_rcu for the same reason
		if (brace_nesting == 0 && $1 == "static" && $2 == "void" &&
		    $3 == "wakeme_after_rcu") {
			while (NF < 5)
				read_line();
			shift_fields(3, 0);
			do {
				while (NF < 3)
					read_line();
				shift_fields(1, 0);
			} while (paren_nesting || brace_nesting);
		}

		if ($1 ~ /^(unsigned|long)$/ && NF < 3) {
			read_line();
			continue;
		}

		# Give srcu_batches_completed the correct type for old SRCU.
		if (brace_nesting == 0 && $1 == "long" &&
		    $2 == "srcu_batches_completed") {
			update_fieldsep("", 0);
			printf("unsigned ") > outputfile;
			print_fields(2);
			continue;
		}
		if (brace_nesting == 0 && $1 == "unsigned" && $2 == "long" &&
		    $3 == "srcu_batches_completed") {
			print_fields(3);
			continue;
		}

		# Just print out the input code by default.
		print_fields(1);
	}
	update_fieldsep("", 0);
	print > outputfile;
	next;
}

END {
	update_fieldsep("", 0);

	if (brace_nesting != 0) {
		print "Unbalanced braces!" > "/dev/stderr";
		exit 1;
	}

	# Define the rcu_batches
	for (name in rcu_batches)
		print "struct rcu_batch " name " = " rcu_batches[name] ";" > c_output;
}
