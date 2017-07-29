#include "pdsdata/xtc/DetInfo.hh"

#include "pds/config/CfgClientNfs.hh"
#include "pds/service/CmdLineTools.hh"
#include "pds/management/SegmentLevel.hh"
#include "pds/management/EventAppCallback.hh"
#include "pds/management/FastSegWire.hh"
#include "pds/service/Task.hh"
#include "pds/xpm/Server.hh"
#include "pds/xpm/Manager.hh"
#include "pds/xpm/Module.hh"
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
using namespace Pds::Xpm;
using Pds::Cphw::XBar;

static void sigHandler( int signal ) {
  Module* m = new(0) Module;
  m->setL0Enabled(false);
  ::exit(signal);
}

static void usage(const char *p)
{
  printf("Usage: %s -p <platform> -a <xpm ip address> [-u <alias>] [-e <DTIs>]\n"
         "\n"
         "Options:\n"
         "\t -p <platform>          platform number\n"
         "\t -a <ip addr>           xpm private ip address (dotted notation)\n"
         "\t -u <alias>             set device alias\n"
         "\t -e <DTIs>              bit list of DTIs to enable\n"
         "\t -h                     print this message and exit\n", p);
}

int main(int argc, char** argv) {

  // parse the command line for our boot parameters
  const uint32_t NO_PLATFORM = uint32_t(-1UL);
  uint32_t  platform  = NO_PLATFORM;
  const char* xpm_ip  = 0;
  bool      lUsage    = false;
  unsigned  dtiEnables = 0;

  Pds::DetInfo info(getpid(),DetInfo::NoDetector,0,DetInfo::Evr,0);

  char* uniqueid = (char *)NULL;

  int c;
  while ( (c=getopt( argc, argv, "a:p:u:F:e:h")) != EOF ) {
    switch(c) {
    case 'a':
      xpm_ip = optarg;
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
    case 'e':
      dtiEnables = strtoul(optarg, NULL, 0);
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

  Pds::Cphw::Reg::set(xpm_ip, 8192, 0);

  Module* m = Module::locate();

  // Drive backplane
  m->_timing.xbar.setOut( XBar::BP, XBar::FPGA );

  // BP broadcast
  m->linkEnable(16, true);

  // DTIs' BP channels
  for (unsigned i = 0; i < 14; ++i)     // 16 slots max - 2 hub slots (switch, XPM)
  {
    m->linkEnable(17 + i, (dtiEnables >> i) & 1);
  }

  m->setL0Enabled(false);

  Xpm::Server* server  = new Xpm::Server(*m, info);
  Manager*     manager = new Manager(*m, *server,
                                     *new CfgClientNfs(info));

//   //  EPICS thread initialization
//   SEVCHK ( ca_context_create(ca_enable_preemptive_callback ),
//            "control calling ca_context_create" );

  Task* task = new Task(Task::MakeThisATask);
  EventAppCallback* seg = new EventAppCallback(task, platform, manager->appliance());
  FastSegWire settings(*server, -1, uniqueid, 1024);
  SegmentLevel* seglevel = new SegmentLevel(platform, settings, *seg);
  seglevel->attach();

  ::signal( SIGINT, sigHandler );

  task->mainLoop();

  //  ca_context_destroy();

  return 0;
}
