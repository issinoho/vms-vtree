#!/usr/bin/env bash
# live.sh <node> [steps...] - upload, build with the native tests, run the live test
# (tests/vms/test_live.com under PTYDRIVE) and print the screens after the named
# steps (all if none). Output kept in out/live-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live.sh <node> [step...]}; shift
mkdir -p "$top/out"
"$top/tools/push.sh" "$node" > /dev/null 2>&1
"$top/tools/vms.sh" "$node" dcl '@build_vms TEST' > "$top/out/build-$node.log" 2>&1
grep -E 'checks|TEST-OK|BUILD-FAILED|%CC-' "$top/out/build-$node.log" || true
grep -q TEST-OK "$top/out/build-$node.log" || { tail -30 "$top/out/build-$node.log"; exit 1; }
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com > "$top/out/live-$node.txt" 2>&1
grep '==== privileges' "$top/out/live-$node.txt" || true
sed -n '/^---- startup/,/^---- end/p' "$top/out/live-$node.txt" > "$top/out/live-$node.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "$top/out/live-$node.pty"
