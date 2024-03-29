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

		KASSERT(NULL != p);/* should have associated process */
        dbg(DBG_THR,"(GRADING1 3.a) The associated process should not be NULL.\n");
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
		dbg(DBG_THR, "The thread (0x%p) of proc \"%s\" %d (0x%p) has been created.\n",
						new_kthread, p->p_comm, p->p_pid, p);
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
        KASSERT(kthr!=NULL);/* should have thread */
		dbg(DBG_THR,"(GRADING1 3.b) The associated thread should not be NULL.\n");
        KASSERT(curthr!=NULL);
		if (kthr == curthr) /* must be runnable */
		{
        		kthread_exit(retval);
		}
		else
		{
			KASSERT(kthr->kt_state== KT_SLEEP || kthr->kt_state==KT_SLEEP_CANCELLABLE);
			kthr->kt_cancelled = 1;
			kthr->kt_retval = retval;			
			
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
        KASSERT(curthr!=NULL);
        KASSERT(!curthr->kt_wchan);/* queue should be empty */
		dbg(DBG_THR,"(GRADING1 3.c) The current thread should not be in any ktqueue.\n");
        KASSERT(!curthr->kt_qlink.l_next && !curthr->kt_qlink.l_prev);/* queue should be empty */
		dbg(DBG_THR,"(GRADING1 3.c) The current thread should not link in any ktqueue.\n");
        KASSERT(curthr->kt_proc == curproc);
		dbg(DBG_THR,"(GRADING1 3.c) The thread must exit by itself.\n");
        
		proc_thread_exited(retval);

		curthr->kt_state = KT_EXITED;
		curthr->kt_retval = retval;

		sched_switch();
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
        /*NOT_YET_IMPLEMENTED("VM: kthread_clone");*/
	KASSERT(KT_RUN == thr->kt_state);
	dbg(DBG_PRINT, "(GRADING3A 8.a) the thread state is run\n ");
	kthread_t *clone_thr;
	proc_t *p;

	clone_thr = (kthread_t *) slab_obj_alloc(kthread_allocator);
	clone_thr->kt_retval = thr->kt_retval;
	clone_thr->kt_errno = thr->kt_errno;

	p = list_tail(&thr->kt_proc->p_children,proc_t,p_child_link);
	clone_thr->kt_proc = p;

	clone_thr->kt_cancelled = thr->kt_cancelled;
	clone_thr->kt_wchan = NULL;
	list_insert_tail(&clone_thr->kt_proc->p_threads,&clone_thr->kt_plink);
#ifdef __MTP__ 
	clone_thr->kt_detached = 0;
	clone_thr->kt_joinq = NULL;
#endif
	clone_thr->kt_kstack = alloc_stack();
	/*clone_thr->kt_ctx = thr->kt_ctx;
	clone_thr->kt_ctx->c_kstack = clone_thr->kt_kstack;*/
	context_setup(&(clone_thr->kt_ctx),
			NULL,
			0,
			0,
			(void *) clone_thr->kt_kstack,
			DEFAULT_STACK_SIZE,
			clone_thr->kt_proc->p_pagedir);
	clone_thr->kt_state = curthr->kt_state;
	KASSERT(KT_RUN == clone_thr->kt_state);
	dbg(DBG_PRINT, "(GRADING3A 8.a) the new thread state is run\n ");
	return clone_thr;
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
