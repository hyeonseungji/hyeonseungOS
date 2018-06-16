# include "types.h"
# include "stat.h"
# include "user.h"

int main(void) {

	int pid;

	pid = fork();

	if(pid < 0)
		printf(1,"Error!\n");

	else if(pid == 0) {
		int pid_2;
		pid_2 = fork();
		if (pid_2 < 0);
		else if(pid_2 == 0){
			for(int i = 0; i < 100; i++){
				printf(1,"e%d\n", i);
			}
		exit();
		}
		else{
		for(int i = 0; i < 100; i++){
			printf(1,"c%d\n", i);
			
		}
		exit();
		}
	}

	else{
		for(int i = 0; i < 100; i++){
			printf(1,"p%d\n",i);
			
		}
		exit();
	}

	exit();
}
