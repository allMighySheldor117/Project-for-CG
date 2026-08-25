# OBJ loader fixtures

These small, hand-authored files define the CPU loader cases that automated
tests will exercise in Blueprint Step 4. Step 3 intentionally adds no test
target.

| Fixture | Intended result |
| --- | --- |
| `valid.obj` | Loads a triangle with complete normals. |
| `split-indices.obj` | Preserves independent position and normal indices. |
| `hard-edges.obj` | Duplicates shared positions when their normal indices differ. |
| `no-normals.obj` | Generates area-weighted, unit-length vertex normals. |
| `mixed-normals.obj` | Fails because normal coverage is partial. |
| `degenerate-face.obj` | Skips one collapsed face and loads the valid face with a warning. |
| `degenerate-only.obj` | Fails because no non-degenerate triangle remains. |
| `zero-extent.obj` | Fails because the referenced geometry has zero extent. |
| `malformed-indices.obj` | Fails because a position index is out of range. |
| `multiple-shapes.obj` | Combines triangles from more than one OBJ shape. |
