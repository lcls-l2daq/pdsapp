/********************************************************************
 *  Program to test an Infiniband outlet wire from a segment level  *
 *
 *  1)  Open IB device
 *  2)  Register datagram pool memory regions with IB device
 *  3)  Open TCP connection to each event level
 *      - exchange IB context
 *  4)  Write Dg header and addr to TCP connection
 *  5)  Listen for addr to return datagram pool memory
 ********************************************************************/

#include "ibcommonw.hh"
#include "pds/xtc/Datagram.hh"

#include "pds/service/GenericPool.hh"
#include "pds/service/RingPool.hh"

#include "pdsdata/psddl/smldata.ddl.h"
#include "pdsdata/psddl/generic1d.ddl.h"

#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>

typedef Pds::SmlData::ProxyV1 SmlD;
typedef Pds::Generic1D::DataV0 GenD;

static bool lverbose = false;

static const unsigned max_payload_size = 0x400000;

static const unsigned short outlet_port = 11000;

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

using Pds::IbW::RdmaComplete;

namespace Pds {
  class RdmaEvent : public IbW::Rdma {
  public:
    RdmaEvent(GenericPool*              pushPool,
              RingPool*                 readPool,
              unsigned                  id,
              const Ins&                remote) :
      Rdma(reinterpret_cast<const char*>(pushPool->buffer()),
           pushPool->size  (),
           remote,
           id),
      _id   (id),
      _pool (pushPool),
      _laddr(pushPool->numberofObjects()),
      _rimm (pushPool->numberofObjects()),
      _wr   (pushPool->numberofObjects()),
      _rpool(readPool),
      _rmr  (reg_mr(readPool->buffer(),
                    readPool->size  ())),
      _swr  (pushPool->numberofObjects()),
      _swrid(0),
      _pid       (0),
      _ncorrupt  (0),
      _nbytespush(0),
      _nbytespull(0)
    {
      GenericPool& pool = *pushPool;
      unsigned nmem = pool.numberofObjects();
      for(unsigned i=0; i<nmem; i++)
        _laddr[i] = (char*)pool.alloc(pool.sizeofObject());

      //  Send indexed list of target addresses to master
      iovec iov[2];
      iov[0].iov_base = &nmem;
      iov[0].iov_len  = sizeof(unsigned);
      iov[1].iov_base = _laddr.data();
      iov[1].iov_len  = _laddr.size()*sizeof(char*);
      ::writev(_fd,iov,2);

      //  Work requests for receiving RDMA_WRITE_WITH_IMM
      for(unsigned i=0; i<_wr.size(); i++) {
        ibv_sge &sge = *new ibv_sge;
        memset(&sge, 0, sizeof(sge));
        sge.addr   = (uintptr_t)new unsigned;
        sge.length =  sizeof(unsigned);
        sge.lkey   = _mr->lkey;
        
        ibv_recv_wr &sr = _wr[i];
        memset(&sr,0,sizeof(sr));
        sr.next    = NULL;
        // stuff index into upper 32 bits
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.wr_id   = i;
        
        ibv_recv_wr* bad_wr=NULL;
        if (ibv_post_recv(_qp, &sr, &bad_wr))
          perror("Failed to post SR");
        if (bad_wr)
          perror("ibv_post_recv bad_wr");
      }    

      //  Work requests for RDMA_READ
      for(unsigned i=0; i<_swr.size(); i++) {
        ibv_sge &sge = *new ibv_sge;
        memset(&sge, 0, sizeof(sge));
        sge.lkey     = _rmr->lkey;
        
        ibv_send_wr &sr = _swr[i];
        memset(&sr,0,sizeof(sr));
        sr.next       = NULL;
        sr.opcode     = IBV_WR_RDMA_READ;
        sr.send_flags = IBV_SEND_SIGNALED;
        // sr.send_flags = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.wr.rdma.rkey = _rkey;
      }    
    }

    void poll() {}

    const Datagram* datagram(unsigned index) 
    {
      unsigned idx = index>>16;

      ibv_recv_wr* bad_wr=NULL;
      if (ibv_post_recv(_qp, &_wr[idx], &bad_wr))
        perror("Failed to post SR");
      if (bad_wr)
        perror("ibv_post_recv bad_wr");

      const Datagram* dg = reinterpret_cast<const Datagram*>(_laddr[idx]);
      /*
      if (dg->seq.stamp().pulseID()!=_pid) {
        printf("Unexpected pulseID %llx/%llx\n",
               dg->seq.stamp().pulseID(),_pid);
        dump(dg);
        _ncorrupt++;
      }
      _pid = dg->seq.stamp().pulseID()+1;
      */

      _nbytespush += sizeof(*dg)+dg->xtc.sizeofPayload();
      return dg;
    }
    void req_read(unsigned    index,
                  const SmlD& proxy)
    {
      //  launch dma
      ibv_send_wr& sr = _swr[_swrid%_swr.size()];
      ibv_sge& sge = *sr.sg_list;
      sge.addr   = (uintptr_t)_rpool->alloc(proxy.extent());
      sge.length =  proxy.extent();
      _rpool->shrink((void*)sge.addr,proxy.extent());

      unsigned idx = index;
      // stuff index into upper 32 bits
      sr.wr_id   = (uint64_t(idx)<<32) | _swrid;
      sr.imm_data= idx;
      sr.wr.rdma.remote_addr = proxy.fileOffset();

      _swrid++;

      if (lverbose) {
        printf("req_read %x\n",index);
        _rpool->dump(0);
      }

      ibv_send_wr* bad_wr=NULL;
      if (ibv_post_send(_qp, &sr, &bad_wr))
        perror("Failed to post SR (read)");
      if (bad_wr)
        perror("ibv_post_send bad_wr (read)");
    }
    void complete(ibv_wc& wc)
    {
      unsigned iswr = (wc.wr_id&0xffffffff)%_swr.size();
      RingPool::free((char*)_swr[iswr].sg_list->addr);
      _nbytespull += _swr[iswr].sg_list->length;

      RdmaComplete cmpl;
      cmpl._dst    = _id;
      cmpl._dstIdx = wc.wr_id>>48;
      cmpl._dg     = (char*)_swr[iswr].wr.rdma.remote_addr;
      ::write(fd(),&cmpl,sizeof(cmpl));

      if (lverbose) {
        printf("rdma read complete id %016llx\n",
               wc.wr_id);
        _rpool->dump(0);
      }
    }
    uint64_t ncomplete () const { return _swrid; }
    uint64_t ncorrupt  () const { return _ncorrupt; }
    uint64_t nbytespush() const { return _nbytespush; }
    uint64_t nbytespull() const { return _nbytespull; }
  private:
    unsigned                 _id;
    GenericPool*             _pool;
    std::vector<char*>       _laddr;
    std::vector<uint32_t*>   _rimm;
    std::vector<ibv_recv_wr> _wr;
    RingPool*                _rpool;
    ibv_mr*                  _rmr;
    std::vector<ibv_send_wr> _swr;
    unsigned                 _swrid;
    uint64_t                 _pid;
    uint64_t                 _ncorrupt;
    uint64_t                 _nbytespush;
    uint64_t                 _nbytespull;
  };
};

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
  const char* addr = "192.168.0.1";
  unsigned    nbuff = 12;
  unsigned    id    = 0;

  int c;
  while ( (c=getopt( argc, argv, "a:n:i:hv")) != EOF ) {
    switch(c) {
    case 'a':
      addr = optarg;
      break;
    case 'i':
      id   = atoi(optarg);
      break;
    case 'n':
      nbuff = atoi(optarg);
      break;
    case 'h':
      show_usage(argv[0]);
      return 0;
    case 'v':
      lverbose = true;
      break;
    default:
      printf("Unknown option: -%c\n",c);
      return -1;
    }
  }

  ::signal( SIGINT, sigHandler );

  in_addr ia;
  inet_aton(addr,&ia);

  //  Memory region for RDMA_WRITE
  const int MAX_PUSH_SZ=512;
  GenericPool* pushPool = new GenericPool(MAX_PUSH_SZ, nbuff);

  //  Memory region for RDMA_READ
  const int MAX_EVENT_SZ=16*1024;
  RingPool* pool = new RingPool(nbuff*MAX_EVENT_SZ, MAX_EVENT_SZ);

  RdmaEvent* rdma = new RdmaEvent(pushPool,
                                  pool,
                                  id,
                                  Ins(ntohl(ia.s_addr),outlet_port));


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
               wc[i].status,wc[i].opcode,wc[i].wr_id);
        abort();
        //        ibv_perror(wc[i].status);
        continue;
      }
      if (wc[i].opcode==IBV_WC_RECV_RDMA_WITH_IMM) {
        //  immediate data indicates location
        const Datagram* dg = rdma->datagram(wc[i].imm_data);
        if (lverbose) {
          printf("rdma write complete id %llx  imm %08x\n",
                 wc[i].wr_id,
                 wc[i].imm_data);
          dump(dg);
        }

        //  Allocate a buffer to read big data
        const SmlD& proxy = *reinterpret_cast<const SmlD*>(dg->xtc.payload());
        rdma->req_read(wc[i].imm_data,
                       proxy);
        continue;
      }
      if (wc[i].opcode==IBV_WC_RDMA_READ) {
        rdma->complete(wc[i]);
        continue;
      }
      if (lverbose)
        printf("rdma completion opcode 0x%x\n",wc[i].opcode);
    }
  }

  return 1;
}

void* statusThread(void* p)
{
  RdmaEvent* rdma = (RdmaEvent*)p;
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
           complete-ncomplete,
           bytespush-nbytespush,
           bytespull-nbytespull,
           corrupt-ncorrupt);
    ncomplete = complete;
    ncorrupt  = corrupt;
    nbytespush = bytespush;
    nbytespull = bytespull;
  }
  return 0;
}
