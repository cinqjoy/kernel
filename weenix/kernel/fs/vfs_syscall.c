/*
 *  FILE: vfs_syscall.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Wed Apr  8 02:46:19 1998
 *  $Id: vfs_syscall.c,v 1.1 2012/10/10 20:06:46 william Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

#if 1
#define TEST_DBG(s)	\
		({	\
			dbg(DBG_PRINT, (s)); \
		})
#endif



/* To read a file:
 *      o fget(fd)
 *      o call its virtual read f_op
 *      o update f_pos
 *      o fput() it
 *      o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
		TEST_DBG("DO_READ_IN\n");
        file_t *ft;
        int nb;

        if(fd == -1){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
		TEST_DBG("DO_READ_OUT\n");
		return -EBADF;
	}
        if((ft = fget(fd)) == NULL){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
			TEST_DBG("DO_READ_OUT\n");
        	return -EBADF;
	}
        if((ft -> f_mode & FMODE_READ) != FMODE_READ){
        	fput(ft);
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not open for reading.\n", fd);
				TEST_DBG("DO_READ_OUT\n");
                return -EBADF;
		}
        if(S_ISDIR(ft->f_vnode->vn_mode)){
        	fput(ft);
		dbg(DBG_PRINT, "ERROR(fd=%d): fd refers to a directory.\n", fd);
			TEST_DBG("DO_READ_OUT\n");
        	return -EISDIR;
		}
        nb = ft -> f_vnode -> vn_ops -> read(ft -> f_vnode, ft -> f_pos, buf, nbytes);
        ft -> f_pos += nb;
        fput(ft);
		TEST_DBG("DO_READ_OUT\n");
        return nb;
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * f_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
		TEST_DBG("DO_WRITE_IN\n");
        file_t *ft;
        int nb;

        if(fd == -1){ 
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
			TEST_DBG("DO_WRITE_OUT\n");
		return -EBADF;
	}
        if((ft = fget(fd)) == NULL){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
			TEST_DBG("DO_WRITE_OUT\n");
        	return -EBADF;
	}
        if((ft -> f_mode & FMODE_WRITE) != FMODE_WRITE){
                fput(ft);
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not open for writing.\n", fd);
				TEST_DBG("DO_WRITE_OUT\n");
                return -EBADF;
		}
        if((ft -> f_mode & FMODE_APPEND) == FMODE_APPEND){
                ft -> f_pos = do_lseek(fd, 0, SEEK_END);/* err */
		}
        nb = ft -> f_vnode -> vn_ops -> write(ft -> f_vnode, ft -> f_pos, buf, nbytes);

        if(nb>=0){
        	KASSERT((S_ISCHR(ft->f_vnode->vn_mode)) || (S_ISBLK(ft->f_vnode->vn_mode)) || ((S_ISREG(ft->f_vnode->vn_mode)) && (ft->f_pos <= ft->f_vnode->vn_len)));
		dbg(DBG_PRINT, "(GRADING2A 3.a) This is a special file or a regular file. If this is a regular file, its current position must less than the length of file.\n");
	}
        ft -> f_pos += nb;

        fput(ft);
		TEST_DBG("DO_WRTIE_OUT\n");
        return nb;        
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't a valid open file descriptor.
 */
	int
do_close(int fd)
{
	TEST_DBG("DO_CLOSE_IN\n");
	file_t *ft;

	if(fd == -1){ 
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
		TEST_DBG("DO_CLOSE_OUT\n");
		return -EBADF;
	}
	if((ft = fget(fd)) == NULL){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
		TEST_DBG("DO_CLOSE_OUT\n");
		return -EBADF;
	}
	/*vput(ft);*/
	fput(ft);
	fput(ft);
	curproc->p_files[fd]=NULL;
	TEST_DBG("DO_CLOSE_OUT\n");
	return 0;
}

/* To dup a file:
 *      o fget(fd) to up fd's refcount
 *      o get_empty_fd()
 *      o point the new fd to the same file_t* as the given fd
 *      o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't an open file descriptor.
 *      o EMFILE
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{
	TEST_DBG("DO_DUP_IN\n");
	file_t *ft;
	int dupfd;

    	if(fd == -1){ 
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
		TEST_DBG("DO_DUP_OUT\n");
		return -EBADF;
	}
    	if((ft = fget(fd)) == NULL){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not a valid file descriptor.\n", fd);
			TEST_DBG("DO_DUP_OUT\n");
    		return -EBADF;
	}

    	dupfd = get_empty_fd(curproc);
	if(dupfd < 0){
		fput(ft);
		dbg(DBG_PRINT, "ERROR(fd=%d): The process already has the maximum number of file descriptors open and tried to open a new one.\n", fd);
		TEST_DBG("DO_DUP_OUT\n");
		return dupfd;
	}
	curproc -> p_files[dupfd] = curproc -> p_files[fd];
        TEST_DBG("DO_DUP_OUT\n");
    	return dupfd;
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{
	TEST_DBG("DO_DUP2_IN\n");
	file_t *nft;

	if(ofd == -1){
		dbg(DBG_PRINT, "ERROR(ofd=%d): fd is not a valid file descriptor.\n", ofd);
		TEST_DBG("DO_DUP2_OUT\n");
		return -EBADF;
	}
    	if(nfd < 0 || nfd >= NFILES){
		dbg(DBG_PRINT, "ERROR(nfd=%d): nfd is out of the allowed range for file descriptors.\n", nfd);
			TEST_DBG("DO_DUP2_OUT\n");
    		return -EBADF;
	}
	if((nft = fget(ofd)) == NULL){
		dbg(DBG_PRINT, "ERROR(ofd=%d): fd is not a valid file descriptor.\n", ofd);
			TEST_DBG("DO_DUP2_OUT\n");
    		return -EBADF;
	}

    	if(nfd == ofd){
    		fput(nft);
			TEST_DBG("DO_DUP2_OUT\n");
    		return nfd;
    	}

    	if(curproc->p_files[nfd] != NULL)
    		do_close(nfd);
	curproc->p_files[nfd]=curproc->p_files[ofd];
        TEST_DBG("DO_DUP2_OUT\n");
    	return nfd;
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mknod(const char *path, int mode, unsigned devid)
{
	TEST_DBG("DO_MKNOD_IN\n");
	size_t namelen;
	const char *name;
	vnode_t *dir, *result;
	int namev_ret, lookup_ret, ret;

	if((!S_ISCHR(mode) && !S_ISBLK(mode)) || strlen(path) == 0) {
		TEST_DBG("DO_MKNOD_OUT\n");
		return -EINVAL;
	}
	if(strlen(path) > MAXPATHLEN){
		TEST_DBG("DO_MKNOD_OUT\n");
		return -ENAMETOOLONG;/* maximum size of a pathname=1024 */
	}
	namev_ret = dir_namev(path, &namelen, &name, NULL, &dir);
	if(namev_ret == -ENOENT || namev_ret == -ENOTDIR){
		TEST_DBG("DO_MKNOD_OUT\n");
		return namev_ret;
	}
	lookup_ret = lookup(dir, name, namelen, &result);
	if(lookup_ret == -ENOTDIR){
		vput(dir);
		TEST_DBG("DO_MKNOD_OUT\n");
		return lookup_ret;
		}
	if(lookup_ret == 0){
		vput(dir);
		vput(result);
		TEST_DBG("DO_MKNOD_OUT\n");
		return -EEXIST;        
	}
	KASSERT(NULL != dir->vn_ops->mknod);
	dbg(DBG_PRINT, "(GRADING2A 3.b) The parent has mknod().\n");
	ret = dir -> vn_ops -> mknod(dir, name, namelen, mode, devid);
	vput(dir);
	TEST_DBG("DO_MKNOD_OUT\n");
	return ret;
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
	TEST_DBG("DO_MKDIR_IN\n");
	size_t namelen;
	const char *name;
	vnode_t *dir, *result;
	int ret, lookupret;
	int pathlen = strlen(path);

	if(strlen(path) == 0){
		TEST_DBG("DO_MKDIR_OUT\n");
		return -EINVAL;
	}

	if(strlen(path) > MAXPATHLEN){
		TEST_DBG("DO_MKDIR_OUT\n");
		return -ENAMETOOLONG;/* maximum size of a pathname=1024 */
	}
	if((ret = dir_namev(path, &namelen, &name, NULL, &dir)) != 0){ /* last one return the parent of base A.txt */
		TEST_DBG("DO_MKDIR_OUT\n");
		return ret;
	}
	/*
	if (path[pathlen-1] == '.' && path[pathlen-2] == '.')
	{
		vput(dir);
		return 	-ENOTEMPTY;
	}

	if (path[pathlen-1] == '.')
	{
		vput(dir);
		return 	-EINVAL;
	}
	*/
	lookupret=lookup(dir, name, namelen, &result);


	/*-------------------*/
	if (lookupret == -ENOENT)
	{
		KASSERT(NULL != dir->vn_ops->mkdir);
		dbg(DBG_PRINT, "(GRADING2A 3.c) The parent has mkdir().\n");
		ret=dir->vn_ops->mkdir(dir, name, namelen);
		vput(dir);
		TEST_DBG("DO_MKDIR_OUT\n");
		return ret;
	}
	else if (lookupret == 0){  /*exsit*/
		vput(result);
		vput(dir);
		TEST_DBG("DO_MKDIR_OUT\n");
		return -EEXIST;
	}
	else
	{
		vput(dir);
		TEST_DBG("DO_MKDIR_OUT\n");
		return lookupret;
	}

}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
	TEST_DBG("DO_RMDIR_IN\n");
	size_t namelen;
	const char *name;
	vnode_t *dir;
	int ret;
	int pathlen = strlen(path);

	if(strlen(path) == 0){
		TEST_DBG("DO_RMDIR_OUT\n");
		return -EINVAL;
	}

	if(pathlen > MAXPATHLEN){ 
		TEST_DBG("DO_RMDIR_OUT\n");
		return -ENAMETOOLONG;/* maximum size of a pathname=1024 */
	}

	ret  = dir_namev(path, &namelen, &name, NULL, &dir); /* last one return the parent of base A.txt */

	if (ret !=0 ){
		TEST_DBG("DO_RMDIR_OUT\n");
			return ret;
	}
	if (path[pathlen-1] == '.' && path[pathlen-2] == '.')
	{
		vput(dir);
		TEST_DBG("DO_RMDIR_OUT\n");
		return 	-ENOTEMPTY;
	}
	if (path[pathlen-1] == '.')
	{
		vput(dir);
		TEST_DBG("DO_RMDIR_OUT\n");
		return 	-EINVAL;
	}

	KASSERT(NULL != dir->vn_ops->rmdir);
	dbg(DBG_PRINT, "(GRADING2A 3.d) The parent has rmdir().\n");
	/* no need to check if child is directory, done by PFS		*/
	ret=dir->vn_ops->rmdir(dir, name, namelen);

	vput(dir);
	TEST_DBG("DO_RMDIR_OUT\n");
	return ret;
}

/*
 * Same as do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EISDIR
 *        path refers to a directory.
 *      o ENOENT
 *        A component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
	TEST_DBG("DO_UNLINK_IN\n");
	size_t namelen;
	const char *name;
	vnode_t *dir, *result;
	int ret, lookupret;
	int pathlen = strlen(path);

	/*if (path[pathlen-1] == '.' && path[pathlen-2] == '.')
		return 	ENOTEMPTY;

	if (path[pathlen-1] == '.')
		return 	EINVAL;*/

	if(pathlen > MAXPATHLEN){
		TEST_DBG("DO_UNLINK_OUT\n");
		return -ENAMETOOLONG;/* maximum size of a pathname=1024 */
	}
	ret  = dir_namev(path, &namelen, &name, NULL, &dir); /* last one return the parent of base A.txt */

	if (ret !=0 ){
		TEST_DBG("DO_UNLINK_OUT\n");
			return ret;
	}
	/* cannot call rmdir since no dir will return */
	lookupret=lookup(dir, name, namelen, &result);

	if (lookupret !=0 )
	{
			vput(dir);
			TEST_DBG("DO_UNLINK_OUT\n");
			return lookupret;
	}

		if(S_ISDIR(result->vn_mode))
		{
			vput(dir);
			vput(result);
			TEST_DBG("DO_UNLINK_OUT\n");
		return -EISDIR;
	}

	vput(result);
	KASSERT(NULL != dir->vn_ops->unlink);
	dbg(DBG_PRINT, "(GRADING2A 3.e) The parent has unlink().\n");
	/* kassert checking non dir in unlink */
	ret=dir->vn_ops->unlink(dir, name, namelen);

	vput(dir);
	TEST_DBG("DO_UNLINK_OUT\n");
	return ret;
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 */
int
do_link(const char *from, const char *to)
{
	TEST_DBG("DO_LINK_IN\n");
	size_t namelen;
	const char *name;
	vnode_t *fromv, *dir, *result;
	int ret, lookupret;
	int pathlen;


	if(strlen(from) > MAXPATHLEN || strlen(to) > MAXPATHLEN){
		TEST_DBG("DO_LINK_OUT\n");
		return -ENAMETOOLONG;/* maximum size of a pathname=1024 */
	}


    /*diffeernt order from dir_namev */
    /*call open_namev */
	/*ret = open_namev(from, O_CREAT || O_RDWR , &fromv, NULL);*/
	ret = open_namev(from, 0 , &fromv, NULL);


	if (ret !=0  ){
		TEST_DBG("DO_LINK_OUT\n");
			return ret;
	}

	if ( (fromv != NULL) && (S_ISDIR(fromv->vn_mode)) )
	{
		vput(fromv);
		TEST_DBG("DO_LINK_OUT\n");
		return -EISDIR;
	}

	/*assume old path must exists according to linux spec*/

	if ( (fromv != NULL) && (S_ISDIR(fromv->vn_mode)) ) 
	{
			vput(fromv);
			TEST_DBG("DO_LINK_OUT\n");
			return -EISDIR; 
	}

	/*vput(fromv);*/

	ret = dir_namev(to, &namelen, &name, NULL, &dir); /* last one return the parent of base A.txt */

	if (ret !=0 )
	{
			vput(fromv);
			TEST_DBG("DO_LINK_OUT\n");
			return ret;
	}

	lookupret=lookup(dir, name, namelen, &result);/* If dir has no lookup(), return -ENOTDIR. */

	/*--------------------------*/
	if (lookupret == -ENOENT)
	{
		ret=dir->vn_ops->link(fromv, dir, name, namelen);
		vput(fromv);
		vput(dir);
		TEST_DBG("DO_LINK_OUT\n");
		return ret;
	}
	else if (lookupret == 0) /* exist */
	{
		vput(fromv);
		vput(dir);
		vput(result);
		TEST_DBG("DO_LINK_OUT\n");
			return -EEXIST;
	}
	else
	{
		vput(fromv);
		vput(dir);
		TEST_DBG("DO_LINK_OUT\n");
		return lookupret;
	}
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
	int identifier, ret;
	TEST_DBG("DO_RENAME_IN\n");
	ret = do_link(oldname,newname);
	if(ret != 0){
		TEST_DBG("DO_RENAME_OUT\n");
		return ret;
	}
	identifier = do_unlink(oldname);
	TEST_DBG("DO_RENAME_OUT\n");
    	return identifier;
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
	struct vnode *res_vnode,*cur_vnode;
	struct vnode *p_cwd = curproc->p_cwd;
	size_t namelen;
	int lookup_stat, dir_stat;
	const char *name;
	/* res_vnode_ref = n , cur_vnode_ref = k */

	TEST_DBG("DO_CHDIR_IN\n");
	if(strlen(path) == 0){ 
		dbg(DBG_PRINT, "ERROR: Path is not valid.\n");
		TEST_DBG("DO_CHDIR_OUT\n");
		return -EINVAL;
	}
	if(strlen(path) > MAXPATHLEN){
		dbg(DBG_PRINT, "ERROR(path=%s): Path is too long.\n", path);
		TEST_DBG("DO_CHDIR_OUT\n");
		return -ENAMETOOLONG;
	}

	if((dir_stat=dir_namev(path, &namelen , &name, NULL , &res_vnode))){
	TEST_DBG("DO_CHDIR_OUT\n");
		return dir_stat;
	} 
 	
	if((lookup_stat=lookup(res_vnode,name,namelen,&cur_vnode))){
		vput(res_vnode);
		TEST_DBG("DO_CHDIR_OUT\n");
		return lookup_stat;
	}
	
	vput(res_vnode);
	
	if(S_ISDIR(cur_vnode->vn_mode)){
		vput(curproc->p_cwd);
		curproc->p_cwd = cur_vnode;
	}else{
		vput(cur_vnode);
		dbg(DBG_PRINT, "ERROR(path=%s):A component of path is not a directory.\n", path);
		TEST_DBG("DO_CHDIR_OUT\n");
		return -ENOTDIR;
	}
	TEST_DBG("DO_CHDIR_OUT\n");
	return 0;
}

/* Call the readdir f_op on the given fd, filling in the given dirent_t*.
 * If the readdir f_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
int
do_getdent(int fd, struct dirent *dirp)
{
	TEST_DBG("DO_GETDENT_IN\n");
	file_t *ft;
	int offset;

    	if(fd == -1){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not an open file descriptor.\n", fd);
		TEST_DBG("DO_GETDENT_OUT\n");
		return -EBADF;
	}
    	if((ft = fget(fd)) == NULL){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not an open file descriptor.\n", fd);
    		TEST_DBG("DO_GETDENT_OUT\n");
			return -EBADF;
	}

	if(!S_ISDIR(ft->f_vnode->vn_mode)){
		dbg(DBG_PRINT, "ERROR(fd=%d): File descriptor does not refer to a directory.\n", fd);
		fput(ft);
		TEST_DBG("DO_GETDENT_OUT\n");
		return -ENOTDIR;
	}
	
	vref(ft->f_vnode);
	KASSERT(NULL != ft->f_vnode->vn_ops->readdir);
	dbg(DBG_PRINT, "The vnode has readdir()\n");
	offset = ft->f_vnode->vn_ops->readdir(ft->f_vnode,ft->f_pos,dirp);
	vput(ft->f_vnode);

	if(offset==0){
		fput(ft);
		TEST_DBG("DO_GETDENT_OUT\n");
		return 0;
	}else if(offset > 0){
		ft->f_pos += offset;
		fput(ft);
		TEST_DBG("DO_GETDENT_OUT\n");
		return sizeof(dirent_t);
	}
	/*Should not get here*/
	TEST_DBG("DO_GETDENT_OUT\n");
	return -EINVAL;
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
	file_t *ft;
	off_t tmp_pos = -1;
	TEST_DBG("DO_LSEEK_IN\n");
	if(fd == -1){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not an open file descriptor.\n", fd);
		TEST_DBG("DO_LSEEK_OUT\n");
		return -EBADF;
	}
	if((ft = fget(fd)) == NULL){
		dbg(DBG_PRINT, "ERROR(fd=%d): fd is not an open file descriptor.\n", fd);
		TEST_DBG("DO_LSEEK_OUT\n");
		return -EBADF;
	}

	switch(whence){
		case SEEK_SET:
			tmp_pos = offset;
			break;
		case SEEK_CUR:
			tmp_pos = ft -> f_pos + offset;
			break;
		case SEEK_END:
			tmp_pos = ft->f_vnode->vn_len + offset;
			break;
		default:
			dbg(DBG_PRINT, "ERROR(fd=%d): whence is not valid.\n", fd);
			fput(ft);
			TEST_DBG("DO_LSEEK_OUT\n");
			return -EINVAL;
			break;
	}
	if(tmp_pos < 0){
		dbg(DBG_PRINT, "ERROR(fd=%d): The resulting file offset is negative.\n", fd);		
		fput(ft);
		TEST_DBG("DO_LSEEK_OUT\n");
		return -EINVAL;
	}

	ft -> f_pos = tmp_pos;
	dbg(DBG_PRINT, "The fpos of fd=%d is moved to %d\n", fd, ft->f_pos);
	fput(ft);
	TEST_DBG("DO_LSEEK_OUT\n");
	return tmp_pos;
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_stat(const char *path, struct stat *buf)
{
	size_t namelen;
	const char *name;
	vnode_t *dir, *result;
	int namev_ret, lookup_ret, ret;
	TEST_DBG("DO_STAT_IN\n");
	if(strlen(path) == 0){ 
		dbg(DBG_PRINT, "ERROR: Path is not valid.\n");
		TEST_DBG("DO_STAT_OUT\n");
		return -EINVAL;
	}
	if(strlen(path) > MAXPATHLEN){ 
		dbg(DBG_PRINT, "ERROR(path=%s): Path is too long.\n", path);
		TEST_DBG("DO_STAT_OUT\n");
		return -ENAMETOOLONG;/* maximum size of a pathname=1024 */
	}

	namev_ret = dir_namev(path, &namelen, &name, NULL, &dir);
	if(namev_ret != 0){
		TEST_DBG("DO_STAT_OUT\n");
		return namev_ret;
	}
	lookup_ret = lookup(dir, name, namelen, &result);
	vput(dir);
	if(lookup_ret != 0){
		TEST_DBG("DO_STAT_OUT\n");
		return lookup_ret;
	}
	KASSERT(result->vn_ops->stat);
	dbg(DBG_PRINT, "(GRADING2A 3.f) The vnode has stat().\n");
	ret = result->vn_ops->stat(result, buf);
	vput(result);

	TEST_DBG("DO_STAT_OUT\n");
	return ret;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
