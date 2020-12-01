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
	int remainingData = 0;

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

	ret = connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	printf("[+]Connected.\n");

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