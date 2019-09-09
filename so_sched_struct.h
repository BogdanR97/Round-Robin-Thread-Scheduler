#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "so_scheduler.h"

#define _XOPEN_SOURCE 600

#define NEW_SYSTEM_THREAD 0
#define TIME_EXPIRED 2
#define TERMINATED 3

/* Structures */

typedef struct {
	int waiting_no;
	int signal_recv;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	pthread_barrier_t reset_barr;
} IO_Barrier;

struct thread_handle {
	pthread_t *tid;
	so_handler *func;
	unsigned int priority;
	sem_t thread_setup_lock;
};

typedef struct {
	pthread_t tid;
	sem_t cpu_lock;
	unsigned int prio;
	unsigned int time_left;
} Thread_Utils;

typedef struct node {
	Thread_Utils *thr;
	struct node *next;
} PQElem;

/* Scheduler Utilities */

static PQElem **prio_table;
static PQElem *running_thread;
static unsigned int time_quant;
static int first_system_thread = 1;
static int scheduler_in_use;

static unsigned int io_nr;
static IO_Barrier *io_bars;
static PQElem **waiting_threads;

static PQElem *terminated_threads;

/* Helper Functions */

static void wait_on_finish(void);
static void replace_and_lock(PQElem *prio_thr);
static void check_scheduler(int action, int running_thr_prio);
static Thread_Utils *thread_utils_setup(struct thread_handle *th_h);
static void *thread_handler(void *args);
static void *first_thread_routine(void *args);

/* Priority Queue Utilities */

static PQElem *new_PQnode(Thread_Utils *thu);
static int insert(PQElem **head, PQElem *new_node);
static int top(PQElem **head);
static PQElem *pop(PQElem **head);

/* Error Signaling */
#define DIE(assertion, call_description, ret_val)\
	do {\
		if (assertion) {\
			fprintf(stderr, "(%s, %d): ",\
					__FILE__, __LINE__);\
			perror(call_description);\
			return ret_val;\
		} \
	} while (0)
