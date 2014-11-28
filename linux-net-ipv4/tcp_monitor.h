#ifndef _TCP_MONITOR_H
#define _TCP_MONITOR_H

/*
 * Author: Wenzhi Cui
 */


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

#define N_FTE (1 << 12)
#define DEFAULT_FTE (N_FTE)

struct FlowTable {
	u32	next;
	u32	cnt;
	FTE_t	ftes[N_FTE];
};
typedef struct FlowTable FlowTable_t;
typedef struct FlowTable *FlowTable_ptr;

#define FTC_ADD	(1)
#define FTC_DEL	(2)
#define FTC_MOD	(3)

struct FTC {
	u32	type;
	FTE_t	entry;
};
typedef struct FTC FTC_t;
typedef struct FTC *FTC_ptr;

int hit_flow(FM_t fmatcher, FA_ptr pact);

void enforce_flow_ai(const struct sock *sk, FA_t action, u32 ack, u32 acked);
void default_enforce_flow_ai(const struct sock *sk);
u32 enforce_flow_md(const struct sock *sk, FA_t action);
u32 default_enforce_flow_md(const struct sock *sk);

void tcpm_store(const struct sock *sk);

#endif
