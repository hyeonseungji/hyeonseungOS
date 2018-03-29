# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <wait.h>

int ExecuteBatch(char*);	//used for batch mode.
int ExecuteChild(char*);	//used for executing process.
char** GetNewStrtok(char*, char*); //used for separating process by tok.
void ReplaceQuote(char*); //used for replacing quote to space.

int main(int argc, char*argv[]){

	if (argc > 1) { // In case argc > 1, shell enter the batch mode.
		if (ExecuteBatch(argv[1])) {
			printf("File open error!\n");
			return 1;
		}

		else {
			return 0;
		}
	}

	char *input_string = (char*)malloc(sizeof(char)*256);
	//This is what users input by keyboard.
	char **command = (char**)malloc(sizeof(char*)*256);
	//This is command set that execute processes.

	int cnt_i,cnt_j; //just count value for 'for' and 'while'
	int stat;

	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		command[cnt_i] = (char*)malloc(sizeof(char)*256);
	}

	while (1) {	//It end when user input "quit"or"Ctrl+d"
		do {
			input_string[0] = '\0';

			printf("prompt>");

			if (scanf("%[^\n]s",input_string)==-1) {
				printf("\n");
				exit(0);
			}	//If user input "ctrl+d", then quit shell.

			getchar();
		} while (input_string[0] == '\0'); 
	//If user just push 'enter', it prints 'prompt>' and wait input.

		ReplaceQuote(input_string);
	//If there are quotes in string, then it replace them to space bar.
		
		if (strcmp(input_string, "quit") == 0) {
			return 0;
		} // If user input "quit", then quit shell.

		command = GetNewStrtok(input_string, ";");
		//command has separated commands by token ';'

		for (cnt_i = 0; command[cnt_i] != NULL; cnt_i++) {
			ExecuteChild(command[cnt_i]);
		} //It execute the command 'command[cnt_i]'.

		for(cnt_j = 0; cnt_j < cnt_i; cnt_j++) {
			wait(&stat);
		} //Shell just wait until all child process ends.

	} //while(1) end
	
	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		free(command[cnt_i]);
	}

	free(command);
	free(input_string);

	return 0;
}

int ExecuteBatch(char *input_string){

	FILE *file = fopen(input_string,"r");
	
	if (file == NULL) {
		return 1;
	}

	int stat;
	int cnt_i, cnt_j, cnt_k; // just used for 'for' and 'while'

	char string[256];

	char **line_command = (char**)malloc(sizeof(char*)*256);
	// It has command set separated by 'enter'.
	char **one_command = (char**)malloc(sizeof(char*)*256);
	// It has just one command. made by spearting line_command.
	
	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		line_command[cnt_i] = (char*)malloc(sizeof(char)*256);
	}

	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		one_command[cnt_i] = (char*)malloc(sizeof(char)*256);
	}

	cnt_i = 0;

	while (fgets(string,sizeof(string), file)) {
		string[strlen(string)-1] = '\0';
		printf("%s\n",string);
		ReplaceQuote(string);
		strcpy(line_command[cnt_i],string);
		cnt_i++;
	} //Shell gets and print string. It push string to line_command[].
	
	line_command[cnt_i] = NULL;

	for (cnt_i = 0; line_command[cnt_i] != NULL; cnt_i++) {
		one_command = GetNewStrtok(line_command[cnt_i],";");

		for (cnt_j = 0; one_command[cnt_j] != NULL; cnt_j++) {
			ExecuteChild(one_command[cnt_j]);
		}	

		for (cnt_k = 0; cnt_k < cnt_j; cnt_k++) {
			wait(&stat);
		}
	}
	//It execute commands sequentially per line command.

	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		free(line_command[cnt_i]);
	}

	free(line_command);

	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {	
		free(one_command[cnt_i]);
	}

	free(one_command);

	
	fclose(file);

	return 0;


}

int ExecuteChild(char *input_string) {
	char * copy_string = malloc(sizeof(char)*256); 
	strcpy(copy_string, input_string);

	char **command = (char**)malloc(sizeof(char*)*256);
	
	int cnt_i;

	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		command[cnt_i] = (char*)malloc(sizeof(char)*256);
	}

	command = GetNewStrtok(copy_string, " ");
	//Now, command has new command set that is separated by space bar.

	if(!strcmp(command[0],"quit")) {
		exit(0);
	}	//If command is "quit" then shell just ends.

	pid_t pid;

	pid = fork();

	if(pid == -1) {
		
		printf("In opening process,Fatal Error occured. \n");
		return 1;

	}

	if (pid == 0) {
		execvp(command[0] , command);
		printf("%s is not found!\n",input_string);
	// if execvp has not operated 'command', then printf execute.
		for (cnt_i = 0; cnt_i < 256 ; cnt_i++)
			free(command[cnt_i]);
		free(command);
		free(copy_string);
		exit(0);	
	}
	
	for (cnt_i = 0; cnt_i < 256 ; cnt_i++) {
		free(command[cnt_i]);
	}

	free(command);
	free(copy_string);
	
	return 0;
}

char ** GetNewStrtok(char* input_string, char* tok){

		char *copy_string = malloc(sizeof(char)*256);
		char **tok_result = malloc(sizeof(char*)*256);
		//It will have final result of function that returns.
		char *tok_word = malloc(sizeof(char)*256);
		//It will have words(or strings) separated by token.
		strcpy(copy_string, input_string);
		tok_word = strtok(copy_string, tok);
		int cnt_i;

		for (cnt_i = 0; cnt_i < 256; cnt_i++) {
			tok_result[cnt_i] = malloc(sizeof(char)*256);
		}
		for (cnt_i = 0; tok_word != NULL; cnt_i++) {	
			strcpy(tok_result[cnt_i], tok_word);
			tok_word = strtok(NULL, tok);
		}

		tok_result[cnt_i] = NULL;
		

		free(copy_string);
		free(tok_word);
		
		return tok_result;

}

void ReplaceQuote(char * input_string){

for (int cnt_i = 0; input_string[cnt_i] != '\0'; cnt_i++) {
	if(input_string[cnt_i] == '\'' || input_string[cnt_i] == '\"') {
			input_string[cnt_i] = ' ';
		}	// if function find quote it replace to space bar.
	}

}
