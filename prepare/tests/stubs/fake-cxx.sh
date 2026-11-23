#!/bin/sh
# Test stand-in for a C++ compiler driver. Compiling copies the source into the object; linking
# concatenates objects into an executable script. Environment: FAKE_CXX_SLEEP=N sleeps N seconds per
# compilation, FAKE_CXX_FAIL=1 fails compilations.
case "$1" in
  --version) echo "fakecc 1.0 (test stub)"; exit 0 ;;
  -dumpmachine) echo "x86_64-fake-linux"; exit 0 ;;
esac
compile=0; out=; src=; objs=
while [ $# -gt 0 ]; do
  case "$1" in
    -c) compile=1; src=$2; shift 2 ;;
    -o) out=$2; shift 2 ;;
    -std=*|-I*|-O*|-w|-l*) shift ;;
    *) objs="$objs $1"; shift ;;
  esac
done
if [ "$compile" = 1 ]; then
  [ -n "$FAKE_CXX_SLEEP" ] && sleep "$FAKE_CXX_SLEEP"
  [ -n "$FAKE_CXX_FAIL" ] && { echo "$src: error: fake failure" >&2; exit 1; }
  cp "$src" "$out"
else
  printf '#!/bin/sh\necho fake game "$@"\n' > "$out"
  chmod +x "$out"
fi
