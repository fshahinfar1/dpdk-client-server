/* vim: set et ts=2 sw=2: */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
// dpdk
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
// current project
#include "config.h"
#include "exp.h"
#include "percentile.h"
#include "arp.h"
#include "zipf.h"
#include "exponential.h"

#define BURST_SIZE (512)
#define MAX_EXPECTED_LATENCY (10000) // (us)
#define get_template "get %s\r\n\0"
#define get_template_sz 7

// Metadata header for Memcached UDP requests
typedef struct __attribute__ ((__packed__)) {
  uint16_t req_id;
  uint16_t seq_no; // must be zero
  uint16_t datagrams; // must be one
  uint16_t reserved; // leave zero
} memcd_udp_header_t;

static inline void prepare_packet(struct rte_mbuf *, struct rte_ether_addr *,
    struct rte_ether_addr *, uint32_t, uint32_t, uint16_t, uint16_t);
static void *_run_receiver_thread(void *_arg);

typedef struct {
  int running;
  struct context *cntx;
  uint64_t start_time;
  uint64_t ignore_result_duration;
  struct p_hist **hist;
  uint64_t *total_received_pkts;
} recv_thread_arg_t;

int do_memcd(void *_cntx) {
  srand(37);
  struct context *cntx = (struct context *)_cntx;
  int dpdk_port = cntx->dpdk_port_id;
  uint16_t qid = cntx->default_qid;
  const uint16_t count_queues = cntx->count_queues;
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;
  struct rte_ether_addr my_eth = cntx->my_eth;
  uint32_t src_ip = cntx->src_ip;
  int src_port = cntx->src_port;
  uint32_t *dst_ips = cntx->dst_ips;
  int count_dst_ip = cntx->count_dst_ip;
  assert (count_dst_ip == 1);
  FILE *fp = cntx->fp; // or stdout
  uint32_t base_port_number = cntx->base_port_number;
  uint8_t bidi = cntx->bidi;
  assert(count_dst_ip >= 1);

  uint64_t start_time, end_time;
  uint64_t duration;
  uint64_t ignore_result_duration = 0;

  struct rte_mbuf *bufs[BURST_SIZE];
  uint16_t nb_tx = 0;
  uint16_t i;
  uint32_t dst_ip;
  int can_send = 1;
  uint16_t selected_q = 0;

  struct rte_ether_addr _server_eth[count_dst_ip];
  struct rte_ether_addr server_eth;

  int selected_dst = 0;

  struct p_hist **hist;
  int rate_limit = cntx->rate_limit;

  uint64_t throughput = 0;
  uint64_t tp_limit = cntx->rate;
  uint64_t tp_start_ts = 0;
  double delta_time;
  double limit_window;

  // throughput
  uint64_t total_sent_pkts = 0;
  uint64_t total_received_pkts = 0;
  uint64_t failed_to_push = 0;

  // hardcoded burst size TODO: get from args
  uint16_t burst = cntx->batch;

  pthread_t recv_thread;

  if (cntx->duration < 0) {
    duration = 0;
  } else {
    duration = rte_get_timer_hz() * cntx->duration;
  }

  // create a latency hist for each ip
  hist = malloc(count_dst_ip * sizeof(struct p_hist *));
  for (i = 0; i < count_dst_ip; i++) {
    hist[i] = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY);
  }
  if (burst > BURST_SIZE) {
    fprintf(stderr, "Maximum burst size is limited to %d (update if needed)\n", BURST_SIZE);
    rte_exit(EXIT_FAILURE, "something failed!\n");
  }
  fprintf(fp, "sending on queues: [%d, %d]\n", qid, qid + count_queues - 1);
  if (rte_eth_dev_socket_id(dpdk_port) > 0 &&
      rte_eth_dev_socket_id(dpdk_port) != (int)rte_socket_id()) {
    printf("Warning port is on remote NUMA node\n");
  }
  fprintf(fp, "Client src port %d\n", src_port);

  // get dst mac address
  if (cntx->do_arp) {
    printf("sending ARP requests\n");
    for (int i = 0; i < count_dst_ip; i++) {
      struct rte_ether_addr dst_mac;
      dst_mac = get_dst_mac(dpdk_port, qid, src_ip, my_eth, dst_ips[i],
          broadcast_mac, tx_mem_pool, 0, count_queues);
      _server_eth[i] = dst_mac;
      char ip[20];
      ip_to_str(dst_ips[i], ip, 20);
      printf("mac address for server %s received: ", ip);
      printf("%x:%x:%x:%x:%x:%x\n",
          dst_mac.addr_bytes[0],dst_mac.addr_bytes[1],dst_mac.addr_bytes[2],
          dst_mac.addr_bytes[3],dst_mac.addr_bytes[4],dst_mac.addr_bytes[5]);
    }
    printf("ARP requests finished\n");
  } else {
    // set fake destination mac address
    for (int i = 0; i < count_dst_ip; i++)
      /* 1c:34:da:41:c6:fc */
      _server_eth[i] = (struct rte_ether_addr)
                              {{0x1c, 0x34, 0xda, 0x41, 0xc6, 0xfc}};
  }
  server_eth = _server_eth[0];


  uint16_t dst_port = base_port_number;
  start_time = rte_get_timer_cycles();
  tp_start_ts = start_time;

  // start receiver thread
  recv_thread_arg_t recv_arg = {
    .running = 1,
    .cntx = cntx,
    .start_time = start_time,
    .ignore_result_duration = ignore_result_duration,
    .hist = hist,
    .total_received_pkts = &total_received_pkts
  };
  if (bidi) {
    pthread_create(&recv_thread, NULL, _run_receiver_thread, &recv_arg);
  }

  // main tx worker loop
  while (cntx->running) {
    end_time = rte_get_timer_cycles();

    // TODO: this is just for testing the switch system
    // if (total_sent_pkts[0] > 1024) break;

    if (duration > 0 && end_time > start_time + duration) {
      if (can_send) {
        can_send = 0;
        start_time = rte_get_timer_cycles();
        // wait some time for the sent packets to return
        duration = 2 * rte_get_timer_hz();
      } else {
        break;
      }
    }
    if (!can_send) {
      continue;
    }

    // select destination ip, port and ... =================================
    dst_ip = dst_ips[selected_dst];
    server_eth = _server_eth[selected_dst];
    dst_port = base_port_number;
    // ===================================================================

    // report throughput
    uint64_t ts = end_time;
    delta_time = ts - tp_start_ts;
    if (delta_time > rte_get_timer_hz()) {
      printf("tp: %lu\n", throughput);
      throughput = 0;
      tp_start_ts = ts;
    }

    // rate limit
    limit_window = tp_limit * (delta_time / (double)(rte_get_timer_hz()));
    if (rate_limit && throughput > limit_window) {
      continue;
    }

    if (rte_pktmbuf_alloc_bulk(tx_mem_pool, bufs, burst)) {
      /* allocating failed */
      continue;
    }

    for (int i = 0; i < burst; i++) {
      prepare_packet(bufs[i], &my_eth, &server_eth, src_ip, dst_ip,
          src_port, dst_port);
    }

    nb_tx = rte_eth_tx_burst(dpdk_port, selected_q, bufs, burst);

    if (likely(end_time > start_time + ignore_result_duration * rte_get_timer_hz())) {
      total_sent_pkts += nb_tx;
      failed_to_push += burst - nb_tx;
    }

    if (nb_tx < burst) {
      /* fprintf(stderr, "not all buffers were sent (%d)\n", burst - nb_tx); */
      // free packets failed to send
      for (i = nb_tx; i < burst; i++)
        rte_pktmbuf_free(bufs[i]);
    }

    throughput += nb_tx;

    // Inter arrival Time
    /* if (delay_cycles > 0) { */
    /*   apply_delay_cycles(delay_cycles, 1); */
    /* } */

  } // end of tx worker

  if (bidi) {
    // wait for  receive thread to stop
    recv_arg.running = 0;
    pthread_join(recv_thread, NULL);
  }

  /* write to the output buffer, (it may or may not be stdout) */
  fprintf(fp, "=========================\n");
  if (bidi) {
    /* latencies are measured by client only in bidi mode */
    for (int k = 0; k < count_dst_ip; k++) {
      uint32_t ip = rte_be_to_cpu_32(dst_ips[k]);
      uint8_t *bytes = (uint8_t *)(&ip);
      fprintf(fp, "Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
      float percentile = get_percentile(hist[k], 0.01);
      fprintf(fp, "%d latency (1.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.50);
      fprintf(fp, "%d latency (50.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.90);
      fprintf(fp, "%d latency (90.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.95);
      fprintf(fp, "%d latency (95.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.99);
      fprintf(fp, "%d latency (99.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.999);
      fprintf(fp, "%d latency (99.9): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.9999);
      fprintf(fp, "%d latency (99.99): %f\n", k, percentile);
    }
  }

  uint32_t ip = rte_be_to_cpu_32(dst_ips[0]);
  uint8_t *bytes = (uint8_t *)(&ip);
  fprintf(fp, "Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
  fprintf(fp, "Tx: %ld\n", total_sent_pkts);
  fprintf(fp, "Rx: %ld\n", total_received_pkts);
  fprintf(fp, "failed to push: %ld\n", failed_to_push);
  fprintf(fp, "warmup time: %ld\n", ignore_result_duration);
  fprintf(fp, "Client done\n");
  fflush(fp);

  /* free allocated memory*/
  for (int k = 0; k < count_dst_ip; k++) {
    free_p_hist(hist[k]);
  }
  free(hist);
  cntx->running = 0;
  return 0;
}

void *_run_receiver_thread(void *_arg)
{
  printf("Receiver thread started\n");
  recv_thread_arg_t *arg = (recv_thread_arg_t *) _arg;
  struct context *cntx = arg->cntx;
  uint64_t start_time = arg->start_time;
  uint64_t ignore_result_duration = arg->ignore_result_duration;
  struct p_hist **hist = arg->hist;
  uint64_t *total_received_pkts = arg->total_received_pkts;

  // context values
  int dpdk_port = cntx->dpdk_port_id;
  int qid = cntx->default_qid;
  int count_queues = cntx->count_queues;
  uint32_t src_ip = cntx->src_ip;
  struct rte_ether_addr my_eth = cntx->my_eth;
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;
  int count_dst_ip = cntx->count_dst_ip;
  uint32_t *dst_ips = cntx->dst_ips;

  struct rte_mbuf *recv_bufs[BURST_SIZE];
  struct rte_mbuf *buf;
  int nb_rx;
  int rx_q;
  int valid_pkt;
  char *ptr;
  uint32_t recv_ip;
  int found;
  uint64_t end_time;

  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;

  while (arg->running) {
    end_time = rte_get_timer_cycles();
    nb_rx = 0;
    for (rx_q = qid; rx_q < qid + count_queues; rx_q++) {
      nb_rx = rte_eth_rx_burst(dpdk_port, rx_q, recv_bufs, BURST_SIZE);
      if (nb_rx != 0)
        break;
    }
    if (nb_rx == 0) continue;

    for (int j = 0; j < nb_rx; j++) {
      buf = recv_bufs[j];
      // rte_pktmbuf_free(buf); // free packet
      // continue;

      valid_pkt = check_eth_hdr(src_ip, &my_eth, buf, tx_mem_pool, 0);
      if (unlikely(!valid_pkt)) {
        printf("invalid packet\n");
        rte_pktmbuf_free(buf);
        continue;
      }

      ptr = rte_pktmbuf_mtod(buf, char *);

      eth_hdr = (struct rte_ether_hdr *)ptr;
      if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
        rte_pktmbuf_free(buf);
        continue;
      }

      /* skip some seconds of the experiment, and do not record results */
      if (unlikely(end_time <
                   start_time + ignore_result_duration * rte_get_timer_hz())) {
        rte_pktmbuf_free(buf); // free packet
        continue;
      }

      ptr = ptr + RTE_ETHER_HDR_LEN;
      ipv4_hdr = (struct rte_ipv4_hdr *)ptr;
      recv_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);

      /* find ip index */
      found = 0;
      for (int k = 0; k < count_dst_ip; k++) {
        if (recv_ip == dst_ips[k]) {
          found = 1; break;
        }
      }
      if (unlikely(found == 0)) {
        uint32_t ip = rte_be_to_cpu_32(recv_ip);
        uint8_t *bytes = (uint8_t *)(&ip);
        printf("Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
        printf("k not found: qid=%d\n", qid);
        rte_pktmbuf_free(buf); // free packet
        continue;
      }
      total_received_pkts[0]++;

      /* TODO: make sure the response is not an error */
      /* TODO: measure the latency (use the request id) */
      rte_pktmbuf_free(buf); // free packet
    }
  }
}

static inline uint16_t __find_uint_digits(uint32_t i) {
  if (i == 0)
    return 1;
  uint16_t counter = 0;
  while (i > 0) {
    counter++;
    i /= 10;
  }
  return counter;
}

static inline void prepare_packet(struct rte_mbuf *buf,
    struct rte_ether_addr *my_eth, struct rte_ether_addr *server_eth,
    uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port)
{
  uint32_t key_index = rand() % config.memcd.records; // randomly select a key
  static char key_str[32];
  uint8_t num_digits = __find_uint_digits(key_index);
  const uint32_t keylen = config.memcd.keylen;
  const uint32_t lead_zero = keylen - num_digits;
  snprintf(key_str, 32, "%d", key_index);
  key_str[num_digits] = 0; // I am not sure if utoa is null-terminating or not

  static uint16_t req_id = 0;

  // ether header
  const size_t payload_length = (sizeof(memcd_udp_header_t) + keylen + get_template_sz);
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;
  eth_hdr = (struct rte_ether_hdr *)rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);

  rte_ether_addr_copy(my_eth, &eth_hdr->src_addr);
  rte_ether_addr_copy(server_eth, &eth_hdr->dst_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

  // ipv4 header
  ipv4_hdr = (struct rte_ipv4_hdr *)rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
  ipv4_hdr->version_ihl = 0x45;
  ipv4_hdr->type_of_service = 0;
  ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
        sizeof(struct rte_udp_hdr) + payload_length);
  ipv4_hdr->packet_id = 0;
  ipv4_hdr->fragment_offset = 0;
  ipv4_hdr->time_to_live = 64;
  ipv4_hdr->next_proto_id = IPPROTO_UDP;
  ipv4_hdr->hdr_checksum = 0;
  ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip);
  ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip);

  // upd header
  udp_hdr = (struct rte_udp_hdr *)rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr));
  udp_hdr->src_port = rte_cpu_to_be_16(src_port);
  udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
  udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_length);
  udp_hdr->dgram_cksum = 0;

  /* udp request metadata */
  memcd_udp_header_t *meta = (memcd_udp_header_t *)rte_pktmbuf_append(buf, sizeof(memcd_udp_header_t));
  meta->req_id = rte_cpu_to_be_16(req_id++);
  meta->seq_no = 0;
  meta->datagrams = rte_cpu_to_be_16(1);
  meta->reserved = 0;

  /* payload */
  buf_ptr = (char *)rte_pktmbuf_append(buf, keylen + get_template_sz);
  buf_ptr[0] = 'g';
  buf_ptr[1] = 'e';
  buf_ptr[2] = 't';
  buf_ptr[3] = ' ';
  for (uint16_t z = 0; z < lead_zero; z++) {
      buf_ptr[4+z] = '0';
  }
  memcpy(&buf_ptr[4 + lead_zero], key_str, num_digits);
  buf_ptr[4 + keylen + 0] = '\r';
  buf_ptr[4 + keylen + 1] = '\n';
  buf_ptr[4 + keylen + 2] = '\0';

  buf->l2_len = RTE_ETHER_HDR_LEN;
  buf->l3_len = sizeof(struct rte_ipv4_hdr);
  buf->ol_flags = RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_UDP_CKSUM;
}
