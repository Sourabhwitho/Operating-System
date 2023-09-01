#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "condvar.h"
#include "bufelem.h"
#include "semaphore.h"

// For barrier
int barrier_cnt = 0, barrier_flag = 0;
int barrier_array[] = {0,0,0,0,0,0,0,0,0,0};
struct sleeplock barrier_lk;
cond_t barrier_cv;

// For producer-consumer condvar
buffer_elem buffer_cond[BUF_SIZE];
int buf_head = 0, buf_tail = 0;
struct sleeplock buf_lkdelete, buf_lkinsert, buf_lkprint;

// For semaphore
int buffer_sem[BUF_SIZE];
int nextp = 0, nextc = 0;
struct semaphore empty, full, pro, con;
struct sleeplock sem_print;

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_getppid(void)
{
  if (myproc()->parent) return myproc()->parent->pid;
  else {
     printf("No parent found.\n");
     return 0;
  }
}

uint64
sys_yield(void)
{
  yield();
  return 0;
}

uint64
sys_getpa(void)
{
  uint64 x;
  if (argaddr(0, &x) < 0) return -1;
  return walkaddr(myproc()->pagetable, x) + (x & (PGSIZE - 1));
}

uint64
sys_forkf(void)
{
  uint64 x;
  if (argaddr(0, &x) < 0) return -1;
  return forkf(x);
}

uint64
sys_waitpid(void)
{
  uint64 p;
  int x;

  if(argint(0, &x) < 0)
    return -1;
  if(argaddr(1, &p) < 0)
    return -1;

  if (x == -1) return wait(p);
  if ((x == 0) || (x < -1)) return -1;
  return waitpid(x, p);
}

uint64
sys_ps(void)
{
   return ps();
}

uint64
sys_pinfo(void)
{
  uint64 p;
  int x;

  if(argint(0, &x) < 0)
    return -1;
  if(argaddr(1, &p) < 0)
    return -1;

  if ((x == 0) || (x < -1) || (p == 0)) return -1;
  return pinfo(x, p);
}

uint64
sys_forkp(void)
{
  int x;
  if(argint(0, &x) < 0) return -1;
  return forkp(x);
}

uint64
sys_schedpolicy(void)
{
  int x;
  if(argint(0, &x) < 0) return -1;
  return schedpolicy(x);
}

uint64
sys_barrier_alloc(void)
{
  if(barrier_flag == 0){
    initsleeplock(&barrier_lk, "barrier");
    barrier_flag++;
  }
  for(int i=0; i<10; i++){
    if(barrier_array[i] == 0){
      barrier_array[i] = 1;
      return i;
    }
  }
  return -1;
}

uint64
sys_barrier(void)
{
  int ins, id, n;
  if(argint(0, &ins) < 0 || argint(1, &id) < 0 || argint(2, &n) < 0) return -1;
  if(barrier_array[id] == 0){
    printf("Error : Barrier not yet allocated\n");
    return -1;
  }
  acquiresleep(&barrier_lk);
  printf("%d: Entered barrier#%d for barrier array id %d\n", myproc()->pid, ins, id);
  barrier_cnt++;
  if(barrier_cnt == n){
    barrier_cnt = 0;
    cond_broadcast(&barrier_cv);
  }
  else cond_wait(&barrier_cv, &barrier_lk);
  printf("%d: Finished barrier#%d for barrier array id %d\n", myproc()->pid, ins, id);
  releasesleep(&barrier_lk);
  return 0;
}

uint64
sys_barrier_free(void)
{
  int id;
  if(argint(0, &id) < 0) return -1;
  if(barrier_array[id] == 0){
    printf("Error: Freeing a non allocated barrier\n");
    return -1;
  }
  barrier_array[id] = 0;
  return 0;
}

uint64
sys_buffer_cond_init(void)
{
  buf_head = 0;
  buf_tail = 0;
  initsleeplock(&buf_lkdelete,"buf_del");
  initsleeplock(&buf_lkinsert,"buf_ins");
  initsleeplock(&buf_lkprint,"buf_prt");
  for(int i=0; i<BUF_SIZE; i++){
    buffer_cond[i].x = -1;
    buffer_cond[i].full = 0;
    initsleeplock(&buffer_cond[i].lock,0);
  }
  return 0;
}

uint64
sys_cond_produce(void)
{
  int item, index;
  if(argint(0, &item) < 0) return -1;
  acquiresleep(&buf_lkinsert);
  index = buf_tail;
  buf_tail = (buf_tail + 1)%BUF_SIZE;
  releasesleep(&buf_lkinsert);
  acquiresleep(&buffer_cond[index].lock);
  while(buffer_cond[index].full) cond_wait(&buffer_cond[index].deleted, &buffer_cond[index].lock);
  buffer_cond[index].x = item;
  buffer_cond[index].full = 1;
  cond_signal(&buffer_cond[index].inserted);
  releasesleep(&buffer_cond[index].lock);
  return 0;
}

uint64
sys_cond_consume(void)
{
  int item, index;
  acquiresleep(&buf_lkdelete);
  index = buf_head;
  buf_head = (buf_head + 1)%BUF_SIZE;
  releasesleep(&buf_lkdelete);
  acquiresleep(&buffer_cond[index].lock);
  while(!buffer_cond[index].full) cond_wait(&buffer_cond[index].inserted, &buffer_cond[index].lock);
  item = buffer_cond[index].x;
  buffer_cond[index].full = 0;
  cond_signal(&buffer_cond[index].deleted);
  releasesleep(&buffer_cond[index].lock);
  acquiresleep(&buf_lkprint);
  printf("%d ", item);
  releasesleep(&buf_lkprint);
  return item;
}

uint64
sys_buffer_sem_init(void)
{
  nextp = 0;
  nextc = 0;
  sem_init(&empty, BUF_SIZE);
  sem_init(&full, 0);
  sem_init(&pro, 1);
  sem_init(&con, 1);
  initsleeplock(&sem_print, "sem_prt");
  for(int i=0; i<BUF_SIZE; i++){
    buffer_sem[i] = -1;
  }
  return 0;
}

uint64
sys_sem_produce(void)
{
  int item;
  if(argint(0, &item) < 0) return -1;
  sem_wait(&empty);
  sem_wait(&pro);
  buffer_sem[nextp] = item;
  nextp = (nextp + 1)%BUF_SIZE;
  sem_post(&pro);
  sem_post(&full);
  return 0;
}

uint64
sys_sem_consume(void)
{
  int item;
  sem_wait(&full);
  sem_wait(&con);
  item = buffer_sem[nextc];
  nextc = (nextc + 1)%BUF_SIZE;
  sem_post(&con);
  sem_post(&empty);
  acquiresleep(&sem_print);
  printf("%d ", item);
  releasesleep(&sem_print);
  return item;
}
