# Initial direct Vulkan verification

Recorded 2026-09-12 for the first VulkanTron development checkpoint, based on
faithful-remaster commit `99d2f080ea2e9d3c1db693cb4f7c21354e9114a4`.
This verifies an early renderer and simulation bridge, not a complete faithful
port or a performance target.

## Build and headless checks

- Release builds both the preserved OpenGL reference and the new Vulkan program.
- All 14 inherited remaster regressions passed in this independent checkout.
- Three new checks pass in Release and with AddressSanitizer/UndefinedBehaviorSanitizer:
  `vulkantron-classic-bridge`, `vulkantron-scene`, and `vulkantron-headless-check`.
- Shader compilation and `spirv-val --target-env vulkan1.3` pass.
- Dynamic-library and symbol inspection finds Vulkan, SDL3 and libpng in the new
  executable, with no OpenGL or SDL_GPU rendering calls.
- Original production sources under `src`, `nebu`, and `lua`, and the inherited
  `art`, `data`, `music`, and `scripts` are unchanged from the fork baseline.

The bridge uses production rules/AI/events/camera code. Its focused test covers
relative turns, boost/release, repeated seeded resets, real camera modes,
1,200 queued turns, round completion, and shutdown/reinitialization. The scene
test checks the original model's vertex positions, materials/headings, trail
heights, projection conventions, finite bounds, malformed assets, and explicit
geometry limits. The integration self-test ensures drawing does not mutate the
gameplay hash.

## Native GPU checks

Machine: Linux x86-64, NVIDIA GeForce GTX 1660, driver 610.57.04, device API
1.4.341, SDL3 3.4.16, Vulkan loader/validation 1.4.357. The renderer requests
Vulkan 1.3 dynamic rendering and synchronization2. This device exposes the
optional presentation fences used by this implementation.

```sh
python3 tools/check_vulkantron_native.py \
  --executable build/release/bin/vulkantron --driver x11 \
  --output /tmp/vulkantron-check-x11
python3 tools/check_vulkantron_native.py \
  --executable build/asan/bin/vulkantron --driver wayland \
  --output /tmp/vulkantron-check-wayland
```

Each output directory must be new. The helper enables synchronization
validation with `VK_LAYER_VALIDATE_SYNC=1`, captures logs, and has a per-process
timeout. Both backend runs passed these checks:

| Check | Result |
| --- | --- |
| Overview and production follow camera | Each rendered exactly 160 frames and saved a complete PNG. |
| GPU correctness | Zero core or synchronization validation errors, including explicit shutdown. |
| Window lifecycle | Three requests issued; actual fullscreen entry/exit events and drawable-change events observed; rendering and capture continued. |
| Renderer independence | All four runs produced gameplay hash `dc180613b141086e` at the same 160 fixed steps. |
| Capture protection | An existing PNG was rejected without changing its SHA256. |
| Failure cleanup | An invalid shader failed with a clear diagnostic and no validation/cleanup errors. |
| Memory instrumentation | The native Wayland checks also ran with ASan/UBSan and reported no errors. |

The desktop tiled the requested 1280x720 window into a 1882x2052 drawable;
captures have those actual dimensions. The checks observe fullscreen and
drawable events, not exact compliance with a requested window size. Captures
were visually inspected: original cycle geometry, four player colors, trails,
floor and boundary walls are visible. Thin grid geometry still aliases at a
distance; textures, antialiasing, lighting/color parity and other presentation
work remain pending.

## Findings and limits

Synchronization validation initially caught a swapchain-acquisition/layout
transition hazard that ordinary validation had not reported. The first image
barrier now includes the color-output stage in its source execution dependency,
matching the acquire semaphore wait. Final reruns pass synchronization checks.
The program finalizes Vulkan resources before checking validation totals, so
cleanup errors cannot be hidden by an earlier successful frame count.

An overview depth-precision problem was corrected by choosing a scene-scaled
near plane for that diagnostic camera. The gameplay camera retains its classic
near-plane value. The overview is a development aid, not an original camera.

This is a conservative one-frame-in-flight renderer, not an optimized GPU
architecture. No FPS/latency improvement is claimed. Minimize/restore behavior,
physical keyboard/controller interaction, additional GPUs/platforms, and the
presentation-fence fallback path have not received native certification in
this checkpoint. Whole-program leak freedom is not claimed; traced native runs
disable LeakSanitizer. The isolated bridge and scene tests also passed separate
untraced LeakSanitizer runs.

The [development plan](VULKANTRON_PLAN.md) lists the remaining faithful-port
milestones. Inherited OpenGL release evidence does not certify Vulkan parity.
