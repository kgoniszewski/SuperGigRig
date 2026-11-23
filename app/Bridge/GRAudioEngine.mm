// GRAudioEngine.mm - system I/O audio unit in full duplex, running GigRigDSP.
//
// Uses the AudioUnit C API (AudioComponentInstance, AudioUnitRender) for the HAL output
// unit on macOS and the Remote I/O unit on iPadOS. Both units and these calls have been
// stable for many OS releases, and the C render callback keeps the audio thread free of
// Objective-C and Swift (docs/architecture.md, sections 3 and 6).
//
// The render callback only touches RenderState, which is allocated once when the engine
// is created and reconfigured only while the unit is stopped.
#import "GRAudioEngine.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <CoreAudio/CoreAudio.h>
#endif

#include "RealtimeIO.hpp"

#include <gigrig/gigrig.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

constexpr int kMaxChannels = 32;
constexpr int kMaxFrames = gigrig::app::RealtimeIO::kMaxFrames;
constexpr AudioUnitElement kInputElement = 1;   // the device input side of an I/O unit
constexpr AudioUnitElement kOutputElement = 0;  // the device output side

// Everything the render callback reads. Allocated once; never resized while running.
struct RenderState {
    AudioUnit unit = nullptr;
    int numIn = 0;
    gigrig::app::RealtimeIO io;
    std::unique_ptr<float[]> inputStorage;  // kMaxChannels * kMaxFrames
    std::unique_ptr<uint8_t[]> inputListStorage;
    AudioBufferList* inputList = nullptr;
    const float* inPtrs[kMaxChannels] = {};
    float* outPtrs[kMaxChannels] = {};

    RenderState() {
        inputStorage = std::make_unique<float[]>(static_cast<size_t>(kMaxChannels) * kMaxFrames);
        const size_t listBytes =
            offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * static_cast<size_t>(kMaxChannels);
        inputListStorage = std::make_unique<uint8_t[]>(listBytes);
        std::fill(inputListStorage.get(), inputListStorage.get() + listBytes, uint8_t{0});
        inputList = reinterpret_cast<AudioBufferList*>(inputListStorage.get());
    }
};

// Real-time: no allocation, no locks, no Objective-C.
OSStatus renderCallback(void* refCon, AudioUnitRenderActionFlags* /*ioActionFlags*/,
                        const AudioTimeStamp* timeStamp, UInt32 /*busNumber*/, UInt32 numFrames,
                        AudioBufferList* ioData) {
    auto* s = static_cast<RenderState*>(refCon);
    if (ioData == nullptr) return noErr;
    const int frames = static_cast<int>(numFrames);
    const int numOut = std::min(static_cast<int>(ioData->mNumberBuffers), kMaxChannels);
    for (int c = 0; c < numOut; ++c) {
        s->outPtrs[c] = static_cast<float*>(ioData->mBuffers[c].mData);
    }
    if (frames > kMaxFrames) {
        gigrig::app::RealtimeIO::clearOutputs(s->outPtrs, numOut, frames);
        return noErr;
    }

    // AudioUnitRender may shrink mDataByteSize or swap mData, so restore both every cycle.
    AudioBufferList* list = s->inputList;
    list->mNumberBuffers = static_cast<UInt32>(s->numIn);
    for (int c = 0; c < s->numIn; ++c) {
        list->mBuffers[c].mNumberChannels = 1;
        list->mBuffers[c].mDataByteSize = static_cast<UInt32>(numFrames * sizeof(float));
        list->mBuffers[c].mData = s->inputStorage.get() + static_cast<size_t>(c) * kMaxFrames;
    }
    AudioUnitRenderActionFlags inFlags = 0;
    const OSStatus err = AudioUnitRender(s->unit, &inFlags, timeStamp, kInputElement, numFrames, list);
    const int numIn = (err == noErr) ? s->numIn : 0;
    for (int c = 0; c < numIn; ++c) {
        s->inPtrs[c] = static_cast<const float*>(list->mBuffers[c].mData);
    }
    s->io.renderMono(numIn > 0 ? s->inPtrs : nullptr, numIn, s->outPtrs, numOut, frames);
    return noErr;
}

// Native float, non-interleaved: one buffer per device channel.
AudioStreamBasicDescription floatFormat(double sampleRate, int channels) {
    AudioStreamBasicDescription f = {};
    f.mSampleRate = sampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    // The two constants come from different unnamed enums; combine them as AudioFormatFlags.
    f.mFormatFlags = static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked) |
                     static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
    f.mBytesPerPacket = sizeof(float);
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = sizeof(float);
    f.mChannelsPerFrame = static_cast<UInt32>(channels);
    f.mBitsPerChannel = 32;
    return f;
}

#if TARGET_OS_OSX

AudioObjectPropertyAddress address(AudioObjectPropertySelector sel,
                                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return AudioObjectPropertyAddress{sel, scope, kAudioObjectPropertyElementMain};
}

std::vector<AudioObjectID> allDeviceIDs() {
    AudioObjectPropertyAddress a = address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr) return {};
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (ids.empty()) return ids;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr) {
        return {};
    }
    ids.resize(size / sizeof(AudioObjectID));
    return ids;
}

NSString* deviceString(AudioObjectID dev, AudioObjectPropertySelector sel) {
    AudioObjectPropertyAddress a = address(sel);
    CFStringRef str = nullptr;
    UInt32 size = sizeof(str);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &str) != noErr || str == nullptr) return @"";
    return (__bridge_transfer NSString*)str;
}

int channelCount(AudioObjectID dev, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a = address(kAudioDevicePropertyStreamConfiguration, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::vector<uint8_t> storage(size);
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, list) != noErr) return 0;
    int total = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        total += static_cast<int>(list->mBuffers[i].mNumberChannels);
    return total;
}

UInt32 deviceUInt32(AudioObjectID dev, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a = address(sel, scope);
    UInt32 value = 0;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &value) != noErr) return 0;
    return value;
}

Float64 nominalRate(AudioObjectID dev) {
    AudioObjectPropertyAddress a = address(kAudioDevicePropertyNominalSampleRate);
    Float64 rate = 0;
    UInt32 size = sizeof(rate);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &rate) != noErr) return 0;
    return rate;
}

// Latency of the first stream on one side of the device (part of what the HAL adds).
UInt32 streamLatency(AudioObjectID dev, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a = address(kAudioDevicePropertyStreams, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size < sizeof(AudioStreamID)) {
        return 0;
    }
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, streams.data()) != noErr) return 0;
    return deviceUInt32(streams[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal);
}

// Device properties whose change means a restart or a new latency report.
const AudioObjectPropertySelector kWatchedDeviceProperties[] = {
    kAudioDevicePropertyNominalSampleRate,
    kAudioDevicePropertyBufferFrameSize,
    kAudioDevicePropertyDeviceIsAlive,
};

bool deviceIsAlive(AudioObjectID dev) {
    return deviceUInt32(dev, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal) != 0;
}

#endif  // TARGET_OS_OSX

NSString* describeStatus(OSStatus err) {
    const UInt32 code = static_cast<UInt32>(err);
    char c[4] = {static_cast<char>((code >> 24) & 0xFF), static_cast<char>((code >> 16) & 0xFF),
                 static_cast<char>((code >> 8) & 0xFF), static_cast<char>(code & 0xFF)};
    bool printable = true;
    for (char ch : c) printable = printable && ch >= 32 && ch < 127;
    if (printable) return [NSString stringWithFormat:@"'%c%c%c%c' (%d)", c[0], c[1], c[2], c[3], (int)err];
    return [NSString stringWithFormat:@"%d", (int)err];
}

}  // namespace

// MARK: - Value objects

@interface GRAudioDevice ()
- (instancetype)initWithUID:(NSString*)uid name:(NSString*)name inputs:(NSInteger)ins outputs:(NSInteger)outs;
#if TARGET_OS_OSX
@property(nonatomic) AudioObjectID objectID;
#endif
@end

@implementation GRAudioDevice
- (instancetype)initWithUID:(NSString*)uid
                       name:(NSString*)name
                     inputs:(NSInteger)ins
                    outputs:(NSInteger)outs {
    if ((self = [super init])) {
        _uid = [uid copy];
        _name = [name copy];
        _inputChannels = ins;
        _outputChannels = outs;
    }
    return self;
}
@end

@interface GRLatencyReport ()
@property(nonatomic, readwrite) double sampleRate;
@property(nonatomic, readwrite) NSInteger bufferFrames;
@property(nonatomic, readwrite) NSInteger inputDeviceLatencyFrames;
@property(nonatomic, readwrite) NSInteger inputSafetyOffsetFrames;
@property(nonatomic, readwrite) NSInteger outputDeviceLatencyFrames;
@property(nonatomic, readwrite) NSInteger outputSafetyOffsetFrames;
@property(nonatomic, readwrite) NSInteger dspLatencyFrames;
@end

@implementation GRLatencyReport
- (NSInteger)estimatedRoundTripFrames {
    return 2 * _bufferFrames + _inputDeviceLatencyFrames + _inputSafetyOffsetFrames +
           _outputDeviceLatencyFrames + _outputSafetyOffsetFrames + _dspLatencyFrames;
}
- (double)estimatedRoundTripMs {
    return _sampleRate > 0 ? 1000.0 * static_cast<double>(self.estimatedRoundTripFrames) / _sampleRate : 0.0;
}
@end

// MARK: - Engine

@implementation GRAudioEngine {
    std::unique_ptr<RenderState> _render;
    GigRigEngine* _dsp;
    AudioUnit _unit;
    BOOL _wantsRunning;
    BOOL _restartPending;
    double _runningRate;
    NSInteger _loopbackExpected;
    BOOL _loopbackActive;
#if TARGET_OS_OSX
    AudioObjectID _listenedDevice;
    AudioObjectPropertyListenerBlock _systemListener;
    AudioObjectPropertyListenerBlock _deviceListener;
#else
    NSMutableArray<id<NSObject> >* _observers;
#endif
}

- (instancetype)init {
    if ((self = [super init])) {
        _render = std::make_unique<RenderState>();
        _dsp = gigrig_create();
        _preferredSampleRate = 48000.0;
        _preferredBufferFrames = 32;
        _inputChannel = 0;
        _outputToBothChannels = NO;
        _state = GREngineStateStopped;
        _statusMessage = @"Stopped";
#if TARGET_OS_OSX
        [self installSystemListener];
#else
        [self installSessionObservers];
#endif
    }
    return self;
}

- (void)dealloc {
    [self teardownUnit];
#if TARGET_OS_OSX
    [self removeDeviceListener];
    if (_systemListener != nil) {
        AudioObjectPropertyAddress a = address(kAudioHardwarePropertyDevices);
        AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &a, dispatch_get_main_queue(),
                                               _systemListener);
    }
#else
    for (id<NSObject> o in _observers) [[NSNotificationCenter defaultCenter] removeObserver:o];
#endif
    gigrig_destroy(_dsp);
}

// MARK: Settings

- (void)setPreferredDeviceUID:(NSString*)uid {
    if ((uid == nil && _preferredDeviceUID == nil) || [uid isEqualToString:_preferredDeviceUID]) return;
    _preferredDeviceUID = [uid copy];
    [self restartIfWanted];
}

- (void)setPreferredSampleRate:(double)rate {
    if (rate == _preferredSampleRate) return;
    _preferredSampleRate = rate;
    [self restartIfWanted];
}

- (void)setPreferredBufferFrames:(NSInteger)frames {
    if (frames == _preferredBufferFrames) return;
    _preferredBufferFrames = frames;
    [self restartIfWanted];
}

- (void)setInputChannel:(NSInteger)channel {
    _inputChannel = std::max<NSInteger>(0, channel);
    _render->io.setInputChannel(static_cast<int>(_inputChannel));
}

- (void)setOutputToBothChannels:(BOOL)both {
    _outputToBothChannels = both;
    _render->io.setOutputToBoth(both);
}

// MARK: Start and stop

- (void)start {
    _wantsRunning = YES;
    [self restart];
}

- (void)stop {
    _wantsRunning = NO;
    [self teardownUnit];
    [self setState:GREngineStateStopped message:@"Stopped"];
}

- (void)restartIfWanted {
    if (_wantsRunning) [self restart];
}

// Coalesces bursts of device notifications into one restart on the next main-queue turn.
- (void)scheduleRestart {
    if (_restartPending) return;
    _restartPending = YES;
    __weak GRAudioEngine* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
      GRAudioEngine* strongSelf = weakSelf;
      if (strongSelf == nil) return;
      strongSelf->_restartPending = NO;
      [strongSelf restartIfWanted];
    });
}

- (void)restart {
    [self teardownUnit];
    if (!_wantsRunning) return;
    [self configureAndStart];
}

- (void)setState:(GREngineState)state message:(NSString*)message {
    _state = state;
    _statusMessage = [message copy];
    if (state != GREngineStateRunning) _latency = nil;
    if (self.onChange) self.onChange();
}

- (void)teardownUnit {
    if (_unit != nullptr) {
        AudioOutputUnitStop(_unit);
        AudioUnitUninitialize(_unit);
        AudioComponentInstanceDispose(_unit);
        _unit = nullptr;
    }
    _render->unit = nullptr;
    _render->numIn = 0;
    _runningRate = 0;
    _loopbackActive = NO;
#if TARGET_OS_OSX
    [self removeDeviceListener];
#endif
}

// Creates, configures and starts the I/O unit on the chosen device at `rate`.
// Returns nil on success, or a description of the step that failed.
- (NSString*)startUnitWithSubType:(OSType)subType
#if TARGET_OS_OSX
                           device:(AudioObjectID)device
#endif
                             rate:(double)rate
                           inputs:(int)numIn
                          outputs:(int)numOut {
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = subType;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr) return @"No system I/O audio unit found";

    OSStatus err = AudioComponentInstanceNew(comp, &_unit);
    if (err != noErr) {
        _unit = nullptr;
        return [NSString stringWithFormat:@"Creating the I/O unit failed: %@", describeStatus(err)];
    }

#define GR_CHECK(expr, what)                                                                            \
    do {                                                                                                \
        OSStatus e_ = (expr);                                                                           \
        if (e_ != noErr) return [NSString stringWithFormat:@"%@ failed: %@", what, describeStatus(e_)]; \
    } while (0)

    UInt32 one = 1;
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input,
                                  kInputElement, &one, sizeof(one)),
             @"Enabling input");
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output,
                                  kOutputElement, &one, sizeof(one)),
             @"Enabling output");
#if TARGET_OS_OSX
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                  &device, sizeof(device)),
             @"Selecting the device");
#endif
    // Client formats at the device rate, so the unit never inserts a sample rate converter.
    AudioStreamBasicDescription inFmt = floatFormat(rate, numIn);
    AudioStreamBasicDescription outFmt = floatFormat(rate, numOut);
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                  kInputElement, &inFmt, sizeof(inFmt)),
             @"Setting the input format");
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                                  kOutputElement, &outFmt, sizeof(outFmt)),
             @"Setting the output format");
    UInt32 maxFrames = kMaxFrames;
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                                  &maxFrames, sizeof(maxFrames)),
             @"Setting the maximum frames per slice");

    _render->unit = _unit;
    _render->numIn = numIn;
    if (!_render->io.prepare(_dsp, rate)) return @"Preparing the DSP core failed";
    _render->io.setInputChannel(static_cast<int>(_inputChannel));
    _render->io.setOutputToBoth(_outputToBothChannels);

    AURenderCallbackStruct cb = {};
    cb.inputProc = renderCallback;
    cb.inputProcRefCon = _render.get();
    GR_CHECK(AudioUnitSetProperty(_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                  kOutputElement, &cb, sizeof(cb)),
             @"Installing the render callback");
    GR_CHECK(AudioUnitInitialize(_unit), @"Initialising the I/O unit");
    GR_CHECK(AudioOutputUnitStart(_unit), @"Starting the I/O unit");
#undef GR_CHECK
    _runningRate = rate;
    return nil;
}

// MARK: Loopback

- (void)startLoopbackMeasurementWithPings:(NSInteger)pings {
    if (_state != GREngineStateRunning || self.loopbackPhase == GRLoopbackPhaseMeasuring) return;
    const NSInteger n = std::clamp<NSInteger>(pings, 1, gigrig::app::LoopbackProbe::kMaxPings);
    const double rate = _runningRate;
    // 200 ms of silence before each click lets the previous one die away (and gives the
    // probe its noise measurement); a click that has not come back after 500 ms means the
    // cable or the input channel is wrong. -34 dBFS is only the lowest threshold: the
    // probe raises it above the measured noise.
    _render->io.probe().arm(static_cast<int>(n), static_cast<int>(0.2 * rate), static_cast<int>(0.5 * rate),
                            0.02f);
    _loopbackExpected = n;
    _loopbackActive = YES;
}

- (GRLoopbackPhase)loopbackPhase {
    if (!_loopbackActive) {
        return _render->io.probe().resultCount() > 0 ? GRLoopbackPhaseDone : GRLoopbackPhaseIdle;
    }
    if (_render->io.probe().resultCount() >= _loopbackExpected) {
        _loopbackActive = NO;
        return GRLoopbackPhaseDone;
    }
    return GRLoopbackPhaseMeasuring;
}

- (NSArray<NSNumber*>*)loopbackResults {
    auto& probe = _render->io.probe();
    const int count = probe.resultCount();
    NSMutableArray<NSNumber*>* out = [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(count)];
    for (int i = 0; i < count; ++i) [out addObject:@(probe.result(i))];
    return out;
}

#if TARGET_OS_OSX

// MARK: - macOS: HAL output unit

- (NSArray<GRAudioDevice*>*)availableDevices {
    NSMutableArray<GRAudioDevice*>* list = [NSMutableArray array];
    for (AudioObjectID dev : allDeviceIDs()) {
        const int ins = channelCount(dev, kAudioObjectPropertyScopeInput);
        const int outs = channelCount(dev, kAudioObjectPropertyScopeOutput);
        if (ins == 0 || outs == 0) continue;  // full duplex on one device only
        GRAudioDevice* d = [[GRAudioDevice alloc] initWithUID:deviceString(dev, kAudioDevicePropertyDeviceUID)
                                                         name:deviceString(dev, kAudioObjectPropertyName)
                                                       inputs:ins
                                                      outputs:outs];
        d.objectID = dev;
        [list addObject:d];
    }
    return list;
}

- (GRAudioDevice*)chooseDevice {
    NSArray<GRAudioDevice*>* devices = [self availableDevices];
    if (_preferredDeviceUID.length > 0) {
        for (GRAudioDevice* d in devices) {
            if ([d.uid isEqualToString:_preferredDeviceUID]) return d;
        }
        return nil;  // the chosen device is unplugged: wait for it rather than pick another
    }
    for (GRAudioDevice* d in devices) {
        if ([d.name rangeOfString:@"AXE I/O" options:NSCaseInsensitiveSearch].location != NSNotFound)
            return d;
    }
    return devices.firstObject;
}

- (void)configureAndStart {
    GRAudioDevice* device = [self chooseDevice];
    if (device == nil) {
        NSString* msg = _preferredDeviceUID.length > 0
                            ? @"The selected audio device is not connected"
                            : @"No audio device with both input and output: connect the AXE I/O One";
        [self setState:GREngineStateWaitingForDevice message:msg];
        return;
    }
    _currentDevice = device;
    const AudioObjectID dev = device.objectID;

    // Sample rate: set the device's nominal rate. The change is asynchronous, so wait
    // briefly for it; if the device refuses, run at whatever rate it has.
    if (nominalRate(dev) != _preferredSampleRate) {
        AudioObjectPropertyAddress a = address(kAudioDevicePropertyNominalSampleRate);
        Float64 wanted = _preferredSampleRate;
        if (AudioObjectSetPropertyData(dev, &a, 0, nullptr, sizeof(wanted), &wanted) == noErr) {
            for (int i = 0; i < 50 && nominalRate(dev) != wanted; ++i) [NSThread sleepForTimeInterval:0.02];
        }
    }
    const double rate = nominalRate(dev);
    if (!(rate >= GIGRIG_MIN_SAMPLE_RATE && rate <= GIGRIG_MAX_SAMPLE_RATE)) {
        [self setState:GREngineStateFailed message:@"The device reports an unusable sample rate"];
        return;
    }

    // Buffer size, clamped to what the device allows.
    {
        AudioObjectPropertyAddress rangeAddr = address(kAudioDevicePropertyBufferFrameSizeRange);
        AudioValueRange range = {0, 0};
        UInt32 size = sizeof(range);
        UInt32 frames = static_cast<UInt32>(std::max<NSInteger>(1, _preferredBufferFrames));
        if (AudioObjectGetPropertyData(dev, &rangeAddr, 0, nullptr, &size, &range) == noErr &&
            range.mMaximum > 0) {
            frames = static_cast<UInt32>(
                std::clamp<double>(frames, std::ceil(range.mMinimum), std::floor(range.mMaximum)));
        }
        AudioObjectPropertyAddress a = address(kAudioDevicePropertyBufferFrameSize);
        AudioObjectSetPropertyData(dev, &a, 0, nullptr, sizeof(frames), &frames);
    }

    const int numIn = std::min(static_cast<int>(device.inputChannels), kMaxChannels);
    const int numOut = std::min(static_cast<int>(device.outputChannels), kMaxChannels);
    NSString* error = [self startUnitWithSubType:kAudioUnitSubType_HALOutput
                                          device:dev
                                            rate:rate
                                          inputs:numIn
                                         outputs:numOut];
    if (error != nil) {
        [self teardownUnit];
        [self setState:GREngineStateFailed message:error];
        return;
    }
    [self installDeviceListener:dev];
    _latency = [self measureLatencyReport:dev rate:rate];
    [self setState:GREngineStateRunning message:@"Running"];
}

- (GRLatencyReport*)measureLatencyReport:(AudioObjectID)dev rate:(double)rate {
    GRLatencyReport* r = [[GRLatencyReport alloc] init];
    r.sampleRate = rate;
    r.bufferFrames = deviceUInt32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
    r.inputDeviceLatencyFrames =
        deviceUInt32(dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput) +
        streamLatency(dev, kAudioObjectPropertyScopeInput);
    r.inputSafetyOffsetFrames =
        deviceUInt32(dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput);
    r.outputDeviceLatencyFrames =
        deviceUInt32(dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput) +
        streamLatency(dev, kAudioObjectPropertyScopeOutput);
    r.outputSafetyOffsetFrames =
        deviceUInt32(dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput);
    r.dspLatencyFrames = gigrig_latency_samples(_dsp);
    return r;
}

// Device list changes (plug, unplug): restart if the device we need appeared or vanished.
- (void)installSystemListener {
    __weak GRAudioEngine* weakSelf = self;
    _systemListener = ^(UInt32 numAddresses, const AudioObjectPropertyAddress* addresses) {
      (void)numAddresses;
      (void)addresses;
      GRAudioEngine* s = weakSelf;
      if (s == nil || !s->_wantsRunning) return;
      const bool running = s->_state == GREngineStateRunning;
      const bool currentGone =
          running && s->_currentDevice != nil && !deviceIsAlive(s->_currentDevice.objectID);
      if (!running || currentGone)
          [s scheduleRestart];
      else if (s.onChange)
          s.onChange();  // the device list in Settings changed
    };
    AudioObjectPropertyAddress a = address(kAudioHardwarePropertyDevices);
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &a, dispatch_get_main_queue(),
                                        _systemListener);
}

// Rate changes by another app, buffer changes, and the device dying.
- (void)installDeviceListener:(AudioObjectID)dev {
    [self removeDeviceListener];
    __weak GRAudioEngine* weakSelf = self;
    _deviceListener = ^(UInt32 numAddresses, const AudioObjectPropertyAddress* addresses) {
      (void)numAddresses;
      (void)addresses;
      GRAudioEngine* s = weakSelf;
      if (s == nil || s->_state != GREngineStateRunning) return;
      const AudioObjectID d = s->_listenedDevice;
      if (!deviceIsAlive(d) || nominalRate(d) != s->_runningRate) {
          [s scheduleRestart];  // the DSP must be prepared again at the new rate
      } else {
          s->_latency = [s measureLatencyReport:d rate:s->_runningRate];
          if (s.onChange) s.onChange();
      }
    };
    _listenedDevice = dev;
    for (AudioObjectPropertySelector sel : kWatchedDeviceProperties) {
        AudioObjectPropertyAddress a = address(sel);
        AudioObjectAddPropertyListenerBlock(dev, &a, dispatch_get_main_queue(), _deviceListener);
    }
}

- (void)removeDeviceListener {
    if (_deviceListener == nil) return;
    for (AudioObjectPropertySelector sel : kWatchedDeviceProperties) {
        AudioObjectPropertyAddress a = address(sel);
        AudioObjectRemovePropertyListenerBlock(_listenedDevice, &a, dispatch_get_main_queue(),
                                               _deviceListener);
    }
    _deviceListener = nil;
    _listenedDevice = kAudioObjectUnknown;
}

#else  // iPadOS

// MARK: - iPadOS: Remote I/O unit and AVAudioSession

- (GRAudioDevice*)routeDevice {
    AVAudioSessionRouteDescription* route = [AVAudioSession sharedInstance].currentRoute;
    AVAudioSessionPortDescription* in = route.inputs.firstObject;
    AVAudioSessionPortDescription* out = route.outputs.firstObject;
    if (in == nil || out == nil) return nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    return [[GRAudioDevice alloc] initWithUID:in.UID
                                         name:in.portName
                                       inputs:session.inputNumberOfChannels
                                      outputs:session.outputNumberOfChannels];
}

- (NSArray<GRAudioDevice*>*)availableDevices {
    GRAudioDevice* d = [self routeDevice];
    return d != nil ? @[ d ] : @[];
}

- (void)configureAndStart {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    // Measurement mode turns off the system's voice processing and automatic gain on the input.
    if (![session setCategory:AVAudioSessionCategoryPlayAndRecord
                         mode:AVAudioSessionModeMeasurement
                      options:0
                        error:&error]) {
        [self setState:GREngineStateFailed
               message:[NSString stringWithFormat:@"Audio session category: %@", error.localizedDescription]];
        return;
    }
    [session setPreferredSampleRate:_preferredSampleRate error:nil];
    [session setPreferredIOBufferDuration:static_cast<double>(_preferredBufferFrames) / _preferredSampleRate
                                    error:nil];
    if (![session setActive:YES error:&error]) {
        [self
            setState:GREngineStateFailed
             message:[NSString stringWithFormat:@"Audio session activation: %@", error.localizedDescription]];
        return;
    }
    // Ask for every channel the interface has, so input 2 can be selected.
    [session setPreferredInputNumberOfChannels:session.maximumInputNumberOfChannels error:nil];
    [session setPreferredOutputNumberOfChannels:session.maximumOutputNumberOfChannels error:nil];
    // Ask for the buffer again now that the route and channel count are final: a route
    // or channel change can reset the I/O buffer to the system default.
    [session setPreferredSampleRate:_preferredSampleRate error:nil];
    [session setPreferredIOBufferDuration:static_cast<double>(_preferredBufferFrames) / session.sampleRate
                                    error:nil];

    AVAudioSessionRouteDescription* route = session.currentRoute;
    AVAudioSessionPortDescription* in = route.inputs.firstObject;
    AVAudioSessionPortDescription* out = route.outputs.firstObject;
    // The built-in mic into the built-in speaker would howl; wait for the interface instead.
    if (in == nil || out == nil ||
        ([in.portType isEqualToString:AVAudioSessionPortBuiltInMic] &&
         [out.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker])) {
        [self setState:GREngineStateWaitingForDevice message:@"Connect the AXE I/O One over USB-C"];
        return;
    }
    _currentDevice = [self routeDevice];

    const double rate = session.sampleRate;
    if (!(rate >= GIGRIG_MIN_SAMPLE_RATE && rate <= GIGRIG_MAX_SAMPLE_RATE)) {
        [self setState:GREngineStateFailed message:@"The audio session reports an unusable sample rate"];
        return;
    }
    const int numIn = std::clamp(static_cast<int>(session.inputNumberOfChannels), 1, kMaxChannels);
    const int numOut = std::clamp(static_cast<int>(session.outputNumberOfChannels), 1, kMaxChannels);
    NSString* failure = [self startUnitWithSubType:kAudioUnitSubType_RemoteIO
                                              rate:rate
                                            inputs:numIn
                                           outputs:numOut];
    if (failure != nil) {
        [self teardownUnit];
        [self setState:GREngineStateFailed message:failure];
        return;
    }
    _latency = [self sessionLatencyReport];
    [self setState:GREngineStateRunning message:@"Running"];
}

- (GRLatencyReport*)sessionLatencyReport {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    const double rate = session.sampleRate;
    GRLatencyReport* r = [[GRLatencyReport alloc] init];
    r.sampleRate = rate;
    r.bufferFrames = std::lround(session.IOBufferDuration * rate);
    r.inputDeviceLatencyFrames = std::lround(session.inputLatency * rate);
    r.outputDeviceLatencyFrames = std::lround(session.outputLatency * rate);
    r.inputSafetyOffsetFrames = 0;
    r.outputSafetyOffsetFrames = 0;
    r.dspLatencyFrames = gigrig_latency_samples(_dsp);
    return r;
}

- (void)installSessionObservers {
    _observers = [NSMutableArray array];
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    NSOperationQueue* main = [NSOperationQueue mainQueue];
    AVAudioSession* session = [AVAudioSession sharedInstance];
    __weak GRAudioEngine* weakSelf = self;

    [_observers addObject:[nc addObserverForName:AVAudioSessionRouteChangeNotification
                                          object:session
                                           queue:main
                                      usingBlock:^(NSNotification* note) {
                                        GRAudioEngine* s = weakSelf;
                                        if (s == nil) return;
                                        const NSUInteger reason =
                                            [note.userInfo[AVAudioSessionRouteChangeReasonKey]
                                                unsignedIntegerValue];
                                        // Our own category change also posts a route change; only
                                        // plugging and unplugging needs a restart.
                                        if (reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable ||
                                            reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
                                            [s scheduleRestart];
                                        }
                                      }]];

    // iOS 27 replaces the interruption notification with session life-cycle notifications.
    // The app never deactivates its own session, so becoming inactive always means the
    // system took it (a call, another app); stop cleanly and wait for the recommendation.
    [_observers addObject:[nc addObserverForName:AVAudioSessionDidBecomeInactiveNotification
                                          object:session
                                           queue:main
                                      usingBlock:^(NSNotification* note) {
                                        (void)note;
                                        GRAudioEngine* s = weakSelf;
                                        if (s == nil || !s->_wantsRunning) return;
                                        [s teardownUnit];
                                        [s setState:GREngineStateInterrupted
                                            message:@"Interrupted by another app"];
                                      }]];

    // Posted when an interruption ends, and also when an activation was refused. Restart
    // only from the interrupted state: a refused restart then lands in Failed instead of
    // looping. A live rig should come back on its own, so the recommendation itself is
    // not read; if the system still holds the audio, activation fails and says so.
    [_observers addObject:[nc addObserverForName:AVAudioSessionResumptionRecommendationNotification
                                          object:session
                                           queue:main
                                      usingBlock:^(NSNotification* note) {
                                        (void)note;
                                        GRAudioEngine* s = weakSelf;
                                        if (s == nil || s->_state != GREngineStateInterrupted) return;
                                        [s scheduleRestart];
                                      }]];

    [_observers addObject:[nc addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                                          object:session
                                           queue:main
                                      usingBlock:^(NSNotification* note) {
                                        (void)note;
                                        // The media server restarted: rebuild the session and the unit.
                                        [weakSelf scheduleRestart];
                                      }]];
}

#endif

@end
