#include "sound_session.h"
#include "compiler.h"
#include "engine.h"
#include "engine_catalog.h"
#include "exhaust_system.h"
#include "impulse_response.h"
#include "simulator.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <unistd.h>

namespace {
struct TemporaryEntrypoint {
    std::filesystem::path path;
    ~TemporaryEntrypoint() { std::error_code ignored;if(!path.empty())std::filesystem::remove(path,ignored); }
    std::filesystem::path wrap(const std::filesystem::path &script) {
        std::string name=(std::filesystem::temp_directory_path()/"engine-sound-XXXXXX.mr").string();
        int descriptor=mkstemps(name.data(),3);
        if(descriptor<0)throw std::runtime_error("Cannot create the engine entrypoint");
        ::close(descriptor);path=name;
        std::ofstream file(path);
        file<<"import "<<std::quoted(script.generic_string())<<"\n\nmain()\n";
        file.close();
        if(!file)throw std::runtime_error("Cannot write the engine entrypoint");
        return path;
    }
};
}

struct SoundSession::Impl {
    Engine *engine = nullptr;
    Vehicle *vehicle = nullptr;
    Transmission *transmission = nullptr;
    Simulator *simulator = nullptr;
    AudioEngineRunner runner;
    SdlAudioOutput output;
    ~Impl() {
        // Finish the device callback before destroying its source.
        output.stop();
        runner.stop();
        if (simulator) { simulator->releaseSimulation(); delete simulator; }
        if (engine) { engine->destroy(); delete engine; }
        delete vehicle;
        delete transmission;
    }
};

const std::vector<SoundSession::Preset> &SoundSession::presets() {
    static const std::vector<Preset> presets=[] {
        std::vector<Preset> result{
            {"supra", "Toyota Supra 2JZ", "Straight-six", "audio_supra.mr", .02, .02},
            {"ls", "GM LS V8", "Cross-plane V8", "audio_ls.mr", 0, .15}
        };
        for(const auto &entry:engineCatalog()) {
            if(entry.relativeScriptPath.find("03_2jz.mr")!=std::string::npos ||
               entry.relativeScriptPath.find("07_gm_ls.mr")!=std::string::npos)continue;
            std::string id=std::filesystem::path(entry.relativeScriptPath).stem().string();
            const auto prefix=id.find('_');
            if(prefix!=std::string::npos && prefix>0 &&
               std::all_of(id.begin(),id.begin()+prefix,[](unsigned char c){return std::isdigit(c);}))
                id.erase(0,prefix+1);
            Preset preset{id,entry.name,entry.group,entry.relativeScriptPath,0,.05,true};
            if(id=="ferrari_f136_v8") { preset.title="Ferrari F136 V8";preset.detail="Flat-plane V8"; }
            else if(id=="ferrari_412_t2") { preset.title="Ferrari 412 T2 V12";preset.detail="Formula 1 V12";preset.simulationHz=5000; }
            else if(id=="lfa_v10") { preset.title="Lexus LFA V10";preset.detail="V10"; }
            else if(id=="bmw_m52b28") { preset.title="BMW M52B28";preset.detail="Straight-six"; }
            else if(id=="porsche_911_gt3") {
                preset.title="Porsche 911 GT3 4.0";preset.detail="Flat-six / approximate model";preset.simulationHz=5000;
            }
            else if(id=="porsche_911_carrera_32") {
                preset.title="Porsche 911 Carrera 3.2";preset.detail="Flat-six / approximate model";
            }
            result.push_back(std::move(preset));
        }
        std::sort(result.begin()+2,result.end(),[](const Preset &a,const Preset &b){return a.title<b.title;});
        return result;
    }();
    return presets;
}

SoundSession::SoundSession() = default;
SoundSession::~SoundSession() = default;

void SoundSession::close() {
    m_ready = false;
    m_impl.reset();
}

bool SoundSession::open(std::size_t presetIndex, const std::filesystem::path &assets, double volume) {
    close();
    m_error.clear();
    if (presetIndex >= presets().size() || !std::isfinite(volume) || volume < 0 || volume > 1) {
        m_error = "Invalid engine or volume";
        return false;
    }
    m_preset = presetIndex;
    m_impl = std::make_unique<Impl>();
    auto &impl = *m_impl;
    try {
        es_script::Compiler compiler;
        compiler.initialize(assets.string());
        TemporaryEntrypoint entrypoint;
        const auto scriptPath = assets / preset().script;
        const auto path = preset().needsEntrypoint ? entrypoint.wrap(scriptPath) : scriptPath;
        if (!compiler.compile(path.string())) {
            compiler.destroy();
            throw std::runtime_error("Could not compile " + path.string());
        }
        const auto script = compiler.execute();
        compiler.destroy();
        impl.engine = script.engine;
        impl.vehicle = script.vehicle;
        impl.transmission = script.transmission;
        if (!impl.engine || !impl.vehicle || !impl.transmission)
            throw std::runtime_error("The preset did not create an engine, vehicle and transmission");
        impl.engine->calculateDisplacement();
        m_engineName = impl.engine->getName();
        m_redline = impl.engine->getRedline() / units::rpm(1);
        m_defaultRoughness = impl.engine->getInitialJitter();
        impl.simulator = impl.engine->createSimulator(impl.vehicle, impl.transmission, 44100);
        impl.simulator->setSimulationFrequency(preset().simulationHz);
        impl.simulator->m_dyno.m_rotationSpeed = units::rpm(1000);
        m_defaultHighFrequency=impl.engine->getInitialHighFrequencyGain();
        m_defaultLowNoise=impl.engine->getInitialNoise();
        // Immutable geometry is copied once, before the worker owns physics.
        auto &layout=m_visualLayout;layout={};
        layout.bankCount=impl.engine->getCylinderBankCount();
        layout.cylinderCount=impl.engine->getCylinderCount();
        layout.crankCount=impl.engine->getCrankshaftCount();
        if(layout.bankCount>VisualMaxBanks || layout.cylinderCount>VisualMaxCylinders || layout.crankCount>VisualMaxCranks)
            throw std::runtime_error("Engine exceeds the visual snapshot capacity");
        layout.displacementLiters=impl.engine->getDisplacement()/units::L;
        layout.tireRadius=impl.vehicle->getTireRadius();
        for(int i=0;i<layout.bankCount;++i) {
            const auto *bank=impl.engine->getCylinderBank(i);auto *head=impl.engine->getHead(i);
            auto &b=layout.banks[i];
            b.x=bank->getX();b.y=bank->getY();b.dx=bank->getDx();b.dy=bank->getDy();
            b.angle=bank->getAngle();b.bore=bank->getBore();b.deck=bank->getDeckHeight();
            b.chamberHeight=head->getCombustionChamberVolume()/(3.141592653589793*b.bore*b.bore/4);
            b.displayDepth=bank->getDisplayDepth();b.cylinders=bank->getCylinderCount();b.flip=head->getFlipDisplay();
            auto *intake=head->getIntakeCamshaft(),*exhaust=head->getExhaustCamshaft();
            b.intakeBaseRadius=intake->getBaseRadius();b.exhaustBaseRadius=exhaust->getBaseRadius();
            // Same roller-envelope construction as GeometryGenerator::generateCam,
            // evaluated once into a small, immutable contour.
            const auto contour=[](Camshaft *cam,auto &points) {
                constexpr double pi=3.141592653589793,roller=.00762;
                std::array<EngineVisualLayout::Point,64> centers,edge;
                for(int j=0;j<64;++j) {
                    const double angle=j*2*pi/64;
                    const double radius=cam->getBaseRadius()+roller+cam->sampleLobe(angle-pi);
                    centers[j]={float(std::cos(angle+pi/2)*radius),float(std::sin(angle+pi/2)*radius)};
                }
                for(int j=0;j<64;++j) {
                    const auto &p=centers[(j+63)%64],&q=centers[j];
                    const double dx=q.x-p.x,dy=q.y-p.y,length=std::hypot(dx,dy);
                    edge[j]={float(q.x-dy/length*roller),float(q.y+dx/length*roller)};
                }
                for(int j=0;j<64;++j) {
                    const auto &p=edge[(j+63)%64],&q=edge[(j+1)%64];
                    points[j]={(p.x+q.x)*.5f,(p.y+q.y)*.5f};
                }
            };
            contour(intake,b.intakeCam);contour(exhaust,b.exhaustCam);
        }
        for(int i=0;i<layout.crankCount;++i) {
            auto *crank=impl.engine->getCrankshaft(i);auto &c=layout.cranks[i];
            c.x=crank->getPosX();c.y=crank->getPosY();c.radius=crank->getThrow();
            c.journals=std::min(crank->getRodJournalCount(),VisualMaxCylinders);
            for(int j=0;j<c.journals;++j)c.journalAngles[j]=crank->getRodJournalAngle(j);
        }
        for(int i=0;i<layout.cylinderCount;++i) {
            const auto *piston=impl.engine->getPiston(i);const auto *rod=piston->getRod();auto &c=layout.cylinders[i];
            c.bank=piston->getCylinderBank()->getIndex();c.index=piston->getCylinderIndex();c.layer=rod->getLayer();
            c.compression=piston->getCompressionHeight();c.wrist=piston->getWristPinLocation();
            c.rodBig=rod->getBigEndLocal();c.rodLittle=rod->getLittleEndLocal();c.rodLength=rod->getLength();
            for(int j=0;j<layout.crankCount;++j)if(impl.engine->getCrankshaft(j)==rod->getCrankshaft())c.crank=j;
            layout.maxLayer=std::max(layout.maxLayer,c.layer);
        }
        auto audio = impl.simulator->synthesizer().getAudioParameters();
        audio.dF_F_mix = impl.engine->getInitialHighFrequencyGain();
        audio.inputSampleNoise = m_defaultRoughness;
        audio.airNoise = impl.engine->getInitialNoise();
        audio.volume = volume;
        impl.simulator->synthesizer().setAudioParameters(audio);
        for (int i = 0; i < impl.engine->getExhaustSystemCount(); ++i) {
            const auto *ir = impl.engine->getExhaustSystem(i)->getImpulseResponse();
            if (ir && !impl.output.loadImpulseResponse(impl.simulator->synthesizer(),
                    ir->getFilename(), ir->getVolume(), i))
                throw std::runtime_error("Could not load exhaust sound: " + ir->getFilename());
        }
        impl.runner.enableVisualTelemetry(true);
        if (!impl.runner.start(*impl.simulator))
            throw std::runtime_error("Could not start the audio producer");
        impl.output.enableVisualization(true);
        if (!impl.output.start(impl.simulator))
            throw std::runtime_error(std::string("Could not open audio output: ") + SDL_GetError());
        const char *name = SDL_GetAudioDeviceName(impl.output.device());
        m_deviceName = name ? name : "System output";
        m_ready = true;
        return true;
    } catch (const std::exception &error) {
        m_error = error.what();
        close();
        return false;
    }
}

bool SoundSession::command(AudioEngineRunner::Action action, double value) {
    return m_ready && std::isfinite(value) && m_impl->runner.command({action, value});
}
bool SoundSession::startEngine() { return command(AudioEngineRunner::Action::Start, preset().startThrottle); }
bool SoundSession::rev() { return command(AudioEngineRunner::Action::Blip, preset().revThrottle); }
AudioEngineRunner::Snapshot SoundSession::snapshot() const { return m_impl ? m_impl->runner.snapshot() : AudioEngineRunner::Snapshot{}; }
bool SoundSession::vehicleTelemetry(AudioEngineRunner::VehicleTelemetry &value) const {return m_ready && m_impl->runner.vehicleTelemetry(value);}
void SoundSession::setRoadDeceleration(double value) {if(m_ready)m_impl->runner.setRoadDeceleration(value);}
SdlAudioOutput::Statistics SoundSession::statistics() const { return m_impl ? m_impl->output.statistics() : SdlAudioOutput::Statistics{}; }
SDL_AudioDeviceID SoundSession::device() const { return m_ready ? m_impl->output.device() : 0; }
bool SoundSession::readVisualization(SdlAudioOutput::VisualSamples &samples) {
    return m_ready && m_impl->output.readVisualization(samples);
}
bool SoundSession::readEngineVisualization(EngineVisualSnapshot &snapshot) {
    return m_ready && m_impl->runner.readVisualTelemetry(snapshot);
}
