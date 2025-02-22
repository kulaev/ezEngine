#include <ToolsFoundation/ToolsFoundationPCH.h>

#include <Foundation/Logging/Log.h>
#include <Foundation/Types/VariantTypeRegistry.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>
#include <ToolsFoundation/Serialization/DocumentObjectConverter.h>

ezAbstractObjectNode* ezDocumentObjectConverterWriter::AddObjectToGraph(const ezDocumentObject* pObject, ezStringView sNodeName)
{
  ezAbstractObjectNode* pNode = AddSubObjectToGraph(pObject, sNodeName);

  while (!m_QueuedObjects.IsEmpty())
  {
    auto itCur = m_QueuedObjects.GetIterator();

    AddSubObjectToGraph(itCur.Key(), nullptr);

    m_QueuedObjects.Remove(itCur);
  }

  return pNode;
}

void ezDocumentObjectConverterWriter::AddProperty(ezAbstractObjectNode* pNode, const ezAbstractProperty* pProp, const ezDocumentObject* pObject)
{
  if (m_Filter.IsValid() && !m_Filter(pObject, pProp))
    return;

  const ezRTTI* pPropType = pProp->GetSpecificType();
  const bool bIsValueType = ezReflectionUtils::IsValueType(pProp);

  switch (pProp->GetCategory())
  {
    case ezPropertyCategory::Member:
    {
      if (pProp->GetFlags().IsSet(ezPropertyFlags::Pointer))
      {
        if (pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner))
        {
          const ezUuid guid = pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName()).Get<ezUuid>();

          pNode->AddProperty(pProp->GetPropertyName(), guid);
          if (guid.IsValid())
            m_QueuedObjects.Insert(m_pManager->GetObject(guid));
        }
        else
        {
          pNode->AddProperty(pProp->GetPropertyName(), pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName()));
        }
      }
      else
      {
        if (pProp->GetFlags().IsAnySet(ezPropertyFlags::IsEnum | ezPropertyFlags::Bitflags))
        {
          ezStringBuilder sTemp;
          ezReflectionUtils::EnumerationToString(
            pPropType, pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName()).ConvertTo<ezInt64>(), sTemp);
          pNode->AddProperty(pProp->GetPropertyName(), sTemp.GetData());
        }
        else if (bIsValueType)
        {
          pNode->AddProperty(pProp->GetPropertyName(), pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName()));
        }
        else if (pProp->GetFlags().IsSet(ezPropertyFlags::Class))
        {
          const ezUuid guid = pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName()).Get<ezUuid>();
          EZ_ASSERT_DEV(guid.IsValid(), "Embedded class cannot be null.");
          pNode->AddProperty(pProp->GetPropertyName(), guid);
          m_QueuedObjects.Insert(m_pManager->GetObject(guid));
        }
      }
    }

    break;

    case ezPropertyCategory::Array:
    case ezPropertyCategory::Set:
    {
      const ezInt32 iCount = pObject->GetTypeAccessor().GetCount(pProp->GetPropertyName());
      EZ_ASSERT_DEV(iCount >= 0, "Invalid array property size {0}", iCount);

      ezVariantArray values;
      values.SetCount(iCount);

      for (ezInt32 i = 0; i < iCount; ++i)
      {
        values[i] = pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName(), i);
        if (!bIsValueType)
        {
          m_QueuedObjects.Insert(m_pManager->GetObject(values[i].Get<ezUuid>()));
        }
      }
      pNode->AddProperty(pProp->GetPropertyName(), values);
    }
    break;
    case ezPropertyCategory::Map:
    {
      const ezInt32 iCount = pObject->GetTypeAccessor().GetCount(pProp->GetPropertyName());
      EZ_ASSERT_DEV(iCount >= 0, "Invalid map property size {0}", iCount);

      ezVariantDictionary values;
      values.Reserve(iCount);
      ezHybridArray<ezVariant, 16> keys;
      pObject->GetTypeAccessor().GetKeys(pProp->GetPropertyName(), keys);

      for (const ezVariant& key : keys)
      {
        ezVariant value = pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName(), key);
        values.Insert(key.Get<ezString>(), value);
        if (!bIsValueType)
        {
          m_QueuedObjects.Insert(m_pManager->GetObject(value.Get<ezUuid>()));
        }
      }
      pNode->AddProperty(pProp->GetPropertyName(), values);
    }
    break;
    case ezPropertyCategory::Constant:
      // Nothing to do here.
      break;
    default:
      EZ_ASSERT_NOT_IMPLEMENTED
  }
}

void ezDocumentObjectConverterWriter::AddProperties(ezAbstractObjectNode* pNode, const ezDocumentObject* pObject)
{
  ezHybridArray<const ezAbstractProperty*, 32> properties;
  pObject->GetTypeAccessor().GetType()->GetAllProperties(properties);

  for (const auto* pProp : properties)
  {
    AddProperty(pNode, pProp, pObject);
  }
}

ezAbstractObjectNode* ezDocumentObjectConverterWriter::AddSubObjectToGraph(const ezDocumentObject* pObject, ezStringView sNodeName)
{
  ezAbstractObjectNode* pNode = m_pGraph->AddNode(pObject->GetGuid(), pObject->GetType()->GetTypeName(), pObject->GetType()->GetTypeVersion(), sNodeName);
  AddProperties(pNode, pObject);
  return pNode;
}

ezDocumentObjectConverterReader::ezDocumentObjectConverterReader(const ezAbstractObjectGraph* pGraph, ezDocumentObjectManager* pManager, Mode mode)
{
  m_pManager = pManager;
  m_pGraph = pGraph;
  m_Mode = mode;
  m_uiUnknownTypeInstances = 0;
}

ezDocumentObject* ezDocumentObjectConverterReader::CreateObjectFromNode(const ezAbstractObjectNode* pNode)
{
  ezDocumentObject* pObject = nullptr;
  const ezRTTI* pType = ezRTTI::FindTypeByName(pNode->GetType());
  if (pType)
  {
    pObject = m_pManager->CreateObject(pType, pNode->GetGuid());
  }
  else
  {
    if (!m_UnknownTypes.Contains(pNode->GetType()))
    {
      ezLog::Error("Cannot create node of unknown type '{0}'.", pNode->GetType());
      m_UnknownTypes.Insert(pNode->GetType());
    }
    m_uiUnknownTypeInstances++;
  }
  return pObject;
}

void ezDocumentObjectConverterReader::AddObject(ezDocumentObject* pObject, ezDocumentObject* pParent, ezStringView sParentProperty, ezVariant index)
{
  EZ_ASSERT_DEV(pObject && pParent, "Need to have valid objects to add them to the document");
  if (m_Mode == ezDocumentObjectConverterReader::Mode::CreateAndAddToDocument && pParent->GetDocumentObjectManager()->GetObject(pParent->GetGuid()))
  {
    m_pManager->AddObject(pObject, pParent, sParentProperty, index);
  }
  else
  {
    pParent->InsertSubObject(pObject, sParentProperty, index);
  }
}

void ezDocumentObjectConverterReader::ApplyPropertiesToObject(const ezAbstractObjectNode* pNode, ezDocumentObject* pObject)
{
  // EZ_ASSERT_DEV(pObject->GetChildren().GetCount() == 0, "Can only apply properties to empty objects!");
  ezHybridArray<const ezAbstractProperty*, 32> properties;
  pObject->GetTypeAccessor().GetType()->GetAllProperties(properties);

  for (auto* pProp : properties)
  {
    auto* pOtherProp = pNode->FindProperty(pProp->GetPropertyName());
    if (pOtherProp == nullptr)
      continue;

    ApplyProperty(pObject, pProp, pOtherProp);
  }
}

void ezDocumentObjectConverterReader::ApplyDiffToObject(ezObjectAccessorBase* pObjectAccessor, const ezDocumentObject* pObject, ezDeque<ezAbstractGraphDiffOperation>& ref_diff)
{
  ezHybridArray<ezAbstractGraphDiffOperation*, 4> change;

  for (auto& op : ref_diff)
  {
    if (op.m_Operation == ezAbstractGraphDiffOperation::Op::PropertyChanged && pObject->GetGuid() == op.m_Node)
      change.PushBack(&op);
  }

  for (auto* op : change)
  {
    const ezAbstractProperty* pProp = pObject->GetTypeAccessor().GetType()->FindPropertyByName(op->m_sProperty);
    if (!pProp)
      continue;

    ApplyDiff(pObjectAccessor, pObject, pProp, *op, ref_diff);
  }

  // Recurse into owned sub objects (old or new)
  for (const ezDocumentObject* pSubObject : pObject->GetChildren())
  {
    ApplyDiffToObject(pObjectAccessor, pSubObject, ref_diff);
  }
}

void ezDocumentObjectConverterReader::ApplyDiff(ezObjectAccessorBase* pObjectAccessor, const ezDocumentObject* pObject, const ezAbstractProperty* pProp, ezAbstractGraphDiffOperation& op, ezDeque<ezAbstractGraphDiffOperation>& diff)
{
  ezStringBuilder sTemp;

  const bool bIsValueType = ezReflectionUtils::IsValueType(pProp);

  auto NeedsToBeDeleted = [&diff](const ezUuid& guid) -> bool
  {
    for (auto& op : diff)
    {
      if (op.m_Operation == ezAbstractGraphDiffOperation::Op::NodeRemoved && guid == op.m_Node)
        return true;
    }
    return false;
  };
  auto NeedsToBeCreated = [&diff](const ezUuid& guid) -> ezAbstractGraphDiffOperation*
  {
    for (auto& op : diff)
    {
      if (op.m_Operation == ezAbstractGraphDiffOperation::Op::NodeAdded && guid == op.m_Node)
        return &op;
    }
    return nullptr;
  };

  switch (pProp->GetCategory())
  {
    case ezPropertyCategory::Member:
    {
      if (pProp->GetFlags().IsAnySet(ezPropertyFlags::IsEnum | ezPropertyFlags::Bitflags) || bIsValueType)
      {
        pObjectAccessor->SetValue(pObject, pProp, op.m_Value).IgnoreResult();
      }
      else if (pProp->GetFlags().IsSet(ezPropertyFlags::Class))
      {
        if (pProp->GetFlags().IsSet(ezPropertyFlags::Pointer))
        {
          if (pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner))
          {
            const ezUuid oldGuid = pObjectAccessor->Get<ezUuid>(pObject, pProp);
            const ezUuid newGuid = op.m_Value.Get<ezUuid>();
            if (oldGuid.IsValid())
            {
              if (NeedsToBeDeleted(oldGuid))
              {
                pObjectAccessor->RemoveObject(pObjectAccessor->GetObject(oldGuid)).IgnoreResult();
              }
            }

            if (newGuid.IsValid())
            {
              if (ezAbstractGraphDiffOperation* pCreate = NeedsToBeCreated(newGuid))
              {
                pObjectAccessor->AddObject(pObject, pProp, ezVariant(), ezRTTI::FindTypeByName(pCreate->m_sProperty), pCreate->m_Node).IgnoreResult();
              }

              const ezDocumentObject* pChild = pObject->GetChild(newGuid);
              EZ_ASSERT_DEV(pChild != nullptr, "References child object does not exist!");
            }
          }
          else
          {
            pObjectAccessor->SetValue(pObject, pProp, op.m_Value).IgnoreResult();
          }
        }
        else
        {
          // Noting to do here, value cannot change
        }
      }
      break;
    }
    case ezPropertyCategory::Array:
    case ezPropertyCategory::Set:
    {
      const ezVariantArray& values = op.m_Value.Get<ezVariantArray>();
      ezInt32 iCurrentCount = pObjectAccessor->GetCount(pObject, pProp);
      if (bIsValueType || (pProp->GetFlags().IsAnySet(ezPropertyFlags::Pointer) && !pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner)))
      {
        for (ezUInt32 i = 0; i < values.GetCount(); ++i)
        {
          if (i < (ezUInt32)iCurrentCount)
            pObjectAccessor->SetValue(pObject, pProp, values[i], i).IgnoreResult();
          else
            pObjectAccessor->InsertValue(pObject, pProp, values[i], i).IgnoreResult();
        }
        for (ezInt32 i = iCurrentCount - 1; i >= (ezInt32)values.GetCount(); --i)
        {
          pObjectAccessor->RemoveValue(pObject, pProp, i).IgnoreResult();
        }
      }
      else // Class
      {
        const ezInt32 iCurrentCount2 = pObject->GetTypeAccessor().GetCount(pProp->GetPropertyName());

        ezHybridArray<ezVariant, 16> currentValues;
        pObject->GetTypeAccessor().GetValues(pProp->GetPropertyName(), currentValues);
        for (ezInt32 i = iCurrentCount2 - 1; i >= 0; --i)
        {
          if (NeedsToBeDeleted(currentValues[i].Get<ezUuid>()))
          {
            pObjectAccessor->RemoveObject(pObjectAccessor->GetObject(currentValues[i].Get<ezUuid>())).IgnoreResult();
          }
        }

        for (ezUInt32 i = 0; i < values.GetCount(); ++i)
        {
          if (ezAbstractGraphDiffOperation* pCreate = NeedsToBeCreated(values[i].Get<ezUuid>()))
          {
            pObjectAccessor->AddObject(pObject, pProp, i, ezRTTI::FindTypeByName(pCreate->m_sProperty), pCreate->m_Node).IgnoreResult();
          }
          else
          {
            pObjectAccessor->MoveObject(pObjectAccessor->GetObject(values[i].Get<ezUuid>()), pObject, pProp, i).IgnoreResult();
          }
        }
      }
      break;
    }
    case ezPropertyCategory::Map:
    {
      const ezVariantDictionary& values = op.m_Value.Get<ezVariantDictionary>();
      ezHybridArray<ezVariant, 16> keys;
      EZ_VERIFY(pObjectAccessor->GetKeys(pObject, pProp, keys).Succeeded(), "Property is not a map, getting keys failed.");

      if (bIsValueType || (pProp->GetFlags().IsAnySet(ezPropertyFlags::Pointer) && !pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner)))
      {
        for (const ezVariant& key : keys)
        {
          const ezString& sKey = key.Get<ezString>();
          if (!values.Contains(sKey))
          {
            EZ_VERIFY(pObjectAccessor->RemoveValue(pObject, pProp, key).Succeeded(), "RemoveValue failed.");
          }
        }
        for (auto it = values.GetIterator(); it.IsValid(); ++it)
        {
          ezVariant variantKey(it.Key());
          if (keys.Contains(variantKey))
            pObjectAccessor->SetValue(pObject, pProp, it.Value(), variantKey).IgnoreResult();
          else
            pObjectAccessor->InsertValue(pObject, pProp, it.Value(), variantKey).IgnoreResult();
        }
      }
      else // Class
      {
        for (const ezVariant& key : keys)
        {
          ezVariant value;
          EZ_VERIFY(pObjectAccessor->GetValue(pObject, pProp, value, key).Succeeded(), "");
          if (NeedsToBeDeleted(value.Get<ezUuid>()))
          {
            pObjectAccessor->RemoveObject(pObjectAccessor->GetObject(value.Get<ezUuid>())).IgnoreResult();
          }
        }
        for (auto it = values.GetIterator(); it.IsValid(); ++it)
        {
          const ezVariant& value = it.Value();
          ezVariant variantKey(it.Key());
          if (ezAbstractGraphDiffOperation* pCreate = NeedsToBeCreated(value.Get<ezUuid>()))
          {
            pObjectAccessor->AddObject(pObject, pProp, variantKey, ezRTTI::FindTypeByName(pCreate->m_sProperty), pCreate->m_Node).IgnoreResult();
          }
          else
          {
            pObjectAccessor->MoveObject(pObjectAccessor->GetObject(value.Get<ezUuid>()), pObject, pProp, variantKey).IgnoreResult();
          }
        }
      }
      break;
    }

    case ezPropertyCategory::Function:
    case ezPropertyCategory::Constant:
      break; // nothing to do
  }
}

void ezDocumentObjectConverterReader::ApplyProperty(ezDocumentObject* pObject, const ezAbstractProperty* pProp, const ezAbstractObjectNode::Property* pSource)
{
  ezStringBuilder sTemp;

  const bool bIsValueType = ezReflectionUtils::IsValueType(pProp);

  switch (pProp->GetCategory())
  {
    case ezPropertyCategory::Member:
    {
      if (pProp->GetFlags().IsSet(ezPropertyFlags::Pointer))
      {
        if (pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner))
        {
          if (pSource->m_Value.IsA<ezUuid>())
          {
            const ezUuid guid = pSource->m_Value.Get<ezUuid>();
            if (guid.IsValid())
            {
              auto* pSubNode = m_pGraph->GetNode(guid);
              EZ_ASSERT_DEV(pSubNode != nullptr, "invalid document");

              if (auto* pSubObject = CreateObjectFromNode(pSubNode))
              {
                ApplyPropertiesToObject(pSubNode, pSubObject);
                AddObject(pSubObject, pObject, pProp->GetPropertyName(), ezVariant());
              }
            }
          }
        }
        else
        {
          pObject->GetTypeAccessor().SetValue(pProp->GetPropertyName(), pSource->m_Value);
        }
      }
      else
      {
        if (pProp->GetFlags().IsAnySet(ezPropertyFlags::IsEnum | ezPropertyFlags::Bitflags) || bIsValueType)
        {
          pObject->GetTypeAccessor().SetValue(pProp->GetPropertyName(), pSource->m_Value);
        }
        else if (pSource->m_Value.IsA<ezUuid>()) // ezPropertyFlags::Class
        {
          const ezUuid& nodeGuid = pSource->m_Value.Get<ezUuid>();
          if (nodeGuid.IsValid())
          {
            const ezUuid subObjectGuid = pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName()).Get<ezUuid>();
            ezDocumentObject* pEmbeddedClassObject = pObject->GetChild(subObjectGuid);
            EZ_ASSERT_DEV(pEmbeddedClassObject != nullptr, "CreateObject should have created all embedded classes!");
            auto* pSubNode = m_pGraph->GetNode(nodeGuid);
            EZ_ASSERT_DEV(pSubNode != nullptr, "invalid document");

            ApplyPropertiesToObject(pSubNode, pEmbeddedClassObject);
          }
        }
      }
      break;
    }
    case ezPropertyCategory::Array:
    case ezPropertyCategory::Set:
    {
      const ezVariantArray& array = pSource->m_Value.Get<ezVariantArray>();
      const ezInt32 iCurrentCount = pObject->GetTypeAccessor().GetCount(pProp->GetPropertyName());
      if (bIsValueType || (pProp->GetFlags().IsAnySet(ezPropertyFlags::Pointer) && !pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner)))
      {
        for (ezUInt32 i = 0; i < array.GetCount(); ++i)
        {
          if (i < (ezUInt32)iCurrentCount)
          {
            pObject->GetTypeAccessor().SetValue(pProp->GetPropertyName(), array[i], i);
          }
          else
          {
            pObject->GetTypeAccessor().InsertValue(pProp->GetPropertyName(), i, array[i]);
          }
        }
        for (ezInt32 i = iCurrentCount - 1; i >= (ezInt32)array.GetCount(); i--)
        {
          pObject->GetTypeAccessor().RemoveValue(pProp->GetPropertyName(), i);
        }
      }
      else
      {
        for (ezUInt32 i = 0; i < array.GetCount(); ++i)
        {
          const ezUuid guid = array[i].Get<ezUuid>();
          if (guid.IsValid())
          {
            auto* pSubNode = m_pGraph->GetNode(guid);
            EZ_ASSERT_DEV(pSubNode != nullptr, "invalid document");

            if (i < (ezUInt32)iCurrentCount)
            {
              // Overwrite existing object
              ezUuid childGuid = pObject->GetTypeAccessor().GetValue(pProp->GetPropertyName(), i).ConvertTo<ezUuid>();
              if (ezDocumentObject* pSubObject = m_pManager->GetObject(childGuid))
              {
                ApplyPropertiesToObject(pSubNode, pSubObject);
              }
            }
            else
            {
              if (ezDocumentObject* pSubObject = CreateObjectFromNode(pSubNode))
              {
                ApplyPropertiesToObject(pSubNode, pSubObject);
                AddObject(pSubObject, pObject, pProp->GetPropertyName(), -1);
              }
            }
          }
        }
        for (ezInt32 i = iCurrentCount - 1; i >= (ezInt32)array.GetCount(); i--)
        {
          EZ_REPORT_FAILURE("Not implemented");
        }
      }
      break;
    }
    case ezPropertyCategory::Map:
    {
      const ezVariantDictionary& values = pSource->m_Value.Get<ezVariantDictionary>();
      ezHybridArray<ezVariant, 16> keys;
      pObject->GetTypeAccessor().GetKeys(pProp->GetPropertyName(), keys);

      if (bIsValueType || (pProp->GetFlags().IsAnySet(ezPropertyFlags::Pointer) && !pProp->GetFlags().IsSet(ezPropertyFlags::PointerOwner)))
      {
        for (const ezVariant& key : keys)
        {
          pObject->GetTypeAccessor().RemoveValue(pProp->GetPropertyName(), key);
        }
        for (auto it = values.GetIterator(); it.IsValid(); ++it)
        {
          pObject->GetTypeAccessor().InsertValue(pProp->GetPropertyName(), ezVariant(it.Key()), it.Value());
        }
      }
      else
      {
        for (auto it = values.GetIterator(); it.IsValid(); ++it)
        {
          const ezVariant& value = it.Value();
          const ezUuid guid = value.Get<ezUuid>();

          const ezVariant variantKey(it.Key());

          if (guid.IsValid())
          {
            auto* pSubNode = m_pGraph->GetNode(guid);
            EZ_ASSERT_DEV(pSubNode != nullptr, "invalid document");
            if (ezDocumentObject* pSubObject = CreateObjectFromNode(pSubNode))
            {
              ApplyPropertiesToObject(pSubNode, pSubObject);
              AddObject(pSubObject, pObject, pProp->GetPropertyName(), variantKey);
            }
          }
        }
      }
      break;
    }

    case ezPropertyCategory::Function:
    case ezPropertyCategory::Constant:
      break; // nothing to do
  }
}
