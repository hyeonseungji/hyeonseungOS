#include "types.h"
#include "stat.h"
#include "user.h"

void* printj(void *);
void printj2(void);
void printj3(int);

int g_a;

int
main(int argc, char *argv[])
{
  thread_t t1 = 2;
  thread_t t2 = 3;
  thread_create(&t1, printj, (void *)3);
  thread_create(&t2, printj, (void *)5);
  printf(1,"t1:%d\n",t1);
  printf(1,"t2:%d\n",t2);
  while(1);
  exit();
}

void* printj(void * arg){
	int a = (int) arg;
	int b = 212;
	int c = 53;
	printf(1, "printj: good!\n");
	printf(1, "printj:arg %d good!\n", a);
	printf(1, "printj:b %d c %d good!\n", b,c);
	printf(1, "g_a addr : (%d)\n",&g_a);
	printj2();
	return 0;
}

void printj2(void){
	printf(1,"2 done!\n");
	printj3(7);
	while(10);
}

void printj3(int arg){
	int a = 12;
	printf(1,"printj3:arg is %d and a is %d\n",arg,a);
}

