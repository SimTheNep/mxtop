#include "state.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

stateLayer::stateLayer(const moduleDef& module) : module_(module){


    for (const auto& obj : module_.objects)
    {
        if (obj.type == kind::CC && obj.cc)
            ccIndex_[*obj.cc].push_back(&obj);
    }
}

void stateLayer::eventHandler(const RawEvent& ev){
    if (ev.kind != MsgKind::CC)
        return;

    uint8_t ccNumber = ev.data[1];
    uint8_t ccValue  = ev.data[2];

    for (const ModuleObject* obj : ccIndex_[ccNumber])
        printf("%s ch=%d val=%d\n", obj->id.c_str(), ev.channel, ccValue);
}

int main(){ // Scratch
    ModuleObject volumeObj;
    volumeObj.id = "volume";
    volumeObj.type = kind::CC;
    volumeObj.cc = 7;

    ModuleObject sustainObj;
    sustainObj.id = "sustain";
    sustainObj.type = kind::CC;
    sustainObj.cc = 64;

    moduleDef module;
    module.objects = { volumeObj, sustainObj };

    stateLayer state(module);

    RawEvent fakeCC;
    fakeCC.kind = MsgKind::CC;
    fakeCC.channel = 0;
    fakeCC.data = { 0xB0, 64, 100 };

    state.eventHandler(fakeCC);
}