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


/**
 * Constants
 */

// default number of planets and maximum allowed count
#define COUNT 500
#define MAXCOUNT 100000

// terminal window size
#define WINW 1024
#define WINH 768

// max distance from display center when randomizing planets
#define MAXPOS 5000

// maximum mass in kg when randomizing
#define MAXKG 1e12

// maximum speed when randomizing
#define MAXV 9e-1

// pixel dimension settings when drawing planets
#define MAX_PIXEL_RADIUS 40
#define MIN_PIXEL_RADIUS 8

// gravitational constant
#define G 6.67428e-11

// default number of threads to use for calculations and the maximum
#define THREAD_COUNT 4
#define MAX_THREADS 1000


/**
 * Special types
 */

// rectangular vector for planets acceleration
typedef struct
{
  // rectangular vector
  double accelerationX;
  double accelerationY;
} accelerationVector;

// structure used to define each planet
typedef struct
{
  double x, y; // 2d position
  double mass; // mass
  double velocityX; // velocity in x direction
  double velocityY; // velocity in y direction
  accelerationVector acceleration; // gravitational acceleration
  double nearestDistance; // used to decide if this planet needs collision detection
  int flash; // flash state
  int calc; // calculation state
  
  // temporary variables used when running calculations
  double calcDistance;
  double calcGravity;
  double calcDirection;
  double calcAccelerationX;
  double calcAccelerationY;
} planet;


/**
 * declare functions
 */

void randomizePlanets();
void calculateDistance(int p1, int p2);
void calculateGravitationalForce(int p1, int p2);
void calculateGravitationalDirection(int p1, int p2);
void addGravitationalAcceleration(int p1, int p2);
int inCollisionRange(double mass1, double mass2, double distance);
void calculateCollisions();
void * calcWorker(void * args);


/**
 * globals
 */

double massMax, massMin; // planet mass extremes
planet planets[MAXCOUNT]; // array of planet structs

double pival, halfpi, twopival, sqrtpi; // calculated pi values
double sphereradc; // calculated constant for sphere radius formula
int count; // how many planets we can have
int threads; // number of calculation threads to run

// thread variables
pthread_barrier_t calcBarrier;
pthread_t calcThreads[MAX_THREADS];
pthread_mutex_t calcMutex = PTHREAD_MUTEX_INITIALIZER;


/**
 * main application method
 * 
 * usage / command line arguments
 * xgravity [planet count] [calculation threads]
 * 
 * @param argc
 * @param argv
 */
main(int argc, char *argv[]) {

  double minx, maxx, miny, maxy, cx, cy;
  int pi; // planet iterator
  long int zoomFactor; // zoom factor
  double timeFactor; // time factor
  double forceMultiplier; // force multiplier used when drawing force lines
  int centerID; // id of object to use for auto centering
  double radiusScale; // scaling of drawing radius
  int radius; // radius in pixels

  int winw, winh; // window dimensions

  double fg, td, dist; // temporary accelerating force and direction
  
  int shownum; // show stat numbers flag
  int showforce; // show force lines flag

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

  // calculate pi values
  pival = 4.0 * atan(1.0); /* get pi without having to type in 16 digits */
  sphereradc = (4 / 3 * pival) * 5000000000; // multiplied by constant for dirty density calc
  halfpi = pival / 2;
  twopival = 2 * pival;
  sqrtpi = sqrt(pival);
  
  // set thread counts to defaults
  count = COUNT;
  threads = THREAD_COUNT;
  
  // check for planet count in arguments
  if( argc > 1 ) {
    // first argument is planet count
    count = atoi(argv[1]);
    if( count > MAXCOUNT ) count = MAXCOUNT;
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

  // define color names
  #define COLOR_GREEN 0
  #define COLOR_BLUE 1
  #define COLOR_RED 2
  #define COLOR_WHITE 3
  #define COLOR_BLACK 4
  #define COLOR_STAR 5
  #define COLOR_BACKGROUND 6
  #define COLOR_FLASH 7

  #define COLOR_COUNT 8
  
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

  // initialize the thread barrier to thread count plus main
  pthread_barrier_init(&calcBarrier, NULL, threads + 1);
  
  // initialize threads
  for(pi = 0; pi < threads; pi++)
  {
    pthread_create(&calcThreads[pi], NULL, &calcWorker, NULL);
  }
  
  // initialize planets
  randomizePlanets();

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
          if( planets[pi].mass > 0 ) {
            if( planets[pi].x < minx ) minx = planets[pi].x - 500;
            if( planets[pi].x > maxx ) maxx = planets[pi].x + 500;
            if( planets[pi].y < miny ) miny = planets[pi].y - 500;
            if( planets[pi].y > maxy ) maxy = planets[pi].y + 500;
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
        randomizePlanets();
      }
      
      // wipe all planets
      else if( text[0] == 'w' ) {
        massMax = 0;
        massMin = DBL_MAX;

        for(pi = 0; pi < count; pi++) {
          planets[pi].x = 0;
          planets[pi].y = 0;
          planets[pi].velocityX = 0;
          planets[pi].velocityY = 0;
          planets[pi].mass = 0;
        }
      }
      
      else if( text[0] == 's' ) {
        // create a massive gravitation well :)
        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = MAXKG * (int)(1000 * (rand() / (RAND_MAX + 1.0)));
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
      }
      
      else if( text[0] == 'b' ) {
        // create a binary massive gravitation well :)
        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 500 - cy;
        planets[pi].mass = MAXKG * (int)(1000 * (rand() / (RAND_MAX + 1.0)));
        planets[pi].velocityX = 2;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = -500 - cy;
        planets[pi].mass = MAXKG * (int)(1000 * (rand() / (RAND_MAX + 1.0)));
        planets[pi].velocityX = -2;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
      }
      
      else if( text[0] == 'h' ) {
        // create a binary massive gravitation well :)
        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 2e14;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = -200 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = -8;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = -500 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 5;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 800 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = -4.5;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 1200 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = 3.8;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
      }
      
      else if( text[0] == 'g' ) {
        // create a binary massive gravitation well :)
        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = -200 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = -8;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = -500 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 5;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 800 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 2e14;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = -4.5;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        pi = count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 1200 - cy;
        planets[pi].mass = 5e8;
        planets[pi].velocityX = 3.25;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
      }
      
      else if( text[0] == 'p' ) {
        // sol
        pi =  0; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 1.9891e30;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        // mercury
        pi = 1; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 57909050e3 - cy;
        planets[pi].mass = 3.3022e23;
        planets[pi].velocityX = 47.87e3;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        // venus
        pi = 2; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = -108209184e3 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 4.8685e24;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 35.02e3;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        //earth
        pi = 3; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 149597887e3 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 5.9736e24;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = -29.783e3;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        //moon
        pi = 4; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 149597887e3 + 384400e3 - cx; // + 384400e3
        planets[pi].y = 0 - cy;
        planets[pi].mass = 7.3477e22;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = -29.783e3 - 1.022e3;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        // mars
        pi = 5; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 227939150e3 - cy;
        planets[pi].mass = 6.4185e23;
        planets[pi].velocityX = 24.077e3;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        // jupiter
        pi = 6; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = -778547200e3 - cy;
        planets[pi].mass = 1.8986e27;
        planets[pi].velocityX = -13.07e3;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        // saturn
        pi = 6; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 1433449369.5e3 - cy;
        planets[pi].mass = 5.6846e26;
        planets[pi].velocityX = 9.69e3;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
      }
      else if( text[0] == 'm' ) {
        //earth
        pi = 3; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 0 - cx;
        planets[pi].y = 0 - cy;
        planets[pi].mass = 5.9736e24;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = 0;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;

        //satellite in molniya orbit
        pi = 4; //count * (rand() / (RAND_MAX + 1.0));
        planets[pi].x = 6929e3 - cx; // + 384400e3
        planets[pi].y = 0 - cy;
        planets[pi].mass = 11000;
        planets[pi].velocityX = 0;
        planets[pi].velocityY = -10.0125e3;
        if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
        else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
      }
    }

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
        dist = sqrt(pow((cx + planets[pi].x) / zoomFactor + (winw / 2) - event.xbutton.x, 2) + pow((cy + planets[pi].y) / zoomFactor + (winh / 2) - event.xbutton.y, 2));
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
      if ( planets[pi].mass > 0 )
      {
        planets[pi].calc = 1;
      }
    }
    
    // wait for all threads to start calculations
    pthread_barrier_wait(&calcBarrier);
    
    // wait for all threads to end calculations
    pthread_barrier_wait(&calcBarrier);
    
    // move planets
    for(pi = 0; pi < count; pi++) {
      if( planets[pi].mass > 0 ) {
        // update planet's velocity with new acceleration
        planets[pi].velocityX += planets[pi].acceleration.accelerationX * timeFactor;
        planets[pi].velocityY += planets[pi].acceleration.accelerationY * timeFactor;

        // move planet position
        planets[pi].x += planets[pi].velocityX * timeFactor;
        planets[pi].y += planets[pi].velocityY * timeFactor;
      }
    }

    // calculate collisions
    calculateCollisions();
        
    // clear display
    XSetForeground(display, gc, drawColors[COLOR_BACKGROUND].pixel);
    XFillRectangle(display, pixmap, gc, 0, 0, winw, winh);

    // if following a planet then recenter display on the planet
    if( centerID > -1 ) {
      cx = -1 * planets[centerID].x;
      cy = -1 * planets[centerID].y;
    }

    // set radius scale of kg per pixel
    radiusScale = (massMax - massMin) / (MAX_PIXEL_RADIUS - MIN_PIXEL_RADIUS);
  
    // draw each planet
    for(pi = 0; pi < count; pi++) {
      // if planet has mass and is within the display area then we draw
      if( planets[pi].mass > 0 && 
          (cx + planets[pi].x) / zoomFactor > -1 * (winw / 2) && (cx + planets[pi].x) / zoomFactor < (winw / 2) && 
          (cy + planets[pi].y) / zoomFactor > -1 * (winh / 2) && (cy + planets[pi].y) / zoomFactor < (winh / 2) ) {
        // calculate radius relative to mass and other planets
        radius = (int)(planets[pi].mass / radiusScale) + MIN_PIXEL_RADIUS;

        // determine color by flash or radius divisions
        if( planets[pi].flash ) {
            XSetForeground(display, gc, drawColors[COLOR_FLASH].pixel);
            radius = radius * planets[pi].flash;
            planets[pi].flash -= 1;
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
                 ((cx + planets[pi].x) / zoomFactor + (winw / 2) - radius / 2), 
                 ((cy + planets[pi].y) / zoomFactor + (winh / 2) - radius / 2), 
                 radius, radius, 0, 360 * 64);

        // draw black border
        XSetForeground(display, gc, drawColors[COLOR_BLACK].pixel);
        XDrawArc(display, pixmap, gc, 
                 ((cx + planets[pi].x) / zoomFactor + (winw / 2) - radius / 2), 
                 ((cy + planets[pi].y) / zoomFactor + (winh / 2) - radius / 2), 
                 radius, radius, 0, 360 * 64);

        // show force vectors
        if( showforce > 0 ) {
          switch( showforce ) {
            case 1:
              //draw gravitational force
              XSetForeground(display, gc, drawColors[COLOR_RED].pixel);
              XDrawLine(display, pixmap, gc, 
                 ((cx + planets[pi].x) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi].y) / zoomFactor + (winh / 2)),
                 ((cx + planets[pi].x + (planets[pi].mass * planets[pi].acceleration.accelerationX) * forceMultiplier) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi].y + (planets[pi].mass * planets[pi].acceleration.accelerationY) * forceMultiplier) / zoomFactor + (winh / 2)));

              XSetForeground(display, gc, drawColors[COLOR_BLUE].pixel);
              XDrawLine(display, pixmap, gc,
                 ((cx + planets[pi].x) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi].y) / zoomFactor + (winh / 2)),
                 ((cx + planets[pi].x + (planets[pi].mass * planets[pi].velocityX * forceMultiplier / 10)) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi].y + (planets[pi].mass * planets[pi].velocityY * forceMultiplier / 10)) / zoomFactor + (winh / 2)));

              break;

            case 2:
             //draw gravitational acceleration
              XSetForeground(display, gc, drawColors[COLOR_WHITE].pixel);
              XDrawLine(display, pixmap, gc,
                 ((cx + planets[pi].x) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi].y) / zoomFactor + (winh / 2)),
                 ((cx + planets[pi].x + (planets[pi].acceleration.accelerationX) * forceMultiplier) / zoomFactor + (winw / 2)),
                 ((cy + planets[pi].y + (planets[pi].acceleration.accelerationY) * forceMultiplier) / zoomFactor + (winh / 2)));

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
              sprintf(text, "%2.2E kg", planets[pi].mass);
              break;

            // show planet velocity
            case 3:
              fg = sqrt(pow(planets[pi].velocityX, 2) + pow(planets[pi].velocityY, 2));
              td = atan2(planets[pi].velocityY, planets[pi].velocityX);
              if( isinf(td) ) td = halfpi; //pival / 2;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) < 0 ) td = pival;
              td = td * 180 / pival + 180;
              sprintf(text, "%2.2G m/s %3.0f degrees", fg, td);
              break;

            // show planet coordinates
            case 4:
              sprintf(text, "%G, %G", planets[pi].x, planets[pi].y);
              break;

            // show mass and velocity
            case 5:
              fg = sqrt(pow(planets[pi].velocityX, 2) + pow(planets[pi].velocityY, 2));
              td = atan2(planets[pi].velocityY, planets[pi].velocityX);
              if( isinf(td) ) td = halfpi; //pival / 2;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) < 0 ) td = pival;
              td = td * 180 / pival + 180;
  //font_info
  //font_height = font_info->max_bounds.ascent +
  //font_info->max_bounds.descent;

              sprintf(text, "%2.2E kg", planets[pi].mass);
              XDrawString(display, pixmap, gc, 
                          (cx + planets[pi].x) / zoomFactor + (winw / 2), 
                          (cy + planets[pi].y) / zoomFactor + (winh / 2) + 
                            font_info->max_bounds.ascent +
                            font_info->max_bounds.descent, 
                          text, strlen(text));
              sprintf(text, "%2.2G m/s %3.0f degrees", fg, td);

              break;

            // show inertia and acting gravitational force
            case 6:
              fg = planets[pi].mass * sqrt(pow(planets[pi].velocityX, 2) + pow(planets[pi].velocityY, 2));
              td = atan2(planets[pi].velocityY, planets[pi].velocityX);
              if( isinf(td) ) td = halfpi; //pival / 2;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) < 0 ) td = pival;
              td = td * 180 / pival + 180;

              sprintf(text, "   P = %2.2G Ns %3.0f degrees", fg, td);
              XDrawString(display, pixmap, gc,
                          (cx + planets[pi].x) / zoomFactor + (winw / 2),
                          (cy + planets[pi].y) / zoomFactor + (winh / 2) +
                            font_info->max_bounds.ascent +
                            font_info->max_bounds.descent,
                          text, strlen(text));


              fg = planets[pi].mass * sqrt(pow(planets[pi].acceleration.accelerationX, 2) + pow(planets[pi].acceleration.accelerationY, 2));
              td = atan2(planets[pi].acceleration.accelerationY, planets[pi].acceleration.accelerationX);
              if( isinf(td) ) td = halfpi; //pival / 2;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) > 0 ) td = 0;
              if( isnan(td) && (planets[pi].velocityX - planets[pi].velocityX) < 0 ) td = pival;
              td = td * 180 / pival + 180;
              sprintf(text, "   Fg = %2.2G N %3.0f degrees", fg, td);

              break;

          }
          
          XDrawString(display, pixmap, gc, (cx + planets[pi].x) / zoomFactor + (winw / 2), (cy + planets[pi].y) / zoomFactor + (winh / 2), text, strlen(text));
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
void randomizePlanets()
{
  int i;
  double r, a;
  
  // seed
  srand((unsigned int)time((time_t *)NULL));
  massMax = 0;
  massMin = DBL_MAX;

  // loop through all planets
  for(i = 0; i < count; i++) {
    // randomize polar coordinates from center
    r = MAXPOS * (rand() / (RAND_MAX + 1.0));
    a = twopival * (rand() / (RAND_MAX + 1.0));
    
    // convert polar coordinates into rectangular
    planets[i].x = (r * cos(a));
    if( isnan(planets[i].x) )
    {
      planets[i].x = 0;
    }
    else if( isinf(planets[i].x) )
    {
      planets[i].x = r;
    }

    planets[i].y = (r * sin(a));
    if( isnan(planets[i].y) )
    {
      planets[i].y = 0;
    }
    else if( isinf(planets[i].y) )
    {
      planets[i].y = r;
    }
      
    // random planet velocity
    planets[i].velocityX = (2 * MAXV * (rand() / (RAND_MAX + 1.0))) - MAXV;
    planets[i].velocityY = (2 * MAXV * (rand() / (RAND_MAX + 1.0))) - MAXV;
    
    // random mass
    planets[i].mass = MAXKG * (pow(1/sqrt(pival), -1 * pow(rand() / (RAND_MAX + 1.0), 2) / 0.75) - 1);
    
    // reset flash and calculating flags
    planets[i].flash = 0;
    planets[i].calc = 0;
    
    // keep track of the maximum mass size
    if( planets[i].mass > massMax ) massMax = planets[i].mass;
    else if( planets[i].mass < massMin ) massMin = planets[i].mass;
  }
}


/**
 * calculate the distance between two planets
 * 
 * @param p1
 * @param p2
 * @return 
 */
void calculateDistance(int p1, int p2)
{
  planets[p1].calcDistance = sqrt(pow(planets[p1].x - planets[p2].x, 2) + pow(planets[p1].y - planets[p2].y, 2));
}


/**
 * calculate the gravitational acceleration between two planets
 * 
 * @param p1
 * @param p2
 * @return 
 */
void calculateGravitationalAcceleration(int p1, int p2)
{
  calculateDistance(p1, p2);
  
  if ( planets[p1].calcDistance < planets[p1].nearestDistance )
  {
    planets[p1].nearestDistance = planets[p1].calcDistance;
  }
  
  planets[p1].calcGravity = G * (planets[p1].mass * planets[p2].mass / pow(planets[p1].calcDistance, 2));
}


/**
 * calculate the polar direction of the gravitational force between two planets
 * 
 * @param p1
 * @param p2
 * @return 
 */
void calculateGravitationalDirection(int p1, int p2)
{
  planets[p1].calcDirection = atan2(planets[p2].y - planets[p1].y, planets[p2].x - planets[p1].x);
  if( isinf(planets[p1].calcDirection) )
  {
    planets[p1].calcDirection = pival / 2;
  }
  else if( isnan(planets[p1].calcDirection) && (planets[p2].x - planets[p1].x) > 0 )
  {
    planets[p1].calcDirection = 0;
  }
  else if( isnan(planets[p1].calcDirection) && (planets[p2].x - planets[p1].x) < 0 )
  {
    planets[p1].calcDirection = pival;
  }
}


/**
 * add the gravitational acceleration from planet 2 on planet 1 parameters
 * 
 * @param p1
 * @param p2
 */
void addGravitationalAcceleration(int p1, int p2)
{
  // calculate the polar acceleration between two planets
  calculateGravitationalAcceleration(p1, p2);
  calculateGravitationalDirection(p1, p2);
  
  planets[p1].calcAccelerationX = (planets[p1].calcGravity / planets[p1].mass) * cos(planets[p1].calcDirection);
  if( isnan(planets[p1].calcAccelerationX) )
  {
    planets[p1].calcAccelerationX = 0;
  }
  else if( isinf(planets[p1].calcAccelerationX) )
  {
    planets[p1].calcAccelerationX = planets[p1].calcGravity / planets[p1].mass;
  }
  
  planets[p1].calcAccelerationY = (planets[p1].calcGravity / planets[p1].mass) * sin(planets[p1].calcDirection);
  if( isnan(planets[p1].calcAccelerationY) )
  {
    planets[p1].calcAccelerationY = 0;
  }
  else if( isinf(planets[p1].calcAccelerationY) )
  {
    planets[p1].calcAccelerationY = planets[p1].calcGravity / planets[p1].mass;
  }
  
  planets[p1].acceleration.accelerationX += planets[p1].calcAccelerationX;
  planets[p1].acceleration.accelerationY += planets[p1].calcAccelerationY;
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
  if ( cbrt(mass1 / sphereradc) + cbrt(mass2 / sphereradc) >= distance )
  {
    // collision
    return 1;
  }
  
  return 0;
}


void calculateCollisions()
{
  int pi, vi;
  double dist;
  
  // calculate collisions
  for(pi = 0; pi < count; pi++) {
    // only need to process if this planet not consumed and worst case planet came too close
    if ( planets[pi].mass > 0 && inCollisionRange(planets[pi].mass, massMax, planets[pi].nearestDistance) )
    {
       // check all planets to find collisions
      for(vi = 0; vi < count; vi++) {
        // not self, other planet has mass, and other planet mass is less than or equal
        if ( vi != pi && planets[vi].mass > 0 && planets[vi].mass <= planets[pi].mass )
        {
          calculateDistance(pi, vi);
          dist = planets[pi].calcDistance;

          // simple collision
          if( inCollisionRange(planets[pi].mass, planets[vi].mass, dist) ) {
            // collision
            planets[pi].velocityX = (planets[pi].velocityX * planets[pi].mass + planets[vi].velocityX * planets[vi].mass) / (planets[pi].mass + planets[vi].mass);
            planets[pi].velocityY = (planets[pi].velocityY * planets[pi].mass + planets[vi].velocityY * planets[vi].mass) / (planets[pi].mass + planets[vi].mass);
            planets[pi].mass += planets[vi].mass;
            if( planets[pi].mass > massMax ) massMax = planets[pi].mass;
            else if( planets[pi].mass < massMin ) massMin = planets[pi].mass;
            planets[vi].mass = 0;
            planets[pi].flash = 10;
          }
        }
      }
    }
  }
}


/**
 * worker thread
 * 
 * performs gravitational calculations in a multi-thread safe environment
 * 
 * @param args
 * @return 
 */
void * calcWorker(void * args)
{
  int p, i;
  
  while (1)
  {
    p = 0;
    
    // wait for for all calculation ready
    pthread_barrier_wait(&calcBarrier);
  
    while (p < count)
    {
      p = getNextCalcIndex(p);
      
      if ( p < count )
      {
        // reset gravity values for this planet
        planets[p].acceleration.accelerationX = 0;
        planets[p].acceleration.accelerationY = 0;
        planets[p].nearestDistance = DBL_MAX;
        
        // calculate between individual planets
        for(i = 0; i < count; i++) {
          if( i != p && planets[i].mass > 0 ) {
            addGravitationalAcceleration(p, i);
          }
        }
      }
    } // planet gravitational calculation loop
    
    // wait for for all calculation finished
    pthread_barrier_wait(&calcBarrier);
    
  } // main loop
  
}


/**
 * thread safe method to lock a specific planet index for calculations
 * 
 * @param p
 * @return 
 */
int getNextCalcIndex(int p)
{
  int i;
  
  pthread_mutex_lock(&calcMutex);
  
  for(i = p; i < count; i++)
  {
    if ( planets[i].calc )
    {
      planets[i].calc = 0;
      pthread_mutex_unlock(&calcMutex);
      return i;
    }
  }
    
  pthread_mutex_unlock(&calcMutex);
  
  return i;
}
