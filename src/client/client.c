#include <arpa/inet.h>

#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tls.h>
// functions for TLS: 
// tls_init(void): 
		// The tls_init() function initializes global data structures. 
		// It is no longer necessary to call this function directly, since it is invoked internally when needed. 
		// It may be called more than once, and may be called concurrently.
// struct tls_config* tls_config_new(void):
		// create configuration before connection is created
		// allocaties, init, and returns new default configuration for future connections
		// --> tls_config_set_protocols(3), tls_load_file(3), tls_config_ocsp_require_stapling(3), and tls_config_verify(3).
// const char* tls_config_error(struct tls_config *config);
		// used to retrieve a string contiang more info about most recent error relating to a configurations
// tls connection object is created by tls_client (or tls_server) and configured with tls_configure
// client connection is init after configuration by calling tls_connect. server accepts new client connection with tls_accept _socket on already established socket connection
// input - tls_read, output - tls_write
// after use, tls connection closed with tls_close and then freed with tls_free.

static void usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s -port proxyportnumber filename\n", __progname);
	exit(1);
}

// your application name -port proxyportnumber filename
int main(int argc, char *argv[])
{

	int clientSocket, ret;
	struct sockaddr_in serverAddr;
	char buffer[1024], *ep;
	char fileName[1024];
	ssize_t r, rc;
	u_short port;
	u_long p;
	int sd;

	ssize_t len;
	int fileSize;
	FILE *recievedFile;
	int remainingData = 0;

	if (argc != 4) // not enough arguments passed in
	{
		usage();
	}

	p = strtoul(argv[2], &ep, 10); // grab proxyportnumber
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
	port = p;

	/*
	* first set up "serverAddr" to be the location of the server
	*/
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	// grab the filename from argument
	strcpy(fileName, argv[3]);
	printf("[+]File Name: %s\n", fileName);

	clientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (clientSocket < 0)
	{
		printf("[-]Error in connection.\n");
		exit(1);
	}
	// from bob-beck libtls tutorial
	// after you call connect, you call tls_connect_socket to associate a tls context with your connected socket
	// struct tls *ctx; 
	// struct tls_config *config;
	// config = tls_config_new();
	// if ((ctx = tls_client()) == NULL) { err(1, "[-]Failed to create tls_client\n"); }
	// if (tls_configure(ctx, config) == -1 ) { err(1, "[-]Failed to configure: %s", tls_error(ctx));}
	printf("[+]Client Socket is created.\n");


	ret = connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	// grab a socket
	// if (tls_connect(ctx, "127.0.0.1", argv[2]) == -1) {
	// 	printf("[-]Error on tls_connect_socket\n");
	// 	exit(1);
	// }
	printf("[+]Connected to Server.\n");

	// tls_handshake();

	while (1)
	{
		ssize_t written, w;
		w = 0;
		written = 0;
		while (written < strlen(fileName))
		{
			w = write(clientSocket, fileName+written, strlen(fileName) - written);
			// w = tls_write(ctx, fileName+written, strlen(fileName) - written);
			if (w == -1)
			{
				if (errno != EINTR)
					err(1, "write failed\n");
			}
			else
				written += w;
		}

		printf("[+]Sent Proxy file name: %s\n", fileName);

		if (strcmp(buffer, ":exit") == 0)
		{
			// if (tls_close(ctx) == -1) {errx(1, "failed to close: %s", tls_error(ctx));}
			close(clientSocket);
			printf("[-]Disconnected from server.\n");
		}
		// recieve the file size
		if (recv(clientSocket, buffer, sizeof(buffer), 0) < 0)
		{
			printf("[-]Error in receiving data.\n");
		}
		else
		{
			fileSize = atoi(buffer);
			recievedFile = fopen(fileName, "w");
			if (recievedFile == NULL)
			{
				fprintf(stderr, "[-]Failed to open file foo --> %s\n", strerror(errno));

				exit(EXIT_FAILURE);
			}
			remainingData = fileSize;
			while ((remainingData > 0) && ((len = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0))
			{
				fwrite(buffer, sizeof(char), len, recievedFile);
				remainingData -= len;
				printf("[+]Received %d bytes. Waiting on: %d bytes\n", (int)len, remainingData);
			}
			if (remainingData <= 0) {
				printf("[+]Finished receiving file. Closing Socket.\n");
				fclose(recievedFile);
				close(clientSocket);
				exit(0);
			}
			else {
				printf("[-]Something went wrong!\n");
				exit(1);
			}
		}
	}

	return 0;
}