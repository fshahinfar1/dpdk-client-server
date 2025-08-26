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
  int16_t hdr_encp_sz;
  
  bool select_src_ip;
  uint32_t unique_client_addresses;
  uint32_t unique_client_ports;
  float zipf_arg;
  
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
  uint8_t dest_mac[6]; // used when do_arp is false
};

// Some global variables
extern struct app_config config;
void parse_args(int argc, char *argv[]);

# endif
