#include <linux/module.h>
#include <net/tcp.h>
#include <linux/time.h>
#include "tcp_monitor.h"


struct dummy {
	u32	fid;
	u32	ref;
};

static void dmtcp_init(struct sock *sk)
{
	struct dummy *ca = inet_csk_ca(sk);
	ca->fid = 0;
	ca->ref = DEFAULT_FTE;
	printk("[DMTCP]: create pp %x\n", sk->sk_portpair);
}

static void dmtcp_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	u64 ap = sk->sk_addrpair;
	u32 pp = sk->sk_portpair;

	FM_t fmatcher = {ap, pp, TCPM_PROT};
	FA_t faction;
	if (hit_flow(fmatcher, &faction)) {
		enforce_flow_ai(sk, faction, ack, acked);
	} else
		default_enforce_flow_ai(sk);
}

static u32 dmtcp_ssthresh(struct sock *sk)
{
	return max(default_enforce_flow_md(sk), 2U);
}

static void dmtcp_pkts_acked(struct sock *sk, u32 pkts_acked, s32 rtt)
{
	// const struct tcp_sock *tp = tcp_sk(sk);
	// printk("[DMTCP]: Ack in RTT %d PKTS_ACKED %d (%u, %u) at %ld\n", rtt, pkts_acked, tp->snd_una, tp->snd_nxt, current_timestamp());
}

static u32 dmtcp_undo_cwnd(struct sock *sk)
{
	return 0;
}

static struct tcp_congestion_ops tcp_dummy __read_mostly = {
	.init		= dmtcp_init,
	.ssthresh	= dmtcp_ssthresh,
	.cong_avoid	= dmtcp_cong_avoid,
	.pkts_acked	= dmtcp_pkts_acked,
	.undo_cwnd	= dmtcp_undo_cwnd,

	.owner		= THIS_MODULE,
	.name		= "dummy"
};

static int __init dmtcp_register(void)
{
	// BUILD_BUG_ON(sizeof(struct dmtcp) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&tcp_dummy);
}

static void __exit dmtcp_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_dummy);
}

module_init(dmtcp_register);
module_exit(dmtcp_unregister);

MODULE_AUTHOR("Wenzhi Cui");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dummy TCP");
