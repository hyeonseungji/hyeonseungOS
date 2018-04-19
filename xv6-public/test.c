#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
	int pid;
	/*printf(1,"my ppid is %d \n",getppid());*/
	printf(1,"my pid is %d\n",getpid());
        yield();	
	yield();
	yield();
	set_cpu_share(35);
	for(int i = 0; i < 10; i++){
	 pid = fork();
	 if(pid == -1)
	  exit();
	 if(pid == 0){
	  set_cpu_share(40);
	  sleep(10);
	  for(int p = 0; p < 100000; p++);
	  exit();
	 }
	 
	}
	wait();
	printf(1,"yield end!\n");
	exit();
}
