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
int max_pid; //the largest pid number.
int max_sum;
int tick_mlfq; //used for MLFQ to evalute its tick.

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);


struct {
 int able_tick;
 int rr_tick_limit;
 int rr_tick;
 int size;
 struct mlfq * next;
 struct mlfq * head;
 struct mlfq * end;
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
MLFQ_in(struct proc * p, int level,int code)
{

if(code == 0){
 mlfq[p->pid].my_tick = 0;
 mlfq[p->pid].sum_tick = 0;
 mlfq[p->pid].is_mlfq = 1;
 mlfq[p->pid].lev = level;
 mlfq[p->pid].proc = p;
}

 if(mlfq_lev[level].size == 0){
   mlfq[p->pid].prev = &mlfq[p->pid];
   mlfq[p->pid].next = &mlfq[p->pid];
   mlfq_lev[level].end = &mlfq[p->pid];
   mlfq_lev[level].head = &mlfq[p->pid];
 }

 else{
  mlfq[p->pid].prev = mlfq_lev[level].end;
  mlfq[p->pid].next = mlfq_lev[level].head;
  mlfq_lev[level].head -> prev = &mlfq[p->pid];
  mlfq_lev[level].end -> next = &mlfq[p->pid];
  mlfq_lev[level].end = &mlfq[p->pid];
 }
 mlfq_lev[level].size++;
}

void
MLFQ_out(struct proc * p, int level, int code)
{
	if(mlfq_lev[level].size <= 0){
 	panic("MLFQ OUT ERROR");
	}
	else if (mlfq_lev[level].size == 1){
   	 mlfq_lev[level].end = 0;
     	 mlfq_lev[level].head = 0;
	}
	else{
 	mlfq_lev[level].head = mlfq_lev[level].head -> next;
 	mlfq_lev[level].end -> next = mlfq_lev[level].head;
 	mlfq_lev[level].head -> prev = mlfq_lev[level].end;
	}

	if(code == 0){
	mlfq[p->pid].my_tick = 0;
	mlfq[p->pid].sum_tick = 0;
	mlfq[p->pid].next = 0;
	mlfq[p->pid].prev = 0;
	mlfq[p->pid].is_mlfq = -1;
	mlfq[p->pid].lev = -1;
        mlfq[p->pid].run = 0;
	mlfq_lev[level].size = mlfq_lev[level].size - 1;
        }

	else{
	 mlfq_lev[level].size -= 1;
	 mlfq[p->pid].next = 0;
	 mlfq[p->pid].prev = 0;
	 MLFQ_in(p,level,1);
	}

}
struct proc *
MLFQ(void)
{
 tick_mlfq++;
 if(tick_mlfq % 100 == 0){
  for(int i = 1; i < 3; i++){
   if(mlfq_lev[i].size <= 0){
    continue;
   }
   else{
   for(int p = 0; p < mlfq_lev[i].size; p++){
     mlfq_lev[i].head -> run = 0;
     struct proc * p = mlfq_lev[i].head->proc;
     MLFQ_out(mlfq_lev[i].head->proc,i,0);
     MLFQ_in(p,0,0);
   }
  }
  }
 }
 for(int i = 0; i < 3; i++){
  if(mlfq_lev[i].size > 0){ //한 큐의 내용이 하나 이상인 경우,
   }
  else
   continue;
  for(int p = 0; p < mlfq_lev[i].size; p++){
   if(mlfq_lev[i].head->proc->state != RUNNABLE){
    mlfq_lev[i].head -> run = 0;
    MLFQ_out(mlfq_lev[i].head->proc,i,1);
   }
  }
  if(mlfq_lev[i].head->proc->state != RUNNABLE){
   continue;
  }

  if(mlfq_lev[i].size > 1 && (mlfq_lev[i].head -> run == 1) && mlfq_lev[i].rr_tick >=  mlfq_lev[i].rr_tick_limit){ 
//한 큐의 내용이 두 개 이상이고, 돌아간 적이 있는 프로세스이며, time quantum이 지난 경우.
   mlfq_lev[i].head -> run = 0;
   mlfq_lev[i].rr_tick = 0;
   MLFQ_out(mlfq_lev[i].head->proc,i,1);
  } //다음 프로세스가 있다면 그 쪽으로 작동 권한을 준다.
  else{
   mlfq_lev[i].head -> run = 0;
  } //다음 프로세스가 없거나, time quantum이 남아있거나 실행되지 않은 프로세스는  다시 한번 더  실행한다.
  mlfq_lev[i].head -> run = 1;

  mlfq_lev[i].rr_tick++;
  return (mlfq_lev[i].head)->proc; 
 }
  // MLFQ에 어떤 프로세스도 없는 경우.
 return 0;
}


struct proc *
stride(int destination)
{
 if(stride_table[destination].share < 0)
  panic("no!");
 stride_table[destination].path += (10000/stride_table[destination].share);

 return stride_table[destination].proc;
}


int path_cal(void)
{
  int destination = 0;
  int minimum = 2147483647;
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
  
 return destination;
}

void set_table(int input){
 if(max_pid > NSTRIDE-1){
  cprintf("don't set stride!\n");
  return ;
 }
 if(max_sum + input > 80){
  return ;
 } 
 max_sum = max_sum + input; 
 table_size++;
 if (max_pid < myproc() -> pid)
  max_pid = myproc() -> pid;
 stride_table[0].share = 100 - max_sum;
 stride_table[myproc() -> pid].share = input;
 stride_table[myproc() -> pid].pid = myproc() -> pid;
 stride_table[myproc() -> pid].full = 1;
 stride_table[myproc() -> pid].path = 0;
 stride_table[myproc() -> pid].proc = myproc();
 stride_table[myproc() -> pid].is_stride = 1;
 
for(int i = 0; i < max_pid; i++)
  stride_table[i].path = 0;

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
  struct proc *p, *q;
  struct cpu *c = mycpu();
  c->proc = 0;
  int result; 

 stride_table[0].share = 100;
 stride_table[0].pid = 2;
 stride_table[0].full = 1;

 mlfq_lev[0].able_tick = 5;
 mlfq_lev[1].able_tick = 10;
 
 mlfq_lev[0].rr_tick_limit = 1;
 mlfq_lev[0].rr_tick_limit = 2;
 mlfq_lev[0].rr_tick_limit = 4;

  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
     if(p->state != RUNNABLE) {
      continue;
     }
      if(mlfq[p->pid].is_mlfq == 0 && stride_table[p->pid].is_stride == 0 && p->pid > 2){
       MLFQ_in(p,0,0); // case of new process, if so, new process go to MLFQ.
      }

     result = path_cal();


     if((result != 0) && (p -> pid) > 2){
      p = stride(result);
    }

     if((result == 0) && (p -> pid) > 2){
       q = stride(result);
       q = MLFQ();
       if(q != 0){
         p = q;
       }
     }
     if(p->state != RUNNABLE) {
      continue;
     }
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
     c->proc = p;
     switchuvm(p);
     p->state = RUNNING;

     mlfq[p->pid].my_tick = ticks;
     swtch(&(c->scheduler), p->context);
     switchkvm();
     mlfq[p->pid].sum_tick = mlfq[p->pid].sum_tick + (ticks - mlfq[p->pid].my_tick);
    
    if(p->pid != 1){ 
    }

    if(p -> state == ZOMBIE){
     if(stride_table[p->pid].is_stride == 1){
      stride_table[p->pid].full = 0;
      stride_table[p->pid].is_stride = 0;
      max_sum = max_sum - stride_table[p->pid].share;
      table_size -= 1;
      stride_table[0].share = 100 - max_sum;
     }
      
      if(mlfq[p->pid].is_mlfq ==1){
      MLFQ_out(p,mlfq[p->pid].lev,0);
      }

     
    }
   //여기까지, p가 종료된 경우.

   else if(mlfq[p->pid].is_mlfq == 1 && stride_table[p->pid].is_stride == 1){
    MLFQ_out(p,mlfq[p->pid].lev,0);
   }
   //여기까지, p가 stride를 선언한 경우

  else if(mlfq[p->pid].is_mlfq == 1 && mlfq[p->pid].sum_tick >= mlfq_lev[mlfq[p->pid].lev].able_tick){
  if(mlfq[p->pid].lev != 2){
   int x = mlfq[p->pid].lev;
   MLFQ_out(p,x,0);
   MLFQ_in(p,x+1,0);
  }
 }
 //여기까지, p의 time slice가 지난 경우.
  
  else{
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

void threadret(void){
 cprintf("threadret : done\n");
}

int thread_create_os(thread_t* thread, void*(*start_routine)(void *),void * arg){
	
	struct proc * p = myproc();
	struct proc * np = allocproc();

 	uint sz,ustack[5];
	uint sp;	
	if(np == 0){
		return -1;
	}

	*thread = np->pid;

	*np -> tf = *p -> tf;
	np->sz = p->sz;
	np->parent = p;
        for(int i = 0; i < NOFILE; i++)
         if(p->ofile[i])
          np->ofile[i] = filedup(p->ofile[i]);
        np->cwd = idup(p->cwd);
        safestrcpy(np->name, p->name, sizeof(p->name));
       //커널 스택 할당 끝.

	cprintf("p->sz:%d\n",p->sz);
	sz = PGROUNDUP(p->sz);
	if((sz = allocuvm(p->pgdir, sz, sz + 2*PGSIZE)) == 0){
	 cprintf("allocuvm: error!\n");
	 return -1;
	}
	clearpteu(p->pgdir, (char*)(sz - 2*PGSIZE));
	sp = sz;

	sp = (sp - 4) & ~3;
	if(copyout(p->pgdir, sp, arg, 4) < 0) {
	 cprintf("copyout: error!\n");
	 return -1;
	}

/*
	ustack[3] = sp;
	ustack[4] = 0;
*/
	ustack[0] = 0xffffffff;  // fake return PC
	ustack[1] = (uint)arg; //이게 arg?
	ustack[2] = 0;
/*	ustack[2] = sp - (2*4);  // argv pointer
*/
	sp -= 3 * 4;
	if(copyout(p->pgdir, sp, ustack, 3*4) < 0) {
	 cprintf("copuout[2]: error!\n");
	 return -1;
	}

	cprintf("[%p]sp:%p and %d\n",(int*)sp,*(int*)(sp),*(int*)sp);
	cprintf("[%p]sp+4(argc):%p and %d\n",(int*)(sp+4),*(int*)(sp+4),*(int*)(sp+4));
	cprintf("[%p]sp+8(addr of addr of arg0):%p and %d\n",(int*)(sp+8),*(int*)(sp+8),*(int*)(sp+8));

	np -> pgdir = p -> pgdir;
	np->sz = sz;
	p -> sz = sz;
	np-> tf -> eip = (uint)start_routine;
	np -> tf -> esp = sp;

	//need to switch for this thread
	acquire(&ptable.lock);
	np -> state = RUNNABLE;
	release(&ptable.lock);
	/*switchuvm(np);*/
	cprintf("thread_create:end\n");
	return 0;
}

int thread_join(thread_t* thread, void ** retval){
  
          cprintf("thread_join : clear\n");
          return 0;
}

void thread_exit(void *retval){
  
          cprintf("thread_exit : clear\n");
}

