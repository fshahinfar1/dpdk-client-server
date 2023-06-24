# ifndef _UDP_CONFIG_H
# define _UDP_CONFIG_H
#include "include/exp.h"

#define SHIFT_ARGS {argc--;if(argc>0)argv[1]=argv[0];argv++;};

// Some constants
const int mode_client = 0;
const int mode_server = 1;

// Some types

struct client_config {
  uint16_t client_port;
  uint32_t count_server_ips;
  uint32_t *server_ips;
  // how many flows should client generate
  int count_flow;
  int32_t duration;
};

struct server_config {
  // delay for each burst in server (us)
  uint32_t server_delay;
  uint64_t delay_cycles;
};

struct app_config {
  int bidi;
  int use_vlan;
  uint32_t source_ip;
  uint16_t num_queues;
  int mode;
  uint8_t rate_limit;
  uint64_t rate;
  uint32_t payload_size;
  uint16_t server_port;
  union {
    struct client_config client;
    struct server_config server;
  };
};

// Some global variables
extern static struct app_config config;


static void print_usage(void)
{
  printf("usage: ./app [DPDK EAL arguments] -- [Client arguments]\n"
  "arguments:\n"
  "    * (optional) bidi=<true/false> [default: true]\n"
  "    * source_ip: ip of host on which app is running (useful for arp)\n"
  "    * number of queue\n"
  "    * mode: client or server\n"
  "[client]\n"
  "    * count destination ips\n"
  "    * valid ip values (as many as defined in prev. param)\n"
  "    * count flow\n"
  "    * experiment duration (zero means run with out a limit)\n"
  "    * client port\n"
  "    * client delay cycles\n"
  "    * rate value (pps)\n"
  "[server]\n"
  "    * server delay for each batch\n");
}

static void parse_client(int argc, char *argv[])
{
  // client
  config.mode = mode_client;
  SHIFT_ARGS;

  // parse client arguments
  if (argc < 4) {
    print_usage();
    rte_exit(EXIT_FAILURE, "Wrong number of arguments for the client app");
  }

  config.client.count_server_ips = atoi(argv[1]);
  SHIFT_ARGS;
  if (argc < 3 + count_server_ips) {
    print_usage();
    rte_exit(EXIT_FAILURE, "Wrong number of arguments for the client (number of ips does not match)");
  }

  config.client.server_ips = malloc(count_server_ips * sizeof(uint32_t));
  for (int i = 0; i < count_server_ips; i++) {
    str_to_ip(argv[1], server_ips + i);
    SHIFT_ARGS;
  }

  config.client.count_flow = atoi(argv[1]);
  if (count_flow < 1) {
    rte_exit(EXIT_FAILURE, "number of flows should be at least one");
  }
  printf("Count flows: %d\n", count_flow);
  SHIFT_ARGS;

  if (argc > 1) {
    config.client.duration = atoi(argv[1]);
    SHIFT_ARGS;
  }
  printf("Experiment duration: %d\n", duration);

  if (argc > 1) {
    config.client.client_port = atoi(argv[1]);
    SHIFT_ARGS;
  }
  printf("Client port: %d\n", client_port);

  if (argc > 1) {
    config.client.delay_cycles = atol(argv[1]);
    SHIFT_ARGS;
  }
  printf("Client processing between each packet %ld cycles\n", delay_cycles);

  if (argc > 1) {
    config.client.rate_limit = 1;
    config.client.rate = atol(argv[1]);
    SHIFT_ARGS
  }

  if (config.client.rate_limit) {
    printf("Client rate limit is on rate: %ld\n", rate);
  } else {
    printf("Client rate limit is off\n");
  }
}

static void parse_server(int argc, char *argv[])
{
  // server
  config.mode = mode_server;
  SHIFT_ARGS;

  // parse server arguments
  if (argc < 1) {
    rte_exit(EXIT_FAILURE, "wrong number of arguments for server");
    print_usage();
  }
  server_delay = atoi(argv[1]);
  SHIFT_ARGS;
}

/* TODO: this parsing system is not good implement it differently */
static void parse_args(int argc, char *argv[])
{
  memset(&config, 0, sizeof(config));

  // default values
  config.payload_size = 64;
  config.client.client_port = rte_cpu_to_be_16(3000);
  config.server.server_port = rte_bcpu_to_be_16(8080);

  // let dpdk parse its own arguments
  uint32_t args_parsed = dpdk_init(argc, argv);
  argc -= args_parsed;
  argv += args_parsed;

  if (argc < 4) {
    printf("argc < 4\n");
    print_usage();
    rte_exit(EXIT_FAILURE, "Wrong number of arguments");
  }

  if (strncmp(argv[1], "bidi=", 5) == 0) {
    if (strncmp(argv[1] + 5, "false", 5) == 0) {
      config.bidi = 0;
    } else if (strncmp(argv[1] + 5, "true", 4) == 0) {
      config.bidi = 1;
    } else {
      printf("bidi flag value is not recognized\n");
      print_usage();
      return 1;
    }
    SHIFT_ARGS;
  }

  // source_ip
  str_to_ip(argv[1], &config.source_ip);
  SHIFT_ARGS;

  // number of queues
  config.num_queues = atoi(argv[1]);
  if (num_queues < 1) {
    rte_exit(EXIT_FAILURE, "At least one queue is needed");
  }
  SHIFT_ARGS;

  // mode
  if (!strcmp(argv[1], "client")) {
    parse_client(argc, argv);
  } else if (!strcmp(argv[1], "server")) {
    parse_server(argc, argc);
  } else {
    rte_exit(EXIT_FAILURE, "Second argument should be `client` or `server`\n");
  }
}

# endif // _UDP_CONFIG_H
