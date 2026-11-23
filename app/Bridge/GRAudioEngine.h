// GRAudioEngine.h - the audio engine bridge seen by Swift.
//
// Owns the system I/O audio unit (HAL output on macOS, Remote I/O on iPadOS) in full
// duplex on one device, and the GigRigDSP core it runs in the render callback.
// See docs/architecture.md, section 6.
//
// Every method and property here is for the main thread only. The render callback is
// plain C++ (RealtimeIO.hpp) and never calls back into Objective-C or Swift.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, GREngineState) {
    GREngineStateStopped = 0,     // not started, or stopped by the user
    GREngineStateRunning,         // audio is flowing
    GREngineStateWaitingForDevice,// started, but no usable duplex device (unplugged, wrong route)
    GREngineStateInterrupted,     // iPadOS: another app or a call took the audio session
    GREngineStateFailed           // Core Audio returned an error; see statusMessage
};

typedef NS_ENUM(NSInteger, GRLoopbackPhase) {
    GRLoopbackPhaseIdle = 0,
    GRLoopbackPhaseMeasuring,
    GRLoopbackPhaseDone
};

/// One audio device that can do input and output at the same time.
@interface GRAudioDevice : NSObject
@property (nonatomic, readonly, copy) NSString *uid;
@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, readonly) NSInteger inputChannels;
@property (nonatomic, readonly) NSInteger outputChannels;
@end

/// The latency numbers the device and the system report for the running configuration.
/// All frame counts are at sampleRate. The estimate is what Core Audio claims; only the
/// loopback test measures the real round trip.
@interface GRLatencyReport : NSObject
@property (nonatomic, readonly) double sampleRate;
@property (nonatomic, readonly) NSInteger bufferFrames;            // granted I/O buffer
@property (nonatomic, readonly) NSInteger inputDeviceLatencyFrames; // device + stream latency, input side
@property (nonatomic, readonly) NSInteger inputSafetyOffsetFrames;  // macOS only, 0 on iPadOS
@property (nonatomic, readonly) NSInteger outputDeviceLatencyFrames;
@property (nonatomic, readonly) NSInteger outputSafetyOffsetFrames;
@property (nonatomic, readonly) NSInteger dspLatencyFrames;         // always 0 by design
/// Input buffer + output buffer + device latencies + safety offsets + DSP latency.
@property (nonatomic, readonly) NSInteger estimatedRoundTripFrames;
@property (nonatomic, readonly) double estimatedRoundTripMs;
@end

@interface GRAudioEngine : NSObject

/// Called on the main thread whenever state, device or latency changes.
@property (nonatomic, copy, nullable) void (^onChange)(void);

@property (nonatomic, readonly) GREngineState state;
@property (nonatomic, readonly, copy) NSString *statusMessage;

/// The device in use (or last used). nil before the first start.
@property (nonatomic, readonly, nullable) GRAudioDevice *currentDevice;
/// Latency numbers of the running configuration; nil when not running.
@property (nonatomic, readonly, nullable) GRLatencyReport *latency;

// Settings. Changing the device, rate or buffer restarts the audio if it is running.
// Input channel and output routing apply immediately without a restart.

/// macOS: UID of the device to use. nil or empty picks the AXE I/O One when present,
/// otherwise the first device with both inputs and outputs. Ignored on iPadOS, where
/// the system route decides.
@property (nonatomic, copy, nullable) NSString *preferredDeviceUID;
@property (nonatomic) double preferredSampleRate;      // default 48000
@property (nonatomic) NSInteger preferredBufferFrames; // default 32
@property (nonatomic) NSInteger inputChannel;          // zero-based, default 0 (input 1)
@property (nonatomic) BOOL outputToBothChannels;       // NO: output 1 only; YES: outputs 1 and 2

/// Devices that can run full duplex. macOS: every such Core Audio device.
/// iPadOS: the current route as a single entry (empty if it has no input or output).
- (NSArray<GRAudioDevice *> *)availableDevices;

/// Starts audio, or waits for a usable device if none is there yet.
- (void)start;
/// Stops audio. The engine stays stopped until start is called again.
- (void)stop;

// Debug loopback test: connect output 1 to the selected input with a cable first.

/// Plays `pings` clicks (1 to 8) on the output and measures their return on the input.
/// Does nothing unless running and idle.
- (void)startLoopbackMeasurementWithPings:(NSInteger)pings;
@property (nonatomic, readonly) GRLoopbackPhase loopbackPhase;
/// Round trips measured so far, in frames; -1 marks a click that never came back.
- (NSArray<NSNumber *> *)loopbackResults;

@end

NS_ASSUME_NONNULL_END
