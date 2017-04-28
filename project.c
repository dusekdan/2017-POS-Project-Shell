#define _POSIX_C_SOURCE 199506L
#define _XOPEN_SOURCE 500
#define _XOPEN_SOURCE_EXTENDED 1
#define BUFFER_SIZE 513
#define BUFFER_LAST_INDEX 512
#define DEBUG 0
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <unistd.h> 
#include <ctype.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

void *readInput(void * data);
void *executeCommand(void * data);
void preventViolentTermination(int source);
void killKid(int source);
int isEmptyHit(char* str);
void printShellHud();
void flushStdin();

char* sharedBuffer;

pthread_mutex_t bufferAccessMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t dummyMutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t dummyMutex2 = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t bufferReady1 = PTHREAD_COND_INITIALIZER;
pthread_cond_t bufferReady2 = PTHREAD_COND_INITIALIZER;


/**
 * Application entry point
 * Starts shell loop thread 
 */
int main (int argc, char** argv)
{
	pthread_t readThread;
	pthread_t executeThread;

	signal (SIGINT, &preventViolentTermination);

	/* Shared buffer pre-allocation */
	sharedBuffer = (char*)  malloc (BUFFER_SIZE * sizeof (char));

	pthread_create(&readThread, NULL, readInput,  (void *)(intptr_t)(1));
	pthread_create(&executeThread, NULL, executeCommand,  (void *)(intptr_t)(2));

	/* Clean up threads */
	pthread_join (readThread, NULL);
	pthread_join (executeThread, NULL);

	/* Clean up mutexes and conditional variables */
	pthread_mutex_destroy(&bufferAccessMutex);
	pthread_mutex_destroy(&dummyMutex1);
	pthread_mutex_destroy(&dummyMutex2);
	pthread_cond_destroy(&bufferReady1);
	pthread_cond_destroy(&bufferReady2);
	
	/* Exit main thread properly */
	return 0;
}


/**
 * Routine for input reading thread
 */
void *readInput(void * data)
{
	int inputLength;
	int firstRun = 1;
	char inputData[BUFFER_SIZE];

	/* As long as program is running, read commands */
	while (1)
	{
		/* Displays ddsh> for user and flushes stdout */
		
		if (firstRun)
		{
			printf ("Author: Daniel Dusek, xdusek21@vutbr.cz\n");
			printf ("CHYBEJICI FUNKCIONALITA: Spusteni procesu na pozadi\n");
			printf ("ZNAME PROBLEMY: Presmerovani vstupu a vystupu (<,>) potrebuje mezeru pred a po svem vyskytu.\n");
			printf ("\tUkonceni programu: exit\n");
			firstRun = 0;
		}

		printShellHud();	
		


		memset(inputData, '\0', BUFFER_SIZE);
		inputLength = read(0, inputData, 513);
		
		/* Required null termination for every input string */
		inputData[512] = '\0';

		/* 
			Note that also '\n' character is read - you can enter only 511 characters, 512th will be \n (and should  be) 
			Task assignment is very specific on how the input should be read though, so this is the correct way.
		*/
		if (inputLength == 513)	
		{
			fprintf (stderr, "Shell buffer size exceeded. (MAX_BUFF_SIZE = 512) \n");
			flushStdin();
			continue;
		}

		/* Right here I have string from input read to inputData variable, and I will copy it to sharedBuffer (mutex lock required) */
		pthread_mutex_lock(&bufferAccessMutex);
		memset(sharedBuffer, '\0', BUFFER_SIZE);
		memcpy(sharedBuffer, inputData, inputLength);
		pthread_mutex_unlock(&bufferAccessMutex);

		/* Tell thread #2 that sharedBuffer is ready */
		pthread_cond_signal(&bufferReady1);

		if (DEBUG)
			printf("D:Read vlákno èeká na signál.\n");

		/* Wait for execution thread to do its work */
		pthread_cond_wait(&bufferReady2, &dummyMutex1);
	}
	
	pthread_exit(NULL);
}


/**
 *	Executes command from sharedBuffer
 */
void *executeCommand(void * data)
{

	while (1)
	{
		char* outFDName = NULL;
		char* inFDName = NULL;
		int   outFD;
		int   inFD;

		char* programName = NULL;
		char** arguments = NULL;
		char** tempArgs = NULL;

		char* argument = NULL;
		char* tmpArgument = NULL;

		char *token, *string;

		int firstparam = 1;
		int argcout = 1;
	    int isFileName = 0;

		if (DEBUG)
			printf("D: Execute vlákno èeká na signál.\n");
		
		/* 
			Wait until signaled by reading thread, then acquire mutex 
			Signaling thread should unlock bufferMutex before signaling
		*/
		pthread_cond_wait(&bufferReady1, &dummyMutex2);

		/* Get rid of trailing \n */
		if (strlen(sharedBuffer) > 1)
			sharedBuffer[strlen(sharedBuffer)-1] = 0;

		/* Handle exit command */
		if (strcmp(sharedBuffer, "exit") == 0)
		{
			printf ("Exiting...\n");
			exit (0);
		}
		/* Make empty hits more shell-like (and certainly great again!) */
		else if (isEmptyHit(sharedBuffer))
		{
			pthread_cond_signal(&bufferReady2);
			continue;
		}


		if (DEBUG)
			printf ("E: Received signal, buffer-contents: %s\n", sharedBuffer);

		/* 
		Standard case - only ./program [anything] || nothing cases are handled here
		And cases where < and > are SEPARATED BY SPACE from other text
		Note: I am not proud of this way of coding, but ...time. 
	    */
		string = strdup(sharedBuffer);
		token = strtok(string, " ");
			
		while (token != NULL)
		{
			if (firstparam)
			{
				programName = strdup(token);
				firstparam = 0;
			}

			/* Also handle > and < prefixed or suffixed by SPACE */
			if (strlen(token) == 1 && strcmp(token, ">") == 0)
			{
				if (DEBUG)
					printf("\nWATCH OUT, FILE NAME TO BE EXPECTED\n");
				
				/* Raise flag and continue */
				isFileName = 1;
				token = strtok(NULL, " ");
				continue;
			}
			else if (strlen(token) == 1 && strcmp(token, "<") == 0)
			{
				if (DEBUG)
					printf("\nInput filename to be expected\n");
				
				/* Raise flag and continue */
				isFileName = 2;
				token = strtok(NULL, " ");
				continue;
			}

			if (isFileName == 1)
			{
				/* Reset flag back and jump to forking part with proper dup() call*/
				outFDName = (char*) malloc(strlen(token)*sizeof(char));
				memcpy(outFDName, token, strlen(token));
				
				if (DEBUG)
					printf("FILENAME: %s\n",  outFDName);
				
				token = NULL;
				isFileName = 1;
				continue;
			}
			else if (isFileName == 2)
			{
				/* Reset flag back and jump to forking part with proper dup() call*/
				inFDName = (char*) malloc(strlen(token)*sizeof(char));
				memcpy(inFDName, token, strlen(token));
			
				if (DEBUG)
					printf("FILENAME: %s\n", inFDName);
			
				token = NULL;
				isFileName = 2;
				continue;
			}
		
			tempArgs = (char**) realloc(arguments, (sizeof(char *) * argcout));
			arguments = tempArgs;

			tmpArgument = (char*) realloc(argument, (strlen(token)+1) * sizeof(char));
			argument = tmpArgument;
			
			strcpy(argument, token);
			argument[strlen(argument)] = '\0';

			arguments[argcout-1] = strdup(argument);

			if (DEBUG)
				printf("Also need to incorporate param: %s \n", token);

			argcout++;

			token = strtok(NULL, " ");
		}

		/* Add last null-terminated arg to arguments */
		tempArgs = (char**) realloc(arguments, sizeof(char*) * argcout);
		arguments = tempArgs;
		tmpArgument = (char*) realloc(argument, (sizeof(char*)));
		argument = tmpArgument;
		argument = '\0';
		arguments[argcout-1] = argument;

		if (arguments != NULL)
		{
			/* Call exec with args*/
			pid_t child = fork();
			if (child == 0)
			{

				/* Set output file accordingly */
				if (isFileName == 1)
				{
					outFD = open(outFDName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
					if (outFD < 0)
					{
						fprintf (stderr, "Unable to open output file.\n");
					}

					/* Point stdout(1) to outFD */
					if (dup2(outFD, 1) < 0)
					{
						perror("Failed to dup2(fd, stdout).\n");
					}
				}

				/* Set input file accordingly */
				if (isFileName == 2)
				{
					inFD = open(inFDName, O_RDONLY, 0666);
					if (inFD < 0)
					{
						perror("Opening input file failed.");
					}

					if (dup2(inFD, 0) < 0)
					{
						perror("Opening output file failed.");
					}
				}

				/* Prepare kid for suicide before making him other process */
				signal(SIGINT, &killKid);
				
				if (execvp(programName, arguments) < 0) 
				{
					int err = errno;
					if (err == 2)
					{
						fprintf(stderr, "Command not supported.\n");
					}
					else if (err == 36)
					{
						fprintf(stderr, "Command not supported. And also is too long for execvp() to handle.\n");
					}
					else
					{
						printf("Error: %d", err);
					}
					exit(1);
				}

				exit(0);
			}
			wait(NULL);
		}
		

		/* Releasing mutex & signaling the other thread */
		pthread_cond_signal(&bufferReady2);
	}

	pthread_exit(NULL);
}

/**
 * Displays shell message and flushes stdout
 */
void printShellHud()
{
	printf ("ddsh> ");
	fflush(stdout);
}

/**
 * Flushes input buffer, so the next line can be read
 * Code inspired by http://stackoverflow.com/questions/2187474
 * Note that according to Linux fpurge man page: Usually it is 
 * mistake to want to discard input buffers.
 * TODO: Verify this is not causing any side effects (command not found when more than 511 characters are input?)
 * TODO: Fix the loop, it does not seem to recognize end of the loop correctly when left as it is
 */
void flushStdin()
{
	int c;
	while ((c = getchar()) != '\n');
}

/**
 * Handles violent script termination (for now, only CTRL+C type of signal)
 */
void preventViolentTermination(int source)
{
	if (source == SIGINT)
	{
		/* Ensure following CTRL+C presses are also caught */
		signal(SIGINT, &preventViolentTermination);
		
		printf ("\tKilling kids...\n");
		
		/* Make the shell to prompt again */
		printShellHud();
		/* 
			Known issue: Sometimes the shell prompt displays twice, but I figured
		   that it's better to display it twice than to not display it at all 
		   */
	}
}

/**
 * Signal handler for children processes
 * Kills the process, does not print anything, though.
 */
void killKid(int source)
{
	if (source == SIGINT)
	{
		exit(0);
	}
}

/**
 * Checks whether string is whitespace(s) only
 */
int isEmptyHit(char* str)
{
	int i = 0;
	while(str[i] != '\0')
	{
		if (!isspace(str[i]))
			return 0;
		i++;
	}

	return 1;
}