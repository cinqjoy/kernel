#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function, but you may want to special case
 * "." and/or ".." here depnding on your implementation.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{
		KASSERT(NULL != dir);
		KASSERT(NULL != name);
		KASSERT(NULL != result);
        if(dir->vn_ops->lookup == NULL||(!S_ISDIR(dir->vn_mode)))
        	return -ENOTDIR;


        if(strcmp(name,".")==0||len == 0){
        	vref(dir);
        	*result = dir;
        	return 0;
        }
        if (len > NAME_LEN)
		{
			return -ENAMETOOLONG;
		}


        return dir->vn_ops->lookup(dir,name,len,result);
}


/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
		KASSERT(NULL != pathname);
		KASSERT(NULL != namelen);
		KASSERT(NULL != name);
		KASSERT(NULL != res_vnode);

		char *namehead_ptr, *nametail_ptr, *tail_ptr;
		vnode_t *base_dir;
		vnode_t *tmp_vnode;
		int ret, len;

		if(base == NULL) base_dir = curproc->p_cwd;
		else base_dir = base;

		if(strlen(pathname) == 0){
			*name = pathname;
			*namelen = 0;
			*res_vnode = base;
			return 0;
		}

		tail_ptr = (char*)pathname + strlen(pathname);
		namehead_ptr = (char*)pathname;
		if(pathname[0] == '/'){
			namehead_ptr++;
			base_dir = vfs_root_vn;
		}
		nametail_ptr = namehead_ptr;

		vref(base_dir);

		if(tail_ptr > nametail_ptr)
			nametail_ptr++;
		while(*nametail_ptr != '/' && tail_ptr > nametail_ptr)
			nametail_ptr++;

		while(nametail_ptr != tail_ptr){
			len = nametail_ptr - namehead_ptr;
        	if (len > NAME_LEN)
			{
				return -ENAMETOOLONG;
			}
			KASSERT(NULL != base_dir);
			ret = lookup(base_dir, namehead_ptr, len, &tmp_vnode);
			if(ret != 0){
				vput(base_dir);
				return ret;
			}

			if(tail_ptr > nametail_ptr)
				namehead_ptr = ++nametail_ptr;
			while(*nametail_ptr != '/' && tail_ptr > nametail_ptr)
				nametail_ptr++;
			if(nametail_ptr != tail_ptr){
				vput(base_dir);
			}
			base_dir = tmp_vnode;
		}
		if(!S_ISDIR(base_dir->vn_mode)){
			vput(base_dir);
			return -ENOTDIR;
		}
		len = nametail_ptr - namehead_ptr;
		*res_vnode = base_dir;
		*namelen = len;
		*name = (const char*)namehead_ptr;
        return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fnctl.h>.  If the O_CREAT flag is specified, and the file does
 * not exist call create() in the parent directory vnode.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
	size_t namelen;
	const char *name;
	vnode_t *dir, *result;
	int namev_ret, lookup_ret, create_ret;

	if(strlen(pathname) > MAXPATHLEN) return -ENAMETOOLONG;/* maximum size of a pathname=1024 */

	namev_ret = dir_namev(pathname, &namelen, &name, base, &dir);
	if(namev_ret != 0) return namev_ret;

	lookup_ret = lookup(dir, name, namelen, res_vnode);

	if(lookup_ret != 0 && ((flag | (~(uint32_t)O_CREAT)) == 0xffffffff)){
		KASSERT(NULL != dir->vn_ops->create);
		create_ret = dir->vn_ops->create(dir,name,namelen,res_vnode);
		vput(dir);
		return create_ret;
	}
	vput(dir);
	return lookup_ret;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
