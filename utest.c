#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>

#define BUFFSIZE 5096 
#define u32 uint32_t 
#define u64 uint64_t

#define SERVER_PORT (38055)

#define TCPM_PROT 1

#define TCPM_ON 1
#define TCPM_OFF 0

/* Flow Matcher */
struct FM {
// address pair - ipv4
	u64	ap;
// port pair
	u32	pp;
// protocol - So far we only consider TCP
	u32	prot;
};
typedef struct FM FM_t;
typedef struct FM *FM_ptr;

/* Flow Action */
// An action is high level end-to-end info for congestion control
struct FA {		
// status, ON or OFF
	u32	state;
};
typedef struct FA FA_t;
typedef struct FA *FA_ptr;

/*Flow Table Entry*/
struct FTE {
	u32	id;
	u32	valid;
	FM_t	matcher;
	FA_t	action;
	// there could be more...
};
typedef struct FTE FTE_t;
typedef struct FTE *FTE_ptr;

#define FTC_ADD	(1)
#define FTC_DEL	(2)
#define FTC_MOD	(3)

struct FTC {
	u32	type;
	FTE_t	entry;
};
typedef struct FTC FTC_t;
typedef struct FTC *FTC_ptr;

static void sendcmd(int sock, struct sockaddr_in sendsocket, FTC_t ftc) {
	/* Send message to the server */
	char buffer[BUFFSIZE];
	memcpy(buffer, &ftc, sizeof(ftc));
	int sendlen = sizeof(ftc);
	
	if (sendto(sock, buffer, sendlen, 0, (struct sockaddr *) &sendsocket, sizeof(sendsocket)) != sendlen) {
		perror("sendto");
		return;
	}
}

static u32 getPP(u32 src) {
	return (src << 16) | (38010U);
}

void parse(char *str, char *cmd, int *arg1, int *arg2, int *arg3) {
	char *pch = strtok (str, " ");
	int i = 0;
	while (pch != NULL)
	{
		switch (i) {
			case 0 : *cmd = pch[0]; break;
			case 1 : *arg1 = atoi(pch); break;
			case 2 : *arg2 = (int) strtol(pch, NULL, 16); break;
			case 3 : *arg3 = atoi(pch); break;
		}
		i++;
		pch = strtok (NULL, " ");
	}
}

int main(int argc, char *argv[]) {
	int received = 0;
	char in[128];
	char command = 'x';
	struct sockaddr_in sendsocket;
	FTC_t ftc;
	int sock;
	int arg1, arg2, arg3;
	int ret = 0;

	/* Create the UDP socket */
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("socket");
		return -1;
	}

	/* kernel address */
	memset(&sendsocket, 0, sizeof(sendsocket));
	sendsocket.sin_family = AF_INET;
	sendsocket.sin_addr.s_addr = inet_addr("127.0.0.1");
	sendsocket.sin_port = htons(SERVER_PORT);


	while (1) {
		printf(">");
		command = 'x';
		fgets(in, sizeof(in), stdin);
		parse(in, &command, &arg1, &arg2, &arg3);
		// printf("%c %d %x %d\n", command, arg1, arg2, arg3);
		switch (command) {
			case 'a':
				ftc.type = FTC_ADD;
				ftc.entry.id = arg1;
				ftc.entry.matcher.pp = arg2;
				ftc.entry.matcher.prot = TCPM_PROT;
				ftc.entry.action.state = ((arg3 > 0) ? TCPM_ON : TCPM_OFF);
				sendcmd(sock, sendsocket, ftc);
				break;
			case 'm':
				ftc.type = FTC_MOD;
				ftc.entry.id = arg1;
				ftc.entry.action.state = ((arg3 > 0) ? TCPM_ON : TCPM_OFF);
				sendcmd(sock, sendsocket, ftc);
				break;
			case 'd':
				ftc.type = FTC_DEL;
				ftc.entry.id = arg1;
				sendcmd(sock, sendsocket, ftc);
				break;
			case 's':
				system("sudo cat /sys/kernel/debug/tcp/monitor");
				break;
			case 'e':
				return 0;
			default :
				break;
		}
	}

	return 0;
}
