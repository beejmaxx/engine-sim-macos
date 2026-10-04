#ifndef ENGINE_SIM_CAR_MODELS_H
#define ENGINE_SIM_CAR_MODELS_H
#include <array>
#include <string_view>

namespace sound_ui {
enum class CarBody { Concept, PorscheGt3, SupraMk4, Ferrari458, CorvetteC7, Count };
struct CarModel { const char *directory,*label; };
inline constexpr std::array<CarModel,std::size_t(CarBody::Count)> CarModels{{
    {"concept","CONCEPT BODY"},
    {"porsche_gt3","PORSCHE 911 GT3"},
    {"supra_mk4","TOYOTA SUPRA MK4"},
    {"ferrari_458","FERRARI 458 ITALIA"},
    {"corvette_c7","CORVETTE C7 / LS V8 SWAP"}
}};
inline CarBody carBodyForPreset(std::string_view id) {
    if(id=="porsche_911_gt3" || id=="porsche_911_gt3_sprint")return CarBody::PorscheGt3;
    if(id=="supra")return CarBody::SupraMk4;
    if(id=="ferrari_f136_v8")return CarBody::Ferrari458;
    if(id=="ls")return CarBody::CorvetteC7;
    return CarBody::Concept;
}
inline const CarModel &carModel(CarBody body) {
    const auto index=std::size_t(body);
    return CarModels[index<CarModels.size() ? index : 0];
}
}
#endif
