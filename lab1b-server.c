//NAME: William Chen
//EMAIL: billy.lj.chen@gmail.com
//ID: 405131881

#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <string.h>
#include <termios.h>
#include <sys/socket.h>
#include <zlib.h>
#include <netdb.h>
#include <netinet/in.h>

z_stream toServer;
z_stream fromServer;
int pipeToShell[2];
int pipeFromShell[2];
pid_t pid;
int socketfd;
int newSocketfd;
struct sockaddr_in serverAddy;
struct sockaddr_in clientAddy;
int terminate = 0;
int shellFlag = 0;

void createPipe(int pipe1[2]){
    if ((pipe(pipe1)) < 0) 
    {
		fprintf(stderr, "Failure to create pipe: %s\n", strerror(errno));
		exit(1);
	}
}

void signalHandler(int signal){
    if(signal == SIGPIPE){
        kill(pid, SIGKILL);
        exit(0);
    }
}

void endStream(){
    deflateEnd(&toServer);
    inflateEnd(&fromServer);
}

void callAtExit(){
    shutdown(socketfd, SHUT_RDWR);
    shutdown(newSocketfd, SHUT_RDWR);
    if(shellFlag == 1){
        int stat;
        int val = waitpid(pid, &stat, 0);
        if(val < 0){
            fprintf(stderr, "Error in waitpid: %s\n", strerror(errno));
            exit(1);
        }
        fprintf(stderr, "SHELL EXIT SIGNAL=%d, STATUS=%d\r\n", stat&0x007f, stat>>8);
        close(pipeFromShell[0]);
        close(pipeToShell[1]);
        
    }
}

void compressInit(){
	fromServer.zalloc = Z_NULL;
	fromServer.zfree = Z_NULL;
	fromServer.opaque = Z_NULL;
    int stat = deflateInit(&fromServer, Z_DEFAULT_COMPRESSION);
	if (stat != Z_OK) {
		fprintf(stderr, "Failure to inflateInit on client side: %s\n", strerror(errno));
		exit(1);
	}
	toServer.zalloc = Z_NULL;
	toServer.zfree = Z_NULL;
	toServer.opaque = Z_NULL;
    stat = inflateInit(&toServer);
	if (stat != Z_OK) {
		fprintf(stderr, "Failure to deflateInit on client side: %s\n", strerror(errno));
		exit(1);
	}
    atexit(endStream);
}

int main(int argc, char* argv[]){
    struct option options[] = {
		{"port", required_argument, NULL, 'p'},
    	{"compress", no_argument, NULL, 'c'},
        {"shell", required_argument, NULL, 's'},
    	{0, 0, 0, 0}
	};
    socklen_t clientLength = 0;
    char* program = NULL; 
    int portFlag = 0;
    int compressFlag = 0;
    int portNumber = -1;
    int choice;
    while(1){
        choice = getopt_long(argc, argv, "", options, NULL);
        if(choice == -1)
        break;
        switch(choice){
            case 's':
                shellFlag = 1;
                program = optarg;
                break;
            case 'p':
                portFlag = 1;
                portNumber = atoi(optarg);
                break;
            case 'c':
                compressFlag = 1;
                compressInit();
                break;
            default:
                fprintf(stderr, "Error: Invalid Argument: %s\n", strerror(errno));
                exit(1);
                break;
        }
    }

    if(portFlag != 1){
        fprintf(stderr, "Error: --port= is mandatory: %s\n", strerror(errno));
		exit(1);
    }
    if(portNumber == -1){
        fprintf(stderr, "Error: Port number is not specified: %s\n", strerror(errno));
        exit(1);
    }

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketfd < 0) {
		fprintf(stderr, "Unable to open socket: %s\n", strerror(errno));
		exit(1);
	}
	memset((void*) &serverAddy, 0, sizeof(serverAddy));
	serverAddy.sin_family = AF_INET;
	serverAddy.sin_addr.s_addr = INADDR_ANY;
	serverAddy.sin_port = htons(portNumber);

    int status = bind(socketfd, (struct sockaddr*)&serverAddy, sizeof(serverAddy));
	if (status < 0) {
		fprintf(stderr, "Unable to bind: %s\n", strerror(errno));
		exit(1);
	}
	listen(socketfd, 8);
	clientLength = sizeof(clientAddy);
    newSocketfd = accept(socketfd, (struct sockaddr*)&clientAddy, &clientLength);
	if (newSocketfd < 0) {
		fprintf(stderr, "Unable to accept: %s\n", strerror(errno));
		exit(1);
	}

    if(shellFlag == 0){ //send input back to client
        char buf[256];
        int charRead;
        int exitCheck = atexit(callAtExit);
        if(exitCheck < 0){
            fprintf(stderr, "Error exiting: %s", strerror(errno));
            exit(1);
        }
        while(1){
            charRead = read(newSocketfd, &buf, 256);
            if(charRead < 0){
                fprintf(stderr, "Failure to read characters from client: %s\n", strerror(errno));
			    exit(1);
            }
            int i;
            char c;
            for(i = 0; i < charRead; i++){
                if(buf[i] == 0x04){
				    exit(0);
                }
                else if(buf[i] == 0x03){
                    exit(0);
                }
                else{
                    c = buf[i];
				    write(newSocketfd, &c, 1);
                }
                
            }
        }
    }
    else{
        if(program == NULL){
            fprintf(stderr, "Error: Shell program must be specified.\n");
		    exit(1);
        }
        createPipe(pipeToShell);
        createPipe(pipeFromShell);
        int exitCheck = atexit(callAtExit);
        if(exitCheck < 0){
            fprintf(stderr, "Error exiting: %s", strerror(errno));
            exit(1);
        }
        signal(SIGPIPE, signalHandler);
        pid = fork();
        if(pid == 0){ //child
            close(pipeToShell[1]);
		    close(pipeFromShell[0]);
		    dup2(pipeToShell[0], STDIN_FILENO); 
		    close(pipeToShell[0]); 
		    dup2(pipeFromShell[1], STDOUT_FILENO);
		    dup2(pipeFromShell[1], STDERR_FILENO);
		    close(pipeFromShell[1]);
		    execl(program, program, (char*) NULL); 
		    fprintf(stderr, "Failure to execute shell in child process: %s\n", strerror(errno));
		    exit(1);
        }
        else if(pid > 0){ //parent
            close(pipeToShell[0]);
		    close(pipeFromShell[1]);
            terminate = 0;

            struct pollfd fds[] =  // define the two input channels we are multiplexing
		    {
			    {newSocketfd, POLLIN|POLLHUP|POLLERR, 0},
			    {pipeFromShell[0], POLLIN|POLLHUP|POLLERR, 0}
		    };
            int r;
            while(1){
                if((r = poll(fds, 2, 0)) < 0){
                    fprintf(stderr, "Failure to call poll(): %s\n", strerror(errno));
                    exit(1);
                }
                if(fds[0].revents & POLLIN){
                    char buf[1024];
                    int charRead = read(newSocketfd, &buf,  1024);
                    if(charRead < 0){
                        fprintf(stderr, "Failure to read characters: %s\n", strerror(errno));
		                exit(1);
                    }
                    if(compressFlag  == 1){
                        char compressBuf[1024];
                        toServer.avail_in = charRead;
                        toServer.next_in = (unsigned char*) buf;
                        toServer.avail_out = 1024;
                        toServer.next_out = (unsigned char*) compressBuf;
                        do{
		                    int s = inflate(&toServer, Z_SYNC_FLUSH);
                            if(s == Z_STREAM_ERROR){
                                fprintf(stderr, "Failed Stream Inflate: %s\n", strerror(errno));
                                exit(1);
                            }
	                    } while(toServer.avail_in > 0);
                        int newCharRead = 1024 - toServer.avail_out;
                        int i;
                        for(i = 0; i < newCharRead; i++){
                            if (compressBuf[i] == 0x03){
                                if ((kill(pid, SIGINT)) < 0) {
    			                    fprintf(stderr, "Error sending SIGINT to shell: %s\n", strerror(errno));
	    		                    exit(1);
		                        }
			                }
			                else if (compressBuf[i] == '\r' || compressBuf[i] == '\n') {
                                char a = '\n';
                                write(pipeToShell[1], &a, 1);
		    	            }
		                    else if (compressBuf[i] == 0x04){
                                close(pipeToShell[0]);
                                exit(0);
                            }
                            else{
                                char x = compressBuf[i];
                                write(pipeToShell[1], &x, 1);
                            }
                        }
                    }
                    else{
                        int i;
                        for(i = 0; i < charRead; i++){
                            if (buf[i] == 0x03){
                                if ((kill(pid, SIGINT)) < 0) {
    			                    fprintf(stderr, "Error sending SIGINT to shell: %s\n", strerror(errno));
	    		                    exit(1);
		                        }
			                }
			                else if (buf[i] == '\r' || buf[i] == '\n') {
                                char a = '\n';
                                write(pipeToShell[1], &a, 1);
		    	            }
		                    else if (buf[i] == 0x04){
                                close(pipeToShell[1]);
                                exit(0);
                            }
                            else{
                                char x = buf[i];
                                write(pipeToShell[1], &x, 1);
                            }
                        }
                    }
                }
                if(fds[0].revents & POLLERR || fds[0].revents & POLLHUP){
                    exit(0);
                }
                if(fds[1].revents & POLLIN){
                    char buf[1024];
                    int charRead = read(pipeFromShell[0], &buf,  1024);
                    char combine1[2];
				    combine1[0] = '\r';
				    combine1[1] = '\n';
                    if(charRead < 0){
                        fprintf(stderr, "Failure to read characters: %s\n", strerror(errno));
		                exit(1);
                    }
                    int charsInLine = 0;
                    int locationAndChars = 0;
                    int i;
                    for(i = 0; i < charRead; i++){
                        if(buf[i] == '\012'){
                            if(compressFlag == 1){
                                char* compressBuf[1024];
                                fromServer.avail_in = charsInLine;
                                fromServer.next_in = (unsigned char*) (buf + locationAndChars);
                                fromServer.avail_out = 1024;
                                fromServer.next_out = (unsigned char*) compressBuf;
                                do{
		                            int s = deflate(&fromServer, Z_SYNC_FLUSH);
                                    if(s == Z_STREAM_ERROR){
                                        fprintf(stderr, "Failed Stream Deflate: %s\n", strerror(errno));
                                        exit(1);
                                    }
	                            } while(fromServer.avail_in > 0);
                                int newCharRead = 1024 - fromServer.avail_out;
                                if((write(newSocketfd, &compressBuf, newCharRead)) < 0){
                                    fprintf(stderr, "Unable to write to socket: %s\n", strerror(errno));
                                    exit(1);
                                }
                                char newLinebuf[1024];
                                fromServer.avail_in = 2;
                                fromServer.next_in = (unsigned char*) combine1;
                                fromServer.avail_out = 1024;
                                fromServer.next_out = (unsigned char*) newLinebuf;
                                do{
		                            int s = deflate(&fromServer, Z_SYNC_FLUSH);
                                    if(s == Z_STREAM_ERROR){
                                        fprintf(stderr, "Failed Stream Deflate: %s\n", strerror(errno));
                                        exit(1);
                                    }
	                            } while(fromServer.avail_in > 0);
                                newCharRead = 1024 - fromServer.avail_out;
                                if((write(newSocketfd, &newLinebuf, newCharRead)) < 0){
                                    fprintf(stderr, "Unable to write to socket: %s\n", strerror(errno));
                                    exit(1);
                                }
                            }
                            else{
                                write(newSocketfd, (buf + locationAndChars), charsInLine);
								write(newSocketfd, combine1, 2);
                            }
                            charsInLine++;
                            locationAndChars += charsInLine;
                            charsInLine = 0;
                            continue;
                        }
                        else if(buf[i] == '\004'){
                            exit(0);
                        }
                        charsInLine++;
                    }
                    write(newSocketfd, (buf + locationAndChars), charsInLine);
                    
                }
                if(fds[1].revents & POLLERR || fds[1].revents & POLLHUP){
                    exit(0);
                }
                if(terminate == 1){
                    exit(0);
                }
            }

        }
        else{
            //neither parent process nor child process
            fprintf(stderr, "Failure to fork a new process: %s\n", strerror(errno));
		    exit (1);
        }
    }
    exit(0);
}