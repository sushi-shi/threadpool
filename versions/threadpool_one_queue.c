#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#include "queue.h"

#include "threadpool.h"

typedef struct __pool *p_pool;

typedef struct __thread {
	HANDLE id;
	
} t_thread, *p_thread;

typedef struct __pool {
	p_thread* threads;
	queue tasks; 
	CRITICAL_SECTION rw_mutex;  

	HANDLE event_on_data;
	HANDLE event_on_state;
	int threads_num;

	int threads_alive;
	int threads_working;
	volatile int keep_alive;
} t_pool, *p_pool;

typedef struct __task {
	void (*on_task)(void *);
    void *args; 
} t_task, *p_task;


    /*  Prototypes  */

p_pool tp_create(int n);
void tp_destroy(p_pool);
int tp_add_task(p_pool, void (*on_task)(void *), void*args);
void tp_wait(p_pool);

static p_thread thread_create(p_pool);
static unsigned long int WINAPI thread_loop(void *);

static p_task task_create(void (*task)(void *), void *args);
static void task_destroy(p_task task);

    /*  Pool functions  */

p_pool tp_create(int n)
{
    p_pool pool = malloc(sizeof(t_pool));
    if (pool == NULL)
        return NULL;

    pool->threads = malloc(n * sizeof(void *));
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }

    pool->tasks = q_create();
    if (pool->tasks == NULL) {
        free(pool);
        return NULL;
    }

	InitializeCriticalSection(&pool->rw_mutex);

	// is used for receiving tasks in queue
	pool->event_on_data = CreateEventA(
		NULL, 
		FALSE, // auto reset
		FALSE, // non-signaled
		NULL);

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


int tp_add_task(p_pool pool, void (*on_task)(void *), void *args)
{
	p_task task = task_create(on_task, args);
	if (task == NULL)
		return -1;
	
	q_enque(pool->tasks, (void *)task);

	// raise an event each time there is a task to run
	// maybe not the best approach, but give it a go
	int isNotFailed = SetEvent(pool->event_on_data);
	if (!isNotFailed) {
		fprintf(stderr, "tp_add_task: %d\n", GetLastError());
		exit(-1);
	}
	return 0;
}

void tp_wait(p_pool pool)
{
	WaitForSingleObject(pool->event_on_state, INFINITE);
}

void tp_destroy(p_pool pool)
{
	if (pool == NULL)
		return;

	tp_wait(pool);

	// closing infinite cycle 
	pool->keep_alive = 0;

	while (pool->threads_alive) {
		printf("Tried to lie!\n");
		WaitForSingleObject(pool->event_on_state, INFINITE);
	}

	free(pool->threads);
	DeleteCriticalSection(&pool->rw_mutex);
	CloseHandle(pool->event_on_data);
	CloseHandle(pool->event_on_state);
	q_destroy(pool->tasks);
	free(pool);
}


		/*	Task functions	*/

/*
 * Returns:
 * 	Null on error
 */ 

static p_task task_create(void (*on_task)(void *), void *args)
{
	p_task task = malloc(sizeof(t_task));
	if (task == NULL) {
		fprintf(stderr, "task_create: malloc\n");
		return NULL;
	}

	task->args = args;
	task->on_task = on_task;
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
	thread->id = CreateThread(NULL, 0, thread_loop, (void *)pool, 0, NULL);
	if (thread->id == NULL) {
		fprintf(stderr, "CreateThread: %d\n", GetLastError());
		exit(-1);
	}

	return thread;
}

static unsigned long int WINAPI thread_loop(void *p)
{
	p_pool pool = (p_pool)p;

	// tell everyone you are alive and try grabbing the job right away
	EnterCriticalSection(&pool->rw_mutex);

	pool->threads_alive++;	
	if (pool->threads_alive == pool->threads_num)
		SetEvent(pool->event_on_state);
	pool->threads_working++;

	LeaveCriticalSection(&pool->rw_mutex);

	while (pool->keep_alive) {
		// this queue should be atomic
		p_task task = q_deque(pool->tasks);

		if (task == NULL) {

			EnterCriticalSection(&pool->rw_mutex);
			pool->threads_working--;
			// nobody is working, no good
			if (pool->threads_working == 0)
				SetEvent(pool->event_on_state);
			LeaveCriticalSection(&pool->rw_mutex);

			// that's why it was not a good idea with events
			// every 100 msek or so we need to check whether we missed a job
			WaitForSingleObject(pool->event_on_data, 1000);
		}
		else {
			EnterCriticalSection(&pool->rw_mutex);
			pool->threads_working++;
			LeaveCriticalSection(&pool->rw_mutex);
			//printf("%d\n", (int)task);
			//task->on_task(task->args);
			//task_destroy(task);
		}

	}

	EnterCriticalSection(&pool->rw_mutex);
	pool->threads_working--;
	pool->threads_alive--;

	// last survivor has to tell that all is gone, bye
	if (pool->threads_alive == 0)
		SetEvent(pool->event_on_state);
	LeaveCriticalSection(&pool->rw_mutex);

	ExitThread(0);
}
