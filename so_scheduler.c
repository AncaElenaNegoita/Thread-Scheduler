#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_scheduler_add.h"

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{
	if (io > SO_MAX_NUM_EVENTS || !time_quantum || init_scheduler)
		return ERROR;

	if (pthread_mutex_init(&mutex, NULL))
		return pthread_mutex_init(&mutex, NULL);

	/* Allocate memory for the scheduler and its fields.*/
	so_scheduler = malloc(sizeof(scheduler));
	so_scheduler->io = io;
	so_scheduler->time_quantum = time_quantum;
	so_scheduler->pq_size = 0;
	so_scheduler->total_threads = 0;
	so_scheduler->id_curr_thread = INITIAL_THREAD;
	so_scheduler->threads = malloc(sizeof(thread) * MAX_THREADS);
	so_scheduler->real_threads = (pthread_t *)malloc(MAX_THREADS * sizeof(pthread_t));
	so_scheduler->pq = malloc(sizeof(thread) * MAX_THREADS);
	sems = malloc(sizeof(sem_t) * MAX_THREADS);

	if (!sems || !so_scheduler || !so_scheduler->threads
		|| !so_scheduler->real_threads || !so_scheduler->pq) {
		free(sems);
		free(so_scheduler->threads);
		free(so_scheduler->real_threads);
		free(so_scheduler->pq);
		free(so_scheduler);
		return ERROR;
	}
	sem_t initialise;
	/* This function indicated if all threads have finished. */
	sem_init(&initialise, 0, 1);

	/* Allocate space for the array that will have the waiting threads. */
	waiting_size = calloc (SO_MAX_NUM_EVENTS, sizeof(int));
	waiting_threads = calloc(sizeof(thread *), SO_MAX_NUM_EVENTS);

	/* Allocate space for each waiting thread */
	for (int i = 0; i < SO_MAX_NUM_EVENTS; i++)
		waiting_threads[i] = calloc(sizeof(thread), MAX_THREADS);

	init_scheduler = 1;
	return SUCCESS;
}

void add_pq(thread new_thread)
{
	int pos;

	/* The for loop searches for the position where the thread should be inserted.
	* If the priorities are equal, it continues. If the priority is bigger than the
	* current element from the queue, then it should be inserted on the current
	* position (pos) */
	for (pos = 0; pos < so_scheduler->pq_size; pos++)
		if (so_scheduler->pq[pos].priority < new_thread.priority) {
			/* Starting from the end, each element from the queue is moved one position
			* to the end in order to add the given element on the current position (pos)
			* and the pq size increases by one (because we add an element)*/
			for (int j = so_scheduler->pq_size; j >= pos + 1; j--)
				so_scheduler->pq[j] = so_scheduler->pq[j - 1];
			break;
		}

	so_scheduler->pq_size++;
	so_scheduler->pq[pos] = new_thread;
}

void delete_pq(void)
{
	int i;

	for (i = 0; i < so_scheduler->pq_size - 1; i++)
		so_scheduler->pq[i] = so_scheduler->pq[i + 1];

	so_scheduler->pq_size--;
}

int first_thread_pq(void)
{
	/* Put in so_scheduler the atributes of the first element in queue */
	so_scheduler->curr_thread = so_scheduler->pq[0];
	so_scheduler->id_curr_thread = so_scheduler->pq[0].id;
	so_scheduler->curr_thread.actual_quantum = so_scheduler->time_quantum;
	so_scheduler->curr_thread.state = RUNNING_STATE;

	/* Delete first element */
	delete_pq();

	/* Execute the thread */
	int verifyError = sem_post(&sems[so_scheduler->id_curr_thread]);
	if (verifyError)
		return verifyError;

	return SUCCESS;
}

int switch_threads(void)
{
	int verifyError;

	thread th_current = so_scheduler->pq[0], th_last = so_scheduler->curr_thread;

	/* We put in so_scheduler the first thread from the priority queue,
	* becoming the current thread */
	so_scheduler->curr_thread = so_scheduler->pq[0];
	so_scheduler->id_curr_thread = so_scheduler->pq[0].id;
	so_scheduler->curr_thread.actual_quantum = so_scheduler->time_quantum;
	so_scheduler->curr_thread.state = RUNNING_STATE;
	th_last.actual_quantum = so_scheduler->time_quantum;

	/* Delete the first thread and add the previous current one */
	delete_pq();
	add_pq(th_last);

	/* Execute the current thread */
	verifyError = sem_post(&sems[th_current.id]);
	if (verifyError)
		return verifyError;

	return SUCCESS;
}

int schedule(void)
{
	int verifyError, ok = 0;

	/* Verify if there isn't any running thread */
	if (so_scheduler->id_curr_thread == INITIAL_THREAD) {
		/* Choose the first element from the priority queue if it isn't empty */
		if (so_scheduler->pq_size != 0) {
			verifyError = first_thread_pq();
			if (verifyError)
				return verifyError;
		}
	} else {
		if (so_scheduler->curr_thread.state == TERMINATED_STATE &&
		    so_scheduler->pq_size == 0)
			return SUCCESS;

		if (so_scheduler->pq_size != 0) {
			/* If the queue isn't empty and the current thread is terminated or
			* in the waiting state, then it chooses the first thread from the
			* priority queue */
			if (so_scheduler->curr_thread.state == TERMINATED_STATE
				|| so_scheduler->curr_thread.state == WAITING_STATE) {
				verifyError = first_thread_pq();
				if (verifyError)
					return verifyError;
			}

			/* If the first element from the priority queue has a bigger priority than
			* the current thread, they switch. Also, if the thread has run out of time
			* (quantum time is 0 or below), then it chooses the first thread from the
			* priority queue if it has the same priority */
			else if (so_scheduler->pq[0].priority > so_scheduler->curr_thread.priority
					 || (so_scheduler->curr_thread.priority == so_scheduler->pq[0].priority
					&& so_scheduler->curr_thread.actual_quantum <= 0)) {
					verifyError = switch_threads();
					if (verifyError)
							return verifyError;
			}

			/* If it hasn't found a best candidate for the switch action, its
			* quantum time is reseted*/
			else if (so_scheduler->curr_thread.actual_quantum <= 0) {
					so_scheduler->curr_thread.actual_quantum =so_scheduler->time_quantum;
					so_scheduler->curr_thread.state = RUNNING_STATE;

					verifyError = sem_post(&sems[so_scheduler->id_curr_thread]);
					if (verifyError)
						return verifyError;
			} else {
				ok = 1;
			}
		} else {
			/* If the quantum time is below 0, it needs to be reseted because the
			* queue is empty */
			if (so_scheduler->curr_thread.actual_quantum <= 0) {
				so_scheduler->curr_thread.actual_quantum = so_scheduler->time_quantum;
				so_scheduler->curr_thread.state = RUNNING_STATE;

				verifyError = sem_post(&sems[so_scheduler->id_curr_thread]);
				if (verifyError)
					return verifyError;
			} else {
				ok = 1;
			}
		}
	}

	/* If ok is 1, it means the same thread continues */
	if (ok) {
		so_scheduler->curr_thread.state = RUNNING_STATE;
		verifyError = sem_post(&sems[so_scheduler->id_curr_thread]);
		if (verifyError)
			return verifyError;
	}

	return SUCCESS;
}

void start_thread(void *start_thread)
{
	thread *th = (thread *)start_thread;

	/* Plan the thread */
	sem_wait(&sems[th->id]);

	th->handl(th->priority);
	th->state = TERMINATED_STATE;
	so_scheduler->curr_thread.state = TERMINATED_STATE;

	schedule();
}

DECL_PREFIX tid_t so_fork(so_handler *func, unsigned int priority)
{
	int verifyError, total_threads = so_scheduler->total_threads;

	if (!init_scheduler || priority > SO_MAX_PRIO || !func)
		return INVALID_TID;

	/* Initialise the thread */
	so_scheduler->threads[total_threads].priority = priority;
	so_scheduler->threads[total_threads].id = total_threads;
	so_scheduler->threads[total_threads].handl = func;
	so_scheduler->threads[total_threads].actual_quantum =
		so_scheduler->time_quantum;
	so_scheduler->threads[total_threads].state = READY_STATE;

	verifyError = sem_init(&sems[total_threads], 0, 0);
	if (verifyError != SUCCESS)
		return INVALID_TID;

	/* Create the thread */
	pthread_create(&so_scheduler->real_threads[total_threads],
				   NULL, (void *)start_thread, &so_scheduler-> threads[total_threads]);

	/* Verify if there is another thread that locked the mutex */
	verifyError = pthread_mutex_lock(&mutex);

	if (verifyError)
		return ERROR;

	/* Add the thread in the priority queue */
	add_pq(so_scheduler->threads[so_scheduler->total_threads]);
	so_scheduler->total_threads++;
	verifyError = pthread_mutex_unlock(&mutex);

	if (verifyError)
		return ERROR;

	/* Execute the thread */
	so_exec();

	return so_scheduler->real_threads[total_threads];
}


DECL_PREFIX int so_wait(unsigned int io)
{
	if (io >= so_scheduler->io || !init_scheduler)
		return ERROR;

	/* Add the thread in the waiting list and increment the size of it*/
	waiting_threads[io][waiting_size[io]] = so_scheduler->curr_thread;
	waiting_size[io]++;
	so_scheduler->curr_thread.state = WAITING_STATE;

	so_exec();

	return SUCCESS;
}


DECL_PREFIX int so_signal(unsigned int io)
{
	if (io >= so_scheduler->io)
		return ERROR;

	/* Put in the priority queue the threads that are waiting at the io size*/
	for (int i = 0; i < waiting_size[io]; i++) {
		waiting_threads[io][i].state = READY_STATE;
		add_pq(waiting_threads[io][i]);
		memset(&waiting_threads[io][i], 0, sizeof(thread));
	}
	int size = waiting_size[io];
	waiting_size[io] = 0;

	so_exec();

	return size;
}


DECL_PREFIX void so_exec(void)
{
	int verifyError, id_current_thread = so_scheduler->id_curr_thread;
	thread current_thread = so_scheduler->curr_thread;

	/* Decrease the quantum time and reschedule the thread if necessary */
	so_scheduler->curr_thread.actual_quantum--;
	verifyError = schedule();

	if (verifyError)
		return;

	/* The current thread waits to be executed if it still exists */
	if (id_current_thread != INITIAL_THREAD) {
		verifyError = sem_wait(&sems[current_thread.id]);
		if (verifyError)
			return;
	}
}

DECL_PREFIX void so_end(void)
{
	/* Verify if the scheduler is empty and also wait for the threads to end */
	if (!so_scheduler)
		return;

	/* Make calling thread wait for termination of the thread and also free
	* resources associated with semaphore object sem*/
	for (int i = 0; i < so_scheduler->total_threads; i++) {
		if (pthread_join(so_scheduler->real_threads[i], NULL) || sem_destroy(&sems[i]))
			return;
	}

	/* Free the scheduler */
	free(waiting_size);
	free(so_scheduler->threads);
	free(so_scheduler->real_threads);
	free(so_scheduler->pq);
	free(so_scheduler);
	free(sems);

	/* Free the waiting list */
	for (int i = 0; i < SO_MAX_NUM_EVENTS; i++)
		free(waiting_threads[i]);
	free(waiting_threads);

	if (pthread_mutex_destroy(&mutex))
		return;

	init_scheduler = 0;
}
