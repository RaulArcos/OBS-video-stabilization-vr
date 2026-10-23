# Smooth turns and local builds
Branch: vr-head-stabilizer. Delivery: local work-unit commits only; no push, release, or OBS installation.
Checks: standalone C++ filter tests, existing estimator harness, Python reference parity, Windows CMake build and local ZIP.

## Specs
S1: "Lets do it" — referring to smoother turn-following while preserving vibration suppression from the original request below.
S2: "Lets try it yeah" — accepts baseline crop plus limited extra dynamic zoom during large turns, with smooth return and unchanged image proportions.
S3: "everything manual, build me the things in local here" — manual GitHub Actions and local Windows artifacts, without publishing.

## Tasks
T1 [S1,S2] inline — done; RED outward-follow exit 1, GREEN filter tests/CTest, 400-frame Python parity and byte-identical baseline vibration differential; Windows build and all 11 estimator cases pass. Commit: first feature work unit (recorded below). Risk: none (internal filter behavior requested).
T2 [S3] inline — in_progress; make Actions manual and remove release automation, document and structurally test; commit pending. Risk: item 5 (CI).
T3 [S1-S3] inline + independent verification — pending; local build/package and final verification; commit pending.

## Log
L1: Original request: "This is my stabilization plugin for OBS, the thing is that it works flawlessly, but, when i Hit the limit intencionally, for example when turning my head to see the mirrors on purpose, I trigger that limmit of movement, and the movement does not look too smooth, even making the video feels like lagging, when is just the stabilizer hitting the limit of movement and following the video until my head is still again and then it just stabilices vibrations perfectly, is there any way that i could fix that behaviour without making the actual stabilization work worse?"
L2: "Lets do it, and also, i think i prefer seeing back bars rather than distorsioning the image, it makes it look weird"
L3: "of course, if we do that X% crop according to the movement %, I dont need to the black borders and i could have my image without any distorsion"
L4: "Lets try it yeah, and also fix the github actions, I dont want it doing weird things, whenever I want a release i will push it myself"
L5: "everything manual, build me the things in local here"
L6: Assumptions: dynamic zoom capped at baseline + 5 percentage points; retain baseline crop and small-motion algorithm. Exact safety zoom follows shader corner geometry. Crop must never expose edges, so zoom increases immediately when necessary and releases gradually; follow anticipates the boundary. Synthetic checks cannot prove subjective smoothness in live OBS.
L7: Exploration helper unavailable (assistant error); continued inline. Existing local CMake cache targets this checkout and Visual Studio 18 2026; default install prefix is ProgramData, so packaging will override it to a project-local directory. gentle-ai executable unavailable in shell (exit 127); do not change review mode.
L8: Forecast approximately 390 authored changed lines across two behavior units; no PR or publication requested.
L9: T1 verified: standalone filter test and CTest pass; 400-frame Python parity and baseline/current small-vibration exact stdout/stderr/exit comparison pass. VS CMake build passes; estimator synth 10 known transforms plus rejected noise pass. PATH CMake build failed (unsupported VS18 generator), corrected using cached VS CMake. Native assess unassessable before staging due to undeclared untracked files; no native review started. Live OBS visual judgement remains human-only. Build-generated counter restored to avoid unrelated source changes.
