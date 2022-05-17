// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for(volatile int i = 0;i < NCPU; ++i) {
    initlock(&kmem[i].lock, "kmem");
    void* begin = end + NMEM*i;
    freerange(begin, begin + NMEM);
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  volatile uint64 tmp = NMEM;
  volatile int i = ((uint64)((char*)pa - end))/tmp;

  r = (struct run*)pa;

  acquire(&kmem[i].lock);
  r->next = kmem[i].freelist;
  kmem[i].freelist = r;
  release(&kmem[i].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int i = cpuid();
  pop_off();

  acquire(&kmem[i].lock);
  r = kmem[i].freelist;
  if(r)
    kmem[i].freelist = r->next;

  release(&kmem[i].lock);
  if(!r) {
    int j = 0;
    for(j = 0;j < NCPU; ++j) {
      acquire(&kmem[j].lock);
      r = kmem[j].freelist;
      if(r){
        kmem[j].freelist = r->next;
        release(&kmem[j].lock);
        break;
      }
      release(&kmem[j].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
