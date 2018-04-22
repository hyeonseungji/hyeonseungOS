#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
	int pid;
	/*printf(1,"my ppid is %d \n",getppid());*/
	/*printf(1,"my pid is %d\n",getpid());*/
        sleep(10);
/*
for(int i = 0; i < 50; i++)        
yield();*/	
	set_cpu_share(10);
	for(int i = 0; i < 10; i++){
	 pid = fork();
	 if(pid == -1)
	  exit();
	 if(pid == 0){
	  set_cpu_share(20);
	  sleep(10);
	  for(int p = 0; p < 100000; p++);
	  exit();
	 }
	 
	}
	wait();
	/*printf(1,"\n\nyield end!\n\n");*/
	exit();
}
