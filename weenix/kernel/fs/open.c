/*
 *  FILE: open.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Mon Apr  6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"

/* find empty index in p->p_files[] */
int
get_empty_fd(proc_t *p)
{
        int fd;

        for (fd = 0; fd < NFILES; fd++) {
                if (!p->p_files[fd])
                        return fd;
        }

        dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
            "for pid %d\n", curproc->p_pid);
        return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 *      1. Get the next empty file descriptor.
 *      2. Call fget to get a fresh file_t.
 *      3. Save the file_t in curproc's file descriptor table.
 *      4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
 *         oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 *         O_APPEND.
 *      5. Use open_namev() to get the vnode for the file_t.
 *      6. Fill in the fields of the file_t.
 *      7. Return new fd.
 *
 * If anything goes wrong at any point (specifically if the call to open_namev
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        oflags is not valid.
 *      o EMFILE
 *        The process already has the maximum number of files open.
 *      o ENOMEM
 *        Insufficient kernel memory was available.
 *      o ENAMETOOLONG
 *        A component of filename was too long.
 *      o ENOENT
 *        O_CREAT is not set and the named file does not exist.  Or, a
 *        directory component in pathname does not exist.
 *      o EISDIR
 *        pathname refers to a directory and the access requested involved
 *        writing (that is, O_WRONLY or O_RDWR is set).
 *      o ENXIO
 *        pathname refers to a device special file and no corresponding device
 *        exists.
 */

int
do_open(const char *filename, int oflags)
{
	file_t *ft;
	int fd, accmode, flag, err;
	vnode_t *res_vnode;

	fd=get_empty_fd(curproc);
	if(fd<0){
		dbg(DBG_PRINT, "ERROR(Filename=%s): Current process(pid=%d) already has the maximum number of files open.\n", filename, curproc->p_pid);
		return fd;/* return -EMFILE */
	}

	ft=fget(-1);
	if(!ft){
		dbg(DBG_PRINT, "ERROR(Filename=%s): Insufficient kernel memory was available.\n", filename);
		return -ENOMEM;
	}
	else curproc->p_files[fd]=ft;

	accmode=oflags&0x00F;
	flag=oflags&0xF00;

	switch(accmode){
		case O_RDONLY: 
			ft->f_mode=FMODE_READ;
			break;
		case O_WRONLY:
			ft->f_mode=FMODE_WRITE;
			break;
		case O_RDWR:
			ft->f_mode=FMODE_READ|FMODE_WRITE;
			break;
		default:
			fput(ft);
			dbg(DBG_PRINT, "ERROR(Filename=%s): Oflags is not valid.", filename);
			return -EINVAL;
		}

	if(flag&O_APPEND) ft->f_mode|=FMODE_APPEND;
	if(flag&O_TRUNC && !(accmode&O_RDONLY)){
		ft->f_pos=0;
		ft->f_vnode->vn_len=0;
		}

	if(strlen(filename) > MAXPATHLEN){ 
		fput(ft);
		dbg(DBG_PRINT, "ERROR(Filename=%s): A component of filename was too long.\n", filename);
		return -ENAMETOOLONG;
		}
	if(!(accmode&O_RDONLY) && S_ISDIR(accmode)){
		fput(ft);
		dbg(DBG_PRINT, "ERROR(Filename=%s): Pathname refers to a directory and the access requested involved writing.\n", filename);
		return -EISDIR;
		}

	err=open_namev(filename, flag, &res_vnode, NULL);
	if(err<0){
		fput(ft);
		dbg(DBG_PRINT, "ERROR(Filename=%s): The file a directory component in pathname does not exist.\n", filename);
		return err;/* return -ENOENT */
		}
	if(S_ISCHR(res_vnode->vn_mode) && !bytedev_lookup(res_vnode->vn_devid)){
		vput(res_vnode);
		fput(ft);
		dbg(DBG_PRINT, "ERROR(Filename=%s): Pathname refers to a character special file and no corresponding device(id=%d) exists.\n", filename, res_vnode->vn_devid);
		return -ENXIO;
		}
	if(S_ISBLK(res_vnode->vn_mode) && !blockdev_lookup(res_vnode->vn_devid)){
		vput(res_vnode);
		fput(ft);
		dbg(DBG_PRINT, "ERROR(Filename=%s): Pathname refers to a block special file and no corresponding device(id=%d) exists.\n", filename,res_vnode->vn_devid);
		return -ENXIO;
		}

	ft->f_vnode=res_vnode;   
	ft->f_pos=0;

	dbg(DBG_PRINT, "Successfully opened the file \"%s\".\n", filename);
	return fd;
}
