#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  int share;
} ptable;

static struct proc *initproc;

int table_size;
int nextpid = 1;
int max_pid;
int max_sum;
int tick_mlfq;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct {
  int pid;
  int share;
  int full;
  int path;
  int is_stride;
  struct proc* proc;
} stride_table[NSTRIDE];

struct mlfq{
 int my_tick;
 int spare_tick;
 int sum_tick;
 int is_mlfq;
 int lev;
 int run;
 struct mlfq * prev;
 struct mlfq * next;
 struct proc* proc;
} mlfq[NSTRIDE];

struct {
 int spare_tick;
 int size;
 struct mlfq * next;
 struct mlfq * end;
 struct mlfq * cur;
} mlfq_lev[3];

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED){
      goto found;
  }
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

void
MLFQ_in(struct proc * p, int level)
{
mlfq[p->pid].my_tick = 0;
mlfq[p->pid].sum_tick = 0;
mlfq[p->pid].is_mlfq = 1;
mlfq[p->pid].lev = level;
mlfq[p->pid].proc = p;
 if(mlfq_lev[level].size == 0){
   mlfq_lev[level].next = &mlfq[p->pid];
   mlfq_lev[level].end = &mlfq[p->pid];
   mlfq_lev[level].cur = &mlfq[p->pid];
   /*cprintf("in clear![%d]\n",p->pid);*/
 }
 else{
  mlfq[p->pid].prev = mlfq_lev[level].end;
  mlfq[p->pid].prev -> next = &mlfq[p->pid];
  mlfq_lev[level].end -> next = &mlfq[p->pid];
  mlfq_lev[level].end = &mlfq[p->pid];
  cprintf("prev = [%d]\n",mlfq[p->pid].prev->proc->pid);
  cprintf("prev->next = [%d]\n",mlfq[p->pid].prev->next->proc->pid);
 }
 mlfq[p->pid].next = 0;
 mlfq_lev[level].size++;
}

void
MLFQ_out(struct proc * p, int level)
{
/*cprintf("  out operated  ");*/

if(mlfq_lev[mlfq[p->pid].lev].cur == &mlfq[p->pid]){
 cprintf("\njijijijijia\n");
 if(mlfq[p->pid].next != 0)
  mlfq_lev[mlfq[p->pid].lev].cur = mlfq[p->pid].next;
 else{
   cprintf("\njijijijib\n");
   if(mlfq_lev[mlfq[p->pid].lev].next != mlfq_lev[mlfq[p->pid].lev].cur)
    mlfq_lev[mlfq[p->pid].lev].cur = mlfq_lev[mlfq[p->pid].lev].next;
   else{
   }
  }
}

mlfq[p->pid].my_tick = 0;
mlfq[p->pid].sum_tick = 0;
mlfq[p->pid].is_mlfq = -1;
mlfq[p->pid].lev = -1;
mlfq_lev[level].size -= 1;
/*cprintf("size is %d [out]\n",mlfq_lev[level].size);*/
if(mlfq_lev[level].next == &mlfq[p->pid]) //각 레벨의 큐의 첫번째 항목이었다면,
 mlfq_lev[level].next = mlfq[p->pid].next;
if(mlfq_lev[level].size == 0)
 mlfq_lev[level].end = 0;

if(&mlfq[p->pid] == mlfq_lev[level].end){
 mlfq_lev[level].end = mlfq[p->pid].prev;
}

if(mlfq[p->pid].prev != 0)
 mlfq[p->pid].prev -> next = mlfq[p->pid].next;

mlfq[p->pid].next = 0; //end 만들어야
/*cprintf("  out  ended   ");*/
}

struct proc *
MLFQ(void)
{
tick_mlfq = ticks - tick_mlfq;
for(int i = 0; i < 3; i++){
/*cprintf("   MLFQ operated    \n");*/

/*cprintf("   first, mlfq_lev[%d].cur",i);*/
/*if(mlfq_lev[i].cur == 0)
  cprintf(" is 0\n");
else
  cprintf(" [%d] \n",mlfq_lev[i].cur -> proc -> pid);*/

if(mlfq_lev[i].cur == 0){
  if(mlfq_lev[i].next == 0)
    continue;
  mlfq_lev[i].cur = mlfq_lev[i].next;
 }

if(mlfq_lev[i].size > 0){
 /*cprintf("   MLFQ check...    ");*/
  cprintf("[lev %d]size:%d\n",i,mlfq_lev[i].size);
 if((mlfq_lev[i].cur)->proc->state == ZOMBIE){
  /*MLFQ_out(x->proc,i);
  cprintf("[0]size:%d\n",mlfq_lev[i].size);
  i -= 1;*/
 }
 else if(stride_table[mlfq_lev[i].cur->proc->pid].is_stride == 1){
  /*cprintf("\n\n[stride]pid[%d],ticks[%d] is out from MLFQ\n\n",mlfq_lev[i].cur->proc->pid,mlfq_lev[i].cur->sum_tick);
  struct mlfq *x = mlfq_lev[i].cur;
  mlfq_lev[i].cur = mlfq_lev[i].cur -> next;
  MLFQ_out(x->proc,i);
  i -= 1;*/
}
 else if((mlfq_lev[i].cur)->sum_tick >= 10){
  cprintf("\n\n[tick over]pid[%d],ticks[%d] is out from MLFQ[%d]\n\n",mlfq_lev[i].cur->proc->pid,mlfq_lev[i].cur->sum_tick,i);
  if(i != 2){
  struct mlfq *x = mlfq_lev[i].cur;
  /*mlfq_lev[i].cur = mlfq_lev[i].cur -> next;*/
  MLFQ_out(x->proc,i);
  MLFQ_in(x->proc,i+1);
   i -= 1;
  }
  if(i == 2)
   return (mlfq_lev[i].cur)->proc;
 }
 else{
} // 지금까지 실행한 process들의 상태를 보고 해석한 if문
 
 if(mlfq_lev[i].cur == 0){
   if(mlfq_lev[i].next == 0)
     continue;
   mlfq_lev[i].cur = mlfq_lev[i].next;
   cprintf("adfaewf\n");
 } // 현재 가리키고 있는 프로세스가 없으면, 큐의 첫번째 부분을 가리키고 그 마저 없으면 다음 큐로 넘어간다.

 if(mlfq_lev[i].cur->proc->state == SLEEPING){
  if(mlfq_lev[i].cur -> next != 0)
   mlfq_lev[i].cur = mlfq_lev[i].cur -> next;
  else{
   if(mlfq_lev[i].next != mlfq_lev[i].cur)
    mlfq_lev[i].cur = mlfq_lev[i].next;
   else{
      continue;
   }
  }
}

/* (mlfq_lev[i].next)->proc->state = RUNNABLE;*/
  if(mlfq_lev[i].size > 1 && (mlfq_lev[i].cur -> run == 1)){
     /*cprintf("It already run...   ");*/
     mlfq_lev[i].cur -> run = 0;
     if(mlfq_lev[i].cur -> next != 0){
      mlfq_lev[i].cur = mlfq_lev[i].cur -> next;
      /*cprintf("I selected the next process..     ");*/
     }
     else{
      mlfq_lev[i].cur = mlfq_lev[i].next;
      /*cprintf("I selected the first process....    ");*/
     }
  } // 
  if(mlfq_lev[i].size == 1){ // size is just one, next one is just first array.
   mlfq_lev[i].cur -> run = 0;
   mlfq_lev[i].cur = mlfq_lev[i].next;
  }
  /*if(i == 0)
   cprintf("ffufufuf\n");*/
  cprintf("[1]mlfq_lev[%d].cur is ",i);
  if(mlfq_lev[i].cur == 0)
   cprintf("...0\n");
  else
   cprintf(" [%d]\n",mlfq_lev[i].cur -> proc -> pid);
  mlfq_lev[i].cur -> run = 1;
  /*cprintf("return.......");*/
  return (mlfq_lev[i].cur)->proc;
  
}
}
cprintf("this is not....\n");
return 0;
}

struct proc *
stride(struct proc * p)
{

  if(stride_table[p->pid].is_stride != 1){
      if(mlfq[p->pid].is_mlfq == 0){
       MLFQ_in(p,0); // case of new process, if so, new process go to MLFQ.
       cprintf("\n\ni'm mlfq[%d]\n\n",p->pid);
       MLFQ();
       /*p = stride();*/
      }
  } // after finish MLFQ, all the if gone(except pid !=2)

  int minimum = 2147483647;
  int destination = 0;
  if(table_size == 0){
   struct proc * p;
   p = MLFQ();
   cprintf("[[[%d]]]",p->state);
   if(p != 0)
    return p;
   else
    return stride_table[2].proc;
  }
for(int i = 0; i < 3; i++){
 if(mlfq_lev[i].size > 0){
  if(mlfq_lev[i].cur == 0)
    cprintf("100 error!\n");
  if(stride_table[mlfq_lev[i].cur->proc->pid].is_stride == 1){
   cprintf("\n\n[stride]pid[%d],ticks[%d] is out from MLFQ\n\n",mlfq_lev[i].cur->proc->pid,mlfq_lev[i].cur->sum_tick);
   struct mlfq *x = mlfq_lev[i].cur;
   /*mlfq_lev[i].cur = mlfq_lev[i].cur -> next;*/
   MLFQ_out(x->proc,i);
  }
 }
}

while(1){

  minimum = 2147483647;
  for(int i = 0; i < max_pid+1; i++){
   if(i == 1 || i == 2) {
    continue;
   }
   if(stride_table[i].full ==1){
    if(stride_table[i].path < minimum){
     minimum = stride_table[i].path;
     destination = i;
    }
   }
  }
 if(destination == 0){
  stride_table[destination].path += (10000/stride_table[destination].share);
  struct proc * ret = MLFQ();
  cprintf("In, [%d], path is [%d], share is [%d]\n",destination,stride_table[destination].path,stride_table[destination].share);
  if(ret != 0){
   cprintf("[[[%d]]]",p->state);
   return ret;
   }
  else
   continue;
 }

 break;

}
 stride_table[destination].path += (10000/stride_table[destination].share);
 cprintf("not mlfq,In, [%d], path is [%d]\n",destination,stride_table[destination].path);

 return stride_table[destination].proc;
 
}

void set_table(int input){
 if(max_pid > NSTRIDE-1){
  cprintf("don't set stride!\n");
  return ;
 }
 max_sum += input;
 if(max_sum > 80){
  cprintf("[percentage over]you can't use stride\n");
  max_sum -= input;
  return ;
 } 
 
 table_size++;
 cprintf("table_size:%d\n",table_size);
 if (max_pid < myproc() -> pid)
  max_pid = myproc() -> pid;
 stride_table[0].share = 100 - max_sum;
 stride_table[myproc() -> pid].share = input;
 stride_table[myproc() -> pid].pid = myproc() -> pid;
 stride_table[myproc() -> pid].full = 1;
 stride_table[myproc() -> pid].path = 0;
 stride_table[myproc() -> pid].proc = myproc();
 stride_table[myproc() -> pid].is_stride = 1;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p/*, *q*/;
  struct cpu *c = mycpu();
  c->proc = 0;
 
 stride_table[0].share = 100;
 stride_table[0].pid = 2;
 stride_table[0].full = 1;

  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
     if(/*stride_table[p->pid].is_stride == 1*/p -> pid > 2 && (p -> state == RUNNABLE)){
      p = stride(p);
      /*cprintf("[[[%d]]]\n",p->state);*/
    }
     /*if (p-> pid != 1 && p -> pid != 2 && (p -> state == 2)){
      p->state = RUNNABLE;
      p->chan = 0;
    }*/
     if(p->state != RUNNABLE) {
      continue;
     }
     if(p -> pid == 2)
      stride_table[2].proc = p;


    /*if( stride_table[p->pid].is_stride != 1 ){
     stride_table[p->pid].is_stride = 2;
    }*/

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
     c->proc = p;
     switchuvm(p);
     p->state = RUNNING;

     cprintf("%d\n",stride_table[0].share);
     if(mlfq[p->pid].is_mlfq==1){ //if p is mlfq program
       mlfq[p->pid].my_tick = ticks;
     }

     swtch(&(c->scheduler), p->context);
     switchkvm();
     mlfq[p->pid].sum_tick = mlfq[p->pid].sum_tick + (ticks - mlfq[p->pid].my_tick);
    if(p->pid != 1) 
      cprintf("consumes, pid:[%d],ticks[%d].\n",p->pid,mlfq[p->pid].sum_tick);
     if(p -> state == ZOMBIE){
	if(stride_table[p->pid].is_stride == 1){
        stride_table[p->pid].full = 0;
	stride_table[p->pid].is_stride = 0;
	max_sum -= stride_table[p->pid].share;
	table_size -= 1;
        if(max_pid > 80)
         cprintf("\n\n\n\n\n\n\n\n\n");
 	stride_table[0].share = 100 - max_pid;
        }
        if(mlfq[p->pid].is_mlfq == 1){
         cprintf("\n\n[Process end]pid[%d],ticks[%d] is out from MLFQ\n\n",p->pid,mlfq[p->pid].sum_tick);
         struct mlfq *x = mlfq_lev[mlfq[p->pid].lev].cur;
         mlfq_lev[mlfq[p->pid].lev].cur = mlfq_lev[mlfq[p->pid].lev].cur -> next;
         MLFQ_out(x->proc,mlfq[p->pid].lev);
       }
      }
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
      release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
int
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
  return 0;
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
