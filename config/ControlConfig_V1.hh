#ifndef Pds_ControlConfig_V1_hh
#define Pds_ControlConfig_V1_hh

#include "pdsapp/config/Serializer.hh"

namespace Pds_ConfigDb {
  namespace ControlConfig_V1 {
    class ControlConfig : public Serializer {
    public:
      ControlConfig();
      ~ControlConfig() {}
    public:
      int  readParameters (void* from);
      int  writeParameters(void* to);
      int  dataSize       () const;
    private:
      class Private_Data;
      Private_Data* _private_data;
    };
  };
};

#endif
