xgravity --  A 2D gravity simulation written in C and using X display and pthreads to speed up calculations on multi-core systems.
==============

xgravity is a 2D Newtonian gravity simulator written in C that uses X to display the simulation graphically and uses pthreads to speed up calculations on multi-core CPUs.

Building xgravity
--------------

Requires libX11 development libraries.

A bash shell script is included to simplify the build process. It assumes you are using gcc and have the appropriate libraries installed.

The script will attempt to build both 32 bit and 64 bit executables. If you do not want to build both or do not have the libraries for both then you can use the first gcc command in the script to compile for your system.

Building is as simple as the following command...

    sh xgravity-build.sh


Command line arguments
--------------

xgravity will accept two arguments, the number of planets to work with and the number of threads to use for the calculations.

Running xgravity with 500 planets and 2 threads would require the following command...

    ./xgravity 500 2


X Interface
--------------

Once xgravity is running you can use the following keyboard commands:

q - quit

z - zoom in (reduce the number of meters / pixel)
Z - zoom in 2x current scale

x - zoom out (increase the number of meters / pixel)
X - zoom out current scale / 2

v - reset the zoom value to 1 meter per pixel
a - adjust zoom and center to view all objects
c - center the view at 0, 0

r - restart from beginning, will randomize new objects
w - wipe all objects from space

(the following will randomly choose from existing objects and reassign)
s - drop a large sized object at the center of the screen (sun)
b - drop a binary large object that will begin to orbit one another
h - drop a heliocentric orbital system
g - drop a geocentric orbital system (not stable, duh)

(will use up objects 0 through 7)
p - drop sol from the sun to jupiter, this is tricky due to the massive numbers. to use successfully press w c p a in sequence.

(will use up objects 3 and 4)
m - drop a Molniya orbiting pair.

t/T - reduce/increase time scale by 1 magnitude
(note that increasing the time scale increases the inherent error in the calculation. this is a very simple simulation)

o - toggle between object info views

f - toggle between force lines display
d/D - adjust force line dimensional multiplier

Left click in the window to recenter the view.
Cick on a planet to follow a specific planet.

Thats about it, enjoy.

bnielsen1965@gmail.com


Files
--------------------

xgravity.c - The C source code for the simulation.
xgravity-build.sh - A bash script to simplify the process of compiling the source code.
README.md - This readme file.
xgravity - Executable for the specific system on which the source is compiled.
xgravity-32 - A 32 bit executable.
xgravity-64 - A 64 bit executable.
