25/25/25/25

Work Distribution:
	Ming-Yi Hsiao:
	mmap.c , pframe.c , syscall.c, vfs_syscall.c

	Chieh-I Wu:
	shadow.c , vnode.c, fork.c, proc.c

	Yan-Ching Lin:
	anon.c, brk.c, access.c, kthread.c

	Chieh-Chun Chang:
	vmmap.c, vnode.c , pagefault.c, open.c

Part(B):
All passed
	We can pass the tests both with user shell and withour user shell.

Part(C):
	All tests below can run with user shell, but while we type /sbin/halt, we fail in halting.
	/usr/bin/segfault  
	/usr/bin/stress    : our shell return at the end of the execution.
	/usr/bin/forkbomb  : the execution time is more than 5 minutes.

	/usr/bin/eatmem    : It works but it will be pending after eating more than 7600 pages.
	/usr/bin/vfstest   : We pass all function test except function vfstest_s5fs_vm().
	/usr/bin/memtest   : only childtest(test_overflow) can pass the test


Part(D): skip
Part(E):
	We tested all parts implemented this time. 
	These including: 
		fork		(in forkbomb, fork-and-wait test)
		clone		(in forkbomb, fork-and-wait test)
		copy-on-write	(in forkbomb, fork-and-wait test)
		shadow obj	(for every test)
		pagefault	(for every test)
		map, unmap	(for every test)
		break		(in memtest, stress)
		zeromap		(in stress)
		seg fault	(in segfault)
		pframe get	(for every test)
		anon obj	(for every test)	
