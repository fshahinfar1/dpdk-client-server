#pragma once
#include "prepare_packet.h"
#include "zipf.h"

// the structure of the header-option used to embed server_id is:
//  __u8 kind | __u8 len | __u32 server_id
// Arbitrarily picked unused value from IANA TCP Option Kind Numbers
#define KATRAN_TCP_HDR_OPT_KIND_TPR 0xB7
// Length of the tcp header option
#define KATRAN_TCP_HDR_OPT_LEN_TPR 6
#define KATRAN_MAX_QUIC_REALS 0x00fffffe
#define TCP_NOP_OPT 0x01

struct katran_srv_opt {
    uint8_t kind;
    uint8_t len;
    uint32_t srv_id;
    /* uint16_t pad; */
} __attribute__((packed));

struct tcp_packet_info {
  uint32_t add_katran_option;
};

void prepare_tcp(struct rte_mbuf *buf,
                 struct prepare_packet_info *info,
                 struct tcp_packet_info *tcp_info,
                void **out_payload)
{
  char *buf_ptr = NULL;
  struct rte_ether_hdr *eth_hdr = NULL;
  struct rte_vlan_hdr *vlan_hdr = NULL;
  struct rte_ipv4_hdr *ipv4_hdr = NULL;
  
  uint32_t size_tcp_opts = 0;
  uint32_t count_noop_opt = 0;

  _prepare_eth_vlan_ip(buf, info, &eth_hdr, &vlan_hdr, &ipv4_hdr);

  if (tcp_info->add_katran_option > 0) {
    count_noop_opt = 2; // noop is one byte so its size is same as count
    // the TCP option is 6 bytes and 2 bytes of padding for making it a multiple of 4B (32-bit block)
    size_tcp_opts = KATRAN_TCP_HDR_OPT_LEN_TPR + count_noop_opt;
    assert(size_tcp_opts % 4 == 0);
  }

  const size_t hdr_size = sizeof(struct rte_tcp_hdr) + size_tcp_opts;
  ipv4_hdr->next_proto_id = IPPROTO_TCP;
  ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                                            hdr_size + info->payload_length);

  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_tcp_hdr));
  struct rte_tcp_hdr *tcp_hdr = (struct rte_tcp_hdr *)buf_ptr;
  
  // _prepare_eth_vlan_ip updates the src_port if needed based on client_address_selection
  tcp_hdr->src_port = rte_cpu_to_be_16(info->src_port);
  tcp_hdr->dst_port = rte_cpu_to_be_16(info->dst_port);
  tcp_hdr->sent_seq = 123;                           // TODO: does it matter in our experiment?
  tcp_hdr->recv_ack = 321;                           // TODO: does it matter in our experiment?
  tcp_hdr->data_off = (((hdr_size) / 4) & 0xf) << 4; // how many 32 bit rows?
  tcp_hdr->tcp_flags = RTE_TCP_ACK_FLAG | RTE_TCP_PSH_FLAG;
  tcp_hdr->rx_win = 256; // TODO: does it matter in our experiment?
  tcp_hdr->cksum = 0;
  tcp_hdr->tcp_urp = 0;

  if (tcp_info->add_katran_option > 0) {
    buf_ptr = rte_pktmbuf_append(buf, size_tcp_opts);
    for (uint32_t z = 0; z < count_noop_opt; z++) {
        *buf_ptr = TCP_NOP_OPT;
        buf_ptr++;
    }
    struct katran_srv_opt *opt = (struct katran_srv_opt *)buf_ptr;
    opt->kind = KATRAN_TCP_HDR_OPT_KIND_TPR;
    opt->len = KATRAN_TCP_HDR_OPT_LEN_TPR;

    opt->srv_id = myrand() % tcp_info->add_katran_option;
    if (opt->srv_id == 0)
        opt->srv_id = 1;
    /* static uint32_t last_srv_id = 1; */
    /* opt->srv_id = last_srv_id; */
    /* last_srv_id = (last_srv_id + 1024) % KATRAN_MAX_QUIC_REALS; */
    /* if (last_srv_id == 0) // server id zero is invalid */
    /*   last_srv_id = 1; */

    /* opt->pad = 0; */
    // TODO: do I need to add more options?
  }

  // payload
  *out_payload = rte_pktmbuf_append(buf, info->payload_length);

  // TODO: if the hardware does not support the checksum offload, I have
  // to calculate it in this program ...
  buf->ol_flags = RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_TCP_CKSUM;
}
