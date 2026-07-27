Demonstration: viewing 3-D star-catalog Data

I wondered if it would make a good demonstration of 3-d kdtrees, if we could display
star catalog data. To reliably store such 3-d data, which is all indexed by right-ascention,
declination, and parallax data, we have to have pretty accurate calculations of parallax,
or the distance to a star. The Gaia Star catalog contains about 1.8 billion stars. The
parallax distance is only reliably calculated for about (crudely) 157 million of them.
And, my poor graphics card (RTX 2060) can really only handle maybe 2.5 million at a time.

The process of collecting the star catalog data is not a very simple affair, it turned out.
The sky is pictured as a celestial sphere, where every star has a right-ascension and
declination coordinate. The probe is sitting on one of the earth's Lagrange points. It takes
pictures of the entire sky every half-year, and compares images taken of the same spot
a half-year ago, when the two images were taken from opposite ends of the earth's orbit.
The distance between the two observation points is 2 AU. They then can calculate using
geometry the distance (parallax) of the star. There's all sorts of complexity involved,
but we'll ignore most of that for the time being. The celestial sphere is split up into
786,432 chunks, each with a number. (It's a system called "HEALPix" (lev 8) -- look it up in
Wikipedia (https://en.wikipedia.org/wiki/HEALPix). The Gaia folks have grouped several grid 
rectangles together so have about 500,000 stars in each "chunk". They are available in gzip-compressed 
CSV files. The basic data (ra, dec, and parallax, plus a bunch of other relevant data) is in
GaiaSource (grid coord-grid coord).csv.gz. Another set of data is stored in the 
Astrophysical_parameters files, which has other data, like how bright, the spectral data,
the odds of the star belonging to particular classes of stars, etc. etc. My scripts
download a "chunk" of the GaiaSource, and its corresponding Astrophysical_paramaters, and
merges the two into a single table, and removes all the "bad" stars with unresolvable
parallax values, probable binary stars, etc. etc. and saves that into a "fits" file format,
compressed with gzip. Fits format is used, because it's mostly binary, and provides better
compression ratios. These filtered, compressed files are titled 
"GaiaSource_Filtered_<grid-range>.fits.gz". Then I take each "chunk", form it into a 3d kdtree
and write a serialized version of the kdtree to disk titled: 
"GaiaSource_Filtered_<grid-range>.kdtree" All of this takes about 2.5 minutes for each "chunk". I then improved the code to run on C instead of Python, and run in parallel, reducing the necessary to download and process the entire GaiaSouce database (with the Astrophysical_params) down to 5-6 hours, which will depend on your internet connection speed, and how much memory you have in your workstation.
Forming a kd-tree of 157,000 stars only takes a a second, including writing it out
serialized to disk. Reading it in via memory mapping is even faster. I have challenged gemini
to reduce this time, both in getting out of Python, and into C instead, and running everything
in parallel. Experimentation tells me, that for my environment (starlink, 64G mem, etc), 5
levels of parallelism was Ok, getting some complaints about internet bandwidth being saturated,
etc. At 5 threads, I was getting a time of 5:30:06 (5+1/2 hours), which was much, much better
than the whole week of previous coding. 4 levels of parallelism took 6:07:26, and 3 levels took
6:46:36 hours. Below 5 levels, I had no complaints about internet bandwidth saturation.
The original .csv.gz files are removed. The total space for the resulting data is about 90G total. The .csv data are estimated to take up about 9 Terabytes of disk to store, so moving
to the .fits file format, and weeding out stars without a "solid" parallax values cuts total
storage space to 1/100th the space.  My workstation can safely run 3-5 parallel pipelines. 10 
pipelines freezes my meager workstation, and I have to reboot it to get it back.

The viewer.py justs reads the first "chunk" of the .fits.gz data, and displays the ~159,000 stars contained therein. It's pretty quick. The C version uses the raylib library to display 
the stars, but my poor 2060 RTX gpu card can realistically only about 30 of the ~3300 files.
I've had it read in the full 3300 files, but it's thrashing so badly, it's pretty worthless
as an interactive pan/zoom tool. But with 30 files, I can fly thru the data and watch the
stars fly by as I travel at FTL speeds.

But, then, I used Claude to further upgrade the viewer. Lazy loading of star data based on what is actually needed to display the current window. For example, why wait around for half the data to load, when it is behind you, and not visible in the current view frame? As a result, of that and using better LOD criteria, a two-level kdtree scheme where the botton tree layer contains star object, and the top layer is a tree of sub-tree objects, and a better way to display close-up stars, I can now fly thru the entire 157-million star field at 3-30 frames per second.

Someday, they'll send out two probes at opposite ends of a Neptunish sized orbit, get them
sync'd up so that they take their images at the same ~second, and then they'll get really
good, solid parallax value without the individual star movements to clutter things up-- 
that should cover the entire galaxy, and then some.

I picture star maps that are hierarchical, covering individual stars, and their entire
planetary systems as an index into a separate database, with carefully timestamped data
that will allow true current position based on individual star and planet movements. Also, I picture galaxy objects, with data containg their orientation relative to our galaxy, and a tree of their stars, each one pointing to a tree of that star's palnetary systems, asterioid belts, etc. etc.

Some drawbacks to this approach (mapping celestial sphere coordinates to cartesian), is that
a 3-d viewer will see, in any random view of the stars, perhaps hundreds, if not thousands, 
of separate kd-trees in each view. Each grid of the HEALPix sphere forms a "ray" of stars
sprinkled over incredible distances. I have played with splitting that ray into separate
trees based on distance, which forms many more kdtrees, but reduces bounding box overlap
a bit. A kdtree of these split-up chunks could be created, so that any one view might not
retrieve such huge numbers of stars. If you are viewing all the stars at once, well, such
is life! 

Any way, it's a demonstration tool. I had a lot of fun building it. Very educational! 
Hope you enjoy.

And, many thanks to Gemini, which helped a great deal. And Claude as well. My impression is these tools are very useful, speed up development work, off-load a world of trivia from the developer, help quickly nail down bugs and problems, not only with programs, but with system problems, and even email issues as well. I have heard complaints about "AI slop", writing buggy code, writing ineffible code,  etc, and my opinion is that you have to do have to make sure it does everything you hoped it would do. But even when it does overlook some aspects of operation, you can always ask it to go back and add that in. And it will do it. As far as ineffible code, my opinion is, that humans can write the most ineffible code, too. Just look at the Obfuscated C Code Contest winners (http://ioccc.org/years.html). And a lot of open-source code is poorly commented, and tough to read. And as to AI-slop, well, if it generates code that actually works, and passes all your tests, then it isn't generating slop. If it doesn't work right in the field, then perhaps your tests are the slop.

BUILDING:

I did the work in the AlmaLinux 10.1 and 10.2 environments. The package list does not
include the astropy or gaiadr3_zeropoint. Add to that, they discourage the use of pip for 
global installations. The solution: create a virtual environment.

cd <project-dir>
python -m venv gaia

Then activate it:

source gaia/bin/activate

Then use pip to install the necessary packages:

pip install astropy astropy-iers-data gaiadr3_zeropoint
<when you try to run the viewer, or the run_pipeline.sh script, it may complain about missing
 packages. Add such via the pip install mechanism>
 
The C/viewer will need the ray library. It's source can be downloaded from
github:

git clone https://github.com/raysan5/raylib.git

follow instructions to install it on your system. The .so to the right place, and 
the include files to /usr/include/.

The scripts will also require the CFITSIO library from HEASARC:

git clone https://github.com/HEASARC/cfitsio.git

(or, https://heasarc.gsfc.nasa.gov/docs/software/fitsio/fitsio.html)

build and install as per instructions.

The actual Gaia star (source) data is at:

https://cdn.gea.esac.esa.int/Gaia/gdr3/gaia_source/

and the astrophysical data is at:

https://cdn.gea.esac.esa.int/Gaia/gdr3/Astrophysical_parameters/astrophysical_parameters/



 
To download the data, merge, and filter the data,

First, copy the run_pipeline.sh, process_chunk_shell.sh, and fits2kd to the directory
you want to gather the data in (~100 Gigabytes required there):

In the kdtree project dir, do:     make
cp run_pipeline.sh process_chunk_shell.sh C/fits2kd <dir you want to download>
 
Then you can simply run the run_pipeline.sh script to begin downloading the star 
catalog data. You can change the level of parallelism by editing the run_pipeline script.
I have the default parallelism set to 5. You may find this too high or too low for your
setup. Feel free to modify the value of MAX_THREADS in both run_pipeline.sh and
process_chunk_shell.sh.

run_pipeline.sh gives you, per chunk, both a "full" .kdtree (every star in that chunk)
and 10 more .kdtree files split by parallax (i.e. by distance band) - segments 0
through 9. Segment 9 is the closest band (parallax 20-1,000,000 mas, roughly 0-50pc,
the solar neighborhood); segment 0 is the farthest (parallax 0-0.5 mas, ~2000pc out
to the catalog's detection limit).

Once you've got a batch of .kdtree files, there are two more one-time steps before
the C viewer can fly through them: annotating every shard with LOD (level-of-detail)
data via kd2lod, and building a meta-index (a kd-tree of the shard files themselves,
so the viewer can find which ones are actually near the camera instead of opening
every single one at startup) via build_metatree. build_lod_index.sh does both:

cp build_lod_index.sh C/kd2lod C/build_metatree <star-catalog data dir>
cd <star-catalog data dir>
./build_lod_index.sh

This only ever touches the parallax-segment files (*-0.kdtree .. *-9.kdtree), not the
bare per-chunk "full" trees - the segments already cover the same stars, split by
distance, so that's what the viewer actually opens. It produces a .kdtree.lod sidecar
next to every shard, plus catalog.metatree / catalog.manifest / catalog.metatree.lod
(the meta-index) in the same directory. It's safe to re-run any time you've added
more chunks - it just re-annotates and rebuilds the meta-index from whatever .kdtree
files are present.

and when you have a sufficient sample set, you can run either the python viewer, 
or the C viewer.


First, copy the desired viewer, or both, to the directory where the data is kept:

cp viewer.py C/viewer <star-catalog data dir>
cd <star-catalog data dir>
python viewer.py <path to a GaiaSource_Filtered_<range>.fits.gz file>
and/or:
./viewer

The C viewer needs catalog.metatree (and its .manifest/.metatree.lod) to already
exist in the current directory - that's what build_lod_index.sh above produces.
It only ever looks in the current directory, never anywhere else, so if you get
"catalog.metatree not found", you're either in the wrong directory or haven't run
build_lod_index.sh yet. Once it's found, the viewer lazily mmaps only the shards
whose bounding box is actually near the camera - you can point it at your whole
catalog, all 10 distance segments, not just a hand-picked subset, and it'll only
load what a given flight path actually needs.

If you want to run the python viewer, remember to activate the virtual enviroment in
your shell session first.

This whole copy-everything-to-the-datadir-and-run-locally-from-there thing, was done
to make it easy for you to use a disk that could handle 90 Gb to 9 Tb of data. Not 
everyone has that kind of space available in their home directory!
