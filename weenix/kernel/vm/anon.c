#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/debug.h"

#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/tlb.h"

int anon_count = 0; /* for debugging/verification purposes */

static slab_allocator_t *anon_allocator;

static void anon_ref(mmobj_t *o);
static void anon_put(mmobj_t *o);
static int  anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf);
static int  anon_fillpage(mmobj_t *o, pframe_t *pf);
static int  anon_dirtypage(mmobj_t *o, pframe_t *pf);
static int  anon_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t anon_mmobj_ops = {
        .ref = anon_ref,
        .put = anon_put,
        .lookuppage = anon_lookuppage,
        .fillpage  = anon_fillpage,
        .dirtypage = anon_dirtypage,
        .cleanpage = anon_cleanpage
};

/*
 * This function is called at boot time to initialize the
 * anonymous page sub system. Currently it only initializes the
 * anon_allocator object.
 */
void
anon_init()
{
        /*NOT_YET_IMPLEMENTED("VM: anon_init");*/
	anon_allocator = slab_allocator_create("anonobj", sizeof(mmobj_t));
	KASSERT(NULL != vmmap_allocator && "failed to create anonobj allocator!");
}

/*
 * You'll want to use the anon_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
anon_create()
{
        /*NOT_YET_IMPLEMENTED("VM: anon_create");*/
	mmobj_t *myAnon;
	myAnon = (mmobj_t*)slab_obj_alloc(anon_allocator);
	*myAnon->mmo_ops = &anon_mmobj_ops;
	myAnon->mmo_refcount = 1;
	myAnon->mmo_nrespages = 0;
	list_init(&myAnon->mmo_respages);
	list_init(&myAnon->mmo_vmas);
        return myAnon;
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void
anon_ref(mmobj_t *o)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_ref");*/
	/* add KAASSERT to check whether the mmobj is anonymous*/
	o->mmo-refcount++;
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is an anonymous object, it will
 * never be used again. You should unpin and uncache all of the
 * object's pages and then free the object itself.
 */
static void
anon_put(mmobj_t *o)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_put");*/
	/* add KAASSERT to check whether the mmobj is anonymous and whether the refcount is ess the number of resident pages*/
	pframe *myFrame;
	/*o->refcount--;*/
	o->mmo_ops->put(o);
	if(o->refcount==o->mmo_nrespages){
		list_iterate_begin(&o->mmo_respages, myFrame, pframe, pf_olink) {
			while(pframe_is_pin(myFrame)){
				pframe_unpin(&myFrame);
			}
			if(pframe_is_busy(myFrame)){
				sched_sleep_on(&myFrame->pf_waitq);
			}
			if(pframe_is_dirty(myFrame)){
				pframe_clean(myFrame);
			}
			
			pframe_free(myFrame);
		} list_iterate_end();
                slab_obj_free(anon_allocator,o);
	}
}

/* Get the corresponding page from the mmobj. No special handling is
 * required. */
static int
anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_lookuppage");*/
	pframe *myFrame;
	list_iterate_begin(&o->mmo_respages, myFrame, pframe, pf_olink) {
        	if(myFrame->pf_pagenum == pagenum){
			*pf = myFrame;
			return 0;	
		}
        } list_iterate_end();

        return 0;
}

/* The following three functions should not be difficult. */

static int
anon_fillpage(mmobj_t *o, pframe_t *pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_fillpage");*/
	pframe *myFrame;
	if((o==pf->pf_obj)){
		myFrame = pframe_get_resident(pf->pf_obj,pf->pf_pagenum);
		if(myFrame!=NULL){
			memcpy(pf->pf_addr,myFrame->pf_addr,PAGE_SIZE);
			if(!pframe_is_pinned(myFrame)){
				pframe_pin(myFrame);
			}
		}else{
			return -EFAULT;
		}
	}else{
		/*add the right return value*/
	}	
        return 0;
}

static int
anon_dirtypage(mmobj_t *o, pframe_t *pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_dirtypage");*/
	
        return 0;
}

static int
anon_cleanpage(mmobj_t *o, pframe_t *pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_cleanpage");*/
	pframe *myFrame;
	if((o==pf->pf_obj)){
		myFrame = pframe_get_resident(pf->pf_obj,pf->pf_pagenum);
		if(myFrame!=NULL){
			memcpy(myFrame->pf_addr,pf->pf_addr,PAGE_SIZE);
		}else{
			return -EFAULT;
		}
	}else{
		/*add the right return value*/
	}	
        return -1;
}
