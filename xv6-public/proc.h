// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

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

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct thread {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  int tid;                     // Thread ID
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int share;		       // used in stride scheduling
  int thread[100];             // thread per one process(total 100)
  int thread_p[100];		// thread(including tid) list
  uint tid;		       // tid of one theread that process made
  uint osz;			// original size of process(except stack)
  uint mtid;			// max tid of process(if stack is mapping on 1, 3, 9 then 9 is the max size)
  /*uint mapno;*/			// mapping number of thread 
  void* retval;		       // return value of thread
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

int thread_create_os(thread_t* thread, void*(*start_routine)(void*),void * arg);
int thread_join_os(thread_t thread, void** retval, int select, thread_t original_thread);
void thread_exit_os(void *retval, thread_t thread);
