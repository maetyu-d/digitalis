# Digitalis

![](https://github.com/maetyu-d/digitalis/blob/main/Screenshot%202026-02-18%20at%2003.00.12.png)

Digitalis is a suite of hyper-digital audio effects built with JUCE (AU + VST3).

## Plugins

1. **FloatingPointCollapse**  
   Destroys numerical precision with float truncation, nonlinear quantization, and temporal stepping.

2. **NyquistDestroyer**  
   Treats aliasing as musical material using dynamic internal sample-rate damage and foldback behavior.

3. **BufferGlitchEngine**  
   Exposes DAW-style block artifacts with buffer seam errors, reordering, and deliberate dropouts.

4. **AutomationQuantiser**  
   Forces smooth automation into hard stepped grid motion for zipper-heavy, clocked modulation.

5. **StreamingArtifactGenerator**  
   Simulates lossy codec behavior with frame loss, smearing, and aggressive masking artifacts.

6. **FFTBrutalist**  
   Brutal spectral-domain processing with bin reduction, phase abuse, freezes, and spectral scrambling.

7. **OverclockFailure**  
   Models CPU/DSP instability with processing skips, latency spikes, clock drift, and channel desync.

8. **DeterministicMachine**  
   Pushes machine-like repetition through quantized internal states, micro-loop fixation, and hash-driven behavior.

9. **ClassicBufferStutter**  
   A classic rhythmic stutter effect with controllable slice capture, repeats, reverses, and timing jitter.

10. **MelodicSkippingEngine**  
    Diskont-era inspired melodic skipping/glitch playback with longer scratched-CD-style segment jumps.

## Build

This project uses CMake + JUCE. Build outputs are generated under `build/`, and packaged artifacts can be found in `Releases/`.
