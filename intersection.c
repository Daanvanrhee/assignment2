#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "arrivals.h"
#include "intersection_time.h"

#include "input.h"

/*
 * curr_arrivals[][][]
 *
 * A 3D array that stores the arrivals that have occurred
 * The first two indices determine the entry lane: first index is Side, second index is Direction
 * curr_arrivals[s][d] returns an array of all arrivals for the entry lane on side s for direction d,
 *   ordered in the same order as they arrived
 */
static Arrival curr_arrivals[4][3][20];

/*
 * semaphores[][]
 *
 * A 2D array that defines a semaphore for each entry lane,
 *   which are used to signal the corresponding traffic light that a car has arrived
 * The two indices determine the entry lane: first index is Side, second index is Direction
 */
static sem_t semaphores[4][3];

// Sections that efficiently represent the intersection intersection paths
typedef enum
{
  ExitNorth = 0,
  ExitSouth = 1,
  ExitWest = 2,
  CenterNW = 3,
  CenterNE = 4,
  CenterSW = 5,
  CenterSE = 6
} IntersectionSection;

/*
 * A mutex that is used to lock the intersection
 */
pthread_mutex_t section_locks[7];

/*
 * supply_arrivals()
 *
 * A function for supplying arrivals to the intersection
 * This should be executed by a separate thread
 */
static void *supply_arrivals()
{
  int t = 0;
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

  // for every arrival in the list
  for (int i = 0; i < sizeof(input_arrivals) / sizeof(Arrival); i++)
  {
    // get the next arrival in the list
    Arrival arrival = input_arrivals[i];
    // wait until this arrival is supposed to arrive
    sleep(arrival.time - t);
    t = arrival.time;
    // store the new arrival in curr_arrivals
    curr_arrivals[arrival.side][arrival.direction][num_curr_arrivals[arrival.side][arrival.direction]] = arrival;
    num_curr_arrivals[arrival.side][arrival.direction] += 1;
    // increment the semaphore for the traffic light that the arrival is for
    sem_post(&semaphores[arrival.side][arrival.direction]);
  }

  return (0);
}

// The side and direction of a traffic light
typedef struct
{
  Side side;           // the side of the intersection at whcih the car arrives
  Direction direction; // the direction the car wants to go
} TrafficLight;

/*
 * is_section_on_path()
 *
 * A function that determines whether a section is on a path
 */
static bool is_section_on_path(IntersectionSection section, TrafficLight *path)
{
  switch (path->side)
  {
  case NORTH:
    switch (path->direction)
    {
    case STRAIGHT:
      return section == CenterNW || section == CenterSW || section == ExitSouth;
    case RIGHT:
      return section == ExitWest;
    }
  case EAST:
    switch (path->direction)
    {
    case LEFT:
      return section == CenterSE || section == ExitSouth;
    case STRAIGHT:
      return section == CenterNE || section == CenterNW || section == ExitWest;
    case RIGHT:
      return section == ExitNorth;
    }
  case SOUTH:
    switch (path->direction)
    {
    case LEFT:
      return section == CenterNW || section == CenterSE || section == ExitWest;
    case STRAIGHT:
      return section == CenterNE || section == CenterSE || section == ExitNorth;
    }
  case WEST:
    switch (path->direction)
    {
    case LEFT:
      return section == CenterSW || section == CenterNE || section == ExitNorth;
    case RIGHT:
      return section == ExitSouth;
    }
  }
  return false;
}

/*
 * manage_light(void* arg)
 *
 * A function that implements the behaviour of a traffic light
 */
static void *
manage_light(void *arg)
{
  TrafficLight *traffic_light = (TrafficLight *)arg;

  // Count the total number of arrivals for this traffic light
  int total_expected_arrivals = 0;
  for (int i = 0; i < sizeof(input_arrivals) / sizeof(Arrival); i++)
  {
    Arrival arrival = input_arrivals[i];
    if (arrival.side == traffic_light->side && arrival.direction == traffic_light->direction)
    {
      total_expected_arrivals++;
    }
  }

  // Wait for all arrivals to be handled
  int handled_arrival_this_traffic_light = 0;
  while (handled_arrival_this_traffic_light < total_expected_arrivals)
  {
    // Wait for a car to arrive
    sem_wait(&semaphores[traffic_light->side][traffic_light->direction]);

    // Lock all sections on the path
    int i = 0;
    while (i < 7)
    {
      if (is_section_on_path(i, traffic_light))
      {
        if (pthread_mutex_trylock(&section_locks[i]) == EBUSY)
        {
          // If a section is locked, unlock all sections that have been locked
          for (int j = 0; j < i; j++)
          {
            if (is_section_on_path(j, traffic_light))
            {
              pthread_mutex_unlock(&section_locks[j]);
            }
          }
          // Try again
          i = 0;
          continue;
        }
      }
      i++;
    }

    Arrival arrival = curr_arrivals[traffic_light->side][traffic_light->direction][handled_arrival_this_traffic_light];

    // Wait until the car crosses the intersection
    printf("traffic light %d %d turns green at time %d for car %d\n", traffic_light->side, traffic_light->direction, get_time_passed(), arrival.id);
    sleep(CROSS_TIME);

    // Car has crossed the intersection
    printf("traffic light %d %d turns red at time %d\n", traffic_light->side, traffic_light->direction, get_time_passed());
    handled_arrival_this_traffic_light++;

    // Unlock all sections on the path
    for (int i = 0; i < 7; i++)
    {
      if (is_section_on_path(i, traffic_light))
      {
        pthread_mutex_unlock(&section_locks[i]);
      }
    }
  }

  // Free the memory allocated for the traffic light
  free(arg);
  return NULL;
}

int main(int argc, char *argv[])
{
  // Initialize mutexes
  for (int i = 0; i < 7; i++)
  {
    pthread_mutex_init(&section_locks[i], NULL);
  }

  // Initialize semaphores
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_init(&semaphores[i][j], 0, 0);
    }
  }

  // Create threads for each side and direction
  pthread_t light_threads[4][3];
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      // Create a traffic light in the heap and pass it to the thread
      TrafficLight *traffic_light = malloc(sizeof(TrafficLight));
      traffic_light->side = i;
      traffic_light->direction = j;
      pthread_create(&light_threads[i][j], NULL, manage_light, (void *){traffic_light});
    }
  }

  start_time();

  pthread_t supply_thread;
  pthread_create(&supply_thread, NULL, supply_arrivals, NULL);

  // Wait for all threads to finish
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      pthread_join(light_threads[i][j], NULL);
    }
  }
  pthread_join(supply_thread, NULL);

  // Destroy semaphores and mutexes
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }
  for (int i = 0; i < 7; i++)
  {
    pthread_mutex_destroy(&section_locks[i]);
  }

  return 0;
}