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
	KASSERT(NULL != anon_allocator && "failed to create anonobj allocator!");
	dbg(DBG_PRINT, "(GRADING3A 4.a) anon_allocator is successfully created.\n");
	KASSERT(anon_allocator);
	dbg(DBG_PRINT, "(GRADING3A 4.a) anon_allocator is successfully created.\n");
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
        mmobj_init(myAnon, &anon_mmobj_ops);
	myAnon->mmo_refcount = 1;
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
	/* add KAASSERT to check whether the mmobj is anonymousi*/
	KASSERT(o && (0 < o->mmo_refcount) && (&anon_mmobj_ops == o->mmo_ops));
	dbg(DBG_PRINT, "(GRADING3A 4.b) object o is not NULL and o's reference count is greater than 0 and its ops is anon obj ops.\n");
	o->mmo_refcount++;
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
	KASSERT(o && (0 < o->mmo_refcount) && (&anon_mmobj_ops == o->mmo_ops));
	dbg(DBG_PRINT, "(GRADING3A 4.c) object o is not NULL and o's reference count is greater than 0 and its ops is anon obj ops.\n");
	pframe_t *myFrame;
	
	if(o->mmo_refcount-1==o->mmo_nrespages){
		list_iterate_begin(&o->mmo_respages, myFrame, pframe_t, pf_olink) {
			if(pframe_is_pinned(myFrame)){
				pframe_unpin(myFrame);
			}
			while(pframe_is_busy(myFrame)){
				sched_sleep_on(&myFrame->pf_waitq);
			}
			if(pframe_is_dirty(myFrame)){
				pframe_clean(myFrame);
			}

			pframe_free(myFrame);
		} list_iterate_end();	
	}o->mmo_refcount--;
	if(o->mmo_refcount==0) slab_obj_free(anon_allocator,o);
}

/* Get the corresponding page from the mmobj. No special handling is
 * required. */
static int
anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_lookuppage");*/
	pframe_t *myFrame;
/*	list_iterate_begin(&o->mmo_respages, myFrame, pframe_t, pf_olink) {
        	if(myFrame->pf_pagenum == pagenum){
			*pf = myFrame;
			return 0;	
		}
        } list_iterate_end();
*/	
	/*if(forwrite==1){
		return -EPERM;
	}else{*/
		if(o->mmo_nrespages>0){
			list_iterate_begin(&o->mmo_respages, myFrame, pframe_t, pf_olink) {
				if(myFrame->pf_obj==o && myFrame->pf_pagenum==pagenum){
					*pf = myFrame;
					return 0;
				}
		    }list_iterate_end();
			return pframe_get(o,pagenum,pf);
		}else{
			return pframe_get(o,pagenum,pf);
		}

	/*}*/
	
        return 0;
}

/* The following three functions should not be difficult. */

static int
anon_fillpage(mmobj_t *o, pframe_t *pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_fillpage");*/
	pframe_pin(pf);
	KASSERT(pframe_is_busy(pf));
	dbg(DBG_PRINT, "(GRADING3A 4.d) pframe is not busy\n ");
	KASSERT(!pframe_is_pinned(pf));
	dbg(DBG_PRINT, "(GRADING3A 4.d) pframe is not pinned\n ");
	memset(pf->pf_addr,0,PAGE_SIZE);
	pframe_unpin(pf);
	/*pframe_t *myFrame=NULL;
	list_iterate_begin(&o->mmo_respages, myFrame, pframe_t,pf_olink){
		if(myFrame->pf_obj==o&& myFrame->pf_pagenum==pf->pf_pagenum){
			memcpy(pf->pf_addr,myFrame->pf_addr,PAGE_SIZE);
	                if(!pframe_is_pinned(myFrame)){
        	                pframe_pin(myFrame);
               		}
		}
	}list_iterate_end();
	if(myFrame==NULL)
		return -EFAULT;*/
	/*myFrame = pframe_get_resident(o,pf->pf_pagenum);
	if(myFrame!=NULL){
		memcpy(pf->pf_addr,myFrame->pf_addr,PAGE_SIZE);
		if(!pframe_is_pinned(myFrame)){
			pframe_pin(myFrame);
		}
	}else{
		return -EFAULT;
	}*/
        return 0;
}

static int
anon_dirtypage(mmobj_t *o, pframe_t *pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_dirtypage");*/
	/*since teh anonymous object should not be dirty. Nothing have to do here*/
        return -EPERM;
}

static int
anon_cleanpage(mmobj_t *o, pframe_t *pf)
{
        /*NOT_YET_IMPLEMENTED("VM: anon_cleanpage");*/
	/*pframe_t *myFrame=NULL;
	list_iterate_begin(&o->mmo_respages, myFrame, pframe_t,pf_olink){
		if(myFrame->pf_obj==o&& myFrame->pf_pagenum==pf->pf_pagenum){
			memcpy(myFrame->pf_addr,ph->pf_addr,PAGE_SIZE);
		}
	}list_iterate_end();	
	if(myFrame==NULL)
		return -EFAULT;
	*/
	/*myFrame = pframe_get_resident(o,pf->pf_pagenum);
	if(myFrame!=NULL){
		memcpy(myFrame->pf_addr,pf->pf_addr,PAGE_SIZE);
	}else{
		return -EFAULT;
	}*/
        return 0;
}
