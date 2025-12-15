
#!/usr/bin/env bash
set -euo pipefail
presets=(
  wf-gcc-debug
  wf-gcc-release
  wf-clang15-debug
  wf-clang15-release
)

for p in "${presets[@]}"; do
  echo "=== Running workflow: $p ==="
  cmake --workflow --preset "$p"
done

