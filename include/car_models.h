#ifndef ENGINE_SIM_CAR_MODELS_H
#define ENGINE_SIM_CAR_MODELS_H
#include <array>
#include <string_view>

namespace sound_ui {
enum class CarBody {
    Concept, PorscheGt3, SupraMk4, Ferrari458, CorvetteC7, BmwE36, AudiQuattro,
    HondaIntegra, SubaruWrx, LexusLfa, Porsche930, CaterhamSeven, FordHotrod,
    Nissan350z, FerrariF1, Count
};
struct CarModel { const char *directory,*label; };
inline constexpr std::array<CarModel,std::size_t(CarBody::Count)> CarModels{{
    {"concept","CONCEPT BODY"},
    {"porsche_gt3","PORSCHE 911 GT3"},
    {"supra_mk4","TOYOTA SUPRA MK4"},
    {"ferrari_458","FERRARI 458 ITALIA"},
    {"corvette_c7","CORVETTE C7 / LS V8 SWAP"},
    {"bmw_e36","BMW E36 M3 / M52B28 SWAP"},
    {"audi_quattro","AUDI QUATTRO"},
    {"honda_integra","HONDA INTEGRA TYPE R / B18C5"},
    {"subaru_wrx_sti","SUBARU IMPREZA WRX STI"},
    {"lexus_lfa","LEXUS LFA"},
    {"porsche_930","PORSCHE 930 / CARRERA 3.2 SWAP"},
    {"caterham_seven","CATERHAM SUPER SEVEN / ENGINE SWAP"},
    {"ford_hotrod","FORD HOT ROD / ENGINE SWAP"},
    {"nissan_350z","NISSAN 350Z / CUSTOM V6"},
    {"ferrari_f1_2019","FERRARI F1 2019 / 412 T2 V12 SWAP"}
}};
struct CarSelection { std::string_view preset;CarBody body;const char *menu; };
inline constexpr std::array<CarSelection,24> CarSelections{{
    {"supra",CarBody::SupraMk4,"Toyota Supra Mk4 / 2JZ"},
    {"ls",CarBody::CorvetteC7,"Chevrolet Corvette C7 / LS V8 swap"},
    {"60_degree_v6",CarBody::Nissan350z,"Nissan 350Z / 60-degree V6 swap"},
    {"audi_i5",CarBody::AudiQuattro,"Audi Quattro / five-cylinder"},
    {"bmw_m52b28",CarBody::BmwE36,"BMW E36 M3 / M52B28 swap"},
    {"even_fire_v6",CarBody::Nissan350z,"Nissan 350Z / even-fire V6 swap"},
    {"ferrari_412_t2",CarBody::FerrariF1,"Ferrari F1 2019 / 412 T2 V12 swap"},
    {"ferrari_f136_v8",CarBody::Ferrari458,"Ferrari 458 Italia / F136 V8"},
    {"harley_davidson_shovelhead",CarBody::CaterhamSeven,"Caterham Seven / Harley V-twin swap"},
    {"hayabusa",CarBody::CaterhamSeven,"Caterham Seven / Hayabusa swap"},
    {"honda_trx520",CarBody::CaterhamSeven,"Caterham Seven / Honda TRX520 swap"},
    {"honda_vtec",CarBody::HondaIntegra,"Honda Integra Type R / B18C5 VTEC"},
    {"kohler_ch750",CarBody::CaterhamSeven,"Caterham Seven / Kohler V-twin swap"},
    {"lfa_v10",CarBody::LexusLfa,"Lexus LFA / V10"},
    {"merlin_v12",CarBody::FordHotrod,"Ford hot rod / Merlin V12 swap"},
    {"odd_fire_v6",CarBody::Nissan350z,"Nissan 350Z / odd-fire V6 swap"},
    {"porsche_911_carrera_32",CarBody::Porsche930,"Porsche 930 / Carrera 3.2 swap"},
    {"porsche_911_gt3",CarBody::PorscheGt3,"Porsche 911 GT3 4.0"},
    {"porsche_911_gt3_sprint",CarBody::PorscheGt3,"Porsche GT3 Sprint"},
    {"radial_5",CarBody::FordHotrod,"Ford hot rod / five-cylinder radial swap"},
    {"radial_9",CarBody::FordHotrod,"Ford hot rod / nine-cylinder radial swap"},
    {"subaru_ej25",CarBody::SubaruWrx,"Subaru WRX STI / EJ25"},
    {"subaru_ej25_eh",CarBody::SubaruWrx,"Subaru WRX STI / equal-length exhaust"},
    {"subaru_ej25_uh",CarBody::SubaruWrx,"Subaru WRX STI / unequal-length exhaust"}
}};
inline const CarSelection *carSelectionForPreset(std::string_view id) {
    for(const auto &selection:CarSelections)if(selection.preset==id)return &selection;
    return nullptr;
}
inline CarBody carBodyForPreset(std::string_view id) {
    if(const auto *selection=carSelectionForPreset(id))return selection->body;
    return CarBody::Concept;
}
inline const CarModel &carModel(CarBody body) {
    const auto index=std::size_t(body);
    return CarModels[index<CarModels.size() ? index : 0];
}
}
#endif
