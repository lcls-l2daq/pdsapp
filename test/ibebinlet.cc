#include "pds/ibeb/RdmaEvent.hh"
#include "pds/ibeb/RdmaRdPort.hh"
#include "pds/xtc/Datagram.hh"

#include "pdsdata/psddl/smldata.ddl.h"
#include "pdsdata/psddl/generic1d.ddl.h"

#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <inttypes.h>

typedef Pds::SmlData::ProxyV1 SmlD;
typedef Pds::Generic1D::DataV0 GenD;

static const unsigned max_payload_size = 0x400000;

static void dump(const Pds::Datagram* dg)
{
  char buff[128];
  time_t t = dg->seq.clock().seconds();
  strftime(buff,128,"%H:%M:%S",localtime(&t));
  printf("%s.%09u %016lx %s extent 0x%x ctns %s damage %x\n",
         buff,
         dg->seq.clock().nanoseconds(),
         dg->seq.stamp().pulseID(),
         Pds::TransitionId::name(dg->seq.service()),
         dg->xtc.extent, 
         Pds::TypeId::name(dg->xtc.contains.id()),
         dg->xtc.damage.value());
}

using namespace Pds;

static void show_usage(const char* p)
{
  printf("Usage: %s -a <Server IP address: dotted notation> [-v]\n",p);
}

void sigHandler( int signal ) {
  ::exit(signal);
}

void* statusThread(void*);
  
int main(int argc, char* argv[])
{
  unsigned    nbuff = 12;
  unsigned    id    = 0;
  unsigned    maxEventSize = 4096;
  std::vector<std::string> addr;

  int c;
  while ( (c=getopt( argc, argv, "a:n:i:s:hv")) != EOF ) {
    switch(c) {
    case 'a':
      addr.push_back(std::string(optarg));
      break;
    case 'i':
      id   = atoi(optarg);
      break;
    case 'n':
      nbuff = atoi(optarg);
      break;
    case 's':
      maxEventSize = strtoul(optarg,NULL,0);
      break;
    case 'h':
      show_usage(argv[0]);
      return 0;
    case 'v':
      Pds::IbEb::RdmaRdPort::verbose(true);
      break;
    default:
      printf("Unknown option: -%c\n",c);
      return -1;
    }
  }

  ::signal( SIGINT, sigHandler );

  Pds::IbEb::RdmaEvent* rdma = new Pds::IbEb::RdmaEvent(nbuff,
                                                        maxEventSize,
                                                        id,
                                                        addr);

  pthread_attr_t tattr;
  pthread_attr_init(&tattr);
  pthread_t thr;
  if (pthread_create(&thr, &tattr, &statusThread, rdma))
    perror("Error creating RDMA status thread");


  //
  //  Poll the write completions until an event is complete
  //  Once complete launch the reads
  //  Poll the read completions and ack
  //
  const int MAX_WC=32;
  ibv_wc wc[MAX_WC];
  while(1) {
    int n = ibv_poll_cq(rdma->cq(), MAX_WC, wc);
    if (n<0) continue;
    for(int i=0; i<n; i++) {
      if (wc[i].status!=IBV_WC_SUCCESS) {
        printf("wc error %x  opcode %x  wr_id %llx\n",
               wc[i].status,wc[i].opcode,(unsigned long long)wc[i].wr_id);
        abort();
        continue;
      }
      rdma->complete(wc[i]);
    }
  }

  return 1;
}

void* statusThread(void* p)
{
  Pds::IbEb::RdmaEvent* rdma = (Pds::IbEb::RdmaEvent*)p;
  uint64_t ncomplete =0;
  uint64_t ncorrupt  =0;
  uint64_t nbytespush=0;
  uint64_t nbytespull=0;
  while(1) {
    usleep(1000000);
    uint64_t complete  = rdma->ncomplete();
    uint64_t corrupt   = rdma->ncorrupt ();
    uint64_t bytespush = rdma->nbytespush();
    uint64_t bytespull = rdma->nbytespull();
    printf("RDMA complete %llu  push %llu  pull %llu  corrupt %llu\n", 
           (unsigned long long)(complete-ncomplete),
           (unsigned long long)(bytespush-nbytespush),
           (unsigned long long)(bytespull-nbytespull),
           (unsigned long long)(corrupt-ncorrupt));
    ncomplete = complete;
    ncorrupt  = corrupt;
    nbytespush = bytespush;
    nbytespull = bytespull;
  }
  return 0;
}
