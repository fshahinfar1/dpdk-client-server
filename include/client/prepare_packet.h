/* vim: set et ts=2 sw=2 */
#pragma once
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <rte_tcp.h>
#include <rte_byteorder.h>

#include "zipf.h"

enum client_addr_selection
{
  FIX_CLIENT_ADDR = 0, // use the fixed source address
  ZIPF_CLIENT_ADDR,    // use zipf distribution to select source address
};

struct zipf_client_addr {
  struct zipfgen *src_zipf;
  uint64_t count_port; // number of unique ports
};

struct prepare_packet_info
{
  bool use_vlan;
  enum client_addr_selection client_address_selection;
  struct rte_ether_addr *src_mac;
  struct rte_ether_addr *dst_mac;
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t tci;
  uint16_t payload_length;
  
  void *client_addr_select_info; // it would point to a struct of appropriate type (e.g., zip_client_addr)
};

void _ethernet_header(struct rte_ether_hdr *eth_hdr, struct rte_ether_addr *src,
                      struct rte_ether_addr *dst, uint16_t ether_type)
{
  rte_ether_addr_copy(src, &eth_hdr->src_addr);
  rte_ether_addr_copy(dst, &eth_hdr->dst_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(ether_type);
}

void _ipv4_header(struct rte_ipv4_hdr *ipv4_hdr, uint32_t src_ip,
                  uint32_t dst_ip)
{
  ipv4_hdr->version_ihl = 0x45;
  ipv4_hdr->type_of_service = 0;
  ipv4_hdr->packet_id = 0;
  ipv4_hdr->fragment_offset = 0;
  ipv4_hdr->time_to_live = 64;
  ipv4_hdr->hdr_checksum = 0;
  ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip);
  ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip);
}

void _prepare_eth_vlan_ip(struct rte_mbuf *buf,
                          struct prepare_packet_info *info,
                          struct rte_ether_hdr **out_eth_hdr,
                          struct rte_vlan_hdr **out_vlan_hdr,
                          struct rte_ipv4_hdr **out_ipv4_hdr)
{
  struct rte_ether_hdr *eth_hdr = NULL;
  struct rte_vlan_hdr *vlan_hdr = NULL;
  struct rte_ipv4_hdr *ipv4_hdr = NULL;

  eth_hdr = (struct rte_ether_hdr *)rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
  if (!info->use_vlan)
  {
    _ethernet_header(eth_hdr, info->src_mac, info->dst_mac, RTE_ETHER_TYPE_IPV4);
    buf->l2_len = RTE_ETHER_HDR_LEN;
  }
  else
  {
    _ethernet_header(eth_hdr, info->src_mac, info->dst_mac, RTE_ETHER_TYPE_VLAN);

    vlan_hdr = (struct rte_vlan_hdr *)rte_pktmbuf_append(buf, sizeof(struct rte_vlan_hdr));
    vlan_hdr->vlan_tci = rte_cpu_to_be_16(info->tci);
    vlan_hdr->eth_proto = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    buf->l2_len = RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr);
  }

  switch (info->client_address_selection)
  {
  case FIX_CLIENT_ADDR:
    break;
  case ZIPF_CLIENT_ADDR:
    {
      struct zipf_client_addr *t = (struct zipf_client_addr *)info->client_addr_select_info;
      const uint32_t src_idx = t->src_zipf->gen(t->src_zipf) - 1;
      const uint32_t ip_offset = src_idx / t->count_port;
      const uint32_t port_offset = src_idx % t->count_port;

      info->src_ip += ip_offset;
      info->src_port += port_offset;
    }
    break;
  default:
    rte_exit(EXIT_FAILURE, "Unexpected client address selection mode\n");
    break;
  }

  ipv4_hdr = (struct rte_ipv4_hdr *)rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
  _ipv4_header(ipv4_hdr, info->src_ip, info->dst_ip);

  *out_eth_hdr = eth_hdr;
  *out_vlan_hdr = vlan_hdr;
  *out_ipv4_hdr = ipv4_hdr;

  buf->l3_len = sizeof(struct rte_ipv4_hdr);
}

void prepare_udp(struct rte_mbuf *buf,
                 struct prepare_packet_info *info,
                void **out_payload)
{
  struct rte_ether_hdr *eth_hdr = NULL;
  struct rte_vlan_hdr *vlan_hdr = NULL;
  struct rte_ipv4_hdr *ipv4_hdr = NULL;
  struct rte_udp_hdr *udp_hdr = NULL;


  _prepare_eth_vlan_ip(buf, info, &eth_hdr, &vlan_hdr, &ipv4_hdr);

  const uint16_t dgram_len = sizeof(struct rte_udp_hdr) + info->payload_length;
  const uint16_t total_length = sizeof(struct rte_ipv4_hdr) + dgram_len;
  ipv4_hdr->next_proto_id = IPPROTO_UDP;
  ipv4_hdr->total_length = rte_cpu_to_be_16(total_length);

  // upd header
  udp_hdr = (struct rte_udp_hdr *)rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr));

  // _prepare_eth_vlan_ip updates the src_port if needed based on client_address_selection
  udp_hdr->src_port = rte_cpu_to_be_16(info->src_port);
  udp_hdr->dst_port = rte_cpu_to_be_16(info->dst_port);
  udp_hdr->dgram_len = rte_cpu_to_be_16(dgram_len);
  udp_hdr->dgram_cksum = 0;

  /* payload */
  *out_payload = rte_pktmbuf_append(buf, info->payload_length);

  // TODO: if the hardware does not support the checksum offload, I have
  // to calculate it in this program ...
  buf->ol_flags = RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_UDP_CKSUM;
}
