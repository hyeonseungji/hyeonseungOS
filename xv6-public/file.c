//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

typedef struct node{
	int from;
	int to;
	struct node* next;
} node;

node head[1000];
node *fn;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();
      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

int pwrite_os(int fd, void* addr, int n, int off) {
	struct file *f;
	int r;

	if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0){
	 cprintf("[pwriete]error!\n");
	 return -1;
	}
  	if(f->type == FD_INODE){
    	// write a few blocks at a time to avoid exceeding
    	// the maximum log transaction size, including
    	// i-node, indirect block, allocation blocks,
    	// and 2 blocks of slop for non-aligned writes.
    	// this really belongs lower down, since writei()
    	// might be writing a device like the console.
    	int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    	int i = 0;
    	while(i < n){
      		int n1 = n - i;
      		if(n1 > max)
        		n1 = max;
      		begin_op();
      		ilock(f->ip);
		if(off > f->ip->size) { //By this statement, os can avoid inappropriate error in 'writei' function. It sets new ip size.
			int off_x = off;
			int tot, m;
			for(tot = 0; tot<n1; tot+=m, off_x+=m) {
				m = min(n - tot, BSIZE - off%BSIZE);
			}
			f->ip->size = off_x;
		}
		iunlock(f->ip);
      		if ((r = writei(f->ip, addr + i, off, n1)) > 0) {
        		off += r;
		} //I just change the value of 'Filewrite' function(f->off  -> off).
		ilock(f->ip);
		if(f->off < off) { //It sets new file offset. New file offset is max value of input offsets(when user uses multicores).
			f->off = off;
		}
      		iunlock(f->ip);
      		end_op();
      		if(r < 0){
        		break;
		}
      		if(r != n1)
        		panic("short filewrite");
      		i += r;
    	}
    	return i == n ? n : -1;
  }
  panic("filewrite");
  return 0;
}
//This pread_os is almost same as 'fileread' except it uses input 'off' values not 'f->off' values.
int pread_os(int fd, void* addr, int n, int off) {
	struct file *f;
	int r;
	if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0){
	 cprintf("[pread]error!\n");
	 return -1;
	}
  	if(f->type == FD_INODE){
    	 ilock(f->ip);
   	 if((r = readi(f->ip, addr, off, n)) > 0)
      		off += r;
    	 iunlock(f->ip);
    	 return r;
  	}
  	panic("ErrorR on pread");
  	return 0;
}
