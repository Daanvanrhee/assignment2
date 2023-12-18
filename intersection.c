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
int handled_arrivals = 0;
/*
 * lock
 *
 * A mutex that is used to lock the intersection
 */
pthread_mutex_t lock; // Declare a mutex

/*
 * semaphores[][]
 *
 * A 2D array that defines a semaphore for each entry lane,
 *   which are used to signal the corresponding traffic light that a car has arrived
 * The two indices determine the entry lane: first index is Side, second index is Direction
 */
static sem_t semaphores[4][3];

/*
 * supply_arrivals()
 *
 * A function for supplying arrivals to the intersection
 * This should be executed by a separate thread
 */
static void* supply_arrivals()
{
  int t = 0;
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

  // for every arrival in the list
  for (int i = 0; i < sizeof(input_arrivals)/sizeof(Arrival); i++)
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

  return(0);
}


/*
 * manage_light(void* arg)
 *
 * A function that implements the behaviour of a traffic light
 */
static void* manage_light(void* arg)
{
  Arrival* arrival = (Arrival*)arg;
  int car_id = arrival->id;
  Side side = arrival->side;
  Direction direction = arrival->direction;
  int total_cars = sizeof(input_arrivals) / sizeof(Arrival);
  while (handled_arrivals < total_cars)
  {
    sem_wait(&semaphores[side][direction]);
    pthread_mutex_lock(&lock);
    printf("traffic light %d %d turns green at time %d for car %d\n", side, direction, get_time_passed(), car_id);
    sleep(CROSS_TIME);
    printf("traffic light %d %d turns red at time %d\n", side, direction, get_time_passed());
    handled_arrivals++;
    pthread_mutex_unlock(&lock);  
    if (handled_arrivals == total_cars)
    {
      exit(0);
    }
  }
  return NULL;
}

int main(int argc, char * argv[])
{  
  pthread_mutex_init(&lock, NULL);
  int total_cars = sizeof(input_arrivals) / sizeof(Arrival);

  // Initialize semaphores
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_init(&semaphores[i][j], 0, 0);
    }
  }

  pthread_t light_threads[total_cars];
  for (int i = 0; i < total_cars; i++)
  {
    pthread_create(&light_threads[i], NULL, manage_light, (void*)&input_arrivals[i]);
  }

  start_time();

  pthread_t supply_thread;
  pthread_create(&supply_thread, NULL, supply_arrivals, NULL);

  // Wait for all light_threads to finish
  for (int i = 0; i < total_cars; i++)
  {
    pthread_join(light_threads[i], NULL);
  }

  // Wait for supply_thread to finish
  pthread_join(supply_thread, NULL);

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }


  pthread_mutex_destroy(&lock);

  return 0;
}