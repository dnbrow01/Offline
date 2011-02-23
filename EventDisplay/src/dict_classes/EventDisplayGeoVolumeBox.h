//
// Class which displays GeoVolumes with a box (used e.g. by the Cube class). It is inherited from ROOT's TGeoVolume and the ComponentInfo class which stores specific information for this support structure. The class' constructure creates a TGeoBox, which is put into the TGeoVolume. The context menu is overwritten with a menu item allowing the user to display information for this vane.
//
// $Id: EventDisplayGeoVolumeBox.h,v 1.1 2011/02/23 00:29:27 ehrlich Exp $
// $Author: ehrlich $ 
// $Date: 2011/02/23 00:29:27 $
//
// Original author Ralf Ehrlich
//

#ifndef EVENTDISPLAY_GEOBOX_H
#define EVENTDISPLAY_GEOBOX_H

#include <TGeoBBox.h>
#include <TGeoVolume.h>
#include <TGeoManager.h>
#include "ComponentInfo.h"
#include <TClass.h>
#include <TList.h>
#include <TClassMenuItem.h>

namespace mu2e_eventdisplay
{

class EventDisplayGeoVolumeBox : public TGeoVolume, public ComponentInfo
{
  EventDisplayGeoVolumeBox();
  EventDisplayGeoVolumeBox(const EventDisplayGeoVolumeBox &);
  EventDisplayGeoVolumeBox& operator=(const EventDisplayGeoVolumeBox &);

  public:
#ifndef __CINT__
  EventDisplayGeoVolumeBox(double dx, double dy, double dz, const TObject *mainframe, const boost::shared_ptr<ComponentInfo> info):TGeoVolume(),ComponentInfo(info)
  {
    //bare pointer needed since ROOT manages this object
    TGeoBBox *box=new TGeoBBox(NULL, dx, dy, dz);
    SetShape(box);
    SetNumber(GetGeoManager()->AddVolume(this)); //this is what happens in the base TGeoVolume constructor
    TList  *l=IsA()->GetMenuList();
    TClassMenuItem *m = new TClassMenuItem(TClassMenuItem::kPopupUserFunction,IsA(),"Information","showInfo",const_cast<TObject*>(mainframe),"TObject*",1); //bare pointer needed since ROOT manages this object
    l->Clear();
    l->AddFirst(m);
  }
#endif

  virtual ~EventDisplayGeoVolumeBox() 
  {
  }

  virtual const char* ClassName() const
  {
    IsA()->SetName(getName()->c_str());
    return(IsA()->GetName());
  } 

  ClassDef(EventDisplayGeoVolumeBox,0);
};

}
#endif
