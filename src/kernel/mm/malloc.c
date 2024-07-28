#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ke.h"
#include "mm.h"

#define BLOCK_SIZE 1024 // 1 KB
#define PAGE_SIZE 4096 // 4 KB
#define UINT32BITS (32)

#define BSIZE sizeof(uint32_t)

#define calcsize(blocks, bitmaps) ((blocks) * BLOCK_SIZE + (bitmaps) * sizeof(uint32_t) + sizeof(bitmap_list_t))

#define test_bit(val, bit) ((val & bit) != 0)
#define set_bit(val, bit) val |= bit
#define clear_bit(val, bit) val &= ~bit
#define next_bit(bit) bit <<= 1;
#define calc_pageaddr(bitmapi, bit) (((bitmapi) * UINT32BITS + bit) * BLOCK_SIZE)
#define test_bitnumber(num, bitn) ((num & (1 << bitn)) != 0)

bitmap_list_t* firstblock = NULL;

uint32_t align_up(uint32_t address, uint32_t align) {
    if (align <= 0) {
        return address;
    }
    uint32_t remainder = address % align;
    if (remainder == 0) {
        return address;
    }
    return address + align - remainder;
}

void calc_blocks_and_bitmaps(uint32_t size, uint32_t* num_blocks, uint32_t* num_bitmaps)
{
    uint32_t blocks = size / BLOCK_SIZE;
    uint32_t bitmaps = blocks / UINT32BITS + 1;

    while (1)
    {
        if (calcsize(blocks, bitmaps) <= size)
            break;

        blocks--;
        bitmaps = blocks / UINT32BITS + 1;
    }

    *num_blocks = blocks;
    *num_bitmaps = bitmaps;
}

static inline void block_setstart(struct multiboot_mmap_entry* block, uint32_t new)
{
    int32_t old = new - block->addr;
    block->addr = new;
    block->len = (old > 0) ? (block->len - (uint32_t)old) : ((block->len - (uint32_t)(-old)));
}

static inline void block_setend(struct multiboot_mmap_entry* block, uint32_t new)
{
    block->len = new - block->addr;
}

static inline uint32_t block_getend(struct multiboot_mmap_entry* block)
{
    return block->addr + block->len;
}

static inline uint32_t is_block_under1mb(struct multiboot_mmap_entry* block)
{
    return block_getend(block) < 0x100000;
}

static inline uint32_t is_partofblock_under1mb(struct multiboot_mmap_entry* block)
{
    return block_getend(block) >= 0x100000 && block->addr < 0x100000;
}

bitmap_list_t* mm_initmemblock(struct multiboot_mmap_entry* block)
{
    //We dont allow memory under 1MB
    if(is_block_under1mb(block))
    {
        block->type = MULTIBOOT_MEMORY_RESERVED;
        printf("Chunk is under 1MB\n");
        return NULL;
    }

    if(is_partofblock_under1mb(block)) // and if block end is under 1 mb
    {
        block_setstart(block, 0x100000);
    }
    
    uint32_t blockend = block_getend(block);

    int b = block->addr < (uint32_t)&kernel_start;
    int t = blockend > (uint32_t)&kernel_end;

    //Cut conflict between kernel memory and block memmory
    if((b && t) || t)
    {
        block_setstart(block, (uint32_t)&kernel_end);
    }
    else if(b)
    {
        block_setend(block, (uint32_t)&kernel_start);
    }
    //Align memory on page
    uint32_t alignedaddr = align_up(block->addr, PAGE_SIZE);

    if(alignedaddr >= block->addr + block->len)
    {
        //🙂
        printf("Cut so much that no memory there is left\n");
        block->type = MULTIBOOT_MEMORY_RESERVED;
        return NULL;
    }

    block_setstart(block, alignedaddr);

    //Clear block with zero
    memset((void*)block->addr, 0, block->len);

    uint32_t blocks = 0;
    uint32_t bitmaps = 0;

    calc_blocks_and_bitmaps(block->len, &blocks, &bitmaps);
    
    bitmap_list_t* entry = (bitmap_list_t*)((uint32_t)block->addr + blocks * BLOCK_SIZE + bitmaps * sizeof(uint32_t));
    entry->blocks_count = blocks;
    entry->first_block = (void*)((uint32_t)block->addr);
    entry->first_bitmap = (uint32_t*)((uint32_t)block->addr + blocks * BLOCK_SIZE);
    entry->search_next_startblock = 0;
    entry->next = NULL;

    if(firstblock == NULL)
        firstblock = entry;

    return entry;
}

void* malloc(uint32_t count, uint32_t align_up)
{
    if(count == 0 || count > 32)
        return NULL;
    
    if(align_up == 0)
        align_up = 1;

    bitmap_list_t* curblock = firstblock;
    while(1)
    {
        const uint32_t repeatcount = curblock->blocks_count / UINT32BITS + 1;
        char isnotset_snsb = 1;
        for(uint32_t i = curblock->search_next_startblock; i < repeatcount; i++)
        {
            uint32_t* bitmap = &(curblock->first_bitmap)[i];

            if(*bitmap == 0xffffFFFF)
                continue;

            uint32_t bit = 1;
            uint32_t streak = 0;
            uint32_t firstj = 0;
            for(uint32_t j = 0; j < UINT32BITS; j++)
            {
                if(!streak) {
                    if(32 - j < count) {
                        break;
                    }

                    if(j % align_up != 0) {
                        continue;
                    }
                }

                if(!test_bit(*bitmap, bit))
                {
                    if(streak == 0)
                    {
                        firstj = j;
                    }

                    streak++;

                    if(isnotset_snsb)
                    {
                        curblock->search_next_startblock = i;
                        isnotset_snsb = 0;
                    }

                    if(streak == count)
                    {
                        j -= streak - 1;

                        bit = 1 << j;
                        for (uint32_t k = 0; k < streak; k++)
                        {
                            set_bit(*bitmap, bit);
                            next_bit(bit);
                        }
                        return (void*)((uintptr_t)(curblock->first_block) + calc_pageaddr(i, firstj));
                    }
                }
                else
                {
                    streak = 0;
                }
                next_bit(bit);
            }
        }

        if(curblock->next == NULL) {
            return NULL;
        }
        
        curblock = curblock->next;
    }
    return NULL;
}

void mfree(void* addr, uint32_t count)
{
    uint32_t addrint = (uint32_t)addr;

    if(count == 0 || count > 32)
        return;

    bitmap_list_t* curblock = firstblock;
    while(1)
    {
        if(addrint >= (uint32_t)curblock->first_block && addrint <= (uint32_t)curblock->first_block + curblock->blocks_count)
        {
            addrint -= (uint32_t)curblock->first_block;
            addrint /= BLOCK_SIZE;
            uint32_t bitn = addrint % 31;
            addrint -= bitn;
            uint32_t bitmapn = addrint / 32;
            uint32_t* bitmap = &(curblock->first_bitmap)[bitmapn];
            uint32_t bit = 1 << bitn;

            if(32 - bitn < count) {
                printf("ERR!1");
                return;
            }
            printf("fprev %b\n", *bitmap); //for debug

            for (uint32_t k = 0; k < count; k++)
            {
                clear_bit(*bitmap, bit);
                next_bit(bit);
            }
            printf("fnew  %b\n", *bitmap); //for debug
            return;
        }
        printf("err 1;%x %x\n", addrint, (uint32_t)curblock->first_block);

        if(curblock->next == NULL) {
            break;
        }
        
        curblock = curblock->next;
    }
    printf("Cant free block with addr %x and size %i\n", addr, count);
    return;
}

void mm_init()
{
    if(test_bitnumberumber(mb->flags, 6))
    {
        bitmap_list_t* newblock = NULL;
        for (size_t i = 0; i < mb->mmap_length; i += sizeof(struct multiboot_mmap_entry))
        {
            struct multiboot_mmap_entry* me = (struct multiboot_mmap_entry*)(mb->mmap_addr + i);

            if(me->type == MULTIBOOT_MEMORY_AVAILABLE)  
            {
                bitmap_list_t* oldblock = newblock;
                newblock = mm_initmemblock(me);

                if(oldblock) {
                    oldblock->next = newblock;
                }

                printf("addr = %x, len = %x, size = %x, type = %i, ret = %x\n", (uint32_t)me->addr, (uint32_t)me->len, me->size, me->type, newblock);
            }
        }
    }
    else
    {
        //🙂
        return;
    }
}