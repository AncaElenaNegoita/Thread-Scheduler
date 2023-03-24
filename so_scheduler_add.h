#include "so_scheduler.h"

#include <pthread.h>
#include <semaphore.h>

#define ERROR -1
#define SUCCESS 0

/* Each state that a thread can be found is marked by an integer */
#define NEW_STATE 1
#define READY_STATE 2
#define RUNNING_STATE 3
#define WAITING_STATE 4
#define TERMINATED_STATE 5

#define MAX_THREADS 1000
#define INITIAL_THREAD -1

typedef struct thread {
	unsigned int priority;
	int state;
	so_handler *handl;
	int id;
	unsigned int actual_quantum;
} thread;

typedef struct scheduler {
	thread *threads;
	pthread_t *real_threads;
	thread *pq;
	int pq_size;
	thread curr_thread;
	int id_curr_thread;
	unsigned int time_quantum;
	unsigned int io;
	int total_threads;
} scheduler;

static scheduler *so_scheduler;
// a kind of array of unsigned char
static sem_t *sems;
static int init_scheduler;
static pthread_mutex_t mutex;
static thread **waiting_threads;
static int *waiting_size;

/* Add an element in the priority queue */
void add_pq(thread new_thread);

/* Delete first thread from the priority queue */
void delete_pq(void);

/* Set as the current thread the first element from the priority queue*/
int first_thread_pq(void);

/* Function in which it is verified the context of the thread in order to
* execute it */
void start_thread(void *args);

/* Stop the current thread from running and take the first one from the queue */
int switch_threads(void);

/* Plan the thread that should run */
int schedule(void);
