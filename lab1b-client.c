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
struct termios restoreAttr;
struct sockaddr_in serverAddy;
struct hostent *srv;
int socketfd;

void restoreTerminal(){
    if(tcsetattr(STDIN_FILENO, TCSANOW, &restoreAttr) < 0)
	{
		fprintf(stderr, "Failure to restore original terminal: %s\n", strerror(errno));
		exit(1);
	}
}

void setTerminal(){
    struct termios attr;
    if((tcgetattr(STDIN_FILENO, &attr)) < 0){
        fprintf(stderr, "Failure to obtain the initial attributes: %s\n", strerror(errno));
		exit(1);
    }
    restoreAttr = attr;;
    attr.c_iflag = ISTRIP;
    attr.c_oflag = 0;
    attr.c_lflag = 0;
    if ((tcsetattr(0, TCSANOW, &attr)) < 0){
		fprintf(stderr, "Failure to set terminal to character-at-a-time, no-echo mode: %s\n", strerror(errno));
		exit(1);
	}
    atexit(restoreTerminal);
}

void endStream(){
    deflateEnd(&toServer);
    inflateEnd(&fromServer);
}

void compressInit(){
	fromServer.zalloc = Z_NULL;
	fromServer.zfree = Z_NULL;
	fromServer.opaque = Z_NULL;
    int stat = inflateInit(&fromServer);
	if (stat != Z_OK) {
		fprintf(stderr, "Failure to inflateInit on client side: %s\n", strerror(errno));
		exit(1);
	}
	toServer.zalloc = Z_NULL;
	toServer.zfree = Z_NULL;
	toServer.opaque = Z_NULL;
    stat = deflateInit(&toServer, Z_DEFAULT_COMPRESSION);
	if (stat != Z_OK) {
		fprintf(stderr, "Failure to deflateInit on client side: %s\n", strerror(errno));
		exit(1);
	}
    atexit(endStream);
}

void writeToLogFile(int fd, const char* buffer, size_t numBytes){
    if (write(fd, buffer, numBytes) < 0){
        fprintf(stderr, "Error writing to log file: %s\n", strerror(errno));
        exit(1);
    }

}

int main(int argc, char* argv[]){
    static const struct option ops[] = {
        {"port", 1, NULL, 'p'},
        {"log", 1, NULL, 'l'},
        {"compress", 0, NULL, 'c'},
        {0, 0, 0, 0}
    };
    int portFlag = 0;
    int logFlag = 0;
    int compressFlag = 0;
    int portNumber = -1;
    int logFile;
    int c;
    while(1){
        c = getopt_long(argc, argv, "", ops, NULL);
        if(c == -1)
        break;
        switch(c){
            case 'p':
                portFlag = 1;
                portNumber = atoi(optarg);
                break;
            case 'l':
                logFlag = 1;
                logFile = creat(optarg,0666);
                if(logFile < 0){
                    fprintf(stderr, "Unable to create/write to file: %s\n", strerror(errno));
                    exit(1);
                }
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
    if(socketfd < 0){
        fprintf(stderr, "Unable to open socket: %s\n", strerror(errno));
        exit(1);
    }

    srv = gethostbyname("localhost");
    if(srv == NULL){
        fprintf(stderr, "Unable to find host: %s\n", strerror(errno));
        exit(1);
    }

    memset((char*) &serverAddy, 0, sizeof(serverAddy));
    serverAddy.sin_family = AF_INET;
    memcpy((char*)&serverAddy.sin_addr.s_addr, (char*)srv->h_addr, srv->h_length);
    serverAddy.sin_port = htons(portNumber);

    if (connect(socketfd, (struct sockaddr*)&serverAddy, sizeof(serverAddy)) < 0){
        fprintf(stderr, "Unable to connect from client: %s\n", strerror(errno));
  	    exit(1);
    }
  	
    setTerminal();

    //begin poll
    struct pollfd fds[] = {
  	    { STDIN_FILENO, POLLIN|POLLHUP|POLLERR, 0 },
  	    { socketfd, POLLIN|POLLHUP|POLLERR, 0 }
    };

    while(1){
        int status = poll(fds, 2, 0);
        if(status < 0){
            fprintf(stderr, "Error creating poll: %s\n", strerror(errno));
            exit(1);
        }
        if(fds[0].revents & POLLIN){
            char buf[256];
            int charRead;
            charRead = read(STDIN_FILENO, &buf, 256);
            if(charRead < 0){
                fprintf(stderr, "Failure to read characters: %s\n", strerror(errno));
		        exit(1);
            }
            int i;
            for(i = 0; i < charRead; i++){
			    if (buf[i] == '\n') {
                    char combine1[2];
				    combine1[0] = '\r';
				    combine1[1] = '\n';
				    write(STDOUT_FILENO, combine1, 2);
			    }
                else if(buf[i] == '\r'){
                    char x = '\n';
                    write(STDOUT_FILENO, &x, 1);
                }
                else{
                    char x = buf[i];
                    write(STDOUT_FILENO, &x, 1);
                }
            }
            if(compressFlag != 1){
                write(socketfd, buf, charRead);
                if(logFlag == 1){
                    char sent[] = "SENT ";
                    writeToLogFile(logFile, sent, 5);
                    char num[5];
                    sprintf(num, "%d", charRead);
                    writeToLogFile(logFile, num, strlen(num));
                    char byte[] = " bytes: ";
                    writeToLogFile(logFile, byte, 8);
                    writeToLogFile(logFile, buf, charRead);
                    writeToLogFile(logFile, "\n", 1);
                }
            }
            else{
                toServer.avail_in = charRead;
                toServer.next_in = (unsigned char*) buf;
                toServer.avail_out = 256;
                char* compressBuf[256];
                toServer.next_out = (unsigned char*) compressBuf;
                do{
		            deflate(&toServer, Z_SYNC_FLUSH);
	            } while(toServer.avail_in > 0);
                write(socketfd, compressBuf, 256 - toServer.avail_out);
                if(logFlag == 1 || charRead > 0){
                    char sent[] = "SENT ";
                    writeToLogFile(logFile, sent, 5);
                    char num[5];
                    char used = (256 - toServer.avail_out);
                    sprintf(num, "%d", used);
                    writeToLogFile(logFile, num, strlen(num));
                    char byte[] = " bytes: ";
                    writeToLogFile(logFile, byte, 8);
                    writeToLogFile(logFile, buf, used);
                    writeToLogFile(logFile, "\n", 1);
                }
            }
        }
        if(fds[1].revents & POLLIN){
            char buf[256];
            int charRead;
            charRead = read(socketfd, &buf, 256);
            if(charRead < 0){
                fprintf(stderr, "Failure to read characters: %s\n", strerror(errno));
		        exit(1);
            }
            if(compressFlag != 1){
                write(STDOUT_FILENO, buf, charRead);
            }
            else{
                fromServer.avail_in = charRead;
                fromServer.next_in = (unsigned char*) buf;
                fromServer.avail_out = 1024;
                char* compressBuf[1024];
                fromServer.next_out = (unsigned char*) compressBuf;
                do{
		            inflate(&fromServer, Z_SYNC_FLUSH);
	            } while(fromServer.avail_in > 0);
                char used = (1024 - fromServer.avail_out);
                write(STDOUT_FILENO, compressBuf, used);
            }
            if(logFlag == 1 || charRead > 0){
                char receive[] = "RECEIVED ";
                writeToLogFile(logFile, receive, 9);
                char num[5];
                sprintf(num, "%d", charRead);
                writeToLogFile(logFile, num, strlen(num));
                char byte[] = " bytes: ";
                writeToLogFile(logFile, byte, 8);
                writeToLogFile(logFile, buf, charRead);
                writeToLogFile(logFile, "\n", 1);
            } 
        }
        if (fds[1].revents & POLLERR || fds[1].revents & POLLHUP || fds[0].revents & POLLERR || fds[0].revents & POLLHUP) { //polling error
            exit(0);
		}
    }
    exit(0);

}