#NAME: William Chen
#EMAIL: billy.lj.chen@gmail.com
#ID: 405131881

default: server client

client:
	gcc -Wall -Wextra -lz lab1b-client.c -o lab1b-client

server: 
	gcc -Wall -Wextra -lz lab1b-server.c -o lab1b-server

dist: 
	tar -cvzf lab1b-405131881.tar.gz lab1b-server.c lab1b-client.c  Makefile README

clean: 
	rm -f lab1b-405131881.tar.gz lab1b-client lab1b-server