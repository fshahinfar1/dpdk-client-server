# ifndef _CONFIG_H
# define _CONFIG_H
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include "include/exp.h"

#define SHIFT_ARGS {argc--;if(argc>0)argv[1]=argv[0];argv++;};
#define MAX_SERVER_IP_DEST 32
#define MAX_BATCH_SIZE 64

enum {
  mode_undefined = 0,
  mode_client,
  mode_server,
  mode_latency_clinet,
};

// Some types

struct client_config {
  uint16_t client_port;
  uint32_t count_server_ips;
  uint32_t *server_ips;
  // how many flows should client generate
  int count_flow;
  int32_t duration;
  uint8_t rate_limit;
  uint64_t rate;
  uint64_t delay_cycles;
  uint16_t batch;
};

struct server_config {
  // delay for each burst in server (us)
  uint32_t server_delay;
};

struct app_config {
  int bidi;
  int use_vlan;
  int do_arp;
  uint32_t source_ip;
  uint16_t num_queues;
  int mode;
  uint32_t payload_size;
  uint16_t server_port;
  union {
    struct client_config client;
    struct server_config server;
  };
};

// Some global variables
extern struct app_config config;

static void print_usage_app(void)
{
  printf("Usage: ./app [DPDK EAL arguments] -- [App arguments]\n"
      "    --client\n"
      "    --latency-client\n"
      "    --server\n");
}

static void print_usage_server(void)
{
  printf("Usage: ./app [DPDK EAL arguments] -- --server [Server arguments]\n"
      "    --ip-local\n"
      "    --port      [default: 8080]\n"
      "    --num-queue [default: 1]\n"
      "    --delay     [default: 0]\n"
      );
}

static void print_usage_client(void)
{
  printf("Usage: ./app [DPDK EAL arguments] -- --client [Client arguments]\n"
      "    --ip-local\n"
      "    --ip-dest\n"
      "    --num-flow\n"
      "    --duration\n"
      "    --port client UDP port number [default: 3000]\n"
      "    --port-dest server UDP port [default: 8080]\n"
      "    --delay (cycles) [default: 0 no dleay]\n"
      "    --rate (pps)     [default: 0 no rate limit]\n"
      "    --payload (UDP payload size) [default: 64 bytes]\n"
      "    --batch   [default: 32]\n"
      );
}

static void print_usage_latency_client(void)
{
  printf(
      "Descrition: Send a batch of packets toward server and wait for echo.\n"
      "Will not send the next batch until all the packets have been received.\n"
      "Outputs the observed latency of each packet to stdout\n\n"

      "Usage: ./app [DPDK EAL arguments] -- --latency-client [Client arguments]\n"
      "    --ip-local\n"
      "    --ip-dest\n"
      "    --port client UDP port number [default: 3000]\n"
      "    --port-dest server UDP port [default: 8080]\n"
      "    --batch (packets) [default: 1]\n"
      "    --payload (UDP payload size) [default: 64 bytes]\n"
      );
}

static void usage(void)
{
  switch (config.mode) {
    case mode_undefined:
      print_usage_app();
      break;
     case mode_client:
      print_usage_client();
      break;
     case mode_server:
      print_usage_server();
      break;
     case mode_latency_clinet:
      print_usage_latency_client();
      break;
     default:
      rte_exit(EXIT_FAILURE, "Unexpected mode!\n");
  }
}

static int dpdk_init(int argc, char *argv[]) {
  int args_parsed;
  args_parsed = rte_eal_init(argc, argv);
  if (args_parsed < 0) {
    usage();
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }
  return args_parsed;
}

static void check_only_one_mode(void)
{
  if (config.mode == mode_undefined)
    return;
  rte_exit(EXIT_FAILURE, "Multiple application mode were selected\n");
}

static void check_client_mode(void)
{
  if (config.mode != mode_client && config.mode != mode_latency_clinet) {
    rte_exit(EXIT_FAILURE, "Expected to be in client mode\n");
  }
}

static void parse_args(int argc, char *argv[])
{
  int ret;
  int i;
  memset(&config, 0, sizeof(config));
  enum opts {
    HELP = 500,
    NUM_QUEUE,
    CLIENT,
    SERVER,
    LATENCY_CLIENT,
    IP_LOCAL,
    IP_DEST,
    PORT,
    DEST_PORT,
    SERVER_PORT,
    NUM_FLOW,
    DURATION,
    DELAY,
    RATE,
    BATCH_SIZE,
    PAYLOAD_LENGTH,
    UNIDIR,
    NO_ARP,
  };

  struct option long_opts[] = {
    {"help",               no_argument,       NULL, HELP},
    {"client",             no_argument,       NULL, CLIENT},
    {"server",             no_argument,       NULL, SERVER},
    {"latency-client",     no_argument,       NULL, LATENCY_CLIENT},
    {"ip-local",           required_argument, NULL, IP_LOCAL},
    {"ip-dest",            required_argument, NULL, IP_DEST},
    {"port",               required_argument, NULL, PORT},
    {"port-dest",          required_argument, NULL, DEST_PORT},
    {"num-flow",           required_argument, NULL, NUM_FLOW},
    {"duration",           required_argument, NULL, DURATION},
    {"delay",              required_argument, NULL, DELAY},
    {"rate",               required_argument, NULL, RATE},
    {"batch",              required_argument, NULL, BATCH_SIZE},
    {"payload",            required_argument, NULL, PAYLOAD_LENGTH},
    {"unidir",             no_argument,       NULL, UNIDIR},
    {"no-arp",             no_argument,       NULL, NO_ARP},
    /* End of option list ----------------------------------------------- */
    {NULL, 0, NULL, 0},
  };

  // default values
  config.bidi = 1;
  config.use_vlan = 0;
  config.do_arp = 1;
  config.payload_size = 64;
  config.client.client_port = 3000;
  config.server_port = 8080;
  config.client.count_server_ips = 0;
  config.client.server_ips = malloc(MAX_SERVER_IP_DEST * sizeof(uint32_t));
  config.num_queues = 1;
  config.client.batch = 0;

  // let dpdk parse its own arguments
  uint32_t args_parsed = dpdk_init(argc, argv);
  argc -= args_parsed;
  argv += args_parsed;

  while(1) {
    ret = getopt_long(argc, argv, "", long_opts, NULL);
    if (ret == -1)
      break;
    switch (ret) {
      case NUM_QUEUE:
        ret = atoi(optarg);
        if (ret < 1) {
          rte_exit(EXIT_FAILURE, "Invalid number of queues\n");
        }
        config.num_queues = ret;
        break;
      case CLIENT:
        check_only_one_mode();
        config.mode = mode_client;
        break;
      case SERVER:
        check_only_one_mode();
        config.mode = mode_server;
        break;
      case LATENCY_CLIENT:
        check_only_one_mode();
        config.mode = mode_latency_clinet;
        break;
      case IP_LOCAL:
        /* ret = inet_pton(AF_INET, optarg, &config.source_ip); */
        ret = str_to_ip(optarg, &config.source_ip);
        if (ret != 0) {
          rte_exit(EXIT_FAILURE, "Failed to read source ip address\n");
        }
        break;
      case IP_DEST:
        check_client_mode();
        i = config.client.count_server_ips;
        if (i >= MAX_SERVER_IP_DEST) {
          rte_exit(EXIT_FAILURE, "Maximum number of destination servers have been reached\n");
        }
        config.client.count_server_ips++;
        /* ret = inet_pton(AF_INET, optarg, &config.client.server_ips[i]); */
        ret = str_to_ip(optarg, &config.client.server_ips[i]);
        if (ret != 0) {
          rte_exit(EXIT_FAILURE, "Failed to read destination ip address\n");
        }
        break;

      case PORT:
        ret = atoi(optarg);
        if (ret <= 0) {
          rte_exit(EXIT_FAILURE, "Invalid client port number\n");
        }
        if (config.mode == mode_undefined) {
          // Note: this is bad code!
          rte_exit(EXIT_FAILURE, "Expect to set the application mode before defining local port\n");
        }
        if (config.mode == mode_server) {
          config.server_port = ret;
        } else {
          config.client.client_port = ret;
        }
        break;
      case DEST_PORT:
        check_client_mode();
        ret = atoi(optarg);
        if (ret <= 0) {
          rte_exit(EXIT_FAILURE, "Invalid server port number\n");
        }
        config.server_port = ret;
        break;
      case NUM_FLOW:
        config.client.count_flow = atoi(optarg);
        break;
      case DURATION:
        config.client.duration = atoi(optarg);
        break;
      case DELAY:
        if (config.mode == mode_undefined) {
          // Note: this is bad code!
          rte_exit(EXIT_FAILURE, "Expect to set the application mode before setting delay cycles\n");
        }
        ret = atol(optarg);
        if (config.mode == mode_server) {
          config.server.server_delay = ret;
        } else {
          config.client.delay_cycles = ret;
        }
        break;
      case RATE:
        config.client.rate_limit = 1;
        config.client.rate = atol(optarg);
        break;
      case BATCH_SIZE:
        if (config.mode != mode_latency_clinet && config.mode != mode_client) {
          rte_exit(EXIT_FAILURE, "Expected to be in latency-client mode\n");
        }
        ret = atoi(optarg);
        if (ret < 1) {
          rte_exit(EXIT_FAILURE, "Unexpected value for batch size\n");
        }
        config.client.batch = ret;
        break;
      case PAYLOAD_LENGTH:
        check_client_mode();
        ret = atoi(optarg);
        if (ret < 1) {
          rte_exit(EXIT_FAILURE, "Unexpected value for payload length\n");
        }
        config.payload_size = ret;
        break;
      case UNIDIR:
        config.bidi = 0;
        break;
      case NO_ARP:
        config.do_arp = 0;
        break;
      case HELP:
        usage();
        rte_exit(EXIT_SUCCESS, "!");
        break;
      default:
        usage();
        rte_exit(EXIT_FAILURE, "!");
        break;
    }
  }

  if (config.mode == mode_undefined) {
    usage();
    rte_exit(EXIT_FAILURE, "Application mode not set\n");
  }

  if (config.client.batch == 0) {
    if (config.mode == mode_client) {
      config.client.batch = 32;
    } else if (config.mode == mode_latency_clinet) {
      config.client.batch = 1;
    }
  }

  if (config.source_ip == 0) {
    rte_exit(EXIT_FAILURE, "Expect source Ip to be set!\n");
  }

  if (config.client.count_server_ips == 0) {
    if (config.mode == mode_client || config.mode == mode_latency_clinet) {
      rte_exit(EXIT_FAILURE, "Expect at least one destination address\n");
    }
  }
}

# endif
