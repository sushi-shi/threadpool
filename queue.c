#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#include "queue.h"

typedef struct __node {
	void   *data;
	struct __node *next;
} t_node, *p_node;

typedef struct __queue {
    CRITICAL_SECTION rw_mutex;         
    p_node first;
    p_node last;
    int length;
} t_queue, *p_queue;


    /*  Prototypes */

p_queue q_create(void);
void q_destroy(p_queue queue);
int q_enque(p_queue queue, void* data);
void* q_deque(p_queue queue);
int q_length(p_queue queue);

static p_node node_create(void* data);
static void node_delete(p_node node);


    /*  Queue functions */

p_queue q_create(void)
{
    p_queue queue = malloc(sizeof(t_queue));
    if (queue == NULL)
        return NULL;

    InitializeCriticalSection(&queue->rw_mutex);
    queue->first = NULL;
    queue->last = NULL;
    queue->length = 0;
    return queue;
}

void q_destroy(p_queue queue)
{
    if (queue == NULL)
        return; 
	while (queue->length)
		node_delete(q_deque(queue));

	DeleteCriticalSection(&queue->rw_mutex);
	free(queue);
}

int q_length(p_queue queue)
{
	if (queue == NULL)
		return -1;
	return queue->length;
}


int q_enque(p_queue queue, void* data)
{
    if (queue == NULL) {
        return -1;
	}

    EnterCriticalSection(&queue->rw_mutex);
	p_node node = node_create(data);

    switch (queue->length) {
        case 0:
        {         
            queue->first = node;
            queue->last = node;
            break;
        }
        default:
        {
            queue->last->next = node;
            queue->last = node;
            break;
        }
    }
	queue->length++;

    LeaveCriticalSection(&queue->rw_mutex);
    return 0;
}

void* q_deque(p_queue queue)
{
    if (queue == NULL) {
		fprintf(stderr, "q_deque: NULL\n");
        return NULL;
	}

	EnterCriticalSection(&queue->rw_mutex);
    void *data = NULL;
    p_node node = NULL;
    switch (queue->length) {
        case 0: 
            break;
        case 1: 
        {
            node = queue->first;
            data = node->data;

            queue->first = NULL;
            queue->last  = NULL;
            queue->length = 0;

            break;
        }
        default:
        {
            node = queue->first;
            data = node->data;

            queue->first = node->next;
            queue->length--;

            break;
        }
    }
	node_delete(node);
    LeaveCriticalSection(&queue->rw_mutex);

    return data;
}

        /*  Node functions  */

static p_node node_create(void* data)
{
    p_node node = malloc(sizeof(t_node));
    if (node == NULL) {
		fprintf(stderr, "node_create: cannot create");
        exit(-1);
	}

    node->next = NULL;
    node->data = data;

    return node;
}

static void node_delete(p_node node)
{

   free(node);
}
