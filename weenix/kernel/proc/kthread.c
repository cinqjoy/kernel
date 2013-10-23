#include "config.h"
#include "globals.h"

#include "errno.h"

#include "util/init.h"
#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"

#include "mm/slab.h"
#include "mm/page.h"

/* MC remember current thread */
kthread_t *curthr; /* global */

/* MC
 strcut slab_allocator = slab_allocator_t
define in kernel/mm/slab.c
 thread_init create space for it
 ?? which function should assign it's instance to a list if multiple
 10/19  if one process has one thread, then that each process has only one is reasonable. for multiple threads per process, may not */
static slab_allocator_t *kthread_allocator = NULL;

/*MC MTP
 multiple threads per process */
#ifdef __MTP__
/* Stuff for the reaper daemon, which cleans up dead detached threads */
static proc_t *reapd = NULL;
static kthread_t *reapd_thr = NULL;
static ktqueue_t reapd_waitq;
static list_t kthread_reapd_deadlist; /* Threads to be cleaned */

static void *kthread_reapd_run(int arg1, void *arg2);
#endif

void
kthread_init()
{
		/* MC 
		 staticglobal pointer
		 slab_allocatir_create defined in 
		 kernel/mm/slab.c */
        kthread_allocator = slab_allocator_create("kthread", sizeof(kthread_t));
        KASSERT(NULL != kthread_allocator);
}

/**
 * Allocates a new kernel stack.
 *
 * @return a newly allocated stack, or NULL if there is not enough
 * memory available
 */
static char *
alloc_stack(void)
{
        /* extra page for "magic" data */
        char *kstack;
        int npages = 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT);
		/* MC
		 define in kernel/mm/page.c, kernel/include/config.h
		 page shift = 12, 4MB as a unit
		 define in kernel/include/config.h
		 DEFAULT_STACK_SIZE 56*1024 */
        kstack = (char *)page_alloc_n(npages);

        return kstack;
}

/**
 * Frees a stack allocated with alloc_stack.
 *
 * @param stack the stack to free
 */
static void
free_stack(char *stack)
{
        page_free_n(stack, 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT));
}

/*
 * Allocate a new stack with the alloc_stack function. The size of the
 * stack is DEFAULT_STACK_SIZE.
 *
 * Don't forget to initialize the thread context with the
 * context_setup function. The context should have the same pagetable
 * pointer as the process.
 */
kthread_t *
kthread_create(struct proc *p, kthread_func_t func, long arg1, void *arg2)
{
		kthread_t * new_kthread;

		KASSERT(NULL != p);

		new_kthread = (kthread_t *) slab_obj_alloc(kthread_allocator);
		KASSERT(NULL != new_kthread);
		new_kthread->kt_retval = NULL; /* void point, setup to null since havent return*/
		new_kthread->kt_errno = 0; /* ?? changed by system call, */
		new_kthread->kt_proc = p;
		new_kthread->kt_cancelled = 0;
		new_kthread->kt_wchan = NULL;
		list_init(&new_kthread->kt_qlink);
		list_init(&new_kthread->kt_plink);
		list_insert_tail(&p->p_threads, &new_kthread->kt_plink);
#ifdef __MTP__
		new_kthread->kt_detached = 0; /* another function will change it */
		new_kthread->kt_joinq = NULL; /*  changed while asking a mutex  */
#endif
		new_kthread->kt_kstack = alloc_stack();
		KASSERT(NULL != new_kthread->kt_kstack) ; /* not sure if needed */
		context_setup(&(new_kthread->kt_ctx), 
						(void *)func, 
						arg1,
						arg2,
						(void *) new_kthread->kt_kstack , 
						DEFAULT_STACK_SIZE, 
						new_kthread->kt_proc->p_pagedir);

		return new_kthread;

        /*NOT_YET_IMPLEMENTED("PROCS: kthread_create");
        return NULL; */
}

void
kthread_destroy(kthread_t *t)
{
		/* MC
		 makre sure thread and it's stack is not null */
        KASSERT(t && t->kt_kstack);
        free_stack(t->kt_kstack);
		/* MC
		 check if it links
		 remove from belonged process's own thread list */
        if (list_link_is_linked(&t->kt_plink))
                list_remove(&t->kt_plink);

		/* MC
		 kernel/mm/slab.c */
        slab_obj_free(kthread_allocator, t);
}

/*
 * If the thread to be cancelled is the current thread, this is
 * equivalent to calling kthread_exit. Otherwise, the thread is
 * sleeping and we need to set the cancelled and retval fields of the
 * thread.
 *
 * If the thread's sleep is cancellable, cancelling the thread should
 * wake it up from sleep.
 *
 * If the thread's sleep is not cancellable, we do nothing else here.
 */
void
kthread_cancel(kthread_t *kthr, void *retval)
{
		/*MC
		 canceled thread is not null and current thread is not */
        KASSERT(kthr!=NULL);
        KASSERT(curthr!=NULL);
		if (kthr == curthr) /* must be runnable */
		{
        		kthread_exit(retval);
		}
		else
		{
			KASSERT(kthr->kt_state== KT_SLEEP || kthr->kt_state==KT_SLEEP_CANCELLABLE);
			kthr->kt_cancelled = 1; 
			if (kthr->kt_state==KT_SLEEP_CANCELLABLE)
			{
				sched_cancel(kthr);
			}

		}


        /*NOT_YET_IMPLEMENTED("PROCS: kthread_cancel");*/
}

/*
 * You need to set the thread's retval field, set its state to
 * KT_EXITED, and alert the current process that a thread is exiting
 * via proc_thread_exited.
 *
 * It may seem unneccessary to push the work of cleaning up the thread
 * over to the process. However, if you implement MTP, a thread
 * exiting does not necessarily mean that the process needs to be
 * cleaned up.
 */
void
kthread_exit(void *retval)
{
	       
		//kthread_t * old_cur_kthread;
		/* MC check current thread is not null
		 since current thread is about to exit */
		//old_cur_kthread = curthr;
		/*  ? not sure if need to check */
        /*KASSERT(old_cur_kthread!=NULL);*/

		/* switch current state while getting mutex lock inside */
		/* 10/20 should be called in proc_thread_exited(); */
		/*sched_switch();*/
	

		curthr->kt_state = KT_EXITED;
		/* return value to process
		 default is success*/

		curthr->kt_retval = retval;

		/* MC
		alert proc_thread_exited 
		 defined in proc.c
		 ?? */

		/*MC
		 back to initial state in some variable
		old_cur_thread->kt_errno = 0; // changed by system call, 
		10/20 i think changed in process for totoal clean */
		/*old_cur_kthread->kt_proc = p;
		old_cur_kthread->kt_cancelled = 1;*/

		/*10/20 ? schedule do or I do */
		/*old_cur_kthread->kt_wchan = NULL;*/ /* wait for schedule */
		/*old_cur_thread->qlink = NULL; */ /* assgined by schedule to proper queue */

		/*old_cur_kthread->plink = NULL;*/

		/*10/21 ?
		 no need to change
		old_cur_thread->kt_detached = 0;
		10/21
		old_cur_thread->kt_joinq = NULL; // changed while asking a mutex */

		/* MC
		alert proc_thread_exited 
		 defined in proc.c */
		KASSERT(!curthr->kt_wchan);
		KASSERT(!curthr->kt_qlink.l_next && !curthr->kt_qlink.l_prev)
		proc_thread_exited(curthr->kt_retval);
		

		/*MC
		 ? destroy
		 but ID and return value should be kept for join
 		 ID  may not need 
		 return value is passed by argument
		 10/20 ? in process or in thread*/
		/*kthread_destroy(old_cur_kthread);*/

		/* switch current state while getting mutex lock inside */
		/* 10/20 should be called in proc_thread_exited(); */
		sched_switch();


        /*NOT_YET_IMPLEMENTED("PROCS: kthread_exit"); */
}

/*
 * The new thread will need its own context and stack. Think carefully
 * about which fields should be copied and which fields should be
 * freshly initialized.
 *
 * You do not need to worry about this until VM.
 */
kthread_t *
kthread_clone(kthread_t *thr)
{
        NOT_YET_IMPLEMENTED("VM: kthread_clone");
        return NULL;
}

/*
 * The following functions will be useful if you choose to implement
 * multiple kernel threads per process. This is strongly discouraged
 * unless your weenix is perfect.
 */
#ifdef __MTP__
int
kthread_detach(kthread_t *kthr)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_detach");
        return 0;
}

int
kthread_join(kthread_t *kthr, void **retval)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_join");
        return 0;
}

/* ------------------------------------------------------------------ */
/* -------------------------- REAPER DAEMON ------------------------- */
/* ------------------------------------------------------------------ */
static __attribute__((unused)) void
kthread_reapd_init()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_init");
}
init_func(kthread_reapd_init);
init_depends(sched_init);

void
kthread_reapd_shutdown()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_shutdown");
}

static void *
kthread_reapd_run(int arg1, void *arg2)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_run");
        return (void *) 0;
}
#endif
