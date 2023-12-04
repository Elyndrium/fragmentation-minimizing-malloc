/*
 * We implemented an adress-ordered explicit free list with best fit and bidirectionnal coalescing, with custom realloc.
 * We don't use any global variable.
 * 
 * Every block has a header corresponding to its full block size. Since blocks are 8-bits aligned, we use the first bit of the size to store an "allocated" flag: 0 if the block is free, 1 otherwise. There is no footer.
 * Every allocated block contains the header and the payload, that extends until the next block's header.
 * Every free block contains, in addition to the header, two pointers: one to the next free block, one to the previous. It then has undefined bit values until its end (defined by header adress + size).
 * 
 * To malloc, we use a best-fit approach. If no block is found and the last one is free, we extend by the just right amount.
 * To free, we use a bidirectionnal coalescing and preserve the adress order of the free list in a linear cost.
 * To realloc, we try to extends the block in place if the one after is free. If it is the last block of the heap we extend the memory by the appropriate amount. Otherwise we call malloc then free.
 * 
 * Look at each functions's docstring for a further description of the implementations and optimisations.
 *
 * We provide two functions to print and debug the free list as well as the whole heap. Various cheks are perfomed, descriped in the respective docstrings. We combine the two functions in mm_check().
 * 
 *                                              We achieve a consistent 94/100.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "RPOG",
    /* First member's full name */
    "Octave GASPARD",
    /* First member's email address */
    "octave.gaspard@polytechnique.edu",
    /* Second member's full name (leave blank if none) */
    "Romain PUECH",
    /* Second member's email address (leave blank if none) */
    "romain.puech@polytechnique.edu"
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


#define BLOCK_HEADER    0
#define BLOCK_END       2
#define BLOCK_PAYLOAD   3
#define BLOCK_FORWARD   3
#define BLOCK_BACKWARD  4

/*
 * moved_pointer - Used to do pointer arithmetic and easily travel through different positions in block
 *                "blocklen" can be set to 0 when it is not needed
 */

void* moved_pointer(void* point_in, size_t blocklen, int position_point, int position_wanted){
    char *point = (char *) point_in;
    if (position_point == BLOCK_END){
        point = point-blocklen;
    } else if (position_point == BLOCK_FORWARD){
        point = point - SIZE_T_SIZE;
    } else if (position_point == BLOCK_BACKWARD){
        point = point - sizeof(void*) - SIZE_T_SIZE;
    }
    // point is now at the header
    if (position_wanted == BLOCK_HEADER){
        return (void*)point;
    } else if (position_wanted == BLOCK_END){
        return (void*)(point + blocklen);
    } else if (position_wanted == BLOCK_FORWARD){
        return (void*)(point + SIZE_T_SIZE);
    } else{
        return (void*)(point + SIZE_T_SIZE + sizeof(void*));
    }
}

// debug functions

/*
 * free_list_debug - Prints in a readable way the free list, and performs cheks along the way:
 *  - Is each free block actually flagged as free?
 *  - Is the bachward pointer indeed pointing toward the previous element of the list?
 *  - Are the adresses well ordered in the list? (We rely on an adress-ordered free list to optimize some parts of the code) 
 *  - Are there contiguous free blocks that escaped coalescing? 
 * 
 * verbose = 1 will print, verbose = 0 will only check. 
 * returns 1 if everything went well, otherwise print error message and returns 0.       
 */
int free_list_debug(int verbose){
    if(verbose)printf("\n___FREE LIST:___\n");

    void **pos = *(void***)mem_heap_lo();

     if(pos==NULL || *pos==NULL){
        return;
    }

    void **prevpos = NULL;
    size_t size = 0;
    size_t prevsize = 0;

    while (pos!=NULL){
        size = *(size_t*)moved_pointer(pos, 0, BLOCK_FORWARD, BLOCK_HEADER);
        if(verbose){
            printf("   Forward position : %p\n", pos);
            printf("   Forward  : %p\n", *pos);
            printf("   Backward : %p\n", *(void**)moved_pointer(pos, 0, BLOCK_FORWARD, BLOCK_BACKWARD));
            printf("   Header : %zu\n", size);
        }

        // some sanity checks
        
        //is the backward link indeed pointing towards the previous element?
        if (prevpos != NULL && *(void **)moved_pointer(pos, 0, BLOCK_FORWARD, BLOCK_BACKWARD) != moved_pointer(prevpos, 0, BLOCK_FORWARD, BLOCK_BACKWARD)){
            printf("   Backwards pointer is WRONG \n");
            return 0;
        }

        // is every block of the list free?
        
        if(size%2==1){
            printf("   The block is not flagged as free\n");
            return 0;
        }

        // are the adresses in increasing order?
        
        if(pos <= prevpos){
            printf("   The adresses are not well ordered\n");
            return 0;
        }

        // Did the free blocks escape coalescing?
        
        if(prevpos!=NULL && moved_pointer(prevpos,prevsize,BLOCK_FORWARD,BLOCK_END) == moved_pointer(pos,0,BLOCK_FORWARD,BLOCK_HEADER)){
            printf("   The two last blocks escaped coalescing\n");
            return 0;
        }
        prevpos = pos;
        prevsize=size;
        // follow linked list
        pos = (void**)*pos;
        
    }
    return 1;
}

/*
 * print_heap_blocks - Prints in a readable way the whole heap as it is and its boundaries, 
 * and cheks along the way if every free block is in the free list. 
 *
 * if verbose = 1, it will print. If verbose = 0, it will only check.
 * returns 1 if everything went well, otherwise print error message and returns 0.         
 */
int print_heap_blocks(int verbose){
    
    if(verbose){
        printf("\n___HEAP BLOCKS PRINT___\n");
        printf("First block is start of linked list\n  pos = %p \n  pointing to: %p\n",mem_heap_lo(),*(void **)mem_heap_lo());
    }

    size_t *pos = (size_t*)ALIGN((size_t)((char*)mem_heap_lo()+1));
    size_t current_size_header = *pos;

    if(current_size_header==0){
        printf("empty\n");
        return 2;
    }

    void* next_free_block = *(void**)mem_heap_lo();
    size_t *end_of_heap = (size_t *)((char*)mem_heap_hi()+1); // +1 since mem_heap_hi() points to last byte

    if(verbose)printf("Start of heap.\n");

    //we jump from blocks to blocks untill we reach the heap boundary
    while (pos!=end_of_heap){

        if(verbose)printf("\nblock at adress %p :\n",pos);
        current_size_header = *pos;

        // is the block allocated? Use flag stored in first bit of size to know
        unsigned short allocated = current_size_header & 1;
        if(allocated){
            if(verbose)printf("  allocated\n");
        }else{
            if(verbose)printf("  free block \n");
            // Does this adress corresponds to the one indicated by the last forward pointer of the linked list?
            if(next_free_block!=moved_pointer(pos,0,BLOCK_HEADER,BLOCK_FORWARD)){
                printf("  free block not in free list!\n");
                return 0;
            }
            //follow the linked list
            next_free_block = *(void**)moved_pointer(pos,0,BLOCK_HEADER,BLOCK_FORWARD);
        }
        
        if(verbose)printf("  size = %zu\n",current_size_header);
        // jump size bytes ahead. & ~1 to get true size without allocated flag.
        pos = (size_t*)moved_pointer(pos,current_size_header & ~1,BLOCK_HEADER,BLOCK_END);
    }
    if(verbose)printf("end of heap: %p\n",mem_heap_hi());
    return 1;
}

/*
 * mm_check - calls the two functions print_heap_blocks and free_list_debug to perform checks on the heap.
 * See above docstrings for details. 
 * Nothing is printed.
 *  
 * returns 1 if both functions spotted no error, otherwise print error message and returns 0.       
 */
int mm_check(void){
    return print_heap_blocks(0) & free_list_debug(0);
}

/////// memory allocator code

/*
 * mm_init - initialize the malloc package. Nothing to do as we don't use any global variable.
 */
int mm_init(void)
{
    return 0;
}

/* 
 * mm_malloc - Allocate a block and returns its adress.
 * Always allocate a block whose size is a multiple of the alignment.
 * 
 *  - Tries to find best fit among free blocks (minimal size difference) if possible. If perfect fit is found stops earlier.
 *  - If a fit is found, if possible split the free block in two: the part we use to allocate the new pointer, and the new free part.
 *  - If previous search fails, since we are adress ordered we point to the last free block. If it's the heap's last block, then we extend the heap by the just right amount.
 *  - Otherwise extends memory by the asked size.
 * 
 */
void *mm_malloc(size_t size)
{   
    // To be sure there is enough space to put pointers when freed (2*pointer)
    if ((int)size < 2*sizeof(void *)){
        size = (size_t) 2*sizeof(void *);
    }

    size_t newsize = ALIGN(size + SIZE_T_SIZE); // Size is payload size, newsize includes full block
    
    if (mem_heapsize() == 0){ // Basically first call
        // Start writing after letting 1 byte for first free list pointer
        void *p = mem_sbrk(newsize+sizeof(void *))+sizeof(void *);
        if ((void*)ALIGN((size_t)p)!=p){
            mem_sbrk(ALIGN((size_t)p)-(size_t)p);
            p = (void*)ALIGN((size_t)p);
        }
        *(void **)mem_heap_lo() = NULL; // Points to first free block (none)
        if (p == (void *)-1){
            printf("Unknown error when allocating first block\n");
            return NULL;
        }else {
            // Place size signs
            *(size_t *)p = newsize + 1;
            return (void *)moved_pointer(p, newsize, BLOCK_HEADER, BLOCK_PAYLOAD);
        }
    }

    void **current_pos = mem_heap_lo(); // Get entry to linked list
    size_t size_act;
    void *best_p = NULL;
    size_t best_s = pow(16, SIZE_T_SIZE) - 1; // Max possible size (= worst)
    
    if (*current_pos != NULL){
        
        // If we have at least 1 element in the list, we go to it
        current_pos = *((void **)current_pos);
        

        while (1){ // While we don't reach the end of the "forward" linked list
            
            //Find best fit
            size_act = *(size_t *)moved_pointer(current_pos, 0, BLOCK_FORWARD, BLOCK_HEADER);

            if (size_act > newsize && size_act < best_s){
                
                best_p = current_pos;
                best_s = size_act;
            }
            else if (size_act == newsize){
                
                best_p = current_pos;
                best_s = size_act;
                break;
            }
            if(*((void **)current_pos) == NULL){
                break;// that way, we make sure that whe we exit the loop, current_pos represents the last free block (to be used later!)
            }
            current_pos = *((void **)current_pos);
            
        }
    }

    if (best_p == NULL){
        // we extend only the amount we need
        // Since we are adress ordered, if the last block is free then is is pointed by current_pos
        void *p;
        if (moved_pointer(current_pos,size_act,BLOCK_FORWARD,BLOCK_END) == (void*)((char*) mem_heap_hi()+1)){
            
            // In this case, the last pointer is a free block, we can increase by just the right amount!
            mem_sbrk(newsize-size_act);
            
            void **before = (void **)moved_pointer(current_pos, 0, BLOCK_FORWARD, BLOCK_BACKWARD);
            
            if (*before!=NULL){
    
                *(void **)moved_pointer(*(void **)moved_pointer(current_pos, 0, BLOCK_FORWARD, BLOCK_BACKWARD), 0, BLOCK_BACKWARD, BLOCK_FORWARD) = NULL; // remove it from linked list
            }
            else{
                *(void **)mem_heap_lo() = NULL;
            }// no need to change the forward: it is the last pointer of the list!
            
            p = moved_pointer(current_pos,0,BLOCK_FORWARD,BLOCK_HEADER);
           

        }else{
            // The last one wasn't free
            p = mem_sbrk(newsize);
            
        }
        
        if (p == (void *)-1){
            printf("Unknown error when allocating a block\n");
            return NULL;
        }else {
            
            *(size_t *)p = newsize + 1;
            return moved_pointer(p, newsize, BLOCK_HEADER, BLOCK_PAYLOAD);
        }
    }
    // Best_p points the beginning of the payload
    
    // Else, split if necessary/possible | keep first part free so as not to change free list but just size
    else{
        if (best_s-newsize >= SIZE_T_SIZE + 2*sizeof(void *)){
            // If splittable
            
            // Change header of free block
            *((size_t*)moved_pointer(best_p, newsize, BLOCK_FORWARD, BLOCK_HEADER)) = best_s-newsize;
            best_p = (void*)((char*)best_p + best_s-newsize); // Place at the beginning of the newly allocated HEADER
        }
        else{ 
            // If not splittable
            newsize = best_s; // Increase the size of the block to fill it

            // Skip block in the "forward" linked list = first pointer
            if (*((void **)moved_pointer(best_p, newsize, BLOCK_FORWARD, BLOCK_BACKWARD)) != NULL){
                *(void**)moved_pointer(*((void **)moved_pointer(best_p, newsize, BLOCK_FORWARD, BLOCK_BACKWARD)), 0, BLOCK_BACKWARD, BLOCK_FORWARD) = *(void **)best_p;
            }
            else{
                *(void **)mem_heap_lo() = *(void **)best_p;
            }
            // Skip block in the "backwards" linked list = second pointer
            
            if (*(void**)best_p != NULL){
                *(void**)moved_pointer(*(void**)best_p, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = *(void**)moved_pointer(best_p, newsize, BLOCK_FORWARD, BLOCK_BACKWARD);
            }
        }
        // Now that the free block is done, set the allocated block metadata
        *(size_t*)moved_pointer(best_p, newsize, BLOCK_FORWARD, BLOCK_HEADER) = newsize+1;
    }
    
    // Finally, return the pointer to the block
    return best_p;
}

/*
 * mm_free - Frees the given pointer and coalesces free blocks on the right and on the left if possible.
 * preserves the adress ordering of the free blocks in the free list.
 */
void mm_free(void *ptr)
{
    // ptr is at "BLOCK_FORWARD"
    // Remove the allocated flags :
    size_t free_size = *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) & ~1;
    
    *(size_t *)moved_pointer(ptr, free_size, BLOCK_FORWARD, BLOCK_HEADER) = free_size;
    

    void **before_block = mem_heap_lo(); // Get first element of linked list
    void **after_block;
    short list_empty = 1;
    
    if (*before_block != NULL){
        // If we have at least 1 element in the list
        
        // If this element is after pointer, add ptr between beginning and the block
        if ((long unsigned int)*before_block > (long unsigned int) ptr){
            // First add it
            after_block = *before_block;
            *(void**)before_block = ptr; // Before forward to self
            *(void**)moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD);// After backward to ptr backward
            *((void **)ptr) = after_block;// Ptr forward to after forward
            *((void**)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD)) = NULL; // Ptr backward to previous backward

            // If coalescing possible with the block after:
                // (we check that after_block header is ptr end)
            if (moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_HEADER) == moved_pointer(ptr, *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER), BLOCK_FORWARD, BLOCK_END)){
                
                // ptr gets bigger size
                size_t new_block_size = *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) + *(size_t *)moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_HEADER);
                *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) = new_block_size;
                
                // skip after_block
                *(void**)ptr = *(void**)after_block; // ptr forward = after forward
                if (*(void**)ptr != NULL){
                    // If there is a next (next) one, skip after_block in backwards list
                    *(void**)moved_pointer(*(void**)ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD);
                }
            }
            return;

        }
        // Otherwise, we go to it
        before_block = *((void **)before_block);
        
        list_empty = 0;
    }
    
    while (*before_block != NULL){ // While we don't reach the end of the "forward" linked list
        // Wait until it's in between before_pos and *before_pos
        
        if ((long unsigned int)*before_block > (long unsigned int)ptr){
            after_block = *before_block;

            // First, just run normally :
                // Add ptr between before and after block
            *before_block = ptr;// Previous forward to ptr forward
            *(void**)moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD);// After backward to ptr backward
            *((void **)ptr) = after_block;// Ptr forward to after forward
            *((void**)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD)) = moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_BACKWARD);// Ptr backward to previous backward

            // If coalescing possible with the block before: (should not be possible to do it twice)
                // (we check that ptr header is before_block end)
            if (moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) == moved_pointer(before_block, *(size_t *)moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_HEADER), BLOCK_FORWARD, BLOCK_END)){
                
                // before_block gets bigger size
                size_t new_block_size = *(size_t *)moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_HEADER) + *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER);
                *(size_t *)moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_HEADER) = new_block_size;
                
                // skip ptr
                *(void**)before_block = after_block; // before forward = ptr forward = after_block
                *(void**)moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_BACKWARD);

                // ptr "becomes" before_block for the next coalescing
                ptr = before_block;
            }

            

            // If coalescing possible with the block after: (should not be possible to do it twice)
                // (we check that after_block header is ptr end)
            if (moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_HEADER) == moved_pointer(ptr, *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER), BLOCK_FORWARD, BLOCK_END)){
                
                // ptr gets bigger size
                size_t size_new_block = *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) + *(size_t *)moved_pointer(after_block, 0, BLOCK_FORWARD, BLOCK_HEADER);
                *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) = size_new_block;
                
                // skip after_block
                *(void**)ptr = *(void**)after_block; // ptr forward = after forward
                if (*(void**)ptr != NULL){
                    
                    // If there is a next (next) one, skip after_block in backwards list
                    *(void**)moved_pointer(*(void**)ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD);
                }
            }
            return;
        }
        before_block = *((void **)before_block);
    }
    

    
    // If never reached, add to the end of the list
    
    if (list_empty){
        *before_block = ptr;// Previous forward to ptr forward
        *(void **)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = NULL;// Ptr backward to NULL
    } else{
        // If we can coalesce with before
        if (moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER) == moved_pointer(before_block, *(size_t *)moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_HEADER), BLOCK_FORWARD, BLOCK_END)){
                // before_block gets bigger size
                size_t new_block_size = *(size_t *)moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_HEADER) + *(size_t *)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_HEADER);
                *(size_t *)moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_HEADER) = new_block_size;
    
                // skip ptr
                *(void**)before_block = NULL; // before forward = ptr forward = NULL

                // ptr "becomes" before_block for the next coalescing
                ptr = before_block;
            }
        else{
            *before_block = ptr;// Previous forward to ptr forward
            *(void **)moved_pointer(ptr, 0, BLOCK_FORWARD, BLOCK_BACKWARD) = moved_pointer(before_block, 0, BLOCK_FORWARD, BLOCK_BACKWARD);
        }
    }
    *((void **)ptr) = NULL;// Ptr forward to NULL
    
    return;
}

/*
 * mm_realloc - reallocs a block pointed by ptr to a size given by asked_size. returns a pointer to the adress of the new block.
 * Calling with ptr==NULL is equivalent to a call to malloc. Calling with asked_size=0 and ptr!=NULL is equivalent to free.
 * 
 *  - Does not realloc if asked_size is less or equal then the current block size.
 *  - Directly extends the block if there is a free block after it. If possible, split it into the allocated part and a new, smaller free block.
 *  - If ptr points to the last block of the heap, extends the heap by the just right amount and keep the ptr adress.
 *  - IF all the above fail, we default to a call to malloc and free.
 * 
 */
void *mm_realloc(void *ptr, size_t asked_size)
{
    
    // dealing with degenerate cases
    if(ptr==NULL){
        
        return mm_malloc(asked_size);
    }
    if(asked_size==0){
        
        mm_free(ptr);
        return ptr;
    }

    size_t asked_block_size = ALIGN(asked_size+SIZE_T_SIZE); // since the size we store corresponds to the actual size of the full block - not just payload
    size_t current_size = *(size_t*) moved_pointer(ptr,0,BLOCK_PAYLOAD,BLOCK_HEADER)-1; //-1 because of flag

    // Do we actually have to realloc? With the padding we add, this situation occurs quite often:
    if(asked_block_size <= current_size){
        return ptr;
    }
    
    // first check if there is already enough space after the block.
    void* next_block_header = moved_pointer(ptr,current_size,BLOCK_PAYLOAD,BLOCK_END);
    size_t next_block_size = *(size_t*) next_block_header;
    //is the next block not the end of the heap and free?
    if ((current_size < asked_block_size) && (next_block_header != (void*)((char*) mem_heap_hi()+1)) && !(next_block_size&1)){
        // Is this free block larger than the additionnal space we need?
        if((asked_block_size-current_size) <= (next_block_size)){
            //We just extend block.  Now two cases:
            //1. What will be left of the free block is enough to constitute a new free block
            //2. What will remain is too small, in that case we include it in the allocated block to avoid permanent memory leak.

            if((int)(next_block_size - (asked_block_size-current_size)) >= SIZE_T_SIZE + 2*sizeof(void *)){
                //1. What will remain is large enough and will replace the current free block in the linkedlist
                //save free block info
                void* next_forward = *(void**)moved_pointer(next_block_header,next_block_size,BLOCK_HEADER,BLOCK_FORWARD);
                void* next_backward = *(void**)moved_pointer(next_block_header,next_block_size,BLOCK_HEADER,BLOCK_BACKWARD);
                
                // We extend the memory block
                *(size_t*) moved_pointer(ptr,0,BLOCK_PAYLOAD,BLOCK_HEADER) = asked_block_size+1; //+1 for flag
                
                // We create the new free block
                void* new_free_block_head = moved_pointer(ptr,asked_block_size,BLOCK_PAYLOAD,BLOCK_END);
                void* new_free_block = moved_pointer(new_free_block_head,0,BLOCK_HEADER,BLOCK_FORWARD);
                void* new_free_block_backward = moved_pointer(new_free_block_head,0,BLOCK_HEADER,BLOCK_BACKWARD);
                size_t new_free_size = (size_t)(next_block_size - (asked_block_size-current_size));
                
                *(size_t*) new_free_block_head = new_free_size;
                *(void**)moved_pointer(new_free_block_head,new_free_size,BLOCK_HEADER,BLOCK_FORWARD) = next_forward;
                *(void**)moved_pointer(new_free_block_head,new_free_size,BLOCK_HEADER,BLOCK_BACKWARD) = next_backward;


                // update the linked list 

                // change backward of next element, if it is not NULL
                if(next_forward != NULL){
                    
                    size_t next_forward_size = *(size_t*) moved_pointer(next_forward,0,BLOCK_FORWARD,BLOCK_HEADER);
                    *(void**)moved_pointer(next_forward,next_forward_size,BLOCK_FORWARD,BLOCK_BACKWARD) = new_free_block_backward;
                }

                //change forward of previous element if not NULL
                if(next_backward != NULL){
                    *(void**) moved_pointer(next_backward,0,BLOCK_BACKWARD,BLOCK_FORWARD) = new_free_block;
                }else{
                    //it was actually the first free block of the linked list, so we have to change the entry pointer
                    *(void**)mem_heap_lo() =  new_free_block;   
                }
                
            }else
            {
                //2. What will remain is too small, in that case we include it in the allocated block to avoid permanent memory leak.
                //save free block info
                void* next_forward = *(void**)moved_pointer(next_block_header,next_block_size,BLOCK_HEADER,BLOCK_FORWARD);
                void* next_backward = *(void**)moved_pointer(next_block_header,next_block_size,BLOCK_HEADER,BLOCK_BACKWARD);
                
                // We extend the memory block
                *(size_t*) moved_pointer(ptr,0,BLOCK_PAYLOAD,BLOCK_HEADER) = current_size+next_block_size+1; //+1 for flag
                

                // There is no more free block, we have to remove it from the LinkedList 
                // change backward of next element, if it is not NULL
                if(next_forward != NULL){
                    *(void**)moved_pointer(next_forward,0,BLOCK_FORWARD,BLOCK_BACKWARD) = next_backward;
                }
                
                //change forward of previous element if not NULL
                if(next_backward != NULL){
                    *(void**) moved_pointer(next_backward,0,BLOCK_BACKWARD,BLOCK_FORWARD) = next_forward;
                }else{
                    
                    //it was actually the first free block of the linked list, so we have to change the entry pointer
                    *(void**)mem_heap_lo() =  NULL;
                }
                
            }
            return ptr;
        }
    }
    // Is the block we are trying to realloc the very last?
    if(next_block_header == (void*)((char*) mem_heap_hi()+1)){
        //then we just extend the heap by the amount we need
        mem_sbrk(asked_block_size-current_size);
        //update header size
        *(size_t*)moved_pointer(ptr,0,BLOCK_PAYLOAD,BLOCK_HEADER) = asked_block_size+1;//+1 for flag
        return ptr;
    }
    //defaulting to the good old way: malloc + free.
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(asked_size);

    if (newptr == NULL){
        printf("_______________Error reallocating memory_______________\n");
        return NULL;
    }

    copySize = *(size_t *)((char *)oldptr);

    if (asked_size < copySize)
        copySize = asked_size;
    
    memcpy(newptr, oldptr, asked_size);
    mm_free(oldptr);
    
    return newptr;
}
// :)
