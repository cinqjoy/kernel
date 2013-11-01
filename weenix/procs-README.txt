Team grade 25 25 25 25
(1): 
Ming-Yi Hsiao: 
sched_sleep_on, sched_cancellable_sleep_on, sched_wakeup_on, 
sched_broadcast_on, sched_cancel, sched_switch, sched_make_runnable

Chieh-I Wu: 
kmutex_init, kmutex_lock, kmutex_cancellable, kmutex_unlock, 
bootstrap, initproc_creat, initproc_run.

Yan-Ching Lin: proc_creat, proc_cleanup, do_waitpid

Chieh-Chun Chang:
kthread_create, kthread_cancel, kthread_exit, proc_kill, 
proc_kill_all, proc_thread_exited, do_exit

(2): At the first, we split the project by file. When we started to implement 
the function, we realized that some functions are better to be assigned by association. 
For example, proc_thread_exited is highly related to kthread_exit. So some of functions 
should be implemented in the proc.c were reassigned to the person who was in charge of 
kthread_exit. Although we split the project by function, all members in the team understand 
each implemented function. This makes each member possible to trace code and solve problems 
when bug comes out.


(3) The instructions for kshell:
	Help: prints a list of available commands
	Echo: display a line of text
	Exit: exits the shell
	Testproc: Ted Fabor’s tests
	Shtest: sunghan’s tests
	Dltest: sunghan’s deadlock tests

(4) The function tested in the test case:
All implemented functions in proc.c are tested by the Shunghan’s test 
file and Ted Fabor’s test using dbg_print.

kmutex.c, sched.c, kmain.c, kthread.c are tested by Fabor’s test case. 
Those are not done by dbg_print. However, during the implement, we have to 
use gdb debug tool to find out where is our bug. During this process, all of 
the functions need to be implemented are tested since while we debug, we have 
to set break points in all of those functions.
