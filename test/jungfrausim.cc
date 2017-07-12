#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <time.h>

#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <vector>

//#define USE_RAW

static unsigned parse_ip(const char* ipString) {
  unsigned ip = 0;
  in_addr inp;
  if (inet_aton(ipString, &inp)) {
    ip = ntohl(inp.s_addr);
  }
  return ip;
}

static unsigned parse_interface(const char* interfaceString) {
  unsigned interface = parse_ip(interfaceString);
  if (interface == 0) {
    int so = socket(AF_INET, SOCK_DGRAM, 0);
    if (so < 0) {
      perror("Failed to open socket\n");
      return 0;
    }
    ifreq ifr;
    strcpy(ifr.ifr_name, interfaceString);
    int rv = ioctl(so, SIOCGIFADDR, (char*)&ifr);
    close(so);
    if (rv != 0) {
      printf("Cannot get IP address for network interface %s.\n",interfaceString);
      return 0;
    }
    interface = ntohl( *(unsigned*)&(ifr.ifr_addr.sa_data[2]) );
  }
  printf("Using interface %s (%d.%d.%d.%d)\n",
         interfaceString,
         (interface>>24)&0xff,
         (interface>>16)&0xff,
         (interface>> 8)&0xff,
         (interface>> 0)&0xff);
  return interface;
}

#define BUFFER_SIZE 8214
#define DATA_SIZE 8192
#define PACKET_NUM 128

#pragma pack(push)
#pragma pack(2)
  struct jungfrau_dgram {
    char emptyheader[6];
    uint32_t reserved;
    char packetnum;
    char framenum[3];
    uint64_t bunchid;
    /* uint64_t framenum;   //  modified dec 15 */
    /* uint64_t packetnum;     */
  };
#pragma pack(pop)

static void usage(const char* p)
{
  printf("Usage: %p [options]\n",p);
  printf("\t-a\tDestination address, dotted notation\n");
  printf("\t-p\tDestination UDP port\n");
  printf("\t-i\tNetwork interface, address or name\n");
}

int main(int argc, char **argv) 
{
  unsigned interface = 0x7f000001;
  unsigned port  = 0;
  unsigned uaddr=0;
  extern char* optarg;

  int c;
  while ( (c=getopt( argc, argv, "a:i:p:h?")) != EOF ) {
    switch(c) {
    case 'a':
      uaddr = parse_ip(optarg);
      break;
    case 'i':
      interface = parse_interface(optarg);
      break;
    case 'p':
      port = strtoul(optarg,NULL,0);
      break;
    default:
      usage(argv[0]);
      return 0;
    }
  }

  if (uaddr==0) {
    printf("Destination address required\n");
    usage(argv[0]);
  }

  sockaddr_in dsa;
  dsa.sin_family      = AF_INET;
  dsa.sin_addr.s_addr = htonl(uaddr);
  dsa.sin_port        = htons(port);
  memset(dsa.sin_zero,0,sizeof(dsa.sin_zero));

  int fd;
  if ((fd = ::socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket");
    return -1;
  }
  
  unsigned skb_size = 0x100000;

  int y=1;
  if(setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (char*)&y, sizeof(y)) == -1) {
    perror("set broadcast");
    return -1;
  }
      
  {
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, 
                     (char*)&skb_size, sizeof(skb_size)) < 0) {
      perror("so_sndbuf");
      return -1;
    }

    { sockaddr_in sa;
      sa.sin_family      = AF_INET;
      sa.sin_addr.s_addr = htonl(interface|0xff);
      sa.sin_port        = htons(port);
      memset(sa.sin_zero,0,sizeof(sa.sin_zero));
      printf("binding to %x.%d\n",ntohl(sa.sin_addr.s_addr),ntohs(sa.sin_port));
      if (::bind(fd, (sockaddr*)&sa, sizeof(sockaddr_in)) < 0) {
        perror("bind");
        return -1;
      }
      sockaddr_in name;
      socklen_t name_len=sizeof(name);
      if (::getsockname(fd,(sockaddr*)&name,&name_len) == -1) {
        perror("getsockname");
        return -1;
      }
      printf("bound to %x.%d\n",ntohl(name.sin_addr.s_addr),ntohs(name.sin_port));
    }

    if ((uaddr>>28) == 0xe) {
      in_addr addr;
      addr.s_addr = htonl(interface);
      if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&addr,
                     sizeof(in_addr)) < 0) {
        perror("set ip_mc_if");
        return -1;
      }
    }
  }

  uint64_t nbytes = 0, tbytes = 0;

  struct jungfrau_dgram hdr;
  uint32_t& hdr_id = *reinterpret_cast<uint32_t*>(&hdr.packetnum);
  hdr.bunchid = 0xaaaabbbbccccddddULL;

  char* payload = new char[PACKET_NUM*DATA_SIZE];
  {
    uint16_t* p = reinterpret_cast<uint16_t*>(payload);
    for(unsigned i=0; i<PACKET_NUM; i++)
      for(unsigned j=0; j<DATA_SIZE/2; j++)
        *p++ = ((i&0xff)<<8) | ((j&0xff)<<0);
  }

  struct iovec iov[2];
  iov[0].iov_base = &hdr;
  iov[0].iov_len  = sizeof(hdr);
  iov[1].iov_base = payload;
  iov[1].iov_len  = DATA_SIZE;

  struct msghdr msg;
  msg.msg_name       = &dsa;
  msg.msg_namelen    = sizeof(dsa);
  msg.msg_iov        = iov;
  msg.msg_iovlen     = 2;
  msg.msg_controllen = 0;
  msg.msg_flags      = 0;

  timespec tv_begin;
  clock_gettime(CLOCK_REALTIME,&tv_begin);
  double t = 0;

  unsigned frame=0;

  while(1) {

    for(unsigned i=0; i<PACKET_NUM; i++) {

      hdr_id = (frame<<8) | (PACKET_NUM-1-i);
      iov[1].iov_base = payload + i*DATA_SIZE;

      ssize_t bytes;
      bytes = ::sendmsg(fd, &msg, 0);

      if (bytes < 0) {
        perror("recv/send");
        continue;
      }
      tbytes += bytes;

      timespec tv;
      clock_gettime(CLOCK_REALTIME,&tv);
      double dt = double(tv.tv_sec - tv_begin.tv_sec) +
        1.e-9*(double(tv.tv_nsec)-double(tv_begin.tv_nsec));
      if (dt > 1) {
        t += dt;
        nbytes += tbytes;
        printf("\t%uMB/s\t%uMB/s\t%dMB\t%d.%09d\n",
               unsigned(double(nbytes)/ t*1.e-6),
               unsigned(double(tbytes)/dt*1.e-6),
               unsigned(double(tbytes)*1.e-6),
               unsigned(tv.tv_sec), unsigned(tv.tv_nsec));
                      
        tv_begin = tv;
        tbytes = 0;
      }
    }

    ++frame;

  }

  return 0;
}
