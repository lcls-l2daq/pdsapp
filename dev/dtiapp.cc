#include "pdsdata/xtc/DetInfo.hh"

#include "pds/config/CfgClientNfs.hh"
#include "pds/service/CmdLineTools.hh"
#include "pds/management/SegmentLevel.hh"
#include "pds/management/EventAppCallback.hh"
#include "pds/management/FastSegWire.hh"
#include "pds/service/Task.hh"
#include "pds/dti/Server.hh"
#include "pds/dti/Manager.hh"
#include "pds/dti/Module.hh"
#include "pds/cphw/AmcTiming.hh"

#include "cadef.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>

extern int optind;

using namespace Pds;
using namespace Pds::Dti;
using Pds::Cphw::XBar;

static unsigned usLinks;

static void sigHandler( int signal ) {
  Module* m = Module::locate();
  unsigned links = usLinks;
  for (unsigned i = 0; i < Module::NUsLinks; ++i)
  {
    if (links & (1<<i))
    {
      m->_usLinkConfig[i].enable(false);
      links &= ~(1<<i);
    }
  }
  ::exit(signal);
}

static void usage(const char *p)
{
  printf("Usage: %s -p <platform> -a <dti ip address> [-u <alias>] [-e <DTIs>]\n"
         "\n"
         "Options:\n"
         "\t -p <platform>          platform number\n"
         "\t -a <ip addr>           dti private ip address (dotted notation)\n"
         "\t -u <alias>             set device alias\n"
         "\t -l <Detectors>         bit list of upstream links to enable\n"
         "\t -h                     print this message and exit\n", p);
}

int main(int argc, char** argv) {

  // parse the command line for our boot parameters
  const uint32_t NO_PLATFORM = uint32_t(-1UL);
  uint32_t  platform  = NO_PLATFORM;
  const char* ip  = 0;
  bool      lUsage    = false;

  Pds::DetInfo info(getpid(),DetInfo::NoDetector,0,DetInfo::NoDevice,0);

  char* uniqueid = (char *)NULL;

  int c;
  while ( (c=getopt( argc, argv, "a:p:u:l:h")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg;
      break;
    case 'p':
      if (!CmdLineTools::parseUInt(optarg,platform)) {
        printf("%s: option `-p' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'u':
      if (strlen(optarg) > SrcAlias::AliasNameMax-1) {
        printf("Device alias '%s' exceeds %d chars, ignored\n", optarg, SrcAlias::AliasNameMax-1);
      } else {
        uniqueid = optarg;
      }
      break;
    case 'l':
      usLinks = strtoul(optarg, NULL, 0);
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    case '?':
    default:
      lUsage = true;
    }
  }

  if (platform==NO_PLATFORM) {
    printf("%s: platform required\n",argv[0]);
    lUsage = true;
  }

  if (optind < argc) {
    printf("%s: invalid argument -- %s\n",argv[0], argv[optind]);
    lUsage = true;
  }

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  Node node(Level::Segment, platform);
  printf("Using %s\n",Pds::DetInfo::name(info));

  Pds::Cphw::Reg::set(ip, 8192, 0);

  // Revisit: Perhaps the following Module accesses don't belong here
  Module* m = Module::locate();

  // Enable reception of timing from the backplane
  m->_timing.xbar.setOut( XBar::FPGA, XBar::BP );

  m->bpTxInterval(100);

  m->clearCounters();

  for (unsigned i = 0; i < 2; ++i)      // Revisit: 2 -> NUsLinks + NDsLinks
    m->_pgp[i].clearCounters();

  unsigned links = usLinks;
  for (unsigned i = 0; i < Module::NUsLinks; ++i)
  {
    if (links & (1<<i))
    {
      m->_usLinkConfig[i].fwdMask(1 << i); // Revisit: Allow forwarding to multiple DRPs
      m->_usLinkConfig[i].trigDelay(0);    // Revisit: Dial this in
      m->_usLinkConfig[i].enable(true);
      links &= ~(1<<i);
    }
  }

  m->updateCounters();

  m->init();

  Dti::Server* server  = new Dti::Server(*m, info);
  Manager*     manager = new Manager(*m, *server, *new CfgClientNfs(info));

//   //  EPICS thread initialization
//   SEVCHK ( ca_context_create(ca_enable_preemptive_callback ),
//            "control calling ca_context_create" );

  Task*             task = new Task(Task::MakeThisATask);
  EventAppCallback* seg  = new EventAppCallback(task, platform, manager->appliance());
  FastSegWire       settings(*server, -1, uniqueid, 1024);
  SegmentLevel*     seglevel = new SegmentLevel(platform, settings, *seg);
  seglevel->attach();

  ::signal( SIGINT, sigHandler );

  task->mainLoop();

  //  ca_context_destroy();

  return 0;
}
