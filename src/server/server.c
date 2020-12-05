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

/**
 *  Finds the filename in the database and puts the content into buffer 
 *  returns -1 if the file is not found
 * */
int getFileContent(FILE *database, const char *filename, char *buffer)
{
	// find the file in the 'files.txt' file
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	// read until new line
	while ((read = getline(&line, &len, database)) > 0)
	{
		line[read-2] = '\0';
		//printf("'%s'\n", line);
		if (strstr(line, filename) != NULL)
		{
			strcpy(buffer, line);
			return 0;
		}
	}
	return -1;
}

static void usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s\n", __progname);
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

	/* TLS Server Configuration */
	struct tls_config *cfg = NULL;
	struct tls *ctx = NULL;
	struct tls *cctx = NULL;
	uint8_t *mem;
	size_t mem_len;

		//Init TLS
	if (tls_init() != 0)
	{
		errx(1, "tls_init:");
	}

	/*Configuring TLS*/

	if((cfg = tls_config_new()) == NULL)
	{
		err(1, "tls_config_new:");
	}

	printf("[+]TLS config created.\n");

	/*Setting the auth certificate for proxy*/

	if(tls_config_set_ca_file(cfg, "../../certificates/root.pem") != 0) // Set the certificate file
	{
		err(1, "[-]tls_config_set_ca_file error\n");
	}

	printf("[+]TLS server root certificate set.\n");

	if(tls_config_set_cert_file(cfg, "../../certificates/root.pem") != 0) //Set server certificate
	{
		err(1, "[-]tls_config_set_cert_file error\n");
	}

	printf("[+]TLS server certificate set.\n");

	if(tls_config_set_key_file(cfg, "../../certificates//root/private/ca.key.pem") != 0) //Set server certificate
	{
		err(1, "[-]tls_config_set_key_file error");
	}

	printf("[+]TLS server private key set.\n");

	if((ctx = tls_server())== NULL)
	{
		err(1, "[-]tls_server error");
	}

	printf("[+]TLS server created.\n");

	if(tls_configure(ctx, cfg) != 0)
	{
		err(1, "[-]tls_configure: %s", tls_error(ctx));
	}
	printf("[+]TLS server instance created.\n");

	errno = 0;
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
			exit(1);
		}
		printf("[+]Connection accepted from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));

		printf("[+]Securing socket with TLS...\n");
		if(tls_accept_socket(ctx, &cctx, newSocket) != 0)
		{
			errx(1, "[-]New socket could not be accepted.\n");
		}
		printf("[+]Socket secured with TLS.\n");
		printf("[+]Connection accepted from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));

		if ((childpid = fork()) == 0)
		{
			close(sockfd);

			while (1)
			{
				ssize_t msgLength;
				//if ((msgLength = recv(newSocket, buffer, sizeof(buffer), 0)) <= 0)
				if ((msgLength = tls_read(cctx, buffer, sizeof(buffer))) <= 0)
				{ // check to see if client closed connection
					printf("[-]Disconnected from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
					break;
				}
				else // sending the file back to the proxy.
				{
					int fd;
					char fileContent[1024], c;
					buffer[msgLength] = '\0'; // make sure that we only look at the message we read in
					printf("[+]Proxy requests: '%s'\n", buffer);
					// find the file from filename
					FILE *db;
					if ((db = fopen("../../src/server/files.txt", "r")) == NULL)
					{ // will store the filename: content for all files in files.txt
						printf("[-]Error! opening file 'files.txt'\n");
						strncpy(buffer, "File does not exist.", sizeof(buffer));
						//send(newSocket, buffer, sizeof(buffer), 0);
						if((tls_write(cctx, buffer, sizeof(buffer))) <= 0)
						{
							err(1, "tls_write: %s", tls_error(ctx));
						};
						bzero(buffer, sizeof(buffer));
						bzero(fileName, sizeof(fileName));
						close(newSocket);
						break;
					}

					if (getFileContent(db, buffer, fileContent) == -1)
					{ // if file does not exist in files.txt

						printf("[-]'%s' does not exist\n", buffer);
						strncpy(buffer, "File does not exist.", sizeof(buffer));
						// send(newSocket, buffer, sizeof(buffer), 0);
						if((tls_write(cctx, buffer, sizeof(buffer))) <= 0)
						{
							err(1, "tls_write: %s", tls_error(ctx));
						};
						printf("[-]Disconnected from proxy\n\n");
						bzero(buffer, sizeof(buffer));
						bzero(fileName, sizeof(fileName));
						close(newSocket);
						break;
					}
					else
					{
						printf("Sending file: filecontent to proxy: '%s'\n",fileContent);
						//send(newSocket, fileContent, sizeof(fileContent), 0);
						if((tls_write(cctx, fileContent, sizeof(fileContent))) <= 0)
						{
							err(1, "tls_write: %s", tls_error(ctx));
						};
						printf("[+]Finished sending file to Proxy\n\n");
						bzero(buffer, sizeof(buffer));
						bzero(fileName, sizeof(fileName));
						close(newSocket);
						break;
					}
					
				}
			}
		}
	}
	close(newSocket);

	return 0;
}