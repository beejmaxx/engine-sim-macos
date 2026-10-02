#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "runtime_paths.h"
#include "sound_session.h"
#include "sound_metal.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace sound_ui;
namespace {
struct Options {
    std::filesystem::path assets;
    std::string log, verify, benchmark, uiTest;
    int preset=0, stall=1000;
    double seconds=12;
    bool play=false, uncapped=false, offscreen=false, effects=true, silent=false;
};
double now() { return SDL_GetTicksNS()/1e9; }
template<size_t N> void put(std::array<char,N> &target,const std::string &value) { std::snprintf(target.data(),N,"%s",value.c_str()); }
std::string percentile(const std::array<uint64_t,256> &last,const std::array<uint64_t,256> &first,double fraction) {
    uint64_t total=0,accumulated=0;
    for(int i=0;i<256;++i)total+=last[i]-first[i];
    if(!total)return "null";
    for(int i=0;i<255;++i) { accumulated+=last[i]-first[i];if(accumulated>=total*fraction)return std::to_string((i+1)*.05); }
    return "null"; // The final histogram bucket is open-ended, not an upper bound.
}
}

@class SoundApp;
@interface SoundMetalView : NSView {
    NSTrackingArea *_tracking;
    Control _drag, _pressed;
}
@property(nonatomic,weak) SoundApp *controller;
- (void)testClick:(Control)control;
- (void)testSlider:(Control)control fraction:(double)value;
- (void)testKey:(NSString *)key;
- (void)testKeyUp:(NSString *)key;
- (void)testHoldMouse:(BOOL)down;
@end

@interface SoundApp : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate> {
    Options _options;
    NSWindow *_window;
    SoundMetalView *_view;
    NSTimer *_timer;
    NSMenuItem *_engineMenuItem;
    unsigned _heldRev;
    bool _revPending;
    id _activity;
    State _state;
    SoundMetalRenderer _renderer;
    std::shared_ptr<SoundSession> _session;
    std::unique_ptr<SoundSession> _pending;
    std::thread _loader;
    std::atomic<bool> _loadReady;
    bool _closing, _passed, _benchMeasured;
    int _testStage, _testPreset;
    unsigned _expectedCaptures;
    double _loadedAt, _lastLog, _benchAt;
    uint64_t _benchStallFrames;
    SdlAudioOutput::Statistics _benchAudio;
    Metrics _benchMetrics;
    std::ofstream _log;
    int _exitCode;
}
- (instancetype)initWithOptions:(const Options &)options;
- (void)activate:(Control)control;
- (void)slider:(Control)control fraction:(double)fraction;
- (void)hover:(Control)control pressed:(Control)pressed;
- (void)key:(NSString *)key shift:(BOOL)shift;
- (void)holdRev:(BOOL)down source:(unsigned)source;
- (void)cancelHeldRev;
- (NSMenu *)engineMenu;
- (State)currentState;
- (int)exitCode;
@end

@implementation SoundMetalView
- (instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame])) {
        _drag=None;_pressed=None;
        // A layer-hosting view leaves presentation with the render thread.
        // AppKit-managed backing layers couple transactions to its main loop.
        self.layer=[CAMetalLayer layer];self.wantsLayer=YES;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)resizeDrawable {
    const CGFloat scale=self.window.backingScaleFactor ?: 2;
    CAMetalLayer *layer=(CAMetalLayer *)self.layer;
    layer.frame=self.bounds;
    layer.contentsScale=scale;
    layer.drawableSize=CGSizeMake(std::max(1.0,self.bounds.size.width*scale),std::max(1.0,self.bounds.size.height*scale));
}
- (void)setFrameSize:(NSSize)size { [super setFrameSize:size];[self resizeDrawable]; }
- (void)viewDidChangeBackingProperties { [self resizeDrawable]; }
- (void)viewDidMoveToWindow { [self resizeDrawable];self.window.acceptsMouseMovedEvents=YES; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if(_tracking)[self removeTrackingArea:_tracking];
    _tracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInActiveApp|NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (NSPoint)logicalPoint:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    return NSMakePoint(p.x*Width/self.bounds.size.width,p.y*Height/self.bounds.size.height);
}
- (void)mouseMoved:(NSEvent *)event {
    NSPoint p=[self logicalPoint:event];[self.controller hover:hit(p.x,p.y) pressed:_pressed];
}
- (void)mouseExited:(NSEvent *)event { (void)event;[self.controller hover:None pressed:_pressed]; }
- (void)mouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    NSPoint p=[self logicalPoint:event];_pressed=hit(p.x,p.y);
    _drag=isSlider(_pressed) ? _pressed : None;
    if(_pressed==Rev)[self.controller holdRev:YES source:2];
    [self.controller hover:_pressed pressed:_pressed];
    if(_drag!=None) { UiRect r=bounds(_drag);[self.controller slider:_drag fraction:(p.x-r.x-7)/(r.w-14)]; }
}
- (void)mouseDragged:(NSEvent *)event {
    if(_drag==None)return;
    NSPoint p=[self logicalPoint:event];UiRect r=bounds(_drag);
    [self.controller slider:_drag fraction:(p.x-r.x-7)/(r.w-14)];
}
- (void)mouseUp:(NSEvent *)event {
    NSPoint p=[self logicalPoint:event];Control released=hit(p.x,p.y);
    if(_pressed==Rev)[self.controller holdRev:NO source:2];
    else if(_drag==None && released==_pressed)[self.controller activate:released];
    _pressed=_drag=None;[self.controller hover:released pressed:None];
}
- (void)keyDown:(NSEvent *)event {
    if(event.modifierFlags & NSEventModifierFlagCommand) { [super keyDown:event];return; }
    NSString *characters=event.charactersIgnoringModifiers;
    const unichar key=characters.length ? [characters characterAtIndex:0] : 0;
    const bool repeatArrow=(key==NSLeftArrowFunctionKey || key==NSRightArrowFunctionKey) && isSlider([self.controller currentState].focus);
    if(!event.isARepeat || repeatArrow)
        [self.controller key:characters shift:(event.modifierFlags & NSEventModifierFlagShift)!=0];
}
- (void)keyUp:(NSEvent *)event {
    if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"r"])
        [self.controller holdRev:NO source:1];
    else [super keyUp:event];
}
- (NSEvent *)eventAt:(NSPoint)point type:(NSEventType)type {
    NSPoint local=NSMakePoint(point.x*self.bounds.size.width/Width,point.y*self.bounds.size.height/Height);
    NSPoint window=[self convertPoint:local toView:nil];
    return [NSEvent mouseEventWithType:type location:window modifierFlags:0 timestamp:now()
        windowNumber:self.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
}
- (void)testClick:(Control)control {
    UiRect r=bounds(control);NSPoint point=NSMakePoint(r.x+r.w/2,r.y+r.h/2);
    [self mouseDown:[self eventAt:point type:NSEventTypeLeftMouseDown]];
    [self mouseUp:[self eventAt:point type:NSEventTypeLeftMouseUp]];
}
- (void)testSlider:(Control)control fraction:(double)value {
    UiRect r=bounds(control);NSPoint point=NSMakePoint(r.x+7+value*(r.w-14),r.y+16);
    [self mouseDown:[self eventAt:point type:NSEventTypeLeftMouseDown]];
    [self mouseUp:[self eventAt:point type:NSEventTypeLeftMouseUp]];
}
- (void)testKey:(NSString *)key {
    [self keyDown:[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:now()
        windowNumber:self.window.windowNumber context:nil characters:key charactersIgnoringModifiers:key isARepeat:NO keyCode:0]];
}
- (void)testKeyUp:(NSString *)key {
    [self keyUp:[NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0 timestamp:now()
        windowNumber:self.window.windowNumber context:nil characters:key charactersIgnoringModifiers:key isARepeat:NO keyCode:0]];
}
- (void)testHoldMouse:(BOOL)down {
    const auto r=bounds(Rev);
    // Release outside the button: it must still release the throttle.
    const auto point=down ? NSMakePoint(r.x+r.w/2,r.y+r.h/2) : NSMakePoint(630,400);
    if(down)[self mouseDown:[self eventAt:point type:NSEventTypeLeftMouseDown]];
    else [self mouseUp:[self eventAt:point type:NSEventTypeLeftMouseUp]];
}
@end

@implementation SoundApp
- (instancetype)initWithOptions:(const Options &)options {
    if((self=[super init])) {
        _options=options;_loadReady=false;_passed=true;
        _benchAudio={};_benchMetrics={};
        _state.preset=options.preset;_state.uncapped=options.uncapped;_state.effects=options.effects;
        _state.engineCount=SoundSession::presets().size();
        _state.volume=SoundSession::DefaultVolume;
        _state.silent=std::string(SDL_GetCurrentAudioDriver())=="dummy";
        _state.automated=!options.uiTest.empty() || !options.benchmark.empty();
        if(!options.log.empty())_log.open(options.log);
    }
    return self;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    const bool hidden=_options.offscreen || !_options.uiTest.empty();
    [NSApp setActivationPolicy:hidden ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
    NSMenu *menu=[NSMenu new],*items=[NSMenu new];NSMenuItem *application=[NSMenuItem new];
    [items addItemWithTitle:@"About Engine Sound" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [items addItem:NSMenuItem.separatorItem];
    [items addItemWithTitle:@"Hide Engine Sound" action:@selector(hide:) keyEquivalent:@"h"];
    [items addItemWithTitle:@"Quit Engine Sound" action:@selector(terminate:) keyEquivalent:@"q"];
    application.submenu=items;[menu addItem:application];
    _engineMenuItem=[[NSMenuItem alloc] initWithTitle:@"Engines" action:nil keyEquivalent:@""];
    _engineMenuItem.submenu=[self engineMenu];[menu addItem:_engineMenuItem];NSApp.mainMenu=menu;
    _window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,Width,Height)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    _window.title=@"Engine Simulator - Metal";_window.delegate=self;_window.releasedWhenClosed=NO;
    _window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _window.contentAspectRatio=NSMakeSize(Width,Height);
    _window.contentMinSize=NSMakeSize(1000,625);
    _window.backgroundColor=[NSColor colorWithSRGBRed:.043 green:.063 blue:.08 alpha:1];
    _view=[[SoundMetalView alloc] initWithFrame:NSMakeRect(0,0,Width,Height)];
    _view.controller=self;_view.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    _window.contentView=_view;
    [_window center];[_window makeFirstResponder:_view];
    if(!hidden) {
        if(_state.automated) { _window.ignoresMouseEvents=YES;[_window orderBack:nil]; }
        else { [_window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES]; }
    }
    if(!_renderer.start((CAMetalLayer *)_view.layer,hidden,_options.assets.c_str())) {
        std::cerr<<_renderer.error()<<'\n';_exitCode=2;[NSApp terminate:nil];return;
    }
    std::cout<<"RENDERER Metal window="<<_window.windowNumber<<" pixels="
        <<((CAMetalLayer *)_view.layer).drawableSize.width<<'x'<<((CAMetalLayer *)_view.layer).drawableSize.height
        <<" offscreen="<<hidden<<" audio_driver="<<SDL_GetCurrentAudioDriver()<<std::endl;
    _timer=[NSTimer timerWithTimeInterval:1.0/60 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    [self loadPreset:_state.preset];
}
- (void)publish { _renderer.update(_state); }
- (State)currentState { return _state; }
- (int)exitCode { return _exitCode; }
- (void)loadPreset:(int)preset {
    if(_closing || _loader.joinable())return;
    [self cancelHeldRev];_heldRev=0;_revPending=false;_state.revHeld=false;
    _state.loading=true;_state.ready=false;_state.ignitionRequested=false;_state.preset=preset;
    _engineMenuItem.submenu=[self engineMenu];
    put(_state.title,SoundSession::presets()[preset].title);
    _state.engine={};_state.waveform.fill(0);_state.throttle=0;_state.missing=0;_state.writeErrors=0;_state.layer=0;_state.dyno=false;_state.dynoRpm=1000;_state.clutch=0;
    put(_state.notice,"Loading engine and exhaust sound...");put(_state.output,"");
    _renderer.connectSession(nullptr);_session.reset();
    if(_activity) { [NSProcessInfo.processInfo endActivity:_activity];_activity=nil; }
    const double volume=_state.volume;
    _loadReady=false;
    _loader=std::thread([self,preset,volume] {
        self->_pending=std::make_unique<SoundSession>();
        self->_pending->open(preset,self->_options.assets,volume);
        self->_loadReady.store(true,std::memory_order_release);
    });
    [self publish];
}
- (BOOL)send:(AudioEngineRunner::Action)action value:(double)value {
    bool sent=_session && _session->command(action,value);
    if(!sent)put(_state.notice,"Control could not be sent. Try again.");
    return sent;
}
- (NSMenu *)engineMenu {
    NSMenu *menu=[[NSMenu alloc] initWithTitle:@"Engines"];menu.delegate=self;
    const auto &presets=SoundSession::presets();
    for(size_t i=0;i<presets.size();++i) {
        if(i==2)[menu addItem:NSMenuItem.separatorItem];
        NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:presets[i].title.c_str()]
            action:@selector(selectEngine:) keyEquivalent:@""];
        item.target=self;item.tag=i;item.state=int(i)==_state.preset ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    return menu;
}
- (void)selectEngine:(NSMenuItem *)item {
    if(item.tag>=0 && item.tag<SoundSession::presets().size())[self loadPreset:int(item.tag)];
}
- (void)flushHeldRev {
    if(_revPending && _session && _session->command(AudioEngineRunner::Action::Throttle,_heldRev ? 1 : 0))
        _revPending=false;
}
- (void)holdRev:(BOOL)down source:(unsigned)source {
    if(down && (!_state.ready || !_state.ignitionRequested))return;
    const unsigned previous=_heldRev;
    _heldRev=down ? (_heldRev|source) : (_heldRev&~source);
    if(bool(previous)==bool(_heldRev))return;
    _state.revHeld=_heldRev!=0;_state.throttle=_heldRev ? 1 : 0;
    _revPending=true;[self flushHeldRev];
    if(_log)_log<<"held_throttle="<<_state.throttle<<std::endl;
    [self publish];
}
- (void)cancelHeldRev { [self holdRev:NO source:3]; }
- (void)windowDidResignKey:(NSNotification *)notification { (void)notification;[self cancelHeldRev]; }
- (void)applicationDidResignActive:(NSNotification *)notification { (void)notification;[self cancelHeldRev]; }
- (void)menuWillOpen:(NSMenu *)menu { (void)menu;[self cancelHeldRev]; }
- (void)activate:(Control)c {
    if(c==None)return;
    _state.focus=c;
    if(c==Effects) { _state.effects=!_state.effects;[self publish];return; }
    if(c==Uncapped) { _state.uncapped=!_state.uncapped;[self publish];return; }
    if(_state.loading)return;
    if(c==Library) {
        [self cancelHeldRev];
        const UiRect r=bounds(Library);
        [[self engineMenu] popUpMenuPositioningItem:nil
            atLocation:NSMakePoint(r.x*_view.bounds.size.width/Width,(r.y+r.h)*_view.bounds.size.height/Height) inView:_view];
        return;
    }
    if(c==Supra || c==Ls) { [self loadPreset:c==Ls ? 1 : 0];return; }
    if(!_state.ready)return;
    switch(c) {
    case LayerBack: _state.layer=std::max(0,_state.layer-1);break;
    case LayerNext: _state.layer=std::min(_session->visualLayout().maxLayer,_state.layer+1);break;
    case Dyno:
        if([self send:AudioEngineRunner::Action::Dyno value:!_state.dyno])_state.dyno=!_state.dyno;break;
    case GearDown: [self send:AudioEngineRunner::Action::Gear value:-1];break;
    case GearUp: [self send:AudioEngineRunner::Action::Gear value:1];break;
    case Start: {
        [self cancelHeldRev];
        bool sent=_state.ignitionRequested ? _session->command(AudioEngineRunner::Action::Stop) : _session->startEngine();
        if(sent) {
            _state.ignitionRequested=!_state.ignitionRequested;_state.throttle=0;
            [self send:AudioEngineRunner::Action::Throttle value:0];
            put(_state.notice,_state.ignitionRequested ? "Hold R: throttle   B: blip   I: idle   Space: stop" : "Engine off. Press Space or Start engine.");
            if(_state.ignitionRequested && !_activity)
                _activity=[NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated reason:@"Live engine audio"];
            if(!_state.ignitionRequested && _activity) { [NSProcessInfo.processInfo endActivity:_activity];_activity=nil; }
        }
        break;
    }
    case Rev: if(_state.ignitionRequested)_session->rev();break;
    case Idle: [self cancelHeldRev];if([self send:AudioEngineRunner::Action::Throttle value:0])_state.throttle=0;break;
    case Mute:
        if([self send:AudioEngineRunner::Action::Volume value:_state.muted ? _state.volume : 0])_state.muted=!_state.muted;
        break;
    case Reset:
        _state.exhaust=1;_state.roughness=_session->defaultRoughness();
        [self send:AudioEngineRunner::Action::ExhaustMix value:1];
        [self send:AudioEngineRunner::Action::Roughness value:_state.roughness];
        _state.highFrequency=_session->defaultHighFrequency();_state.lowNoise=_session->defaultLowNoise();
        [self send:AudioEngineRunner::Action::HighFrequency value:_state.highFrequency];
        [self send:AudioEngineRunner::Action::LowNoise value:_state.lowNoise];break;
    default:break;
    }
    if(_log)_log<<"control="<<int(c)<<" requested_ignition="<<_state.ignitionRequested<<std::endl;
    [self publish];
}
- (void)slider:(Control)c fraction:(double)fraction {
    if(!_state.ready || !isSlider(c))return;
    fraction=std::clamp(fraction,0.0,1.0);_state.focus=c;
    switch(c) {
    case Throttle:
        [self cancelHeldRev];
        if([self send:AudioEngineRunner::Action::Throttle value:fraction*fraction])_state.throttle=fraction*fraction;break;
    case Volume:
        if([self send:AudioEngineRunner::Action::Volume value:fraction]) { _state.volume=fraction;_state.muted=false; }break;
    case Exhaust:
        if([self send:AudioEngineRunner::Action::ExhaustMix value:fraction])_state.exhaust=fraction;break;
    case HighFrequency:
        if([self send:AudioEngineRunner::Action::HighFrequency value:fraction*.01])_state.highFrequency=fraction*.01;break;
    case LowNoise:
        if([self send:AudioEngineRunner::Action::LowNoise value:fraction])_state.lowNoise=fraction;break;
    case Clutch:
        if([self send:AudioEngineRunner::Action::Clutch value:fraction])_state.clutch=fraction;break;
    case DynoSpeed: {
        const double rpm=500+fraction*(_state.redline-500);
        if([self send:AudioEngineRunner::Action::DynoSpeed value:rpm])_state.dynoRpm=rpm;break;
    }
    case Roughness:
        if([self send:AudioEngineRunner::Action::Roughness value:fraction])_state.roughness=fraction;break;
    default:break;
    }
    [self publish];
}
- (void)hover:(Control)c pressed:(Control)pressed { _state.hover=c;_state.pressed=pressed;[self publish]; }
- (void)key:(NSString *)key shift:(BOOL)shift {
    if(!key.length)return;
    unichar c=[key.lowercaseString characterAtIndex:0];
    if(c=='\t' || c==NSBackTabCharacter) {
        const bool backwards=shift || c==NSBackTabCharacter;
        _state.focus=_state.focus==None ? (backwards ? Control(ControlCount-1) : Supra)
            : Control((int(_state.focus)+(backwards ? ControlCount-1 : 1))%ControlCount);
        [self publish];return;
    }
    if(c=='\r' && _state.focus!=None) { [self activate:_state.focus];return; }
    if((c==NSLeftArrowFunctionKey || c==NSRightArrowFunctionKey) && isSlider(_state.focus)) {
        double value=_state.focus==Throttle ? std::sqrt(_state.throttle) : _state.focus==Volume ? _state.volume : _state.focus==Exhaust ? _state.exhaust
            : _state.focus==HighFrequency ? _state.highFrequency*100 : _state.focus==LowNoise ? _state.lowNoise
            : _state.focus==Clutch ? _state.clutch : _state.focus==DynoSpeed ? (_state.dynoRpm-500)/(_state.redline-500) : _state.roughness;
        [self slider:_state.focus fraction:value+(c==NSLeftArrowFunctionKey ? -.01 : .01)];return;
    }
    switch(c) {
    case ' ': [self activate:Start];break;case 'r':[self holdRev:YES source:1];break;case 'b':[self activate:Rev];break;case 'i':[self activate:Idle];break;
    case 'm':[self activate:Mute];break;case 'f':[self activate:Effects];break;case 'u':[self activate:Uncapped];break;
    case '1':[self activate:Supra];break;case '2':[self activate:Ls];break;
    case 'e':[self activate:Library];break;
    case '[':[self activate:LayerBack];break;case ']':[self activate:LayerNext];break;
    case 'd':[self activate:Dyno];break;
    default:break;
    }
}
- (void)tick:(NSTimer *)timer {
    (void)timer;if(_closing)return;
    [self flushHeldRev];
    if(_state.loading && _loadReady.load(std::memory_order_acquire)) {
        _loader.join();_session=std::move(_pending);_state.loading=false;_state.ready=_session->ready();
        if(!_state.ready) {
            put(_state.notice,_session->error());
            if(_state.automated) { std::cerr<<_session->error()<<'\n';_exitCode=2;[NSApp terminate:nil];return; }
        } else {
            _renderer.connectSession(_session);
            _state.redline=_session->redline();_state.roughness=_session->defaultRoughness();_state.exhaust=1;
            _state.highFrequency=_session->defaultHighFrequency();_state.lowNoise=_session->defaultLowNoise();
            _state.muted=false;_state.throttle=0;_loadedAt=now();_lastLog=-1;
            put(_state.output,_session->deviceName());put(_state.notice,"Ready. Press Space or Start engine.");
            if(_log)_log<<"loaded preset="<<_session->preset().id<<" device="<<_session->deviceName()
                <<" driver="<<SDL_GetCurrentAudioDriver()<<std::endl;
            if(_options.play || !_options.benchmark.empty())[self activate:Start];
        }
    }
    if(_state.ready) {
        _state.engine=_session->snapshot();auto audio=_session->statistics();
        _state.missing=audio.silenceFrames;_state.writeErrors=audio.writeErrors;
        const double elapsed=now()-_loadedAt;
        if(_log && elapsed-_lastLog>=1) {
            _lastLog=elapsed;
            _log<<std::fixed<<std::setprecision(3)<<"t="<<elapsed<<" rpm="<<_state.engine.rpm
                <<" throttle="<<_state.engine.throttle<<" volume="<<_state.engine.volume
                <<" ignition="<<_state.engine.ignition<<" cranking="<<_state.engine.cranking
                <<" missing="<<audio.silenceFrames<<" frames="<<_renderer.metrics().frames<<std::endl;
        }
        if(!_options.uiTest.empty())[self runTests:elapsed];
        else if(!_options.benchmark.empty())[self benchmark:elapsed];
    }
    _state.active=_state.automated || (_window.visible && !_window.miniaturized && !NSApp.hidden);
    _state.displayId=[_window.screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    [self publish];
}
- (void)check:(bool)condition name:(const char *)name {
    std::cout<<"UI_CHECK "<<name<<'='<<(condition ? "PASS" : "FAIL")<<std::endl;
    _passed=_passed && condition;
}
- (void)runTests:(double)t {
    struct TestEngine { const char *id; int cylinders; };
    static constexpr TestEngine testEngines[]={
        {"supra",6},{"ls",8},{"ferrari_f136_v8",8},{"ferrari_412_t2",12},
        {"porsche_911_gt3",6},{"porsche_911_carrera_32",6},{"bmw_m52b28",6}
    };
    auto s=_session->snapshot();
    if(_testStage==0 && t>.25) { [_view testClick:Start];++_testStage; }
    else if(_testStage==1 && t>3.5) {
        [self check:s.ignition && !s.cranking && s.rpm>200 name:"mouse_start"];
        [_view testSlider:Throttle fraction:std::sqrt(_session->preset().revThrottle)];++_testStage;
    } else if(_testStage==2 && t>4.8) {
        [self check:std::abs(s.throttle-_session->preset().revThrottle)<.001 name:"mouse_throttle"];
        const auto path=std::filesystem::path(_options.uiTest)/("engine-"+std::to_string(_testPreset)+".png");
        _renderer.capture(path.c_str());++_expectedCaptures;
        [_view testKey:@"i"];[_view testKey:@"b"];
        const auto before=_renderer.metrics();SDL_Delay(1200);
        const auto after=_renderer.metrics();
        [self check:after.frames>before.frames+30 name:"renderer_continues_during_ui_stall"];
        [self check:after.audioBlocksSeen>before.audioBlocksSeen+100 name:"live_numbers_during_ui_stall"];
        [self check:after.visualBlocksSeen>before.visualBlocksSeen+100 name:"pistons_continue_during_ui_stall"];
        [self check:after.animatedFrames>before.animatedFrames+100 name:"real_physics_animation"];
        [_view testKey:@"]"];[self check:_state.layer==std::min(1,_session->visualLayout().maxLayer) name:"cutaway_layer_next"];
        [_view testKey:@"["];[self check:_state.layer==0 name:"cutaway_layer_back"];
        ++_testStage;
    } else if(_testStage==3 && t>6.5) {
        [self check:!s.blipping && s.throttle==0 && s.rpm>200 name:"rev_ends_during_ui_stall"];
        [_view testSlider:HighFrequency fraction:.2];[_view testSlider:LowNoise fraction:.3];
        [_view testSlider:Volume fraction:.4];[_view testSlider:Exhaust fraction:.8];[_view testSlider:Roughness fraction:.3];++_testStage;
    } else if(_testStage==4 && t>7.5) {
        [self check:std::abs(s.volume-.4)<.001 && std::abs(s.exhaustMix-.8)<.001 && std::abs(s.roughness-.3)<.001 name:"sound_controls"];
        [_view testKey:@"m"];++_testStage;
    } else if(_testStage==5 && t>8.5) {
        [self check:s.volume==0 name:"mute"];
        [_view testClick:Mute];[_view testClick:Reset];[_view testKey:@"u"];[_view testKey:@"f"];++_testStage;
    } else if(_testStage==6 && t>9.5) {
        [self check:std::abs(s.volume-.4)<.001 && s.exhaustMix==1 && std::abs(s.roughness-_session->defaultRoughness())<.001 name:"restore"];
        [self check:_state.uncapped && !_state.effects name:"render_toggles"];
        [self check:_session->visualLayout().cylinderCount==testEngines[_testPreset].cylinders name:"correct_engine_geometry"];
        [_view testKey:@"u"];[_view testKey:@"f"];
        [_window setContentSize:NSMakeSize(1000,625)];
        [_view testSlider:Volume fraction:.35];[_view testKey:@" "];++_testStage;
    } else if(_testStage==7 && t>10.5) {
        [self check:!s.ignition && s.throttle==0 name:"keyboard_stop"];
        [self check:std::abs(s.volume-.35)<.001 name:"input_after_resize"];
        [_view testClick:Start];++_testStage;
    } else if(_testStage==8 && t>14) {
        [self check:s.ignition && !s.cranking && s.rpm>200 name:"restart"];
        [_view testKey:@"r"];++_testStage;
    } else if(_testStage==9 && t>15.2) {
        [self check:s.throttle==1 && _state.revHeld name:"held_keyboard_throttle"];
        [_view testKeyUp:@"r"];++_testStage;
    } else if(_testStage==10 && t>15.7) {
        [self check:s.throttle==0 && !_state.revHeld name:"key_up_releases_throttle"];
        [_view testHoldMouse:YES];++_testStage;
    } else if(_testStage==11 && t>16.5) {
        [self check:s.throttle==1 && _state.revHeld name:"held_mouse_throttle"];
        [_view testHoldMouse:NO];++_testStage;
    } else if(_testStage==12 && t>17) {
        [self check:s.throttle==0 && !_state.revHeld name:"mouse_up_outside_releases_throttle"];
        [_view testKey:@"r"];[self windowDidResignKey:[NSNotification notificationWithName:NSWindowDidResignKeyNotification object:_window]];++_testStage;
    } else if(_testStage==13 && t>17.5) {
        [self check:s.throttle==0 && !_state.revHeld name:"focus_loss_releases_throttle"];
        [self check:_engineMenuItem.submenu.numberOfItems==SoundSession::presets().size()+1 name:"all_engines_in_menubar"];
        [self check:_session->statistics().silenceFrames==0 && _session->statistics().writeErrors==0 name:"no_audio_gaps"];
        [self check:_renderer.captures()>=_expectedCaptures && _renderer.metrics().errors==0 name:"metal_capture"];
        if(++_testPreset<std::size(testEngines)) {
            [_window setContentSize:NSMakeSize(Width,Height)];
            const auto &presets=SoundSession::presets();
            auto found=std::find_if(presets.begin(),presets.end(),[&](const auto &p){return p.id==testEngines[_testPreset].id;});
            if(found==presets.end()) {
                [self check:false name:"test_engine_available"];
                _exitCode=1;[NSApp terminate:nil];return;
            }
            NSMenuItem *item=[[self engineMenu] itemWithTag:found-presets.begin()];
            [self check:item && [NSApp sendAction:item.action to:item.target from:item] name:"library_selection"];
            _testStage=0;
        } else {
            std::cout<<"UI_RESULT="<<(_passed ? "PASS" : "FAIL")<<std::endl;
            _exitCode=_passed ? 0 : 1;[NSApp terminate:nil];
        }
    }
}
- (void)benchmark:(double)t {
    if(_testStage==0 && t>4) { _renderer.capture((_options.benchmark+".png").c_str());_testStage=1; }
    if(!_benchMeasured && t>3) { _benchMeasured=true;_benchAt=now();_benchMetrics=_renderer.metrics();_benchAudio=_session->statistics(); }
    if(_testStage==1 && t>5) { _session->command(AudioEngineRunner::Action::Throttle,_session->preset().revThrottle);_state.throttle=_session->preset().revThrottle;_testStage=2; }
    if(_testStage==2 && t>7) { [self activate:Idle];_testStage=3; }
    if(_testStage==3 && t>8) {
        const auto before=_renderer.metrics();
        SDL_Delay(1200);
        const auto after=_renderer.metrics();
        _benchStallFrames=after.frames-before.frames;
        [self check:_benchStallFrames>30 name:"live_renderer_during_ui_stall"];
        [self check:after.audioBlocksSeen>before.audioBlocksSeen+100 name:"live_numbers_during_ui_stall"];
        [self check:after.visualBlocksSeen>before.visualBlocksSeen+100 name:"pistons_continue_during_ui_stall"];
        [self check:after.animatedFrames>before.animatedFrames+100 name:"real_physics_animation"];
        [_view testKey:@"]"];[self check:_state.layer==std::min(1,_session->visualLayout().maxLayer) name:"cutaway_layer_next"];
        [_view testKey:@"["];[self check:_state.layer==0 name:"cutaway_layer_back"];
        _testStage=4;
    }
    if(!_benchMeasured || now()-_benchAt<_options.seconds)return;
    const double seconds=now()-_benchAt;
    const auto last=_renderer.metrics();const auto audio=_session->statistics();
    const auto count=last.frames-_benchMetrics.frames;
    const double fps=count/seconds,cpu=(last.cpuNs-_benchMetrics.cpuNs)/1e6/std::max(uint64_t(1),count);
    const double gpu=(last.gpuNs-_benchMetrics.gpuNs)/1e6/std::max(uint64_t(1),count);
    const bool passed=_passed && count>0 && last.errors==0 && audio.silenceFrames==_benchAudio.silenceFrames && audio.writeErrors==0 && _session->snapshot().rpm>200;
    std::ofstream report(_options.benchmark+".json");
    report<<std::fixed<<std::setprecision(4)<<"{\n  \"result\": \""<<(passed ? "PASS" : "FAIL")<<"\",\n"
        <<"  \"mode\": \""<<(_options.offscreen ? "offscreen" : "onscreen")<<"\",\n"
        <<"  \"uncapped\": "<<(_state.uncapped ? "true" : "false")<<",\n"
        <<"  \"effects\": "<<(_state.effects ? "true" : "false")<<",\n"
        <<"  \"driver\": \""<<SDL_GetCurrentAudioDriver()<<"\",\n"
        <<"  \"width_pixels\": "<<(_options.offscreen ? Width*2 : ((CAMetalLayer *)_view.layer).drawableSize.width)<<",\n"
        <<"  \"height_pixels\": "<<(_options.offscreen ? Height*2 : ((CAMetalLayer *)_view.layer).drawableSize.height)<<",\n"
        <<"  \"seconds\": "<<seconds<<",\n  \"completed_frames\": "<<count<<",\n  \"render_fps\": "<<fps
        <<",\n  \"cpu_mean_ms\": "<<cpu<<",\n  \"gpu_mean_ms\": "<<gpu
        <<",\n  \"cpu_p95_ms_upper_bound\": "<<percentile(last.cpuHistogram,_benchMetrics.cpuHistogram,.95)
        <<",\n  \"gpu_p95_ms_upper_bound\": "<<percentile(last.gpuHistogram,_benchMetrics.gpuHistogram,.95)
        <<",\n  \"histogram_limit_ms\": 12.75"
        <<",\n  \"ui_stall_ms\": "<<(_testStage>=4 ? 1200 : 0)
        <<",\n  \"ui_stall_rendered_frames\": "<<_benchStallFrames
        <<",\n  \"peak_vertices\": "<<last.peakVertices
        <<",\n  \"visual_blocks_seen\": "<<last.visualBlocksSeen
        <<",\n  \"missing_frames\": "<<audio.silenceFrames-_benchAudio.silenceFrames
        <<",\n  \"write_errors\": "<<audio.writeErrors<<",\n  \"metal_errors\": "<<last.errors<<"\n}\n";
    report.close();
    std::cout<<"BENCHMARK result="<<(passed ? "PASS" : "FAIL")<<" render_fps="<<fps<<" cpu_ms="<<cpu<<" gpu_ms="<<gpu
        <<" missing="<<audio.silenceFrames-_benchAudio.silenceFrames<<std::endl;
    _exitCode=passed ? 0 : 1;[NSApp terminate:nil];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { (void)sender;return YES; }
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;_closing=true;[_timer invalidate];
    _renderer.stop();
    if(_loader.joinable())_loader.join();
    _pending.reset();_session.reset();
    if(_activity)[NSProcessInfo.processInfo endActivity:_activity];
    SDL_Quit();
    if(_exitCode)std::exit(_exitCode);
}
@end

int main(int argc,char **argv) {
    @autoreleasepool {
        Options options;
        try {
            for(int i=1;i<argc;++i) {
                std::string arg=argv[i];
                if(arg=="--help") {
                    std::cout<<"Engine Sound - native C++ / Metal sound studio\n"
                        <<"--play --uncapped --no-effects --silent --log FILE\n"
                        <<"--benchmark PREFIX [--seconds N] [--offscreen] [--uncapped]\n"
                        <<"--ui-test DIRECTORY  Hidden, programmatic input/audio/Metal checks\n"
                        <<"--self-test PREFIX [--preset ID] [--ui-stall-ms 0..1000]\n"
                        <<"--list-engines  List the IDs accepted by --preset\n"
                        <<"Keys: Space start/stop, R rev, I idle, M mute, F effects, U frame mode, E library, 1/2 favorites\n";
                    return 0;
                }
                if(arg=="--list-engines") {
                    for(const auto &preset:SoundSession::presets())std::cout<<preset.id<<"\t"<<preset.title<<'\n';
                    return 0;
                }
                if(arg=="--play"){options.play=true;continue;}
                if(arg=="--uncapped"){options.uncapped=true;continue;}
                if(arg=="--offscreen"){options.offscreen=true;continue;}
                if(arg=="--no-effects"){options.effects=false;continue;}
                if(arg=="--silent"){options.silent=true;continue;}
                if(i+1>=argc)throw std::runtime_error("Missing option value: "+arg);
                std::string value=argv[++i];
                if(arg=="--log")options.log=value;
                else if(arg=="--benchmark")options.benchmark=value;
                else if(arg=="--ui-test")options.uiTest=value;
                else if(arg=="--self-test")options.verify=value;
                else if(arg=="--seconds")options.seconds=std::stod(value);
                else if(arg=="--ui-stall-ms")options.stall=std::stoi(value);
                else if(arg=="--preset") {
                    const auto &presets=SoundSession::presets();
                    auto found=std::find_if(presets.begin(),presets.end(),[&](const auto &p){return p.id==value;});
                    if(found==presets.end())throw std::runtime_error("Unknown engine; use --list-engines");
                    options.preset=found-presets.begin();
                }
                else throw std::runtime_error("Unknown option or value: "+arg);
            }
            if(!std::isfinite(options.seconds) || options.seconds<2 || options.seconds>120 || options.stall<0 || options.stall>1000)
                throw std::runtime_error("Invalid duration or stall");
            if(options.silent || !options.uiTest.empty())SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
        } catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 2; }
        if(!SDL_Init(SDL_INIT_AUDIO)){std::cerr<<SDL_GetError()<<'\n';return 2;}
        options.assets=RuntimePaths::discover(SDL_GetBasePath(),ENGINE_SIM_SOURCE_ASSET_DIRECTORY).assetDirectory;
        if(!options.verify.empty()) {
            const int result=verifySoundSession(options.assets,options.preset,options.verify,options.stall);SDL_Quit();return result;
        }
        [NSApplication sharedApplication];
        __attribute__((objc_precise_lifetime)) SoundApp *controller=[[SoundApp alloc] initWithOptions:options];
        NSApp.delegate=controller;[NSApp run];return [controller exitCode];
    }
}
