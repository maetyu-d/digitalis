# Digitalis

![](https://github.com/maetyu-d/digitalis/blob/main/Screenshot%202026-02-18%20at%2003.00.12.png)

Digitalis is a suite of hyper-digital audio effects built with JUCE (AU + VST3). They're a little bit Cagean, and generally microsound-inspired (Kim Cascone, Marcus Popp, Carsten Nicolai) (although those techniques are only specifically referenced in 2/10 of the plugins), but, essentially, they give you lots of new ways to mess up, dirty, and corrupt pristine audio.

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
    Diskont-era Oval-inspired melodic skipping/glitch playback with longer scratched-CD-style segment jumps.

## Build

This project uses CMake + JUCE. Build outputs are generated under `build/`, and packaged artifacts can be found in `Releases/`. They have been tested on Mac OS 13.7.7.

## Useful Reading

You don't need to read these to use the plugin suite, but the following provide some useful background to the ideas behind it:

Cascone, K., & Jandrić, P. (2021). [The Failure of Failure: Postdigital Aesthetics Against Techno-mystification](https://pmc.ncbi.nlm.nih.gov/articles/PMC7811792/). Postdigital science and education, 3(2), 566–574. https://doi.org/10.1007/s42438-020-00209-1

Cramer, F. and Jandrić, P. (2001). [Postdigital: A Term That Sucks but Is Useful](https://monoskop.org/images/e/e0/Cramer_Florian_Jandric_Petar_2021_Postdigital_A_Term_That_Sucks_but_Is_Useful.pdf). Postdigital Science and Education. https://doi.org/10.1007/s42438-021-00225-9

Menkman, R. (2011). [The Glitch Moment(um)](https://networkcultures.org/_uploads/NN%234_RosaMenkman.pdf). Network notebooks, Institute of Network Cultures.


## License

Do what you like with these, and feel free to show or credit me if you make anything cool, but I give free use of them as is, as neither gift nor curse, and without guarantee or assuming liability of any kind. Or in the more technical language of the Unlicense:

> This is free and unencumbered software released into the public domain.

> Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

> In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any and all copyright interest in the software to the public domain. We make this dedication for the benefit of the public at large and to the detriment of our heirs and successors. We intend this dedication to be an overt act of relinquishment in perpetuity of all present and future rights to this software under copyright law.

> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

> For more information, please refer to <https://unlicense.org>
