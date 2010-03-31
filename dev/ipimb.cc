#include "pdsdata/xtc/DetInfo.hh"

#include "pds/management/SegmentLevel.hh"
#include "pds/management/EventCallback.hh"
#include "pds/collection/Arp.hh"

#include "pds/management/SegStreams.hh"
#include "pds/utility/SegWireSettings.hh"
#include "pds/utility/InletWire.hh"
#include "pds/service/Task.hh"
#include "pds/ipimb/IpimbManager.hh"
#include "pds/ipimb/IpimbServer.hh"
#include "pds/config/CfgClientNfs.hh"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// todo: make *list* of IpimbServers

namespace Pds {

  //
  //  This class creates the server when the streams are connected.
  //
  class MySegWire : public SegWireSettings {
  public:
    MySegWire(IpimbServer** ipimbServer, int nServers) : _ipimbServer(ipimbServer), _nServers(nServers) { 
      for (int i=0; i< _nServers; i++) {
	_sources.push_back(ipimbServer[i]->client()); 
      }
    }
    virtual ~MySegWire() {}
    void connect (InletWire& wire,
		  StreamParams::StreamType s,
		  int interface) {
      for (int i=0; i< _nServers; i++) {
	printf("Adding input of server %d, fd %d\n", i, _ipimbServer[i]->fd());
	wire.add_input(_ipimbServer[i]);
      }
    }
    const std::list<Src>& sources() const { return _sources; }
  private:
    IpimbServer** _ipimbServer;
    std::list<Src> _sources;
    const int _nServers;
  };

  //
  //  Implements the callbacks for attaching/dissolving.
  //  Appliances can be added to the stream here.
  //
  class Seg : public EventCallback {
  public:
    Seg(Task*                 task,
        unsigned              platform,
	CfgClientNfs**        cfgService,
        SegWireSettings&      settings,
        Arp*                  arp,
        IpimbServer**         ipimbServer,
	int nServers) :
      _task(task),
      _platform(platform),
      _cfg   (cfgService),
      _ipimbServer(ipimbServer),
      _nServers(nServers)

    {
    }

    virtual ~Seg()
    {
      _task->destroy();
    }
    
  private:
    // Implements EventCallback
    void attached(SetOfStreams& streams)
    {
      printf("Seg connected to platform 0x%x\n", 
	     _platform);
      
      Stream* frmk = streams.stream(StreamParams::FrameWork);
      IpimbManager& ipimbMgr = *new IpimbManager(_ipimbServer, _nServers, _cfg);//, _wire);
      ipimbMgr.appliance().connect(frmk->inlet());
    }
    void failed(Reason reason)
    {
      static const char* reasonname[] = { "platform unavailable", 
					  "crates unavailable", 
					  "fcpm unavailable" };
      printf("Seg: unable to allocate crates on platform 0x%x : %s\n", 
	     _platform, reasonname[reason]);
      delete this;
    }
    void dissolved(const Node& who)
    {
      const unsigned userlen = 12;
      char username[userlen];
      Node::user_name(who.uid(),username,userlen);
      
      const unsigned iplen = 64;
      char ipname[iplen];
      Node::ip_name(who.ip(),ipname, iplen);
      
      printf("Seg: platform 0x%x dissolved by user %s, pid %d, on node %s", 
	     who.platform(), username, who.pid(), ipname);
      
      delete this;
    }
    
  private:
    Task*         _task;
    unsigned      _platform;
    CfgClientNfs** _cfg;
    IpimbServer**  _ipimbServer;
    const int _nServers;
  };
}

using namespace Pds;

int main(int argc, char** argv) {

  // parse the command line for our boot parameters
  unsigned detid = -1UL;
  unsigned platform = 0;
  unsigned nboards = 1;
  Arp* arp = 0;

  extern char* optarg;
  int c;
  while ( (c=getopt( argc, argv, "a:i:p:n:C")) != EOF ) {
    switch(c) {
    case 'a':
      arp = new Arp(optarg);
      break;
    case 'i':
      detid  = strtoul(optarg, NULL, 0);
      break;
    case 'p':
      platform = strtoul(optarg, NULL, 0);
      break;
    case 'n':
      nboards = strtoul(optarg, NULL, 0);
      break;
    }
  }

  if ((!platform) || (detid == -1UL)) {
    printf("Platform and detid required\n");
    printf("Usage: %s -i <detid> -p <platform> [-a <arp process id>]\n", argv[0]);
    return 0;
  }

  // launch the SegmentLevel
  if (arp) {
    if (arp->error()) {
      char message[128];
      sprintf(message, "failed to create odfArp : %s", 
	      strerror(arp->error()));
      printf("%s %s\n",argv[0], message);
      delete arp;
      return 0;
    }
  }

  Node node(Level::Source,platform);

  Task* task = new Task(Task::MakeThisATask);
  
  //  const unsigned nServers = 2;
  const unsigned nServers = nboards; // compiler distinguished ** and *...[2] if 2 is const
  IpimbServer* ipimbServer[nServers];
  CfgClientNfs* cfgService[nServers];
  for (unsigned i=0; i<nServers; i++) {
    DetInfo detInfo(node.pid(), (Pds::DetInfo::Detector)detid, 0, DetInfo::Ipimb, i);
    cfgService[i] = new CfgClientNfs(detInfo);
    ipimbServer[i] = new IpimbServer(detInfo);
  }

  MySegWire settings(ipimbServer, nServers);
  Seg* seg = new Seg(task, platform, cfgService, settings, arp, ipimbServer, nServers);
  SegmentLevel* seglevel = new SegmentLevel(platform, settings, *seg, arp);
  seglevel->attach();

  printf("entering ipimb task main loop\n");
  task->mainLoop();
  printf("exiting ipimb task main loop\n");
  if (arp) delete arp;
  return 0;
}