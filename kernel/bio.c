// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define MAXTIME 1000000

struct {
  struct {
    struct spinlock lock;  

    struct buf* buf;
  } bucket[NBUCKET];

  struct buf buf[NBUF];

  struct spinlock lock;  

  uint evict;
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");
  bcache.evict = 0;

  for(int i = 0;i < NBUCKET; ++i) {
    initlock(&bcache.bucket[i].lock, "bcache");
    bcache.bucket[i].buf = &bcache.buf[i];  // just for initialization
  }

  for(int i = 0;i < NBUF; ++i) {
    initsleeplock(&bcache.buf[i].lock, "buffer");
    bcache.buf[i].time = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;


  // Is the block already cached? first search in the hashmap
  for(int i = 0;i < NBUCKET; ++i) {
    acquire(&bcache.bucket[i].lock);
    b = bcache.bucket[i].buf;
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->time = ticks;
      release(&bcache.bucket[i].lock);
      acquiresleep(&b->lock);
      return b;
    }
    release(&bcache.bucket[i].lock);
  }

  // Then search in the whole cache and evict in the hashtable
  acquire(&bcache.lock);
  for(int i = 0;i < NBUF; ++i) {
    b = &bcache.buf[i];
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->time = ticks;
      acquire(&bcache.bucket[i%NBUCKET].lock);
      bcache.bucket[i%NBUCKET].buf = b; // put this to the hashtable
      release(&bcache.bucket[i%NBUCKET].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }


  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(int i = 0; i < NBUF; ++i) {
    b = &bcache.buf[i];
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->time = ticks;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->time = 0;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


