
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

#include "pds/dti/Module.hh"
#include "pds/cphw/AxiVersion.hh"

#include "cpsw_api_builder.h"
#include "cpsw_mmio_dev.h"
#include "cpsw_proto_mod_depack.h"

#include <sstream>
#include <string>
#include <vector>


extern int optind;

static bool lVerbose = false;

static unsigned rw_reg(void* p, unsigned reg,
                       bool lWrite, unsigned val)
{
  Stream* pstream = (Stream*)p;
  Stream strm = *pstream;
  unsigned word = val;

  CTimeout         tmo(100000);

  uint8_t buf [1500];
  unsigned sz;
  {
    CAxisFrameHeader hdr;
    hdr.insert(buf, sizeof(buf));
    hdr.iniTail(buf + hdr.getSize()+0x8);
    sz = hdr.getSize()+hdr.getTailSize()+0x8;
    uint32_t* bb = reinterpret_cast<uint32_t*>(&buf[hdr.getSize()]);
    bb[0] = reg | (lWrite ? 0:1);
    bb[1] = word;
    strm->write( (uint8_t*)buf, sz);
    if (lVerbose)
      printf("Write %u  %08x:%08x\n",
             hdr.getFrameNo(),
             bb[0],
             bb[1]);
  }

  {
    uint8_t ibuf[256];
    int v = strm->read( ibuf, sizeof(ibuf), tmo, 0 );
    if (v) {
      CAxisFrameHeader hdr;
      if (!hdr.parse(ibuf, sizeof(ibuf))) {
        printf("bad header\n");
        return -1;
      }
      const uint32_t* bb = reinterpret_cast<const uint32_t*>(&ibuf[hdr.getSize()]);
      if (lVerbose)
        printf("Read %u  %08x:%08x\n",
               hdr.getFrameNo(),
               bb[0],
               bb[1]);
      if ((bb[0]&~1)!=reg) {
        if (lWrite)
          printf("Error in ack to %s reg %x\n", lWrite?"write":"read", reg);
      }
      return bb[1];
    }
  }

  return 0;
}

static unsigned read_reg(void* p, unsigned reg)
{
  return rw_reg(p,reg,false,0);
}

static unsigned write_reg(void* p, unsigned reg, unsigned val)
{
  return rw_reg(p,reg,true,val);
}

void usage(const char* p) {
  printf("Usage: %s [-a <IP addr (dotted notation)>] [-p <port>] [-w]\n",p);
}

static void* dti_input(void*);
static void  configLinks();

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;
  unsigned tdest = 0;

  const char* ip = "10.0.2.103";
  unsigned short port = 8192;

  while ( (c=getopt( argc, argv, "a:p:wh")) != EOF ) {
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

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  Pds::Cphw::Reg::set(ip, port, 0);

  Pds::Cphw::AxiVersion* vsn = new((void*)0) Pds::Cphw::AxiVersion;
  printf("buildStamp %s\n",vsn->buildStamp().c_str());

  Pds::Dti::Module*         m = new((void*)0x80000000) Pds::Dti::Module;
  m->_countCtrl = (1<<30);
  m->_countCtrl = (1<<31);

  timespec _t; clock_gettime(CLOCK_REALTIME,&_t);

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
    bldr->setTDestMuxTDEST           (                 tdest );

    Field    irq = IField::create("irq");
    root->addAtAddress( irq, bldr );
  }

  Path path = IPath::create(root);

  Stream strm = IStream::create( path->findByName("irq") );

  //  lVerbose = true;

  unsigned i=0;
  while(1) {
    write_reg(&strm, 4, 1<<i);
    unsigned v = read_reg(&strm, 4);
    if (v!=(1<<i))
      printf("payload: wrote %x, read %x\n",1<<i,v);

    write_reg(&strm, 8, (1<<i)-1);
    v = read_reg(&strm, 8);
    if (v!=((1<<i)-1))
      printf("scratch: wrote %x, read %x\n",(1<<i)-1,v);

    i++;
  }

}

void* dti_input(void* p)
{
  while(1) {
    unsigned addr, value;
    while (scanf("%x %x",&addr,&value)!=2)
      ;
    
    Pds::Cphw::Reg* r = reinterpret_cast<Pds::Cphw::Reg*>(addr);

    unsigned u = unsigned(*r);
    *r = value;
    unsigned v = unsigned(*r);

    printf("[%p] : %x -> %x\n", r, u,v);
  }

  return 0;
}

void configLinks()
{
  static unsigned chip_addr[]  = { 0x09000000,
                                   0x09010000,
                                   0x09020000,
                                   0x09030000,
                                   0x09040000,
                                   0x09050000 };
  static unsigned chan_addr[]  = { 0x34, 0x50, 0x6c, 0x88, 0xa8, 0xc4, 0xe0, 0xfc };
  static unsigned config[][6] = { { 0x2, 0x2C, 0x00, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x01, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x02, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x03, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x07, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x15, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x0B, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x0F, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x55, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x1F, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x2F, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x3F, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0xAA, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0x7F, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0xBF, 0xAD, 0x02, 0x00 },
                                  { 0x2, 0x2C, 0xFF, 0xAD, 0x02, 0x00 } };

  static unsigned iq = 0;

  for(unsigned i=0; i<6; i++) {
    { char* p = (char*)0 + chip_addr[i];
      Pds::Cphw::Reg* r = (Pds::Cphw::Reg*)p;
      r[6] = 0x18;   // Enable SMBUS control
      r[8] = 0x1C; } // SMBUS controls MODE, RXDET, IDLE
    for(unsigned j=0; j<8; j++) {
      char* p = (char*)0 + chip_addr[i] + chan_addr[j];
      Pds::Cphw::Reg* r = (Pds::Cphw::Reg*)p;
      for(unsigned k=0; k<6; k++)
        r[k] = config[iq][k];
      // r[0] = 0x02; // Force sigdet to ON
      // r[1] = 0x2C; // Force output ON, input ON
      // r[2] = 0x02; // Equalization control
      //      r[3] = 0xAD; // Mode select, VOD control
      //      r[4] = 0x02; // De-emphasis control
      //      r[5] = 0x00; // IDLE threshold
    }
  }

  printf("Config %u\n",iq);
  iq++;

  if (iq==16) iq=0;
}
