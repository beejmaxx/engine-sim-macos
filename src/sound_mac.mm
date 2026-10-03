#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "runtime_paths.h"
#include "sound_session.h"
#include "sound_metal.h"
#include "game_audio_probe.h"
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
    std::string log, verify, driveVerify, benchmark, uiTest, gameTest, arcadeTest;
    int preset=0, stall=1000;
    double seconds=12;
    bool play=false, drive=false, roadView=false, uncapped=false, offscreen=false, effects=true, silent=false, muted=false;
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
- (void)testShift:(BOOL)down;
- (void)testHoldMouse:(BOOL)down;
- (void)testHoldMouse:(BOOL)down control:(Control)control;
@end

@interface SoundApp : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate> {
    Options _options;
    NSWindow *_window;
    SoundMetalView *_view;
    NSTimer *_timer;
    NSMenuItem *_engineMenuItem;
    unsigned _heldRev, _heldBrake;
    unsigned _heldDrift;
    unsigned _steerLeft,_steerRight;
    double _gamePhaseAt;
    Metrics _gameBefore;
    GameAudioProbe _gameAudio;
    double _testThrottle,_testBrake;
    bool _gameSunMoved,_gameSunHidden;
    double _gameSunTurnX;
    bool _revPending, _brakePending;
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
- (void)holdBrake:(BOOL)down source:(unsigned)source;
- (void)holdDrift:(BOOL)down source:(unsigned)source;
- (void)cancelHeldRev;
- (void)steer:(BOOL)down right:(BOOL)right;
- (void)steer:(BOOL)down right:(BOOL)right source:(unsigned)source;
- (void)gameTest:(double)t;
- (void)arcadeTest:(double)t;
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
    NSPoint p=[self logicalPoint:event];[self.controller hover:hit(p.x,p.y,[self.controller currentState].roadView) pressed:_pressed];
}
- (void)mouseExited:(NSEvent *)event { (void)event;[self.controller hover:None pressed:_pressed]; }
- (void)mouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    NSPoint p=[self logicalPoint:event];_pressed=hit(p.x,p.y,[self.controller currentState].roadView);
    _drag=isSlider(_pressed) ? _pressed : None;
    if(_pressed==Rev)[self.controller holdRev:YES source:2];
    if(_pressed==Brake)[self.controller holdBrake:YES source:2];
    if(_pressed==Drift)[self.controller holdDrift:YES source:2];
    [self.controller hover:_pressed pressed:_pressed];
    if(_drag!=None) { UiRect r=bounds(_drag,[self.controller currentState].roadView);[self.controller slider:_drag fraction:(p.x-r.x-7)/(r.w-14)]; }
}
- (void)mouseDragged:(NSEvent *)event {
    if(_drag==None)return;
    NSPoint p=[self logicalPoint:event];UiRect r=bounds(_drag,[self.controller currentState].roadView);
    [self.controller slider:_drag fraction:(p.x-r.x-7)/(r.w-14)];
}
- (void)mouseUp:(NSEvent *)event {
    NSPoint p=[self logicalPoint:event];Control released=hit(p.x,p.y,[self.controller currentState].roadView);
    if(_pressed==Rev)[self.controller holdRev:NO source:2];
    else if(_pressed==Brake)[self.controller holdBrake:NO source:2];
    else if(_pressed==Drift)[self.controller holdDrift:NO source:2];
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
    if(!event.charactersIgnoringModifiers.length)return;
    if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"r"])
        [self.controller holdRev:NO source:1];
    else if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"s"])
        [self.controller holdBrake:NO source:1];
    else if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"w"])
        [self.controller holdRev:NO source:8];
    else if([event.charactersIgnoringModifiers characterAtIndex:0]==NSUpArrowFunctionKey)
        [self.controller holdRev:NO source:16];
    else if([event.charactersIgnoringModifiers characterAtIndex:0]==NSDownArrowFunctionKey)
        [self.controller holdBrake:NO source:8];
    else if([event.charactersIgnoringModifiers characterAtIndex:0]==NSLeftArrowFunctionKey)
        [self.controller steer:NO right:NO source:1];
    else if([event.charactersIgnoringModifiers characterAtIndex:0]==NSRightArrowFunctionKey)
        [self.controller steer:NO right:YES source:1];
    else if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"a"])
        [self.controller steer:NO right:NO source:2];
    else if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"d"])
        [self.controller steer:NO right:YES source:2];
    else if([event.charactersIgnoringModifiers isEqualToString:@" "])
        [self.controller holdBrake:NO source:16];
    else if([event.charactersIgnoringModifiers isEqualToString:@"\r"]) {
        [self.controller holdBrake:NO source:4];[self.controller holdDrift:NO source:4];
    }
    else [super keyUp:event];
}
- (void)flagsChanged:(NSEvent *)event {
    [self.controller holdDrift:(event.modifierFlags & NSEventModifierFlagShift)!=0 source:1];
}
- (NSEvent *)eventAt:(NSPoint)point type:(NSEventType)type {
    NSPoint local=NSMakePoint(point.x*self.bounds.size.width/Width,point.y*self.bounds.size.height/Height);
    NSPoint window=[self convertPoint:local toView:nil];
    return [NSEvent mouseEventWithType:type location:window modifierFlags:0 timestamp:now()
        windowNumber:self.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
}
- (void)testClick:(Control)control {
    UiRect r=bounds(control,[self.controller currentState].roadView);NSPoint point=NSMakePoint(r.x+r.w/2,r.y+r.h/2);
    [self mouseDown:[self eventAt:point type:NSEventTypeLeftMouseDown]];
    [self mouseUp:[self eventAt:point type:NSEventTypeLeftMouseUp]];
}
- (void)testSlider:(Control)control fraction:(double)value {
    UiRect r=bounds(control,[self.controller currentState].roadView);NSPoint point=NSMakePoint(r.x+7+value*(r.w-14),r.y+16);
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
- (void)testShift:(BOOL)down {
    [self flagsChanged:[NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
        modifierFlags:down ? NSEventModifierFlagShift : 0 timestamp:now() windowNumber:self.window.windowNumber
        context:nil characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:56]];
}
- (void)testHoldMouse:(BOOL)down {
    [self testHoldMouse:down control:Rev];
}
- (void)testHoldMouse:(BOOL)down control:(Control)control {
    const auto r=bounds(control,[self.controller currentState].roadView);
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
        _benchAudio={};_benchMetrics={};_testThrottle=_testBrake=-1;
        _state.preset=options.preset;_state.uncapped=options.uncapped;_state.effects=options.effects;
        _state.roadView=options.roadView;
        _state.engineCount=SoundSession::presets().size();
        _state.volume=SoundSession::DefaultVolume;
        _state.muted=options.muted;
        _state.silent=std::string(SDL_GetCurrentAudioDriver())=="dummy";
        _state.automated=!options.uiTest.empty() || !options.benchmark.empty() || !options.gameTest.empty() || !options.arcadeTest.empty();
        _state.testPilot=options.drive && !options.benchmark.empty();
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
    [self holdBrake:NO source:31];[self holdDrift:NO source:7];[self steer:NO right:NO];[self steer:NO right:YES];_heldBrake=0;_brakePending=false;_state.brakeHeld=false;_state.drive=false;
    _state.loading=true;_state.ready=false;_state.ignitionRequested=false;_state.preset=preset;
    _engineMenuItem.submenu=[self engineMenu];
    put(_state.title,SoundSession::presets()[preset].title);
    _state.carBody=SoundSession::presets()[preset].id=="porsche_911_gt3"
        ? State::CarBody::PorscheGt3 : State::CarBody::Concept;
    _state.engine={};_state.waveform.fill(0);_state.throttle=0;_state.missing=0;_state.writeErrors=0;_state.layer=0;_state.dyno=false;_state.dynoRpm=1000;_state.clutch=0;
    put(_state.notice,"Loading engine and exhaust sound...");put(_state.output,"");
    _renderer.connectSession(nullptr);_session.reset();
    if(_activity) { [NSProcessInfo.processInfo endActivity:_activity];_activity=nil; }
    const double volume=_state.muted ? 0 : _state.volume;
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
    if(_state.automated && _options.uiTest.empty())return;
    if(item.tag>=0 && item.tag<SoundSession::presets().size())[self loadPreset:int(item.tag)];
}
- (void)flushHeldRev {
    if(_revPending && _session && _session->command(AudioEngineRunner::Action::Throttle,_heldRev ? 1 : 0))
        _revPending=false;
    if(_brakePending && _session) {
        const bool back=_state.roadView && (_heldBrake & 9);
        const bool brake=_state.roadView ? (_heldBrake & 22)!=0 : _heldBrake!=0;
        if(_session->command(AudioEngineRunner::Action::BackPedal,back) &&
            _session->command(AudioEngineRunner::Action::Brake,brake))_brakePending=false;
    }
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
- (void)cancelHeldRev { [self holdRev:NO source:31]; }
- (void)steer:(BOOL)down right:(BOOL)right {
    [self steer:down right:right source:3]; // focus loss clears every alias
}
- (void)steer:(BOOL)down right:(BOOL)right source:(unsigned)source {
    auto &held=right ? _steerRight : _steerLeft;
    held=down ? held|source : held&~source;
    const float input=float(bool(_steerRight))-float(bool(_steerLeft));
    if(input==_state.steering)return;
    _state.steering=input;
    if(_log)_log<<"steering_event t="<<now()-_loadedAt<<" input="<<input<<std::endl;
    [self publish];
}
- (void)holdBrake:(BOOL)down source:(unsigned)source {
    if(down && !_state.ready)return;
    const unsigned previous=_heldBrake;
    _heldBrake=down ? (_heldBrake|source) : (_heldBrake&~source);
    if(previous==_heldBrake)return;
    if(_log)_log<<"brake_event t="<<now()-_loadedAt<<" pressure="<<bool(_heldBrake)<<std::endl;
    _state.brakeHeld=_heldBrake!=0;_brakePending=true;[self flushHeldRev];[self publish];
}
- (void)holdDrift:(BOOL)down source:(unsigned)source {
    if(_state.automated && _options.uiTest.empty())return;
    if(down && (!_state.roadView || !_state.ready))return;
    _heldDrift=down ? _heldDrift|source : _heldDrift&~source;
    _state.driftHeld=_heldDrift!=0;[self publish];
}
- (void)windowDidResignKey:(NSNotification *)notification { (void)notification;[self cancelHeldRev];[self holdBrake:NO source:31];[self holdDrift:NO source:7];[self steer:NO right:NO];[self steer:NO right:YES]; }
- (void)applicationDidResignActive:(NSNotification *)notification { (void)notification;[self cancelHeldRev];[self holdBrake:NO source:31];[self holdDrift:NO source:7];[self steer:NO right:NO];[self steer:NO right:YES]; }
- (void)menuWillOpen:(NSMenu *)menu { (void)menu;[self cancelHeldRev];[self holdBrake:NO source:31];[self holdDrift:NO source:7];[self steer:NO right:NO];[self steer:NO right:YES]; }
- (void)activate:(Control)c {
    if(c==None)return;
    _state.focus=c;
    if(c==Effects) { _state.effects=!_state.effects;[self publish];return; }
    if(c==Uncapped) { _state.uncapped=!_state.uncapped;[self publish];return; }
    if(c==RoadView) {
        [self cancelHeldRev];[self holdBrake:NO source:31];[self holdDrift:NO source:7];
        [self steer:NO right:NO];[self steer:NO right:YES];
        _state.roadView=!_state.roadView;
        if(bounds(_state.focus,_state.roadView).w==0)_state.focus=None;
        [self publish];return;
    }
    if(_state.loading)return;
    if(c==Library) {
        [self cancelHeldRev];
        const UiRect r=bounds(Library,_state.roadView);
        [[self engineMenu] popUpMenuPositioningItem:nil
            atLocation:NSMakePoint(r.x*_view.bounds.size.width/Width,(r.y+r.h)*_view.bounds.size.height/Height) inView:_view];
        return;
    }
    if(c==Supra || c==Ls) { [self loadPreset:c==Ls ? 1 : 0];return; }
    if(!_state.ready)return;
    if(c==RecoverCar) {_renderer.recoverCar();return;}
    if(c==RestartRace) {_renderer.restartRace();return;}
    switch(c) {
    case Drive:
        if([self send:AudioEngineRunner::Action::Drive value:!_state.drive]) {
            _state.drive=!_state.drive;_state.dyno=false;
            _options.drive=_state.drive;
            if(_state.roadView)put(_state.notice,_state.drive ? "W gas / S brake then reverse / Space brake / Shift drift" : "NEUTRAL: G automatic drive");
            else put(_state.notice,_state.drive ? "DRIVE: hold R to accelerate / S to brake / A for neutral" : "NEUTRAL: R throttle / B blip / A automatic drive");
        }
        break;
    case Brake: [self holdBrake:YES source:4];break;
    case Drift: [self holdDrift:YES source:4];break;
    case LayerBack: _state.layer=std::max(0,_state.layer-1);break;
    case LayerNext: _state.layer=std::min(_session->visualLayout().maxLayer,_state.layer+1);break;
    case Dyno:
        if([self send:AudioEngineRunner::Action::Dyno value:!_state.dyno]) {
            _state.dyno=!_state.dyno;if(_state.dyno)_options.drive=false;
        }break;
    case GearDown: _options.drive=false;[self send:AudioEngineRunner::Action::Gear value:-1];break;
    case GearUp: _options.drive=false;[self send:AudioEngineRunner::Action::Gear value:1];break;
    case Start: {
        [self cancelHeldRev];[self holdBrake:NO source:31];[self holdDrift:NO source:7];
        bool sent=_state.ignitionRequested ? _session->command(AudioEngineRunner::Action::Stop) : _session->startEngine();
        if(sent) {
            _state.ignitionRequested=!_state.ignitionRequested;_state.throttle=0;
            [self send:AudioEngineRunner::Action::Throttle value:0];
            if(_state.roadView)put(_state.notice,_state.ignitionRequested ? "WASD drive / S brake then reverse / Space brake / Shift drift" : "Engine off. Press X or Start engine.");
            else put(_state.notice,_state.ignitionRequested ? "A drive / Hold R throttle / Hold S brake / Space stop" : "Engine off. Press Space or Start engine.");
            if(_state.ignitionRequested && !_activity)
                _activity=[NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated reason:@"Live engine audio"];
            if(!_state.ignitionRequested && _activity) { [NSProcessInfo.processInfo endActivity:_activity];_activity=nil; }
        }
        break;
    }
    case Rev: if(_state.ignitionRequested)_session->rev();break;
    case Idle: [self cancelHeldRev];[self holdBrake:NO source:9];[self holdDrift:NO source:7];
        if([self send:AudioEngineRunner::Action::Throttle value:0])_state.throttle=0;break;
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
        if([self send:AudioEngineRunner::Action::Clutch value:fraction]) {_state.clutch=fraction;_options.drive=false;}break;
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
    // Onscreen benchmarks can receive focus from LaunchServices. Physical
    // typing must not mute, steer or stop a measurement; the native input
    // suite deliberately exercises this handler and is the sole exception.
    if(_state.automated && _options.uiTest.empty())return;
    if(!key.length)return;
    unichar c=[key.lowercaseString characterAtIndex:0];
    if(_state.roadView) {
        if(c==NSLeftArrowFunctionKey || c==NSRightArrowFunctionKey) {[self steer:YES right:c==NSRightArrowFunctionKey source:1];return;}
        if(c=='a' || c=='d') {[self steer:YES right:c=='d' source:2];return;}
        if(c==NSUpArrowFunctionKey || c=='w') {[self holdRev:YES source:c=='w' ? 8 : 16];return;}
        if(c==NSDownArrowFunctionKey) {[self holdBrake:YES source:8];return;}
        if(c==' ') {[self holdBrake:YES source:16];return;}
        if(c=='x') {[self activate:Start];return;}
        if(c=='g') {[self activate:Drive];return;}
        if(c=='c') {[self activate:RecoverCar];return;}
        if(c==NSDeleteCharacter || c==NSBackspaceCharacter) {[self activate:RestartRace];return;}
    }
    if(c=='\t' || c==NSBackTabCharacter) {
        const bool backwards=shift || c==NSBackTabCharacter;
        _state.focus=_state.focus==None ? (backwards ? Control(ControlCount-1) : Supra)
            : Control((int(_state.focus)+(backwards ? ControlCount-1 : 1))%ControlCount);
        for(int i=0;i<ControlCount && bounds(_state.focus,_state.roadView).w==0;++i)
            _state.focus=Control((int(_state.focus)+(backwards ? ControlCount-1 : 1))%ControlCount);
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
    case 'a':[self activate:Drive];break;case 's':[self holdBrake:YES source:1];break;
    case 'v':[self activate:RoadView];break;
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
            _state.throttle=0;_loadedAt=now();_lastLog=-1;
            put(_state.output,_session->deviceName());put(_state.notice,"Ready. Press Space or Start engine.");
            if(_log)_log<<"loaded preset="<<_session->preset().id<<" device="<<_session->deviceName()
                <<" driver="<<SDL_GetCurrentAudioDriver()<<std::endl;
            if(_options.play || !_options.benchmark.empty() || !_options.gameTest.empty())[self activate:Start];
            if(_options.drive)[self activate:Drive];
        }
    }
    if(_state.ready) {
        _state.engine=_session->snapshot();auto audio=_session->statistics();
        _state.drive=_state.engine.drive;
        _state.missing=audio.silenceFrames;_state.writeErrors=audio.writeErrors;
        const double elapsed=now()-_loadedAt;
        if(_log && elapsed-_lastLog>=1) {
            _lastLog=elapsed;
            const auto metrics=_renderer.metrics();
            _log<<std::fixed<<std::setprecision(3)<<"t="<<elapsed<<" rpm="<<_state.engine.rpm
                <<" throttle="<<_state.engine.throttle<<" volume="<<_state.engine.volume
                <<" ignition="<<_state.engine.ignition<<" cranking="<<_state.engine.cranking
                <<" drive="<<_state.engine.drive<<" gear="<<_state.engine.gear+1
                <<" mph="<<_state.engine.vehicleSpeed/.44704<<" shifting="<<_state.engine.shifting
                <<" brake="<<_state.engine.brake<<" applied_throttle="<<_state.engine.appliedThrottle
                <<" distance_m="<<metrics.roadDistance<<" road_view="<<_state.roadView
                <<" game_x="<<metrics.game.x<<" game_z="<<metrics.game.z
                <<" signed_mph="<<metrics.game.speed/.44704<<" drift_angle="<<metrics.game.driftAngle<<" drift_score="<<metrics.game.totalDriftScore+metrics.game.driftScore
                <<" laps="<<metrics.game.laps<<" checkpoint="<<metrics.game.nextCheckpoint
                <<" steering="<<_state.steering<<" steering_input="<<metrics.game.steeringInput
                <<" lateral_g="<<metrics.game.lateralG<<" offroad_fraction="<<metrics.game.offroadFraction
                <<" collisions="<<metrics.game.collisions
                <<" missing="<<audio.silenceFrames<<" frames="<<metrics.frames<<std::endl;
        }
        if(!_options.uiTest.empty())[self runTests:elapsed];
        else if(!_options.arcadeTest.empty())[self arcadeTest:elapsed];
        else if(!_options.gameTest.empty())[self gameTest:elapsed];
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
        [_view testKey:@"v"];[self check:_state.roadView name:"road_view_key"];
        [_view testClick:RoadView];[self check:!_state.roadView name:"cutaway_view_button"];
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
        [self check:(_state.carBody==State::CarBody::PorscheGt3)==(_session->preset().id=="porsche_911_gt3") name:"body_matches_engine_selection"];
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
        [_view testKey:@"v"];
        bool visibleFocus=true,focusedDrift=false;
        for(int i=0;i<ControlCount;++i) {
            [_view testKey:@"\t"];
            visibleFocus &= bounds(_state.focus,true).w>0;
            focusedDrift |= _state.focus==Drift;
        }
        [self check:_state.roadView && visibleFocus && focusedDrift name:"driving_hud_tabs_through_visible_controls_including_drift"];
        _state.focus=Throttle;const float throttleBefore=_state.throttle;
        [_view testKey:@"\uF703"];
        [self check:_state.steering==1 && _state.throttle==throttleBefore name:"game_right_arrow_steers_without_editing_slider"];
        [_view testKey:@"\uF702"];[self check:_state.steering==0 name:"opposing_steering_keys_cancel"];
        [_view testKeyUp:@"\uF703"];[self check:_state.steering==-1 name:"steering_key_up_preserves_other_direction"];
        [self windowDidResignKey:[NSNotification notificationWithName:NSWindowDidResignKeyNotification object:_window]];
        [self check:_state.steering==0 name:"focus_loss_releases_steering"];
        const bool driveBefore=_state.drive,dynoBefore=_state.dyno;
        [_view testKey:@"d"];
        [self check:_state.steering==1 && _state.drive==driveBefore && _state.dyno==dynoBefore name:"wasd_d_steers_without_enabling_dyno"];
        [_view testKey:@"\uF703"];[_view testKeyUp:@"d"];
        [self check:_state.steering==1 name:"arrow_stays_held_when_wasd_alias_released"];
        [_view testKeyUp:@"\uF703"];[_view testKey:@"a"];
        [self check:_state.steering==-1 && _state.drive==driveBefore name:"wasd_a_steers_without_disabling_drive"];
        [_view testKey:@"\uF702"];[_view testKeyUp:@"\uF702"];
        [self check:_state.steering==-1 name:"wasd_stays_held_when_arrow_alias_released"];
        [self windowDidResignKey:[NSNotification notificationWithName:NSWindowDidResignKeyNotification object:_window]];
        [self check:_state.steering==0 name:"focus_loss_releases_wasd"];
        [_view testShift:YES];[self check:_state.driftHeld name:"shift_holds_drift"];
        [_view testHoldMouse:YES control:Drift];[_view testShift:NO];
        [self check:_state.driftHeld name:"mouse_drift_survives_shift_release"];
        [_view testHoldMouse:NO control:Drift];[self check:!_state.driftHeld name:"mouse_up_releases_drift"];
        [_view testShift:YES];[self windowDidResignKey:[NSNotification notificationWithName:NSWindowDidResignKeyNotification object:_window]];
        [self check:!_state.driftHeld name:"focus_loss_releases_drift"];
        [_view testKey:@"\uF700"];[self check:_state.revHeld && _state.throttle==1 name:"game_up_arrow_accelerates"];
        [_view testKey:@"w"];[_view testKeyUp:@"\uF700"];
        [self check:_state.revHeld && _state.throttle==1 name:"w_stays_held_when_up_arrow_released"];
        [_view testKeyUp:@"w"];[self check:!_state.revHeld && _state.throttle==0 name:"game_up_arrow_release"];
        [_view testKey:@"\uF701"];[self check:_state.brakeHeld name:"game_down_arrow_brakes"];
        [_view testKeyUp:@"\uF701"];[self check:!_state.brakeHeld name:"game_down_arrow_release"];
        [_view testKey:@"g"];++_testStage;
    } else if(_testStage==14 && t>18) {
        [self check:s.drive && s.gear==0 name:"automatic_drive_key"];
        [_view testKey:@"r"];[_view testKey:@" "];++_testStage;
    } else if(_testStage==15 && t>18.5) {
        [self check:s.brake==1 && s.appliedThrottle==0 name:"brake_overrides_accelerator"];
        [self check:s.ignition && _state.ignitionRequested name:"space_brakes_without_stopping_engine"];
        [_view testKey:@"s"];[_view testKeyUp:@" "];
        [self check:_state.brakeHeld name:"s_stays_held_when_space_released"];
        [_view testKeyUp:@"s"];++_testStage;
    } else if(_testStage==16 && t>19) {
        [self check:s.brake==0 && !_state.brakeHeld name:"brake_key_up"];
        [_view testKey:@" "];[self windowDidResignKey:[NSNotification notificationWithName:NSWindowDidResignKeyNotification object:_window]];
        ++_testStage;
    } else if(_testStage==17 && t>19.5) {
        [self check:s.brake==0 && s.throttle==0 && !s.backPedal name:"focus_loss_releases_both_pedals"];
        [_view testClick:Drive];++_testStage;
    } else if(_testStage==18 && t>20) {
        [self check:!s.drive && s.gear==-1 && s.clutch==0 name:"automatic_drive_button_neutral"];
        _state.focus=Brake;[_view testKey:@"\r"];++_testStage;
    } else if(_testStage==19 && t>20.3) {
        [self check:s.brake==1 name:"focused_brake_return_key"];
        [_view testKeyUp:@"\r"];++_testStage;
    } else if(_testStage==20 && t>20.6) {
        [self check:s.brake==0 name:"focused_brake_return_release"];
        [_view testHoldMouse:YES control:Brake];++_testStage;
    } else if(_testStage==21 && t>20.9) {
        [self check:s.brake==1 name:"held_mouse_brake"];
        const auto path=std::filesystem::path(_options.uiTest)/("road-engine-"+std::to_string(_testPreset)+".png");
        _renderer.capture(path.c_str());++_expectedCaptures;
        [_view testHoldMouse:NO control:Brake];++_testStage;
    } else if(_testStage==22 && t>21.2) {
        // PNG encoding is asynchronous; let slow disks finish without blocking
        // the event loop, render thread, or audio worker.
        if(_renderer.captures()<_expectedCaptures && t<25)return;
        [self check:s.brake==0 name:"mouse_up_outside_releases_brake"];
        [self check:_session->statistics().silenceFrames==0 && _session->statistics().writeErrors==0 name:"no_audio_gaps"];
        [self check:_renderer.captures()>=_expectedCaptures && _renderer.metrics().errors==0 name:"metal_capture"];
        [_view testClick:RoadView];[self check:!_state.roadView name:"return_from_driving_hud"];
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
    if(_testStage==0 && t>4) { _renderer.capture((_options.benchmark+(_state.roadView ? "-idle.png" : ".png")).c_str());_testStage=1; }
    if(!_benchMeasured && t>3) { _benchMeasured=true;_benchAt=now();_benchMetrics=_renderer.metrics();_benchAudio=_session->statistics(); }
    if(_testStage==1 && t>5) {
        const float pedal=_options.drive ? 1 : _session->preset().revThrottle;
        _session->command(AudioEngineRunner::Action::Throttle,pedal);_state.throttle=pedal;_testStage=2;
    }
    if(_testStage==2 && t>7) { if(!_options.drive)[self activate:Idle];_testStage=3; }
    if(_testStage==3 && t>8) {
        const auto before=_renderer.metrics();
        SDL_Delay(1200);
        const auto after=_renderer.metrics();
        _benchStallFrames=after.frames-before.frames;
        [self check:_benchStallFrames>30 name:"live_renderer_during_ui_stall"];
        [self check:after.audioBlocksSeen>before.audioBlocksSeen+100 name:"live_numbers_during_ui_stall"];
        [self check:after.visualBlocksSeen>before.visualBlocksSeen+100 name:"pistons_continue_during_ui_stall"];
        [self check:after.animatedFrames>before.animatedFrames+100 name:"real_physics_animation"];
        if(_state.roadView && _options.drive) {
            [self check:after.movingRoadFrames>before.movingRoadFrames+30 && after.roadDistance>before.roadDistance+5 name:"car_continues_during_ui_stall"];
        }
        [_view testKey:@"]"];[self check:_state.layer==std::min(1,_session->visualLayout().maxLayer) name:"cutaway_layer_next"];
        [_view testKey:@"["];[self check:_state.layer==0 name:"cutaway_layer_back"];
        _testStage=4;
    }
    if(_testStage==4 && t>12) {
        if(_state.roadView)_renderer.capture((_options.benchmark+".png").c_str());
        _testStage=5;
    }
    if(_state.roadView && _options.drive) {
        if(_testStage==5 && t>13) {
            // Benchmark commands are independent of actual window focus.
            [self activate:Idle];_session->command(AudioEngineRunner::Action::Brake,1);_testStage=6;
        } else if(_testStage==6 && t>14) {
            _renderer.capture((_options.benchmark+"-brake.png").c_str());_testStage=7;
        } else if(_testStage==7 && t>21) {
            const auto before=_renderer.metrics();SDL_Delay(600);const auto after=_renderer.metrics();
            [self check:_session->snapshot().vehicleSpeed<.1 && std::abs(after.roadDistance-before.roadDistance)<.01
                && after.frames>before.frames+15 name:"road_stops_with_vehicle"];
            _renderer.capture((_options.benchmark+"-stopped.png").c_str());_testStage=8;
        }
    }
    if(!_benchMeasured || now()-_benchAt<_options.seconds)return;
    const double seconds=now()-_benchAt;
    const auto last=_renderer.metrics();const auto audio=_session->statistics();
    const auto count=last.frames-_benchMetrics.frames;
    const double fps=count/seconds,cpu=(last.cpuNs-_benchMetrics.cpuNs)/1e6/std::max(uint64_t(1),count);
    const double gpu=(last.gpuNs-_benchMetrics.gpuNs)/1e6/std::max(uint64_t(1),count);
    const bool roadPassed=!(_state.roadView && _options.drive) || (last.roadDistance>20 && last.movingRoadFrames>120);
    const bool passed=_passed && roadPassed && count>0 && last.errors==0 && audio.silenceFrames==_benchAudio.silenceFrames && audio.writeErrors==0 && _session->snapshot().rpm>200;
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
        <<",\n  \"road_frames\": "<<last.roadFrames<<",\n  \"moving_road_frames\": "<<last.movingRoadFrames
        <<",\n  \"distance_metres\": "<<last.roadDistance
        <<",\n  \"vehicle_speed_mps\": "<<_session->snapshot().vehicleSpeed
        <<",\n  \"missing_frames\": "<<audio.silenceFrames-_benchAudio.silenceFrames
        <<",\n  \"write_errors\": "<<audio.writeErrors<<",\n  \"metal_errors\": "<<last.errors<<"\n}\n";
    report.close();
    std::cout<<"BENCHMARK result="<<(passed ? "PASS" : "FAIL")<<" render_fps="<<fps<<" cpu_ms="<<cpu<<" gpu_ms="<<gpu
        <<" game_x="<<_renderer.metrics().game.x<<" game_z="<<_renderer.metrics().game.z
                <<" laps="<<_renderer.metrics().game.laps<<" checkpoint="<<_renderer.metrics().game.nextCheckpoint
                <<" steering="<<_state.steering<<" collisions="<<_renderer.metrics().game.collisions
                <<" missing="<<audio.silenceFrames-_benchAudio.silenceFrames<<std::endl;
    _exitCode=passed ? 0 : 1;[NSApp terminate:nil];
}
- (void)arcadeTest:(double)t {
    const auto metrics=_renderer.metrics();const auto engine=_session->snapshot();
    auto pedal=[&](AudioEngineRunner::Action action,double value) {
        [self check:_session->command(action,value) name:"arcade_command_accepted"];
        if(action==AudioEngineRunner::Action::Throttle)_state.throttle=value;
    };
    using A=AudioEngineRunner::Action;
    if(_testStage==0 && t>3) {
        [self check:_gameAudio.attach(_session->device()) name:"arcade_pcm_probe_attached"];
        _benchAt=now();_benchMetrics=metrics;_benchAudio=_session->statistics();
        pedal(A::Throttle,.7);_testStage=1;
    } else if(_testStage==1 && t>4.5) {
        const auto blocks=engine.blocks;SDL_Delay(1200);
        [self check:_session->snapshot().blocks>blocks+120 name:"arcade_audio_runs_during_ui_stall"];
        [self check:_renderer.metrics().game.steps>metrics.game.steps+70 name:"arcade_game_runs_during_ui_stall"];
        _testStage=2;
    } else if(_testStage==2 && t>6) {
        [self check:metrics.game.speed>10 name:"arcade_accelerates_before_drift"];
        _state.steering=.8;_state.driftHeld=true;_testStage=3;
    } else if(_testStage==3 && t>6.65) {
        [self check:std::abs(metrics.game.driftAngle)>.15 && metrics.game.driftScore>1 name:"arcade_drift_moves_and_scores"];
        _renderer.capture((_options.arcadeTest+"-drift.png").c_str());++_expectedCaptures;
        _state.steering=0;_state.driftHeld=false;pedal(A::Throttle,0);pedal(A::Brake,1);_testStage=4;
    } else if(_testStage==4 && t>8.3) {
        [self check:engine.vehicleSpeed<.2 && engine.rpm>400 name:"arcade_brakes_without_stalling"];
        [self check:std::abs(metrics.game.driftAngle)<.02 && metrics.game.totalDriftScore>1 name:"arcade_release_straightens_and_banks_drift"];
        _gameBefore=metrics;pedal(A::Brake,0);pedal(A::BackPedal,1);_testStage=5;
    } else if(_testStage==5 && t>11) {
        [self check:engine.gear==-2 && metrics.game.speed<-1 && metrics.game.wheelDistance<_gameBefore.game.wheelDistance-2
            name:"arcade_reverse_uses_signed_vehicle_motion"];
        _renderer.capture((_options.arcadeTest+"-reverse.png").c_str());++_expectedCaptures;
        _gamePhaseAt=0;_testStage=6;
    } else if(_testStage==6 && _gamePhaseAt==0 && _renderer.captures()==_expectedCaptures) {
        _gameBefore=metrics;_gamePhaseAt=now();_renderer.stallRendering(1200);
    } else if(_testStage==6 && _gamePhaseAt>0 && now()-_gamePhaseAt>1.4) {
        [self check:metrics.game.steps>_gameBefore.game.steps+90 && metrics.game.wheelDistance<_gameBefore.game.wheelDistance-2
            name:"arcade_reverse_continues_during_render_stall"];
        [self check:engine.blocks>_gameBefore.audioBlocksSeen+150 && _session->statistics().silenceFrames==_benchAudio.silenceFrames
            name:"arcade_sound_continues_during_render_stall"];
        pedal(A::BackPedal,0);pedal(A::Throttle,.65);_testStage=7;
    } else if(_testStage==7 && t>15.5) {
        [self check:engine.gear>=0 && metrics.game.speed>1 && engine.rpm>400 name:"arcade_forward_pedal_stops_reverse_and_drives"];
        pedal(A::Throttle,0);pedal(A::Brake,1);_testStage=8;
    } else if(_testStage==8 && t>17.3) {
        [self check:engine.vehicleSpeed<.2 && engine.rpm>400 name:"arcade_final_stop_keeps_idle"];
        _testStage=9;
    }
    const bool timedOut=t>25;
    if(_testStage!=9 && !timedOut)return;
    if(_renderer.captures()<_expectedCaptures && !timedOut)return;
    [self check:!timedOut && _testStage==9 name:"arcade_sequence_completed"];
    [self check:_renderer.captures()>=2 name:"arcade_metal_captures_written"];
    _gameAudio.detach();const auto audio=_session->statistics();
    [self check:audio.silenceFrames==_benchAudio.silenceFrames && audio.writeErrors==0 name:"arcade_no_missing_audio_frames"];
    [self check:_gameAudio.frames>44100 && _gameAudio.silentBlocks==0 && _gameAudio.invalidSamples==0 name:"arcade_continuous_valid_pcm"];
    [self check:_gameAudio.clippedSamples==0 name:"arcade_no_clipped_pcm"];
    [self check:metrics.errors==0 name:"arcade_no_metal_errors"];
    const double seconds=now()-_benchAt;const uint64_t frames=metrics.frames-_benchMetrics.frames;
    std::ofstream report(_options.arcadeTest+".json");
    report<<std::fixed<<std::setprecision(4)<<"{\n  \"result\": \""<<(_passed ? "PASS" : "FAIL")<<"\",\n"
        <<"  \"driver\": \""<<SDL_GetCurrentAudioDriver()<<"\",\n  \"seconds\": "<<seconds
        <<",\n  \"render_fps_including_stall\": "<<frames/seconds
        <<",\n  \"cpu_mean_ms\": "<<(metrics.cpuNs-_benchMetrics.cpuNs)/1e6/std::max(uint64_t(1),frames)
        <<",\n  \"gpu_mean_ms\": "<<(metrics.gpuNs-_benchMetrics.gpuNs)/1e6/std::max(uint64_t(1),frames)
        <<",\n  \"drift_points\": "<<metrics.game.totalDriftScore
        <<",\n  \"missing_audio_frames\": "<<audio.silenceFrames-_benchAudio.silenceFrames
        <<",\n  \"silent_pcm_blocks\": "<<_gameAudio.silentBlocks.load()<<",\n  \"invalid_pcm_samples\": "<<_gameAudio.invalidSamples.load()
        <<",\n  \"clipped_pcm_samples\": "<<_gameAudio.clippedSamples.load()<<",\n  \"max_mixer_callback_gap_ms\": "<<_gameAudio.maxGapNs.load()/1e6
        <<",\n  \"metal_errors\": "<<metrics.errors<<"\n}\n";
    report.close();
    std::cout<<"ARCADE_RESULT="<<(_passed ? "PASS" : "FAIL")<<" missing="<<audio.silenceFrames-_benchAudio.silenceFrames<<std::endl;
    _exitCode=_passed ? 0 : 1;[NSApp terminate:nil];
}
- (void)gameTest:(double)t {
    auto metrics=_renderer.metrics();const auto engine=_session->snapshot();
    if(_testStage==0 && t>3) {
        [_view testKey:@"m"];[_view testKey:@" "];[_view testKey:@"d"];[_view testKey:@"x"];
        [self check:!_state.muted && !_state.brakeHeld && _state.steering==0 && _state.ignitionRequested
            name:"automatic_measurement_ignores_gameplay_keys"];
        [self check:_gameAudio.attach(_session->device()) name:"game_pcm_probe_attached"];
        _benchMeasured=true;_benchAt=now();_benchMetrics=metrics;_benchAudio=_session->statistics();
        [self check:metrics.sunVisible name:"world_sun_visible_at_start"];
        _renderer.capture((_options.gameTest+"-sun-start.png").c_str());
        _state.testPilot=true;_state.testKeyboard=true;_testStage=1;
    }
    if(_testStage>=1 && _testStage<=4) {
        if(!_gameSunMoved && metrics.sunVisible && std::abs(metrics.sunX-_benchMetrics.sunX)>120) {
            _gameSunMoved=true;_gameSunTurnX=metrics.sunX;
            _renderer.capture((_options.gameTest+"-sun-turn.png").c_str());
        }
        if(!metrics.sunVisible)_gameSunHidden=true;
        const double error=metrics.gameTargetSpeed-engine.vehicleSpeed;
        const double rawBrake=std::clamp(-error*.3,0.0,.85);
        const double brake=rawBrake>.04 ? rawBrake : 0;
        const double throttle=brake>.03 ? 0 : std::clamp(.28+error*.18,.05,.9);
        if(std::abs(throttle-_testThrottle)>.02) {_session->command(AudioEngineRunner::Action::Throttle,throttle);_testThrottle=throttle;_state.throttle=throttle;}
        if(std::abs(brake-_testBrake)>.02 || (brake==0 && _testBrake!=0)) {_session->command(AudioEngineRunner::Action::Brake,brake);_testBrake=brake;}
    }
    if(_testStage==1 && t>12) {
        const auto before=metrics;const auto blocks=engine.blocks;SDL_Delay(1200);const auto after=_renderer.metrics();
        [self check:after.game.steps>before.game.steps+70 && drivingLength({after.game.x-before.game.x,after.game.z-before.game.z})>5 name:"game_continues_during_ui_stall"];
        [self check:_session->snapshot().blocks>blocks+120 name:"audio_producer_continues_during_ui_stall"];
        _testStage=2;
    } else if(_testStage==2 && t>18) {
        _gameBefore=metrics;_gamePhaseAt=now();_renderer.stallRendering(1200);_testStage=3;
    } else if(_testStage==3 && now()-_gamePhaseAt>1.35) {
        [self check:metrics.game.steps>_gameBefore.game.steps+90 && drivingLength({metrics.game.x-_gameBefore.game.x,metrics.game.z-_gameBefore.game.z})>5 name:"game_continues_during_render_stall"];
        [self check:engine.blocks>_gameBefore.audioBlocksSeen+150 && _session->statistics().silenceFrames==_benchAudio.silenceFrames name:"sound_continues_during_render_stall"];
        _renderer.capture((_options.gameTest+"-corner.png").c_str());_testStage=4;
    } else if(_testStage==4 && metrics.game.laps>=1) {
        [self check:metrics.game.lastLap>30 && metrics.game.nextCheckpoint==1 name:"complete_lap_with_ordered_checkpoints"];
        [self check:metrics.game.collisions==0 name:"keyboard_circuit_drivable_without_collisions"];
        _gameBefore=metrics;_state.testPilot=false;_state.steering=1;_gamePhaseAt=now();
        _session->command(AudioEngineRunner::Action::Brake,0);_session->command(AudioEngineRunner::Action::Throttle,.85);_state.throttle=.85;_testStage=5;
    } else if(_testStage==5 && metrics.game.collisions>_gameBefore.game.collisions) {
        [self check:metrics.game.offroad && metrics.game.roadDeceleration>2 name:"offroad_and_collision_apply_vehicle_load"];
        _renderer.capture((_options.gameTest+"-collision.png").c_str());
        _gameBefore=metrics;_state.steering=0;_state.throttle=0;
        _session->command(AudioEngineRunner::Action::Throttle,0);_session->command(AudioEngineRunner::Action::Brake,1);
        _renderer.recoverCar();_gamePhaseAt=now();_testStage=6;
    } else if(_testStage==6 && metrics.game.recoveries>_gameBefore.game.recoveries) {
        [self check:engine.vehicleSpeed<1 && std::abs(metrics.game.lateral)<1 name:"recovery_stops_and_returns_to_checkpoint"];
        _renderer.capture((_options.gameTest+"-recovered.png").c_str());
        _state.testPilot=true;_session->command(AudioEngineRunner::Action::Brake,0);_session->command(AudioEngineRunner::Action::Throttle,.5);
        _state.throttle=.5;_gamePhaseAt=now();_testStage=7;
    } else if(_testStage==7 && now()-_gamePhaseAt>4) {
        [self check:engine.rpm>200 && engine.vehicleSpeed>1 name:"drive_after_recovery_without_engine_stall"];
        _testStage=8;
    }
    const bool timedOut=t>180 || ((_testStage==5 || _testStage==6) && now()-_gamePhaseAt>15);
    if(_testStage!=8 && !timedOut)return;
    if(timedOut)[self check:false name:"game_test_finished_before_timeout"];
    _gameAudio.detach();const auto audio=_session->statistics();
    [self check:audio.silenceFrames==_benchAudio.silenceFrames && audio.writeErrors==0 name:"game_no_missing_audio_frames"];
    [self check:_gameAudio.frames>44100 && _gameAudio.silentBlocks==0 && _gameAudio.invalidSamples==0 name:"game_continuous_valid_pcm"];
    [self check:_gameAudio.clippedSamples==0 name:"game_no_clipped_pcm"];
    [self check:metrics.errors==0 name:"game_no_metal_errors"];
    [self check:_gameSunMoved && _gameSunHidden name:"world_sun_moves_and_leaves_view_on_circuit"];
    const double seconds=now()-_benchAt;const uint64_t frames=metrics.frames-_benchMetrics.frames;
    std::ofstream report(_options.gameTest+".json");
    report<<std::fixed<<std::setprecision(4)<<"{\n  \"result\": \""<<(_passed ? "PASS" : "FAIL")<<"\",\n"
        <<"  \"steering\": \"keyboard\",\n  \"keyboard_sample_hz\": 10,\n"
        <<"  \"driver\": \""<<SDL_GetCurrentAudioDriver()<<"\",\n  \"seconds\": "<<seconds
        <<",\n  \"render_fps_including_stall\": "<<frames/seconds
        <<",\n  \"cpu_mean_ms\": "<<(metrics.cpuNs-_benchMetrics.cpuNs)/1e6/std::max(uint64_t(1),frames)
        <<",\n  \"gpu_mean_ms\": "<<(metrics.gpuNs-_benchMetrics.gpuNs)/1e6/std::max(uint64_t(1),frames)
        <<",\n  \"laps\": "<<metrics.game.laps<<",\n  \"best_lap_seconds\": "<<metrics.game.bestLap
        <<",\n  \"collisions\": "<<metrics.game.collisions<<",\n  \"recoveries\": "<<metrics.game.recoveries
        <<",\n  \"game_steps\": "<<metrics.game.steps<<",\n  \"missing_audio_frames\": "<<audio.silenceFrames-_benchAudio.silenceFrames
        <<",\n  \"sun_start_x\": "<<_benchMetrics.sunX<<",\n  \"sun_turn_x\": "<<_gameSunTurnX
        <<",\n  \"sun_left_view\": "<<(_gameSunHidden ? "true" : "false")
        <<",\n  \"silent_pcm_blocks\": "<<_gameAudio.silentBlocks.load()<<",\n  \"clipped_pcm_samples\": "<<_gameAudio.clippedSamples.load()
        <<",\n  \"max_audio_cpu_block_ms\": "<<engine.maxCpuBlockMs
        <<",\n  \"metal_errors\": "<<metrics.errors<<"\n}\n";
    report.close();
    std::cout<<"GAME_RESULT="<<(_passed ? "PASS" : "FAIL")<<" laps="<<metrics.game.laps<<" missing="<<audio.silenceFrames-_benchAudio.silenceFrames<<std::endl;
    _exitCode=_passed ? 0 : 1;[NSApp terminate:nil];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { (void)sender;return YES; }
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;_closing=true;[_timer invalidate];
    _gameAudio.detach();
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
                        <<"--play --drive --road --uncapped --no-effects --silent --muted --log FILE\n"
                        <<"--benchmark PREFIX [--seconds N] [--offscreen] [--uncapped]\n"
                        <<"--ui-test DIRECTORY  Hidden, programmatic input/audio/Metal checks\n"
                        <<"--self-test PREFIX [--preset ID] [--ui-stall-ms 0..1000]\n"
                        <<"--game-test PREFIX              Circuit, collision, recovery and audio test\n"
                        <<"--arcade-test PREFIX            Reverse, drift, render stalls and PCM checks\n"
                        <<"--drive-test PREFIX [--preset ID]  Acceleration/shifts/braking + PCM capture\n"
                        <<"--list-engines  List the IDs accepted by --preset\n"
                        <<"Keys: WASD/arrows drive, S/down brake then reverse in game, Shift assisted drift, G drive/neutral in game (A on dashboard), C recover, Backspace new run, Space brake in game / ignition on dashboard, X game ignition, hold R throttle, hold S dashboard brake, V car/pistons, B blip, I idle, M mute, F effects, U frame mode, E library, 1/2 favorites\n";
                    return 0;
                }
                if(arg=="--list-engines") {
                    for(const auto &preset:SoundSession::presets())std::cout<<preset.id<<"\t"<<preset.title<<'\n';
                    return 0;
                }
                if(arg=="--play"){options.play=true;continue;}
                if(arg=="--drive"){options.drive=true;continue;}
                if(arg=="--road"){options.roadView=true;continue;}
                if(arg=="--game-test" && i+1<argc){options.gameTest=argv[++i];options.play=true;options.drive=true;options.roadView=true;continue;}
                if(arg=="--arcade-test" && i+1<argc){options.arcadeTest=argv[++i];options.play=true;options.drive=true;options.roadView=true;continue;}
                if(arg=="--uncapped"){options.uncapped=true;continue;}
                if(arg=="--offscreen"){options.offscreen=true;continue;}
                if(arg=="--no-effects"){options.effects=false;continue;}
                if(arg=="--silent"){options.silent=true;continue;}
                if(arg=="--muted"){options.muted=true;continue;}
                if(i+1>=argc)throw std::runtime_error("Missing option value: "+arg);
                std::string value=argv[++i];
                if(arg=="--log")options.log=value;
                else if(arg=="--benchmark")options.benchmark=value;
                else if(arg=="--ui-test")options.uiTest=value;
                else if(arg=="--self-test")options.verify=value;
                else if(arg=="--drive-test")options.driveVerify=value;
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
        if(!options.verify.empty() || !options.driveVerify.empty()) {
            const int result=!options.driveVerify.empty() ? verifyDriveSession(options.assets,options.preset,options.driveVerify)
                : verifySoundSession(options.assets,options.preset,options.verify,options.stall);
            SDL_Quit();return result;
        }
        [NSApplication sharedApplication];
        __attribute__((objc_precise_lifetime)) SoundApp *controller=[[SoundApp alloc] initWithOptions:options];
        NSApp.delegate=controller;[NSApp run];return [controller exitCode];
    }
}
