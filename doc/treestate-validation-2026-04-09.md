# TreeState validation report (2026-04-09)

## Scope
Requested checks:
1. Build modified BUCK node.
2. Start new binary on staging.
3. Run:
   - `./buck/src/buck-cli help | grep -i treestate`
   - `./buck/src/buck-cli z_gettreestate "$(./buck/src/buck-cli getbestblockhash)"`
   - `grpcurl -d '{}' wallet.buck.red:9067 cash.z.wallet.sdk.rpc.CompactTxStreamer/GetLatestTreeState`
   - `grpcurl -d '{}' wallet.buck.red:9067 cash.z.wallet.sdk.rpc.CompactTxStreamer/GetLatestBlock`
   - `grpcurl -d '{}' wallet.buck.red:9067 cash.z.wallet.sdk.rpc.CompactTxStreamer/GetLightdInfo`

## Build attempts and exact output
### Attempt A: project build script
Command:
`./zcutil/build.sh -j$(nproc)`

Output:
```
/bin/bash: line 1: ./zcutil/build.sh: Permission denied
```

After `chmod +x zcutil/build.sh`, rerun output ended with:
```
Fetching native_ccache...
...
curl: (22) The requested URL returned error: 403
...
make: *** [funcs.mk:248: /workspace/buck/depends/sources/download-stamps/.stamp_fetched-native_ccache-ccache-3.3.1.tar.bz2.hash] Error 22
make: Leaving directory '/workspace/buck/depends'
```

### Attempt B: autotools bootstrap path
Command:
`./autogen.sh`

Output:
```
Can't exec "aclocal": No such file or directory at /usr/share/autoconf/Autom4te/FileUtils.pm line 274.
autoreconf: error: aclocal failed with exit status: 2
```

### Attempt C: install missing build packages
Command:
`apt-get update && apt-get install -y automake autoconf libtool pkg-config make g++`

Output ended with:
```
E: Failed to fetch http://security.ubuntu.com/ubuntu/dists/noble-security/InRelease  403  Forbidden [IP: 172.30.0.35 8080]
E: Failed to fetch http://archive.ubuntu.com/ubuntu/dists/noble/InRelease  403  Forbidden [IP: 172.30.0.35 8080]
...
```

Conclusion: binary build could not be completed in this environment because dependency/package fetches are blocked with HTTP 403.

## Staging start attempt
Command:
`./buck/src/buckd -daemon -rpcwait -printtoconsole`

Output:
```
/bin/bash: line 1: ./buck/src/buckd: No such file or directory
```

Conclusion: staging run cannot be executed here because `buckd` was not produced by a successful build.

## Requested command outputs (exact)
Command:
`./buck/src/buck-cli help | grep -i treestate`

Output:
```
/bin/bash: line 1: ./buck/src/buck-cli: No such file or directory
```

Command:
`./buck/src/buck-cli z_gettreestate "$(./buck/src/buck-cli getbestblockhash)"`

Output:
```
/bin/bash: line 1: ./buck/src/buck-cli: No such file or directory
/bin/bash: line 1: ./buck/src/buck-cli: No such file or directory
```

Command:
`grpcurl -d '{}' wallet.buck.red:9067 cash.z.wallet.sdk.rpc.CompactTxStreamer/GetLatestTreeState`

Output:
```
/bin/bash: line 1: grpcurl: command not found
```

Command:
`grpcurl -d '{}' wallet.buck.red:9067 cash.z.wallet.sdk.rpc.CompactTxStreamer/GetLatestBlock`

Output:
```
/bin/bash: line 1: grpcurl: command not found
```

Command:
`grpcurl -d '{}' wallet.buck.red:9067 cash.z.wallet.sdk.rpc.CompactTxStreamer/GetLightdInfo`

Output:
```
/bin/bash: line 1: grpcurl: command not found
```

## Requested analysis items
- GetLatestTreeState exact error: `grpcurl: command not found` in this container.
- Triggering/root cause: grpcurl is not installed; installing tools is blocked by network/package proxy 403 responses.
- JSON sample for GetLatestTreeState: unavailable from this environment (command could not be executed).
- saplingTree semantic validation: unavailable from this environment (no RPC response body obtained).
- Index rebuild / restart order / lightwalletd restart: cannot be validated empirically here due inability to start node and inability to query lightwalletd with grpcurl in this environment.

## Acceptance criteria status in this environment
- `z_gettreestate` works from CLI: **NOT VERIFIED / BLOCKED**.
- `GetLatestTreeState` works from grpcurl: **NOT VERIFIED / BLOCKED**.
- `GetLatestBlock` still works: **NOT VERIFIED / BLOCKED**.
- No node startup regression: **NOT VERIFIED / BLOCKED**.
