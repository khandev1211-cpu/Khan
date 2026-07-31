# Vision + LLM + webi demo - one static binary

Real classical computer vision (`vision` package) + a real local LLM
(`llm_lib.c`, bridged to llama.cpp) + a real HTTP server (`webi` package) -
all compiled into Khan's single `khan` binary. No venv, no pip, no docker,
no system package manager involved at *run* time.

## Run it

```bash
# from the repo root, after: make LLM=1   (see docs/llm.md)
./khan examples/vision_llm_demo/app.kh
# then: curl http://localhost:8123/analyze
```

`GET /` gives a one-line HTML page. `GET /analyze` runs real vision
analysis (dimensions, brightness histogram, blob count after
thresholding) on `sample.png`, and - if a real GGUF model is present -
asks it for a one-sentence description of what those stats suggest.

## Enabling the LLM part

Without a model file, `/analyze` still returns the full, real vision
analysis - it just says so instead of a description:

```json
"llm_description": "(no model loaded - place a real .gguf at model.gguf; see README.md)"
```

To enable it: put any real GGUF file at `examples/vision_llm_demo/model.gguf`
(any small instruct model works - this only sends it one short prompt).
This wasn't tested against a real model in the sandbox this was built in
(no path to download one there - see `docs/llm.md`'s honesty note about
the same gap) - what *is* verified: `llm_load()`'s and `llm_complete()`'s
code compiles and links against real llama.cpp, and `llm_load()` on a
missing/bad file correctly fails and falls back the way this demo expects.
Test the actual generation with a real file before relying on it.

## The size/dependency comparison this demo exists for

The claim: getting vision + OCR/LLM + a web server working together in
Python usually means a venv, `pip install opencv-python
llama-cpp-python flask` (or similar), and whatever system libraries those
wheels need underneath. Khan's version is one binary, built once, that
runs anywhere with no install step.

See the chat for the actual measured numbers (binary size, venv size,
install time) - this README documents the demo itself, not a snapshot of
numbers that will drift as package versions change.
