// include directory
#include "exp.h"
#include "percentile.h"
#include "exponential.h"

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>

#define BURST_SIZE (1024)
#define MAX_DURATION (60)             // (sec)
#define MAX_EXPECTED_LATENCY (100000) // (us)

void print_stats(FILE *fp, uint64_t tp, struct p_hist *hist);

void print_stats(FILE *fp, __attribute__((unused)) uint64_t tp, struct p_hist *hist) {
  float percentile;
  // printf("\033[2J");
  // printf("TP: %lu\n", tp);
  // return;
  // Print latency statistics
  percentile = get_percentile(hist, 0.010);
  fprintf(fp, "Latency [@01](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.500);
  fprintf(fp, "Latency [@50](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.900);
  fprintf(fp, "Latency [@90](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.950);
  fprintf(fp, "Latency [@95](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.990);
  fprintf(fp, "Latency [@99](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.999);
  fprintf(fp, "Latency [@99.9](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.9999);
  fprintf(fp, "Latency [@99.99](us): %f\n", percentile);
  fprintf(fp, "====================================\n");
}

int do_server(void *_cntx) {
  struct context *cntx = (struct context *)_cntx;
  uint32_t dpdk_port = cntx->dpdk_port_id;
  uint32_t delay_cycles = cntx->delay_cycles;
  double cycles_error = 0; // EWMA
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool; // just for sending arp
  uint16_t qid = cntx->default_qid;
  uint32_t count_queues = cntx->count_queues;
  struct rte_ether_addr my_eth = cntx->my_eth;
  uint32_t my_ip = cntx->src_ip;
  uint8_t use_vlan = cntx->use_vlan;
  uint8_t bidi = cntx->bidi;
  FILE *fp = cntx->fp;
  uint32_t q_index = 0;

  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;

  uint16_t nb_rx;
  uint16_t nb_tx;
  struct rte_mbuf *rx_bufs[BURST_SIZE];
  int first_pkt = 0; // flag indicating if has received the first packet

  const uint64_t hz = rte_get_timer_hz();
  uint64_t throughput = 0;
  uint64_t empty_burst = 0;
  uint64_t start_time;
  uint64_t exp_begin;
  uint64_t current_time;
  uint64_t last_pkt_time = 0;
  const uint64_t wait_until_exp_begins = 60 * hz; /* cycles */
  const uint64_t termination_threshold = 2 * hz;
  int run = 1;

  uint64_t failed_to_push = 0;

  char *ptr;

  // histogram for calculating latency percentiles
  /* struct p_hist *hist; */
  /* uint64_t ts_offset; */
  /* uint64_t timestamp; */
  /* uint64_t latency; */

  uint64_t i;
  struct rte_mbuf *tx_buf[64];
  struct rte_mbuf *buf;
  struct rte_ether_addr tmp_addr;
  uint32_t tmp_ip;
  uint64_t k;

  // TODO: take these parameters from command line
  uint64_t token_limit = 3000000;
  uint8_t rate_limit = 0;
  uint64_t delta_time;
  uint64_t limit_window;

  /* TODO: remove cdq */
  uint8_t cdq = 0;
  int valid_pkt;

  uint32_t nb_pkts_process;
  // can not read queue until this time stamp
  uint64_t queue_status[count_queues];

  int sum_nb_rx = 0;
  int count_rx = 0;

  for (i = 0; i < count_queues; i++)
    queue_status[i] = 0;

  /* hist = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY); */

  /* check if running on the correct numa node */
  if (rte_eth_dev_socket_id(dpdk_port) > 0 &&
      rte_eth_dev_socket_id(dpdk_port) != (int)rte_socket_id()) {
    printf("Warining port is on remote NUMA node\n");
  }

  /* print the id of managed queues */
  fprintf(fp, "managed queues: ");
  for (uint16_t i = 0; i < count_queues; i++){
    fprintf(fp, "%d ", cntx->managed_queues[i]);
  }
  fprintf(fp, "\n");

  /* timestamp offset from the data section of the packet */
  /* if (use_vlan) { */
  /*   ts_offset = RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr) */
  /*                   + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr); */
  /* } else { */
  /*   ts_offset = RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr) */
  /*                   + sizeof(struct rte_udp_hdr); */
  /* } */

  fprintf(fp, "Running server\n");

  exp_begin = rte_get_timer_cycles();
  start_time = exp_begin;
  /* main worker loop */
  while (run && cntx->running) {
    /* manage the next queue */
    qid = cntx->managed_queues[q_index];
    q_index = (q_index + 1) % count_queues;

    /* get this iteration's timestamp */
    current_time = rte_get_timer_cycles();

    /* if experiment time has passed kill the server
     * (not all experiments need this)
     * */
    if (!first_pkt && current_time - exp_begin > wait_until_exp_begins) {
      run = 0;
      break;
    }

    /* update throughput */
    delta_time = current_time - start_time;
    if (delta_time > rte_get_timer_hz()) {
      // print_stats(throughput, hist);
      double avg_nb_rx = 0;
      if (count_rx != 0)
        avg_nb_rx = sum_nb_rx / (double)count_rx;
      printf("TP: %lu\n", throughput);
      printf("Average burst size: %.2f\n", avg_nb_rx);
      // printf("No empty burst: %ld\n", empty_burst);
      throughput = 0;
      sum_nb_rx = 0;
      count_rx = 0;
      start_time = current_time;
      empty_burst = 0;
      // printf("failed to push: %ld\n", failed_to_push);
    }

    limit_window = token_limit * (delta_time / (double)(rte_get_timer_hz()));
    if (rate_limit && throughput >= limit_window) {
      continue;
    }

    // check if this queue is ready to be serviced
    if (queue_status[qid] > current_time)
      continue;

    // receive some packets
    nb_rx = rte_eth_rx_burst(dpdk_port, qid, rx_bufs, BURST_SIZE);

    if (nb_rx == 0) {
      empty_burst++;
      if (first_pkt) {
        /* if client has started sending data and some amount of time has
         * passed since last pkt then server is done */
        if (current_time - last_pkt_time > termination_threshold) {
          // TODO: commented just for experiment
          run = 0;
          break;
        }
      }
      continue;
    }

    sum_nb_rx += nb_rx;
    count_rx++;

    nb_pkts_process = 0;
    /* echo packets */
    k = 0;
    for (i = 0; i < nb_rx; i++) {
      buf = rx_bufs[i];

      /* in unidirectional mode the arp is not send so destination mac addr
       * is incorrect
       * */
      if (cntx->do_arp) {

        valid_pkt = check_eth_hdr(my_ip, &my_eth, buf, tx_mem_pool, cdq);

        if (!valid_pkt) {
          // free packet
          printf("invalid packet\n");
          rte_pktmbuf_free(rx_bufs[i]);
          continue;
        }
      }

      ptr = rte_pktmbuf_mtod_offset(buf, char *, 0);
      eth_hdr = (struct rte_ether_hdr *)ptr;
      uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
      if (use_vlan) {
        if (ether_type != RTE_ETHER_TYPE_VLAN) {
          /* discard the packet (not our packet) */
          rte_pktmbuf_free(rx_bufs[i]);
          continue;
        }
      } else {
        if (ether_type != RTE_ETHER_TYPE_IPV4) {
          /* discard the packet (not our packet) */
          rte_pktmbuf_free(rx_bufs[i]);
          continue;
        }
      }

      if (use_vlan) {
        ipv4_hdr = (struct rte_ipv4_hdr *)(ptr + RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr));
      } else {
        ipv4_hdr = (struct rte_ipv4_hdr *)(ptr + RTE_ETHER_HDR_LEN);
      }

      /* if ip address does not match discard the packet */
      if (rte_be_to_cpu_32(ipv4_hdr->dst_addr) != my_ip) {
        printf("ip address does not match\n");
        rte_pktmbuf_free(rx_bufs[i]);
        continue;
      }

      /* received a valid packet, update the time a valid packet was seen */
      last_pkt_time = current_time;
      if (!first_pkt) {
        first_pkt = 1;
        start_time = current_time;
      }

      /* update throughput */
      throughput += 1;

      udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);

      // Service time 5ns per packet
      /* wait(get_exponential_sample(0.2)); */

      /* get time stamp */
      /* ptr = ptr + ts_offset; */
      /* timestamp = (*(uint64_t *)ptr); */
      /* TODO: the following line only works on single node */
      /* latency = (rte_get_timer_cycles() - timestamp) * 1000 * 1000 / rte_get_timer_hz(); //(us) */
      /* add_number_to_p_hist(hist, (float)latency); */

      // if (src_port >= 8000) {
        // this packet needs heavy processing
        nb_pkts_process++;
      // }

      if (!bidi) {
        rte_pktmbuf_free(rx_bufs[i]);
        continue;
      }

      /*  swap mac address */
      rte_ether_addr_copy(&eth_hdr->src_addr, &tmp_addr);
      rte_ether_addr_copy(&eth_hdr->dst_addr, &eth_hdr->src_addr);
      rte_ether_addr_copy(&tmp_addr, &eth_hdr->dst_addr);

      /* swap ip address */
      tmp_ip = ipv4_hdr->src_addr;
      ipv4_hdr->src_addr = ipv4_hdr->dst_addr;
      ipv4_hdr->dst_addr = tmp_ip;

      /* swap port */
      uint16_t tmp = udp_hdr->src_port;
      udp_hdr->src_port = udp_hdr->dst_port;
      udp_hdr->dst_port = tmp;

      /* add to list of echo packets */
      tx_buf[k] = buf;
      k++;
    }

    /* apply processing cost */
    /* if (delay_cycles > 0) { */
    /*   for (i = 0; i < nb_pkts_process; i++) { */
    /*     // rte_delay_us_block(delay_us); */
    /*     uint64_t now = rte_get_tsc_cycles(); */
    /*     uint64_t end = */
    /*       rte_get_tsc_cycles() + delay_cycles; */
    /*     while (now < end) { */
    /*       now = rte_get_tsc_cycles(); */
    /*     } */
    /*     cycles_error =(now - end) * 0.5 + 0.5 * (cycles_error); */
    /*   } */
    /* } */

    if (!bidi)
      continue;

    // this queue can not be serviced until the deadline is reached
    queue_status[qid] = rte_get_timer_cycles() + (nb_pkts_process * delay_cycles);

    while (k > 0) {
      nb_tx = rte_eth_tx_burst(dpdk_port, qid, tx_buf, k);

      /* move packets failed to send to the front of the queue */
      for (i = nb_tx; i < k; i++) {
        tx_buf[i - nb_tx] = tx_buf[i];
      }
      k -= nb_tx;
    }

    // failed_to_push += k - nb_tx;
  }

  /* if (!bidi) */
  /*   print_stats(fp, 0, hist); */
  fprintf(fp, "failed to push %ld\n", failed_to_push);
  fprintf(fp, "average cycles error %f\n", cycles_error);
  fprintf(fp, "=================================\n");
  fflush(fp);

  /* free allocated memory */
  /* free_p_hist(hist); */
  cntx->running = 0;
  return 0;
}
