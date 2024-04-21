#include <stdio.h>
#include <unistd.h>
#include <signal.h>
// dpdk
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

#include "exp.h"
#include "arp.h"


#define BURST_SIZE (64)

static uint8_t in_the_loop = 0;
static uint8_t running = 1;
void handle_int(__attribute__((unused)) int sig) {
	running = 0;
	if (!in_the_loop) {
		rte_exit(EXIT_FAILURE, "Interrupted!");
	}
}

static void report_measurements(uint64_t *m, size_t count)
{
	printf("-------------------------------------\n");
	for (size_t i = 0; i < count; i++) {
		printf("%ld\n", m[i]);
	}
	printf("-------------------------------------\n");
}

int do_latency_client(void *_cntx)
{
	signal(SIGINT, handle_int);
	struct context *cntx = _cntx;
	int dpdk_port = cntx->dpdk_port_id;
	uint16_t qid = cntx->default_qid;
	uint32_t nb_rx, nb_tx;

	struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;

	if (cntx->count_dst_ip != 1) {
		rte_exit(EXIT_FAILURE, "Expect only one server ip");
	}
	uint32_t dst_ip = cntx->dst_ips[0];
	uint32_t src_ip = cntx->src_ip;
	uint16_t dst_port = rte_cpu_to_be_16(cntx->dst_port);
	uint16_t src_port = rte_cpu_to_be_16(cntx->src_port);

	const uint32_t payload_length = cntx->payload_length;

	struct rte_ether_addr my_eth = cntx->my_eth;
	struct rte_ether_addr server_eth;

	struct rte_mbuf *bufs[BURST_SIZE];
	struct rte_mbuf *buf;
	char *buf_ptr;
	const uint32_t burst = cntx->batch;

	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	uint64_t timestamp, resp_recv_time;

	const uint16_t ip_total_len = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
						sizeof(struct rte_udp_hdr) + payload_length);
	const uint16_t udp_total_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_length);

	if (rte_eth_dev_socket_id(dpdk_port) > 0 && rte_eth_dev_socket_id(dpdk_port) != (int)rte_socket_id()) {
		printf("Warning port is on remote NUMA node\n");
	}

	// Getting server mac addrses
	printf("sending ARP requests\n");
	server_eth = get_dst_mac(dpdk_port, qid, src_ip, my_eth, dst_ip,
			broadcast_mac, tx_mem_pool, 0, 1);
	char ip[20];
	ip_to_str(dst_ip, ip, 20);
	printf("mac address for server %s received: ", ip);
	printf("%x:%x:%x:%x:%x:%x\n",
			server_eth.addr_bytes[0],server_eth.addr_bytes[1],server_eth.addr_bytes[2],
			server_eth.addr_bytes[3],server_eth.addr_bytes[4],server_eth.addr_bytes[5]);
	printf("ARP requests finished\n");


	uint32_t dst_ip_net = rte_cpu_to_be_32(dst_ip);
	uint32_t src_ip_net = rte_cpu_to_be_32(src_ip);

	assert(cntx->count_queues == 1);

	const size_t max_measure_size = 1000000000LL;
	uint64_t *measurements = calloc(max_measure_size, sizeof(uint64_t)); 
	size_t m_index = 0;

	in_the_loop = 1;
	while(running) {
		// allocate some packets ! notice they should either be sent or freed
		if (rte_pktmbuf_alloc_bulk(tx_mem_pool, bufs, burst)) {
			// allocating failed
			continue;
		}

		// create a burst for selected flow
		for (uint32_t i = 0; i < burst; i++) {
			buf = bufs[i];
			// ether header
			buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
			eth_hdr = (struct rte_ether_hdr *)buf_ptr;
			rte_ether_addr_copy(&my_eth, &eth_hdr->src_addr);
			rte_ether_addr_copy(&server_eth, &eth_hdr->dst_addr);
			eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
			// ipv4 header
			buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
			ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
			ipv4_hdr->version_ihl = 0x45;
			ipv4_hdr->type_of_service = 0;
			ipv4_hdr->total_length = ip_total_len;
			ipv4_hdr->packet_id = 0;
			ipv4_hdr->fragment_offset = 0;
			ipv4_hdr->time_to_live = 64;
			ipv4_hdr->next_proto_id = IPPROTO_UDP;
			ipv4_hdr->hdr_checksum = 0;
			ipv4_hdr->src_addr = src_ip_net;
			ipv4_hdr->dst_addr = dst_ip_net;
			// upd header
			buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr) + payload_length);
			udp_hdr = (struct rte_udp_hdr *)buf_ptr;
			udp_hdr->src_port = src_port;
			udp_hdr->dst_port = dst_port;
			udp_hdr->dgram_len = udp_total_len;
			udp_hdr->dgram_cksum = 0;
			// payload (timestamp)
			timestamp = rte_get_timer_cycles();
			*(uint64_t *)(buf_ptr + (sizeof(struct rte_udp_hdr))) = timestamp;
			memset(buf_ptr + sizeof(struct rte_udp_hdr) + sizeof(timestamp), 0xAB,
					payload_length - sizeof(timestamp));
			// flags
			buf->l2_len = RTE_ETHER_HDR_LEN;
			buf->l3_len = sizeof(struct rte_ipv4_hdr);
			buf->ol_flags = (RTE_MBUF_F_TX_IP_CKSUM |
					RTE_MBUF_F_TX_IPV4 |
					RTE_MBUF_F_TX_UDP_CKSUM);

			ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr);
			udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ipv4_hdr, udp_hdr);
		}
		nb_tx = rte_eth_tx_burst(dpdk_port, qid, bufs, burst);
recv:
		// wait for response
		nb_rx = 0;
		while(running) {
			nb_rx = rte_eth_rx_burst(dpdk_port, qid, bufs, BURST_SIZE);
			if (nb_rx != 0)
				break;
		}
		// printf("recv: %d\n", nb_rx);
		resp_recv_time = rte_get_timer_cycles();
		/* assert(nb_tx >= nb_rx); */

		for (uint32_t j = 0; j < nb_rx; j++) {
			buf = bufs[j];

			uint8_t valid_pkt = check_eth_hdr(src_ip, &my_eth, buf, tx_mem_pool, 0);
			if (unlikely(!valid_pkt)) {
				printf("invalid packet\n");
				rte_pktmbuf_free(buf);
				continue;
			}
			nb_tx -= 1;

			eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
			if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
				rte_pktmbuf_free(buf);
				continue;
			}
			ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
			udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);
			// get timestamp
			timestamp = (*(uint64_t *)(udp_hdr + 1));
			uint64_t latency = (resp_recv_time - timestamp) * 1000 * 1000 / rte_get_timer_hz(); // (us)
			// free packet
			rte_pktmbuf_free(buf);

			measurements[m_index++] = latency;
			assert(m_index < max_measure_size);
		}

		if (nb_tx > 0 && running)
			goto recv;

		// wait some time
		/* wait(100000000LL); */
	}
	report_measurements(measurements, m_index);
	return 0;
}
