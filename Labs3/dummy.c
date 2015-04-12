/*
 * Dummy scheduling class, mapped to range of 5 levels of SCHED_NORMAL policy
 */

#include "sched.h"

/*
 * Timeslice and age threshold are repsented in jiffies. Default timeslice
 * is 100ms. Both parameters can be tuned from /proc/sys/kernel.
 */

#define DUMMY_TIMESLICE		(100 * HZ / 1000)
#define DUMMY_AGE_THRESHOLD	(3 * DUMMY_TIMESLICE)

// Do not forget to change it in sched.h
#define DAN_JAR_NB_LEVEL_PRIORITY 5

#define DAN_JAR_MIN_DUMMY_PRIO 131
#define DAN_JAR_MAX_DUMMY_PRIO 135

unsigned int sysctl_sched_dummy_timeslice = DUMMY_TIMESLICE;
static inline unsigned int get_timeslice(void)
{
	return sysctl_sched_dummy_timeslice;
}

unsigned int sysctl_sched_dummy_age_threshold = DUMMY_AGE_THRESHOLD;
static inline unsigned int get_age_threshold(void)
{
	return sysctl_sched_dummy_age_threshold;
}

static unsigned int get_rr_interval_dummy(struct rq* rq, struct task_struct *p)
{
	return get_timeslice();
}

/*
 * Init
 */

void init_dummy_rq(struct dummy_rq *dummy_rq, struct rq *rq)
{
	int i = 0;
	for(; i < DAN_JAR_NB_LEVEL_PRIORITY; ++i)
		INIT_LIST_HEAD(&dummy_rq->queue[i]);
}

/*
 * Helper functions
 */

static inline int _keep_prio(int prio)
{
	if (prio >= DAN_JAR_MIN_DUMMY_PRIO && prio <= DAN_JAR_MAX_DUMMY_PRIO)
		return 1;
	return 0;
}

static inline struct task_struct *dummy_task_of(struct sched_dummy_entity *dummy_se)
{
	return container_of(dummy_se, struct task_struct, dummy_se);
}

static inline int _compute_queue_level(int prio)
{
	return prio - DAN_JAR_MIN_DUMMY_PRIO;
}

static inline void _enqueue_task_dummy(struct rq *rq, struct task_struct *p)
{
	int queue_level = _compute_queue_level(p->prio);
	struct sched_dummy_entity *dummy_se = &p->dummy_se;
	struct list_head *queue = &rq->dummy.queue[queue_level];
	
	// Initialize the extra fields of dummy_se, for the RR (1st one) and aging (2 others)
	dummy_se->time_slice = get_rr_interval_dummy(rq, p);
	dummy_se->time_aging = 0;
	dummy_se->prio = p->prio;

	list_add_tail(&dummy_se->run_list, queue);
}

static inline void _dequeue_task_dummy(struct task_struct *p)
{
	struct sched_dummy_entity *dummy_se = &p->dummy_se;
	list_del_init(&dummy_se->run_list);
}

/*
 * Scheduling class functions to implement
 */

// Happens when a process changes from a sleeping into a runnable state
static void enqueue_task_dummy(struct rq *rq, struct task_struct *p, int flags)
{
	if(_keep_prio(p->prio)) 
	{
		_enqueue_task_dummy(rq, p);
		add_nr_running(rq,1); // Increment counter of nr_running
	}
}

// Happens when a process switches from a runnable into an un-runnable state or when the kernel decides to take it off the run queue
static void dequeue_task_dummy(struct rq *rq, struct task_struct *p, int flags)
{
	if(_keep_prio(p->prio)) 
	{
		_dequeue_task_dummy(p);
		sub_nr_running(rq,1); // Decrement counter of nr_running
	}
}

// When a process gives the CPU to other processes voluntarily
// Caused by a sched_yield system call
static void yield_task_dummy(struct rq *rq)
{
	struct sched_dummy_entity *dummy_se = &rq->curr->dummy_se;
	int queue_level = _compute_queue_level(rq->curr->prio);
	struct list_head *queue = &rq->dummy.queue[queue_level];

	// We move the current task to the end of its corresponding queue
	list_move_tail(&(dummy_se->run_list), queue);
}

// Check if the current running task should be preempted by a new ready task and call resched_task if so
// Called for example when a task wakes up
static void check_preempt_curr_dummy(struct rq *rq, struct task_struct *p, int flags)
{
	struct dummy_rq *dummy_rq = &rq->dummy;

	int queue_level = _compute_queue_level((&p->dummy_se)->prio);
	int level_with_higher_priority = queue_level;
	
	//Check whether a process with a higher priority exists
	int i = 0;
	for(; i < queue_level && level_with_higher_priority == queue_level; ++i)
		if(!list_empty(&dummy_rq->queue[i]))
			level_with_higher_priority = i;

	if(level_with_higher_priority < queue_level)
		// Before going into user-mode, the kernel check whether the current process has to be reschedule checking this flag.
		// Afterwards, when picking the next process, the lowest one will be chosen (as written in pick_next_task_dummy)
		set_tsk_need_resched(p);
}

// Select the next task to run
// put_prev_task is called before
static struct task_struct *pick_next_task_dummy(struct rq *rq, struct task_struct* prev)
{
	struct dummy_rq *dummy_rq = &rq->dummy;
	struct sched_dummy_entity *next;

	int i = 0;
	// We choose the highest task
	for(; i < DAN_JAR_NB_LEVEL_PRIORITY; ++i)
		if(!list_empty(&dummy_rq->queue[i]))
		{
			next = list_first_entry(&dummy_rq->queue[i], struct sched_dummy_entity, run_list);
            put_prev_task(rq, prev);
			return dummy_task_of(next);
		}

	return NULL;
}

// Called when a running task is rescheduled
// Right before pick_next_task
static void put_prev_task_dummy(struct rq *rq, struct task_struct *prev)
{
	// Nothing, we don't use statistics.
}

// Called when a scheduling policy of the task is changed
static void set_curr_task_dummy(struct rq *rq)
{
	// Nothing, we won't change scheduling policy
}

// Time accounting (e.g., aging, timeslice control)
// Timeslice-based preemption
// Called on timer
static void task_tick_dummy(struct rq *rq, struct task_struct *curr, int queued)
{
	struct sched_dummy_entity *dummy_se = &curr->dummy_se;
	int queue_level = _compute_queue_level(dummy_se->prio);
	struct list_head *queue = &rq->dummy.queue[queue_level];

	int i;
	int level_with_lower_priority, level_with_higher_priority;
	struct sched_dummy_entity* old_task_se;
	struct dummy_rq *dummy_rq = &rq->dummy;

	/********************/
	/****    Aging    ***/
	/********************/
 	i = 0;
	level_with_higher_priority = level_with_lower_priority = queue_level;
	// Find the highest/lowest queue with a task
	for(; i < DAN_JAR_NB_LEVEL_PRIORITY; ++i)
		if(!list_empty(&dummy_rq->queue[i]))
		{
			if(i < level_with_higher_priority)
				level_with_higher_priority = i;
			if(i > level_with_lower_priority)
				level_with_lower_priority = i;
		}

	// If there exists a task with a lower priority (e.g. in another lower queue), we age the lowest task
	if(level_with_lower_priority > level_with_higher_priority)
	{
		old_task_se = list_first_entry(&dummy_rq->queue[level_with_lower_priority], struct sched_dummy_entity, run_list);

		// Increment the time_aging
		old_task_se->time_aging++;

		// If the age-threshold has been reached, we move the task and set the time_slice !
		if(old_task_se->time_aging >= get_age_threshold() && old_task_se->prio > curr->prio)
		{
			// Set the time_aging at 1, because when it'll be processed, we can know that it was an aging task
			old_task_se->time_aging = 1;
			
			// Update the timeslice
			// Further the task is, lower CPU it will have. All tasks has the same time_slice as default.
			// For example, if we have A 11, B 12 and C 13, C will have twice less as B and C when it
			// will have the CPU because it ages ! In the case of RR, it doesn't change
			old_task_se->time_slice /= (level_with_lower_priority-level_with_higher_priority);

			// Dequeue the task, we don't use the function dequeue because it can act differently
			list_del_init(&old_task_se->run_list);
			sub_nr_running(rq, 1);

			// Increase priority, bounded by the current process which is supposed to have the highest priority
			if(--old_task_se->prio < curr->prio)
				old_task_se->prio = curr->prio;

			// Enqueue the task to the corresponding new queue
			list_add_tail(&old_task_se->run_list, &dummy_rq->queue[_compute_queue_level(old_task_se->prio)]);
			add_nr_running(rq, 1);
		}
	}

	/**********************/
	/****      RR      ****/
	/**********************/
	if(dummy_se->time_slice > 0)
		--dummy_se->time_slice;
	else 
	{
		dummy_se->time_slice = get_rr_interval_dummy(rq, curr);

		// if it is an aging task, we have to reset its priority
		if(dummy_se->time_aging > 0)
		{
			dummy_se->time_aging = 0;
			dummy_se->prio = curr->prio;
	
			// Move the aging process back from where it was
            list_move_tail(&(dummy_se->run_list), &dummy_rq->queue[_compute_queue_level(dummy_se->prio)]);
			set_tsk_need_resched(curr);
		}
		// Requeue the element if there are others processes in the same queue (RR)
		// Otherwise the process can still run
		else if (dummy_se->run_list.prev != dummy_se->run_list.next)
		{
            // Move the current process at the end of the queue
            list_move_tail(&(dummy_se->run_list), queue);
			set_tsk_need_resched(curr);
		}
	}
}

// Called when a scheduling class changed
static void switched_from_dummy(struct rq *rq, struct task_struct *p)
{
	// Nothing, won't change scheduling class
}

// Called when a scheduling class changed
static void switched_to_dummy(struct rq *rq, struct task_struct *p)
{
	// Nothing, won't change scheduling class
}

// Called when the priority changes
static void prio_changed_dummy(struct rq*rq, struct task_struct *p, int oldprio)
{
	// Update the prio field for the aging if the task has changed its priority
	(&p->dummy_se)->prio = p->prio;
}

#ifdef CONFIG_SMP
/*
 * SMP related functions	
 */

static inline int select_task_rq_dummy(struct task_struct *p, int cpu, int sd_flags, int wake_flags)
{
	int new_cpu = smp_processor_id();
	
	return new_cpu; //set assigned CPU to zero
}


static void set_cpus_allowed_dummy(struct task_struct *p,  const struct cpumask *new_mask)
{
}
#endif
/*
 * Scheduling class
 */
static void update_curr_dummy(struct rq*rq)
{
	//Nothing, We do not update rq statistics.
}
const struct sched_class dummy_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_dummy,
	.dequeue_task		= dequeue_task_dummy,
	.yield_task		= yield_task_dummy,

	.check_preempt_curr	= check_preempt_curr_dummy,
	
	.pick_next_task		= pick_next_task_dummy,
	.put_prev_task		= put_prev_task_dummy,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_dummy,
	.set_cpus_allowed	= set_cpus_allowed_dummy,
#endif

	.set_curr_task		= set_curr_task_dummy,
	.task_tick		= task_tick_dummy,

	.switched_from		= switched_from_dummy,
	.switched_to		= switched_to_dummy,
	.prio_changed		= prio_changed_dummy,

	.get_rr_interval	= get_rr_interval_dummy,
	.update_curr		= update_curr_dummy,
};
