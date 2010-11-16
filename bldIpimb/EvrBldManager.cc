//#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>

#include "EvrBldManager.hh" 
#include "BldHeader.hh"

#include "pds/client/Fsm.hh"  
#include "pds/client/Action.hh" 
#include "pds/xtc/CDatagram.hh" 
#include "pds/xtc/XtcType.hh"
#include "pdsdata/xtc/Src.hh"
#include "pdsdata/xtc/TypeId.hh"
#include "pdsdata/xtc/BldInfo.hh"
#include "pdsdata/xtc/XtcIterator.hh"
#include "pdsdata/bld/bldData.hh"
#include "pds/service/GenericPoolW.hh"

#define PULSE_DELAY           0.000769992   
#define PULSE_WIDTH           100.0e-9     
#define BLD_HDR_SOURCE2_BASE  (uint32_t) Pds::BldInfo::Ipimb
#define BLD_HDR_CONTAINS      (uint32_t) Pds::TypeId::Id_Ipimb

using namespace Pds;

static EvrBldManager* evrBldMgrGlobal = NULL;

//EVR Signal Handler
extern "C" {
  void evrsa_sig_handler(int parm) { 
  if(evrBldMgrGlobal)	  
    evrBldMgrGlobal->handleEvrIrq();  
  }
} 


class EvrBldConfigAction : public Action {
public:
  EvrBldConfigAction(EvrBldManager* evrBldMgr,CfgClientNfs** cfgIpimb,
                     unsigned nIpimbBoards,IpimbConfigType* ipimbConfig):
                     _evrBldMgr(evrBldMgr),_cfgIpimb(cfgIpimb),
                     _ipimbConfig(ipimbConfig),_nIpimbBoards(nIpimbBoards) { }
	
  Transition* fire(Transition* tr) {   	  
    for (unsigned i=0; i<_nIpimbBoards; i++) { //capturing IPIMB configuration
      int len = (*_cfgIpimb[i]).fetch(*tr,_ipimbConfigType, &_ipimbConfig[i],sizeof(_ipimbConfig[i]));
      if (len <= 0) {
        printf("*** EvrBldConfigAction::fire(tr):failed to capture IPIMB configuration for %dth board: (%d)%s \n",
               i,errno,strerror(errno));
        continue;
      }
    }
    _evrBldMgr->configure(); 
    return tr;
  }

private:
  EvrBldManager* _evrBldMgr;
  CfgClientNfs** _cfgIpimb;
  IpimbConfigType* _ipimbConfig;
  unsigned _nIpimbBoards;
  
};

 
class EvrBldL1Action : public Action, public XtcIterator {
public:
  enum {Stop, Continue};
  EvrBldL1Action(EvrBldManager* evrBldMgr, CfgClientNfs** cfgIpimb, IpimbConfigType* ipimbConfig, 
                 unsigned nIpimbBoards,unsigned boardPayloadSize,unsigned* interfaceOffset):
                 _evrBldMgr(evrBldMgr), _pool((nIpimbBoards*boardPayloadSize)+sizeof(InDatagram),10), 
                 _boardPayloadSize(boardPayloadSize),_cfgIpimb(cfgIpimb),_ipimbConfig(ipimbConfig),
                 _interfaceOffset(interfaceOffset),_nIpimbBoards(nIpimbBoards), _nIpimbData(0),_nEvrData(0) {
					 
    unsigned payloadSize = _boardPayloadSize*_nIpimbBoards;
    _payload = new char [payloadSize];
    memset((void*) _payload,0,payloadSize);
    for (unsigned i=0; i<BLD_IPIMB_DEVICES; i++) 
      _bldHeader[i] = 0;
    for (unsigned i=0; i<_nIpimbBoards; i++)
      _bldHeader[i] = new BldHeader((BLD_HDR_SOURCE2_BASE+ *(_interfaceOffset+i)),BLD_HDR_CONTAINS,sizeof(BldDataIpimb));	
  }
  
  Transition* fire(Transition* tr) { return tr; }

  InDatagram* fire(InDatagram* in) {
    _datagram = new(&_pool)CDatagram(*in);
	Datagram& dg = in->datagram();
    _nIpimbData	= 0; _nEvrData = 0;
	
    if(dg.xtc.contains.id() == TypeId::Id_Xtc) {  
      iterate(& dg.xtc);
	} else {
      printf("*** EvrBldL1Action::fire(In): Id_Xtc does not exist in L1 Dg \n");
      return in;
	}
	
    if(_nIpimbData != _nIpimbBoards)
      printf("*** EvrBldL1Action::fire(In): Received %u boards data segments. Expected %u \n",_nIpimbData,_nIpimbBoards);
    if(_nEvrData != 1)	
      printf("*** EvrBldL1Action::fire(In): Received %u contributions from EVR. Expected: 1 \n",_nEvrData);	
	  
    for(unsigned i=0; i < _nIpimbBoards; i++) {
	  Xtc* ipimbXtc = reinterpret_cast<Pds::Xtc*>(_payload + i*_boardPayloadSize);	
	  _datagram->insert(*ipimbXtc, (void*) ipimbXtc->payload());
    }

    return _datagram;	  
  }
  
  
  int process(Xtc* xtc) {

    if(xtc->contains.id() == TypeId::Id_EvrData) { 		
      EvrDatagram* evrDatagram =  (EvrDatagram*) xtc->payload();
      for (unsigned i=0; i< _nIpimbBoards; i++) 
        _bldHeader[i]->timeStamp(evrDatagram->seq.clock().seconds(),evrDatagram->seq.clock().nanoseconds(),evrDatagram->seq.stamp().fiducials());
      _nEvrData++;		
      // printf("*** EvrBldL1Action : process : Id_EvrData  fid = %x \n",evrDatagram->seq.stamp().fiducials());
    } else if(xtc->contains.id() == TypeId::Id_IpimbData)  {
      unsigned i=0;
      for(i=0; i < _nIpimbBoards; i++) {
        if (xtc->src.phy() == (*_cfgIpimb[i]).src().phy()) {  //find match of Detector & Device
          char* p = _payload+(i*_boardPayloadSize);	
          Xtc& ipimbXtc = *new(p) Xtc(TypeId(TypeId::Id_Ipimb,(uint32_t)BldDataIpimb::version), xtc->src);
          ipimbXtc.extent = _boardPayloadSize;
          _bldHeader[i]->setDamage(0);  //set damage for individual board based on data
          memcpy(p+sizeof(Xtc),(char*)_bldHeader[i], sizeof(BldHeader));
          memcpy(p+sizeof(Xtc)+sizeof(BldHeader) ,(char*) xtc->payload(), xtc->sizeofPayload());
          memcpy(p+sizeof(Xtc)+sizeof(BldHeader)+sizeof(IpimbDataType),(char*) &_ipimbConfig[i], sizeof(IpimbConfigType));	
          memcpy(p+sizeof(Xtc)+sizeof(BldHeader)+sizeof(IpimbDataType)+sizeof(IpimbConfigType),
          (char*) reinterpret_cast<Pds::DetInfo*>(&(xtc->src)), sizeof(Pds::DetInfo));
          _nIpimbData++;				 
        }
      }
    } else	
      printf("*** EvrBldL1Action::process(): Unknown XTC Type xtcType=%s \n",Pds::TypeId::name(xtc->contains.id()));

    return Continue;
  }  
 
private:
  EvrBldManager* _evrBldMgr;
  GenericPoolW  _pool;
  CDatagram* _datagram;
  BldHeader* _bldHeader[BLD_IPIMB_DEVICES];
  unsigned  _boardPayloadSize;
  char* _payload;
  unsigned _payloadIndex;
  CfgClientNfs** _cfgIpimb;
  IpimbConfigType* _ipimbConfig;
  unsigned* _interfaceOffset;	
  unsigned _nIpimbBoards;
  unsigned _nIpimbData;
  unsigned _nEvrData; 	
};

class EvrBldEnableAction : public Action {

public:
  EvrBldEnableAction(EvrBldManager* evrBldMgr): _evrBldMgr(evrBldMgr) { }
 
  Transition* fire(Transition* tr) {
    printf("EvrBldEnableAction(): Starting EVR \n");
    _evrBldMgr->reset();
    _evrBldMgr->start();
    return tr;
  }
  
private:
  EvrBldManager* _evrBldMgr;  
};

class EvrBldDisableAction : public Action {

public:
  EvrBldDisableAction(EvrBldManager* evrBldMgr): _evrBldMgr(evrBldMgr) { }

  Transition* fire(Transition* tr) {
    printf("EvrBldDisableAction(): Stopping EVR \n");
    _evrBldMgr->stop();
    return tr;
  }
  
private:
  EvrBldManager* _evrBldMgr;  
};



class EvrBldBeginCalibAction : public Action {

public:
  EvrBldBeginCalibAction(EvrBldManager* evrBldMgr): _evrBldMgr(evrBldMgr) { }
 
  Transition* fire(Transition* tr) {
    printf("EvrBldBeginCalibAction(): Enable EVR \n");
    _evrBldMgr->enable();
    return tr;
  }
  
private:
  EvrBldManager* _evrBldMgr;  
};

class EvrBldEndCalibAction : public Action {

public:
  EvrBldEndCalibAction(EvrBldManager* evrBldMgr): _evrBldMgr(evrBldMgr) { }
 
  Transition* fire(Transition* tr) {
    printf("EvrBldEndCalibAction(): Disable EVR \n");
    _evrBldMgr->disable();
    return tr;
  }
  
private:
  EvrBldManager* _evrBldMgr;  
};

EvrBldManager::EvrBldManager(EvgrBoardInfo<Evr> &erInfo, unsigned opcode, EvrBldServer& evrBldServer, 
                             CfgClientNfs** cfgIpimb, unsigned nIpimbBoards, unsigned* interfaceOffset) :
                             _er(erInfo.board()), _opcode(opcode),_evrBldServer(evrBldServer),
                             _erInfo(&erInfo), _evtCounter(0),_fsm(*new Fsm),
                             _ipimbConfig(new IpimbConfigType[nIpimbBoards]) {
								 
  _er.IrqAssignHandler(erInfo.filedes(), &evrsa_sig_handler);  
  evrBldMgrGlobal = this;
  
  unsigned boardPayloadSize = sizeof(BldHeader)+sizeof(BldDataIpimb)+sizeof(Xtc); 
  _fsm.callback(TransitionId::Configure,new EvrBldConfigAction(this,cfgIpimb,nIpimbBoards,_ipimbConfig));
  _fsm.callback(TransitionId::BeginCalibCycle, new EvrBldBeginCalibAction(this));
  _fsm.callback(TransitionId::Enable,   new EvrBldEnableAction(this));
  _fsm.callback(TransitionId::L1Accept, new EvrBldL1Action(this,cfgIpimb,_ipimbConfig,nIpimbBoards,boardPayloadSize,interfaceOffset));
  _fsm.callback(TransitionId::Disable,  new EvrBldDisableAction(this));
  _fsm.callback(TransitionId::EndCalibCycle  , new EvrBldEndCalibAction  (this));  
   
}

void EvrBldManager::start() {
  unsigned ram = 0;
  _er.MapRamEnable(ram, 1);
};

void EvrBldManager::stop() { 
  // switch to the "dummy" map ram 
  unsigned dummyram=1;
  _er.MapRamEnable(dummyram,1);
  _er.ClearFIFO();  
}

void EvrBldManager::enable() {
  int test = _er.IrqEnable(EVR_IRQ_MASTER_ENABLE | EVR_IRQFLAG_EVENT);
  printf("### Enabled EVR IRQ Flags 0x%x\n",test);
  _er.EnableFIFO(1);
  _er.Enable(1);
}

void EvrBldManager::disable() {
  _er.IrqEnable(0);
  _er.Enable(0);
  _er.EnableFIFO(0);
}

void EvrBldManager::reset() { _evtCounter = 0; }

static long long unsigned nSecPrev = 0;  

void EvrBldManager::handleEvrIrq() {

  int flags = _er.GetIrqFlags();
  if (flags & EVR_IRQFLAG_EVENT) {
    _er.ClearIrqFlags(EVR_IRQFLAG_EVENT);
    if(flags & EVR_IRQFLAG_FIFOFULL) {
      printf("*** Received: EVENT & FIFO_FULL IRQ FLAG : 0x%x \n",flags);
      _er.ClearIrqFlags(EVR_IRQFLAG_FIFOFULL);	  
    }
    FIFOEvent fe;
    _er.GetFIFOEvent(&fe);
 
    if ( fe.EventCode != _opcode ) 
      printf("*** Received Diff. Event code %03d @ evtCount = %u \n", fe.EventCode,_evtCounter);
    else {
      timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts); 
      long long unsigned nSecCurr = (long long unsigned ) ( (ts.tv_sec* 1.0e9) + ts.tv_nsec );
      if((_evtCounter % 1200) == 0) 
        printf("Received IrqFlags = 0x%x Fiducial %06x  Event code %03d  Timestamp %d Tdiff= %f events = %u  \n",
                flags,fe.TimestampHigh, fe.EventCode, fe.TimestampLow,((double)(nSecCurr - nSecPrev)/1.0e6),_evtCounter);
      ClockTime ctime(ts.tv_sec, ts.tv_nsec);
      TimeStamp stamp(fe.TimestampLow, fe.TimestampHigh, _evtCounter);
      Sequence seq(Sequence::Event, TransitionId::L1Accept, ctime, stamp);
      EvrDatagram datagram(seq, _evtCounter++);	
      _evrBldServer.sendEvrEvent(&datagram);  //write to EVR server fd here
      nSecPrev = nSecCurr;
      //printf ("PidNo = %ld TidNo = %ld \n" ,getpid(),syscall(224));	
    } 
  } else { 
    printf("*** Spurious interrupt, IRQ flags = 0x%x \n",flags);  
  } 

    int fdEr = _erInfo->filedes();
    _er.IrqHandled(fdEr); 
}

void EvrBldManager::configure() {
 
  printf("Configuring Evr\n");   
  _er.Reset();
  // Problem in Reset() function: It doesn't reset the set and clear masks 
  // workaround: manually call the clear function to set and clear all masks
  for (unsigned ram=0;ram<2;ram++) {
    for (unsigned iopcode=0;iopcode<=EVR_MAX_EVENT_CODE;iopcode++) {
	  for (unsigned jSetClear=0;jSetClear<EVR_MAX_PULSES;jSetClear++)
	    _er.ClearPulseMap(ram, iopcode, jSetClear, jSetClear, jSetClear);
    }
  }    

  int ram = 0;  
  unsigned opcode = _opcode;
  int pulse    = 0;   int presc = 1;  int enable = 1;
  int polarity = 0;   int map_reset_ena=0;  int map_set_ena=0;  int map_trigger_ena=1;
  _er.SetPulseProperties(pulse, polarity, map_reset_ena, map_set_ena, map_trigger_ena,enable);

  const double tickspersec = 119.0e6;
  int delay=(int)(PULSE_DELAY*tickspersec);
  int width=(int)(PULSE_WIDTH*tickspersec);
  _er.SetPulseParams(pulse,presc,delay,width);

  _er.SetFPOutMap(0, pulse);
  _er.SetFPOutMap(1, pulse);
  _er.SetFPOutMap(2, pulse);
  _er.SetFIFOEvent(ram, opcode, enable);   
  
  int trig=0; int set=-1; int clear=-1;
  _er.SetPulseMap(ram, opcode, trig, set, clear);
  
  _er.IrqEnable(0);
  _er.Enable(0);
  _er.EnableFIFO(0);
  _er.ClearFIFO();
  _er.ClearIrqFlags(0xffffffff);
  unsigned dummyram = 1;
  _er.MapRamEnable(dummyram, 1); 
  
}






