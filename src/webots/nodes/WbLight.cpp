// Copyright 1996-2018 Cyberbotics Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "WbLight.hpp"

#include "WbField.hpp"
#include "WbFieldChecker.hpp"
#include "WbMFColor.hpp"
#include "WbNodeUtilities.hpp"
#include "WbPerspective.hpp"
#include "WbPreferences.hpp"
#include "WbSFBool.hpp"
#include "WbSFColor.hpp"
#include "WbSFDouble.hpp"
#include "WbTransform.hpp"
#include "WbWorld.hpp"
#include "WbWrenRenderingContext.hpp"

#include <wren/scene.h>

#include <QtCore/QList>

QList<const WbLight *> WbLight::cLights;

void WbLight::init() {
  mInitialColor = WbRgb();

  mAmbientIntensity = findSFDouble("ambientIntensity");
  mColor = findSFColor("color");
  mIntensity = findSFDouble("intensity");
  mOn = findSFBool("on");
  mCastShadows = findSFBool("castShadows");
  mCastLensFlares = findSFBool("castLensFlares");
}

WbLight::WbLight(const WbLight &other) : WbBaseNode(other) {
  init();
}

WbLight::WbLight(const WbNode &other) : WbBaseNode(other) {
  init();
}

WbLight::WbLight(const QString &modelName, WbTokenizer *tokenizer) : WbBaseNode(modelName, tokenizer) {
  init();
}

void WbLight::preFinalize() {
  WbBaseNode::preFinalize();

  cLights << this;

  updateAmbientIntensity();
  updateColor();
  updateIntensity();
  updateOn();
  updateCastShadows();

  mInitialColor = mColor->value();
}

void WbLight::postFinalize() {
  WbBaseNode::postFinalize();

  connect(mAmbientIntensity, &WbSFDouble::changed, this, &WbLight::updateAmbientIntensity);
  connect(mColor, &WbSFColor::changed, this, &WbLight::updateColor);
  connect(mIntensity, &WbSFDouble::changed, this, &WbLight::updateIntensity);
  connect(mOn, &WbSFBool::changed, this, &WbLight::updateOn);
  connect(mCastShadows, &WbSFBool::changed, this, &WbLight::updateCastShadows);

  if (!WbWorld::instance()->isLoading())
    emit WbWrenRenderingContext::instance()->numberOfOnLightsChanged();
}

WbLight::~WbLight() {
  if (areWrenObjectsInitialized()) {
    cLights.removeOne(this);
    applySceneAmbientColorToWren();
    if (!WbWorld::instance()->isCleaning())
      emit WbWrenRenderingContext::instance()->numberOfOnLightsChanged();
  }
}

bool WbLight::isOn() const {
  return mOn->value();
}

bool WbLight::castShadows() const {
  return mCastShadows->value();
}

bool WbLight::castLensFlares() const {
  return mCastLensFlares->value();
}

double WbLight::intensity() const {
  return mIntensity->value();
}

double WbLight::ambientIntensity() const {
  return mAmbientIntensity->value();
}

void WbLight::setAmbientIntensity(double value) {
  mAmbientIntensity->setValue(value);
}

const WbRgb &WbLight::color() const {
  return mColor->value();
}

void WbLight::setColor(const WbRgb &color) {
  mColor->setValue(color);
}

void WbLight::toggleOn(bool on) {
  mOn->setValue(on);
}

int WbLight::numberOfOnLights() {
  int n, i;
  for (i = 0, n = 0; i < cLights.count(); ++i) {
    if (cLights[i]->isOn())
      ++n;
  }

  return n;
}

void WbLight::createWrenObjects() {
  WbBaseNode::createWrenObjects();

  applyLightColorToWren();
  applyLightIntensityToWren();
  applyLightVisibilityToWren();
  applyLightShadowsToWren();
  applySceneAmbientColorToWren();

  connect(WbPreferences::instance(), &WbPreferences::changedByUser, this, &WbLight::updateCastShadows);
}

void WbLight::updateAmbientIntensity() {
  if (WbFieldChecker::checkDoubleInRangeWithIncludedBounds(this, mAmbientIntensity, 0.0, 1.0,
                                                           mAmbientIntensity->value() > 1.0 ? 1.0 : 0.0))
    return;

  if (areWrenObjectsInitialized())
    applySceneAmbientColorToWren();
}

void WbLight::updateIntensity() {
  if (WbFieldChecker::checkDoubleIsNonNegative(this, mIntensity, 1.0))
    return;

  if (areWrenObjectsInitialized())
    applyLightIntensityToWren();

  emit intensityChanged();
}

void WbLight::updateOn() {
  if (areWrenObjectsInitialized()) {
    applyLightVisibilityToWren();

    if (!WbWorld::instance()->isLoading())
      emit WbWrenRenderingContext::instance()->numberOfOnLightsChanged();
  }

  emit isOnChanged();
}

void WbLight::updateColor() {
  if (WbFieldChecker::checkColorIsValid(this, mColor))
    return;

  if (areWrenObjectsInitialized())
    applyLightColorToWren();

  emit colorChanged();
}

void WbLight::updateCastShadows() {
  if (areWrenObjectsInitialized())
    applyLightShadowsToWren();

  if (!WbWorld::instance()->isLoading() && numberOfLightsCastingShadows() < 2)
    emit WbWrenRenderingContext::instance()->shadowsStateChanged();
}

void WbLight::applySceneAmbientColorToWren() {
  computeAmbientLight();
}

void WbLight::computeAmbientLight() {
  float rgb[] = {0.0f, 0.0f, 0.0f};

  foreach (const WbLight *light, cLights) {
    if (light->isOn()) {
      rgb[0] += light->ambientIntensity() * light->color().red();
      rgb[1] += light->ambientIntensity() * light->color().green();
      rgb[2] += light->ambientIntensity() * light->color().blue();
    }
  }

  wr_scene_set_ambient_light(rgb);
}

int WbLight::numberOfLightsCastingShadows() {
  int counter = 0;
  foreach (const WbLight *light, cLights)
    if (light->isOn() && light->castShadows())
      counter++;
  return counter;
}

void WbLight::exportNodeFields(WbVrmlWriter &writer) const {
  if (writer.isWebots()) {
    WbBaseNode::exportNodeFields(writer);
    return;
  }

  if (writer.isX3d()) {
    const WbNode *n = this;
    while (n && !n->isWorldRoot()) {
      if (n->isDefNode() && n->useCount() > 0) {
        warn("DEF/USE mechanism for light nodes could not work in some X3D viewers, like X3DOM.");
        break;
      }
      n = n->parent();
    }
  }

  findField("on", true)->write(writer);
  findField("color", true)->write(writer);
  findField("intensity", true)->write(writer);
  findField("ambientIntensity", true)->write(writer);
  if (writer.isX3d() && castShadows()) {
    QHash<QString, QString> x3dExportParameters = WbWorld::instance()->perspective()->x3dExportParameters();
    if (x3dExportParameters.contains("shadowIntensity"))
      writer << " shadowIntensity=\'" << x3dExportParameters.value("shadowIntensity") << "\'";
    else
      writer << " shadowIntensity=\'" << defaultX3dShadowsParameter("shadowIntensity") << "\'";
    if (x3dExportParameters.contains("shadowMapSize"))
      writer << " shadowMapSize=\'" << x3dExportParameters.value("shadowMapSize") << "\'";
    else
      writer << " shadowMapSize=\'" << defaultX3dShadowsParameter("shadowMapSize") << "\'";
    if (x3dExportParameters.contains("shadowFilterSize") && !x3dExportParameters.value("shadowFilterSize").isEmpty())
      writer << " shadowFilterSize=\'" << x3dExportParameters.value("shadowFilterSize") << "\'";
    if (x3dExportParameters.contains("shadowsCascades") && !x3dExportParameters.value("shadowsCascades").isEmpty())
      writer << " shadowsCascades=\'" << x3dExportParameters.value("shadowsCascades") << "\'";
  }
}

QString WbLight::defaultX3dShadowsParameter(const QString &parameterName) {
  if (parameterName == "shadowMapSize")
    return "2048";
  else if (parameterName == "shadowFilterSize")
    return "0";
  else if (parameterName == "shadowsCascades")
    return "0";
  else if (parameterName == "shadowIntensity")
    return "0.5";
  return QString();
}
