/*
 * A monitor for TCP congestion control params
 */

#include <linux/module.h>
#include <net/tcp.h>
#include <linux/hashtable.h>
#include <linux/random.h>
#include <linux/cryptohash.h>
#include "tcp_monitor.h"

struct Dummy {
	u32	cwnd;
	// there could be more...
};

// static DEFINE_SPINLOCK(tcp_monitor_lock);
// static DEFINE_HASHTABLE(tcp_monitor_cache, 8);

/* store a cwnd to monitor */
void tcpm_store(const struct sock *sk, u32 cwnd) {
	u64 ap = sk->sk_addrpair;
	u32 pp = sk->sk_portpair;
	printk(KERN_ALERT "TCPM STORE: (%lld, %d, %d)\n", ap, pp, cwnd);
}
EXPORT_SYMBOL_GPL(tcpm_store);

static int __init tcpm_init(void)
{
	printk(KERN_ALERT "TCP Moniter Loaded.\n");
	return 0;
}

static void __exit tcpm_exit(void)
{
	// nothing to be done.
}


module_init(tcpm_init);
module_exit(tcpm_exit);

MODULE_AUTHOR("Wenzhi Cui");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Monitor");
