# Measured round-trip latency

Work package 1 result. Measured on 2026-11-23 by Krzysztof with the app's loopback test
(debug builds, Settings): a short cable from output 1 to input 1 of the IK AXE I/O One,
five clicks per run, all five identical in every run. "Reported" is what the OS and the
driver claim (buffers, device and stream latency, safety offsets); "measured" is the real
round trip from the loopback cable. The DSP core adds 0 samples.

| Device | Rate | Buffer | Reported | Measured | Under 5 ms |
| --- | --- | --- | --- | --- | --- |
| Mac mini M4, macOS 27 | 48 kHz | 32 | 2.50 ms (120 samples) | 5.58 ms (268 samples) | no |
| Mac mini M4, macOS 27 | 96 kHz | 32 | 1.75 ms (168 samples) | **2.58 ms** (248 samples) | yes |
| Mac mini M4, macOS 27 | 96 kHz | 64 | 2.42 ms (232 samples) | 3.25 ms (312 samples) | yes |
| iPad Pro 11 M2, iPadOS 27 | 48 kHz | 32 | 3.92 ms (188 samples) | 7.90 ms (379 samples) | no |
| iPad Pro 11 M2, iPadOS 27 | 96 kHz | 32 | 3.21 ms (308 samples) | **4.53 ms** (435 samples) | yes |
| iPad Pro 11 M2, iPadOS 27 | 96 kHz | 64 | 3.88 ms (372 samples) | 5.20 ms (499 samples) | no |

## What this means

- Both systems keep latency they do not report: about 3 ms on the Mac and 4 ms on the iPad
  at 48 kHz. It sits in the USB path and the converters, outside the app. It is roughly
  fixed in samples, so doubling the sample rate almost halves it in time.
- **96 kHz with a 32-sample buffer is the default on both devices**, the only setting that
  meets the 5 ms target on both. 64 samples at 96 kHz is the fallback on the Mac.
- Consequence for the DSP work packages: the CPU budget (plan section 2, full chain under
  25% of one core) must hold at **96 kHz / 32 samples**, about twice the work of 48 kHz.
  Oversampling factors can be lower at 96 kHz for the same aliasing.
