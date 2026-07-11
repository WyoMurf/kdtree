import os
import sys

# Force X11/GLX context to prevent PyOpenGL context retrieval crashes on Wayland
os.environ['QT_QPA_PLATFORM'] = 'xcb'
os.environ['PYOPENGL_PLATFORM'] = 'glx'

import numpy as np
import pyqtgraph.opengl as gl
from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QVector3D
from astropy.table import Table
import warnings

# Ignore minor FITS warnings
warnings.filterwarnings('ignore')

def load_stars(filepath):
    print(f"Loading {filepath}...")
    try:
        tb = Table.read(filepath)
    except Exception as e:
        print(f"Error loading file: {e}")
        sys.exit(1)
        
    print(f"File loaded. Extracting coordinates for {len(tb)} stars...")
    
    # Extract arrays safely
    plx = np.array(tb['parallax'].filled(np.nan)) if hasattr(tb['parallax'], 'filled') else np.array(tb['parallax'])
    ra = np.array(tb['ra'].filled(np.nan)) if hasattr(tb['ra'], 'filled') else np.array(tb['ra'])
    dec = np.array(tb['dec'].filled(np.nan)) if hasattr(tb['dec'], 'filled') else np.array(tb['dec'])
    
    # Filter: Only keep stars with positive parallax (since d = 1/p, negative/zero p means infinite or invalid distance)
    # We also filter out NaNs just in case the file wasn't pre-filtered.
    valid_mask = (plx > 0) & ~np.isnan(plx) & ~np.isnan(ra) & ~np.isnan(dec)
    plx = plx[valid_mask]
    ra = np.radians(ra[valid_mask])
    dec = np.radians(dec[valid_mask])
    
    print(f"Projecting {len(plx)} valid stars into 3D Cartesian space...")
    
    # Distance in parsecs
    d = 1000.0 / plx  
    
    # Spherical to Cartesian conversion
    x = d * np.cos(dec) * np.cos(ra)
    y = d * np.cos(dec) * np.sin(ra)
    z = d * np.sin(dec)
    
    # Stack into an N x 3 array for the OpenGL engine
    pos = np.vstack([x, y, z]).transpose()
    return pos

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 viewer.py <path_to_fits_file>")
        print("Example: python3 viewer.py /backup/star-catalogs/GaiaSource_Filtered_000000-003111.fits.gz")
        sys.exit(1)

    # Initialize the Qt Application
    app = QApplication(sys.argv)
    
    # Initialize the 3D OpenGL Window
    w = gl.GLViewWidget()
    w.setWindowTitle('Gaia 3D Star Viewer')
    w.setGeometry(100, 100, 1280, 720)
    w.opts['distance'] = 2000 # Initial camera zoom level (in parsecs)
    w.show()
    
    # Add an XYZ coordinate axis for visual reference (X=Red, Y=Green, Z=Blue)
    # Scaled to 500 parsecs length
    w.addItem(gl.GLAxisItem(size=QVector3D(500, 500, 500)))
    
    # Load and calculate the star positions
    file_path = sys.argv[1]
    pos = load_stars(file_path)
    
    print("Sending data to GPU...")
    # Create the scatter plot
    # color=(1, 1, 1, 0.6) sets it to white with 60% opacity for a nice glowing effect when stars cluster
    # pxMode=True means the points stay exactly 1.0 pixel in size on your screen, regardless of zoom level
    scatter = gl.GLScatterPlotItem(pos=pos, color=(1, 1, 1, 0.6), size=1.0, pxMode=True)
    w.addItem(scatter)
    
    print("Rendering! You can now pan and zoom in the window.")
    print(" - Left-Click + Drag: Orbit camera")
    print(" - Middle-Click + Drag (or Shift+Left-Click): Pan camera")
    print(" - Scroll Wheel: Zoom in/out")
    
    # Start the application loop
    sys.exit(app.exec_())
