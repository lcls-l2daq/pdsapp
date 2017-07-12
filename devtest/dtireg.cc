
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <new>

#include <cpsw_api_builder.h>
#include <cpsw_mmio_dev.h>
#include <cpsw_proto_mod_depack.h>

#include "pds/xpm/Module.hh"

#include <string>

//#define USE_STREAM

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [-a <IP addr (dotted notation)>] [-p <port>] [-w]\n",p);
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;
  bool lWrite = false;

  const char* ip = "10.0.2.103";
  unsigned short port = 8194;

  while ( (c=getopt( argc, argv, "a:p:wh")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
      break;
    case 'p':
      port = strtoul(optarg,NULL,0);
      break;
    case 'w':
      lWrite = true;
      break;
    case '?':
    default:
      lUsage = true;
      break;
    }
  }

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  NetIODev  root = INetIODev::create("fpga", ip);

  {  // Streaming access
    ProtoStackBuilder bldr = IProtoStackBuilder::create();
    bldr->setSRPVersion              ( IProtoStackBuilder::SRP_UDP_V3 );
    bldr->setUdpPort                 (                  port );
    bldr->setSRPTimeoutUS            (                 90000 );
    bldr->setSRPRetryCount           (                     5 );
    bldr->setSRPMuxVirtualChannel    (                     0 );
    bldr->useDepack                  (                  true );
    bldr->useRssi                    (                  true );

    Field    irq = IField::create("irq");
    root->addAtAddress( irq, bldr );
  }

  Path path = IPath::create(root);

  Stream strm = IStream::create( path->findByName("irq") );
  CTimeout         tmo(100000);
  CAxisFrameHeader hdr;

  uint8_t buf[256];
  int v;

  if (lWrite) {
    CAxisFrameHeader hdr;
    uint8_t          buf[1500];
    hdr.insert(buf, sizeof(buf));
    hdr.iniTail(buf + hdr.getSize()+0x8);
    unsigned sz = hdr.getSize()+hdr.getTailSize()+0x8;
    uint32_t* bb = reinterpret_cast<uint32_t*>(&buf[hdr.getSize()]);
    bb[0] = 1;  // read from address 0
    bb[1] = 0xDEADBEEF;
    strm->write( (uint8_t*)buf, sz);
  }

  while ((v=strm->read( buf, sizeof(buf), tmo, 0 ))>=0) {

    if (v) {

      if (!hdr.parse(buf, sizeof(buf))) {
        printf("bad header\n");
        continue;
      }

      timespec tv;
      clock_gettime(CLOCK_REALTIME,&tv);
      
      double dt = double(tv.tv_sec-tvo.tv_sec)+1.e-9*(double(tv.tv_nsec)-double(tvo.tv_nsec));
      tvo=tv;
      printf("Frame %u  %04x  %f\n",hdr.getFrameNo(),*reinterpret_cast<uint32_t*>(&buf[hdr.getSize()]),dt);

    else 
      printf("tmo\n");
    }
  }

  return 0;
}
