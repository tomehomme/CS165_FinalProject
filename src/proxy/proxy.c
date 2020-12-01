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

#include <pthread.h>
#include <fcntl.h>
#include <math.h>
#include <tls.h> // for TLS
#define PORT 9999

struct File
{
	char fileName[1024];
	char content[1024];
};

struct BloomFilter
{
	double size; // number of indices in the bloomfilter
	u_int8_t *bloomFilter;
};

struct Proxy
{
	struct BloomFilter *bloomFilter;
	char *blackListed[30000];
	int numBlacklist;
	struct File *cache;
	int numCache;
};

/**
 * Adds ASCII value in string to convert to integer value
 * returns int value of the string
 * */
int stringToInt(const char *object)
{
	long k = 0;
	int i = 0;
	while (object[i] != '\0')
	{
		k += object[i];
		i++;
	}
	return k;
}

/**
 * Adds file to the cache
 * 
 * */
void addToCache(struct Proxy *proxy, const char *file, const char *fileName)
{
	strcpy(memset(proxy->cache[proxy->numCache].fileName, 0, sizeof(fileName) + 1), fileName);
	strcpy(memset(proxy->cache[proxy->numCache].content, 0, sizeof(file) + 1), file + strlen(fileName) + 2);
	proxy->numCache++;
}

/**
 * Checks to see if the fileName is in the proxy's cache
 * Returns 1 if the file is in the cache
 * Returns 0 if the file is not in cacne
 * */
int isInCache(struct Proxy *proxy, const char *fileName)
{
	int i;
	for (i = 0; i < proxy->numCache; i++)
	{
		// printf("%s", proxy->cache[i].fileName);
		if (strcmp(proxy->cache[i].fileName, fileName) == 0)
		{
			return 1;
		}
	}
	return 0;
}

/**
 *  Returns file from cache into buffer
 *  fileName: fileContent
 * */
void getFromCache(struct Proxy *proxy, const char *fileName, char *buffer)
{
	int i;
	for (i = 0; i < proxy->numCache; i++)
	{
		if (strcmp(proxy->cache[i].fileName, fileName) == 0)
		{
			// if we have the correct file
			strcpy(buffer, proxy->cache[i].fileName);
			strcat(buffer, ": ");
			strcat(buffer, proxy->cache[i].content);
		}
	}
}
/**
 *  Adds object to bloom filter using 5 different hash functions.
 * 
 **/
void hash(struct BloomFilter *bloomFilter, const char *object)
{

	int k = stringToInt(object);
	bloomFilter->bloomFilter[k % (int)bloomFilter->size] = 1;
	bloomFilter->bloomFilter[k % 677] = 1;
	bloomFilter->bloomFilter[k % 367] = 1;
	bloomFilter->bloomFilter[k % 9949] = 1;
	bloomFilter->bloomFilter[k % 19793] = 1;
}

/**
 *  Checks to see if the BloomFilter contains the fileName
 *  If bloomFilter[index] corresponding to hash(fileName) is 0
 *  Then the file is NOT in the bloom filter.
 *  
 * 
 *  Returns 1 if the item is possibly contained, 0 if not
 * */
int isInBloomFilter(struct BloomFilter *bloomFilter, const char *fileName)
{

	int k = stringToInt(fileName);
	if (bloomFilter->bloomFilter[k % (int)bloomFilter->size] == 0)
		return 0;
	if (bloomFilter->bloomFilter[k % 677] == 0)
		return 0;
	if (bloomFilter->bloomFilter[k % 367] == 0)
		return 0;
	if (bloomFilter->bloomFilter[k % 9949] == 0)
		return 0;
	if (bloomFilter->bloomFilter[k % 19793] == 0)
		return 0;

	// if all bits are 1, then the item is in the bloom filter.
	return 1;
}

/**
 *  Checks to see if the file is in the black list.
 *  Use this function after isInBloomFilter() returns 0.
 * */
int isInBlackList(struct Proxy *proxy, const char fileName[])
{
	int i;
	for (i = 0; i < proxy->numBlacklist; i++)
	{
		if (strcmp(proxy->blackListed[i], fileName) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static void usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s -port portnumber -server serverportnumber\n", __progname);
	exit(1);
}

static void kidhandler(int signum)
{
	/* signal handler for SIGCHLD */
	waitpid(WAIT_ANY, NULL, WNOHANG);
}

struct thread_data
{
	struct Proxy *proxy;
	int newSocket;
	struct sockaddr_in newAddr;
	struct sockaddr_in server;
	int serverSock;
};

pthread_mutex_t lock;

void *handleClient(void *inputs)
{
	while (1)
	{
		char buffer[1024];
		struct thread_data *thread_data = (struct thread_data *)inputs;
		ssize_t msgLength;
		if ((msgLength = recv(thread_data->newSocket, buffer, sizeof(buffer), 0)) <= 0)
		{ // check to see if client closed connection
			printf("[-]Disconnected from %s:%d\n\n", inet_ntoa(thread_data->newAddr.sin_addr), ntohs(thread_data->newAddr.sin_port));
			break;
		}
		else // sending the file back to the user.
		{
			int fd;
			char fileName[1024], c;
			buffer[msgLength] = '\0'; // make sure that we only look at the message we read in
			strcpy(fileName, buffer);

			printf("[+]Client requests: '%s'\n", fileName);
			// 1. Check compute hash bloom filter first with isInBloomFilter() function
			if (isInBloomFilter(thread_data->proxy->bloomFilter, buffer))
			{
				// 1a. if isInBloomFilter() == 1, then respond "Request Denied" becuase item is blacklisted
				send(thread_data->newSocket, "Access Denied.", sizeof(buffer), 0);
			}
			else
			{
				// 1b. is isInBloomFilter() == 0, then run isInBlacklist(). If == 1, then respond "Request Denied"
				if (isInBlackList(thread_data->proxy, buffer))
				{
					printf("[!]File in blacklist. Denying access\n");
					send(thread_data->newSocket, "Access Denied.", sizeof(buffer), 0);
				}
				else
				{
					// mutex so that we don't have multiple threads checking if the same file is not yet in the cache
					pthread_mutex_lock(&lock);
					// 2. check the cache files to see if file is stored
					if (!isInCache(thread_data->proxy, buffer))
					{
						printf("[+]File not in cache. Initiating handshake with server\n");
						// 3. TLS connection/handshake with server and request file
						memset(&thread_data->server, 0, sizeof(thread_data->server));
						thread_data->server.sin_family = AF_INET;
						thread_data->server.sin_port = htons(9998);
						thread_data->server.sin_addr.s_addr = inet_addr("127.0.0.1");
						if (thread_data->server.sin_addr.s_addr == INADDR_NONE)
						{
							fprintf(stderr, "Invalid IP address 127.0.0.1 \n");
							usage();
						}

						/* ok now get a socket. we don't care where... */
						if ((thread_data->serverSock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
							err(1, "socket failed");

						/* connect the socket to the server described in "server_sa" */
						if (connect(thread_data->serverSock, (struct sockaddr *)&thread_data->server, sizeof(thread_data->server)) == -1)
						{
							err(1, "connect failed");
						}
						send(thread_data->serverSock, buffer, sizeof(buffer), 0);
						int serverMsgLength = 0;
						if ((serverMsgLength = recv(thread_data->serverSock, buffer, sizeof(buffer), 0)) <= 0)
						{
							printf("[-]Disconnected from %s:%d\n\n", inet_ntoa(thread_data->newAddr.sin_addr), ntohs(thread_data->newAddr.sin_port));
							break;
						}
						else
						{
							printf("[+]Received '%s' from server.\n", buffer);
							if (strcmp(buffer, "File does not exist.") == 0)
							{
								strncpy(buffer, "Access Denied. File does not exist.", sizeof(buffer));
								send(thread_data->newSocket, buffer, sizeof(buffer), 0);
								printf("File does not exist.\n");
								break;
							}
						}
						// 3a. store the file in the cache
						printf("[+]Adding file to cache...\n");
						addToCache(thread_data->proxy, buffer, fileName);
						printf("[+]Finished adding to cache. Cache size: %d\n", thread_data->proxy->numCache);
					}
					 // unlock mutex
					 pthread_mutex_unlock(&lock);
					// 4. send file to client over
					getFromCache(thread_data->proxy, fileName, buffer);
					send(thread_data->newSocket, buffer, sizeof(buffer), 0);
					printf("[+]Finished sending file to client\n");
					bzero(buffer, sizeof(buffer));
				}
				// 5. close connection
			}
		}
	}
}

// your application name -port portnumber
int main(int argc, char *argv[])
{

	// relative to this proxy
	int sockfd, ret;
	struct sockaddr_in proxyAddr;
	char buffer[1024], *ep;
	u_long p;
	u_short port;

	// for any new connections
	int newSocket;
	struct sockaddr_in newAddr;
	socklen_t addr_size;
	pid_t childpid;
	pid_t pid;

	// server
	struct sockaddr_in server;
	int serverSock;
	pid_t serverPID;

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
	port = p;

	// initialize the proxy w/ blacklist & bloomfilter
	struct Proxy proxy;
	struct BloomFilter bloomFilter;

	proxy.cache = malloc(30000 * sizeof(struct File *));
	proxy.numCache = 0;

	proxy.bloomFilter = &bloomFilter;
	bloomFilter.size = pow(2, 32) - 1; // to hold 30000 obj
	bloomFilter.bloomFilter = malloc(bloomFilter.size * sizeof(u_int8_t));
	memset(bloomFilter.bloomFilter, 0, sizeof(bloomFilter.bloomFilter));
	printf("[+]Reading black-listed objects from 'blacklisted.txt' and adding to black list\n");

	FILE *fp;
	char blackListFile[1024];
	size_t fileLen = 0;
	ssize_t read;
	int i = 0;
	if ((fp = fopen("blacklisted.txt", "r")) == NULL)
	{
		printf("[-]Failed to open the 'blacklisted.txt' file! Terminating program.\n");
		exit(1);
	}
	while (fgets(blackListFile, sizeof(blackListFile), fp) != NULL)
	{
		if (blackListFile[strlen(blackListFile) - 1] == '\n')
		{
			blackListFile[strlen(blackListFile) - 1] = '\0'; // eat the newline fgets() stores
		}
		// add file to blacklist
		printf("\tADDING: '%s' to blacklist\n", blackListFile);
		proxy.blackListed[i] = strcpy(malloc(strlen(blackListFile) + 1), blackListFile);
		i++;
	}
	proxy.numBlacklist = i; // holds the number of blacklisted items
	fclose(fp);

	printf("[+]Successfully added blacklisted objects to black List.\n");

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		printf("[-]Error in connection.\n");
		exit(1);
	}
	printf("[+]Proxy Socket is created.\n");

	memset(&proxyAddr, '\0', sizeof(proxyAddr));
	proxyAddr.sin_family = AF_INET;
	proxyAddr.sin_port = htons(port);
	proxyAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (proxyAddr.sin_addr.s_addr == INADDR_NONE)
	{
		fprintf(stderr, "Invalid IP address 127.0.0.1 \n");
		usage();
	}

	ret = bind(sockfd, (struct sockaddr *)&proxyAddr, sizeof(proxyAddr));
	if (ret < 0)
	{
		printf("[-]Error in binding.\n");
		exit(1);
	}
	printf("[+]Bind to port %d\n", port);

	if (listen(sockfd, 10) == 0)
	{
		printf("[+]Listening....\n\n");
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

		pthread_t thread_id;
		struct thread_data *thread_data = malloc(sizeof(struct thread_data));

		thread_data->proxy = &proxy;
		thread_data->newSocket = newSocket;
		thread_data->newAddr = newAddr;
		thread_data->server = server;
		thread_data->serverSock = serverSock;
		// create a thread to handle this connection
		if (pthread_create(&thread_id, NULL, &handleClient, thread_data))
		{
			fprintf(stderr, "No threads for you.\n");
			return 1;
		}
		// close thread
		pthread_join(thread_id, NULL);
	}
	close(newSocket);

	return 0;
}