#include "plugin.hpp"
Plugin* pluginInstance;
void init (Plugin* p) { pluginInstance = p; p->addModel (modelArpeggio); p->addModel (modelRipple); p->addModel (modelFilter); p->addModel (modelSlider); p->addModel (modelMixer); }
