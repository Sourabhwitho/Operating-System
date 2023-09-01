struct semaphore {
  int value;
  struct sleeplock lk;
  struct condvar cv;
};
