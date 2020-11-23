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

#include <tls.h> // for TLS

struct BloomFilter {
	int size; // number of indices in the bloomfilter
	int *bloomFilter;
	char *blackListed[30000];
	int numBlacklist;
};

void hash(struct BloomFilter *bloomFilter, const char object[]) {
	/**
	 *  Adds object to bloom filter using 5 different hash functions.
	 * 
	 **/ 
	u_int32_t k = atoi(object);
	bloomFilter->bloomFilter[k % bloomFilter->size] = 1;
	bloomFilter->bloomFilter[k % 3] = 1;
	bloomFilter->bloomFilter[k % 5] = 1;
	bloomFilter->bloomFilter[k % 7] = 1;
	bloomFilter->bloomFilter[k % 23] = 1;
}

int contains(struct BloomFilter *bloomFilter, const char fileName[]) {
	/**
	 *  Checks to see if the BloomFilter contains the fileName
	 *  If bloomFilter[index] corresponding to hash(fileName) is not 1,
	 *  Then the file is NOT in the bloom filter.
	 *  Otherwise, the file possible is contained in the bloom filter.
	 * 
	 *  Returns 1 if the item is possibly contained, 0 if not
	 * */

	u_int32_t k = atoi(fileName);
	if (bloomFilter->bloomFilter[k % bloomFilter->size] == 0) return 0;
	if (bloomFilter->bloomFilter[k % 3] == 0) return 0;
	if (bloomFilter->bloomFilter[k % 5] == 0) return 0;
	if (bloomFilter->bloomFilter[k % 7] == 0) return 0;
	if (bloomFilter->bloomFilter[k % 23] == 0) return 0;
	
	return 1;
}

int isInBlackList(struct BloomFilter *bloomFilter, const char fileName[]) {
	/**
	 *  Checks to see if the file is in the black list.
	 *  Use this function after contains() returns 1.
	 * */
	int i;
	for (i = 0; i < bloomFilter->numBlacklist; i++) {
		if (strcmp(bloomFilter->blackListed[i], fileName) == 0) {
			return 1;
		}
	}
	return 0;
}

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

	// relative to this proxy
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
	port = p;


//	// libtls setup
	// struct tls *ctx;
	// struct tls_config *config; 
	// config = tls_config_new();
	// char ca_file[1024] = "../../certificates/root/certs/ca.cert.pem"; // certificate for server, not sure if this is the right one to use
	// tls_config_set_ca_file(config, ca_file);
	// ctx = tls_server(); // set up server context
	// tls_configure(ctx,config);

	printf("[+]Creating Bloom Filter...\n");
	struct BloomFilter bloomFilter;
	bloomFilter.size = 30000;
	bloomFilter.bloomFilter = malloc(bloomFilter.size * sizeof(int));
	
	printf("[+]Reading black-listed objects from 'blacklisted.txt' and adding to bloom filter\n");
	
	FILE *fp;
	char fileName[1024]; 
	size_t fileLen = 0;
	ssize_t read;
	int i = 0;
	if ((fp = fopen("blacklisted.txt", "r")) == NULL) {
		printf("[-]Failed to open the file! Terminating program.\n");
		exit(1);
	}
	while (fgets(fileName, sizeof(fileName), fp) != NULL)
	{
		if (fileName[strlen(fileName)-1] == '\n') {
			fileName[strlen(fileName) - 1] = '\0'; // eat the newline fgets() stores
		}
		// file to bloom filter.
		printf("\tADDING: '%s' to bloom filter\n", fileName);
		bloomFilter.blackListed[i] = strcpy(malloc(strlen(fileName)+1), fileName);
		hash(&bloomFilter, fileName);
		i++;
	}
	bloomFilter.numBlacklist = i; // holds the number of blacklisted items
	fclose(fp);

	printf("[+]Successfully added blacklisted objects to bloom filter.\n");


	printf("[.]Checking to see if BloomFilter hashing works.\n");
	printf("\t[.]Check if present items return 1\n");
	for (i = 0; i < bloomFilter.numBlacklist; i++) {
		if (contains(&bloomFilter, bloomFilter.blackListed[i]) == 0) {
			printf("[-]FAIL: Bloom filter returns false negative. FileName: '%s'\n", bloomFilter.blackListed[i]);
			exit(1);
		}
	}
	printf("\t[.]Check if non present items return 0 in Blacklist\n");
	char nonExistingFiles[7][20] = {"test.txt", "Test1.txt", "notIn.txt", "ok.txt", "helloworld.txt", "BLACKLIST.txt", "a.txt"};
	for (i = 0; i < 7; i++) {
		if (contains(&bloomFilter, nonExistingFiles[i]) == 1) {
			printf("\t\t[.]Bloom filter marked 1 for FileName: %s. Checking blacklist...\n", nonExistingFiles[i]);
			if (isInBlackList(&bloomFilter, nonExistingFiles[i]) == 1) {
				printf("[-]FAIL: Blacklist marks object as blacklisted\n");
				exit(1);
			}
			
		}
	}

	printf("[+]Bloom filter passes all checks.\n");


	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		printf("[-]Error in connection.\n");
		exit(1);
	}
	printf("[+]Proxy Socket is created.\n");

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
		// struct tls **cctx;
		// if (tls_accept_socket(ctx, cctx, newSocket) == -1){
			// 	printf("[-]Error on tls_accept_socket\n");
			// 	exit(1);
		// }
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
				else // sending the file back to the user.
				{

					int fd;
					char fileContent[1024], c;
					buffer[msgLength] = '\0'; // make sure that we only look at the message we read in
					printf("[+]Client requests: %s, read %d bytes from buffer \n", buffer, (int)msgLength);
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
					printf("[+]Finished sending file to client\n");
					close(fd);
					bzero(buffer, sizeof(buffer));
				}
			}
		}
	}
	close(newSocket);

	return 0;
}