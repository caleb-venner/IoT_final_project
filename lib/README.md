# lib/

OWNER: Aida

Put the Edge Impulse **Arduino library** export here (unzip it so you get `lib/<project-name>_inferencing/`).
PlatformIO picks up anything in `lib/` automatically (only envs that include it compile it). Then include the generated header in
`src/camera/inference.cpp` (e.g. `#include <basil-wilt_inferencing.h>`) and replace the stub.
