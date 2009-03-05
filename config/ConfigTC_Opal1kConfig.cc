#include "ConfigTC_Opal1kConfig.hh"

#include "pdsapp/config/ConfigTC_Parameters.hh"
#include "pdsdata/opal1k/ConfigV1.hh"

#include <new>

namespace ConfigGui {

#define OpalTC Opal1k::ConfigV1
  enum OpalLUT { None };

  static const char* OpalLUTNames[] = { "None",
					NULL };
  static const char* depth_range[] = { "8 Bit", "10 Bit", "12 Bit", NULL };
  static const char* binning_range[] = { "x1", "x2", "x4", NULL };
  static const char* mirroring_range[] = { "None", "HFlip", "VFlip", "HVFlip", NULL };

  class Opal1kConfig::Private_Data {
  public:
    Private_Data() :
      _black_level      ("Black Level",   0, 0, 0xfff),
      _gain             ("Gain"       , 100, 100, 3200),
      _depth            ("Depth"      , OpalTC::Twelve_bit, depth_range),
      _binning          ("Binning", OpalTC::x1, binning_range),
      _mirroring        ("Mirroring", OpalTC::None, mirroring_range),
      _vertical_remap   ("Vertical Remap", Enums::True, Enums::Bool_Names),
      _defect_pixel_corr("Defect Pixel Correction", Enums::True, Enums::Bool_Names),
      _output_lut       ("Output Lookup Table", None, OpalLUTNames)
    {}

    void insert(Pds::LinkedList<Parameter>& pList) {
      pList.insert(&_black_level);
      pList.insert(&_gain);
      pList.insert(&_depth);
      pList.insert(&_binning);
      pList.insert(&_mirroring);
      pList.insert(&_vertical_remap);
      pList.insert(&_defect_pixel_corr);
      pList.insert(&_output_lut);
    }

    bool pull(void* from) {
      OpalTC& tc = *new(from) OpalTC;
      _black_level.value = tc.black_level();
      _gain       .value = tc.gain_percent();
      _depth      .value = tc.output_resolution();
      _binning    .value = tc.vertical_binning();
      _mirroring  .value = tc.output_mirroring();
      _vertical_remap.value = tc.vertical_remapping() ? Enums::True : Enums::False;
      _defect_pixel_corr.value = tc.defect_pixel_correction_enabled() ? Enums::True : Enums::False;
      _output_lut.value = None;
      return true;
    }

    int push(void* to) {
      OpalTC& tc = *new(to) OpalTC(_black_level.value,
				   _gain.value,
				   _depth.value,
				   _binning.value,
				   _mirroring.value,
				   _vertical_remap.value==Enums::True,
				   _defect_pixel_corr.value==Enums::True);
      return tc.size();
    }
  public:
    NumericInt<unsigned short>    _black_level;
    NumericInt<unsigned short>    _gain;
    Enumerated<OpalTC::Depth>     _depth;
    Enumerated<OpalTC::Binning>   _binning;
    Enumerated<OpalTC::Mirroring> _mirroring;
    Enumerated<Enums::Bool>            _vertical_remap;
    Enumerated<Enums::Bool>            _defect_pixel_corr;
    Enumerated<OpalLUT>           _output_lut;
  };
};


using namespace ConfigGui;

Opal1kConfig::Opal1kConfig() : 
  Serializer("Opal1k_Config"),
  _private_data( new Private_Data )
{
  _private_data->insert(pList);
}

bool Opal1kConfig::readParameters (void* from) {
  return _private_data->pull(from);
}

int  Opal1kConfig::writeParameters(void* to) {
  return _private_data->push(to);
}

#include "ConfigTC_Parameters.icc"

template class Enumerated<OpalTC::Depth>;
template class Enumerated<OpalTC::Binning>;
template class Enumerated<OpalTC::Mirroring>;
template class Enumerated<OpalLUT>;
