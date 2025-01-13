#include <stdio.h>
#include <unistd.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include "exp.h"
#include "flow_rules.h"
#include "config.h"

#define RX_RING_SIZE (512)
#define TX_RING_SIZE (512)

#define NUM_MBUFS (32768)
#define MBUF_CACHE_SIZE (512)
#define PRIV_SIZE 256


static unsigned int dpdk_port = 0;

/*
 * Initialize an ethernet port
 */
static inline int dpdk_port_init(uint8_t port, struct rte_mempool *mbuf_pool,
                            uint16_t queues, struct rte_ether_addr *my_eth) {

  struct rte_eth_conf port_conf = {
      .rxmode =
          {
              .max_lro_pkt_size = RTE_ETHER_MAX_LEN,
              .offloads = 0x0,
              .mq_mode = RTE_ETH_MQ_RX_NONE,
          },
      .rx_adv_conf =
          {
              .rss_conf =
                  {
                      .rss_key_len = 0,
                      .rss_key = NULL,
                      .rss_hf = 0x0, // ETH_RSS_NONFRAG_IPV4_UDP,
                  },
          },
      .txmode = {
          .offloads = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_UDP_CKSUM,
      }};

  int retval;
  const uint16_t rx_rings = queues, tx_rings = queues;

  uint16_t nb_rxd = RX_RING_SIZE,
           nb_txd = TX_RING_SIZE; // number of descriptors
  uint16_t q;

  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf *txconf;

  printf("Connecting to port %u with %u queues\n", port, queues);

  if (!(rte_eth_dev_is_valid_port(port)))
    return -1;

  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0)
    return -1;

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0)
    return -1;

  // rx allocate queues
  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(
        port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval != 0)
      return -1;
  }

  // tx allocate queues
  rte_eth_dev_info_get(0, &dev_info);
  txconf = &dev_info.default_txconf;

  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                    rte_eth_dev_socket_id(port), txconf);
    if (retval != 0)
      return -1;
  }

  // start ethernet port
  retval = rte_eth_dev_start(port);
  if (retval < 0)
    return retval;

  // display port mac address
  rte_eth_macaddr_get(port, my_eth);

  // retval = rte_eth_promiscuous_enable(port);
  // if (retval < 0)
  //   return retval;
  return 0;
}

static int create_pools(struct rte_mempool **rx_mbuf_pool,
    struct rte_mempool **tx_mbuf_pool)
{
  const int name_size = 64;
  long long pid = getpid();
  char pool_name[name_size];

  // create mempools
  snprintf(pool_name, name_size, "MUBF_RX_POOL_%lld", pid);
  *rx_mbuf_pool =
    rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (*rx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create rx mbuf pool\n");

  snprintf(pool_name, name_size, "MBUF_TX_POOL_%lld", pid);
  *tx_mbuf_pool =
    rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (*tx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");
}

static volatile int signal_received = 0;
static struct context *context_ptr;
void handle_main_int(__attribute__((unused)) int s) {
	signal_received = 1;
  	int count_core = rte_lcore_count();
	for (int i = 0; i < count_core; i++) {
		struct context *c = &context_ptr[i];
		c->running = 0;
	}
}

/*
 * Main function for running a server or client applications the client is a
 * udp flow generator and the server is a echo server. Client calculates the
 * latency of a round trip and reports percentiles.
 * */
int main(int argc, char *argv[]) {
  parse_args(argc, argv);
  struct rte_mempool *rx_mbuf_pool = NULL;
  struct rte_mempool *tx_mbuf_pool = NULL;

  // mac address of connected port
  struct rte_ether_addr my_eth = {};

  // contex pointers pass to functions
  struct context cntxs[20] = {};
  context_ptr = cntxs;
  char *output_buffers[20] = {};

  // set a function pointer with a context parameter
  int (*process_function)(void *);


  // how many cores is allocated to the program (dpdk)
  int count_core;
  int lcore_id;
  int cntxIndex;

  count_core = rte_lcore_count();
  /* if (config.mode == mode_client && count_core > 1) { */
  /*   rte_exit(EXIT_FAILURE, "client does not support multi core expreiments! things have broken.\n"); */
  /* } */
  printf("Count core: %d\n", count_core);

  if (count_core > config.num_queues) {
    rte_exit(EXIT_FAILURE, "count core is more than available queues\n");
  }

  printf("Number of queues: %d\n", config.num_queues);

  // check if dpdk port exists
  if (!rte_eth_dev_is_valid_port(dpdk_port))
    rte_exit(EXIT_FAILURE, "Error no available port\n");

  create_pools(&rx_mbuf_pool, &tx_mbuf_pool);

  if (dpdk_port_init(dpdk_port, rx_mbuf_pool, config.num_queues, &my_eth) != 0) {
    rte_exit(EXIT_FAILURE, "Cannot init port %hhu\n", dpdk_port);
  }

  // TODO: fractions are not counted here
  assert(config.num_queues % count_core == 0);

  // fill context objects ===========================================
  int next_qid = 0;
  int queue_per_core = config.num_queues / count_core;
  int findex = 0;
  for (int i = 0; i < count_core; i++) {
    cntxs[i].mode = config.mode;
    cntxs[i].rx_mem_pool = rx_mbuf_pool;
    cntxs[i].tx_mem_pool = tx_mbuf_pool;
    cntxs[i].worker_id = i;
    cntxs[i].dpdk_port_id = dpdk_port;
    cntxs[i].my_eth = my_eth;

    cntxs[i].default_qid = next_qid; // poll this queue
    next_qid += queue_per_core;
    assert(next_qid - 1 < config.num_queues);

    cntxs[i].running = 1;     // this job of this cntx has not finished yet
    cntxs[i].src_ip = config.source_ip + i;
    cntxs[i].use_vlan = config.use_vlan;
    cntxs[i].bidi = config.bidi;
    cntxs[i].do_arp = config.do_arp;

    /* how many queue the contex is responsible for */
    cntxs[i].count_queues = queue_per_core;

    /* allocate output buffer and get a file descriptor for that */
    output_buffers[i] = malloc(2048);
    FILE *fp = fmemopen(output_buffers[i], 2048, "w+");
    assert(fp != NULL);
    cntxs[i].fp = fp;

    if (config.mode == mode_server) {
      cntxs[i].dst_port = 0;
      cntxs[i].src_port = config.server_port;
      cntxs[i].managed_queues = malloc(queue_per_core * sizeof(uint32_t));
      for (int q = 0; q < queue_per_core; q++) {
        cntxs[i].managed_queues[q] = (findex * queue_per_core) + q;
      }
      findex++;

      cntxs[i].delay_us = config.server.server_delay;
      cntxs[i].delay_cycles = config.server.server_delay;
      printf("Server delay: %d\n", config.server.server_delay);

      cntxs[i].dst_ips = NULL;
    } else if (config.mode == mode_memcached_client) {
      assert((config.memcd.count_server_ips % count_core) == 0);
      int ips = config.memcd.count_server_ips / count_core;
      cntxs[i].src_port = config.memcd.client_port + i;
      cntxs[i].dst_ips = malloc(sizeof(int) * ips);
      {
        char ip_str[20];
        for (int j = 0; j < ips; j++) {
          cntxs[i].dst_ips[j] = config.memcd.server_ips[findex++];
          ip_to_str(cntxs[i].dst_ips[j], ip_str, 20);
          printf("ip: %s\n", ip_str);
        }
      }
      cntxs[i].count_dst_ip = ips;
      cntxs[i].dst_port = config.server_port;
      cntxs[i].base_port_number = config.server_port;
      cntxs[i].duration = config.memcd.duration;
      cntxs[i].destination_distribution = DIST_UNIFORM; // DIST_ZIPF;
      cntxs[i].queue_selection_distribution = DIST_UNIFORM;
      cntxs[i].managed_queues = NULL;
      cntxs[i].batch = config.memcd.batch;

      cntxs[i].rate_limit = config.memcd.rate_limit;
      cntxs[i].rate = config.memcd.rate;
    } else {
      // this is a client application
      cntxs[i].rate_limit = config.client.rate_limit;
      cntxs[i].rate = config.client.rate;

      // TODO: fractions are not considered for this division
      assert((config.client.count_server_ips % count_core) == 0);
      assert((config.client.count_flow % count_core) == 0);
      int ips = config.client.count_server_ips / count_core;

      cntxs[i].src_port = config.client.client_port + i;
      cntxs[i].dst_ips = malloc(sizeof(int) * ips);
      {
        char ip_str[20];
        for (int j = 0; j < ips; j++) {
          cntxs[i].dst_ips[j] = config.client.server_ips[findex++];
          ip_to_str(cntxs[i].dst_ips[j], ip_str, 20);
          printf("ip: %s\n", ip_str);
        }
      }
      cntxs[i].count_dst_ip = ips;
      cntxs[i].dst_port = config.server_port;
      cntxs[i].payload_length = config.payload_size;
      printf("server port is %d\n", config.server_port);

      cntxs[i].count_flow = config.client.count_flow / count_core;
      cntxs[i].base_port_number = config.server_port;

      cntxs[i].duration = config.client.duration;
      cntxs[i].delay_cycles = config.client.delay_cycles;

      /* use zipf for selecting dst ip */
      cntxs[i].destination_distribution = DIST_UNIFORM; // DIST_ZIPF;
      cntxs[i].queue_selection_distribution = DIST_UNIFORM;

      cntxs[i].managed_queues = NULL;
      cntxs[i].batch = config.client.batch;
    }
  }

  cntxIndex = 1;
  if (config.mode == mode_server) {
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
      rte_eal_remote_launch(do_server, (void *)&cntxs[cntxIndex++], lcore_id);
    }
    do_server(&cntxs[0]);

    // ask other workers to stop
    for (int i = 0; i < count_core; i++) {
      cntxs[i].running = 0;
    }

    rte_eal_mp_wait_lcore(); // wait until all workers are stopped

    for (int i = 0; i < count_core; i++) {
      printf("------ worker %d ------\n", i);
      printf("%s\n", output_buffers[i]);
      printf("------  end  ---------\n");
    }
  } else {
    switch(config.mode) {
      case mode_client:
        process_function = do_client;
        break;
      case mode_latency_clinet:
        process_function = do_latency_client;
        break;
      case mode_memcached_client:
        process_function = do_memcd;
        break;
      default:
        rte_exit(EXIT_FAILURE, "unsupported application mode\n");
    }
    signal(SIGINT, handle_main_int);
    signal(SIGHUP, handle_main_int);
    
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
      rte_eal_remote_launch(process_function, (void *)&cntxs[cntxIndex++], lcore_id);
    }
    process_function(&cntxs[0]);
    rte_eal_mp_wait_lcore();

    for (int i = 0; i < count_core; i++) {
      printf("%s\n", output_buffers[i]);
    }
  }

  // free
  for (int i = 0; i < count_core; i++) {
    if (config.mode == mode_server) {
      free(cntxs[i].managed_queues);
    } else {
      free(cntxs[i].dst_ips);
    }
    fclose(cntxs[i].fp);
    free(output_buffers[i]);
  }
  for (int q = 0; q < config.num_queues; q++) {
    rte_eth_tx_done_cleanup(dpdk_port, q, 0);
  }
  return 0;
}
