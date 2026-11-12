# Vendored audio DSP dependencies

- Signalsmith Stretch 1.3.2: https://github.com/Signalsmith-Audio/signalsmith-stretch
  - Commit `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da`
  - `signalsmith-stretch.h`; local extension: optional `windowAsymmetry` on `configure()` forwards to Linear’s existing asymmetric window support (default 0 preserves upstream behaviour); MIT license in `Signalsmith-Stretch-LICENSE.txt`.
- Signalsmith Linear 0.6.6: https://github.com/Signalsmith-Audio/linear
  - Commit `7bb1c8f36794d6980080ce817c91f8da8fe56a66`
  - `fft.h` and `stft.h`, unchanged; MIT license in `signalsmith-linear/LICENSE.txt`.

Headers are vendored so building Sine Stack needs no additional network fetch.
