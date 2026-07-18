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
rectangles together so as 500,000 stars in each "chunk". They are available in gzip-compressed 
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
"GaiaSource_Filtered_<grid-range>.kdtree" All of this takes about 2.5 minutes for each "chunk".
Forming a kd-tree of 157,000 stars only takes a little over a second, including writing it out
serialized to disk. Reading it in via memory mapping is even faster.
The original .csv.gz files are removed. The total space for the resulting data is about 90G total. The .csv data are estimated to take up about 9 Terabytes of disk to store, so moving
to the .fits file format, and weeding out stars without a "solid" parallax value cuts total
storage space to 1/100th the space. The run_pipeline.sh script will run the sets in parallel so as to speed up the whole process. My workstation can safely run 3 parallel pipelines. 10 pipelines freezes my meager workstation, and I have to reboot it to get it back.

The viewer.py justs reads the first "chunk" of the .fits.gz data, and displays the ~159,000 stars contained therein. It's pretty quick. The C version uses the raylib library to display 
the stars, but my poor 2060 RTX gpu card can realistically only about 30 of the ~3300 files.
I've had it read in the full 3300 files, but it's thrashing so badly, it's pretty worthless
as an interactive pan/zoom tool. But with 30 files, I can fly thru the data and watch the
stars fly by as I travel at FTL speeds.

Someday, they'll send out two probes at opposite ends of a Neptunish sized orbit, get them
sync'd up so that they take their images at the same ~second, and then they'll get really
good, solid parallax value without the individual star movements to clutter things up-- 
that should cover the entire galaxy, and then some.

I picture star maps that are hierarchical, covering individual stars, and their entire
planetary systems as an index into a separate database, with carefully timestamped data
that will allow true current position based on individual star and planet movements.

Any way, it's a demonstration tool. I had a lot of fun building it. Very educational! 
Hope you enjoy.

And, many thanks to Gemini, which helped a great deal.

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

and when you have a sufficient sample set, you can run either the python viewer, 
or the C viewer.


First, copy the desired viewer, or both, to the directory where the data is kept:

cp viewer.py C/viewer <star-catalog data dir>
cd <star-catalog data dir>
python viewer.py <path to a GaiaSource_Filtered_<range>.fits.gz file>
and/or:
./viewer

This whole copy-everything-to-the-datadir-and-run-locally-from-there thing, was done
to make it easy for you to use a disk that could handle 90 Gb to 9 Tb of data. Not 
everyone has that kind of space available in their home directory!
