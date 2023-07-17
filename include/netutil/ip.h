#ifndef _IP_H_
#define _IP_H_

#include <aos/aos.h>
#include <stdint.h>
#include <stddef.h>

#define IP_DEBUG_OPTION 1

#if defined(IP_DEBUG_OPTION)
#define IP_DEBUG(x...) debug_printf("[ip] " x);
#else
#define IP_DEBUG(fmt, ...) ((void)0)
#endif

#define IP_RF 0b100        /* reserved fragment flag */
#define IP_DF 0b010        /* dont fragment flag */
#define IP_MF 0b001        /* more fragments flag */
#define IP_HLEN 20       /* Default size for ip header */
#define IP_PROTO_ICMP    1
#define IP_PROTO_IGMP    2
#define IP_PROTO_UDP     17
#define IP_PROTO_UDPLITE 136
#define IP_PROTO_TCP     6

typedef uint32_t ip_addr_t;
#define MK_IP(a,b,c,d) (((a)<<24)|((b)<<16)|((c)<<8)|(d))

struct ip_hdr {
  struct {
    /* header length */
    uint8_t h_len : 4;
    /* version */
    uint8_t version : 4;
  };
  /* type of service */
  uint8_t tos;
  /* total length */
  uint16_t len;
  /* identification */
  uint16_t id;
  struct {
    uint16_t flags : 3;
    /* fragment offset field */
    uint16_t offset : 13; 
  };
  /* time to live */
  uint8_t ttl;
  /* protocol*/
  uint8_t proto;
  /* checksum */
  uint16_t chksum;
  /* source and destination IP addresses */
  ip_addr_t src;
  ip_addr_t dest; 
} __attribute__((__packed__));

static_assert(sizeof(struct ip_hdr) == 20);

#endif
