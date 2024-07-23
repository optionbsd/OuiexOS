#include <stdint.h>
#include "ke.h"
#include "mm.h"

#define BLOCK_SIZE 1024 // 1 KB
#define UINT32BITS (8 * sizeof(uint32_t))

#define calcsize(chunks, bitmaps) ((chunks) * BLOCK_SIZE + (bitmaps) * sizeof(uint32_t))

void mm_init()
{
    if(testbit(mb->flags, 6))
    {
        for (size_t i = 0; i < mb->mmap_length; i += sizeof(struct multiboot_mmap_entry))
        {
            struct multiboot_mmap_entry *me = (struct multiboot_mmap_entry*)(mb->mmap_addr + i);
            if(me->type == MULTIBOOT_MEMORY_AVAILABLE)
                mm_initblock(me);
        }
    }
}

void mm_initblock(struct multiboot_mmap_entry* block)
{

}

void calc_chunks_and_bitmaps(uint32_t size, uint32_t* num_chunks, uint32_t* num_bitmaps) {
    uint32_t chunks = size / BLOCK_SIZE;
    uint32_t bitmaps = chunks / UINT32BITS + 1;

    while (1)
    {
        if (calcsize(chunks, bitmaps) <= size)
            break;

        chunks--;
        bitmaps = chunks / UINT32BITS + 1;
    }

    *num_chunks = chunks;
    *num_bitmaps = bitmaps;
}

void* malloc()
{
    uint32_t j = 0;
    for(uint32_t i = 0; i < PTSIZE / BSIZE; i++)
    {
        uint32_t* bitmap = &pagebm[i];
        
        if(*bitmap == 0xffffFFFF)
            continue;
        
        uint32_t bit = 1;
        for( j = 0; j < 8 * BSIZE; j++)
        {
            if(!test_bit(*bitmap, bit))
            {
                set_bit(*bitmap, bit);
                return (uintptr_t)pagedata + calc_pageaddr(i, j);
            }
            next_bit(bit);
        }
    }
    return NULL;
}