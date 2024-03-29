#include "kernel.h"
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void
vmmap_init(void)
{
        vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
        KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
        vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
        KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void)
{
        vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
        if (newvma) {
                newvma->vma_vmmap = NULL;
        }
        return newvma;
}

void
vmarea_free(vmarea_t *vma)
{
        KASSERT(NULL != vma);
        if(list_link_is_linked(&vma->vma_olink))
        	list_remove(&vma->vma_olink);
        if(list_link_is_linked(&vma->vma_plink))
        	list_remove(&vma->vma_plink);
		if(vma->vma_obj != NULL)
			vma->vma_obj->mmo_ops->put(vma->vma_obj);
        slab_obj_free(vmarea_allocator, vma);
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */
vmmap_t *
vmmap_create(void)
{
	vmmap_t * vmmp = (vmmap_t *) slab_obj_alloc(vmmap_allocator);
	if(vmmp){
		list_init(&vmmp->vmm_list);
		vmmp->vmm_proc = NULL;
	}
	return vmmp;
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void
vmmap_destroy(vmmap_t *map)
{
	KASSERT(NULL != map);
	dbg(DBG_PRINT, "(GRADING3A 3.a) map is not null.\n");
	vmarea_t * vma;
	while(!list_empty(&map->vmm_list)){
		vma = list_head(&map->vmm_list,vmarea_t,vma_plink);
		vmarea_free(vma);
	}
    slab_obj_free(vmmap_allocator, map);
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void
vmmap_insert(vmmap_t *map, vmarea_t *newvma)
{
	/* assert vmarea is valid */
	/* newvma->vma_obj cannot be NULL */
	KASSERT(NULL != map && NULL != newvma);
	dbg(DBG_PRINT, "(GRADING3A 3.b) Both of map and newvma are not NULL.\n");
	KASSERT(NULL == newvma->vma_vmmap);
	dbg(DBG_PRINT, "(GRADING3A 3.b) newvma->vma_vmmap is NULL.\n");
	KASSERT(newvma->vma_start < newvma->vma_end);
	dbg(DBG_PRINT, "(GRADING3A 3.b) The end addr of vma is greater than the start addr of vma.\n");
	KASSERT(ADDR_TO_PN(USER_MEM_LOW) <= newvma->vma_start && ADDR_TO_PN(USER_MEM_HIGH) >= newvma->vma_end);
	dbg(DBG_PRINT, "(GRADING3A 3.b) The range of the newvma is inside the range of user memmory.\n");

	vmarea_t * vma;
	newvma->vma_vmmap = map;
	list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
		if(vma->vma_start > newvma->vma_start){
			list_insert_before(&vma->vma_plink,&newvma->vma_plink);
			return;
		}
	}list_iterate_end();
	list_insert_tail(&map->vmm_list, &newvma->vma_plink);

}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int
vmmap_find_range(vmmap_t *map, uint32_t npages, int dir)
{
	vmarea_t *vma;
	/* the maximum entry of the pagetable, page number */
	KASSERT(NULL != map);
	dbg(DBG_PRINT, "(GRADING3A 3.c) map is not null.\n");
	KASSERT(0 < npages);
	dbg(DBG_PRINT, "(GRADING3A 3.c) number of pages is greater than 0.\n");
	uint32_t hi = ADDR_TO_PN(USER_MEM_HIGH);
	uint32_t lo = ADDR_TO_PN(USER_MEM_LOW);

	switch(dir){
		case VMMAP_DIR_HILO:
			list_iterate_reverse(&map->vmm_list,vma,vmarea_t,vma_plink){
				lo = vma->vma_end;
				if((hi-lo) >= npages)
					return lo;
				hi = vma->vma_start;
			}list_iterate_end();
			lo = ADDR_TO_PN(USER_MEM_LOW);
			if((hi-lo) >= npages)
				return lo;
			break;
		case VMMAP_DIR_LOHI:
			list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
				hi = vma->vma_start;
				if((hi-lo) >= npages)
					return lo;
				lo = vma->vma_end;
			}list_iterate_end();
			hi = ADDR_TO_PN(USER_MEM_HIGH);
			if((hi-lo) >= npages)
				return lo;
			break;
		default:
			return -1;
	}
	return -1;
}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn)
{
	KASSERT(NULL != map);
	dbg(DBG_PRINT, "(GRADING3A 3.d) map is not null.\n");
	vmarea_t * vma;
	list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
		if(vma->vma_start <= vfn && vfn < vma->vma_end)
			return vma;
	}list_iterate_end();
	return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map)
{
		vmmap_t * new_vmmap = NULL; 
		vmarea_t * new_vmarea = NULL;
		vmarea_t *vma = NULL;
							
		new_vmmap = vmmap_create();
										
		if (!list_empty(&map->vmm_list))
		{

			list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
				new_vmarea = vmarea_alloc();
				new_vmarea->vma_start = vma->vma_start;
				new_vmarea->vma_end = vma->vma_end;
				new_vmarea->vma_prot = vma->vma_prot;
				new_vmarea->vma_flags = vma->vma_flags;
				new_vmarea->vma_off = vma->vma_off;
				/* plink , vma_vmmap initialize */
				vmmap_insert(new_vmmap, new_vmarea);
				new_vmarea->vma_obj = NULL;
				/* no need olink since obj is non-decided */
			}list_iterate_end();
		}																							
		if (new_vmmap != NULL)
			return new_vmmap;
		else
			return NULL;
}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.  Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int
vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
          int prot, int flags, off_t off, int dir, vmarea_t **new)
{
			KASSERT(NULL != map);
			dbg(DBG_PRINT, "(GRADING3A 3.f) map is not null.\n");
			KASSERT(0 < npages);
			dbg(DBG_PRINT, "(GRADING3A 3.f) number of pages is greater than 0.\n");
			KASSERT(!(~(PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC) & prot));
			dbg(DBG_PRINT, "(GRADING3A 3.f) prot belongs to one of four status.\n");
			KASSERT((MAP_SHARED & flags) || (MAP_PRIVATE & flags));
			dbg(DBG_PRINT, "(GRADING3A 3.f) the status of flags is shared or private.\n");	
			KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_LOW) <= lopage));
			dbg(DBG_PRINT, "(GRADING3A 3.f) lower bound page equals to 0 or it's greater than the lower bound of user memory\n");
			KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_HIGH) >= (lopage + npages)));
			dbg(DBG_PRINT, "(GRADING3A 3.f) lower bound page equals to 0 or the pages allocated is not greater than the upper bound of user memory.\n");	
			KASSERT(PAGE_ALIGNED(off));
			dbg(DBG_PRINT, "(GRADING3A 3.f) the offset is page aligned.\n");	
			
			int vfn;
			mmobj_t * tmp_obj = NULL;
			mmobj_t * shadow_obj = NULL;
			proc_t * p = map->vmm_proc;
			vmarea_t * new_vmarea = NULL;
	
			
			if (lopage == 0)
			{
				if ((vfn = vmmap_find_range(map, npages, dir)) == -1)
				{
					/*?? return  error message if no any virtual address available--------------------*/
					return -ENOMEM;
				}
				
			}
			else
			{
				if (!vmmap_is_range_empty(map, lopage, npages))
					vmmap_remove(map, lopage, npages);			
				vfn = lopage;
			}
	
			/* assgin content to vmarea */
			new_vmarea = vmarea_alloc();
			new_vmarea->vma_start = vfn;
			new_vmarea->vma_end = vfn+npages;
			new_vmarea->vma_prot = prot;
			new_vmarea->vma_flags = flags;
			new_vmarea->vma_off = off;
	
			/* assume anaomous objects ,each vmarea needs to have one */
			

			
			if (file==NULL)
			  	tmp_obj = anon_create();
			else
				file->vn_ops->mmap(file, new_vmarea, &tmp_obj);

			if ((flags & 0x00000002) ==MAP_PRIVATE)
		 	{
			  		shadow_obj = shadow_create();
					shadow_obj->mmo_shadowed = tmp_obj;
					if(file!=NULL) tmp_obj->mmo_ops->ref(tmp_obj);

					shadow_obj->mmo_un.mmo_bottom_obj = tmp_obj;
					tmp_obj->mmo_ops->ref(tmp_obj);

					new_vmarea->vma_obj = shadow_obj;
					/*shadow_obj->mmo_ops->ref(shadow_obj);*/
			 }
			 else
			 { 
			  		new_vmarea->vma_obj = tmp_obj;
					if(file!=NULL) tmp_obj->mmo_ops->ref(tmp_obj);
			 }
	
			/*list_init(&(*new)->vma_olink);
			list_init(&(*new)->vma_plink);
			list_t			  mmo_vmas;*/
			/* assign plink  */


			tmp_obj->mmo_shadowed = NULL;
			
			list_init(&tmp_obj->mmo_un.mmo_vmas);
			list_insert_tail(&tmp_obj->mmo_un.mmo_vmas, &new_vmarea->vma_olink);

			vmmap_insert(map, new_vmarea);


			if (new != NULL)
				*new = new_vmarea;

			if (file != NULL)
				tmp_obj->mmo_ops->put(tmp_obj);


			return 0;	

}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int
vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages)
{
	vmarea_t *vma;
	uint32_t lo = lopage;
	uint32_t hi = lopage+npages;
	uint32_t tmp;
	list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
		if(lo > hi) return 0;
		if((lo <= vma->vma_start) && ( vma->vma_start < hi)
				&& (hi < vma->vma_end)){
			/*case 3*/
			vma->vma_off += hi-vma->vma_start;
			vma->vma_start = hi;
			return 0;

		}else if((lo <= vma->vma_start) &&
				(hi >= vma->vma_end)){
			/*case 4*/
			lo = vma->vma_end;
			vmarea_free(vma);

		}else if((lo > vma->vma_start) &&
				(hi < vma->vma_end)){
			/*case 1(split)*/
			vmarea_t * newvma = vmarea_alloc();
			newvma->vma_off = vma->vma_off + (hi-vma->vma_start);
			newvma->vma_start = hi;
			newvma->vma_end = vma->vma_end;
			vma->vma_end = lo;

			vma->vma_obj->mmo_ops->ref(vma->vma_obj);
			newvma->vma_obj = vma->vma_obj;

			vmmap_insert(map,newvma);
			return 0;

		}else if((vma->vma_start < lo) && (lo < vma->vma_end)
				&& (vma->vma_end <= hi)){
			/*case 2*/
			tmp = vma->vma_end;
			vma->vma_end = lo;
			lo = tmp;

		}else{
			/*no match*/
		}
	}list_iterate_end();
	return 0;
}

/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int
vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages)
{
	/*
	 * The pages in designated range should all be mapped or
	 * only if one page had been mapped, we should return 0.
	 */

	/* assert npages != 0 */
	uint32_t endvfn = startvfn+npages;
	KASSERT((startvfn < endvfn) && (ADDR_TO_PN(USER_MEM_LOW) <= startvfn) && (ADDR_TO_PN(USER_MEM_HIGH) >= endvfn));
	dbg(DBG_PRINT, "(GRADING3A 3.e) end frame is greater than the start frame and the frames are inside user memory.\n");
	vmarea_t *vma;
	uint32_t hi = startvfn+npages;
	uint32_t lo = startvfn;

	list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
		if(((vma->vma_start <= lo) && (lo < vma->vma_end)) ||   /* lo is inside a vmarea */
				((vma->vma_start < hi) && (hi <= vma->vma_end)) || /* hi is inside a vmarea */
				((lo <= vma->vma_start) && (vma->vma_end <= hi))){ /* vmarea is in side the range we specified */
			return 0;
		}
	}list_iterate_end();
	return 1;
}

/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
int
vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{

		size_t remainsize = count;
		vmarea_t *vma;
		struct pframe *pf;
		/* ------assume vaddr is 32 bits not ust 20 bits  -------*/
		uintptr_t vma_saddr= (uintptr_t) vaddr;
		uint32_t boffset = 0;
		

		
		list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
			
			uintptr_t vfs= ADDR_TO_PN(((uintptr_t)vma_saddr));
			if((vma->vma_start <= vfs) && (vfs < vma->vma_end)  && (remainsize!=0) )
			{
				size_t size;
				/*uint32_t v_pdindex;
				uint32_t v_ptindex;
				uint32_t pagenum;
				uint32_t *pt_vaddr;				
				uint32_t ppage_paddr;*/								
				uint32_t poffset;
				uint32_t pagenum;

				pagenum =  (((uint32_t)(vma_saddr)) >> PAGE_SHIFT) - vma->vma_start + vma->vma_off;
				poffset =  (((uint32_t)(vma_saddr)) & 0x00000FFF);

				
				if (remainsize <=PAGE_SIZE *(vma->vma_end - vma->vma_start) )/* located in one VMarea */
				{
					size = remainsize;
					remainsize -= size;

				}
				else /* need to find another VMarea , preparing next vma_saddr which starting from current VMarea puls one*/
				{
					size = PAGE_SIZE *(vma->vma_end - vma->vma_start);
					vfs = vma->vma_end;
					vma_saddr = (uintptr_t) PN_TO_ADDR(vfs);
					remainsize -= size;
				}

				/*v_pdindex = (((*(uint32_t *)(vaddr)) >> PAGE_SHIFT) / (PAGE_SIZE / sizeof (uint32_t)));
				v_ptindex = (((*(uint32_t *)(vaddr)) >> PAGE_SHIFT) % (PAGE_SIZE / sizeof (uint32_t)));


				pd = map->vmm_proc->p_pagedir;
				pt_vaddr = pd->pd_virtual[v_pdindex];
				ppage_paddr =pt_vaddr[v_ptindex];*/
				

				
				/*--forwrite =1, performing read from page--*/	
				if (!vma->vma_obj->mmo_ops->lookuppage(vma->vma_obj, pagenum, 0, &pf)) /* success */
				{


						memcpy(  (void*)(((uint32_t)buf)+boffset), (void*)(((uint32_t)pf->pf_addr) | poffset), size);

						boffset+=size;
						
						/*return v->vn_ops->fillpage(v, (int)PN_TO_ADDR(pf->pf_pagenum), pf->pf_addr);*/
						/*return v->vn_ops->fillpage(v, (int)PN_TO_ADDR(pf->pf_pagenum), pf->pf_addr);*/					
						/*map->vmm_proc->p_pagedir->pd_virtual[pdindex] 
						ppaddr = uintptr_t pt_virt_to_phys((uintptr_t) (*pf->pf_addr));*/

						if (0 == remainsize) /* read finish*/
							return 0;
					
				}
				else
					return -EFAULT;
			
			}
			
		}list_iterate_end();


		return 0;


}

/* Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do this, you will need to find the correct
 * vmareas to write into, then find the correct pframes within those vmareas,
 * and finally write into the physical addresses that those pframes correspond
 * to. You should not check permissions of the areas you use. Assume (KASSERT)
 * that all the areas you are accessing exist. Remember to dirty pages!
 * Returns 0 on success, -errno on error.
 */
int
vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{
	size_t remainsize = count;
	vmarea_t *vma;
	struct pframe *pf;
	/* ------assume vaddr is 32 bits not ust 20 bits  -------*/
	uintptr_t vma_saddr= (uintptr_t) vaddr;
	uint32_t boffset = 0;
	
	
	
	list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink){
		
		uintptr_t vfs= ADDR_TO_PN(((uintptr_t)vma_saddr));
		if((vma->vma_start <= vfs) && (vfs < vma->vma_end)  && (remainsize!=0) )
		{
			size_t size;
			/*uint32_t v_pdindex;
			uint32_t v_ptindex;
			uint32_t pagenum;
			uint32_t *pt_vaddr; 			
			uint32_t ppage_paddr;*/ 							
			uint32_t poffset;
			uint32_t pagenum;
	
			pagenum =  (((uint32_t)(vma_saddr)) >> PAGE_SHIFT) - vma->vma_start + vma->vma_off;
			poffset =  (((uint32_t)(vma_saddr)) & 0x00000FFF);
	
			
			if (remainsize <=PAGE_SIZE *(vma->vma_end - vma->vma_start) )/* located in one VMarea */
			{
				size = remainsize;
				remainsize -= size;
	
			}
			else /* need to find another VMarea , preparing next vma_saddr which starting from current VMarea puls one*/
			{
				size = PAGE_SIZE *(vma->vma_end - vma->vma_start);
				vfs = vma->vma_end;
				vma_saddr = (uintptr_t) PN_TO_ADDR(vfs);
				remainsize -= size;
			}
	
			/*v_pdindex = (((*(uint32_t *)(vaddr)) >> PAGE_SHIFT) / (PAGE_SIZE / sizeof (uint32_t)));
			v_ptindex = (((*(uint32_t *)(vaddr)) >> PAGE_SHIFT) % (PAGE_SIZE / sizeof (uint32_t)));
	
	
			pd = map->vmm_proc->p_pagedir;
			pt_vaddr = pd->pd_virtual[v_pdindex];
			ppage_paddr =pt_vaddr[v_ptindex];*/
			
	
			
			/*--forwrite =1, performing write to page--*/	
			if (!vma->vma_obj->mmo_ops->lookuppage(vma->vma_obj, pagenum, 1, &pf)) /* success */
			{
	
	
					memcpy(  (void *) (((uint32_t)pf->pf_addr)| poffset),(void*)(((uint32_t)buf)+boffset), size);
					boffset+=size;
					vma->vma_obj->mmo_ops->dirtypage(vma->vma_obj,pf);
					
					/*return v->vn_ops->fillpage(v, (int)PN_TO_ADDR(pf->pf_pagenum), pf->pf_addr);*/
					/*return v->vn_ops->fillpage(v, (int)PN_TO_ADDR(pf->pf_pagenum), pf->pf_addr);*/					
					/*map->vmm_proc->p_pagedir->pd_virtual[pdindex] 
					ppaddr = uintptr_t pt_virt_to_phys((uintptr_t) (*pf->pf_addr));*/
	
					if (0 == remainsize) /* read finish*/
						return 0;
				
			}
			else
				return -EFAULT;
		
		}
		
	}list_iterate_end();
	
	return 0;
}

/* a debugging routine: dumps the mappings of the given address space. */
size_t
vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
        KASSERT(0 < osize);
        KASSERT(NULL != buf);
        KASSERT(NULL != vmmap);

        vmmap_t *map = (vmmap_t *)vmmap;
        vmarea_t *vma;
        ssize_t size = (ssize_t)osize;

        int len = snprintf(buf, size, "curproc:%d(%s)\n%21s %5s %7s %8s %10s %12s\n",
							curproc->p_pid,curproc->p_comm,
                           "VADDR RANGE", "PROT", "FLAGS", "MMOBJ", "OFFSET",
                           "VFN RANGE");

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                size -= len;
                buf += len;
                if (0 >= size) {
                        goto end;
                }

                len = snprintf(buf, size,
                               "%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
                               vma->vma_start << PAGE_SHIFT,
                               vma->vma_end << PAGE_SHIFT,
                               (vma->vma_prot & PROT_READ ? 'r' : '-'),
                               (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                               (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                               (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                               vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
        } list_iterate_end();

end:
        if (size <= 0) {
                size = osize;
                buf[osize - 1] = '\0';
        }
        /*
        KASSERT(0 <= size);
        if (0 == size) {
                size++;
                buf--;
                buf[0] = '\0';
        }
        */
        return osize - size;
}
