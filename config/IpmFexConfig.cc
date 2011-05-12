#include "pdsapp/config/IpmFexConfig.hh"
#include "pdsapp/config/IpmFexTable.hh"

#include "pds/config/DiodeFexConfigType.hh"
#include "pds/config/IpmFexConfigType.hh"

#include <new>

typedef DiodeFexConfigType T;
typedef IpmFexConfigType   U;

static const int NRANGES = 8;
static const int NCHAN = 4;

using namespace Pds_ConfigDb;

IpmFexConfig::IpmFexConfig():
  Serializer("Ipm_Fex"), _table(new IpmFexTable(NRANGES))
{
  _table->insert(pList);
}

int IpmFexConfig::readParameters(void *from)
{
  U& c = *new(from) U;
  for(int i=0; i<NCHAN; i++)
    _table->set(i,c.diode[i].base,c.diode[i].scale);
  _table->xscale(c.xscale);
  _table->yscale(c.yscale);
  return sizeof(U);
}

int IpmFexConfig::writeParameters(void *to)
{
  T darray[NCHAN];
  for(int i=0; i<NCHAN; i++)
    _table->get(i,darray[i].base,darray[i].scale);
  *new(to) U(darray,_table->xscale(),_table->yscale());
  return sizeof(U);
}

int IpmFexConfig::dataSize() const
{
  return sizeof(U);
}
