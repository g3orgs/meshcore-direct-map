# Contributing

Build and test on Linux with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 tools/package_plugin.py
```

Please report the OnAir version, operating system, plugin version, and the relevant plugin log lines when filing a bug. If a report needs a packet example, remove message text, coordinates, identifiers, and other private data before posting it publicly. A minimal fictional or redacted sample is preferred.

The current package targets Linux x86-64. Contributions for other platforms need platform-specific socket and location handling plus tests. A zero-hop path is only a direct-reception candidate; changes must preserve that wording.
