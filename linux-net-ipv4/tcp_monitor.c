/*
 * A monitor for TCP congestion control params
 */

#include <linux/module.h>
#include <net/tcp.h>
#include <linux/hashtable.h>
#include <linux/random.h>
#include <linux/cryptohash.h>
#include <linux/init.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/inet.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "tcp_monitor.h"

static int add_flow(u32 fid, FM_t fmatcher, FA_t faction);
static int mod_flow(u32 fid, FA_t faction);
static int del_flow(u32 fid);
static void show_flow(void);
static int dump_table(void *buf, int size);

static struct socket *udpsocket=NULL;
static struct socket *clientsocket=NULL;
static DECLARE_COMPLETION( threadcomplete );
struct workqueue_struct *wq;

struct wq_wrapper{
        struct work_struct worker;
	struct sock * sk;
};

struct wq_wrapper wq_data;

/*
 ***********************************
 *Output DebugFS Logic
 ***********************************
 */
int file_value;
struct dentry *tcpdir, *mfile;
char debugfsbuf[N_FTE * 32];

static ssize_t tcpm_read_file(struct file *file, char __user *userbuf,
                                size_t count, loff_t *ppos)
{
	int r = dump_table(debugfsbuf, sizeof(debugfsbuf));
	show_flow();
	return simple_read_from_buffer(userbuf, count, ppos, debugfsbuf, r);
} 

static ssize_t tcpm_write_file(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
	return -EACCES;
}

static const struct file_operations my_fops = {
        .read = tcpm_read_file,
        .write = tcpm_write_file,
};

/*
 ***********************************
 *Input UDP Logic
 ***********************************
 */

int nfilled = 0;
char ftcbuf[sizeof(FTC_t)];

static void cb_data(struct sock *sk, int bytes){
	wq_data.sk = sk;
	queue_work(wq, &wq_data.worker);
}

static void handle_msg(FTC_ptr pftc) {
	u32 fid;
	FM_t fmatcher;
	FA_t faction;
	printk("type: %d state: %d\n", pftc->type, pftc->entry.action.state); /*8 for udp header*/
	if (pftc->type == FTC_ADD) {
		fid = pftc->entry.id;
		fmatcher = pftc->entry.matcher;
		faction = pftc->entry.action;
		add_flow(fid, fmatcher, faction);
		// show_flow();
	}
	if (pftc->type == FTC_MOD) {
		fid = pftc->entry.id;
		faction = pftc->entry.action;
		mod_flow(fid, faction);
	}
	if (pftc->type == FTC_DEL) {
		fid = pftc->entry.id;
		del_flow(fid);
	}
}

void recv_command(struct work_struct *data){
	struct  wq_wrapper * foo = container_of(data, struct  wq_wrapper, worker);
	int len = 0, msglen = 0, tocopy = 0;
	char *msg;
	struct sk_buff *skb = NULL;
	/* as long as there are messages in the receive queue of this socket*/
	while((len = skb_queue_len(&foo->sk->sk_receive_queue)) > 0){
		/* receive packet */
		skb = skb_dequeue(&foo->sk->sk_receive_queue);
		msglen = skb->len - 8;
		msg = skb->data+8;
		while (nfilled + msglen >= sizeof(FTC_t)) {
			tocopy = sizeof(FTC_t) - nfilled;
			memcpy(&ftcbuf[nfilled], msg, tocopy);
			handle_msg((FTC_ptr) ftcbuf);
			msglen -= tocopy;
			msg += tocopy;
			nfilled = 0;			
		}
		if (msglen > 0) {
			tocopy = msglen;
			memcpy(&ftcbuf[nfilled], msg, tocopy);
			nfilled = tocopy;
		}
		
		/* free the initial skb */
		kfree_skb(skb);
	}
}

/*
 ***********************************
 *Flow Table Logic
 ***********************************
 */


#define FTABLE_FULL(next) ( (next) >= N_FTE )

// Define a Flow Table 
static FlowTable_t ftable;

/* compare if two matcher are matched */
static int match_FM(FM_t this, FM_t other) {
// wildcard rule
	return this.pp == other.pp;
}

static void show_flow() {
	int i = 0;
	for (i = 0; i < N_FTE; i++) {
		FTE_t entry = ftable.ftes[i];
		if (entry.valid)
			printk("[TCPM] Entry %d 0x%x %d\n", entry.id, entry.matcher.pp, entry.action.state);
	}
}

static int dump_table(void *buf, int size) {
	int i = 0, r = 0, sum = 0;
	r = snprintf(buf, size, "Form\tFlowId\tPortPair\tState\n");
	buf += r;
	size -= r;
	sum += r;
 	for (i = 0; i < N_FTE; i++) {
		FTE_t entry = ftable.ftes[i];
		if (entry.valid) {
			r = snprintf(buf, size, "%d\t%d\t0x%x\t%d\n", i, entry.id, entry.matcher.pp, entry.action.state);
			buf += r;
			size -= r;
			sum += r;
		}
	}
	return sum;
}

/* insert a new FTE */
static int add_flow(u32 fid, FM_t fmatcher, FA_t faction) {
	u32 old_next = ftable.next;
	// locking

	if (FTABLE_FULL(ftable.next)) {
		// Flow Table is Full
		// unlocking & exit
		return -1;
	}

	ftable.ftes[ftable.next].matcher = fmatcher;
	ftable.ftes[ftable.next].action = faction;
	ftable.ftes[ftable.next].valid = 1;
	ftable.ftes[ftable.next].id = fid;
	ftable.cnt++;

	// update next to the next available FTE
	if (ftable.cnt == N_FTE) {
		ftable.next = N_FTE;
	} else {
		while (1) {
			ftable.next++;
			if (ftable.next == N_FTE)
				ftable.next = 0; // wrap when hit the top of table
			if (ftable.next == old_next) {
				// cannot find any useful PTE at the else br
				// Let's Panic
				printk("[TCPM] Panic: cannot find any useful PTE at the else branch of insert_flow()\n");
				ftable.next = N_FTE;
				break;
			}
			if (ftable.ftes[ftable.next].valid == 0)
				break;
		};
	}

	/* defer binding */

	// unlocking
	return 0;
}

/* search a flow by Flow Matcher */
// there is no locking op in match(), the caller must lock itself
static int match_flow(FM_t fmatcher) {
	int i = 0;
	for (i = 0; i < N_FTE; i++) {
		if (ftable.ftes[i].valid && match_FM(ftable.ftes[i].matcher, fmatcher))
			return i;
	}
	return N_FTE;
}

/* search a flow */
// there is no locking op in lookup(), the caller must lock itself
static int lookup_flow(u32 fid) {
	int i = 0;
	for (i = 0; i < N_FTE; i++) {
		if (ftable.ftes[i].valid && ftable.ftes[i].id == fid)
			return i;
	}
	return N_FTE;
}

/* remove a FTE */
static int del_flow(u32 fid) {
	// locking
	int pos = lookup_flow(fid);
	ftable.ftes[pos].valid = 0;
	ftable.cnt--;
	// unlocking
	return 0;
}

/* update a FTE */
static int mod_flow(u32 fid, FA_t faction) {
	// locking
	int pos = lookup_flow(fid);
	ftable.ftes[pos].action = faction;
	// unlocking
	return 0;
}

/* test if this flow exists in the flow table */
int hit_flow(FM_t fmatcher, FA_ptr pact) {
	// locking
	int pos = match_flow(fmatcher);
	if (pos != N_FTE) {
		(*pact) = ftable.ftes[pos].action;
	}
	// unlocking
	return pos != N_FTE;
}
EXPORT_SYMBOL_GPL(hit_flow);

/* translate from flow table action to cwnd action */
void enforce_flow_ai(const struct sock *sk, FA_t action, u32 ack, u32 acked) {
	struct tcp_sock *tp = tcp_sk(sk);
	if (action.state == TCPM_OFF) {
		tp->snd_cwnd = 0;
		return;
	}

	if (!tcp_is_cwnd_limited(sk))
		return;

	/* In "safe" area, increase. */
	if (tp->snd_cwnd <= tp->snd_ssthresh)
		tcp_slow_start(tp, acked);
	/* In dangerous area, increase slowly. */
	else
		tcp_cong_avoid_ai(tp, tp->snd_cwnd);
}
EXPORT_SYMBOL_GPL(enforce_flow_ai);

u32 enforce_flow_md(const struct sock *sk, FA_t action) {
	const struct tcp_sock *tp = tcp_sk(sk);
	return tp->snd_cwnd >> 1U;
}
EXPORT_SYMBOL_GPL(enforce_flow_md);

void default_enforce_flow_ai(const struct sock *sk) {
	struct tcp_sock *tp = tcp_sk(sk);
	tp->snd_cwnd = 0;
}
EXPORT_SYMBOL_GPL(default_enforce_flow_ai);

u32 default_enforce_flow_md(const struct sock *sk) {
	const struct tcp_sock *tp = tcp_sk(sk);
	return tp->snd_cwnd >> 1U;
}
EXPORT_SYMBOL_GPL(default_enforce_flow_md);

/* store a cwnd to monitor */
void tcpm_store(const struct sock *sk) {
	u64 ap = sk->sk_addrpair;
	u32 pp = sk->sk_portpair;
	struct tcp_sock *tp = tcp_sk(sk);

	printk(KERN_ALERT "TCPM STORE: (%lld, %d, %d) (%d %d) - (ip, port, cwnd) (flight, srtt_us)\n", ap, pp, tp->snd_cwnd, tcp_packets_in_flight(tp), tp->srtt_us);
}
EXPORT_SYMBOL_GPL(tcpm_store);

static int __init tcpm_init(void)
{
	struct sockaddr_in server;
	int servererror;
	/* socket to receive data */
	if (sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &udpsocket) < 0) {
		printk( KERN_ERR "server: Error creating udpsocket.n" );
		return -EIO;
	}
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( (unsigned short)SERVER_PORT);
	servererror = udpsocket->ops->bind(udpsocket, (struct sockaddr *) &server, sizeof(server ));
	if (servererror) {
		sock_release(udpsocket);
		return -EIO;
	}
	udpsocket->sk->sk_data_ready = cb_data;
	
	/* create work queue */	
	INIT_WORK(&wq_data.worker, recv_command);
	wq = create_singlethread_workqueue("myworkqueue"); 
	if (!wq){
		return -ENOMEM;
	}

	tcpdir = debugfs_create_dir("tcp", NULL);
	mfile = debugfs_create_file("monitor", 0644, tcpdir, &file_value, &my_fops);

	printk(KERN_ALERT "TCP Moniter Loaded.\n");
	return 0;
}

static void __exit tcpm_exit(void)
{
	// nothing to be done.
	if (udpsocket)
		sock_release(udpsocket);
	if (clientsocket)
		sock_release(clientsocket);

	if (wq) {
                flush_workqueue(wq);
                destroy_workqueue(wq);
	}

	debugfs_remove(mfile);
	debugfs_remove(tcpdir);
	printk(KERN_ALERT "TCP Moniter Exit.\n");
}


module_init(tcpm_init);
module_exit(tcpm_exit);

MODULE_AUTHOR("Wenzhi Cui");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Monitor");
