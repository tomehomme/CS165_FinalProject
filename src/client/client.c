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

#define PORT 9999

static void usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s filename\n", __progname);
	exit(1);
}

int stringToInt(const char *fileName)
{
	long k = 0;
	int i = 0;
	while (fileName[i] != '\0')
	{
		k += fileName[i];
		i++;
	}
	return k;
}

int whichProxy(const char *fileName)
{
	return stringToInt(fileName) % 5;
}

struct Proxy
{
	int port;
	char name[];
};

int tls_start(struct tls_config *cfg, struct tls *ctx)
{
	int success = 0;
	/* Calling TLS */

	if((tls_init()) != 0)
	{
		perror("TLS could not be initialized");
		return success = 0;
	}

	if((cfg = tls_config_new()) == NULL) //Initiates client TLS config.
	{
		perror("TLS Config could not finish.");
		return success = 0;
	}

	printf("[+]TLS config created.\n");

	if(tls_config_set_ca_file(cfg, "../certificates/root.pem") != 0) //Sets client root certificate.
	{
		perror("Could not set client root certificate.");
		return success = 0;
	}

	printf("[+]TLS certificate set.\n");

	if(tls_config_set_key_file(cfg, "../certificates//root/private/ca.key.pem") != 0) //Sets client private key.
	{
		perror("Could not set private client key.");
		return success = 0;
	}

	printf("[+]TLS server private key set.\n");

	if((ctx = tls_client())== NULL)
	{
		perror("Could not create client TLS context.");
		return success = 0;
	}

	printf("[+]TLS server created.\n");

	if(tls_configure(ctx, cfg) != 0)
	{
		perror("Could not create client TLS configuration.");
		return success = 0;
	}
	printf("[+]TLS server instance created.\n");
	return success = 1;
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

	/* Creating structs for TLS */

	struct tls_config *cfg = NULL;
	struct tls *ctx = NULL;

	if (tls_start(cfg, ctx) == 1)
	{
		printf("[+]TLS client config completed.\n");
	} 

	if (argc != 2) // not enough arguments passed in
	{
		usage();
	}
	/* Done configuring tls */

	/* now safe to do this */
	port = PORT;

	/*
	* first set up "serverAddr" to be the location of the server
	*/
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	struct Proxy proxies[5]; // 5 proxies

	// grab the filename from argument
	strcpy(fileName, argv[1]);
	printf("[+]File Name: %s\n", fileName);

	// TODO: 1. compute has of the file name & proxies
	// 1a. simply call whichProxy(fileName) to get the index of the proxy
	// 2. TLS handshake with selected proxy & portnumber
	// 3. send filename over TLS
	// 4. recieve content of file requested
	// 5. display content of file
	// 6. close connection

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
			w = write(clientSocket, fileName + written, strlen(fileName) - written);
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

		// recieve the file size
		if (recv(clientSocket, buffer, sizeof(buffer), 0) < 0)
		{
			printf("[-]Recv failed!\n");
			exit(1);
		}

		// // first check to see if the file is denied..
		if (strstr(buffer, "Denied") != NULL)
		{
			printf("%s\n", buffer);
			exit(1);
		}
		else
		{
			printf("[+]Finished receiving '%s'. Printing contents...\n", fileName);
			printf("%s\n", buffer);
			close(clientSocket);
			return 0;
		}
	}

	return 0;
}