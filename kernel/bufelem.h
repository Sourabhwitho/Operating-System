typedef struct bufelem{
  int full;
  int x;
  struct sleeplock lock;
  struct condvar inserted;
  struct condvar deleted;
}buffer_elem;
