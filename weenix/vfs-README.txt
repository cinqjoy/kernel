25 25 25 25 

do_open: the flag ALL_TRUNC is not tested during the test.  

do_mkdir: It just creates an entry and does not create the corresponding vnode. 
Vnode is created when the entry is first time accessed. For a special case, if 
the input is ¡§.¡¨, it should return ¡¥exist¡¦ rather then EINVAL.

do_chdir: While we leave the current working directory, we should 
decrease the reference count of vnode of cwd. Since we use the dir_namev & 
lookup to check the target directory, if it is valid directory, before we 
change the cwd to the target directory, we should decrease the reference count 
of vnode of the parent directory of the target directory. A special case happens 
when path is root. When the path is root, it will execute vput twice, because first 
time is for itself, and the second time is for the parent. However, for root, 
the parent is itself.

The most complicated part is to use vput, vref, vget, fget, fput. If with 
carelessness, the reference count will be in a mass. 

