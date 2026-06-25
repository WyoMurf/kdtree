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
got lost. I have recoded a 3D implementation and it is now in the 3d
directory. I have also created a 64-bit coordinate version of the code
for both 2d and 3d versions, and also have Go, Julia, and Rust translations
in for each coordinate size. All have the accompanying make files, and
tests. I intend to make the coordinate size configurable to 32 bit, 64 bit,
and 128 bit. Whether this is done by editing it the .h file, or setting some
environment variable during the build, is yet to be seen... or just build them
all, and include them in the same archive, and rename the functions to account
for the size (like kd_2d_32_create()). I'm just mulling over the choices now.

I've also been mulling over the coordinate spaces, and their resolution in 
terms of meters for real situations. The 32-bit coordinates have a range of
about 4 billion units. If expressing all coordinates in latitude-longitude notation,
each integer point on earth's surface would be separated by 1.11 cm at the
earth's equator. This should be sufficiently precise for most earth-based
geo databases.

However, for galactic 3d databases, this would be insufficient, but 64-bit coordinate
values should suffice, with the distance between pixels being 51.4 metersA (assuming
100,000 light-years in diameter).

And for observable universe, 128-bit coordinates should suffice, as the size of
observable universe is approx. 93 billion light-years (or 8.8e26 meters). For
64-bit integers, this would provide a resolution of approx. 47,800 kilometers between
coordinates, a planet could easily hide between coordinates. But with 128-bit coordinates,
we improve resolution to 2.6 picometers per unit. (That's about 1/40th the size of a hydrogen
atom. So, there would be some room for expansion. But, at the moment, not all languages
support this integer size. C, Rust, and Julia all have 128-bit integer primitives, but
Go requires the use of the math/big package.

I spotted a 3D implementation at http://g3d.sourceforge.net/ (G3D 
Innovation Engine) -- I haven't evaluated it yet, but based on all the 
surrounding goodies, it looks very promising!



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
 * + I added 32 bit coordinate values and 64 bit; 128 may be forthcoming.
 * + I added recoded libs for Go, Julia, and Rust, to make this code more accessible to
 *   users using those languages.

All the code appears to be working. I have a few TODO's, like somehow merging copies
of code to a single source file for 32, 64, and 128 bit values.

Building
--------

    make          # build test executables
    make test     # run soft-delete and hard-delete tests in parallel
    make clean    # remove build artifacts

For C, requires gcc. On Ubuntu/Debian: `apt install build-essential`.
On Windows/MSYS2: `pacman -S mingw-w64-x86_64-gcc`.

TO DO:

1. ~~The really_delete routine seems to have problems with the last delete.~~
   FIXED -- kd_really_delete now handles root-node deletion correctly.

2. ~~The code was part of the OctTools system, and uses other packages,
   like the error stuff (uprintf), and this stuff needs to be cleaned out
   to reduce the complexity and allow other users to determine what to
   do on errors.~~
   FIXED -- Removed ~750 lines of embedded OctTools code (port.h, uprintf,
   errtrap). Replaced with standard C11 headers and a simple kd_fatal()
   function. Code now compiles clean with -std=c11.

3. ~~kd_test needs to be upgraded to test every function. Especially the
   "Nearest Neighbor" stuff.~~
   PARTIAL -- Added kd_test_nearest.c which verifies nearest neighbor
   results against brute-force linear scan with multiple neighbor counts.

4. Valgrind comes up with all sorts of illegal reads and some illegal
   writes. All are quite mysterious. Need to clean all this up.
   NOTE: Many issues were caused by the stale path_length bug (fixed in
   TODO #1), K&R declaration mismatches (fixed in TODO #2), and a metric
   mismatch in kd_nearest where KDdist returned actual distance but
   bounds_overlap_ball used squared distance (now fixed -- KDdist uses
   squared distance internally, converted back to actual at the end).
   Re-run valgrind to see what remains.

5. ~~It would probably be nice to use configure to build from
   source. Wouldn't it?~~
   A Makefile and GitHub Actions CI workflow are now provided.

6. Hey, don't you think that other packages, like GLIB, KDE, GNOME, window
   managers and systems, anything working in 2d-space, would have a good
   geographic search mechanism built in? Oh, yes, I forgot! There will
   never be more than 32 or 64 objects in use, ever, so why bother?
   Just use linear search! Oh, and by now, they all probably have some
   really cool geographical capabilities.

Licensing:

OctTools was licensed to allow anyone to do anything with it, as long
as they kept the copyright notice. 

(See http://www.eecs.berkeley.edu/XRG/Software/Description/octtools5.2.html). 

I added my copyright for my changes,
and made MY changes available under the GNU LGPL v2 license. Life is 
complex, isn't it?


