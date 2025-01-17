// Copyright (c) Meta Platforms, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "SceneInstanceAttributesManager.h"

#include <Corrade/Utility/FormatStl.h>

#include <utility>
#include "esp/metadata/MetadataUtils.h"
#include "esp/physics/RigidBase.h"

#include "esp/io/Json.h"

namespace esp {
namespace metadata {

using attributes::SceneAOInstanceAttributes;
using attributes::SceneInstanceAttributes;
using attributes::SceneObjectInstanceAttributes;

namespace managers {

SceneInstanceAttributes::ptr SceneInstanceAttributesManager::createObject(
    const std::string& sceneInstanceHandle,
    bool registerTemplate) {
  std::string msg;
  SceneInstanceAttributes::ptr attrs = this->createFromJsonOrDefaultInternal(
      sceneInstanceHandle, msg, registerTemplate);

  if (nullptr != attrs) {
    ESP_DEBUG(Mn::Debug::Flag::NoSpace)
        << msg << " scene instance attributes created"
        << (registerTemplate ? " and registered." : ".");
  }
  return attrs;
}  // SceneInstanceAttributesManager::createObject

SceneInstanceAttributes::ptr
SceneInstanceAttributesManager::initNewObjectInternal(
    const std::string& sceneInstanceHandle,
    bool) {
  SceneInstanceAttributes::ptr newAttributes =
      this->constructFromDefault(sceneInstanceHandle);
  if (nullptr == newAttributes) {
    newAttributes = SceneInstanceAttributes::create(sceneInstanceHandle);
  }
  // set the attributes source filedirectory, from the attributes name
  this->setFileDirectoryFromHandle(newAttributes);

  // any internal default configuration here
  return newAttributes;
}  // SceneInstanceAttributesManager::initNewObjectInternal

void SceneInstanceAttributesManager::setValsFromJSONDoc(
    SceneInstanceAttributes::ptr attribs,
    const io::JsonGenericValue& jsonConfig) {
  const std::string attribsDispName = attribs->getSimplifiedHandle();
  // Check for translation origin.  Default to unknown.
  attribs->setTranslationOrigin(getTranslationOriginVal(jsonConfig));

  // Check for stage instance existence
  if (jsonConfig.HasMember("stage_instance")) {
    if (jsonConfig["stage_instance"].IsObject()) {
      attribs->setStageInstance(
          createInstanceAttributesFromJSON(jsonConfig["stage_instance"]));
    } else {
      // stage instance exists but is not a valid JSON Object
      ESP_WARNING(Mn::Debug::Flag::NoSpace)
          << "Stage instance issue in Scene Instance `" << attribsDispName
          << "`: JSON cell `stage_instance` is not a valid JSON object.";
    }
  } else {
    // No stage_instance specified in SceneInstance configuration.
    // We expect a scene instance to be present always, except for the default
    // Scene Dataset that is empty.
    if (attribsDispName == "default_attributes") {
      // Default attributes is empty
      ESP_DEBUG(Mn::Debug::Flag::NoSpace)
          << "No Stage Instance specified in Default Scene Instance, so "
             "setting empty/NONE Stage as Stage instance.";
      SceneObjectInstanceAttributes::ptr instanceAttrs =
          createEmptyInstanceAttributes("");
      // Set to use none stage
      instanceAttrs->setHandle("NONE");
      attribs->setStageInstance(instanceAttrs);
    } else {
      // no stage instance exists in Scene Instance config JSON. This should not
      // happen and would indicate an error in the dataset.
      ESP_CHECK(false,
                "No JSON cell `stage_instance` specified in Scene Instance `"
                    << Mn::Debug::nospace << attribsDispName
                    << Mn::Debug::nospace
                    << "` so no Stage can be created for this Scene.");
    }
  }

  // Check for object instances existence
  if (jsonConfig.HasMember("object_instances")) {
    // object_instances tag exists
    if (jsonConfig["object_instances"].IsArray()) {
      const auto& objectArray = jsonConfig["object_instances"];
      for (rapidjson::SizeType i = 0; i < objectArray.Size(); ++i) {
        const auto& objCell = objectArray[i];
        if (objCell.IsObject()) {
          attribs->addObjectInstance(createInstanceAttributesFromJSON(objCell));
        } else {
          ESP_WARNING(Mn::Debug::Flag::NoSpace)
              << "Object instance issue in Scene Instance `" << attribsDispName
              << "` at idx : " << i
              << " : JSON cell within `object_instances` array is not a "
                 "valid JSON object, so skipping entry.";
        }
      }
    } else {
      // object_instances tag exists but is not an array. should warn (perhaps
      // error?)
      ESP_WARNING(Mn::Debug::Flag::NoSpace)
          << "Object instances issue in Scene Instance `" << attribsDispName
          << "`: JSON cell `object_instances` is not a valid JSON "
             "array, so no object instances loaded.";
    }
  } else {
    // No object_instances tag exists in scene instance. Not necessarily a bad
    // thing, not all datasets have objects
    ESP_DEBUG(Mn::Debug::Flag::NoSpace)
        << "No Objects specified in Scene Instance `" << attribsDispName
        << "`: JSON cell `object_instances` does not exist.";
  }

  // Check for articulated object instances existence
  if (jsonConfig.HasMember("articulated_object_instances")) {
    // articulated_object_instances tag exists
    if (jsonConfig["articulated_object_instances"].IsArray()) {
      const auto& articulatedObjArray =
          jsonConfig["articulated_object_instances"];
      for (rapidjson::SizeType i = 0; i < articulatedObjArray.Size(); ++i) {
        const auto& artObjCell = articulatedObjArray[i];

        if (artObjCell.IsObject()) {
          attribs->addArticulatedObjectInstance(
              createAOInstanceAttributesFromJSON(artObjCell));
        } else {
          ESP_WARNING(Mn::Debug::Flag::NoSpace)
              << "Articulated Object specification error in Scene Instance `"
              << attribsDispName << "` at idx : " << i
              << ": JSON cell within `articulated_object_instances` array is "
                 "not a valid JSON object, so skipping entry.";
        }
      }
    } else {
      // articulated_object_instances tag exists but is not an array. should
      // warn (perhaps error?)
      ESP_WARNING(Mn::Debug::Flag::NoSpace)
          << "Articulated Object instances issue in Scene "
             "InstanceScene Instance `"
          << attribsDispName
          << "`: JSON cell `articulated_object_instances` is not a valid JSON "
             "array, so no articulated object instances loaded.";
    }
  } else {
    // No articulated_object_instances tag exists in scene instance. Not
    // necessarily a bad thing, not all datasets have articulated objects
    ESP_DEBUG(Mn::Debug::Flag::NoSpace)
        << "No Articulated Objects specified for sceneScene Instance `"
        << attribsDispName
        << "`: JSON cell `articulated_object_instances` does not exist.";
  }

  std::string dfltLighting = "";
  if (io::readMember<std::string>(jsonConfig, "default_lighting",
                                  dfltLighting)) {
    // if "default lighting" is specified in scene json set value.
    attribs->setLightingHandle(dfltLighting);
  } else {
    ESP_DEBUG(Mn::Debug::Flag::NoSpace)
        << "No default_lighting specified for Scene Instance `"
        << attribsDispName << "`.";
  }

  std::string navmeshName = "";
  if (io::readMember<std::string>(jsonConfig, "navmesh_instance",
                                  navmeshName)) {
    // if "navmesh_instance" is specified in scene json set value.
    attribs->setNavmeshHandle(navmeshName);
  } else {
    ESP_DEBUG(Mn::Debug::Flag::NoSpace)
        << "No navmesh_instance specified for Scene Instance `"
        << attribsDispName << "`.";
  }

  std::string semanticDesc = "";
  if (io::readMember<std::string>(jsonConfig, "semantic_scene_instance",
                                  semanticDesc)) {
    // if "semantic scene instance" is specified in scene json set value.
    attribs->setSemanticSceneHandle(semanticDesc);
  } else {
    ESP_DEBUG(Mn::Debug::Flag::NoSpace)
        << "No semantic_scene_instance specified for Scene Instance `"
        << attribsDispName << "`.";
  }
  // check for user defined attributes
  this->parseUserDefinedJsonVals(attribs, jsonConfig);

}  // SceneInstanceAttributesManager::setValsFromJSONDoc

SceneObjectInstanceAttributes::ptr
SceneInstanceAttributesManager::createInstanceAttributesFromJSON(
    const io::JsonGenericValue& jCell) {
  SceneObjectInstanceAttributes::ptr instanceAttrs =
      createEmptyInstanceAttributes("");
  // populate attributes
  this->setAbstractObjectAttributesFromJson(instanceAttrs, jCell);
  return instanceAttrs;
}  // SceneInstanceAttributesManager::createInstanceAttributesFromJSON

SceneAOInstanceAttributes::ptr
SceneInstanceAttributesManager::createAOInstanceAttributesFromJSON(
    const io::JsonGenericValue& jCell) {
  SceneAOInstanceAttributes::ptr instanceAttrs =
      createEmptyAOInstanceAttributes("");
  // populate attributes
  this->setAbstractObjectAttributesFromJson(instanceAttrs, jCell);

  // only used for articulated objects
  // fixed base
  io::jsonIntoSetter<bool>(jCell, "fixed_base",
                           [instanceAttrs](bool fixed_base) {
                             instanceAttrs->setFixedBase(fixed_base);
                           });

  // only used for articulated objects
  // auto clamp joint limits
  io::jsonIntoSetter<bool>(
      jCell, "auto_clamp_joint_limits",
      [instanceAttrs](bool auto_clamp_joint_limits) {
        instanceAttrs->setAutoClampJointLimits(auto_clamp_joint_limits);
      });

  // only used for articulated objects
  // initial joint pose
  if (jCell.HasMember("initial_joint_pose")) {
    if (jCell["initial_joint_pose"].IsArray()) {
      std::vector<float> poseRes;
      // read values into vector
      io::readMember<float>(jCell, "initial_joint_pose", poseRes);
      int i = 0;
      for (const float& v : poseRes) {
        const std::string key = Cr::Utility::formatString("joint_{:.02d}", i++);
        instanceAttrs->addInitJointPoseVal(key, v);
      }

    } else if (jCell["initial_joint_pose"].IsObject()) {
      // load values into map
      io::readMember<std::map<std::string, float>>(
          jCell, "initial_joint_pose", instanceAttrs->copyIntoInitJointPose());
    } else {
      ESP_WARNING() << ": Unknown format for "
                       "initial_joint_pose specified for instance"
                    << instanceAttrs->getHandle()
                    << "in Scene Instance File, so no values are set.";
    }
  }
  // only used for articulated objects
  // initial joint velocities
  if (jCell.HasMember("initial_joint_velocities")) {
    if (jCell["initial_joint_velocities"].IsArray()) {
      std::vector<float> poseRes;
      // read values into vector
      io::readMember<float>(jCell, "initial_joint_velocities", poseRes);
      int i = 0;
      for (const float& v : poseRes) {
        const std::string key = Cr::Utility::formatString("joint_{:.02d}", i++);
        instanceAttrs->addInitJointVelocityVal(key, v);
      }

    } else if (jCell["initial_joint_velocities"].IsObject()) {
      // load values into map
      io::readMember<std::map<std::string, float>>(
          jCell, "initial_joint_velocities",
          instanceAttrs->copyIntoInitJointVelocities());
    } else {
      ESP_WARNING() << ": Unknown format for "
                       "initial_joint_velocities specified for instance"
                    << instanceAttrs->getHandle()
                    << "in Scene Instance File, so no values are set.";
    }
  }
  return instanceAttrs;

}  // SceneInstanceAttributesManager::createAOInstanceAttributesFromJSON

void SceneInstanceAttributesManager::setAbstractObjectAttributesFromJson(
    const attributes::SceneObjectInstanceAttributes::ptr& instanceAttrs,
    const io::JsonGenericValue& jCell) const {
  // template handle describing stage/object instance
  io::jsonIntoConstSetter<std::string>(
      jCell, "template_name",
      [instanceAttrs](const std::string& template_name) {
        instanceAttrs->setHandle(template_name);
      });

  // Check for translation origin override for a particular instance.  Default
  // to unknown, which will mean use scene instance-level default.
  instanceAttrs->setTranslationOrigin(getTranslationOriginVal(jCell));

  // set specified shader type value.  May be Unknown, which means the default
  // value specified in the stage or object attributes will be used.
  instanceAttrs->setShaderType(getShaderTypeFromJsonDoc(jCell));

  // motion type of object.  Ignored for stage.
  std::string tmpVal = "";
  if (io::readMember<std::string>(jCell, "motion_type", tmpVal)) {
    // motion type tag was found, perform check - first convert to lowercase
    std::string strToLookFor = Cr::Utility::String::lowercase(tmpVal);
    auto found = attributes::MotionTypeNamesMap.find(strToLookFor);
    if (found != attributes::MotionTypeNamesMap.end()) {
      // only set value if specified in json
      instanceAttrs->setMotionType(strToLookFor);
    } else {
      ESP_WARNING(Mn::Debug::Flag::NoSpace)
          << ": motion_type value in json  : `" << tmpVal << "`|`"
          << strToLookFor
          << "` does not map to a valid physics::MotionType value, so not "
             "setting instance motion type value.";
    }
  }

  // translation from origin
  io::jsonIntoConstSetter<Magnum::Vector3>(
      jCell, "translation",
      [instanceAttrs](const Magnum::Vector3& translation) {
        instanceAttrs->setTranslation(translation);
      });

  // orientation TODO : support euler angles too?
  io::jsonIntoConstSetter<Magnum::Quaternion>(
      jCell, "rotation", [instanceAttrs](const Magnum::Quaternion& rotation) {
        instanceAttrs->setRotation(rotation);
      });

  // uniform scaling for instance
  io::jsonIntoSetter<double>(jCell, "uniform_scale",
                             [instanceAttrs](double uniform_scale) {
                               instanceAttrs->setUniformScale(uniform_scale);
                             });

  // non-uniform scaling for instance
  io::jsonIntoConstSetter<Magnum::Vector3>(
      jCell, "non_uniform_scale",
      [instanceAttrs](const Magnum::Vector3& non_uniform_scale) {
        instanceAttrs->setNonUniformScale(non_uniform_scale);
      });

  // whether particular instance is visible or not - only modify if actually
  // present in instance json
  io::jsonIntoSetter<bool>(
      jCell, "is_instance_visible", [instanceAttrs](bool is_instance_visible) {
        instanceAttrs->setIsInstanceVisible(is_instance_visible);
      });

  // mass scaling for instance
  io::jsonIntoSetter<double>(jCell, "mass_scale",
                             [instanceAttrs](double mass_scale) {
                               instanceAttrs->setMassScale(mass_scale);
                             });

  // check for user defined attributes
  this->parseUserDefinedJsonVals(instanceAttrs, jCell);

}  // SceneInstanceAttributesManager::setAbstractObjectAttributesFromJson

std::string SceneInstanceAttributesManager::getTranslationOriginVal(
    const io::JsonGenericValue& jsonDoc) const {
  // Check for translation origin.  Default to unknown.
  std::string transOrigin = getTranslationOriginName(
      attributes::SceneInstanceTranslationOrigin::Unknown);
  std::string tmpTransOriginVal = "";
  if (io::readMember<std::string>(jsonDoc, "translation_origin",
                                  tmpTransOriginVal)) {
    // translation_origin tag was found, perform check - first convert to
    // lowercase
    std::string strToLookFor =
        Cr::Utility::String::lowercase(tmpTransOriginVal);
    auto found = attributes::InstanceTranslationOriginMap.find(strToLookFor);
    if (found != attributes::InstanceTranslationOriginMap.end()) {
      transOrigin = std::move(tmpTransOriginVal);
    } else {
      ESP_WARNING(Mn::Debug::Flag::NoSpace)
          << ": translation_origin value in json :`" << tmpTransOriginVal
          << "`|`" << strToLookFor
          << "` does not map to a valid SceneInstanceTranslationOrigin "
             "value, so defaulting translation origin to "
             "SceneInstanceTranslationOrigin::Unknown.";
    }
  }
  return transOrigin;
}  // SceneInstanceAttributesManager::getTranslationOriginVal

int SceneInstanceAttributesManager::registerObjectFinalize(
    SceneInstanceAttributes::ptr sceneInstanceAttributes,
    const std::string& sceneInstanceAttributesHandle,
    bool) {
  // adds template to library, and returns either the ID of the existing
  // template referenced by sceneInstanceAttributesHandle, or the next
  // available ID if not found.
  int datasetTemplateID = this->addObjectToLibrary(
      std::move(sceneInstanceAttributes), sceneInstanceAttributesHandle);
  return datasetTemplateID;
}  // SceneInstanceAttributesManager::registerObjectFinalize

}  // namespace managers
}  // namespace metadata
}  // namespace esp
