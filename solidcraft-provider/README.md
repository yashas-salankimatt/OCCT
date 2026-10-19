# solidcraft-occt-provider

A geometry provider for [SolidCraft](https://github.com/yashas-salankimatt/solidcraft) backed by
Open CASCADE Technology. When SolidCraft's own kernel cannot do a feature's Boolean, SolidCraft
can hand it to this program (SolidCraft's `docs/geometry-providers.md`). The program checks
nothing SolidCraft relies on: SolidCraft checks every result itself before keeping it.

SolidCraft never links this program or OCCT. It starts the program as a separate process and
exchanges STEP text with it, so SolidCraft's code stays MIT OR Apache-2.0, a crash here cannot
take SolidCraft down, and the browser build does not need it.

## Build

```sh
solidcraft-provider/build.sh            # OCCT (geometry, algorithms, STEP) + the provider
```

The script needs CMake and a C++17 compiler (Ninja is used when it is installed). It builds this
repository's OCCT into `../occt-build`, beside the repository, installs the provider at
`../occt-build/install/bin/solidcraft-occt-provider`, and runs its self-test:

```sh
../occt-build/install/bin/solidcraft-occt-provider --self-test
ok   union: volume 7500 (expected 7500)
ok   cut: volume 1500 (expected 1500)
ok   intersect: volume 500 (expected 500)
```

The self-test sends two boxes through the same STEP and JSON path as a request and checks each
Boolean's volume against its value worked out by hand.

## Use

```sh
SOLIDCRAFT_GEOMETRY_PROVIDER=../occt-build/install/bin/solidcraft-occt-provider solidcraft-cli mcp
SOLIDCRAFT_GEOMETRY_PROVIDER=../occt-build/install/bin/solidcraft-occt-provider solidcraft
```

`solidcraft-cli` says on standard error which provider it uses. The `geometry.providers` command
reports it as well.

## Protocol

`solidcraft-geometry-provider/1`: one JSON request line on standard input, one JSON reply line on
standard output, then the program exits.

| Request | Reply |
|---|---|
| `{"protocol": "solidcraft-geometry-provider/1", "op": "hello"}` | `{"ok": true, "provider": "occt", "version": "8.0.1", "operations": ["boolean"]}` |
| `{"protocol": …, "op": "boolean", "kind": "union" \| "cut" \| "intersect", "operands": [STEP, STEP], "tolerance_mm": t}` | `{"ok": true, "step": STEP}` or `{"ok": false, "error": "…"}` |

A Boolean is tried exactly first. If that fails, it is tried with fuzzy tolerances of `t`,
10⁻⁶ and 10⁻⁵ of the operands' size, for faces that only nearly touch. A result that is not a
valid solid is fixed with ShapeFix, and refused if it is still invalid. Anything OCCT prints goes
to standard error.

## Licence

This directory is part of this fork of Open CASCADE Technology and is distributed under the same
licence: the GNU Lesser General Public License version 2.1 with the Open CASCADE exception
(`LICENSE_LGPL_21.txt`, `OCCT_LGPL_EXCEPTION.txt`). OCCT itself is used unmodified from the pinned
release (`V8_0_1`).
