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

struct Proxy
{
	int port;
	char *name;
};

static void usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s filename\n", __progname);
	exit(1);
}

/**
 * Given a string, will return the ascii sum of each character in it
 * 
 * */

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

/**
 * Given array of 5 Proxies, and the file name will perform a rendezvous hashing scheme.
 * Concatenates the file name with each proxy name and hashes the strings to get 5 hash values
 * returns the index of the proxy with the highest hash value.
 * */
int whichProxy(const struct Proxy *proxies, const char *fileName)
{
	int hash[5] = {0,0,0,0,0}; // hold hash values for each proxy
	int maxIndex = 0;
	for (int i = 0; i < 5; i++) {
		hash[i] = stringToInt(fileName)+stringToInt(proxies[i].name); // concatenate object name with proxy name
		hash[i] = hash[i] % 17; // hash the string s_i
		if (hash[maxIndex] < hash[i]) {
			// pick the highest hash value
			maxIndex = i;
		}
	}
	// return the proxy number
	return maxIndex;
}

// your application name filename
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
	int remainingData = 0;

	/* Creating structs for TLS */
	struct tls_config *cfg = NULL;
	struct tls *ctx = NULL;
	struct tls *cctx = NULL;

	
	/* Done configuring tls */
	if (argc != 2) // not enough arguments passed in
	{
		usage();
	}


	// 5 proxies
	struct Proxy *proxies = malloc(5*sizeof(struct Proxy)); 
	proxies[0] = (struct Proxy) {.name = "ProxyOne", .port = 9990};
	proxies[1] = (struct Proxy) {.name = "ProxyTwo", .port = 9991};
	proxies[2] = (struct Proxy) {.name = "ProxyThree", .port = 9992};
	proxies[3] = (struct Proxy) {.name = "ProxyFour", .port = 9993};
	proxies[4] = (struct Proxy) {.name = "ProxyFive", .port = 9994};

	// grab the filename from argument
	strcpy(fileName, argv[1]);
	printf("[+]File Name: %s\n", fileName);

	// get the port of the proxy we should connect to
	port = proxies[whichProxy(proxies, fileName)].port;
	printf("[+]Connecting to Port: %d\n", port);

	/*
	* first set up "serverAddr" to be the location of the server
	*/
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	clientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (clientSocket < 0)
	{
		printf("[-]Error in connection.\n");
		exit(1);
	}

	printf("[+]Client Socket is created.\n");

	printf("[+]Running TLS Configuration for client\n");
	/* Calling TLS */

	if((tls_init()) != 0)
	{
		err(1, "[-]TLS could not be initialized\n");
	}
	
	printf("[+]TLS initialized.\n");
	
	if((cfg = tls_config_new()) == NULL) //Initiates client TLS config.
	{
		err(1, "[-]TLS Config could not finish.\n");
	}

	printf("[+]TLS config created.\n");

	if(tls_config_set_ca_file(cfg, "../../certificates/root.pem") != 0) //Sets client root certificate.
	{
		err(1, "[-]Could not set client root certificate.\n");
	}

	printf("[+]TLS certificate set.\n");

	tls_config_insecure_noverifyname(cfg); // Needed to use tls_connectsocket as proxy was trying to verify a name within the certificate.

	if((ctx = tls_client())== NULL)
	{
		err(1, "[-]Could not create client TLS context.\n");
	}

	printf("[+]TLS client context created.\n");

	if(tls_configure(ctx, cfg) != 0)
	{
		err(1, "[-]Could not create client TLS configuration.\n");
	}
	
	printf("[+]TLS client instance created.\n");

	connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));

	printf("[+]Connected to proxy.\n");
	
	/*TLS Connect Check*/

	if((tls_connect_socket(ctx, clientSocket, "client")) != 0)
	{
		err(1, "[-]tls_connect_socket: %s", tls_error(ctx));
	}

	printf("[+]Connected to proxy socket and intializing handshake...\n");

	if((tls_handshake(ctx) != 0))
	{
		errx(1, "[-]Could not establish handshake with proxy.\n");
	}

	printf("[+]Handshake successful. Connection secured to proxy with TLS\n");

	while (1)
	{
		ssize_t written, w;
		w = 0;
		written = 0;
		while (written < strlen(fileName))
		{
			if((w = tls_write(ctx, fileName+written, strlen(fileName) - written)) <= 0)
			{
				err(1, "tls_write: %s", tls_error(ctx));
				
			}
			else
				written += w;
		}

		printf("[+]Sent Proxy file name: %s\n", fileName);

		// recieve the file size
		if((tls_read(ctx, buffer, sizeof(buffer))) <= 0)
		{
			err(1, "tls_read from proxy error");
		}

		// first check to see if the file is denied or does not exist..
		if (strstr(buffer, "Denied") != NULL)
		{
			printf("[!]%s File is blacklisted.\n", buffer);
			exit(1);
		}
		else if (strstr(buffer, "File does not exist") != NULL)
		{
			printf("[!]%s\n", buffer);
			exit(1);
		}
		else
		{
			printf("[+]Finished receiving '%s'. Printing contents...\n", fileName);
			printf("%s\n", buffer);
			bzero(buffer, sizeof(buffer)); //Zero out buffers
			bzero(fileName, sizeof(fileName)); //Zero out buffers
			free(ctx);
			close(clientSocket);
			return 0;
		}
	}

	return 0;
}