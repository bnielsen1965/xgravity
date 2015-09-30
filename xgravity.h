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

/**
 * Constants
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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



/**
 * Special types
 */

/**
 * rectangular vector for planets acceleration
 */
typedef struct
{
  // rectangular vector
  double accelerationX;
  double accelerationY;
} accelerationVector;


/**
 *  structure used to define each planet
 */
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
 * struct to pass arguments to calculation threads
 */
typedef struct
{
  planet **planetData; // pointer to array of pointers to planet structs
  int count; // planet count
  pthread_barrier_t *calcBarrier; // pointer to sychronization barrier
  pthread_mutex_t *calcMutex; // pointer to mutex for discrete planet index selection
} calcArgs;


/**
 * declare functions
 */

void randomizePlanets(planet *planetData[], int count);
void clearPlanets(planet *planetData[], int count);
void createGravityWell(planet *planetData[], int count, int cx, int cy);
void createBinaryWell(planet *planetData[], int count, int cx, int cy);
void createHeliocentricSystem(planet *planetData[], int count, int cx, int cy);
void createGeocentricSystem(planet *planetData[], int count, int cx, int cy);
void createPlanetarySystem(planet *planetData[], int count, int cx, int cy);
void createMolniyaOrbit(planet *planetData[], int count, int cx, int cy);

double getMassMax(planet *planetData[], int count);
double getMassMin(planet *planetData[], int count);

void calculateDistance(int p1, int p2, planet *planetData[]);
void calculateGravitationalForce(int p1, int p2);
void calculateGravitationalDirection(int p1, int p2, planet *planetData[]);
void addGravitationalAcceleration(int p1, int p2, planet *planetData[]);
int inCollisionRange(double mass1, double mass2, double distance);
void movePlanets(double timeFactor, planet *planetData[], int count);
void calculateCollisions(planet *planetData[], int count);

void * calcWorker(void *args);
int getNextCalcIndex(int p, calcArgs *calcThreadArgs);
