#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#ifdef CS333_P2
#include "uproc.h"
#endif

static char *states[] = {
    [UNUSED] "unused",
    [EMBRYO] "embryo",
    [SLEEPING] "sleep ",
    [RUNNABLE] "runnable",
    [RUNNING] "run   ",
    [ZOMBIE] "zombie"};

#ifdef CS333_P3
// record with head and tail pointer for constant-time access to the beginning
// and end of a linked list of struct procs.  use with stateListAdd() and
// stateListRemove().
struct ptrs
{
  struct proc *head;
  struct proc *tail;
};
#endif

static struct
{
#define statecount NELEM(states)
  struct spinlock lock;
  struct proc proc[NPROC];
#ifdef CS333_P3
  struct ptrs list[statecount];
#endif
#ifdef CS333_P4
  struct ptrs ready[MAXPRIO + 1];
  uint PromoteAtTime;
#endif
} ptable;

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs *, struct proc *);
static int stateListRemove(struct ptrs *, struct proc *p);
static void assertState(struct proc *, enum procstate, const char *, int);
#endif

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
    {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
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
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;
  acquire(&ptable.lock);
  int found = 0;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
    {
      found = 1;
      break;
    }
  if (!found)
  {
    release(&ptable.lock);
    return 0;
  }
#ifdef CS333_P3
  if (stateListRemove(&ptable.list[UNUSED], p) == -1)
    panic("Error occur when remove p from the list UNUSED");
  assertState(p, UNUSED, __FILE__, __LINE__);
#endif
  p->state = EMBRYO;
#ifdef CS333_P3
  stateListAdd(&ptable.list[EMBRYO], p);
  assertState(p, EMBRYO, __FILE__, __LINE__);
#endif
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
#ifdef CS333_P3
    acquire(&ptable.lock);
    if (stateListRemove(&ptable.list[EMBRYO], p) == -1)
      panic("Error occur when remove p from the list EMBRYO");
    assertState(p, EMBRYO, __FILE__, __LINE__);
#endif
    p->state = UNUSED;
#ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], p);
    assertState(p, UNUSED, __FILE__, __LINE__);
    release(&ptable.lock);
#endif
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  p->start_ticks = ticks;
#ifdef CS333_P2
  p->cpu_ticks_in = 0;
  p->cpu_ticks_total = 0;
#endif
#ifdef CS333_P4
  p->priority = MAXPRIO;
  p->budget = DEFAULT_BUDGET;
#endif
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
#ifdef CS333_P3
  acquire(&ptable.lock);
  initProcessLists();
  initFreeList();
#ifdef CS333_P4
  ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
#endif
  release(&ptable.lock);
#endif
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
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
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
#ifdef CS333_P2
  p->parent = 0;
  p->uid = 0;
  p->gid = 0;
#endif
  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
#ifndef CS333_P3
  acquire(&ptable.lock);
  p->state = RUNNABLE;
  release(&ptable.lock);
#else
  acquire(&ptable.lock);
  if (stateListRemove(&ptable.list[EMBRYO], p) == -1)
    panic("Error occur when remove p from the list EMBRYO");
  assertState(p, EMBRYO, __FILE__, __LINE__);
  p->state = RUNNABLE;
#if defined(CS333_P4)
  stateListAdd(&ptable.ready[p->priority], p);
  assertState(p, RUNNABLE, __FILE__, __LINE__);
#elif defined(CS333_P3)
  stateListAdd(&ptable.list[RUNNABLE], p);
  assertState(p, RUNNABLE, __FILE__, __LINE__);
#endif
  release(&ptable.lock);
#endif
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
#ifdef CS333_P3
    acquire(&ptable.lock);
    if (stateListRemove(&ptable.list[EMBRYO], np) == -1)
      panic("Error remove from EMBRYO list EMBRYO");
    assertState(np, EMBRYO, __FILE__, __LINE__);
#endif
    np->state = UNUSED;
#ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], np);
    assertState(np, UNUSED, __FILE__, __LINE__);
    release(&ptable.lock);
#endif
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
#ifdef CS333_P2
  np->uid = curproc->uid;
  np->gid = curproc->gid;
#endif
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
#ifdef CS333_P3
  if (stateListRemove(&ptable.list[EMBRYO], np) == -1)
    panic("Error remove from EMBRYO list");
  assertState(np, EMBRYO, __FILE__, __LINE__);
#endif
  np->state = RUNNABLE;
#if defined(CS333_P4)
  stateListAdd(&ptable.ready[np->priority], np);
  assertState(np, RUNNABLE, __FILE__, __LINE__);
#elif defined(CS333_P3)
  stateListAdd(&ptable.list[RUNNABLE], np);
  assertState(np, RUNNABLE, __FILE__, __LINE__);
#endif
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifndef CS333_P3
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
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
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#else
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
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
#if defined(CS333_P4)
  for (int i = 0; i < MAXPRIO + 1; i++)
  {
    for (p = ptable.ready[i].head; p; p = p->next)
    {
      if (p->parent == curproc)
        p->parent = initproc;
    }
  }
#elif defined(CS333_P3)
  for (p = ptable.list[RUNNABLE].head; p; p = p->next)
  {
    if (p->parent == curproc)
      p->parent = initproc;
  }
#endif
  for (p = ptable.list[RUNNING].head; p; p = p->next)
  {
    if (p->parent == curproc)
      p->parent = initproc;
  }
  for (p = ptable.list[SLEEPING].head; p; p = p->next)
  {
    if (p->parent == curproc)
      p->parent = initproc;
  }
  for (p = ptable.list[ZOMBIE].head; p; p = p->next)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      wakeup1(initproc);
    }
  }

  if (stateListRemove(&ptable.list[RUNNING], curproc) == -1)
    panic("Error occur when remove p from the list RUNNING");
  assertState(curproc, RUNNING, __FILE__, __LINE__);
  curproc->state = ZOMBIE;
  stateListAdd(&ptable.list[ZOMBIE], curproc);
  assertState(curproc, ZOMBIE, __FILE__, __LINE__);
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifndef CS333_P3
int wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
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
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}
#else
int wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
#if defined(CS333_P4)
    int i;
    for (i = 0; i < MAXPRIO + 1; i++)
    {
      for (p = ptable.ready[i].head; p; p = p->next)
      {
        if (p->parent != curproc)
          continue;
        havekids = 1;
      }
      if (havekids)
        break;
    }
#elif defined(CS333_P3)
    for (p = ptable.list[RUNNABLE].head; p; p = p->next)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
    }
    for (p = ptable.list[SLEEPING].head; p; p = p->next)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
    }
#endif
    for (p = ptable.list[RUNNING].head; p; p = p->next)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
    }
    for (p = ptable.list[ZOMBIE].head; p; p = p->next)
    {
      if (p->parent != curproc)
      {
        continue;
      }
      else
      {
        havekids = 1;
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        if (stateListRemove(&ptable.list[ZOMBIE], p) == -1)
          panic("Error occur when remove p from the list ZOMBIE");
        assertState(p, ZOMBIE, __FILE__, __LINE__);
        p->state = UNUSED;
        stateListAdd(&ptable.list[UNUSED], p);
        assertState(p, UNUSED, __FILE__, __LINE__);
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}
#endif
//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifndef CS333_P3
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle; // for checking if processor is idle
#endif      // PDX_XV6

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1; // assume idle unless we schedule a process
#endif        // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
#ifdef PDX_XV6
      idle = 0; // not idle this timeslice
#endif          // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
#ifdef CS333_P2
      p->cpu_ticks_in = ticks; // check in when process run in cpu
#endif
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle)
    {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#else
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle; // for checking if processor is idle
#endif // PDX_XV6
  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1; // assume idle unless we schedule a process
#endif // PDX_XV6
    acquire(&ptable.lock);
#ifdef CS333_P4
    struct proc *next;
    if (ticks >= ptable.PromoteAtTime && MAXPRIO)
    {
      for (int i = MAXPRIO; i >= 0; i--)
      {
        p = ptable.ready[i].head;
        while (p)
        {
          if (i == MAXPRIO)
          {
            p->budget = DEFAULT_BUDGET;
            p = p->next;
          }
          else
          {
            next = p->next;
            stateListRemove(&ptable.ready[p->priority], p);
            assertState(p, RUNNABLE, __FILE__, __LINE__);
            p->priority += 1;
            stateListAdd(&ptable.ready[p->priority], p);
            assertState(p, RUNNABLE, __FILE__, __LINE__);
            p->budget = DEFAULT_BUDGET;
            p = next;
          }
        }
      }
      for (p = ptable.list[SLEEPING].head; p; p = p->next)
      {
        if (p->priority == MAXPRIO)
        {
          p->budget = DEFAULT_BUDGET;
        }
        else
        {
          p->priority += 1;
          p->budget = DEFAULT_BUDGET;
        }
      }
      for (p = ptable.list[RUNNING].head; p; p = p->next)
      {
        if (p->priority == MAXPRIO)
        {
          p->budget = DEFAULT_BUDGET;
        }
        else
        {
          p->priority += 1;
          p->budget = DEFAULT_BUDGET;
        }
      }
      cprintf("Promoted all jobs!\n");
      ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
    }
#endif

#if defined(CS333_P4)
    int i;
    for (i = MAXPRIO; i >= 0; i--)
    {
      if (ptable.ready[i].head)
      {
#ifdef PDX_XV6
        idle = 0; // not idle this timeslice
#endif // PDX_XV6
        p = ptable.ready[i].head;
        c->proc = p;
        switchuvm(p);
        stateListRemove(&ptable.ready[p->priority], p);
        assertState(p, RUNNABLE, __FILE__, __LINE__);
        p->state = RUNNING;
        stateListAdd(&ptable.list[RUNNING], p);
        assertState(p, RUNNING, __FILE__, __LINE__);
#ifdef CS333_P2
        p->cpu_ticks_in = ticks; // check in when process run in cpu
#endif
        swtch(&(c->scheduler), p->context);
        switchkvm();
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        break; //done select the highest priority process, get out
      }
    }
#elif defined(CS333_P3)
    for (p = ptable.list[RUNNABLE].head; p; p = p->next)
    {
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0; // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      if (stateListRemove(&ptable.list[RUNNABLE], p) == -1)
        panic("Error occur when remove p from the list RUNNABLE");
      assertState(p, RUNNABLE, __FILE__, __LINE__);
      p->state = RUNNING;
      stateListAdd(&ptable.list[RUNNING], p);
      assertState(p, RUNNING, __FILE__, __LINE__);
#ifdef CS333_P2
      p->cpu_ticks_in = ticks; // check in when process run in cpu
#endif
      swtch(&(c->scheduler), p->context);
      switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
#endif
    release(&ptable.lock);

#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle)
    {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif
// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.

void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
#ifdef CS333_P2
  p->cpu_ticks_total += (ticks - p->cpu_ticks_in); // total = total + current tick - time when enter cpu
#endif
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifndef CS333_P3
void yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock); //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#else
void yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock); //DOC: yieldlock
  if (stateListRemove(&ptable.list[RUNNING], curproc) == -1)
    panic("Error occur when remove p from the list RUNNING");
  assertState(curproc, RUNNING, __FILE__, __LINE__);
  curproc->state = RUNNABLE;

#if defined(CS333_P4)
  stateListAdd(&ptable.ready[curproc->priority], curproc);
  assertState(curproc, RUNNABLE, __FILE__, __LINE__);
  if (MAXPRIO)
  {
    curproc->budget -= (ticks - curproc->cpu_ticks_in);
    if ((curproc->budget <= 0) && (curproc->priority != 0))
    {
      stateListRemove(&ptable.ready[curproc->priority], curproc);
      assertState(curproc, RUNNABLE, __FILE__, __LINE__);
      curproc->priority -= 1;
      stateListAdd(&ptable.ready[curproc->priority], curproc);
      assertState(curproc, RUNNABLE, __FILE__, __LINE__);
      curproc->budget = DEFAULT_BUDGET;
    }
  }
#elif defined(CS333_P3)
  stateListAdd(&ptable.list[RUNNABLE], curproc);
  assertState(curproc, RUNNABLE, __FILE__, __LINE__);
#endif
  sched();
  release(&ptable.lock);
}
#endif
// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
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
#ifndef CS333_P3
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    if (lk)
      release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
    release(&ptable.lock);
    if (lk)
      acquire(lk);
  }
}
#else
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    if (lk)
      release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  if (stateListRemove(&ptable.list[RUNNING], p) == -1)
    panic("Error occur when remove p from the list RUNNING");
  assertState(p, RUNNING, __FILE__, __LINE__);

#ifdef CS333_P4
  if (MAXPRIO)
  {
    p->budget -= (ticks - p->cpu_ticks_in);
    if ((p->budget <= 0) && (p->priority != 0))
    {
      p->priority -= 1;
      p->budget = DEFAULT_BUDGET;
    }
  }
#endif
  p->state = SLEEPING;
  stateListAdd(&ptable.list[SLEEPING], p);
  assertState(p, SLEEPING, __FILE__, __LINE__);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
    release(&ptable.lock);
    if (lk)
      acquire(lk);
  }
}
#endif
//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifndef CS333_P3
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#else
static void wakeup1(void *chan)
{
  struct proc *p;
  struct proc *next;
  p = ptable.list[SLEEPING].head;
  while (p)
  {
    next = p->next;
    if (p->chan == chan)
    {
      if (stateListRemove(&ptable.list[SLEEPING], p) == -1)
        panic("Error occur when remove p from the list SLEEPING");
      assertState(p, SLEEPING, __FILE__, __LINE__);
      p->state = RUNNABLE;
#if defined(CS333_P4)
      stateListAdd(&ptable.ready[p->priority], p);
      assertState(p, RUNNABLE, __FILE__, __LINE__);
#elif defined(CS333_P3)
      stateListAdd(&ptable.list[RUNNABLE], p);
      assertState(p, RUNNABLE, __FILE__, __LINE__);
#endif
    }
    p = next;
  }
}
#endif
// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}
// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
#ifndef CS333_P3
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#else
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.list[SLEEPING].head; p; p = p->next)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      if (stateListRemove(&ptable.list[SLEEPING], p) == -1)
        panic("Error occur when remove p from the list SLEEPING");
      assertState(p, SLEEPING, __FILE__, __LINE__);
      p->state = RUNNABLE;
#if defined(CS333_P4)
      stateListAdd(&ptable.ready[p->priority], p);
      assertState(p, RUNNABLE, __FILE__, __LINE__);
#elif defined(CS333_P3)
      stateListAdd(&ptable.list[RUNNABLE], p);
      assertState(p, RUNNABLE, __FILE__, __LINE__);
#endif
      release(&ptable.lock);
      return 0;
    }
  }
#if defined(CS333_P4)
  for (int i = 0; i < MAXPRIO + 1; i++)
  {
    for (p = ptable.ready[i].head; p; p = p->next)
    {
      if (p->pid == pid)
      {
        p->killed = 1;
        release(&ptable.lock);
        return 0;
      }
    }
  }
#elif defined(CS333_P3)
  for (p = ptable.list[RUNNABLE].head; p; p = p->next)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
    }
    release(&ptable.lock);
    return 0;
  }
#endif
  for (p = ptable.list[RUNNING].head; p; p = p->next)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
    }
    release(&ptable.lock);
    return 0;
  }
  release(&ptable.lock);
  return -1;
}
#endif

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

//#endif
#if defined(CS333_P4)
void procdumpP4(struct proc *p, char *state_string)
{
  int MAXNAME = 13;
  uint zero;
  char *eslaped = "", *cpu_time = "";
  zero = (ticks - p->start_ticks) % 1000;
  if (zero < 100 && zero >= 10)
    eslaped = "0";
  else if (zero < 10)
    eslaped = "00";
  zero = p->cpu_ticks_total % 1000;
  if (zero < 100 && zero >= 10)
    cpu_time = "0";
  else if (zero < 10)
    cpu_time = "00";
  cprintf("%d\t%s", p->pid, p->name);
  int len = strlen(p->name);
  for (int i = len; i < MAXNAME; i++)
    cprintf(" ");
  cprintf("%d\t        %d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t", p->uid, p->gid, p->parent ? p->parent->pid : p->pid, p->priority, (ticks - p->start_ticks) / 1000, eslaped, (ticks - p->start_ticks) % 1000, p->cpu_ticks_total / 1000, cpu_time, p->cpu_ticks_total % 1000, state_string, p->sz);
  return;
}
void runnabledump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Ready List in Ready List Processes:\n");
  for (int i = MAXPRIO; i >= 0; i--)
  {
    cprintf("Priority level %d: ", i);
    for (p = ptable.ready[i].head; p; p = p->next)
    {
      assertState(p, RUNNABLE, __FILE__, __LINE__);
      cprintf("(%d,%d)", p->pid, p->budget);
      if (p->next)
        cprintf("->");
    }
    cprintf("\n");
  }
  cprintf("\n$");
  release(&ptable.lock);
}
void unuseddump(void)
{
  struct proc *p;
  int count = 0;
  acquire(&ptable.lock);
  for (p = ptable.list[UNUSED].head; p; p = p->next)
  {
    assertState(p, UNUSED, __FILE__, __LINE__);
    count++;
  }
  release(&ptable.lock);
  cprintf("Free List Size: %d processes\n$", count);
}
void sleepingdump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Sleeping List Processes:\n");
  for (p = ptable.list[SLEEPING].head; p; p = p->next)
  {
    assertState(p, SLEEPING, __FILE__, __LINE__);
    cprintf(" %d ", p->pid);
    if (p->next)
      cprintf("->");
  }
  cprintf("\n$");
  release(&ptable.lock);
}
void zombiedump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Zombie List Processes: \n");
  if (ptable.list[ZOMBIE].head == NULL)
  {
    cprintf("Zombie list is empty\n$");
    release(&ptable.lock);
    return;
  }
  for (p = ptable.list[ZOMBIE].head; p; p = p->next)
  {
    assertState(p, ZOMBIE, __FILE__, __LINE__);
    cprintf(" (%d, %d) ", p->pid, p->parent->pid ? p->parent->pid : p->pid);
    if (p->next)
      cprintf(" -> ");
  }
  release(&ptable.lock);
  cprintf("\n$");
}
#elif defined(CS333_P3)
void procdumpP3(struct proc *p, char *state_string)
{
  int MAXNAME = 10;
  uint zero;
  char *eslaped = "", *cpu_time = "";
  zero = (ticks - p->start_ticks) % 1000;
  if (zero < 100 && zero >= 10)
    eslaped = "0";
  else if (zero < 10)
    eslaped = "00";
  zero = p->cpu_ticks_total % 1000;
  if (zero < 100 && zero >= 10)
    cpu_time = "0";
  else if (zero < 10)
    cpu_time = "00";
  cprintf("%d\t%s", p->pid, p->name);
  int len = strlen(p->name);
  for (int i = len; i < MAXNAME; i++)
    cprintf(" ");
  cprintf(" %d\t        %d \t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t", p->uid, p->gid, p->parent ? p->parent->pid : p->pid, (ticks - p->start_ticks) / 1000, eslaped, (ticks - p->start_ticks) % 1000, p->cpu_ticks_total / 1000, cpu_time, p->cpu_ticks_total % 1000, state_string, p->sz);
  return;
}
void runnabledump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Ready List Processes:\n");
  if (ptable.list[RUNNABLE].head == NULL)
    cprintf("Ready List Processes is empty");
  else
    for (p = ptable.list[RUNNABLE].head; p; p = p->next)
    {
      assertState(p, RUNNABLE, __FILE__, __LINE__);
      cprintf(" %d ", p->pid);
      if (p->next)
        cprintf("->");
    }
  cprintf("\n$");
  release(&ptable.lock);
}
void unuseddump(void)
{
  struct proc *p;
  int count = 0;
  acquire(&ptable.lock);
  for (p = ptable.list[UNUSED].head; p; p = p->next)
  {
    assertState(p, UNUSED, __FILE__, __LINE__);
    count++;
  }
  release(&ptable.lock);
  cprintf("Free List Size: %d processes\n$", count);
}
void sleepingdump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Sleeping List Processes:\n");
  for (p = ptable.list[SLEEPING].head; p; p = p->next)
  {
    assertState(p, SLEEPING, __FILE__, __LINE__);
    cprintf(" %d ", p->pid);
    if (p->next)
      cprintf("->");
  }
  cprintf("\n$");
  release(&ptable.lock);
}
void zombiedump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Zombie List Processes: \n");
  if (ptable.list[ZOMBIE].head == NULL)
  {
    cprintf("Zombie list is empty\n$");
    release(&ptable.lock);
    return;
  }
  for (p = ptable.list[ZOMBIE].head; p; p = p->next)
  {
    assertState(p, ZOMBIE, __FILE__, __LINE__);
    cprintf(" (%d, %d) ", p->pid, p->parent->pid ? p->parent->pid : p->pid);
    if (p->next)
      cprintf(" -> ");
  }
  release(&ptable.lock);
  cprintf("\n$");
}
#elif defined(CS333_P2)
void procdumpP2(struct proc *p, char *state_string)
{
  int MAXNAME = 12;
  uint zero;
  char *eslaped = "", *cpu_time = "";
  zero = (ticks - p->start_ticks) % 1000;
  if (zero < 100 && zero >= 10)
    eslaped = "0";
  else if (zero < 10)
    eslaped = "00";
  zero = p->cpu_ticks_total % 1000;
  if (zero < 100 && zero >= 10)
    cpu_time = "0";
  else if (zero < 10)
    cpu_time = "00";
  cprintf("%d\t%s", p->pid, p->name);
  int len = strlen(p->name);
  for (int i = len; i < MAXNAME; i++)
    cprintf(" ");
  cprintf(" %d\t        %d \t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t", p->uid, p->gid, p->parent ? p->parent->pid : p->pid, (ticks - p->start_ticks) / 1000, eslaped, (ticks - p->start_ticks) % 1000, p->cpu_ticks_total / 1000, cpu_time, p->cpu_ticks_total % 1000, state_string, p->sz);
  return;
}

#elif defined(CS333_P1)
void procdumpP1(struct proc *p, char *state_string)
{
  int MAXNAME = 12;
  cprintf("%d\t%s", p->pid, p->name);
  int len = strlen(p->name);
  for (int i = len; i < MAXNAME; i++)
    cprintf(" ");
  cprintf("%d.%d\t%s\t%d\t", (ticks - p->start_ticks) / 1000, (ticks - p->start_ticks) % 1000, state_string, p->sz);
  return;
}
#endif

void procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P3)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName         Elapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

  cprintf(HEADER); // not conditionally compiled as must work in all project states

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

      // see TODOs above this function
#if defined(CS333_P4)
    procdumpP4(p, state);
#elif defined(CS333_P3)
    procdumpP3(p, state);
#elif defined(CS333_P2)
    procdumpP2(p, state);
#elif defined(CS333_P1)
    procdumpP1(p, state);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
#ifdef CS333_P1
  cprintf("$ "); // simulate shell prompt
#endif           // CS333_P1
}

#if defined(CS333_P3)
// list management helper functions
static void
stateListAdd(struct ptrs *list, struct proc *p)
{
  if ((*list).head == NULL)
  {
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  }
  else
  {
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}
#endif

#if defined(CS333_P3)
static int
stateListRemove(struct ptrs *list, struct proc *p)
{
  if ((*list).head == NULL || (*list).tail == NULL || p == NULL)
  {
    return -1;
  }

  struct proc *current = (*list).head;
  struct proc *previous = 0;

  if (current == p)
  {
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if ((*list).tail == p)
    {
      (*list).tail = NULL;
    }
    return 0;
  }

  while (current)
  {
    if (current == p)
    {
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if (current == NULL)
  {
    return -1;
  }

  // Process found.
  if (current == (*list).tail)
  {
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  }
  else
  {
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}
#endif

#if defined(CS333_P3)
static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++)
  {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#if defined(CS333_P4)
  for (i = 0; i <= MAXPRIO; i++)
  {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif
}
#endif

#if defined(CS333_P3)
static void
initFreeList(void)
{
  struct proc *p;

  for (p = ptable.proc; p < ptable.proc + NPROC; ++p)
  {
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}
#endif

#if defined(CS333_P3)
// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char *func, int line)
{
  if (p->state == state)
    return;
  cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n",
          states[p->state], states[state], func, line);
  panic("Error: Process state incorrect in assertState()");
}
#endif
#ifdef CS333_P2
int getprocs(uint max, struct uproc *table)
{
  struct proc *p;
  int i = 0;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (i == max)
      break;
    if (p->state == UNUSED || p->state == EMBRYO)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      safestrcpy(table[i].state, states[p->state], STRMAX);
    else
      safestrcpy(table[i].state, "???", STRMAX);
    table[i].pid = p->pid;
    table[i].uid = p->uid;
    table[i].gid = p->gid;
    table[i].ppid = p->parent ? p->parent->pid : p->pid;
    table[i].elapsed_ticks = ticks - p->start_ticks;
    table[i].CPU_total_ticks = p->cpu_ticks_total;
    table[i].size = p->sz;
#ifdef CS333_P4
    table[i].priority = p->priority;
#endif
    safestrcpy(table[i].name, p->name, STRMAX);
    i++;
  }

  release(&ptable.lock);
  return i;
}
#endif
#ifdef CS333_P4
int setpriority(int pid, int priority)
{
  acquire(&ptable.lock);
  struct proc *p;
  int onReadyList = 0;
  for (p = ptable.list[SLEEPING].head; p; p = p->next)
  {
    if (p->pid == pid)
      goto found;
  }

  for (p = ptable.list[RUNNING].head; p; p = p->next)
  {
    if (p->pid == pid)
      goto found;
  }
  int i;
  for (i = 0; i < MAXPRIO + 1; i++)
  {
    for (p = ptable.ready[i].head; p; p = p->next)
    {
      if (p->pid == pid)
      {
        onReadyList = 1;
        goto found;
      }
    }
  }
  release(&ptable.lock);
  return -1;

found:
  if (onReadyList)
  {
    stateListRemove(&ptable.ready[p->priority], p);
    p->priority = priority;
    stateListAdd(&ptable.ready[p->priority], p);
  }
  else
  {
    p->priority = priority;
  }
  p->budget = DEFAULT_BUDGET;
  release(&ptable.lock);
  return 0;
}
int getpriority(int pid)
{
  struct proc *p;
  int priority = -1;
  acquire(&ptable.lock);
  for (int i = 0; i < statecount; i++)
    for (p = ptable.list[i].head; p; p = p->next)
    {
      if (p->pid == pid)
        if (i != 0) // pid found not in UNUSED list
          priority = p->priority;
    }
  release(&ptable.lock);
  return priority;
}
#endif
