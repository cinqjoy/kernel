#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/proc.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/pagetable.h"

#include "vm/pagefault.h"
#include "vm/vmmap.h"

/*
 * This gets called by _pt_fault_handler in mm/pagetable.c The
 * calling function has already done a lot of error checking for
 * us. In particular it has checked that we are not page faulting
 * while in kernel mode. Make sure you understand why an
 * unexpected page fault in kernel mode is bad in Weenix. You
 * should probably read the _pt_fault_handler function to get a
 * sense of what it is doing.
 *
 * Before you can do anything you need to find the vmarea that
 * contains the address that was faulted on. Make sure to check
 * the permissions on the area to see if the process has
 * permission to do [cause]. If either of these checks does not
 * pass kill the offending process, setting its exit status to
 * EFAULT (normally we would send the SIGSEGV signal, however
 * Weenix does not support signals).
 *
 * Now it is time to find the correct page (don't forget
 * about shadow objects, especially copy-on-write magic!). Make
 * sure that if the user writes to the page it will be handled
 * correctly.
 *
 * Finally call pt_map to have the new mapping placed into the
 * appropriate page table.
 *
 * @param vaddr the address that was accessed to cause the fault
 *
 * @param cause this is the type of operation on the memory
 *              address which caused the fault, possible values
 *              can be found in pagefault.h
 */
void
handle_pagefault(uintptr_t vaddr, uint32_t cause)
{
	uint32_t pdflags=0, ptflags=0;
        /* find the vmarea */
	vmarea_t *vmarea;
	if((vmarea=vmmap_lookup(curproc->p_vmmap,ADDR_TO_PN(vaddr)))==NULL){
		proc_kill(curproc,-EFAULT);
		return;
	}

	/* check the permissions on the area */	
	if(!(cause&FAULT_PRESENT)){
		if(((cause&FAULT_WRITE)&&!(vmarea->vma_prot&PROT_WRITE)) || ((cause&FAULT_RESERVED)&&!(vmarea->vma_prot&PROT_NONE))
		 || ((cause&FAULT_EXEC)&&!(vmarea->vma_prot&PROT_EXEC))){
			proc_kill(curproc,-EFAULT);
			return;
		}	
	}

	/* find the vmarea(remember shadow obj), search for correct page */
	pframe_t *pf;
	if(vmarea->vma_flags&MAP_PRIVATE){
		if(vmarea->vma_obj->mmo_ops->lookuppage(vmarea->vma_obj,ADDR_TO_PN(vaddr)-vmarea->vma_start+vmarea->vma_off,(cause&FAULT_WRITE)==FAULT_WRITE,&pf)<0){
			return;
		}
	}
	else{
		if(vmarea->vma_obj->mmo_ops->lookuppage(vmarea->vma_obj,ADDR_TO_PN(vaddr)-vmarea->vma_start+vmarea->vma_off,(cause&FAULT_WRITE)==FAULT_WRITE,&pf)<0){
		/*if(pframe_get(vmarea->vma_obj,ADDR_TO_PN(vaddr)-vmarea->vma_start+vmarea->vma_off,&pf)<0){*/
			return;
		}
	}
	
	if(cause&FAULT_WRITE){
		pdflags=PD_WRITE;
		ptflags=PT_WRITE;
	}

	/* call pt_map */
	uintptr_t paddr=(uintptr_t)PAGE_ALIGN_DOWN(pt_virt_to_phys((uintptr_t)pf->pf_addr));
	pt_map(curproc->p_pagedir,(uintptr_t)PAGE_ALIGN_DOWN(vaddr),paddr,pdflags|PD_PRESENT|PD_USER,ptflags|PT_PRESENT|PT_USER);
}
