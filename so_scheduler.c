#include "so_sched_struct.h"

/* ====== SCHEDULER UTILITIES ====== */

static void check_scheduler(int action, int running_thr_prio)
{
	PQElem *prio_thr;
	int prio;

	if (action == NEW_SYSTEM_THREAD) {
		for (prio = SO_MAX_PRIO; prio > running_thr_prio; prio--) {
			if (top(&prio_table[prio])) {
				replace_and_lock(pop(&prio_table[prio]));
				break;
			}
		}

	} else if (action == TIME_EXPIRED) {
		for (prio = SO_MAX_PRIO; prio >= running_thr_prio; prio--) {
			if (top(&prio_table[prio])) {
				replace_and_lock(pop(&prio_table[prio]));
				break;
			}
		}

	/* Since the tread has finished executing all of its instructions
	 * it will unlock the found thread and simply exit the function
	 */
	} else if (action == TERMINATED) {
		for (prio = SO_MAX_PRIO; prio >= 0; prio--) {
			if (top(&prio_table[prio])) {
				prio_thr = pop(&prio_table[prio]);
				running_thread = prio_thr;
				sem_post(&prio_thr->thr->cpu_lock);
				break;
			}
		}
	}
}

/* The current running thread either has spent all its time on the CPU
 * or a new thread with greater priority has been introduced into the
 * system and thus it must be replaced and gets locked until
 * it will be re-scheduled
 */
static void replace_and_lock(PQElem *prio_thr)
{
	PQElem *preempted_thr;
	Thread_Utils *thu;

	preempted_thr = running_thread;
	running_thread = prio_thr;

	preempted_thr->thr->time_left = time_quant;
	insert(&prio_table[preempted_thr->thr->prio], preempted_thr);

	thu = prio_thr->thr;
	sem_post(&thu->cpu_lock);

	thu = preempted_thr->thr;
	/* Thread will wait untill it will be re-scheduled */
	sem_wait(&thu->cpu_lock);
	running_thread = preempted_thr;
}

/* ====== SO_INIT ====== */

int so_init(unsigned int time_quantum, unsigned int io)
{
	if (scheduler_in_use)
		/* Scheduler cannot be re-initialized */
		return -1;

	if (time_quantum <= 0)
		/* Invalid time quantum */
		return -1;

	if (io > SO_MAX_NUM_EVENTS)
		/* Invalid io */
		return -1;

	int i, rc;

	time_quant = time_quantum;

	io_nr = io;
	io_bars = malloc(io * sizeof(IO_Barrier));
	DIE(io_bars == NULL, "Malloc error", -1);

	for (i = 0; i < io; i++) {
		io_bars[i].signal_recv = 0;
		io_bars[i].waiting_no = 0;

		rc = pthread_mutex_init(&io_bars[i].lock, NULL);
		DIE(rc != 0, "Mutex init error", -1);

		rc = pthread_cond_init(&io_bars[i].cond, NULL);
		DIE(rc != 0, "Cond init error", -1);
	}

	waiting_threads = malloc(io * sizeof(PQElem *));
	DIE(waiting_threads == NULL, "Malloc error", -1);

	for (int i = 0; i < io; i++)
		waiting_threads[i] = NULL;

	/* Priorities are indexed from 1 to SO_MAX_PRIO inclusively */
	prio_table = malloc((SO_MAX_PRIO + 1) * sizeof(PQElem *));
	DIE(prio_table == NULL, "Malloc error", -1);

	for (int i = 0; i <= SO_MAX_PRIO; i++)
		prio_table[i] = NULL;
	scheduler_in_use = 1;

	terminated_threads = NULL;

	return 0;
}

/* ====== SO_FORK ====== */

tid_t so_fork(so_handler *func, unsigned int priority)
{
	if (func == NULL || priority > SO_MAX_PRIO)
		return INVALID_TID;

	tid_t new_tid, rc;
	struct thread_handle *th_param;

	th_param = malloc(sizeof(struct thread_handle));
	DIE(th_param == NULL, "Malloc error", INVALID_TID);
	th_param->func = func;
	th_param->priority = priority;
	th_param->tid = &new_tid;
	rc = sem_init(&th_param->thread_setup_lock, 0, 0);
	DIE(rc != 0, "Semaphore init error", INVALID_TID);

	if (first_system_thread) {
		first_system_thread = !first_system_thread;

		rc = pthread_create(&new_tid, NULL, &first_thread_routine, th_param);
		DIE(rc != 0, "Error upon creating a new thread", INVALID_TID);

		/* Waiting for the created thread to do its set-up and
		 *  get introduced into the priority queue;
		 */
		sem_wait(&th_param->thread_setup_lock);

		return new_tid;
	}

	rc = pthread_create(&new_tid, NULL, &thread_handler, th_param);
	DIE(rc != 0, "Error upon creating a new thread", INVALID_TID);

	/* Waiting for the created thread to do its set-up and
	 *  get introduced into the priority queue;
	 */
	sem_wait(&th_param->thread_setup_lock);
	rc = sem_destroy(&th_param->thread_setup_lock);
	DIE(rc != 0, "Sempahore destroy error", INVALID_TID);

	if (--running_thread->thr->time_left == 0)
		check_scheduler(TIME_EXPIRED, running_thread->thr->prio);
	else
		check_scheduler(NEW_SYSTEM_THREAD, running_thread->thr->prio);

	return new_tid;
}

static void *first_thread_routine(void *args)
{
	unsigned int running_thr_prio;
	PQElem *new_node, *finish_node;
	struct thread_handle *th_h = (struct thread_handle *) args;
	Thread_Utils *new_th = thread_utils_setup(th_h);

	new_node = new_PQnode(new_th);
	finish_node = new_PQnode(new_th);
	running_thread = new_node;
	insert(&terminated_threads, finish_node);

	/* Signaling the parent thread thta the setup is over */
	sem_post(&th_h->thread_setup_lock);

	/* The thread will execute its instructions now */
	th_h->func(new_th->prio);

	running_thr_prio = th_h->priority;
	free(th_h);
	th_h = NULL;

	free(new_node);
	new_node = NULL;

	check_scheduler(TERMINATED, running_thr_prio);

	return NULL;
}

static void *thread_handler(void *args)
{
	unsigned int running_thr_prio;
	PQElem *new_node, *finish_node;
	struct thread_handle *th_h = (struct thread_handle *) args;
	Thread_Utils *new_th = thread_utils_setup(th_h);

	new_node = new_PQnode(new_th);
	finish_node = new_PQnode(new_th);
	insert(&prio_table[th_h->priority], new_node);
	insert(&terminated_threads, finish_node);

	/* Signaling the parent thread thta the setup is over */
	sem_post(&th_h->thread_setup_lock);

	/* Waiting to be scheduled on the CPU */
	sem_wait(&new_th->cpu_lock);

	/* The thread will execute its instructions now */
	th_h->func(new_th->prio);

	running_thr_prio = th_h->priority;
	free(th_h);
	th_h = NULL;

	free(new_node);
	new_node = NULL;

	check_scheduler(TERMINATED, running_thr_prio);

	return NULL;
}

static Thread_Utils *thread_utils_setup(struct thread_handle *th_h)
{
	int rc;
	Thread_Utils *new_th = malloc(sizeof(Thread_Utils));
	DIE(new_th == NULL, "Malloc error", NULL);

	new_th->tid = *(th_h->tid);
	rc = sem_init(&new_th->cpu_lock, 0, 0);
	DIE(rc != 0, "Semaphore init error", NULL);
	new_th->prio = th_h->priority;
	new_th->time_left = time_quant;

	return new_th;
}

/* ====== SO_EXEC ====== */

void so_exec(void)
{
	if (--running_thread->thr->time_left == 0)
		check_scheduler(TIME_EXPIRED, running_thread->thr->prio);
}

/* ====== SO_WAIT ====== */

int so_wait(unsigned int io)
{
	if (io >= io_nr)
		return -1;

	int rc;
	IO_Barrier *this_barr;
	Thread_Utils *thu = running_thread->thr;
	thu->time_left = time_quant;

	/* This thread will be inserted inot the waiting threads queue */
	rc = insert(&waiting_threads[io], running_thread);
	DIE(rc != 0, "insert error", -1);

	this_barr = &io_bars[io];
	(*this_barr).waiting_no += 1;

	pthread_mutex_lock(&(*this_barr).lock);
	/* Another thread must run on the CPU as this one will get into
	 * the waiting state. However, we do not wish to re-introduce it
	 * into the priority table.
	 */
	check_scheduler(TERMINATED, running_thread->thr->prio);
	while (!(*this_barr).signal_recv)
		/* Will wait until a signal is sent from so_signal */
		pthread_cond_wait(&(*this_barr).cond, &(*this_barr).lock);

	pthread_mutex_unlock(&(*this_barr).lock);

	pthread_barrier_wait(&(*this_barr).reset_barr);

	sem_wait(&thu->cpu_lock);

	return 0;
}

/* ====== SO_SIGNAL ====== */

int so_signal(unsigned int io)
{
	if (io >= io_nr)
		return -1;

	int rc, signaled_thr_no;
	PQElem *wait_thr;

	IO_Barrier *this_barr = &io_bars[io];
	pthread_barrier_init(&this_barr->reset_barr, NULL,
		this_barr->waiting_no + 1);

	/* Introducing the waiting threads back in the priority table */
	wait_thr = pop(&waiting_threads[io]);
	while (wait_thr != NULL) {
		rc = insert(&prio_table[wait_thr->thr->prio], wait_thr);
		DIE(rc != 0, "insert error", -1);
		wait_thr = pop(&waiting_threads[io]);
	}

	pthread_mutex_lock(&(*this_barr).lock);

	(*this_barr).signal_recv = 1;
	pthread_cond_broadcast(&(*this_barr).cond);

	pthread_mutex_unlock(&(*this_barr).lock);

	/* Waiting for the signaled threads to arrive at the barrier so that
	 * the conditional variable and the waiting_no variable can be reset
	 */
	pthread_barrier_wait(&(*this_barr).reset_barr);
	pthread_barrier_destroy(&(*this_barr).reset_barr);

	(*this_barr).signal_recv = 0;
	signaled_thr_no = (*this_barr).waiting_no;
	(*this_barr).waiting_no = 0;

	if (--running_thread->thr->time_left == 0)
		check_scheduler(TIME_EXPIRED, running_thread->thr->prio);
	else
		check_scheduler(NEW_SYSTEM_THREAD, running_thread->thr->prio);

	return signaled_thr_no;
}

/* ====== SO_END ====== */

void so_end(void)
{
	if (!scheduler_in_use)
		return;

	wait_on_finish();

	for (int i = 0; i < io_nr; i++) {
		pthread_mutex_destroy(&io_bars[i].lock);
		pthread_cond_destroy(&io_bars[i].cond);
	}
	free(io_bars);
	io_bars = NULL;

	free(waiting_threads);
	waiting_threads = NULL;

	free(prio_table);
	prio_table = NULL;

	scheduler_in_use = 0;
}

/* Will wait for all the created threads to finish executing their
 * instructions via pthread_join()
 */
void wait_on_finish(void)
{
	PQElem *next;
	while (terminated_threads != NULL) {
		pthread_join(terminated_threads->thr->tid, NULL);

		next = terminated_threads->next;
		sem_destroy(&terminated_threads->thr->cpu_lock);
		free(terminated_threads->thr);
		free(terminated_threads);
		terminated_threads = next;
	}
}

/* ====== PRIORITY QUEUE UTILITIES ====== */
static PQElem *new_PQnode(Thread_Utils *thu)
{
	PQElem *new_node;

	new_node = malloc(sizeof(PQElem));
	if (new_node == NULL)
		return NULL;

	new_node->thr = thu;
	new_node->next = NULL;

	return new_node;
}

static int insert(PQElem **head, PQElem *new_node)
{
	if (new_node == NULL)
		return -1;

	if ((*head) == NULL)
		(*head) = new_node;

	else {
		PQElem *it = (*head);
		while (it->next != NULL)
			it = it->next;

		it->next = new_node;
		new_node->next = NULL;
	}

	return 0;
}

static int top(PQElem **head)
{
	if ((*head) != NULL)
		return 1;
	return 0;
}

static PQElem *pop(PQElem **head)
{
	if ((*head) == NULL)
		return NULL;

	PQElem *temp = (*head);
	(*head) = (*head)->next;
	temp->next = NULL;
	return temp;
}
