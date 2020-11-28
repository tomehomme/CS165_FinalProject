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

#define PORT 9999

struct File {
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
	strcpy(memset(proxy->cache[proxy->numCache].fileName,0, sizeof(fileName)+1), fileName);
	strcpy(memset(proxy->cache[proxy->numCache].content,0, sizeof(file)+1), file+strlen(fileName)+2);
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
		printf("%s", proxy->cache[i].fileName);
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
void getFromCache(struct Proxy *proxy, const char *fileName, char *buffer) {
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

int tls_start(struct tls_config *cfg, struct tls *ctx)
{
	int success = 0;
	
	//Init TLS
	if (tls_init() != 0)
	{
		err(1, "tls_init:");
	}

	/*Configuring TLS*/

	if((cfg = tls_config_new()) == NULL)
	{
		err(1, "tls_config_new:");
		return success = 0;
	}

	printf("[+]TLS config created.\n");

	/*Setting the auth certificate for proxy*/

	if(tls_config_set_ca_file(cfg, "../certificates/root.pem") != 0) // Set the certificate file
	{
		err(1, "tls_config_set_ca_file:");
		return success = 0;
	}

	printf("[+]TLS proxy root certificate set.\n");

	if(tls_config_set_cert_file(cfg, "../certificates/root.pem") != 0) //Set server certificate
	{
		err(1, "tls_config_set_cert_file:");
		return success = 0;
	}

	printf("[+]TLS proxy server certificate set.\n");

	if(tls_config_set_key_file(cfg, "../certificates//root/private/ca.key.pem") != 0) //Set server certificate
	{
		err(1, "tls_config_set_key_file:");
		return success = 0;
	}

	printf("[+]TLS server private key set.\n");

	if((ctx = tls_server())== NULL)
	{
		err(1, "tls_server:");
		return success = 0;
	}

	printf("[+]TLS proxy created.\n");

	if(tls_configure(ctx, cfg) != 0)
	{
		err(1, "tls_configure: %s", tls_error(ctx));
		return success = 0;
	}
	printf("[+]TLS proxy instance created.\n");
	return success = 1;
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

	/* TLS Proxy Configuration */
	struct tls_config *cfg = NULL;
	struct tls *ctx = NULL;
	struct tls *cctx = NULL;
	uint8_t *mem;
	size_t mem_len;

	if (tls_start(cfg, ctx) == 1)
	{
		printf("[+]TLS server config completed.\n");
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

	//	// libtls setup
	// struct tls *ctx;
	// struct tls_config *config;
	// config = tls_config_new();
	// char ca_file[1024] = "../../certificates/root/certs/ca.cert.pem"; // certificate for server, not sure if this is the right one to use
	// tls_config_set_ca_file(config, ca_file);
	// ctx = tls_server(); // set up server context
	// tls_configure(ctx,config);

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
	if ((fp = fopen("../src/proxy/blacklisted.txt", "r")) == NULL)
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
		
		if ((childpid = fork()) == 0)
		{
			close(sockfd);

			while (1)
			{
				ssize_t msgLength;
				if ((msgLength = recv(newSocket, buffer, sizeof(buffer), 0)) <= 0)
				{ // check to see if client closed connection
					printf("[-]Disconnected from %s:%d\n\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
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
					if (isInBloomFilter(proxy.bloomFilter, buffer))
					{
						// 1a. if isInBloomFilter() == 1, then respond "Request Denied" becuase item is blacklisted
						send(newSocket, "Access Denied.", sizeof(buffer), 0);
					}
					else
					{
						// 1b. is isInBloomFilter() == 0, then run isInBlacklist(). If == 1, then respond "Request Denied"
						if (isInBlackList(&proxy, buffer))
						{
							printf("[!]File in blacklist. Denying access\n");
							send(newSocket, "Access Denied.", sizeof(buffer), 0);
						}
						else
						{
							// 2. check the cache files to see if file is stored
							if (!isInCache(&proxy, buffer))
							{
								printf("[+]File not in cache. Initiating handshake with server\n");
								// 3. TLS connection/handshake with server and request file
								memset(&server, 0, sizeof(server));
								server.sin_family = AF_INET;
								server.sin_port = htons(9998);
								server.sin_addr.s_addr = inet_addr("127.0.0.1");
								if (server.sin_addr.s_addr == INADDR_NONE)
								{
									fprintf(stderr, "Invalid IP address 127.0.0.1 \n");
									usage();
								}

								/* ok now get a socket. we don't care where... */
								if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
									err(1, "socket failed");

								/* connect the socket to the server described in "server_sa" */
								if (connect(serverSock, (struct sockaddr *)&server, sizeof(server)) == -1)
								{
									err(1, "connect failed");
								}
								send(serverSock, buffer, sizeof(buffer), 0);
								int serverMsgLength = 0;
								if ((serverMsgLength = recv(serverSock, buffer, sizeof(buffer), 0)) <= 0)
								{
									printf("[-]Disconnected from %s:%d\n\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
									break;
								}
								else
								{
									printf("[+]Received '%s' from server.\n", buffer);
									if (strcmp(buffer, "File does not exist.") == 0)
									{
										strncpy(buffer, "Access Denied. File does not exist.", sizeof(buffer));
										send(newSocket, buffer, sizeof(buffer), 0);
										printf("File does not exist.\n");
										break;
									}
								}
								// 3a. store the file in the cache
								printf("[+]Adding file to cache...\n");
								addToCache(&proxy, buffer, fileName);
								printf("[+]Finished adding to cache. Cache size: %d\n", proxy.numCache);
							}
							// 4. send file to client over
							getFromCache(&proxy, fileName, buffer);
							send(newSocket, buffer, sizeof(buffer), 0);
							printf("[+]Finished sending file to client\n");
							bzero(buffer, sizeof(buffer));
						}
					}
					// 5. close connection
				}
			}
		}
	}
	close(newSocket);

	return 0;
}