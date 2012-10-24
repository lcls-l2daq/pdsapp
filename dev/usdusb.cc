#include "pdsdata/xtc/DetInfo.hh"

#include "pds/management/SegmentLevel.hh"
#include "pds/management/EventCallback.hh"
#include "pds/collection/Arp.hh"

#include "pds/management/SegStreams.hh"
#include "pds/utility/SegWireSettings.hh"
#include "pds/utility/InletWire.hh"
#include "pds/service/Task.hh"
#include "pds/usdusb/Manager.hh"
#include "pds/usdusb/Server.hh"
#include "pds/config/CfgClientNfs.hh"
#include "usdusb4/include/libusdusb4.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "libusb.h"

#include <list>

static void reset_usb()
{
  libusb_context* pctx;

  libusb_init(&pctx);

  const int vid = 0x09c9;
  const int pid = 0x0044;

  libusb_device_handle* phdl = libusb_open_device_with_vid_pid(pctx, vid, pid);
  libusb_reset_device(phdl);

  libusb_exit(pctx);
}

namespace Pds {

  //
  //  This class creates the server when the streams are connected.
  //  Real implementations will have something like this.
  //
  class MySegWire : public SegWireSettings {
  public:
    MySegWire(std::list<UsdUsb::Server*>& servers) : _servers(servers) 
    {
      for(std::list<UsdUsb::Server*>::iterator it=servers.begin(); it!=servers.end(); it++)
	_sources.push_back((*it)->client()); 
    }
    virtual ~MySegWire() {}
    void connect (InletWire& wire,
		  StreamParams::StreamType s,
		  int interface) {
      for(std::list<UsdUsb::Server*>::iterator it=_servers.begin(); it!=_servers.end(); it++)
	wire.add_input(*it);
    }
    const std::list<Src>& sources() const { return _sources; }
  private:
    std::list<UsdUsb::Server*>& _servers;
    std::list<Src>              _sources;
  };

  //
  //  Implements the callbacks for attaching/dissolving.
  //  Appliances can be added to the stream here.
  //
  class Seg : public EventCallback {
  public:
    Seg(Task*                     task,
        unsigned                  platform,
        SegWireSettings&          settings,
        Arp*                      arp,
        std::list<UsdUsb::Manager*>& managers) :
      _task      (task),
      _platform  (platform),
      _managers  (managers)
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
      printf("Seg connected to platform 0x%x\n",_platform);
      
      Stream* frmk = streams.stream(StreamParams::FrameWork);
      for(std::list<UsdUsb::Manager*>::iterator it=_managers.begin(); it!=_managers.end(); it++)
	(*it)->appliance().connect(frmk->inlet());
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
    Task*                        _task;
    unsigned                     _platform;
    std::list<UsdUsb::Manager*>& _managers;
  };
}

using namespace Pds;

static void usage(const char* p)
{
  printf("Usage: %s -i <detector> -d <device> -p <platform> [-z]\n"
	 "  -z zeroes encoder counts\n", p);
}

int main(int argc, char** argv) {

  // parse the command line for our boot parameters
  unsigned detid = -1UL;
  unsigned devid = 0;
  unsigned platform = -1UL;
  bool lzero = false;

  extern char* optarg;
  int c;
  while ( (c=getopt( argc, argv, "i:d:p:z")) != EOF ) {
    switch(c) {
    case 'i':
      detid  = strtoul(optarg, NULL, 0);
      break;
    case 'd':
      devid  = strtoul(optarg, NULL, 0);
      break;
    case 'p':
      platform = strtoul(optarg, NULL, 0);
      break;
    case 'z':
      lzero = true;
      break;
    default:
      usage(argv[0]);
      return 1;
    }
  }

  //
  //  There must be a way to detect multiple instruments, but I don't know it yet
  //
  reset_usb();

  short deviceCount = 0;
  int result = USB4_Initialize(&deviceCount);
  if (result != USB4_SUCCESS) {
    printf("Failed to initialize USB4 driver (%d)\n",result);
    USB4_Shutdown();
    return 1;
  }

  printf("Found %d devices\n", deviceCount);

  if (lzero) {
    for(unsigned i=0; i<UsdUsbConfigType::NCHANNELS; i++) {
      if ((result = USB4_SetPresetValue(deviceCount, i, 0)) != USB4_SUCCESS)
	printf("Failed to set preset value for channel %d : %d\n",i, result);
      if ((result = USB4_ResetCount(deviceCount, i)) != USB4_SUCCESS)
	printf("Failed to set preset value for channel %d : %d\n",i, result);
    }
    USB4_Shutdown();
    return 1;
  }

  if ((platform == -1UL) || (detid == -1UL)) {
    printf("Platform and detid required\n");
    printf("Usage: %s -i <detid> -p <platform> [-a <arp process id>]\n", argv[0]);
    USB4_Shutdown();
    return 0;
  }

  Node node(Level::Source,platform);

  std::list<UsdUsb::Server*>  servers;
  std::list<UsdUsb::Manager*> managers;

  DetInfo detInfo(node.pid(), (Pds::DetInfo::Detector)detid, 0, DetInfo::USDUSB, devid);
  UsdUsb::Server* srv = new UsdUsb::Server(detInfo);
  servers   .push_back(srv);
  managers.push_back(new UsdUsb::Manager(0, *srv, *new CfgClientNfs(detInfo)));

  Task* task = new Task(Task::MakeThisATask);
  MySegWire settings(servers);
  Seg* seg = new Seg(task, platform, settings, 0, managers);
  SegmentLevel* seglevel = new SegmentLevel(platform, settings, *seg, 0);
  seglevel->attach();

  task->mainLoop();

  return 0;
}