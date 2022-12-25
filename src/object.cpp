/////////////////////////////////////////////////////////////////////////////
// Name:        object.cpp
// Author:      Laurent Pugin
// Created:     2005
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "object.h"

//----------------------------------------------------------------------------

#include <cassert>
#include <climits>
#include <iostream>
#include <math.h>
#include <sstream>

//----------------------------------------------------------------------------

#include "altsyminterface.h"
#include "chord.h"
#include "clef.h"
#include "comparison.h"
#include "dir.h"
#include "doc.h"
#include "dynam.h"
#include "editorial.h"
#include "featureextractor.h"
#include "findfunctor.h"
#include "functorparams.h"
#include "io.h"
#include "keysig.h"
#include "layer.h"
#include "linkinginterface.h"
#include "mdiv.h"
#include "measure.h"
#include "mensur.h"
#include "metersig.h"
#include "nc.h"
#include "note.h"
#include "page.h"
#include "plistinterface.h"
#include "score.h"
#include "staff.h"
#include "staffdef.h"
#include "surface.h"
#include "syl.h"
#include "syllable.h"
#include "symboltable.h"
#include "system.h"
#include "systemmilestone.h"
#include "tempo.h"
#include "text.h"
#include "textelement.h"
#include "tuning.h"
#include "vrv.h"
#include "zone.h"

namespace vrv {

//----------------------------------------------------------------------------
// Object
//----------------------------------------------------------------------------

thread_local unsigned long Object::s_objectCounter = 0;
thread_local std::mt19937 Object::s_randomGenerator;

Object::Object() : BoundingBox()
{
    if (s_objectCounter++ == 0) {
        this->SeedID();
    }
    this->Init(OBJECT, "m-");
}

Object::Object(ClassId classId) : BoundingBox()
{
    if (s_objectCounter++ == 0) {
        this->SeedID();
    }
    this->Init(classId, "m-");
}

Object::Object(ClassId classId, const std::string &classIdStr) : BoundingBox()
{
    if (s_objectCounter++ == 0) {
        this->SeedID();
    }
    this->Init(classId, classIdStr);
}

Object *Object::Clone() const
{
    // This should never happen because the method should be overridden
    assert(false);
    return NULL;
}

Object::Object(const Object &object) : BoundingBox(object)
{
    this->ResetBoundingBox(); // It does not make sense to keep the values of the BBox

    m_classId = object.m_classId;
    m_classIdStr = object.m_classIdStr;
    m_parent = NULL;

    // Flags
    m_isAttribute = object.m_isAttribute;
    m_isModified = true;
    m_isReferenceObject = object.m_isReferenceObject;

    // Also copy attribute classes
    m_attClasses = object.m_attClasses;
    m_interfaces = object.m_interfaces;
    // New id
    this->GenerateID();
    // For now do not copy them
    // m_unsupported = object.m_unsupported;

    if (!object.CopyChildren()) {
        return;
    }

    int i;
    for (i = 0; i < (int)object.m_children.size(); ++i) {
        Object *current = object.m_children.at(i);
        Object *clone = current->Clone();
        if (clone) {
            LinkingInterface *link = clone->GetLinkingInterface();
            if (link) link->AddBackLink(current);
            clone->SetParent(this);
            clone->CloneReset();
            m_children.push_back(clone);
        }
    }
}

void Object::CloneReset()
{
    this->Modify();
    FunctorParams voidParams;
    this->ResetData(&voidParams);
}

Object &Object::operator=(const Object &object)
{
    // not self assignement
    if (this != &object) {
        ClearChildren();
        this->ResetBoundingBox(); // It does not make sense to keep the values of the BBox

        m_classId = object.m_classId;
        m_classIdStr = object.m_classIdStr;
        m_parent = NULL;
        // Flags
        m_isAttribute = object.m_isAttribute;
        m_isModified = true;
        m_isReferenceObject = object.m_isReferenceObject;

        // Also copy attribute classes
        m_attClasses = object.m_attClasses;
        m_interfaces = object.m_interfaces;
        // New id
        this->GenerateID();
        // For now do now copy them
        // m_unsupported = object.m_unsupported;
        LinkingInterface *link = this->GetLinkingInterface();
        if (link) link->AddBackLink(&object);

        if (object.CopyChildren()) {
            int i;
            for (i = 0; i < (int)object.m_children.size(); ++i) {
                Object *current = object.m_children.at(i);
                Object *clone = current->Clone();
                if (clone) {
                    LinkingInterface *link = clone->GetLinkingInterface();
                    if (link) link->AddBackLink(current);
                    clone->SetParent(this);
                    clone->CloneReset();
                    m_children.push_back(clone);
                }
            }
        }
    }
    return *this;
}

Object::~Object()
{
    ClearChildren();
}

void Object::Init(ClassId classId, const std::string &classIdStr)
{
    assert(classIdStr.size());

    m_classId = classId;
    m_classIdStr = classIdStr;
    m_parent = NULL;
    // Flags
    m_isAttribute = false;
    m_isModified = true;
    m_isReferenceObject = false;
    // Comments
    m_comment = "";
    m_closingComment = "";

    this->GenerateID();

    this->Reset();
}

void Object::SetAsReferenceObject()
{
    assert(m_children.empty());

    m_isReferenceObject = true;
}

const Resources *Object::GetDocResources() const
{
    // Search for the document
    const Doc *doc = NULL;
    if (this->Is(DOC)) {
        doc = vrv_cast<const Doc *>(this);
    }
    else {
        doc = vrv_cast<const Doc *>(this->GetFirstAncestor(DOC));
    }

    // Return the resources or display warning
    if (doc) {
        return &doc->GetResources();
    }
    else {
        LogWarning("Requested resources unavailable.");
        return NULL;
    }
}

void Object::Reset()
{
    ClearChildren();
    this->ResetBoundingBox();
}

void Object::RegisterInterface(std::vector<AttClassId> *attClasses, InterfaceId interfaceId)
{
    m_attClasses.insert(m_attClasses.end(), attClasses->begin(), attClasses->end());
    m_interfaces.push_back(interfaceId);
}

bool Object::IsMilestoneElement()
{
    if (this->IsEditorialElement() || this->Is(ENDING) || this->Is(SECTION)) {
        SystemMilestoneInterface *interface = dynamic_cast<SystemMilestoneInterface *>(this);
        assert(interface);
        return (interface->IsSystemMilestone());
    }
    else if (this->Is(MDIV) || this->Is(SCORE)) {
        PageMilestoneInterface *interface = dynamic_cast<PageMilestoneInterface *>(this);
        assert(interface);
        return (interface->IsPageMilestone());
    }
    return false;
}

Object *Object::GetMilestoneEnd()
{
    if (this->IsEditorialElement() || this->Is(ENDING) || this->Is(SECTION)) {
        SystemMilestoneInterface *interface = dynamic_cast<SystemMilestoneInterface *>(this);
        assert(interface);
        return (interface->GetEnd());
    }
    else if (this->Is(MDIV) || this->Is(SCORE)) {
        PageMilestoneInterface *interface = dynamic_cast<PageMilestoneInterface *>(this);
        assert(interface);
        return (interface->GetEnd());
    }
    return NULL;
}

void Object::MoveChildrenFrom(Object *sourceParent, int idx, bool allowTypeChange)
{
    if (this == sourceParent) {
        assert("Object cannot be copied to itself");
    }
    if (!allowTypeChange && (m_classId != sourceParent->m_classId)) {
        assert("Object must be of the same type");
    }

    int i;
    for (i = 0; i < (int)sourceParent->m_children.size(); ++i) {
        Object *child = sourceParent->Relinquish(i);
        child->SetParent(this);
        if (idx != -1) {
            this->InsertChild(child, idx);
            idx++;
        }
        else {
            m_children.push_back(child);
        }
    }
}

void Object::ReplaceChild(Object *currentChild, Object *replacingChild)
{
    assert(this->GetChildIndex(currentChild) != -1);
    assert(this->GetChildIndex(replacingChild) == -1);

    int idx = this->GetChildIndex(currentChild);
    currentChild->ResetParent();
    m_children.at(idx) = replacingChild;
    replacingChild->SetParent(this);
    this->Modify();
}

void Object::InsertBefore(Object *child, Object *newChild)
{
    assert(this->GetChildIndex(child) != -1);
    assert(this->GetChildIndex(newChild) == -1);

    int idx = this->GetChildIndex(child);
    newChild->SetParent(this);
    this->InsertChild(newChild, idx);

    this->Modify();
}

void Object::InsertAfter(Object *child, Object *newChild)
{
    assert(this->GetChildIndex(child) != -1);
    assert(this->GetChildIndex(newChild) == -1);

    int idx = this->GetChildIndex(child);
    newChild->SetParent(this);
    this->InsertChild(newChild, idx + 1);

    this->Modify();
}

void Object::SortChildren(Object::binaryComp comp)
{
    std::stable_sort(m_children.begin(), m_children.end(), comp);
    this->Modify();
}

void Object::MoveItselfTo(Object *targetParent)
{
    assert(targetParent);
    assert(m_parent);
    assert(m_parent != targetParent);

    Object *relinquishedObject = this->GetParent()->Relinquish(this->GetIdx());
    assert(relinquishedObject && (relinquishedObject == this));
    targetParent->AddChild(relinquishedObject);
}

void Object::SwapID(Object *other)
{
    assert(other);
    std::string swapID = this->GetID();
    this->SetID(other->GetID());
    other->SetID(swapID);
}

void Object::ClearChildren()
{
    if (m_isReferenceObject) {
        m_children.clear();
        return;
    }

    ArrayOfObjects::iterator iter;
    for (iter = m_children.begin(); iter != m_children.end(); ++iter) {
        // we need to check if this is the parent
        // ownership might have been given up with Relinquish
        if ((*iter)->GetParent() == this) {
            delete *iter;
        }
    }
    m_children.clear();
}

int Object::GetChildCount(const ClassId classId) const
{
    return (int)count_if(m_children.begin(), m_children.end(), ObjectComparison(classId));
}

int Object::GetChildCount(const ClassId classId, int depth) const
{
    ListOfConstObjects objects = this->FindAllDescendantsByType(classId, true, depth);
    return (int)objects.size();
}

int Object::GetDescendantCount(const ClassId classId) const
{
    ListOfConstObjects objects = this->FindAllDescendantsByType(classId);
    return (int)objects.size();
}

int Object::GetAttributes(ArrayOfStrAttr *attributes) const
{
    assert(attributes);
    attributes->clear();

    Att::GetAnalytical(this, attributes);
    Att::GetCmn(this, attributes);
    Att::GetCmnornaments(this, attributes);
    Att::GetCritapp(this, attributes);
    // Att::GetEdittrans(this, attributes);
    Att::GetExternalsymbols(this, attributes);
    Att::GetFrettab(this, attributes);
    Att::GetFacsimile(this, attributes);
    // Att::GetFigtable(this, attributes);
    // Att::GetFingering(this, attributes);
    Att::GetGestural(this, attributes);
    // Att::GetHarmony(this, attributes);
    // Att::GetHeader(this, attributes);
    Att::GetMei(this, attributes);
    Att::GetMensural(this, attributes);
    Att::GetMidi(this, attributes);
    Att::GetNeumes(this, attributes);
    Att::GetPagebased(this, attributes);
    // Att::GetPerformance(this, attributes);
    Att::GetShared(this, attributes);
    // Att::GetUsersymbols(this, attributes);
    Att::GetVisual(this, attributes);

    for (auto &pair : m_unsupported) {
        attributes->push_back({ pair.first, pair.second });
    }

    return (int)attributes->size();
}

bool Object::HasAttribute(std::string attribute, std::string value) const
{
    ArrayOfStrAttr attributes;
    this->GetAttributes(&attributes);
    ArrayOfStrAttr::iterator iter;
    for (iter = attributes.begin(); iter != attributes.end(); ++iter) {
        if (((*iter).first == attribute) && ((*iter).second == value)) return true;
    }
    return false;
}

Object *Object::GetFirst(const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetFirst(classId));
}

const Object *Object::GetFirst(const ClassId classId) const
{
    m_iteratorElementType = classId;
    m_iteratorEnd = m_children.end();
    m_iteratorCurrent = std::find_if(m_children.begin(), m_iteratorEnd, ObjectComparison(m_iteratorElementType));
    return (m_iteratorCurrent == m_iteratorEnd) ? NULL : *m_iteratorCurrent;
}

Object *Object::GetNext()
{
    return const_cast<Object *>(std::as_const(*this).GetNext());
}

const Object *Object::GetNext() const
{
    ++m_iteratorCurrent;
    m_iteratorCurrent = std::find_if(m_iteratorCurrent, m_iteratorEnd, ObjectComparison(m_iteratorElementType));
    return (m_iteratorCurrent == m_iteratorEnd) ? NULL : *m_iteratorCurrent;
}

Object *Object::GetNext(const Object *child, const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetNext(child, classId));
}

const Object *Object::GetNext(const Object *child, const ClassId classId) const
{
    ArrayOfObjects::const_iterator iteratorEnd, iteratorCurrent;
    iteratorEnd = m_children.end();
    iteratorCurrent = std::find(m_children.begin(), iteratorEnd, child);
    if (iteratorCurrent != iteratorEnd) {
        ++iteratorCurrent;
        iteratorCurrent = std::find_if(iteratorCurrent, iteratorEnd, ObjectComparison(classId));
    }
    return (iteratorCurrent == iteratorEnd) ? NULL : *iteratorCurrent;
}

Object *Object::GetPrevious(const Object *child, const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetPrevious(child, classId));
}

const Object *Object::GetPrevious(const Object *child, const ClassId classId) const
{
    ArrayOfObjects::const_reverse_iterator riteratorEnd, riteratorCurrent;
    riteratorEnd = m_children.rend();
    riteratorCurrent = std::find(m_children.rbegin(), riteratorEnd, child);
    if (riteratorCurrent != riteratorEnd) {
        ++riteratorCurrent;
        riteratorCurrent = std::find_if(riteratorCurrent, riteratorEnd, ObjectComparison(classId));
    }
    return (riteratorCurrent == riteratorEnd) ? NULL : *riteratorCurrent;
}

Object *Object::GetLast(const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetLast(classId));
}

const Object *Object::GetLast(const ClassId classId) const
{
    ArrayOfObjects::const_reverse_iterator riter
        = std::find_if(m_children.rbegin(), m_children.rend(), ObjectComparison(classId));
    return (riter == m_children.rend()) ? NULL : *riter;
}

int Object::GetIdx() const
{
    assert(m_parent);

    return m_parent->GetChildIndex(this);
}

void Object::InsertChild(Object *element, int idx)
{
    // With this method we require the parent to be set before
    assert(element->GetParent() == this);

    if (idx >= (int)m_children.size()) {
        m_children.push_back(element);
        return;
    }
    ArrayOfObjects::iterator iter = m_children.begin();
    m_children.insert(iter + (idx), element);
}

Object *Object::DetachChild(int idx)
{
    if (idx >= (int)m_children.size()) {
        return NULL;
    }
    Object *child = m_children.at(idx);
    child->ResetParent();
    ArrayOfObjects::iterator iter = m_children.begin();
    m_children.erase(iter + (idx));
    return child;
}

bool Object::HasDescendant(const Object *child, int deepness) const
{
    ArrayOfObjects::const_iterator iter;

    for (iter = m_children.begin(); iter != m_children.end(); ++iter) {
        if (child == (*iter))
            return true;
        else if (deepness == 0)
            return false;
        else if ((*iter)->HasDescendant(child, deepness - 1))
            return true;
    }

    return false;
}

Object *Object::Relinquish(int idx)
{
    if (idx >= (int)m_children.size()) {
        return NULL;
    }
    Object *child = m_children.at(idx);
    child->ResetParent();
    return child;
}

void Object::ClearRelinquishedChildren()
{
    ArrayOfObjects::iterator iter;
    for (iter = m_children.begin(); iter != m_children.end();) {
        if ((*iter)->GetParent() != this) {
            iter = m_children.erase(iter);
        }
        else
            ++iter;
    }
}

Object *Object::FindDescendantByID(const std::string &id, int deepness, bool direction)
{
    return const_cast<Object *>(std::as_const(*this).FindDescendantByID(id, deepness, direction));
}

const Object *Object::FindDescendantByID(const std::string &id, int deepness, bool direction) const
{
    FindByIDFunctor findByID(id);
    findByID.SetDirection(direction);
    this->Process(findByID, deepness, true);
    return findByID.GetElement();
}

Object *Object::FindDescendantByType(ClassId classId, int deepness, bool direction)
{
    return const_cast<Object *>(std::as_const(*this).FindDescendantByType(classId, deepness, direction));
}

const Object *Object::FindDescendantByType(ClassId classId, int deepness, bool direction) const
{
    ClassIdComparison comparison(classId);
    return this->FindDescendantByComparison(&comparison, deepness, direction);
}

Object *Object::FindDescendantByComparison(Comparison *comparison, int deepness, bool direction)
{
    return const_cast<Object *>(std::as_const(*this).FindDescendantByComparison(comparison, deepness, direction));
}

const Object *Object::FindDescendantByComparison(Comparison *comparison, int deepness, bool direction) const
{
    FindByComparisonFunctor findByComparison(comparison);
    findByComparison.SetDirection(direction);
    this->Process(findByComparison, deepness, true);
    return findByComparison.GetElement();
}

Object *Object::FindDescendantExtremeByComparison(Comparison *comparison, int deepness, bool direction)
{
    return const_cast<Object *>(
        std::as_const(*this).FindDescendantExtremeByComparison(comparison, deepness, direction));
}

const Object *Object::FindDescendantExtremeByComparison(Comparison *comparison, int deepness, bool direction) const
{
    FindExtremeByComparisonFunctor findExtremeByComparison(comparison);
    findExtremeByComparison.SetDirection(direction);
    this->Process(findExtremeByComparison, deepness, true);
    return findExtremeByComparison.GetElement();
}

ListOfObjects Object::FindAllDescendantsByType(ClassId classId, bool continueDepthSearchForMatches, int deepness)
{
    ListOfObjects descendants;
    ClassIdComparison comparison(classId);
    FindAllByComparisonFunctor findAllByComparison(&comparison, &descendants);
    findAllByComparison.SetContinueDepthSearchForMatches(continueDepthSearchForMatches);
    this->Process(findAllByComparison, deepness, true);
    return descendants;
}

ListOfConstObjects Object::FindAllDescendantsByType(
    ClassId classId, bool continueDepthSearchForMatches, int deepness) const
{
    ListOfConstObjects descendants;
    ClassIdComparison comparison(classId);
    FindAllConstByComparisonFunctor findAllConstByComparison(&comparison, &descendants);
    findAllConstByComparison.SetContinueDepthSearchForMatches(continueDepthSearchForMatches);
    this->Process(findAllConstByComparison, deepness, true);
    return descendants;
}

void Object::FindAllDescendantsByComparison(
    ListOfObjects *objects, Comparison *comparison, int deepness, bool direction, bool clear)
{
    assert(objects);
    if (clear) objects->clear();

    FindAllByComparisonFunctor findAllByComparison(comparison, objects);
    findAllByComparison.SetDirection(direction);
    this->Process(findAllByComparison, deepness, true);
}

void Object::FindAllDescendantsByComparison(
    ListOfConstObjects *objects, Comparison *comparison, int deepness, bool direction, bool clear) const
{
    assert(objects);
    if (clear) objects->clear();

    FindAllConstByComparisonFunctor findAllConstByComparison(comparison, objects);
    findAllConstByComparison.SetDirection(direction);
    this->Process(findAllConstByComparison, deepness, true);
}

void Object::FindAllDescendantsBetween(
    ListOfObjects *objects, Comparison *comparison, const Object *start, const Object *end, bool clear, int depth)
{
    assert(objects);
    if (clear) objects->clear();

    ListOfConstObjects descendants;
    FindAllBetweenFunctor findAllBetween(comparison, &descendants, start, end);
    this->Process(findAllBetween, depth, true);

    std::transform(descendants.begin(), descendants.end(), std::back_inserter(*objects),
        [](const Object *obj) { return const_cast<Object *>(obj); });
}

void Object::FindAllDescendantsBetween(ListOfConstObjects *objects, Comparison *comparison, const Object *start,
    const Object *end, bool clear, int depth) const
{
    assert(objects);
    if (clear) objects->clear();

    FindAllBetweenFunctor findAllBetween(comparison, objects, start, end);
    this->Process(findAllBetween, depth, true);
}

Object *Object::GetChild(int idx)
{
    return const_cast<Object *>(std::as_const(*this).GetChild(idx));
}

const Object *Object::GetChild(int idx) const
{
    if ((idx < 0) || (idx >= (int)m_children.size())) {
        return NULL;
    }
    return m_children.at(idx);
}

Object *Object::GetChild(int idx, const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetChild(idx, classId));
}

const Object *Object::GetChild(int idx, const ClassId classId) const
{
    ListOfConstObjects objects = this->FindAllDescendantsByType(classId, true, 1);
    if ((idx < 0) || (idx >= (int)objects.size())) {
        return NULL;
    }
    ListOfConstObjects::iterator it = objects.begin();
    std::advance(it, idx);
    return *it;
}

ArrayOfConstObjects Object::GetChildren() const
{
    return ArrayOfConstObjects(m_children.begin(), m_children.end());
}

bool Object::DeleteChild(Object *child)
{
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        m_children.erase(it);
        if (!m_isReferenceObject) {
            delete child;
        }
        this->Modify();
        return true;
    }
    else {
        assert(false);
        return false;
    }
}

int Object::DeleteChildrenByComparison(Comparison *comparison)
{
    int count = 0;
    ArrayOfObjects::iterator iter;
    for (iter = m_children.begin(); iter != m_children.end();) {
        if ((*comparison)(*iter)) {
            if (!m_isReferenceObject) delete *iter;
            iter = m_children.erase(iter);
            ++count;
        }
        else {
            ++iter;
        }
    }
    if (count > 0) this->Modify();
    return count;
}

void Object::GenerateID()
{
    m_id = m_classIdStr.at(0) + Object::GenerateRandID();
}

void Object::ResetID()
{
    GenerateID();
}

void Object::SetParent(Object *parent)
{
    assert(!m_parent);
    m_parent = parent;
}

bool Object::IsSupportedChild(Object *child)
{
    // This should never happen because the method should be overridden
    LogDebug(
        "Method for adding %s to %s should be overridden", child->GetClassName().c_str(), this->GetClassName().c_str());
    // assert(false);
    return false;
}

void Object::AddChild(Object *child)
{
    if (!((child->GetClassName() == "Staff") && (this->GetClassName() == "Section"))) {
        // temporarily allowing staff in section for issue https://github.com/MeasuringPolyphony/mp_editor/issues/62
        if (!this->IsSupportedChild(child)) {
            LogError("Adding '%s' to a '%s'", child->GetClassName().c_str(), this->GetClassName().c_str());
            return;
        }
    }

    child->SetParent(this);
    m_children.push_back(child);
    Modify();
}

int Object::GetDrawingX() const
{
    assert(m_parent);
    return m_parent->GetDrawingX();
}

int Object::GetDrawingY() const
{
    assert(m_parent);
    return m_parent->GetDrawingY();
}

void Object::ResetCachedDrawingX() const
{
    // if (m_cachedDrawingX == VRV_UNSET) return;
    m_cachedDrawingX = VRV_UNSET;
    ArrayOfObjects::const_iterator iter;
    for (iter = m_children.begin(); iter != m_children.end(); ++iter) {
        (*iter)->ResetCachedDrawingX();
    }
}

void Object::ResetCachedDrawingY() const
{
    // if (m_cachedDrawingY == VRV_UNSET) return;
    m_cachedDrawingY = VRV_UNSET;
    ArrayOfObjects::const_iterator iter;
    for (iter = m_children.begin(); iter != m_children.end(); ++iter) {
        (*iter)->ResetCachedDrawingY();
    }
}

int Object::GetChildIndex(const Object *child) const
{
    ArrayOfObjects::const_iterator iter;
    int i;
    for (iter = m_children.begin(), i = 0; iter != m_children.end(); ++iter, ++i) {
        if (child == *iter) {
            return i;
        }
    }
    return -1;
}

int Object::GetDescendantIndex(const Object *child, const ClassId classId, int depth)
{
    ListOfObjects objects = this->FindAllDescendantsByType(classId, true, depth);
    int i = 0;
    for (auto &object : objects) {
        if (child == object) return i;
        ++i;
    }
    return -1;
}

void Object::Modify(bool modified) const
{
    // if we have a parent and a new modification, propagate it
    if (m_parent && modified) {
        m_parent->Modify();
    }
    m_isModified = modified;
}

void Object::FillFlatList(ListOfConstObjects &flatList) const
{
    Functor addToFlatList(&Object::AddLayerElementToFlatList);
    AddLayerElementToFlatListParams addLayerElementToFlatListParams(&flatList);
    this->Process(&addToFlatList, &addLayerElementToFlatListParams);
}

ListOfObjects Object::GetAncestors()
{
    ListOfObjects ancestors;
    Object *object = m_parent;
    while (object) {
        ancestors.push_back(object);
        object = object->m_parent;
    }
    return ancestors;
}

ListOfConstObjects Object::GetAncestors() const
{
    ListOfConstObjects ancestors;
    const Object *object = m_parent;
    while (object) {
        ancestors.push_back(object);
        object = object->m_parent;
    }
    return ancestors;
}

Object *Object::GetFirstAncestor(const ClassId classId, int maxDepth)
{
    return const_cast<Object *>(std::as_const(*this).GetFirstAncestor(classId, maxDepth));
}

const Object *Object::GetFirstAncestor(const ClassId classId, int maxDepth) const
{
    if ((maxDepth == 0) || !m_parent) {
        return NULL;
    }

    if (m_parent->m_classId == classId) {
        return m_parent;
    }
    else {
        return (m_parent->GetFirstAncestor(classId, maxDepth - 1));
    }
}

Object *Object::GetFirstAncestorInRange(const ClassId classIdMin, const ClassId classIdMax, int maxDepth)
{
    return const_cast<Object *>(std::as_const(*this).GetFirstAncestorInRange(classIdMin, classIdMax, maxDepth));
}

const Object *Object::GetFirstAncestorInRange(const ClassId classIdMin, const ClassId classIdMax, int maxDepth) const
{
    if ((maxDepth == 0) || !m_parent) {
        return NULL;
    }

    if ((m_parent->m_classId > classIdMin) && (m_parent->m_classId < classIdMax)) {
        return m_parent;
    }
    else {
        return (m_parent->GetFirstAncestorInRange(classIdMin, classIdMax, maxDepth - 1));
    }
}

Object *Object::GetLastAncestorNot(const ClassId classId, int maxDepth)
{
    return const_cast<Object *>(std::as_const(*this).GetLastAncestorNot(classId, maxDepth));
}

const Object *Object::GetLastAncestorNot(const ClassId classId, int maxDepth) const
{
    if ((maxDepth == 0) || !m_parent) {
        return NULL;
    }

    if (m_parent->m_classId == classId) {
        return this;
    }
    else {
        return (m_parent->GetLastAncestorNot(classId, maxDepth - 1));
    }
}

Object *Object::GetFirstChildNot(const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetFirstChildNot(classId));
}

const Object *Object::GetFirstChildNot(const ClassId classId) const
{
    for (const auto child : m_children) {
        if (!child->Is(classId)) {
            return child;
        }
    }

    return NULL;
}

bool Object::HasEditorialContent()
{
    ListOfObjects editorial;
    IsEditorialElementComparison editorialComparison;
    this->FindAllDescendantsByComparison(&editorial, &editorialComparison);
    return (!editorial.empty());
}

bool Object::HasNonEditorialContent()
{
    ListOfObjects nonEditorial;
    IsEditorialElementComparison editorialComparison;
    editorialComparison.ReverseComparison();
    this->FindAllDescendantsByComparison(&nonEditorial, &editorialComparison);
    return (!nonEditorial.empty());
}

void Object::Process(Functor *functor, FunctorParams *functorParams, Functor *endFunctor, Filters *filters,
    int deepness, bool direction, bool skipFirst)
{
    if (functor->m_returnCode == FUNCTOR_STOP) {
        return;
    }

    // Update the current score stored in the document
    this->UpdateDocumentScore(direction);

    if (!skipFirst) {
        functor->Call(this, functorParams);
    }

    // do not go any deeper in this case
    if (functor->m_returnCode == FUNCTOR_SIBLINGS) {
        functor->m_returnCode = FUNCTOR_CONTINUE;
        return;
    }
    else if (this->IsEditorialElement()) {
        // since editorial object doesn't count, we increase the deepness limit
        deepness++;
    }
    if (deepness == 0) {
        // any need to change the functor m_returnCode?
        return;
    }
    deepness--;

    if (!this->SkipChildren(functor->m_visibleOnly)) {
        // We need a pointer to the array for the option to work on a reversed copy
        ArrayOfObjects *children = &m_children;
        if (direction == BACKWARD) {
            for (ArrayOfObjects::reverse_iterator iter = children->rbegin(); iter != children->rend(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, functorParams, endFunctor, filters, deepness, direction);
                }
            }
        }
        else {
            for (ArrayOfObjects::iterator iter = children->begin(); iter != children->end(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, functorParams, endFunctor, filters, deepness, direction);
                }
            }
        }
    }

    if (endFunctor && !skipFirst) {
        endFunctor->Call(this, functorParams);
    }
}

void Object::Process(Functor *functor, FunctorParams *functorParams, Functor *endFunctor, Filters *filters,
    int deepness, bool direction, bool skipFirst) const
{
    if (functor->m_returnCode == FUNCTOR_STOP) {
        return;
    }

    // Update the current score stored in the document
    const_cast<Object *>(this)->UpdateDocumentScore(direction);

    if (!skipFirst) {
        functor->Call(this, functorParams);
    }

    // do not go any deeper in this case
    if (functor->m_returnCode == FUNCTOR_SIBLINGS) {
        functor->m_returnCode = FUNCTOR_CONTINUE;
        return;
    }
    else if (this->IsEditorialElement()) {
        // since editorial object doesn't count, we increase the deepness limit
        deepness++;
    }
    if (deepness == 0) {
        // any need to change the functor m_returnCode?
        return;
    }
    deepness--;

    if (!this->SkipChildren(functor->m_visibleOnly)) {
        // We need a pointer to the array for the option to work on a reversed copy
        const ArrayOfObjects *children = &m_children;
        if (direction == BACKWARD) {
            for (ArrayOfObjects::const_reverse_iterator iter = children->rbegin(); iter != children->rend(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, functorParams, endFunctor, filters, deepness, direction);
                }
            }
        }
        else {
            for (ArrayOfObjects::const_iterator iter = children->begin(); iter != children->end(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, functorParams, endFunctor, filters, deepness, direction);
                }
            }
        }
    }

    if (endFunctor && !skipFirst) {
        endFunctor->Call(this, functorParams);
    }
}

void Object::Process(MutableFunctor &functor, int deepness, bool skipFirst)
{
    if (functor.GetCode() == FUNCTOR_STOP) {
        return;
    }

    // Update the current score stored in the document
    this->UpdateDocumentScore(functor.GetDirection());

    if (!skipFirst) {
        FunctorCode code = this->Accept(functor);
        functor.SetCode(code);
    }

    // do not go any deeper in this case
    if (functor.GetCode() == FUNCTOR_SIBLINGS) {
        functor.SetCode(FUNCTOR_CONTINUE);
        return;
    }
    else if (this->IsEditorialElement()) {
        // since editorial object doesn't count, we increase the deepness limit
        ++deepness;
    }
    if (deepness == 0) {
        // any need to change the functor m_returnCode?
        return;
    }
    --deepness;

    if (!this->SkipChildren(functor.VisibleOnly())) {
        // We need a pointer to the array for the option to work on a reversed copy
        ArrayOfObjects *children = &m_children;
        Filters *filters = functor.GetFilters();
        if (functor.GetDirection() == BACKWARD) {
            for (ArrayOfObjects::reverse_iterator iter = children->rbegin(); iter != children->rend(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, deepness);
                }
            }
        }
        else {
            for (ArrayOfObjects::iterator iter = children->begin(); iter != children->end(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, deepness);
                }
            }
        }
    }

    if (functor.ImplementsEndInterface() && !skipFirst) {
        FunctorCode code = this->AcceptEnd(functor);
        functor.SetCode(code);
    }
}

void Object::Process(ConstFunctor &functor, int deepness, bool skipFirst) const
{
    if (functor.GetCode() == FUNCTOR_STOP) {
        return;
    }

    // Update the current score stored in the document
    const_cast<Object *>(this)->UpdateDocumentScore(functor.GetDirection());

    if (!skipFirst) {
        FunctorCode code = this->Accept(functor);
        functor.SetCode(code);
    }

    // do not go any deeper in this case
    if (functor.GetCode() == FUNCTOR_SIBLINGS) {
        functor.SetCode(FUNCTOR_CONTINUE);
        return;
    }
    else if (this->IsEditorialElement()) {
        // since editorial object doesn't count, we increase the deepness limit
        ++deepness;
    }
    if (deepness == 0) {
        // any need to change the functor m_returnCode?
        return;
    }
    --deepness;

    if (!this->SkipChildren(functor.VisibleOnly())) {
        // We need a pointer to the array for the option to work on a reversed copy
        const ArrayOfObjects *children = &m_children;
        Filters *filters = functor.GetFilters();
        if (functor.GetDirection() == BACKWARD) {
            for (ArrayOfObjects::const_reverse_iterator iter = children->rbegin(); iter != children->rend(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, deepness);
                }
            }
        }
        else {
            for (ArrayOfObjects::const_iterator iter = children->begin(); iter != children->end(); ++iter) {
                // we will end here if there is no filter at all or for the current child type
                if (this->FiltersApply(filters, *iter)) {
                    (*iter)->Process(functor, deepness);
                }
            }
        }
    }

    if (functor.ImplementsEndInterface() && !skipFirst) {
        FunctorCode code = this->AcceptEnd(functor);
        functor.SetCode(code);
    }
}

FunctorCode Object::Accept(MutableFunctor &functor)
{
    return functor.VisitObject(this);
}

FunctorCode Object::Accept(ConstFunctor &functor) const
{
    return functor.VisitObject(this);
}

FunctorCode Object::AcceptEnd(MutableFunctor &functor)
{
    return functor.VisitObjectEnd(this);
}

FunctorCode Object::AcceptEnd(ConstFunctor &functor) const
{
    return functor.VisitObjectEnd(this);
}

void Object::UpdateDocumentScore(bool direction)
{
    // When we are starting a new score, we need to update the current score in the document
    if (direction == FORWARD && this->Is(SCORE)) {
        Score *score = vrv_cast<Score *>(this);
        assert(score);
        score->SetAsCurrent();
    }
    // We need to do the same in backward direction through the PageMilestoneEnd::m_start
    else if (direction == BACKWARD && this->Is(PAGE_MILESTONE_END)) {
        PageMilestoneEnd *elementEnd = vrv_cast<PageMilestoneEnd *>(this);
        assert(elementEnd);
        if (elementEnd->GetStart() && elementEnd->GetStart()->Is(SCORE)) {
            Score *score = vrv_cast<Score *>(elementEnd->GetStart());
            assert(score);
            score->SetAsCurrent();
        }
    }
}

bool Object::SkipChildren(bool visibleOnly) const
{
    if (visibleOnly) {
        if (this->IsEditorialElement()) {
            const EditorialElement *editorialElement = vrv_cast<const EditorialElement *>(this);
            assert(editorialElement);
            if (editorialElement->m_visibility == Hidden) {
                return true;
            }
        }
        else if (this->Is(MDIV)) {
            const Mdiv *mdiv = vrv_cast<const Mdiv *>(this);
            assert(mdiv);
            if (mdiv->m_visibility == Hidden) {
                return true;
            }
        }
        else if (this->IsSystemElement()) {
            const SystemElement *systemElement = vrv_cast<const SystemElement *>(this);
            assert(systemElement);
            if (systemElement->m_visibility == Hidden) {
                return true;
            }
        }
    }
    return false;
}

bool Object::FiltersApply(const Filters *filters, Object *object) const
{
    return filters ? filters->Apply(object) : true;
}

int Object::SaveObject(SaveParams &saveParams)
{
    Functor save(&Object::Save);
    // Special case where we want to process all objects
    save.m_visibleOnly = false;
    Functor saveEnd(&Object::SaveEnd);
    this->Process(&save, &saveParams, &saveEnd);

    return true;
}

void Object::ReorderByXPos()
{
    ReorderByXPosParams params;
    Functor reorder(&Object::ReorderByXPos);
    this->Process(&reorder, &params);
}

Object *Object::FindNextChild(Comparison *comp, Object *start)
{
    FindNextChildByComparisonFunctor findNextChildByComparison(comp, start);
    this->Process(findNextChildByComparison);
    return const_cast<Object *>(findNextChildByComparison.GetElement());
}

Object *Object::FindPreviousChild(Comparison *comp, Object *start)
{
    FindPreviousChildByComparisonFunctor findPreviousChildByComparison(comp, start);
    this->Process(findPreviousChildByComparison);
    return const_cast<Object *>(findPreviousChildByComparison.GetElement());
}

//----------------------------------------------------------------------------
// Static methods for Object
//----------------------------------------------------------------------------

void Object::SeedID(unsigned int seed)
{
    // Init random number generator for ids
    if (seed == 0) {
        std::random_device rd;
        s_randomGenerator.seed(rd());
    }
    else {
        s_randomGenerator.seed(seed);
    }
}

std::string Object::GenerateRandID()
{
    unsigned int nr = s_randomGenerator();

    // char str[17];
    // snprintf(str, 17, "%016d", nr);
    // return std::string(str);

    return BaseEncodeInt(nr, 36);
}

bool Object::sortByUlx(Object *a, Object *b)
{
    FacsimileInterface *fa = NULL, *fb = NULL;
    InterfaceComparison comp(INTERFACE_FACSIMILE);
    if (a->GetFacsimileInterface() && a->GetFacsimileInterface()->HasFacs())
        fa = a->GetFacsimileInterface();
    else {
        ListOfObjects children;
        a->FindAllDescendantsByComparison(&children, &comp);
        for (auto it = children.begin(); it != children.end(); ++it) {
            if ((*it)->Is(SYL)) continue;
            FacsimileInterface *temp = (*it)->GetFacsimileInterface();
            assert(temp);
            if (temp->HasFacs() && (fa == NULL || temp->GetZone()->GetUlx() < fa->GetZone()->GetUlx())) {
                fa = temp;
            }
        }
    }
    if (b->GetFacsimileInterface() && b->GetFacsimileInterface()->HasFacs())
        fb = b->GetFacsimileInterface();
    else {
        ListOfObjects children;
        b->FindAllDescendantsByComparison(&children, &comp);
        for (auto it = children.begin(); it != children.end(); ++it) {
            if ((*it)->Is(SYL)) continue;
            FacsimileInterface *temp = (*it)->GetFacsimileInterface();
            assert(temp);
            if (temp->HasFacs() && (fb == NULL || temp->GetZone()->GetUlx() < fb->GetZone()->GetUlx())) {
                fb = temp;
            }
        }
    }

    // Preserve ordering of neume components in ligature
    if (a->Is(NC) && b->Is(NC)) {
        Nc *nca = dynamic_cast<Nc *>(a);
        Nc *ncb = dynamic_cast<Nc *>(b);
        if (nca->HasLigated() && ncb->HasLigated() && (a->GetParent() == b->GetParent())) {
            Object *parent = a->GetParent();
            assert(parent);
            if (abs(parent->GetChildIndex(a) - parent->GetChildIndex(b)) == 1) {
                // Return nc with higher pitch
                return nca->PitchDifferenceTo(ncb) > 0; // If object a has the higher pitch
            }
        }
    }

    if (fa == NULL || fb == NULL) {
        if (fa == NULL) {
            LogInfo("No available facsimile interface for %s", a->GetID().c_str());
        }
        if (fb == NULL) {
            LogInfo("No available facsimile interface for %s", b->GetID().c_str());
        }
        return false;
    }

    return (fa->GetZone()->GetUlx() < fb->GetZone()->GetUlx());
}

bool Object::IsPreOrdered(const Object *left, const Object *right)
{
    ListOfConstObjects ancestorsLeft = left->GetAncestors();
    ancestorsLeft.push_front(left);
    // Check if right is an ancestor of left
    if (std::find(ancestorsLeft.begin(), ancestorsLeft.end(), right) != ancestorsLeft.end()) return false;
    ListOfConstObjects ancestorsRight = right->GetAncestors();
    ancestorsRight.push_front(right);
    // Check if left is an ancestor of right
    if (std::find(ancestorsRight.begin(), ancestorsRight.end(), left) != ancestorsRight.end()) return true;

    // Now there must be mismatches since we included left and right into the ancestor lists above
    auto iterPair = std::mismatch(ancestorsLeft.rbegin(), ancestorsLeft.rend(), ancestorsRight.rbegin());
    const Object *commonParent = (*iterPair.first)->m_parent;
    if (commonParent) {
        return (commonParent->GetChildIndex(*iterPair.first) < commonParent->GetChildIndex(*iterPair.second));
    }
    return true;
}

//----------------------------------------------------------------------------
// ObjectListInterface
//----------------------------------------------------------------------------

ObjectListInterface::ObjectListInterface(const ObjectListInterface &interface)
{
    // actually nothing to do, we just don't want the list to be copied
    m_list.clear();
}

ObjectListInterface &ObjectListInterface::operator=(const ObjectListInterface &interface)
{
    // actually nothing to do, we just don't want the list to be copied
    if (this != &interface) {
        m_list.clear();
    }
    return *this;
}

void ObjectListInterface::ResetList(const Object *node) const
{
    // nothing to do, the list if up to date
    if (!node->IsModified()) {
        return;
    }

    node->Modify(false);
    m_list.clear();
    node->FillFlatList(m_list);
    this->FilterList(m_list);
}

const ListOfConstObjects &ObjectListInterface::GetList(const Object *node) const
{
    this->ResetList(node);
    return m_list;
}

ListOfObjects ObjectListInterface::GetList(const Object *node)
{
    this->ResetList(node);
    ListOfObjects result;
    std::transform(m_list.begin(), m_list.end(), std::back_inserter(result),
        [](const Object *obj) { return const_cast<Object *>(obj); });
    return result;
}

bool ObjectListInterface::HasEmptyList(const Object *node) const
{
    this->ResetList(node);
    return m_list.empty();
}

int ObjectListInterface::GetListSize(const Object *node) const
{
    this->ResetList(node);
    return static_cast<int>(m_list.size());
}

const Object *ObjectListInterface::GetListFront(const Object *node) const
{
    this->ResetList(node);
    assert(!m_list.empty());
    return m_list.front();
}

Object *ObjectListInterface::GetListFront(const Object *node)
{
    return const_cast<Object *>(std::as_const(*this).GetListFront(node));
}

const Object *ObjectListInterface::GetListBack(const Object *node) const
{
    this->ResetList(node);
    assert(!m_list.empty());
    return m_list.back();
}

Object *ObjectListInterface::GetListBack(const Object *node)
{
    return const_cast<Object *>(std::as_const(*this).GetListBack(node));
}

int ObjectListInterface::GetListIndex(const Object *listElement) const
{
    ListOfConstObjects::iterator iter;
    int i;
    for (iter = m_list.begin(), i = 0; iter != m_list.end(); ++iter, ++i) {
        if (listElement == *iter) {
            return i;
        }
    }
    return -1;
}

const Object *ObjectListInterface::GetListFirst(const Object *startFrom, const ClassId classId) const
{
    ListOfConstObjects::iterator it = m_list.begin();
    int idx = this->GetListIndex(startFrom);
    if (idx == -1) return NULL;
    std::advance(it, idx);
    it = std::find_if(it, m_list.end(), ObjectComparison(classId));
    return (it == m_list.end()) ? NULL : *it;
}

Object *ObjectListInterface::GetListFirst(const Object *startFrom, const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetListFirst(startFrom, classId));
}

const Object *ObjectListInterface::GetListFirstBackward(const Object *startFrom, const ClassId classId) const
{
    ListOfConstObjects::iterator it = m_list.begin();
    int idx = this->GetListIndex(startFrom);
    if (idx == -1) return NULL;
    std::advance(it, idx);
    ListOfConstObjects::reverse_iterator rit(it);
    rit = std::find_if(rit, m_list.rend(), ObjectComparison(classId));
    return (rit == m_list.rend()) ? NULL : *rit;
}

Object *ObjectListInterface::GetListFirstBackward(const Object *startFrom, const ClassId classId)
{
    return const_cast<Object *>(std::as_const(*this).GetListFirstBackward(startFrom, classId));
}

const Object *ObjectListInterface::GetListPrevious(const Object *listElement) const
{
    ListOfConstObjects::iterator iter;
    int i;
    for (iter = m_list.begin(), i = 0; iter != m_list.end(); ++iter, ++i) {
        if (listElement == *iter) {
            if (i > 0) {
                return *(--iter);
            }
            else {
                return NULL;
            }
        }
    }
    return NULL;
}

Object *ObjectListInterface::GetListPrevious(const Object *listElement)
{
    return const_cast<Object *>(std::as_const(*this).GetListPrevious(listElement));
}

const Object *ObjectListInterface::GetListNext(const Object *listElement) const
{
    ListOfConstObjects::reverse_iterator iter;
    int i;
    for (iter = m_list.rbegin(), i = 0; iter != m_list.rend(); ++iter, ++i) {
        if (listElement == *iter) {
            if (i > 0) {
                return *(--iter);
            }
            else {
                return NULL;
            }
        }
    }
    return NULL;
}

Object *ObjectListInterface::GetListNext(const Object *listElement)
{
    return const_cast<Object *>(std::as_const(*this).GetListNext(listElement));
}

//----------------------------------------------------------------------------
// TextListInterface
//----------------------------------------------------------------------------

std::u32string TextListInterface::GetText(const Object *node) const
{
    // alternatively we could cache the concatString in the interface and instantiate it in FilterList
    std::u32string concatText;
    const ListOfConstObjects &childList = this->GetList(node); // make sure it's initialized
    for (ListOfConstObjects::const_iterator it = childList.begin(); it != childList.end(); ++it) {
        if ((*it)->Is(LB)) {
            continue;
        }
        const Text *text = vrv_cast<const Text *>(*it);
        assert(text);
        concatText += text->GetText();
    }
    return concatText;
}

void TextListInterface::GetTextLines(const Object *node, std::vector<std::u32string> &lines) const
{
    // alternatively we could cache the concatString in the interface and instantiate it in FilterList
    std::u32string concatText;
    const ListOfConstObjects &childList = this->GetList(node); // make sure it's initialized
    for (ListOfConstObjects::const_iterator it = childList.begin(); it != childList.end(); ++it) {
        if ((*it)->Is(LB) && !concatText.empty()) {
            lines.push_back(concatText);
            concatText.clear();
            continue;
        }
        const Text *text = vrv_cast<const Text *>(*it);
        assert(text);
        concatText += text->GetText();
    }
    if (!concatText.empty()) {
        lines.push_back(concatText);
    }
}

void TextListInterface::FilterList(ListOfConstObjects &childList) const
{
    ListOfConstObjects::iterator iter = childList.begin();
    while (iter != childList.end()) {
        if (!(*iter)->Is({ LB, TEXT })) {
            // remove anything that is not an LayerElement (e.g. Verse, Syl, etc. but keep Lb)
            iter = childList.erase(iter);
            continue;
        }
        ++iter;
    }
}

//----------------------------------------------------------------------------
// Functor
//----------------------------------------------------------------------------

Functor::Functor()
{
    m_returnCode = FUNCTOR_CONTINUE;
    m_visibleOnly = true;
    obj_fpt = NULL;
    const_obj_fpt = NULL;
}

Functor::Functor(int (Object::*_obj_fpt)(FunctorParams *))
{
    m_returnCode = FUNCTOR_CONTINUE;
    m_visibleOnly = true;
    obj_fpt = _obj_fpt;
    const_obj_fpt = NULL;
}

Functor::Functor(int (Object::*_const_obj_fpt)(FunctorParams *) const)
{
    m_returnCode = FUNCTOR_CONTINUE;
    m_visibleOnly = true;
    obj_fpt = NULL;
    const_obj_fpt = _const_obj_fpt;
}

void Functor::Call(Object *ptr, FunctorParams *functorParams)
{
    if (const_obj_fpt) {
        m_returnCode = (ptr->*const_obj_fpt)(functorParams);
    }
    else {
        m_returnCode = (ptr->*obj_fpt)(functorParams);
    }
}

void Functor::Call(const Object *ptr, FunctorParams *functorParams)
{
    if (!const_obj_fpt && obj_fpt) {
        LogError("Non-const functor cannot be called from a const method!");
        assert(false);
    }
    m_returnCode = (ptr->*const_obj_fpt)(functorParams);
}

//----------------------------------------------------------------------------
// ObjectFactory methods
//----------------------------------------------------------------------------

ObjectFactory *ObjectFactory::GetInstance()
{
    static thread_local ObjectFactory factory;
    return &factory;
}

Object *ObjectFactory::Create(std::string name)
{
    Object *object = NULL;

    MapOfStrConstructors::iterator it = s_ctorsRegistry.find(name);
    if (it != s_ctorsRegistry.end()) object = it->second();

    if (object) {
        return object;
    }
    else {
        LogError("Factory for '%s' not found", name.c_str());
        return NULL;
    }
}

ClassId ObjectFactory::GetClassId(std::string name)
{
    ClassId classId = OBJECT;

    MapOfStrClassIds::iterator it = s_classIdsRegistry.find(name);
    if (it != s_classIdsRegistry.end()) {
        classId = it->second;
    }
    else {
        LogError("ClassId for '%s' not found", name.c_str());
    }

    return classId;
}

void ObjectFactory::GetClassIds(const std::vector<std::string> &classStrings, std::vector<ClassId> &classIds)
{
    for (auto str : classStrings) {
        if (s_classIdsRegistry.count(str) > 0) {
            classIds.push_back(s_classIdsRegistry.at(str));
        }
        else {
            LogDebug("Class name '%s' could not be matched", str.c_str());
        }
    }
}

void ObjectFactory::Register(std::string name, ClassId classId, std::function<Object *(void)> function)
{
    s_ctorsRegistry[name] = function;
    s_classIdsRegistry[name] = classId;
}

//----------------------------------------------------------------------------
// Object functor methods
//----------------------------------------------------------------------------

int Object::AddLayerElementToFlatList(FunctorParams *functorParams) const
{
    AddLayerElementToFlatListParams *params = vrv_params_cast<AddLayerElementToFlatListParams *>(functorParams);
    assert(params);

    params->m_flatList->push_back(this);
    // LogDebug("List %d", params->m_flatList->size());

    return FUNCTOR_CONTINUE;
}

int Object::ConvertToCastOffMensural(FunctorParams *functorParams)
{
    ConvertToCastOffMensuralParams *params = vrv_params_cast<ConvertToCastOffMensuralParams *>(functorParams);
    assert(params);

    assert(m_parent);
    // We want to move only the children of the layer of any type (notes, editorial elements, etc)
    if (m_parent->Is(LAYER)) {
        assert(params->m_targetLayer);
        this->MoveItselfTo(params->m_targetLayer);
        // Do not precess children because we move the full sub-tree
        return FUNCTOR_SIBLINGS;
    }

    return FUNCTOR_CONTINUE;
}

int Object::PrepareFacsimile(FunctorParams *functorParams)
{
    PrepareFacsimileParams *params = vrv_params_cast<PrepareFacsimileParams *>(functorParams);
    assert(params);

    if (this->HasInterface(INTERFACE_FACSIMILE)) {
        FacsimileInterface *interface = this->GetFacsimileInterface();
        assert(interface);
        if (interface->HasFacs()) {
            std::string facsID = (interface->GetFacs().compare(0, 1, "#") == 0 ? interface->GetFacs().substr(1)
                                                                               : interface->GetFacs());
            Zone *zone = params->m_facsimile->FindZoneByID(facsID);
            if (zone != NULL) {
                interface->AttachZone(zone);
            }
        }
        // Zoneless syl
        else if (this->Is(SYL)) {
            params->m_zonelessSyls.push_back(this);
        }
    }

    return FUNCTOR_CONTINUE;
}

int Object::PrepareLinking(FunctorParams *functorParams)
{
    PrepareLinkingParams *params = vrv_params_cast<PrepareLinkingParams *>(functorParams);
    assert(params);

    if (params->m_fillList && this->HasInterface(INTERFACE_LINKING)) {
        LinkingInterface *interface = this->GetLinkingInterface();
        assert(interface);
        interface->InterfacePrepareLinking(functorParams, this);
    }

    if (this->Is(NOTE)) {
        Note *note = vrv_cast<Note *>(this);
        assert(note);
        PrepareLinkingParams *params = vrv_params_cast<PrepareLinkingParams *>(functorParams);
        assert(params);
        note->ResolveStemSameas(params);
    }

    // @next
    std::string id = this->GetID();
    auto r1 = params->m_nextIDPairs.equal_range(id);
    if (r1.first != params->m_nextIDPairs.end()) {
        for (auto i = r1.first; i != r1.second; ++i) {
            i->second->SetNextLink(this);
        }
        params->m_nextIDPairs.erase(r1.first, r1.second);
    }

    // @sameas
    auto r2 = params->m_sameasIDPairs.equal_range(id);
    if (r2.first != params->m_sameasIDPairs.end()) {
        for (auto j = r2.first; j != r2.second; ++j) {
            j->second->SetSameasLink(this);
            // Issue a warning if classes of object and sameas do not match
            Object *owner = dynamic_cast<Object *>(j->second);
            if (owner && (owner->GetClassId() != this->GetClassId())) {
                LogWarning("%s with @xml:id %s has @sameas to an element of class %s.", owner->GetClassName().c_str(),
                    owner->GetID().c_str(), this->GetClassName().c_str());
            }
        }
        params->m_sameasIDPairs.erase(r2.first, r2.second);
    }
    return FUNCTOR_CONTINUE;
}

int Object::PreparePlist(FunctorParams *functorParams)
{
    PreparePlistParams *params = vrv_params_cast<PreparePlistParams *>(functorParams);
    assert(params);

    if (params->m_fillList && this->HasInterface(INTERFACE_PLIST)) {
        PlistInterface *interface = this->GetPlistInterface();
        assert(interface);
        return interface->InterfacePreparePlist(functorParams, this);
    }

    return FUNCTOR_CONTINUE;
}

int Object::PrepareProcessPlist(FunctorParams *functorParams)
{
    PreparePlistParams *params = vrv_params_cast<PreparePlistParams *>(functorParams);
    assert(params);

    if (!this->IsLayerElement()) return FUNCTOR_CONTINUE;

    std::string id = this->GetID();
    auto i = std::find_if(params->m_interfaceIDTuples.begin(), params->m_interfaceIDTuples.end(),
        [&id](std::tuple<PlistInterface *, std::string, Object *> tuple) { return (std::get<1>(tuple) == id); });
    if (i != params->m_interfaceIDTuples.end()) {
        std::get<2>(*i) = this;
    }

    return FUNCTOR_CONTINUE;
}

int Object::GetAlignmentLeftRight(FunctorParams *functorParams) const
{
    GetAlignmentLeftRightParams *params = vrv_params_cast<GetAlignmentLeftRightParams *>(functorParams);
    assert(params);

    if (!this->IsLayerElement()) return FUNCTOR_CONTINUE;

    if (!this->HasSelfBB() || this->HasEmptyBB()) return FUNCTOR_CONTINUE;

    if (this->Is(params->m_excludeClasses)) return FUNCTOR_CONTINUE;

    int refLeft = this->GetSelfLeft();
    if (params->m_minLeft > refLeft) params->m_minLeft = refLeft;

    int refRight = this->GetSelfRight();
    if (params->m_maxRight < refRight) params->m_maxRight = refRight;

    return FUNCTOR_CONTINUE;
}

int Object::CalcBBoxOverflows(FunctorParams *functorParams)
{
    CalcBBoxOverflowsParams *params = vrv_params_cast<CalcBBoxOverflowsParams *>(functorParams);
    assert(params);

    // starting a new staff
    if (this->Is(STAFF)) {
        Staff *currentStaff = vrv_cast<Staff *>(this);
        assert(currentStaff);

        if (!currentStaff->DrawingIsVisible()) {
            return FUNCTOR_SIBLINGS;
        }

        params->m_staffAlignment = currentStaff->GetAlignment();
        return FUNCTOR_CONTINUE;
    }

    // starting new layer
    if (this->Is(LAYER)) {
        Layer *currentLayer = vrv_cast<Layer *>(this);
        assert(currentLayer);
        // set scoreDef attr
        if (currentLayer->GetStaffDefClef()) {
            // System scoreDef clefs are taken into account but treated separately (see below)
            currentLayer->GetStaffDefClef()->CalcBBoxOverflows(params);
        }
        if (currentLayer->GetStaffDefKeySig()) {
            currentLayer->GetStaffDefKeySig()->CalcBBoxOverflows(params);
        }
        if (currentLayer->GetStaffDefMensur()) {
            currentLayer->GetStaffDefMensur()->CalcBBoxOverflows(params);
        }
        if (currentLayer->GetStaffDefMeterSig()) {
            currentLayer->GetStaffDefMeterSig()->CalcBBoxOverflows(params);
        }
        return FUNCTOR_CONTINUE;
    }

    if (this->IsSystemElement()) {
        return FUNCTOR_CONTINUE;
    }

    if (this->IsControlElement()) {
        return FUNCTOR_CONTINUE;
    }

    if (!this->IsLayerElement()) {
        return FUNCTOR_CONTINUE;
    }

    // Take into account beam in cross-staff situation
    if (this->Is(BEAM)) {
        Beam *beam = vrv_cast<Beam *>(this);
        assert(beam);
        // Ignore it if it has cross-staff content but is not entirely cross-staff itself
        if (beam->m_crossStaffContent && !beam->m_crossStaff) return FUNCTOR_CONTINUE;
    }

    // Take into account stem for notes in cross-staff situation and in beams
    if (this->Is(STEM)) {
        LayerElement *noteOrChord = dynamic_cast<LayerElement *>(this->GetParent());
        if (noteOrChord && noteOrChord->m_crossStaff) {
            if (noteOrChord->GetAncestorBeam()) {
                Beam *beam = vrv_cast<Beam *>(noteOrChord->GetFirstAncestor(BEAM));
                assert(beam);
                // Ignore it but only if the beam is not entirely cross-staff itself
                if (!beam->m_crossStaff) return FUNCTOR_CONTINUE;
            }
            else if (noteOrChord->GetIsInBeamSpan()) {
                return FUNCTOR_CONTINUE;
            }
        }
    }

    if (this->Is(FB) || this->Is(FIGURE)) {
        return FUNCTOR_CONTINUE;
    }

    if (this->Is(SYL)) {
        // We don't want to add the syl to the overflow since lyrics require a full line anyway
        return FUNCTOR_CONTINUE;
    }

    if (!this->HasSelfBB()) {
        // if nothing was drawn, do not take it into account
        return FUNCTOR_CONTINUE;
    }

    assert(params->m_staffAlignment);

    LayerElement *current = vrv_cast<LayerElement *>(this);
    assert(current);

    StaffAlignment *above = NULL;
    StaffAlignment *below = NULL;
    current->GetOverflowStaffAlignments(above, below);

    bool isScoreDefClef = false;
    // Exception for the scoreDef clef where we do not want to take into account the general overflow
    // We have instead distinct members in StaffAlignment to store them
    if (current->Is(CLEF) && current->GetScoreDefRole() == SCOREDEF_SYSTEM) {
        isScoreDefClef = true;
    }

    if (above) {
        int overflowAbove = above->CalcOverflowAbove(current);
        int staffSize = above->GetStaffSize();
        if (overflowAbove > params->m_doc->GetDrawingStaffLineWidth(staffSize) / 2) {
            // LogInfo("%s top overflow: %d", current->GetID().c_str(), overflowAbove);
            if (isScoreDefClef) {
                above->SetScoreDefClefOverflowAbove(overflowAbove);
            }
            else {
                above->SetOverflowAbove(overflowAbove);
                above->AddBBoxAbove(current);
            }
        }
    }

    if (below) {
        int overflowBelow = below->CalcOverflowBelow(current);
        int staffSize = below->GetStaffSize();
        if (overflowBelow > params->m_doc->GetDrawingStaffLineWidth(staffSize) / 2) {
            // LogInfo("%s bottom overflow: %d", current->GetID().c_str(), overflowBelow);
            if (isScoreDefClef) {
                below->SetScoreDefClefOverflowBelow(overflowBelow);
            }
            else {
                below->SetOverflowBelow(overflowBelow);
                below->AddBBoxBelow(current);
            }
        }
    }

    return FUNCTOR_CONTINUE;
}

int Object::CalcBBoxOverflowsEnd(FunctorParams *functorParams)
{
    CalcBBoxOverflowsParams *params = vrv_params_cast<CalcBBoxOverflowsParams *>(functorParams);
    assert(params);

    // starting new layer
    if (this->Is(LAYER)) {
        Layer *currentLayer = vrv_cast<Layer *>(this);
        assert(currentLayer);
        // set scoreDef attr
        if (currentLayer->GetCautionStaffDefClef()) {
            currentLayer->GetCautionStaffDefClef()->CalcBBoxOverflows(params);
        }
        if (currentLayer->GetCautionStaffDefKeySig()) {
            currentLayer->GetCautionStaffDefKeySig()->CalcBBoxOverflows(params);
        }
        if (currentLayer->GetCautionStaffDefMensur()) {
            currentLayer->GetCautionStaffDefMensur()->CalcBBoxOverflows(params);
        }
        if (currentLayer->GetCautionStaffDefMeterSig()) {
            currentLayer->GetCautionStaffDefMeterSig()->CalcBBoxOverflows(params);
        }
    }
    return FUNCTOR_CONTINUE;
}

int Object::GenerateFeatures(FunctorParams *functorParams)
{
    GenerateFeaturesParams *params = vrv_params_cast<GenerateFeaturesParams *>(functorParams);
    assert(params);

    params->m_extractor->Extract(this, params);

    return FUNCTOR_CONTINUE;
}

int Object::Save(FunctorParams *functorParams)
{
    SaveParams *params = vrv_params_cast<SaveParams *>(functorParams);
    assert(params);

    if (!params->m_output->WriteObject(this)) {
        return FUNCTOR_STOP;
    }
    return FUNCTOR_CONTINUE;
}

int Object::SaveEnd(FunctorParams *functorParams)
{
    SaveParams *params = vrv_params_cast<SaveParams *>(functorParams);
    assert(params);

    if (!params->m_output->WriteObjectEnd(this)) {
        return FUNCTOR_STOP;
    }
    return FUNCTOR_CONTINUE;
}

int Object::ReorderByXPos(FunctorParams *functorParams)
{
    if (this->GetFacsimileInterface() != NULL) {
        if (this->GetFacsimileInterface()->HasFacs()) {
            return FUNCTOR_SIBLINGS; // This would have already been reordered.
        }
    }

    std::stable_sort(m_children.begin(), m_children.end(), sortByUlx);
    this->Modify();
    return FUNCTOR_CONTINUE;
}

} // namespace vrv
