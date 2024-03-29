#ifndef H_POOL
#define H_POOL

typedef struct __pool *threadpool;

/*
 * Returns:
 *	NULL on error
 */
threadpool pool_create(int n);

/*
 * Don't even try to call it twice
 */
void pool_destroy(threadpool);

/*
 * Adding task after calling tp_destroy() is undefined
 * Returns:
 *	-1 on error
 *  0 otherwise
 */
int pool_add_task(threadpool, void (*task)(void *), void *args);

/*
 */
void pool_wait(threadpool);

#endif
