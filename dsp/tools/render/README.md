# render

`gigrig_render` runs a WAV file through the GigRigDSP core offline and writes the result. It is used for
listening clips after each sound-producing work package and, later, for golden-render tests.

```sh
cd dsp
cmake --preset release && cmake --build --preset release
./build/release/tools/render/gigrig_render --list-params
./build/release/tools/render/gigrig_render -i di.wav -o out.wav --set input.level=6 --set output.level=-3
./build/release/tools/render/gigrig_render -i di.wav -o out.wav --preset clean.txt --block 32 --format s24
```

- The input may be 16, 24 or 32 bit PCM or 32/64 bit float, any channel count; `--channel` picks one
  (default 1). The output is mono at the input's sample rate, 32-bit float unless `--format` says otherwise.
- The core runs at the file's sample rate; there is no resampling.
- `--tail SECONDS` appends silence so delay and reverb tails are rendered.

## Preset files

Until the JSON preset format arrives in work package 15, a preset for the renderer is a text file with one
`key = value` per line, using the keys from `--list-params` (the same keys the JSON presets will use):

```
# clean.txt
input.level  = 3
output.level = -6
```

Values are clamped to each parameter's range. `--set` options are applied after the preset file.
