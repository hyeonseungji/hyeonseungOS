#include "types.h"
#include "stat.h"
#include "user.h"

void* printj(void *);

int
main(int argc, char *argv[])
{
  thread_t * t1 = 0;
  thread_create(t1, printj, (void *)1);
  exit();
}

void* printj(void * arg){
	int a = (int) arg;
	printf(1, "printj:%d good!\n", a);
	return 0;
}


