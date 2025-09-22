Python Scripting
================

LLDB has been structured from the beginning to be scriptable in two
ways -- a Unix Python session can initiate/run a debug session
non-interactively using LLDB; and within the LLDB debugger tool, Python
scripts can be used to help with many tasks, including inspecting
program data, iterating over containers and determining if a breakpoint
should stop execution or continue. This document will show how to do
some of these things by going through an example, explaining how to use
Python scripting to find a bug in a program that searches for text in a
large binary tree.

The Test Program and Input
--------------------------

We have a simple C program (dictionary.c) that reads in a text file,
and stores all the words from the file in a Binary Search Tree, sorted
alphabetically. It then enters a loop prompting the user for a word,
searching for the word in the tree (using Binary Search), and reporting
to the user whether or not it found the word in the tree.

The input text file we are using to test our program contains the text
for William Shakespeare's famous tragedy "Romeo and Juliet".

The Bug
-------

When we try running our program, we find there is a problem. While it
successfully finds some of the words we would expect to find, such as
"love" or "sun", it fails to find the word "Romeo", which MUST be in
the input text file:

::

   $ ./dictionary Romeo-and-Juliet.txt
   Dictionary loaded.
   Enter search word: love
   Yes!
   Enter search word: sun
   Yes!
   Enter search word: Romeo
   No!
   Enter search word: ^D
   $

Using Depth First Search
------------------------

Our first job is to determine if the word "Romeo" actually got inserted
into the tree or not. Since "Romeo and Juliet" has thousands of words,
trying to examine our binary search tree by hand is completely
impractical. Therefore we will write a Python script to search the tree
for us. We will write a recursive Depth First Search function that
traverses the entire tree searching for a word, and maintaining
information about the path from the root of the tree to the current
node. If it finds the word in the tree, it returns the path from the
root to the node containing the word. This is what our DFS function in
Python would look like, with line numbers added for easy reference in
later explanations:

::

   1: def DFS (root, word, cur_path):
   2:     root_word_ptr = root.GetChildMemberWithName ("word")
   3:     left_child_ptr = root.GetChildMemberWithName ("left")
   4:     right_child_ptr = root.GetChildMemberWithName ("right")
   5:     root_word = root_word_ptr.GetSummary()
   6:     end = len (root_word) - 1
   7:     if root_word[0] == '"' and root_word[end] == '"':
   8:         root_word = root_word[1:end]
   9:     end = len (root_word) - 1
   10:     if root_word[0] == '\'' and root_word[end] == '\'':
   11:        root_word = root_word[1:end]
   12:     if root_word == word:
   13:         return cur_path
   14:     elif word < root_word:
   15:         if left_child_ptr.GetValue() is None:
   16:             return ""
   17:         else:
   18:             cur_path = cur_path + "L"
   19:             return DFS (left_child_ptr, word, cur_path)
   20:     else:
   21:         if right_child_ptr.GetValue() is None:
   22:             return ""
   23:         else:
   24:             cur_path = cur_path + "R"
   25:             return DFS (right_child_ptr, word, cur_path)


Accessing & Manipulating Program Variables
------------------------------------------

Before we can call any Python function on any of our program's
variables, we need to get the variable into a form that Python can
access. To show you how to do this we will look at the parameters for
the DFS function. The first parameter is going to be a node in our
binary search tree, put into a Python variable. The second parameter is
the word we are searching for (a string), and the third parameter is a
string representing the path from the root of the tree to our current
node.

The most interesting parameter is the first one, the Python variable
that needs to contain a node in our search tree. How can we take a
variable out of our program and put it into a Python variable? What
kind of Python variable will it be? The answers are to use the LLDB API
functions, provided as part of the LLDB Python module. Running Python
from inside LLDB, LLDB will automatically give us our current frame
object as a Python variable, "lldb.frame". This variable has the type
`SBFrame` (see the LLDB API for more information about `SBFrame`
objects). One of the things we can do with a frame object, is to ask it
to find and return its local variable. We will call the API function
`SBFrame.FindVariable` on the lldb.frame object to give us our dictionary
variable as a Python variable:

::

   root = lldb.frame.FindVariable ("dictionary")

The line above, executed in the Python script interpreter in LLDB, asks the
current frame to find the variable named "dictionary" and return it. We then
store the returned value in the Python variable named "root". This answers the
question of HOW to get the variable, but it still doesn't explain WHAT actually
gets put into "root". If you examine the LLDB API, you will find that the
`SBFrame` method "FindVariable" returns an object of type `SBValue`. `SBValue`
objects are used, among other things, to wrap up program variables and values.
There are many useful methods defined in the `SBValue` class to allow you to get
information or children values out of SBValues. For complete information, see
the header file SBValue.h. The `SBValue` methods that we use in our DFS function
are ``GetChildMemberWithName()``, ``GetSummary()``, and ``GetValue()``.


Explaining DFS Script in Detail
-------------------------------

Before diving into the details of this code, it would be best to give a
high-level overview of what it does. The nodes in our binary search tree were
defined to have type ``tree_node *``, which is defined as:

::

   typedef struct tree_node
   {
      const char *word;
      struct tree_node *left;
      struct tree_node *right;
   } tree_node;

Lines 2-11 of DFS are getting data out of the current tree node and getting
ready to do the actual search; lines 12-25 are the actual depth-first search.
Lines 2-4 of our DFS function get the word, left and right fields out of the
current node and store them in Python variables. Since root_word_ptr is a
pointer to our word, and we want the actual word, line 5 calls GetSummary() to
get a string containing the value out of the pointer. Since GetSummary() adds
quotes around its result, lines 6-11 strip surrounding quotes off the word.

Line 12 checks to see if the word in the current node is the one we are
searching for. If so, we are done, and line 13 returns the current path.
Otherwise, line 14 checks to see if we should go left (search word comes before
the current word). If we decide to go left, line 15 checks to see if the left
pointer child is NULL ("None" is the Python equivalent of NULL). If the left
pointer is NULL, then the word is not in this tree and we return an empty path
(line 16). Otherwise, we add an "L" to the end of our current path string, to
indicate we are going left (line 18), and then recurse on the left child (line
19). Lines 20-25 are the same as lines 14-19, except for going right rather
than going left.

One other note: Typing something as long as our DFS function directly into the
interpreter can be difficult, as making a single typing mistake means having to
start all over. Therefore we recommend doing as we have done: Writing your
longer, more complicated script functions in a separate file (in this case
tree_utils.py) and then importing it into your LLDB Python interpreter.


The DFS Script in Action
------------------------

At this point we are ready to use the DFS function to see if the word "Romeo"
is in our tree or not. To actually use it in LLDB on our dictionary program,
you would do something like this:

::

   $ lldb
   (lldb) process attach -n "dictionary"
   Architecture set to: x86_64.
   Process 521 stopped
   * thread #1: tid = 0x2c03, 0x00007fff86c8bea0 libSystem.B.dylib`read$NOCANCEL + 8, stop reason = signal SIGSTOP
   frame #0: 0x00007fff86c8bea0 libSystem.B.dylib`read$NOCANCEL + 8
   (lldb) breakpoint set -n find_word
   Breakpoint created: 1: name = 'find_word', locations = 1, resolved = 1
   (lldb) continue
   Process 521 resuming
   Process 521 stopped
   * thread #1: tid = 0x2c03, 0x0000000100001830 dictionary`find_word + 16
   at dictionary.c:105, stop reason = breakpoint 1.1
   frame #0: 0x0000000100001830 dictionary`find_word + 16 at dictionary.c:105
   102 int
   103 find_word (tree_node *dictionary, char *word)
   104 {
   -> 105 if (!word || !dictionary)
   106 return 0;
   107
   108 int compare_value = strcmp (word, dictionary->word);
   (lldb) script
   Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.
   >>> import tree_utils
   >>> root = lldb.frame.FindVariable ("dictionary")
   >>> current_path = ""
   >>> path = tree_utils.DFS (root, "Romeo", current_path)
   >>> print path
   LLRRL
   >>> ^D
   (lldb)

The first bit of code above shows starting lldb, attaching to the dictionary
program, and getting to the find_word function in LLDB. The interesting part
(as far as this example is concerned) begins when we enter the script command
and drop into the embedded interactive Python interpreter. We will go over this
Python code line by line. The first line

::

   import tree_utils


imports the file where we wrote our DFS function, tree_utils.py, into Python.
Notice that to import the file we leave off the ".py" extension. We can now
call any function in that file, giving it the prefix "tree_utils.", so that
Python knows where to look for the function. The line

::

   root = lldb.frame.FindVariable ("dictionary")


gets our program variable "dictionary" (which contains the binary search tree)
and puts it into the Python variable "root". See Accessing & Manipulating
Program Variables in Python above for more details about how this works. The
next line is

::

   current_path = ""

This line initializes the current_path from the root of the tree to our current
node. Since we are starting at the root of the tree, our current path starts as
an empty string. As we go right and left through the tree, the DFS function
will append an 'R' or an 'L' to the current path, as appropriate. The line

::

   path = tree_utils.DFS (root, "Romeo", current_path)

calls our DFS function (prefixing it with the module name so that Python can
find it). We pass in our binary tree stored in the variable root, the word we
are searching for, and our current path. We assign whatever path the DFS
function returns to the Python variable path.

Finally, we want to see if the word was found or not, and if so we want to see
the path through the tree to the word. So we do

::

   print path

From this we can see that the word "Romeo" was indeed found in the tree, and
the path from the root of the tree to the node containing "Romeo" is
left-left-right-right-left.

Using Breakpoint Command Scripts
--------------------------------

We are halfway to figuring out what the problem is. We know the word we are
looking for is in the binary tree, and we know exactly where it is in the
binary tree. Now we need to figure out why our binary search algorithm is not
finding the word. We will do this using breakpoint command scripts.

The idea is as follows. The binary search algorithm has two main decision
points: the decision to follow the right branch; and, the decision to follow
the left branch. We will set a breakpoint at each of these decision points, and
attach a Python breakpoint command script to each breakpoint. The breakpoint
commands will use the global path Python variable that we got from our DFS
function. Each time one of these decision breakpoints is hit, the script will
compare the actual decision with the decision the front of the path variable
says should be made (the first character of the path). If the actual decision
and the path agree, then the front character is stripped off the path, and
execution is resumed. In this case the user never even sees the breakpoint
being hit. But if the decision differs from what the path says it should be,
then the script prints out a message and does NOT resume execution, leaving the
user sitting at the first point where a wrong decision is being made.

Python Breakpoint Command Scripts Are Not What They Seem
--------------------------------------------------------

What do we mean by that? When you enter a Python breakpoint command in LLDB, it
appears that you are entering one or more plain lines of Python. BUT LLDB then
takes what you entered and wraps it into a Python FUNCTION (just like using the
"def" Python command). It automatically gives the function an obscure, unique,
hard-to-stumble-across function name, and gives it two parameters: frame and
bp_loc. When the breakpoint gets hit, LLDB wraps up the frame object where the
breakpoint was hit, and the breakpoint location object for the breakpoint that
was hit, and puts them into Python variables for you. It then calls the Python
function that was created for the breakpoint command, and passes in the frame
and breakpoint location objects.

So, being practical, what does this mean for you when you write your Python
breakpoint commands? It means that there are two things you need to keep in
mind: 1. If you want to access any Python variables created outside your
script, you must declare such variables to be global. If you do not declare
them as global, then the Python function will treat them as local variables,
and you will get unexpected behavior. 2. All Python breakpoint command scripts
automatically have a frame and a bp_loc variable. The variables are pre-loaded
by LLDB with the correct context for the breakpoint. You do not have to use
these variables, but they are there if you want them.

The Decision Point Breakpoint Commands
--------------------------------------

This is what the Python breakpoint command script would look like for the
decision to go right:

::

   global path
   if path[0] == 'R':
      path = path[1:]
      thread = frame.GetThread()
      process = thread.GetProcess()
      process.Continue()
   else:
      print "Here is the problem; going right, should go left!"
   Just as a reminder, LLDB is going to take this script and wrap it up in a function, like this:


   def some_unique_and_obscure_function_name (frame, bp_loc):
      global path
      if path[0] == 'R':
         path = path[1:]
         thread = frame.GetThread()
         process = thread.GetProcess()
         process.Continue()
      else:
         print "Here is the problem; going right, should go left!"

LLDB will call the function, passing in the correct frame and breakpoint
location whenever the breakpoint gets hit. There are several things to notice
about this function. The first one is that we are accessing and updating a
piece of state (the path variable), and actually conditioning our behavior
based upon this variable. Since the variable was defined outside of our script
(and therefore outside of the corresponding function) we need to tell Python
that we are accessing a global variable. That is what the first line of the
script does. Next we check where the path says we should go and compare it to
our decision (recall that we are at the breakpoint for the decision to go
right). If the path agrees with our decision, then we strip the first character
off of the path.

Since the decision matched the path, we want to resume execution. To do this we
make use of the frame parameter that LLDB guarantees will be there for us. We
use LLDB API functions to get the current thread from the current frame, and
then to get the process from the thread. Once we have the process, we tell it
to resume execution (using the Continue() API function).

If the decision to go right does not agree with the path, then we do not resume
execution. We allow the breakpoint to remain stopped (by doing nothing), and we
print an informational message telling the user we have found the problem, and
what the problem is.

Actually Using The Breakpoint Commands
--------------------------------------

Now we will look at what happens when we actually use these breakpoint commands
on our program. Doing a source list -n find_word shows us the function
containing our two decision points. Looking at the code below, we see that we
want to set our breakpoints on lines 113 and 115:

::

   (lldb) source list -n find_word
   File: /Volumes/Data/HD2/carolinetice/Desktop/LLDB-Web-Examples/dictionary.c.
   101
   102 int
   103 find_word (tree_node *dictionary, char *word)
   104 {
   105   if (!word || !dictionary)
   106     return 0;
   107
   108   int compare_value = strcmp (word, dictionary->word);
   109
   110   if (compare_value == 0)
   111     return 1;
   112   else if (compare_value < 0)
   113     return find_word (dictionary->left, word);
   114   else
   115     return find_word (dictionary->right, word);
   116 }
   117


So, we set our breakpoints, enter our breakpoint command scripts, and see what happens:

::

   (lldb) breakpoint set -l 113
   Breakpoint created: 2: file ='dictionary.c', line = 113, locations = 1, resolved = 1
   (lldb) breakpoint set -l 115
   Breakpoint created: 3: file ='dictionary.c', line = 115, locations = 1, resolved = 1
   (lldb) breakpoint command add -s python 2
   Enter your Python command(s). Type 'DONE' to end.
   > global path
   > if (path[0] == 'L'):
   >     path = path[1:]
   >     thread = frame.GetThread()
   >     process = thread.GetProcess()
   >     process.Continue()
   > else:
   >     print "Here is the problem. Going left, should go right!"
   > DONE
   (lldb) breakpoint command add -s python 3
   Enter your Python command(s). Type 'DONE' to end.
   > global path
   > if (path[0] == 'R'):
   >     path = path[1:]
   >     thread = frame.GetThread()
   >     process = thread.GetProcess()
   >     process.Continue()
   > else:
   >     print "Here is the problem. Going right, should go left!"
   > DONE
   (lldb) continue
   Process 696 resuming
   Here is the problem. Going right, should go left!
   Process 696 stopped
   * thread #1: tid = 0x2d03, 0x000000010000189f dictionary`find_word + 127 at dictionary.c:115, stop reason = breakpoint 3.1
   frame #0: 0x000000010000189f dictionary`find_word + 127 at dictionary.c:115
      112   else if (compare_value < 0)
      113     return find_word (dictionary->left, word);
      114   else
   -> 115     return find_word (dictionary->right, word);
      116 }
      117
      118 void
   (lldb)


After setting our breakpoints, adding our breakpoint commands and continuing,
we run for a little bit and then hit one of our breakpoints, printing out the
error message from the breakpoint command. Apparently at this point in the
tree, our search algorithm decided to go right, but our path says the node we
want is to the left. Examining the word at the node where we stopped, and our
search word, we see:

::

   (lldb) expr dictionary->word
   (const char *) $1 = 0x0000000100100080 "dramatis"
   (lldb) expr word
   (char *) $2 = 0x00007fff5fbff108 "romeo"

So the word at our current node is "dramatis", and the word we are searching
for is "romeo". "romeo" comes after "dramatis" alphabetically, so it seems like
going right would be the correct decision. Let's ask Python what it thinks the
path from the current node to our word is:

::

   (lldb) script print path
   LLRRL

According to Python we need to go left-left-right-right-left from our current
node to find the word we are looking for. Let's double check our tree, and see
what word it has at that node:

::

   (lldb) expr dictionary->left->left->right->right->left->word
   (const char *) $4 = 0x0000000100100880 "Romeo"

So the word we are searching for is "romeo" and the word at our DFS location is
"Romeo". Aha! One is uppercase and the other is lowercase: We seem to have a
case conversion problem somewhere in our program (we do).

This is the end of our example on how you might use Python scripting in LLDB to
help you find bugs in your program.

Source Files for The Example
----------------------------

The complete code for the Dictionary program (with case-conversion bug), the
DFS function and other Python script examples (tree_utils.py) used for this
example are available below.

tree_utils.py - Example Python functions using LLDB's API, including DFS

::

   """
   # ===-- tree_utils.py ---------------------------------------*- Python -*-===//
   #
   #  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
   #  See https://llvm.org/LICENSE.txt for license information.
   #  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
   #
   # ===----------------------------------------------------------------------===//

   tree_utils.py  - A set of functions for examining binary
   search trees, based on the example search tree defined in
   dictionary.c.  These functions contain calls to LLDB API
   functions, and assume that the LLDB Python module has been
   imported.

   For a thorough explanation of how the DFS function works, and
   for more information about dictionary.c go to
   http://lldb.llvm.org/scripting.html
   """


   def DFS(root, word, cur_path):
      """
      Recursively traverse a binary search tree containing
      words sorted alphabetically, searching for a particular
      word in the tree.  Also maintains a string representing
      the path from the root of the tree to the current node.
      If the word is found in the tree, return the path string.
      Otherwise return an empty string.

      This function assumes the binary search tree is
      the one defined in dictionary.c  It uses LLDB API
      functions to examine and traverse the tree nodes.
      """

      # Get pointer field values out of node 'root'

      root_word_ptr = root.GetChildMemberWithName("word")
      left_child_ptr = root.GetChildMemberWithName("left")
      right_child_ptr = root.GetChildMemberWithName("right")

      # Get the word out of the word pointer and strip off
      # surrounding quotes (added by call to GetSummary).

      root_word = root_word_ptr.GetSummary()
      end = len(root_word) - 1
      if root_word[0] == '"' and root_word[end] == '"':
         root_word = root_word[1:end]
      end = len(root_word) - 1
      if root_word[0] == '\'' and root_word[end] == '\'':
         root_word = root_word[1:end]

      # Main depth first search

      if root_word == word:
         return cur_path
      elif word < root_word:

         # Check to see if left child is NULL

         if left_child_ptr.GetValue() is None:
               return ""
         else:
               cur_path = cur_path + "L"
               return DFS(left_child_ptr, word, cur_path)
      else:

         # Check to see if right child is NULL

         if right_child_ptr.GetValue() is None:
               return ""
         else:
               cur_path = cur_path + "R"
               return DFS(right_child_ptr, word, cur_path)


   def tree_size(root):
      """
      Recursively traverse a binary search tree, counting
      the nodes in the tree.  Returns the final count.

      This function assumes the binary search tree is
      the one defined in dictionary.c  It uses LLDB API
      functions to examine and traverse the tree nodes.
      """
      if (root.GetValue is None):
         return 0

      if (int(root.GetValue(), 16) == 0):
         return 0

      left_size = tree_size(root.GetChildAtIndex(1))
      right_size = tree_size(root.GetChildAtIndex(2))

      total_size = left_size + right_size + 1
      return total_size


   def print_tree(root):
      """
      Recursively traverse a binary search tree, printing out
      the words at the nodes in alphabetical order (the
      search order for the binary tree).

      This function assumes the binary search tree is
      the one defined in dictionary.c  It uses LLDB API
      functions to examine and traverse the tree nodes.
      """
      if (root.GetChildAtIndex(1).GetValue() is not None) and (
               int(root.GetChildAtIndex(1).GetValue(), 16) != 0):
         print_tree(root.GetChildAtIndex(1))

      print root.GetChildAtIndex(0).GetSummary()

      if (root.GetChildAtIndex(2).GetValue() is not None) and (
               int(root.GetChildAtIndex(2).GetValue(), 16) != 0):
         print_tree(root.GetChildAtIndex(2))


dictionary.c - Sample dictionary program, with bug

::

   //===-- dictionary.c ---------------------------------------------*- C -*-===//
   //
   // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
   // See https://llvm.org/LICENSE.txt for license information.
   // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
   //
   //===----------------------------------------------------------------------===//
   #include <ctype.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>

   typedef struct tree_node {
   const char *word;
   struct tree_node *left;
   struct tree_node *right;
   } tree_node;

   /* Given a char*, returns a substring that starts at the first
      alphabet character and ends at the last alphabet character, i.e. it
      strips off beginning or ending quotes, punctuation, etc. */

   char *strip(char **word) {
   char *start = *word;
   int len = strlen(start);
   char *end = start + len - 1;

   while ((start < end) && (!isalpha(start[0])))
      start++;

   while ((end > start) && (!isalpha(end[0])))
      end--;

   if (start > end)
      return NULL;

   end[1] = '\0';
   *word = start;

   return start;
   }

   /* Given a binary search tree (sorted alphabetically by the word at
      each node), and a new word, inserts the word at the appropriate
      place in the tree.  */

   void insert(tree_node *root, char *word) {
   if (root == NULL)
      return;

   int compare_value = strcmp(word, root->word);

   if (compare_value == 0)
      return;

   if (compare_value < 0) {
      if (root->left != NULL)
         insert(root->left, word);
      else {
         tree_node *new_node = (tree_node *)malloc(sizeof(tree_node));
         new_node->word = strdup(word);
         new_node->left = NULL;
         new_node->right = NULL;
         root->left = new_node;
      }
   } else {
      if (root->right != NULL)
         insert(root->right, word);
      else {
         tree_node *new_node = (tree_node *)malloc(sizeof(tree_node));
         new_node->word = strdup(word);
         new_node->left = NULL;
         new_node->right = NULL;
         root->right = new_node;
      }
   }
   }

   /* Read in a text file and storea all the words from the file in a
      binary search tree.  */

   void populate_dictionary(tree_node **dictionary, char *filename) {
   FILE *in_file;
   char word[1024];

   in_file = fopen(filename, "r");
   if (in_file) {
      while (fscanf(in_file, "%s", word) == 1) {
         char *new_word = (strdup(word));
         new_word = strip(&new_word);
         if (*dictionary == NULL) {
         tree_node *new_node = (tree_node *)malloc(sizeof(tree_node));
         new_node->word = new_word;
         new_node->left = NULL;
         new_node->right = NULL;
         *dictionary = new_node;
         } else
         insert(*dictionary, new_word);
      }
   }
   }

   /* Given a binary search tree and a word, search for the word
      in the binary search tree.  */

   int find_word(tree_node *dictionary, char *word) {
   if (!word || !dictionary)
      return 0;

   int compare_value = strcmp(word, dictionary->word);

   if (compare_value == 0)
      return 1;
   else if (compare_value < 0)
      return find_word(dictionary->left, word);
   else
      return find_word(dictionary->right, word);
   }

   /* Print out the words in the binary search tree, in sorted order.  */

   void print_tree(tree_node *dictionary) {
   if (!dictionary)
      return;

   if (dictionary->left)
      print_tree(dictionary->left);

   printf("%s\n", dictionary->word);

   if (dictionary->right)
      print_tree(dictionary->right);
   }

   int main(int argc, char **argv) {
   tree_node *dictionary = NULL;
   char buffer[1024];
   char *filename;
   int done = 0;

   if (argc == 2)
      filename = argv[1];

   if (!filename)
      return -1;

   populate_dictionary(&dictionary, filename);
   fprintf(stdout, "Dictionary loaded.\nEnter search word: ");
   while (!done && fgets(buffer, sizeof(buffer), stdin)) {
      char *word = buffer;
      int len = strlen(word);
      int i;

      for (i = 0; i < len; ++i)
         word[i] = tolower(word[i]);

      if ((len > 0) && (word[len - 1] == '\n')) {
         word[len - 1] = '\0';
         len = len - 1;
      }

      if (find_word(dictionary, word))
         fprintf(stdout, "Yes!\n");
      else
         fprintf(stdout, "No!\n");

      fprintf(stdout, "Enter search word: ");
   }

   fprintf(stdout, "\n");
   return 0;
   }


The text for "Romeo and Juliet" can be obtained from the Gutenberg Project
(http://www.gutenberg.org).

