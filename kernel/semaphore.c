#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "condvar.h"
#include "semaphore.h"

void
sem_init(struct semaphore *s, int x)
{
  initsleeplock(&s->lk, "semaphore");
  acquiresleep(&s->lk);
  s->value = x;
  releasesleep(&s->lk);
}

void
sem_wait(struct semaphore *s)
{
  acquiresleep(&s->lk);
  s->value--;
  if(s->value < 0){
    condsleep(&s->cv, &s->lk);
  }
  releasesleep(&s->lk);
}

void
sem_post(struct semaphore *s)
{
  acquiresleep(&s->lk);
  s->value++;
  if(s->value <= 0) cond_signal(&s->cv);
  releasesleep(&s->lk);
}
