#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
        proc_t *myProc;
		int i;
		myProc = (proc_t*)slab_obj_alloc(proc_allocator);
		myProc->p_pid = _proc_getid();
		KASSERT(PID_IDLE != myProc->p_pid || list_empty(&_proc_list)); 
		/* pid can only be PID_IDLE if this is the first process */
		dbg(DBG_PROC,"(GRADING1 2.a) pid can only be PID_IDLE if this is the first process.\n");		
		KASSERT(PID_INIT != myProc->p_pid || PID_IDLE == curproc->p_pid);
		/* pid can only be PID_INIT when creating from idle process */
		dbg(DBG_PROC,"(GRADING1 2.a) pid can only be PID_INIT when creating from idle process.\n");
		if(myProc->p_pid == PID_INIT){
			proc_initproc = myProc;	
		}
		strcpy(myProc->p_comm,name);
		
		list_init(&myProc->p_threads);
		list_init(&myProc->p_children);
		list_init(&myProc->p_child_link);
		list_init(&myProc->p_list_link);
		
		myProc->p_pproc = curproc;

		myProc->p_status=0;
		myProc->p_state=PROC_RUNNING;

		sched_queue_init(&myProc->p_wait);

		myProc->p_pagedir = pt_create_pagedir();

		list_insert_tail(&_proc_list,&myProc->p_list_link);
		if(curproc!=NULL){
			dbg(DBG_PROC,"The proc \"%s\" %d (0x%p) had been created by the proc \"%s\" %d (0x%p)\n"
						,myProc->p_comm, myProc->p_pid, myProc, curproc->p_comm, curproc->p_pid, curproc);
			list_insert_tail(&curproc->p_children,&myProc->p_child_link);	
		}else{
			dbg(DBG_PROC,"The proc \"%s\" %d (0x%p) had been created\n"
						,myProc->p_comm, myProc->p_pid, myProc);
		}
		
		for(i=0;i<NFILES;i++)
			myProc->p_files[i] = NULL;
		myProc->p_cwd = NULL;
		
		myProc->p_brk = NULL;
		myProc->p_start_brk = NULL;
		myProc->p_vmmap = vmmap_create();
		if(myProc->p_vmmap != NULL){
			myProc->p_vmmap->vmm_proc = myProc;
		}
        return myProc;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
	int i=0;
	KASSERT(NULL != proc_initproc); /* should have an "init" process */
	dbg(DBG_PROC,"(GRADING1 2.b) The \"init\" process should not ne NULL.\n");
	KASSERT(1 <= curproc->p_pid); /* this process should not be idle process */
	dbg(DBG_PROC,"(GRADING1 2.b) This process should not be \"idle\" process.\n");
	KASSERT(NULL != curproc->p_pproc); /* this process should have parent process */
	dbg(DBG_PROC,"(GRADING1 2.b) This process should have parent process.\n");

	proc_t *myProc;
	curproc->p_state=PROC_DEAD;
	curproc->p_status=status;
	dbg(DBG_PROC,"The proc \"%s\" %d (0x%p) is dead!\n",
				curproc->p_comm, curproc->p_pid, curproc);

	if(!list_empty(&curproc->p_children)){
		list_iterate_begin(&curproc->p_children,myProc,proc_t,p_child_link){
			dbg(DBG_PROC,"The child proc \"%s\" %d (0x%p), had been assigned to the proc \"%s\" %d (0x%p).\n"
						,myProc->p_comm, myProc->p_pid, myProc, proc_initproc->p_comm, proc_initproc->p_pid, proc_initproc);
			myProc->p_pproc = proc_initproc;
			list_remove(&myProc->p_child_link);
			list_insert_tail(&proc_initproc->p_children, &myProc->p_child_link);	
		}list_iterate_end();
	}
	for(i=0;i<NFILES;i++){

		if(curproc->p_files[i]!=NULL){
			dbg(DBG_PROC,"fd %d is being closed and the vn_vno is %d\n",i, curproc->p_files[i]->f_vnode->vn_vno);
			do_close(i);
		}
		
	}
	sched_wakeup_on(&curproc->p_pproc->p_wait);
	KASSERT(NULL != curproc->p_pproc); /* this process should have parent process */
	dbg(DBG_PROC,"(GRADING1 2.b) This process should have parent process.\n");
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
        kthread_t *kthr;

		KASSERT(p != NULL);
		if (p == curproc){
			do_exit(status);
		}else{
        	list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
					KASSERT(kthr != NULL);
			   		kthread_cancel(kthr, NULL);
       	 	} list_iterate_end();

		}
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
	dbg(DBG_PROC,"All processes are going to be killed except the child processes of IDLE process.\n");
	proc_t *myProc;
	list_iterate_begin(&_proc_list,myProc,proc_t,p_list_link){
	        if(myProc->p_pid!=PID_IDLE && myProc->p_pid!=PID_INIT && myProc->p_pid!=curproc->p_pid && myProc->p_pproc->p_pid != PID_IDLE){
			proc_kill(myProc,0);
		}
	}list_iterate_end();
	if(curproc->p_pid!=PID_IDLE && curproc->p_pid!=PID_INIT && curproc->p_pproc->p_pid != PID_IDLE)
		proc_kill(curproc,0);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
        int count = 0;
		KASSERT(curproc != NULL);

        kthread_t *kthr;
        list_iterate_begin(&curproc->p_threads, kthr, kthread_t, kt_plink) {
				if(kthr->kt_state != KT_EXITED)
					count++;
        } list_iterate_end();

		KASSERT(count != 0 && "All threads of curproc are dead!\n");
		if (count == 1){
			dbg(DBG_THR,"Last thread (0x%p) exited from the proc \"%s\" %d (0x%p)\n",
					curthr, curproc->p_comm, curproc->p_pid, curproc);
			proc_cleanup(curproc->p_status);
		}else{
			dbg(DBG_THR,"The thread (0x%p) exited from the proc \"%s\" %d (0x%p)\n",
					curthr, curproc->p_comm, curproc->p_pid, curproc);
		}
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
    int is_inside=0;
	pid_t myPid;
	proc_t *myProc;
	kthread_t *myThread;
	KASSERT(options==0 && pid>=-1);
 	if(list_empty(&curproc->p_children)){
		return -ECHILD;
	}
	list_iterate_begin(&curproc->p_children,myProc,proc_t,p_child_link){
		if(myProc->p_pid==pid){
			is_inside=1;
		} 
	}list_iterate_end();
	if(!is_inside && pid != -1)
		return -ECHILD;
	KASSERT(-1 == pid || is_inside == 1); /* should be able to find the process */
    dbg(DBG_PROC,"(GRADING1 2.c) The child process has been found.\n");

	while(1){
		if(pid==-1){
			list_iterate_begin(&curproc->p_children,myProc,proc_t,p_child_link){
			    KASSERT(NULL != myProc); /* the process should not be NULL */
				if(myProc->p_state==PROC_DEAD){
					if(status != NULL)
						*status = myProc->p_status;
					myPid = myProc->p_pid;
					list_iterate_begin(&myProc->p_threads,myThread,kthread_t,kt_plink){
						/* thr points to a thread to be destroied */
						KASSERT(KT_EXITED == myThread->kt_state);
						dbg(DBG_PROC,"(GRADING1 2.c) The state of the thread that are going to be destroied should be exited.\n");
						kthread_destroy(myThread);
					}list_iterate_end();
					list_remove(&myProc->p_list_link);
					list_remove(&myProc->p_child_link);
					KASSERT(NULL != myProc->p_pagedir); /* this process should have pagedir */
					dbg(DBG_PROC,"(GRADING1 2.c) This process should have pagedir.\n");
					pt_destroy_pagedir(myProc->p_pagedir);
					slab_obj_free(proc_allocator,myProc);
					return myPid;
				}			
			}list_iterate_end();
		}else{
			myProc = proc_lookup(pid);
			KASSERT(NULL != myProc); /* the process should not be NULL */
			dbg(DBG_PROC,"(GRADING1 2.c) The process should be in the process list.\n");
			if(myProc->p_state==PROC_DEAD){
				if(status != NULL)
					*status = myProc->p_status;
				myPid = myProc->p_pid;
				list_iterate_begin(&myProc->p_threads,myThread,kthread_t,kt_plink){
					/* thr points to a thread to be destroied */
					KASSERT(KT_EXITED == myThread->kt_state);
					dbg(DBG_PROC,"(GRADING1 2.c) The state of the thread that are going to be destroied should be exited.\n");
					kthread_destroy(myThread);
				}list_iterate_end();
				list_remove(&myProc->p_list_link);
				list_remove(&myProc->p_child_link);
				KASSERT(NULL != myProc->p_pagedir); /* this process should have pagedir */
				dbg(DBG_PROC,"(GRADING1 2.c) This process should have pagedir.\n");
				pt_destroy_pagedir(myProc->p_pagedir);
				slab_obj_free(proc_allocator,myProc);
				return myPid;
			}
		}
		sched_sleep_on(&curproc->p_wait);	
	}
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
		proc_t * exited_thread_proc;
		kthread_t *kthr;

		exited_thread_proc = curproc;

		KASSERT(exited_thread_proc != NULL);
		KASSERT(curthr != NULL);

		exited_thread_proc->p_status = status;
		
#ifdef  _MTP_
        list_iterate_begin(&exited_thread_proc->p_threads, kthr, kthread_t, kt_plink) {
               if (kthr != curthr)
			   {
					KASSERT(kthr != NULL);
			   		kthread_cancel(kthr, NULL);
				}
						   		
        } list_iterate_end();
		

        list_iterate_begin(&exited_thread_proc->p_threads, kthr, kthread_t, kt_plink) {
        		if (kthr != curthr)
			   	{
					KASSERT(kthr != NULL);
			   		kthread_join(kthr, NULL);
				}				   		
        } list_iterate_end();
#endif	

		kthread_exit(NULL);
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}
