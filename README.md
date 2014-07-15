kdtree
======

Back in the 1980's, David Harrison of UC-Berkeley wrote a wonderful
implementation of a kd-tree geographic search algorithm, as part of
the OctTree project, which was a tool to design Integrated Circuits.
Here, transistor structures needed to be placed edge to edge to form
wires and logical circuits. OctTools provided tools to automatically
generate some circuit parts, and also provided an editor to allow
users to manually view and manipulate the drawings. To do all this 
efficiently, they needed a quick 2-d bounding-box search mechanism.
This is the package that provided it.

While the rest of the world seems to have fallen in love with quad trees,
and other mechanisms, I was inspired by kd-trees, and my tests using
this code showed that kd-trees are efficient, can handle large 
datasets, and are pretty resilient.

A recent search of the web for kdtree implementations turned up nothing
but point searches in 1-d space. For practical purposes, forgive the pun,
but this is pointless. This implementation is for generic 2-d objects,
which could include circles, ellipses, lines, etc, which will all have
a bounding box to describe their minimum and maximum x and y boundaries.
The tree uses the bottom, top, left, and right sides of bounding boxes
so that each side is used to split the data at each level in the tree,
in a cyclical fashion, as you descend the tree to the leaves.

I wrote this code back in 1990, and only discovered it again recently.
I decided to make it available to the world, as I really haven't found
any good public geographic databases available.

Back in 1990, I also wrote a 3D kdtree implementation, but that code
got lost. Sorry. Really, it's not that hard to take this
code and turn it into a 3D database. I spotted a 3D implementation
at http://g3d.sourceforge.net/ (G3D Innovation Engine) -- I haven't 
evaluated it yet, but based on all the surrounding goodies, it looks 
very promising!

Also, this code assumes that bounding boxes are 32-bit integers.
It would not be difficult to turn that into longs or long longs and
get 64-bit resolution.


Here are the list of my changes to his code:

 * + my build used the nodes son's links to form lists, rather than the list package.
 *   This saves time in that malloc is called much less often.
 * + my build uses the geometric mean criteria for finding central nodes, rather than
 *   the centroid of the bounding box. This, on the average, halves the depth of the
 *   tree. Research on random boxes shows that halving the depth of the tree decreases
 *   search traversal 15% Thus are kd trees resilient to degradation.
 * + Added nearest neighbor search routine. TODO: allow the user to pass in pointer
 *   to distance function.
 * + Added rebuild routine. Faster than a build from scratch.
 * + Added node deletion routine. For those purists who hate dead nodes in the tree.
 * + Added some routines to give stats on tree health, info about tree, etc.
 * + I may even have inserted some comments to explain some tricky stuff happening
 *   in the code...
 * + the original code was written in ancient k&r style; it wasn't even compiling
 *   with gcc any more. I updated the code to use function prototypes, and exclude
 *   code that used varargs.h. The porting mechanism no longer applies to the 
 *   current state of compilers.
 * + upgraded kd_test.c to use 1 million random boxes; seems more appropriate 
 *   for todays faster computers and bigger datasets.

Not all of it is working yet. But, eventually, I'll grab a moment here and there
to finish it off.

TO DO:

1. The really_delete routine seems to have problems with the last delete.

2. The code was part of the OctTools system, and uses other packages,
   like the error stuff (uprintf), and this stuff needs to be cleaned out
   to reduce the complexity and allow other users to determine what to 
   do on errors.

3. kd_test needs to be upgraded to test every function. Especially the
   "Nearest Neighbor" stuff.

4. Valgrind comes up with all sorts of illegal reads and some illegal
   writes. All are quite mysterious. Need to clean all this up.

5. It would probably be nice to use configure to build from
   source. Wouldn't it?

6. Hey, don't you think that other packages, like GLIB, KDE, GNOME, window 
   managers and systems, anything working in 2d-space, would have a good
   geographic search mechanism built in? Oh, yes, I forgot! There will
   never be more than 32 or 64 objects in use, ever, so why bother?
   Just use linear search!

Licensing:

OctTools was licensed to allow anyone to do anything with it, as long
as they kept the copyright notice. 

(See http://www.eecs.berkeley.edu/XRG/Software/Description/octtools5.2.html). 

I added my copyright for my changes,
and made MY changes available under the GNU LGPL v2 license. Life is 
complex, isn't it?


