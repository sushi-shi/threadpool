#ifndef H_QUEUE
#define H_QUEUE
/*
 * Lock-full queue. 
 */
typedef struct __queue* queue;

/*
 * Returns:
 *	NULL on error
 */
queue q_create(void);

void q_destroy(queue);

/* 
 * A mutex is used for synchronization
 * Returns:
 *	NULL on error
 */
void* q_deque(queue);

/*
 * A mutex is used for synchronization
 * Returns:
 *	-1 on error
 * 	0 otherwise
 */
int q_enque(queue, void*);



/*
 * Returns:
 *	-1 on error
 */
int q_length(queue);

#endif
