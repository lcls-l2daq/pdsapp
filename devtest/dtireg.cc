
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

#include <string>

//#define USE_STREAM

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [-a <IP addr (dotted notation)>] [-p <port>] addr <value>\n",p);
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;

  const char* ip = "10.0.1.103";
  unsigned short port = 8194;

  while ( (c=getopt( argc, argv, "a:p:h")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
      break;
    case 'p':
      port = strtoul(optarg,NULL,0);
      break;
    case '?':
    default:
      lUsage = true;
      break;
    }
  }

  if (lUsage || optind >= argc) {
    usage(argv[0]);
    exit(1);
  }

  NetIODev  root = INetIODev::create("fpga", ip);

  {  // Streaming access
    ProtoStackBuilder bldr = IProtoStackBuilder::create();
    bldr->setSRPVersion              ( IProtoStackBuilder::SRP_UDP_NONE );
    bldr->setUdpPort                 (                  port );
    bldr->setSRPTimeoutUS            (                 90000 );
    bldr->setSRPRetryCount           (                     5 );
    //    bldr->setSRPMuxVirtualChannel    (                     0 );
    bldr->useDepack                  (                  true );
    bldr->useRssi                    (                  true );
    bldr->setTDestMuxTDEST           (                     0 );

    Field    app = IField::create("app");
    root->addAtAddress( app, bldr );
  }

  Path path = IPath::create(root);

  Stream strm = IStream::create( path->findByName("app") );
  CTimeout         tmo(100000);

  uint8_t buf[256];
  int v;

  bool lWrite = optind < argc-1;

  {
    CAxisFrameHeader hdr;
    hdr.insert(buf, sizeof(buf));
    hdr.iniTail(buf + hdr.getSize()+ (lWrite?0x8:0x4));
    unsigned sz = hdr.getSize()+hdr.getTailSize()+(lWrite?0x8:0x4);
    uint32_t* bb = reinterpret_cast<uint32_t*>(&buf[hdr.getSize()]);
    if (lWrite) {
      bb[0] = strtoul(argv[optind++],NULL,0);
      bb[1] = strtoul(argv[optind++],NULL,0);
      printf("Frame %u: Write reg[%u]=0x%08x\n",
             hdr.getFrameNo(),
             bb[0],
             bb[1]);
      strm->write( (uint8_t*)buf, sz);
      return 0;
    }
    else {
      bb[0] = strtoul(argv[optind++],NULL,0);
      printf("Frame %u: Read reg[%u]\n",
             hdr.getFrameNo(),
             bb[0]);
      strm->write( (uint8_t*)buf, sz);
    }
  }

  while ((v=strm->read( buf, sizeof(buf), tmo, 0 ))>=0) {

    if (v) {

      CAxisFrameHeader hdr;
      if (!hdr.parse(buf, sizeof(buf))) {
        printf("bad header\n");
        continue;
      }

      printf("Frame %u:",hdr.getFrameNo());
      const uint32_t* p = reinterpret_cast<uint32_t*>(&buf[hdr.getSize()]);
      v -= hdr.getSize();
      v >>= 2;
      for(unsigned i=0; i<v; i++)
        printf(" %08x", p[i]);
      printf("\n");
      break;
    }
    else {
      printf("tmo\n");
      break;
    }
  }

  return 0;
}
