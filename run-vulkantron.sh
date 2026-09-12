#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Optional user-local SDK fallback; a caller-provided VULKAN_SDK takes precedence.
if [ -z "${VULKAN_SDK:-}" ] && [ -f "$HOME/.local/opt/vulkan-headers/1.4.357.0/include/vulkan/vulkan.h" ]; then
  VULKAN_SDK="$HOME/.local/opt/vulkan-headers/1.4.357.0"
  export VULKAN_SDK
fi
cmake --preset release -S "$project_dir"
cmake --build "$project_dir/build/release" --target vulkantron
exec "$project_dir/build/release/bin/vulkantron" "$@"
