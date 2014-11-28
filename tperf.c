#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <netdb.h>
#include <string.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <arpa/inet.h>



#define INTERVAL_MS (1000)
#define KBPS (1000)

#define NET_SOFTERROR (-1)
#define NET_HARDERROR (-2)

#define EOT "OK"

struct tperf_stream
{
    int       socket;

    /* non configurable members */
    size_t bytes_received;
    size_t bytes_sent;
    size_t bytes_received_this_interval;
    size_t bytes_sent_this_interval;

    int       blksize;              /* size of read/writes (-l) */
    char      *buffer;		/* data to send, mmapped */
    uint64_t  rate;                 /* target data rate in  */
};


// the procedure for retriving current system time
// used for calculating life time for each hashing process 
long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}

/*******************************************************************/
/* reads 'count' bytes from a socket  */
/********************************************************************/

int
Nread(int fd, char *buf, size_t count, struct tperf_stream *sp)
{
    register ssize_t r;
    register size_t nleft = count;
    long long prev_ms, now_ms, int_ms;
    uint64_t rate; 

    prev_ms = now_ms = current_timestamp();
    while (nleft > 0) {
        r = read(fd, buf, nleft);
        now_ms = current_timestamp();
        // printf("Read: %ld B %lld ms\n", r, now_ms);
        sp->bytes_received_this_interval += r;
        int_ms = now_ms - prev_ms;
        if (int_ms >= INTERVAL_MS) {
            rate = sp->bytes_received_this_interval / (int_ms);
            printf("Recv Rate: %ld KBps\n", rate);
            sp->bytes_received_this_interval = 0;
            prev_ms = now_ms;
        }
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN)
                break;
            else
                return errno;
        } else if (r == 0)
            break;

        nleft -= r;
        buf += r;
    }
    return count - nleft;
}


/*
 *                      N W R I T E
 */

int
Nwrite(int fd, const char *buf, size_t count)
{
    register ssize_t r;
    register size_t nleft = count;

    while (nleft > 0) {
        // printf("before write %ld, (%d, %p)\n", nleft, fd, buf);
	r = write(fd, buf, nleft);
        // printf("write %ld at %lld\n", r, current_timestamp());
	if (r < 0) {
	    switch (errno) {
		case EINTR:
		case EAGAIN:
		return count - nleft;

		case ENOBUFS:
		return NET_SOFTERROR;

		default:
		return NET_HARDERROR;
	    }
	} else if (r == 0)
	    return NET_SOFTERROR;
	nleft -= r;
	buf += r;
    }
    return count;
}

int
tcp_recv(struct tperf_stream *sp)
{
    int r;

    r = Nread(sp->socket, sp->buffer, sp->blksize, sp);
    printf("received %d\n", r);

    if (r < 0)
        return r;

    sp->bytes_received += r;

    return r;
}


/* iperf_tcp_send 
 *
 * sends the data for TCP
 */
int
tcp_send(struct tperf_stream *sp)
{
    int r;
    r = Nwrite(sp->socket, sp->buffer, sp->blksize);

    if (r < 0)
        return r;

    sp->bytes_sent += r;
    sp->bytes_sent_this_interval += r;

    return r;
}

void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}

static uint32_t getPP(uint32_t src) {
	uint32_t ans = (src << 16) | (38010U);
	uint32_t a1 = (uint8_t) ((ans >> 24) & 0xff);
	uint32_t a2 = (uint8_t) ((ans >> 16) & 0xff);
	uint32_t a3 = (uint8_t) ((ans >> 8) & 0xff);
	uint32_t a4 = (uint8_t) ((ans >> 0) & 0xff);
	return a2 << 24 | a1 << 16 | a4 << 8 | a3;
}

int
main_client(char *serverIP, int blksize, char *data, uint64_t rate, uint64_t nint) {
    int sockfd;
    int try = 0, sz;
    struct sockaddr_in servAddr, myAddr; /* server address */
    unsigned short servPort = 38010;     /* server port */  
    struct tperf_stream tps; 
    long long start_ms, end_ms, int_ms;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servAddr,sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr(serverIP);
    servAddr.sin_port = htons(servPort);

    if (connect(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) 
        DieWithError("ERROR on Connect");
    sz = sizeof(myAddr);
    getsockname(sockfd, (struct sockaddr *) &myAddr, &sz);
    printf("after connect %d pp 0x%x\n", sockfd, getPP(myAddr.sin_port));

    tps.socket = sockfd;
    tps.bytes_received = tps.bytes_sent = tps.bytes_received_this_interval = tps.bytes_sent_this_interval = 0;
    tps.blksize = blksize;
    tps.buffer = data;
    tps.rate = rate;

    if (rate != 0) {
        // Fixed Rate Transmission
        for (try = 0; try < nint; try++) {
            start_ms = current_timestamp();
            tps.bytes_sent_this_interval = 0;
            tps.blksize = rate * INTERVAL_MS * KBPS / (1000);
            tcp_send(&tps);
            end_ms = current_timestamp();
            int_ms = end_ms - start_ms;
            // printf("after send %lld, %lld, %lld\n", start_ms, end_ms, int_ms);
            if (int_ms < INTERVAL_MS)
                usleep((INTERVAL_MS - int_ms) * 1000);
        }
    } else {
        tcp_send(&tps);
    }
    
    printf("Start Recv...\n");
    tcp_recv(&tps);
    tps.blksize = 2;
    close(sockfd);
    return 0;

}

int main_server(int blksize, uint64_t rate, uint64_t nint) {
    int listenfd, connfd;
    struct sockaddr_in servAddr,cliAddr;
    socklen_t clilen;
    struct tperf_stream tps;
    long long start_ms, end_ms, int_ms;
    pid_t pid;

    listenfd = socket(AF_INET,SOCK_STREAM,0);

    bzero(&servAddr, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(38010);
    if (bind(listenfd, (struct sockaddr *) &servAddr, 
	   sizeof(servAddr)) < 0) 
        DieWithError("ERROR on bind");

    if (listen(listenfd, 5) < 0) /* allow 5 requests to queue up */ 
        DieWithError("ERROR on listen");


    for (;;) {
	    printf("start serving %d\n", listenfd);
	    clilen = sizeof(cliAddr);
	    connfd = accept(listenfd, (struct sockaddr *)&cliAddr, &clilen);
	    printf("after accept %d\n", connfd);

	    pid = fork();
	    if (pid != 0) {
		printf("New child: %d\n", pid);
		close(connfd);
		continue;
	    }
	    
	    tps.socket = connfd;
	    tps.bytes_received = tps.bytes_sent = tps.bytes_received_this_interval = tps.bytes_sent_this_interval = 0;
	    tps.blksize = blksize;
	    tps.rate = rate;

	    if (rate != 0) {
		// Fixed Rate Transmission
		tps.blksize = rate * INTERVAL_MS * KBPS * nint / (1000);
	    }
	    tps.buffer = malloc(tps.blksize);

	    start_ms = current_timestamp();
	    tcp_recv(&tps);
	    end_ms = current_timestamp();
	    printf("after recv %lld %lld\n", start_ms, end_ms);
	    int_ms = end_ms - start_ms;
	    if (int_ms != 0)
		printf("PID %d Overall Recv Rate: %lld KBps\n", getpid(), tps.blksize / (int_ms));
	    free(tps.buffer);
	    tps.blksize = 2;
	    tps.buffer = EOT;
	    tcp_send(&tps);

	    close(connfd);
	    if (pid == 0) {
		return;
	    }
    }
}

int main(int argc, char **argv) {
    int blksize = 0, fd, kDataSize;
    uint64_t rate = 0;
    uint64_t nint = 0;
    char mode = 's';
    char *data;
    int c, np = 1;
    pid_t pid;
    char *servIP = NULL;

    while ( (c = getopt(argc, argv, "csb:r:t:p:l:")) != -1) {
        switch (c) {
        case 'c':
            mode = 'c';
            break;
        case 's':
            mode = 's';
            break;
        case 'b':
            kDataSize = blksize = atoi(optarg);
            break;
        case 'r':
            rate = atoi(optarg);
            break;
        case 't':
            nint = atoi(optarg);
            break;
        case 'l':
            servIP = optarg;
            break;
        case 'p':
            np = atoi(optarg);
            break;
        default:
            printf ("?? getopt returned character code 0%o ??\n", c);
        }
    }


    if (rate != 0 && nint != 0) {
        kDataSize = rate * INTERVAL_MS * KBPS * nint / (1000);
    }

    if (mode == 'c' && servIP) {
// initialize data buffer to pseudorandom values.
        fd = open("/dev/urandom", O_RDONLY);
        data = malloc(kDataSize + 1);
        // read(fd, data, kDataSize);
        close(fd);
        while (np > 0) {
            if (fork() == 0) {
                return main_client(servIP, blksize, data, rate, nint);
            } else
                np--;
        }
        while (pid = waitpid(-1, NULL, 0)) {
            if (errno == ECHILD) {
                break;
            }
        }
    } else if (mode == 's') {
        main_server(blksize, rate, nint);
    }

    exit(0);
}
