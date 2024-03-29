#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"
#include "mm/pagetable.h"

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
	file_t *ft;
	vnode_t *vn;
	int i;


	/*
	 * EINVAL flags contained neither MAP_PRIVATE or MAP_SHARED, or
	 * 	      contained both of these values.
	 */
	if(((flags & MAP_PRIVATE)!= MAP_PRIVATE &&
			(flags & MAP_SHARED)!= MAP_SHARED) ||
			(flags & (MAP_SHARED|MAP_PRIVATE)) == (MAP_SHARED|MAP_PRIVATE) )
		return -EINVAL;

	/*
	 * ENFILE The system limit on the total number of open files
	 *        has been reached.
	 */
    for (i = 0; i < NFILES; i++){
    	if (!curproc->p_files[i]){
			break;
		}
	}
    if(i == NFILES) return -ENFILE;


	if(fd == -1){
		vn = NULL;
	}else if((ft = fget(fd)) == NULL){
		vn = NULL;
	}else{
		vn = ft->f_vnode;
	}

	if(vn == NULL){
		/* EBADF  fd is not a valid file descriptor (and MAP_ANONYMOUS was not set). */
		if(!((flags & MAP_ANON) == MAP_ANON))
			return -EBADF;
	}
	/*
	 * EACCES A file descriptor refers to a non-regular file.  Or
	 *        MAP_PRIVATE was requested, but fd is not open for reading.  Or
	 *        MAP_SHARED was requested and PROT_WRITE is set, but fd is not
	 *        open in read/write (O_RDWR) mode.  Or PROT_WRITE is set, but
	 *        the file is append-only.
	 */
	else if(/*!((vn->vn_mode & 0xFF00) == 0x0800) ||*/
			((flags & MAP_PRIVATE) == MAP_PRIVATE && (ft -> f_mode & FMODE_READ) != FMODE_READ) ||
			(((flags & MAP_SHARED) == MAP_SHARED) && ((prot & PROT_WRITE) == PROT_WRITE) &&
					!((ft->f_mode & (FMODE_READ|FMODE_WRITE)) == (FMODE_READ|FMODE_WRITE))) ||
			(((prot & PROT_WRITE) == PROT_WRITE) && ((ft->f_mode & FMODE_APPEND) == FMODE_APPEND)))
		return -EACCES;
	/*
	 * EINVAL We don't like addr, length, or offset (e.g., they are too
	 *        large, or not aligned on a page boundary).
	 */
	if(!PAGE_ALIGNED(off) || !PAGE_ALIGNED(addr) ||
			len > (USER_MEM_HIGH-USER_MEM_LOW) || len <= 0 ||
			addr >= (void*)USER_MEM_HIGH || (addr < (void*)USER_MEM_LOW && addr!=(void*)0)) return -EINVAL;

	/*
	 * ENODEV The underlying filesystem of the specified file does not
	 *        support memory mapping.
	 */

	/*
	 * EPERM  The prot argument asks for PROT_EXEC but the mapped area
	 *        belongs to a file on a filesystem that was mounted no-exec.
	 */

	/*
	 * ENOMEM No memory is available, or the process's maximum number of
	 *        mappings would have been exceeded.
	 * This error should be returned by vmmap
	 */


	uint32_t npages = len/PAGE_SIZE + ((uint32_t)(len%PAGE_SIZE == 0)?0:1);
	uint32_t lopage = ADDR_TO_PN(addr);
	vmarea_t *vma;
	int vmp_ret;

	vmp_ret = vmmap_map(curproc->p_vmmap, vn, lopage, npages, prot, flags, off, VMMAP_DIR_LOHI, &vma);
	if(vmp_ret < 0)
		return vmp_ret;
	*ret = PN_TO_ADDR(vma->vma_start);
	KASSERT(NULL != curproc->p_pagedir);
	dbg(DBG_PRINT, "(GRADING3A 2.a) the page directory of current process is no NULL.\n");
	pt_unmap_range(curproc->p_pagedir, (uintptr_t)*ret, (uintptr_t)*ret+npages*PAGE_SIZE);
	tlb_flush_range((uintptr_t)*ret, npages);
	/*tlb_flush_all();*/
	return 0;
}


/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
	/* valid address? Maximum length is 1024? */

	if(len <= 0 || len > (USER_MEM_HIGH-USER_MEM_LOW)
	|| addr >= (void*)USER_MEM_HIGH || (addr < (void*)USER_MEM_LOW && addr!=(void*)0)) 
		return -EINVAL;
		
	uint32_t npages = len/PAGE_SIZE + ((uint32_t)(len%PAGE_SIZE == 0)?0:1);
	uint32_t lopage = ADDR_TO_PN(addr);

	int vmp_ret = vmmap_remove(curproc->p_vmmap, lopage, npages);
	if(vmp_ret < 0) return vmp_ret;
	KASSERT(NULL != curproc->p_pagedir);
	dbg(DBG_PRINT, "(GRADING3A 2.b) the page directory of current process is no NULL.\n");
	pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), (uintptr_t)(PN_TO_ADDR(lopage+npages)));
	tlb_flush_range((uintptr_t)addr,npages);
	return 0;

}

