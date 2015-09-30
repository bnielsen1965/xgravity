/**
 *

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

 * Author: Bryan Nielsen <bnielsen1965@gmail.com>
 * Date: 2014-10-09
 */

#include <X11/Xlib.h> // Every Xlib program must include this
//#include <assert.h>   // I include this to test return values the lazy way
#include <unistd.h>   // So we got the profile for 10 seconds
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <float.h>
#include <pthread.h> 
#include "xgravity.h"


/**
 * main application method
 * 
 * usage / command line arguments
 * xgravity [planet count] [calculation threads]
 * 
 * @param argc
 * @param argv
 */
int main(int argc, char *argv[]) {
  
  pthread_t calcThreads[MAX_THREADS];
  pthread_barrier_t calcBarrier;
  pthread_mutex_t calcMutex = PTHREAD_MUTEX_INITIALIZER;
  calcArgs calcThreadArgs;
  int threads; // number of calculation threads to run
  
  planet *planets[MAXCOUNT];
  planet *aPlanet;
  
  double minx, maxx, miny, maxy, cx, cy, massMax, massMin, timeFactor, forceMultiplier, radiusScale;
  int pi, count; // planet iterator
  long int zoomFactor; // zoom factor
  int centerID; // id of object to use for auto centering
  int radius; // radius in pixels

  double fg, td, dist; // temporary accelerating force and direction
  
  int shownum; // show stat numbers flag
  int showforce; // show force lines flag

  int winw, winh; // window dimensions

  Display *display;
  int screen;
  Window window;
  XEvent event;
  KeySym key;
  char text[255];
  Pixmap pixmap;
  GC gc;
  Colormap colormap;
  
  XGCValues values_return;
  XFontStruct *font_info;
  GContext gid;

  
  // check for planet count in arguments
  if( argc > 1 ) {
    // first argument is planet count
    count = atoi(argv[1]);
    if( count > MAXCOUNT ) count = MAXCOUNT;
  }
  else {
    count = COUNT;
  }
  
  // check for thread count in arguments
  if( argc > 2 ) {
    // first argument is planet count
    threads = atoi(argv[2]);
    if ( threads < 1 )
    {
      threads = THREAD_COUNT;
    }
    else if ( threads > MAX_THREADS )
    {
      threads = MAX_THREADS;
    }
  }
  else {
    threads = THREAD_COUNT;
  }

  // set default control values
  timeFactor = 1; // calculation time factor in seconds
  zoomFactor = 4; // start zoomed out a bit
  forceMultiplier = 1e-8; // need a small multiplier to shrink force lines
  shownum = 0; // do not show numbers
  showforce = 0; // do not show force lines
  centerID = -1; // not centered on any planets
  winw = WINW;
  winh = WINH;
  cx = 0;
  cy = 0;

  // setup Xwindow
  display = XOpenDisplay(NULL);
  if( display == NULL) {
    printf("Cannot open display\n");
    exit(1);
  }

  screen = DefaultScreen(display);
  window = XCreateSimpleWindow(
    display, 
    RootWindow(display, screen), 
    10, 
    10, 
    winw, 
    winh, 
    1,
    WhitePixel(display, screen), 
    BlackPixel(display, screen));
  XMapWindow(display, window);
  XFlush(display);

  // show the window ID in case need to use for screen grab / cast
  printf("Window ID:%d\r\n", window);

  XSelectInput (display, window, KeyPressMask | StructureNotifyMask | ButtonPressMask);

  pixmap = XCreatePixmap(display, window, winw, winh, DefaultDepth(display, screen));
  XFlush(display);

  colormap = DefaultColormap(display, 0);
  gc = XCreateGC(display, pixmap, 0, 0);

  // define color codes
  char *colorCodes[] = {
    "#009900",
    "#4444FF",
    "#FF4400",
    "#FFFFFF",
    "#000000",
    "#FFFF00",
    "#A0A0A0",
    "#E0D1FF"
  };
  
  // create colors for palette
  XColor drawColors[COLOR_COUNT];
  for(pi = 0; pi < COLOR_COUNT; pi++)
  {
    XParseColor(display, colormap, colorCodes[pi], &drawColors[pi]);
    XAllocColor(display, colormap, &drawColors[pi]);
  }
  
  gid = XGContextFromGC(gc);
  font_info = XQueryFont(display, gid);

  XSetForeground(display, gc, drawColors[COLOR_BACKGROUND].pixel);
  XFillRectangle(display, pixmap, gc, 0, 0, winw, winh);
  XCopyArea(display, pixmap, window, gc, 0, 0, winw, winh, 0, 0);
  XFlush(display);

  
  // allocate memory for planet data
  for ( pi = 0; pi < count; pi++ ) {
    planets[pi] = (planet *) malloc(sizeof(planet));
  }
  
  // initialize planets
  randomizePlanets(planets, count);

  // initialize the thread barrier to thread count plus main
  pthread_barrier_init(&calcBarrier, NULL, threads + 1);

  // collect thread arguments into struct
  calcThreadArgs.planetData = planets;
  calcThreadArgs.count = count;
  calcThreadArgs.calcBarrier = &calcBarrier;
  calcThreadArgs.calcMutex = &calcMutex;
  
  // initialize threads
  for(pi = 0; pi < threads; pi++)
  {
    pthread_create(&calcThreads[pi], NULL, &calcWorker, &calcThreadArgs);
  }
  
  // main application loop
  while(1) {

    // keyboard events
    if( XCheckMaskEvent(display, KeyPressMask, &event) && XLookupString(&event.xkey, text, 255, &key, 0)==1 ) {
      // quit
      if (text[0]=='q') {
        XCloseDisplay(display);
        exit(0);
      }
      
      // toggle show force lines
      if( text[0] == 'f' ) {
        showforce += 1;
        if( showforce > 3 ) showforce = 0;
      }
      
      // adjust force line multiplier dimension
      if( text[0] == 'd' ) forceMultiplier = forceMultiplier / 10;
      if( text[0] == 'D' ) forceMultiplier = forceMultiplier * 10;
      
      // toggle show stat numbers
      if( text[0] == 'o' ) {
        shownum += 1;
        if( shownum > 6 ) shownum = 0;
      }
      
      // toggle calculation time factor in seconds
      else if( text[0] == 't' ) timeFactor = timeFactor / 10;
      else if( text[0] == 'T' ) timeFactor = timeFactor * 10;
      
      // zoom in
      else if( text[0] == 'z' ) {
        zoomFactor -= 1;
        if( zoomFactor < 1 ) zoomFactor = 1;
      }
      else if( text[0] == 'Z' ) {
        zoomFactor = zoomFactor / 2;
        if( zoomFactor < 1 ) zoomFactor = 1;
      }
      
      // zoom out
      else if( text[0] == 'x' ) zoomFactor += 1;
      else if( text[0] == 'X' ) zoomFactor = 2 * zoomFactor;
      
      // reset view to no zoom
      else if( text[0] == 'v' ) zoomFactor = 1;
      
      // reset center
      else if( text[0] == 'c' ) {
        cx = 0;
        cy = 0;
      }
      
      // auto zoom to show all planets
      else if( text[0] == 'a' ) {
        minx = 0;
        maxx = 0;
        miny = 0;
        maxy = 0;
        
        // use planets to find minimum and maximum position values for zoom window
        for(pi = 0; pi < count; pi++) {
          if( planets[pi]->mass > 0 ) {
            if( planets[pi]->x < minx ) minx = planets[pi]->x - 500;
            if( planets[pi]->x > maxx ) maxx = planets[pi]->x + 500;
            if( planets[pi]->y < miny ) miny = planets[pi]->y - 500;
            if( planets[pi]->y > maxy ) maxy = planets[pi]->y + 500;
          }
        }
        
        // calculate zoom factor
        if( (maxx - minx) / (double)winw > (maxy - miny) / (double)winh ) zoomFactor = (long int)((maxx - minx) / (double)winw);
        else zoomFactor = (long int)((maxy - miny) / (double)winh) + 1;

        // fractional zoom not allowed
        if( zoomFactor < 1 ) zoomFactor = 1;

        // recenter view
        cx = (double)-1.0 * (minx + (maxx - minx) / (double)2.0);
        cy = (double)-1.0 * (miny + (maxy - miny) / (double)2.0);
      }
      
      // re-randomize planets
      else if( text[0] == 'r' ) {
        randomizePlanets(planets, count);
      }
      
      // wipe all planets
      else if( text[0] == 'w' ) {
        massMax = 0;
        massMin = DBL_MAX;
        clearPlanets(planets, count);
      }
      
      else if( text[0] == 's' ) {
        // create a gravitation well
        createGravityWell(planets, count, cx, cy);
      }
      
      else if( text[0] == 'b' ) {
        // create a binary gravitation well
        createBinaryWell(planets, count, cx, cy);
      }
      
      else if( text[0] == 'h' ) {
        // create a heliocentric system
        createHeliocentricSystem(planets, count, cx, cy);
      }
      
      else if( text[0] == 'g' ) {
        // create some geocentric nonsense
        createGeocentricSystem(planets, count, cx, cy);
      }
      
      else if( text[0] == 'p' ) {
        // create Sol planetary system
        createPlanetarySystem(planets, count, cx, cy);
      }
      else if( text[0] == 'm' ) {
        // create molniya orbit
        createMolniyaOrbit(planets, count, cx, cy);
      }
    } // end of keyboard events

    // window config events
    if( XCheckMaskEvent(display, StructureNotifyMask, &event) && event.type == ConfigureNotify ) {
      if (event.xconfigure.window == window) {
        winw = event.xconfigure.width;
        winh = event.xconfigure.height;
        XFreePixmap(display, pixmap);
        XFlush(display);

        pixmap = XCreatePixmap(display, window, winw, winh, DefaultDepth(display, screen));
        XFlush(display);
      }
    }

    // mouse button events
    if( XCheckMaskEvent(display, ButtonPressMask, &event) ) {
      centerID = -1;

      // if clicked on planet then select as centerID for auto centering
      for(pi = 0; pi < count; pi++) {
        dist = sqrt(pow((cx + planets[pi]->x) / zoomFactor + (winw / 2) - event.xbutton.x, 2) + pow((cy + planets[pi]->y) / zoomFactor + (winh / 2) - event.xbutton.y, 2));
        if( dist < 4 ) {
          centerID = pi;
          continue;
        }
      }

      // if not centered on a planet then recenter to click point
      if( centerID == -1 ) {
        cx += zoomFactor * (winw / 2 - event.xbutton.x);
        cy += zoomFactor * (winh / 2 - event.xbutton.y);
      }
      // keep in mind that cx, cy is not the center coordinate, it is the direction to shift
    }


    // set calculation state on for each planet that has mass
    for(pi = 0; pi < count; pi++)
    {
      if ( planets[pi]->mass > 0 )
      {
        planets[pi]->calc = 1;
      }
    }
    
    // wait for all threads to start calculations
    pthread_barrier_wait(&calcBarrier);
    
    // wait for all threads to end calculations
    pthread_barrier_wait(&calcBarrier);
    
    // move planets after calculations
    movePlanets(timeFactor, planets, count);
    
    // calculate collisions
    calculateCollisions(planets, count);
    massMax = getMassMax(planets, count);
    massMin = getMassMin(planets, count);
        
    // clear display
    XSetForeground(display, gc, drawColors[COLOR_BACKGROUND].pixel);
    XFillRectangle(display, pixmap, gc, 0, 0, winw, winh);

    // if following a planet then recenter display on the planet
    if( centerID > -1 ) {
      cx = -1 * planets[centerID]->x;
      cy = -1 * planets[centerID]->y;
    }

    // set radius scale of kg per pixel
    radiusScale = (massMax - massMin) / (MAX_PIXEL_RADIUS - MIN_PIXEL_RADIUS);
  
    // draw each planet
    for(pi = 0; pi < count; pi++) {
      // if planet has mass and is within the display area then we draw
      if( planets[pi]->mass > 0 && 
          (cx + planets[pi]->x) / zoomFactor > -1 * (winw / 2) && (cx + planets[pi]->x) / zoomFactor < (winw / 2) && 
          (cy + planets[pi]->y) / zoomFactor > -1 * (winh / 2) && (cy + planets[pi]->y) / zoomFactor < (winh / 2) ) {
        // calculate radius relative to mass and other planets
        radius = (int)(planets[pi]->mass / radiusScale) + MIN_PIXEL_RADIUS;

        // determine color by flash or radius divisions
        if( planets[pi]->flash ) {
            XSetForeground(display, gc, drawColors[COLOR_FLASH].pixel);
            radius = radius * planets[pi]->flash;
            planets[pi]->flash -= 1;
        }
        else if( radius > 16 ) {
          // size is color for a star
          XSetForeground(display, gc, drawColors[COLOR_STAR].pixel);
        }
        else if( radius <= 16 && radius > 12 ) {
          // size is color of blue planet
          XSetForeground(display, gc, drawColors[COLOR_BLUE].pixel);
        }
        else {
          // default color for smallest is green
          XSetForeground(display, gc, drawColors[COLOR_GREEN].pixel);
        }

        // draw planet dot
        XFillArc(display, pixmap, gc, 
                 ((cx + planets[pi]->x) / zoomFactor + (winw / 2) - radius / 2), 
                 ((cy + planets[pi]->y) / zoomFactor + (winh / 2) - radius / 2), 
                 radius, radius, 0, 360 * 64);

        // draw black border
        XSetForeground(display, gc, drawColors[COLOR_BLACK].pixel);
        XDrawArc(display, pixmap, gc, 
                 ((cx + planets[pi]->x) / zoomFactor + (winw / 2) - radius / 2), 
                 ((cy + planets[pi]->y) / zoomFactor + (winh / 2) - radius / 2), 
                 radius, radius, 0, 360 * 64);

        // show force vectors
        if( showforce > 0 ) {
          switch( showforce ) {
            case 1:
              //draw gravitational force
              XSetForeground(display, gc, drawColors[COLOR_RED].pixel);
              XDrawLine(display, pixmap, gc, 
                 ((cx + planets[pi]->x) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi]->y) / zoomFactor + (winh / 2)),
                 ((cx + planets[pi]->x + (planets[pi]->mass * planets[pi]->acceleration.accelerationX) * forceMultiplier) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi]->y + (planets[pi]->mass * planets[pi]->acceleration.accelerationY) * forceMultiplier) / zoomFactor + (winh / 2)));

              XSetForeground(display, gc, drawColors[COLOR_BLUE].pixel);
              XDrawLine(display, pixmap, gc,
                 ((cx + planets[pi]->x) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi]->y) / zoomFactor + (winh / 2)),
                 ((cx + planets[pi]->x + (planets[pi]->mass * planets[pi]->velocityX * forceMultiplier / 10)) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi]->y + (planets[pi]->mass * planets[pi]->velocityY * forceMultiplier / 10)) / zoomFactor + (winh / 2)));

              break;

            case 2:
             //draw gravitational acceleration
              XSetForeground(display, gc, drawColors[COLOR_WHITE].pixel);
              XDrawLine(display, pixmap, gc,
                 ((cx + planets[pi]->x) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi]->y) / zoomFactor + (winh / 2)),
                 ((cx + planets[pi]->x + (planets[pi]->acceleration.accelerationX) * forceMultiplier) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi]->y + (planets[pi]->acceleration.accelerationY) * forceMultiplier) / zoomFactor + (winh / 2)));

              break;
          }
        }

        // show stat values
        if( shownum > 0 ) {
          // set text color
          XSetForeground(display, gc, drawColors[COLOR_WHITE].pixel);

          switch( shownum ) {
            // show planet id number
            case 1:
              sprintf(text, "ID:%d", pi);
              break;

            // show planet mass
            case 2:
              sprintf(text, "%2.2E kg", planets[pi]->mass);
              break;

            // show planet velocity
            case 3:
              fg = sqrt(pow(planets[pi]->velocityX, 2) + pow(planets[pi]->velocityY, 2));
              td = atan2(planets[pi]->velocityY, planets[pi]->velocityX);
              if( isinf(td) ) td = M_PI / 2;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) < 0 ) td = M_PI;
              td = td * 180 / M_PI + 180;
              sprintf(text, "%2.2G m/s %3.0f degrees", fg, td);
              break;

            // show planet coordinates
            case 4:
              sprintf(text, "%G, %G", planets[pi]->x, planets[pi]->y);
              break;

            // show mass and velocity
            case 5:
              fg = sqrt(pow(planets[pi]->velocityX, 2) + pow(planets[pi]->velocityY, 2));
              td = atan2(planets[pi]->velocityY, planets[pi]->velocityX);
              if( isinf(td) ) td = M_PI / 2;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) < 0 ) td = M_PI;
              td = td * 180 / M_PI + 180;
  //font_info
  //font_height = font_info->max_bounds.ascent +
  //font_info->max_bounds.descent;

              sprintf(text, "%2.2E kg", planets[pi]->mass);
              XDrawString(display, pixmap, gc, 
                          (cx + planets[pi]->x) / zoomFactor + (winw / 2), 
                          (cy + planets[pi]->y) / zoomFactor + (winh / 2) + 
                            font_info->max_bounds.ascent +
                            font_info->max_bounds.descent, 
                          text, strlen(text));
              sprintf(text, "%2.2G m/s %3.0f degrees", fg, td);

              break;

            // show inertia and acting gravitational force
            case 6:
              fg = planets[pi]->mass * sqrt(pow(planets[pi]->velocityX, 2) + pow(planets[pi]->velocityY, 2));
              td = atan2(planets[pi]->velocityY, planets[pi]->velocityX);
              if( isinf(td) ) td = M_PI / 2;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) < 0 ) td = M_PI;
              td = td * 180 / M_PI + 180;

              sprintf(text, "   P = %2.2G Ns %3.0f degrees", fg, td);
              XDrawString(display, pixmap, gc,
                          (cx + planets[pi]->x) / zoomFactor + (winw / 2),
                          (cy + planets[pi]->y) / zoomFactor + (winh / 2) +
                            font_info->max_bounds.ascent +
                            font_info->max_bounds.descent,
                          text, strlen(text));


              fg = planets[pi]->mass * sqrt(pow(planets[pi]->acceleration.accelerationX, 2) + pow(planets[pi]->acceleration.accelerationY, 2));
              td = atan2(planets[pi]->acceleration.accelerationY, planets[pi]->acceleration.accelerationX);
              if( isinf(td) ) td = M_PI / 2;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi]->velocityX - planets[pi]->velocityX) < 0 ) td = M_PI;
              td = td * 180 / M_PI + 180;
              sprintf(text, "   Fg = %2.2G N %3.0f degrees", fg, td);

              break;

          }
          
          XDrawString(display, pixmap, gc, (cx + planets[pi]->x) / zoomFactor + (winw / 2), (cy + planets[pi]->y) / zoomFactor + (winh / 2), text, strlen(text));
        }
      }
    }

    // apply drawn bitmap
    XCopyArea(display, pixmap, window, gc, 0, 0, winw, winh, 0, 0);
    XFlush(display);

  }

  XCloseDisplay(display);

}


/**
 * Randomize the location, velocity, and mass of all planets.
 */
void randomizePlanets(planet *planetData[], int count)
{
  int i;
  double r, a;
  
  // seed
  srand((unsigned int)time((time_t *)NULL));
//  massMax = 0;
//  massMin = DBL_MAX;

  // loop through all planets
  for(i = 0; i < count; i++) {
    // randomize polar coordinates from center
    r = MAXPOS * (rand() / (RAND_MAX + 1.0));
    a = (2 * M_PI) * (rand() / (RAND_MAX + 1.0));
    
    // convert polar coordinates into rectangular
    planetData[i]->x = (r * cos(a));
    if( isnan(planetData[i]->x) )
    {
      planetData[i]->x = 0;
    }
    else if( isinf(planetData[i]->x) )
    {
      planetData[i]->x = r;
    }

    planetData[i]->y = (r * sin(a));
    if( isnan(planetData[i]->y) )
    {
      planetData[i]->y = 0;
    }
    else if( isinf(planetData[i]->y) )
    {
      planetData[i]->y = r;
    }
      
    // random planet velocity
    planetData[i]->velocityX = (2 * MAXV * (rand() / (RAND_MAX + 1.0))) - MAXV;
    planetData[i]->velocityY = (2 * MAXV * (rand() / (RAND_MAX + 1.0))) - MAXV;
    
    // random mass
    planetData[i]->mass = MAXKG * (pow(1/sqrt(M_PI), -1 * pow(rand() / (RAND_MAX + 1.0), 2) / 0.75) - 1);
    
    // reset flash and calculating flags
    planetData[i]->flash = 0;
    planetData[i]->calc = 0;
  }
}


/**
 * Clear the planet settings.
 * 
 * @param planetData
 * @param count
 */
void clearPlanets(planet *planetData[], int count)
{
  int i;

  for(i = 0; i < count; i++) {
    planetData[i]->x = 0;
    planetData[i]->y = 0;
    planetData[i]->velocityX = 0;
    planetData[i]->velocityY = 0;
    planetData[i]->mass = 0;
  }
}


/**
 * Create a large gravity well.
 * 
 * @param planetData
 * @param count
 */
void createGravityWell(planet *planetData[], int count, int cx, int cy)
{
  int pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = MAXKG * (int)(1000 * (rand() / (RAND_MAX + 1.0)));
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 0;
  
}


/**
 * Create a binary orbiting gravity well.
 * 
 * @param planetData
 * @param count
 * @param cx
 * @param cy
 */
void createBinaryWell(planet *planetData[], int count, int cx, int cy)
{
  int pi;
  
  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 500 - cy;
  planetData[pi]->mass = MAXKG * (int)(1000 * (rand() / (RAND_MAX + 1.0)));
  planetData[pi]->velocityX = 2;
  planetData[pi]->velocityY = 0;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = -500 - cy;
  planetData[pi]->mass = MAXKG * (int)(1000 * (rand() / (RAND_MAX + 1.0)));
  planetData[pi]->velocityX = -2;
  planetData[pi]->velocityY = 0;
}


/**
 * Create a heliocentric system.
 * 
 * @param planetData
 * @param count
 * @param cx
 * @param cy
 */
void createHeliocentricSystem(planet *planetData[], int count, int cx, int cy)
{
  int pi;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 2e14;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 0;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = -200 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = -8;
  planetData[pi]->velocityY = 0;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = -500 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 5;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 800 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = -4.5;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 1200 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = 3.8;
  planetData[pi]->velocityY = 0;
}


/**
 * Create a geocentric system.
 * 
 * @param planetData
 * @param count
 * @param cx
 * @param cy
 */
void createGeocentricSystem(planet *planetData[], int count, int cx, int cy)
{
  int pi;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 0;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = -200 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = -8;
  planetData[pi]->velocityY = 0;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = -500 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 5;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 800 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 2e14;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = -4.5;

  pi = count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 1200 - cy;
  planetData[pi]->mass = 5e8;
  planetData[pi]->velocityX = 3.25;
  planetData[pi]->velocityY = 0;
}


/**
 * Create Sol planetary system out to Saturn.
 * 
 * @param planetData
 * @param count
 * @param cx
 * @param cy
 */
void createPlanetarySystem(planet *planetData[], int count, int cx, int cy)
{
  int pi;

  // sol
  pi =  0; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 1.9891e30;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 0;

  // mercury
  pi = 1; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 57909050e3 - cy;
  planetData[pi]->mass = 3.3022e23;
  planetData[pi]->velocityX = 47.87e3;
  planetData[pi]->velocityY = 0;

  // venus
  pi = 2; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = -108209184e3 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 4.8685e24;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 35.02e3;

  //earth
  pi = 3; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 149597887e3 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 5.9736e24;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = -29.783e3;

  //moon
  pi = 4; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 149597887e3 + 384400e3 - cx; // + 384400e3
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 7.3477e22;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = -29.783e3 - 1.022e3;

  // mars
  pi = 5; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 227939150e3 - cy;
  planetData[pi]->mass = 6.4185e23;
  planetData[pi]->velocityX = 24.077e3;
  planetData[pi]->velocityY = 0;

  // jupiter
  pi = 6; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = -778547200e3 - cy;
  planetData[pi]->mass = 1.8986e27;
  planetData[pi]->velocityX = -13.07e3;
  planetData[pi]->velocityY = 0;

  // saturn
  pi = 7; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 1433449369.5e3 - cy;
  planetData[pi]->mass = 5.6846e26;
  planetData[pi]->velocityX = 9.69e3;
  planetData[pi]->velocityY = 0;
}


/**
 * Create molniya orbit around earth.
 * 
 * @param planetData
 * @param count
 * @param cx
 * @param cy
 */
void createMolniyaOrbit(planet *planetData[], int count, int cx, int cy)
{
  int pi;

  pi = 3; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 0 - cx;
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 5.9736e24;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = 0;

  //satellite in molniya orbit
  pi = 4; //count * (rand() / (RAND_MAX + 1.0));
  planetData[pi]->x = 6929e3 - cx; // + 384400e3
  planetData[pi]->y = 0 - cy;
  planetData[pi]->mass = 11000;
  planetData[pi]->velocityX = 0;
  planetData[pi]->velocityY = -10.0125e3;
}


/**
 * calculate the distance between two planets
 * 
 * @param p1
 * @param p2
 * @return 
 */
void calculateDistance(int p1, int p2, planet *planetData[])
{
  planetData[p1]->calcDistance = sqrt(pow(planetData[p1]->x - planetData[p2]->x, 2) + pow(planetData[p1]->y - planetData[p2]->y, 2));
}


/**
 * calculate the gravitational acceleration between two planets
 * 
 * @param p1
 * @param p2
 * @return 
 */
void calculateGravitationalAcceleration(int p1, int p2, planet *planetData[])
{
  calculateDistance(p1, p2, planetData);
  
  if ( planetData[p1]->calcDistance < planetData[p1]->nearestDistance )
  {
    planetData[p1]->nearestDistance = planetData[p1]->calcDistance;
  }
  
  planetData[p1]->calcGravity = G * (planetData[p1]->mass * planetData[p2]->mass / pow(planetData[p1]->calcDistance, 2));
}


/**
 * calculate the polar direction of the gravitational force between two planets
 * 
 * @param p1
 * @param p2
 * @return 
 */
void calculateGravitationalDirection(int p1, int p2, planet *planetData[])
{
  planetData[p1]->calcDirection = atan2(planetData[p2]->y - planetData[p1]->y, planetData[p2]->x - planetData[p1]->x);
  if( isinf(planetData[p1]->calcDirection) )
  {
    planetData[p1]->calcDirection = M_PI / 2;
  }
  else if( isnan(planetData[p1]->calcDirection) && (planetData[p2]->x - planetData[p1]->x) > 0 )
  {
    planetData[p1]->calcDirection = 0;
  }
  else if( isnan(planetData[p1]->calcDirection) && (planetData[p2]->x - planetData[p1]->x) < 0 )
  {
    planetData[p1]->calcDirection = M_PI;
  }
}


/**
 * add the gravitational acceleration from planet 2 on planet 1 parameters
 * 
 * @param p1
 * @param p2
 */
void addGravitationalAcceleration(int p1, int p2, planet *planetData[])
{
  // calculate the polar acceleration between two planets
  calculateGravitationalAcceleration(p1, p2, planetData);
  calculateGravitationalDirection(p1, p2, planetData);
  
  planetData[p1]->calcAccelerationX = (planetData[p1]->calcGravity / planetData[p1]->mass) * cos(planetData[p1]->calcDirection);
  if( isnan(planetData[p1]->calcAccelerationX) )
  {
    planetData[p1]->calcAccelerationX = 0;
  }
  else if( isinf(planetData[p1]->calcAccelerationX) )
  {
    planetData[p1]->calcAccelerationX = planetData[p1]->calcGravity / planetData[p1]->mass;
  }
  
  planetData[p1]->calcAccelerationY = (planetData[p1]->calcGravity / planetData[p1]->mass) * sin(planetData[p1]->calcDirection);
  if( isnan(planetData[p1]->calcAccelerationY) )
  {
    planetData[p1]->calcAccelerationY = 0;
  }
  else if( isinf(planetData[p1]->calcAccelerationY) )
  {
    planetData[p1]->calcAccelerationY = planetData[p1]->calcGravity / planetData[p1]->mass;
  }
  
  planetData[p1]->acceleration.accelerationX += planetData[p1]->calcAccelerationX;
  planetData[p1]->acceleration.accelerationY += planetData[p1]->calcAccelerationY;
}


/**
 * Adjust planet velocity and move based on time factor.
 */
void movePlanets(double timeFactor, planet *planetData[], int count)
{
  int pi;
  
  // move planets
  for(pi = 0; pi < count; pi++) {
    if( planetData[pi]->mass > 0 ) {
      // update planet's velocity with new acceleration
      planetData[pi]->velocityX += planetData[pi]->acceleration.accelerationX * timeFactor;
      planetData[pi]->velocityY += planetData[pi]->acceleration.accelerationY * timeFactor;

      // move planet position
      planetData[pi]->x += planetData[pi]->velocityX * timeFactor;
      planetData[pi]->y += planetData[pi]->velocityY * timeFactor;
    }
  }
}


/**
 * Calculate collisions between planets.
 */
void calculateCollisions(planet *planetData[], int count)
{
  int pi, vi;
  double dist, massMax;
  
  massMax = getMassMax(planetData, count);
  
  // calculate collisions
  for(pi = 0; pi < count; pi++) {
    // only need to process if this planet not consumed and worst case planet came too close
    if ( planetData[pi]->mass > 0 && inCollisionRange(planetData[pi]->mass, massMax, planetData[pi]->nearestDistance) )
    {
       // check all planets to find collisions
      for(vi = 0; vi < count; vi++) {
        // not self, other planet has mass, and other planet mass is less than or equal
        if ( vi != pi && planetData[vi]->mass > 0 && planetData[vi]->mass <= planetData[pi]->mass )
        {
          calculateDistance(pi, vi, planetData);
          dist = planetData[pi]->calcDistance;

          // simple collision
          if( inCollisionRange(planetData[pi]->mass, planetData[vi]->mass, dist) ) {
            // collision
            planetData[pi]->velocityX = (planetData[pi]->velocityX * planetData[pi]->mass + planetData[vi]->velocityX * planetData[vi]->mass) / (planetData[pi]->mass + planetData[vi]->mass);
            planetData[pi]->velocityY = (planetData[pi]->velocityY * planetData[pi]->mass + planetData[vi]->velocityY * planetData[vi]->mass) / (planetData[pi]->mass + planetData[vi]->mass);
            planetData[pi]->mass += planetData[vi]->mass;
            
            planetData[vi]->mass = 0;
            planetData[pi]->flash = 10;
          }
        }
      }
    }
  }
}


/**
 * determine if the two given masses within the given distance are considered to be in a collision
 * 
 * @param mass1
 * @param mass2
 * @param distance
 * @return 
 */
int inCollisionRange(double mass1, double mass2, double distance)
{
  double sphereradc; // calculated constant for sphere radius formula
  sphereradc = (4 / 3 * M_PI) * 5000000000; // multiplied by constant for dirty density calc

  if ( cbrt(mass1 / sphereradc) + cbrt(mass2 / sphereradc) >= distance )
  {
    // collision
    return 1;
  }
  
  return 0;
}


/**
 * Get the mass maximum in the group of planets.
 * 
 * @param planetData
 * @param count
 * @return 
 */
double getMassMax(planet *planetData[], int count)
{
  int pi;
  double massMax = 0;
  
  // find mass max
  for(pi = 0; pi < count; pi++) {
    if( planetData[pi]->mass > massMax ) massMax = planetData[pi]->mass;
  }
  
  return massMax;
}


/**
 * Get the mass minimum in the group of planets.
 * 
 * @param planetData
 * @param count
 * @return 
 */
double getMassMin(planet *planetData[], int count)
{
  int pi;
  double massMin = DBL_MAX;
  
  // find mass max
  for(pi = 0; pi < count; pi++) {
    if( planetData[pi]->mass < massMin ) massMin = planetData[pi]->mass;
  }
  
  return massMin;
}


/**
 * worker thread
 * 
 * performs gravitational calculations in a multi-thread safe environment
 * 
 * @param args A pointer to arguments, in this case a calcArgs struct.
 * @return 
 */
void * calcWorker(void * args)
{
  calcArgs *threadArgs;
  planet **planetData;  
  int p, i;
  
  threadArgs = (calcArgs *) args;
  planetData = (*threadArgs).planetData;
  
  while (1)
  {
    p = 0;
    
    // wait for for all calculation ready
    pthread_barrier_wait((*threadArgs).calcBarrier);
  
    while (p < (*threadArgs).count)
    {
      p = getNextCalcIndex(p, threadArgs);
      
      if ( p < (*threadArgs).count )
      {
        // reset gravity values for our planet
        planetData[p]->acceleration.accelerationX = 0;
        planetData[p]->acceleration.accelerationY = 0;
        planetData[p]->nearestDistance = DBL_MAX;
        
        // calculate acceleration between individual planets and our planet
        for(i = 0; i < (*threadArgs).count; i++) {
          if( i != p && planetData[i]->mass > 0 ) {
            addGravitationalAcceleration(p, i, planetData);
          }
        }
      }
    } // planet gravitational calculation loop
    
    // wait for for all calculations finished
    pthread_barrier_wait((*threadArgs).calcBarrier);
    
  } // main loop
}


/**
 * thread safe method to lock a specific planet index for calculations
 * 
 * @param p The starting index to use for the look up.
 * @param planetData Pointer to the planet data array.
 * @return 
 */
int getNextCalcIndex(int p, calcArgs *calcThreadArgs)
{
  int i;
  
  pthread_mutex_lock((*calcThreadArgs).calcMutex);
  
  for(i = p; i < (*calcThreadArgs).count; i++)
  {
    if ( (*calcThreadArgs).planetData[i]->calc )
    {
      (*calcThreadArgs).planetData[i]->calc = 0;
      pthread_mutex_unlock((*calcThreadArgs).calcMutex);
      return i;
    }
  }
    
  pthread_mutex_unlock((*calcThreadArgs).calcMutex);
  
  return i;
}
