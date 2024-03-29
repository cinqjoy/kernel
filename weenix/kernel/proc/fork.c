#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{
    	KASSERT(regs != NULL);
    	dbg(DBG_PRINT, "(GRADING3A 7.a) the regs is not null\n ");
	KASSERT(curproc != NULL);
	dbg(DBG_PRINT, "(GRADING3A 7.a) current process is not null\n ");
    	KASSERT(curproc->p_state == PROC_RUNNING);
	dbg(DBG_PRINT, "(GRADING3A 7.a) the state of current process is runnung\n ");

    	int i;
	/*int (*fp3)(struct regs*) = userland_entry;*/
        proc_t *child_proc=proc_create("child_process");
        KASSERT(child_proc->p_pagedir != NULL);
	dbg(DBG_PRINT, "(GRADING3A 7.a) the page directory of child process is not null\n ");
	if(!child_proc->p_vmmap) vmmap_destroy(child_proc->p_vmmap);
        child_proc->p_vmmap=vmmap_clone(curproc->p_vmmap);

        list_link_t *p_link,*c_link;
        vmarea_t *p_vma,*c_vma;
        for (p_link=curproc->p_vmmap->vmm_list.l_next,c_link=child_proc->p_vmmap->vmm_list.l_next;
             p_link!=&curproc->p_vmmap->vmm_list && c_link!=&child_proc->p_vmmap->vmm_list;
             p_link=p_link->l_next,c_link=c_link->l_next){
                p_vma=list_item(p_link,vmarea_t,vma_plink);
                c_vma=list_item(c_link,vmarea_t,vma_plink);
                if(p_vma->vma_flags&MAP_SHARED){
                	c_vma->vma_obj=p_vma->vma_obj;
                	list_insert_tail(&c_vma->vma_obj->mmo_un.mmo_vmas,&c_vma->vma_olink);
                	c_vma->vma_obj->mmo_ops->ref(c_vma->vma_obj);
                }
                else{
                	mmobj_t *p_shadow,*c_shadow;
        		p_shadow = shadow_create();c_shadow = shadow_create();
    			p_shadow->mmo_shadowed = p_vma->vma_obj;c_shadow->mmo_shadowed = p_vma->vma_obj;
    			p_vma->vma_obj->mmo_ops->ref(p_vma->vma_obj);
			p_vma->vma_obj->mmo_ops->ref(p_vma->vma_obj);
    			p_shadow->mmo_un.mmo_bottom_obj = p_vma->vma_obj->mmo_un.mmo_bottom_obj;
    			c_shadow->mmo_un.mmo_bottom_obj = p_vma->vma_obj->mmo_un.mmo_bottom_obj;
    			p_shadow->mmo_un.mmo_bottom_obj->mmo_ops->ref(p_shadow->mmo_un.mmo_bottom_obj);
			c_shadow->mmo_un.mmo_bottom_obj->mmo_ops->ref(c_shadow->mmo_un.mmo_bottom_obj);
			/*p_vma->vma_obj->mmo_ops->dirtypage(p_vma->vma_obj,p_vma->vma_obj->mmo_un.mmo_bottom_obj->mmo_respages);*/
    			p_vma->vma_obj = p_shadow;
    			c_vma->vma_obj = c_shadow;
			/*p_shadow->mmo_ops->ref(p_shadow);
			c_shadow->mmo_ops->ref(c_shadow);*/
			list_insert_tail(&c_vma->vma_obj->mmo_un.mmo_bottom_obj->mmo_un.mmo_vmas,&c_vma->vma_olink);
                }
		/*
		if(p_vma->vma_prot&PROT_WRITE){
			if(p_vma->vma_start==p_vma->vma_end) pt_unmap(curproc->p_pagedir,(uintptr_t)PN_TO_ADDR(p_vma->vma_start));
			else pt_unmap_range(curproc->p_pagedir,(uintptr_t)PN_TO_ADDR(p_vma->vma_start),(uintptr_t)PN_TO_ADDR(p_vma->vma_end));
		}
		*/
        }

        	/*list_init(&child_proc->p_child_link);
	list_insert_tail(&curproc->p_children,&child_proc->p_child_link);*/
        
		
	kthread_t *child_thread=kthread_clone(curthr);
        KASSERT(child_thread->kt_kstack != NULL);
	dbg(DBG_PRINT, "(GRADING3A 7.a) the stack of the thread of child process is not null \n ");

        child_thread->kt_ctx.c_pdptr= child_proc->p_pagedir;
        child_thread->kt_ctx.c_kstacksz=curthr->kt_ctx.c_kstacksz;
	regs->r_eax=0;
        child_thread->kt_ctx.c_esp=fork_setup_stack(regs,child_thread->kt_kstack);    
        child_thread->kt_ctx.c_eip=(uint32_t)userland_entry;
        child_thread->kt_proc=child_proc;
		
        for(i=0;i<NFILES;i++){/* copy the file table */
        	child_proc->p_files[i]=curproc->p_files[i];
        	if(curproc->p_files[i]) fref(curproc->p_files[i]);
        }

	child_proc->p_brk=curproc->p_brk;
	child_proc->p_start_brk=curproc->p_start_brk;
        child_proc->p_status=curproc->p_status;
        child_proc->p_state=curproc->p_state;
	KASSERT(child_proc->p_state == PROC_RUNNING);
	dbg(DBG_PRINT, "(GRADING3A 7.a) the child process's state is running \n ");
        child_proc->p_cwd=curproc->p_cwd;/* set the child's working directory*/
        vref(curproc->p_cwd);

        sched_make_runnable(child_thread);

        pt_unmap_range(curproc->p_pagedir,USER_MEM_LOW, USER_MEM_HIGH);
		pt_unmap_range(child_proc->p_pagedir,USER_MEM_LOW, USER_MEM_HIGH);
		tlb_flush_all();
        return child_proc->p_pid;
}
