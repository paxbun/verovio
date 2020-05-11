/////////////////////////////////////////////////////////////////////////////
// Name:        pedal.h
// Author:      Laurent Pugin
// Created:     2016
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#ifndef __VRV_PEDAL_H__
#define __VRV_PEDAL_H__

#include "atts_cmn.h"
#include "atts_visual.h"
#include "controlelement.h"
#include "timeinterface.h"

namespace vrv {

//----------------------------------------------------------------------------
// Pedal
//----------------------------------------------------------------------------

/**
 * This class models the MEI <pedal> element.
 */
class Pedal : public ControlElement,
              public TimeSpanningInterface,
              public AttColor,
              public AttPedalLog,
              public AttPedalVis,
              public AttPlacement,
              public AttVerticalGroup {
public:
    /**
     * @name Constructors, destructors, and other standard methods
     * Reset method reset all attribute classes
     */
    ///@{
    Pedal();
    virtual ~Pedal();
    virtual Object *Clone() const { return new Pedal(*this); }
    virtual void Reset();
    virtual std::string GetClassName() const { return "Pedal"; }
    virtual ClassId GetClassId() const { return PEDAL; }
    ///@}

    /**
     * @name Getter to interfaces
     */
    ///@{
    virtual TimePointInterface *GetTimePointInterface() { return dynamic_cast<TimePointInterface *>(this); }
    virtual TimeSpanningInterface *GetTimeSpanningInterface() { return dynamic_cast<TimeSpanningInterface *>(this); }
    ////@}

    //----------//
    // Functors //
    //----------//

    /**
     * See Object::PrepareFloatingGrps
     */
    virtual int PrepareFloatingGrps(FunctorParams *);

    /**
     * See Object::GenerateMIDI
     */
    virtual int GenerateMIDI(FunctorParams *functorParams);

    /**
     * See Object::PreparePedalLine
     */
    virtual int PreparePedalLine(FunctorParams *functorParams);

protected:
    //
private:
    //
public:
    //
private:
    //
};

} // namespace vrv

#endif
