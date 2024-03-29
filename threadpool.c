#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#include "queue.h"

#include "threadpool.h"

typedef struct __pool *p_pool;

typedef struct __thread {
	p_pool pool;
	queue task_queue;
	HANDLE event_on_data;
} t_thread, *p_thread;

typedef struct __pool {
	p_thread* threads;
	CRITICAL_SECTION rw_mutex;  

	HANDLE event_on_state;
	int threads_num;

	volatile int threads_alive;
	volatile int threads_working;
	volatile int keep_alive;
} t_pool;

typedef struct __task {
	void (*fun)(void *);
    void *args; 
} t_task, *p_task;


    /*  Prototypes  */

p_pool pool_create(int n);
void pool_destroy(p_pool);
int pool_add_task(p_pool, void (*fun)(void *), void *args);
void pool_wait(p_pool);

static p_thread thread_create(p_pool);
static unsigned long int WINAPI thread_loop(void *);
static void thread_destroy(p_thread thread);

static p_task task_create(void (*fun)(void *), void *args);
static void task_destroy(p_task task);

    /*  Pool functions  */

p_pool pool_create(int n)
{
    p_pool pool = malloc(sizeof(t_pool));
    if (pool == NULL)
        return NULL;

    pool->threads = malloc(n * sizeof(void *));
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }

	InitializeCriticalSection(&pool->rw_mutex);

	// is used for signalling about the internal state of a pool
	pool->event_on_state = CreateEventA(
		NULL, 
		FALSE, // auto reset
		FALSE, // non-signaled
		NULL);
	
	pool->threads_alive = 0;
	pool->threads_working = 0;
	pool->threads_num = n;

	pool->keep_alive = 1;

    for (int i = 0; i < n; i++)
        pool->threads[i] = thread_create(pool);

	// wait until all threads are running

	WaitForSingleObject(pool->event_on_state, INFINITE);


    return pool;
}


int pool_add_task(p_pool pool, void (*fun)(void *), void *args)
{
	EnterCriticalSection(&pool->rw_mutex);
	// No need to add anything on destruction
	if (pool->keep_alive == 0) {
		LeaveCriticalSection(&pool->rw_mutex);
		return -1;
	}

	// round-robin to distribute tasks
	// may change later
	static int i = 0;

	p_task task = task_create(fun, args);
	if (task == NULL) {
		LeaveCriticalSection(&pool->rw_mutex);
		return -1;
	}

	q_enque(pool->threads[i]->task_queue, (void *)task);
	// something to do, thread-kun
	SetEvent(pool->threads[i]->event_on_data);


	i = (i + 1) % pool->threads_num;
	LeaveCriticalSection(&pool->rw_mutex);
	return 0;
}

void pool_wait(p_pool pool)
{
	/*
	 * 				Spooky Story
	 * Imagine a situation, 2 threads where given 2 jobs.
	 * The first thread started executing as soon as it was given a job.
	 * But the second one was lazy (it liked Haskell). 
	 * So Scheduler desided to give it some time to slouch.
	 * But in the mean time our first thread was assiduously working.
	 * And when it finished, it saw that nobody was working anymore.
	 * So it sent a signal to notify us.
	 * And after we received that signal, our lazy frenemy decided to actually start doing something.
	 * But we received our signal so decided that there is no reason to wait for anybody!
	 * Spooky.
	 */
	while (1) {

		// First we wait for a signal
		WaitForSingleObject(pool->event_on_state, INFINITE);
		EnterCriticalSection(&pool->rw_mutex);
		int is_busy = 0;
		// After that we must make sure that nobody has any task in their queues
		for (int i = 0; i < pool->threads_num; i++)
			if (q_length(pool->threads[i]->task_queue) != 0) {
				is_busy = 1;
				break;
			}
		// Oops, somebody is not finished yet
		if (is_busy) {
			LeaveCriticalSection(&pool->rw_mutex);
			continue;
		}
		// Some threads may still be working. So we need to make sure that we are alone
		if (pool->threads_working == 0) {
			LeaveCriticalSection(&pool->rw_mutex);
			// and leave
			break;
		}
		LeaveCriticalSection(&pool->rw_mutex);
	}

}

void pool_destroy(p_pool pool)
{
	if (pool == NULL)
		return;
 
	EnterCriticalSection(&pool->rw_mutex);
	// Break infinite cycle
	pool->keep_alive = 0;
	LeaveCriticalSection(&pool->rw_mutex);

	// Notify every-nyan, some threads may still be running
	for (int i = 0; i < pool->threads_num; i++)
		SetEvent(pool->threads[i]->event_on_data);

	// The last thread will raise an event
	// But this event may also be raised to notify that noone has a job
	// So, cycle is used to ensure that everyone is dead
	while (pool->threads_alive != 0)
		WaitForSingleObject(pool->event_on_state, INFINITE);


	// Destroy all created structures
	EnterCriticalSection(&pool->rw_mutex);
	for (int i = 0; i < pool->threads_num; i++)
		thread_destroy(pool->threads[i]);
	LeaveCriticalSection(&pool->rw_mutex);

	DeleteCriticalSection(&pool->rw_mutex);
	
	CloseHandle(pool->event_on_state);
	free(pool->threads);
	free(pool);
}


		/*	Task functions	*/

/*
 * Returns:
 * 	Null on error
 */ 
static p_task task_create(void (*fun)(void *), void *args)
{

	p_task task = malloc(sizeof(t_task));
	if (task == NULL) {
		fprintf(stderr, "task_create: malloc\n");
		return NULL;
	}
	task->fun = fun;
	task->args = args;
	return task;
}

static void task_destroy(p_task task)
{
	free(task);
}

		/* 	Thread functions	*/

static p_thread thread_create(p_pool pool)
{
	p_thread thread = malloc(sizeof(t_thread));
	thread->task_queue = q_create();
	thread->pool = pool;
	thread->event_on_data = CreateEventA(
		NULL, 
		FALSE, // auto reset
		FALSE, // non-signaled
		NULL);


	if (CreateThread(NULL, 0, thread_loop, (void *)thread, 0, NULL) == NULL) {
		fprintf(stderr, "CreateThread: %d\n", GetLastError());
		exit(-1);
	}

	return thread;
}

static void thread_destroy(p_thread thread)
{
	q_destroy(thread->task_queue);
	CloseHandle(thread->event_on_data);
	free(thread);
}


static unsigned long int WINAPI thread_loop(void *t)
{
	p_thread thread_info = (p_thread)t;
	p_pool pool = thread_info->pool;

	// initialize
	EnterCriticalSection(&pool->rw_mutex);
	pool->threads_alive++;	
	if (pool->threads_alive == pool->threads_num)
		SetEvent(pool->event_on_state);
	LeaveCriticalSection(&pool->rw_mutex);

	while (pool->keep_alive) {

		// raised when there is something in the queue
		// or when it's time for threads to die
		WaitForSingleObject(thread_info->event_on_data, INFINITE);

		if (pool->keep_alive) {

			EnterCriticalSection(&pool->rw_mutex);
			pool->threads_working++;
			LeaveCriticalSection(&pool->rw_mutex);

			p_task task = q_deque(thread_info->task_queue);
			if (task == NULL) {
				fprintf(stderr, "thread_loop: Empty queue\n");
				exit(-1);
			}
			void (*fun)(void *) = task->fun;
			void *args = task->args;


			fun(args);

			// Our (consumer's) job to delete tasks
			task_destroy(task);
			// assuming only we can consume tasks from this queue
			if (q_length(thread_info->task_queue))
				SetEvent(thread_info->event_on_data);

			EnterCriticalSection(&pool->rw_mutex);
			pool->threads_working--;
			// Nobody is working, it's better to tell my employee
			if (pool->threads_working == 0)
				SetEvent(pool->event_on_state);
			LeaveCriticalSection(&pool->rw_mutex);

		}
	}

	EnterCriticalSection(&pool->rw_mutex);
	pool->threads_alive--;

	// last survivor has to raise an event, bye!
	if (pool->threads_alive == 0)
		SetEvent(pool->event_on_state);

	LeaveCriticalSection(&pool->rw_mutex);

	// should we block before return nothing bad will happen
	// all free'd structures are not used anymore anyway
	return 0;
}
