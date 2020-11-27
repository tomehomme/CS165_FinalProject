#include <arpa/inet.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <math.h>
#include <tls.h> // for TLS
#define PORT 9998


static void usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s -port portnumber\n", __progname);
	exit(1);
}

static void kidhandler(int signum)
{
	/* signal handler for SIGCHLD */
	waitpid(WAIT_ANY, NULL, WNOHANG);
}

// your application name -port portnumber
int main(int argc, char *argv[])
{

	// relative to this server
	int sockfd, ret;
	struct sockaddr_in serverAddr;
	char buffer[1024], *ep;
	u_long p;
	u_short port;

	// for any new connections
	int newSocket;
	struct sockaddr_in newAddr;
	socklen_t addr_size;
	pid_t childpid;
	pid_t pid;

	// get the portnumber from argument
	if (argc != 3)
	{
		usage();
	}

	errno = 0;
	p = strtoul(argv[2], &ep, 10); // grab proxy port number
	if (*argv[2] == '\0' || *ep != '\0')
	{
		/* parameter wasn't a number, or was empty */
		fprintf(stderr, "%s - not a number\n", argv[2]);
		usage();
	}
	if ((errno == ERANGE && p == ULONG_MAX) || (p > USHRT_MAX))
	{
		/* It's a number, but it either can't fit in an unsigned
		* long, or is too big for an unsigned short
		*/
		fprintf(stderr, "%s - value out of range\n", argv[2]);
		usage();
	}
	/* now safe to do this */
	port = PORT;

	FILE *fp;
	char fileName[1024]; 
	size_t fileLen = 0;
	ssize_t read;
	int i = 0;
	

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		printf("[-]Error in connection.\n");
		exit(1);
	}
	printf("[+]Server Socket is created.\n");

	memset(&serverAddr, '\0', sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	ret = bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	if (ret < 0)
	{
		printf("[-]Error in binding.\n");
		exit(1);
	}
	printf("[+]Bind to port %d\n", port);

	if (listen(sockfd, 10) == 0)
	{
		printf("[+]Listening....\n");
	}
	else
	{
		printf("[-]Error in binding.\n");
	}

	while (1)
	{
		printf("[+]Accepting new connections..\n");
		newSocket = accept(sockfd, (struct sockaddr *)&newAddr, &addr_size);
		if (newSocket < 0)
		{
			printf("[-]Accept failed.\n");
			exit(1);
		}
		printf("[+]Connection accepted from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));

		if ((childpid = fork()) == 0)
		{
			close(sockfd);

			while (1)
			{
				ssize_t msgLength;
				if ((msgLength = recv(newSocket, buffer, sizeof(buffer), 0)) <= 0)
				{ // check to see if client closed connection
					printf("[-]Disconnected from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
					break;
				}
				else if (strcmp(buffer, ":exit") == 0)
				{
					printf("[-]Disconnected from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
					break;
				}
				else // sending the file back to the proxy.
				{
    				int fd;
					char fileContent[1024], c;
					buffer[msgLength] = '\0'; // make sure that we only look at the message we read in
					printf("[+]Proxy requests: %s, read %d bytes from buffer \n", buffer, (int)msgLength);
					// find the file from filename
					if ((fd = open(buffer, O_RDONLY)) == -1)
					{
						printf("[-]Error! opening file\n");
						// Program exits if the file pointer returns NULL.
						strncpy(buffer,"File does not exist.", sizeof(buffer));
						send(newSocket,buffer, sizeof(buffer), 0);
						break;
					}
					printf("[+]Opened file.\n");

					struct stat fileStat;
					if (fstat(fd, &fileStat) < 0)
					{
						printf("[-]Error at fstat\n");
						exit(1);
					}
					char fileSize[256];
					sprintf(fileSize, "%d", (int)fileStat.st_size);
					int len;
					if ((len = send(newSocket, fileSize, sizeof(fileSize), 0) < 0))
					{
						printf("[-]Fail on sending length of file size");
						exit(1);
					}

					off_t offset = 0;
					int remainingData = fileStat.st_size;
					// sending file data
					int sentBytes = 0;
					while ((sentBytes = sendfile(newSocket, fd, &offset, BUFSIZ)) > 0 && remainingData > 0)
					{
						remainingData -= sentBytes;
					}
					printf("[+]Finished sending file to Proxy\n");
					close(fd);
					bzero(buffer, sizeof(buffer));
				}
			}
		}
	}
	close(newSocket);

	return 0;
}